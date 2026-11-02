#include "tools/log_monitor/model.h"
#include "common/scoped_handle.h"
#include "common/version.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <limits>
#include <stdexcept>
#include <gsl/gsl>
#include <system_error>

namespace qiqi::monitor {
namespace {
constexpr bool Contains(std::wstring_view value, std::wstring_view term) noexcept {
    return std::search(value.begin(), value.end(), term.begin(), term.end(),
        [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); }) != value.end();
}

std::string Escape(std::wstring_view text) {
    std::string utf8;
    if (!text.empty()) {
        const int length = gsl::narrow<int>(text.size());
        const int count = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
        if (!count) throw std::system_error(gsl::narrow_cast<int>(::GetLastError()), std::system_category(), "Export UTF-8 conversion");
        utf8.resize(gsl::narrow_cast<size_t>(count));
        if (!::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, utf8.data(), count, nullptr, nullptr)) {
            throw std::system_error(gsl::narrow_cast<int>(::GetLastError()), std::system_category(), "Export UTF-8 conversion");
        }
    }
    std::string result;
    for (const char c : utf8) {
        switch (c) {
        case '\0': result += "\\0"; break;
        case '\t': result += "\\t"; break;
        case '\r': result += "\\r"; break;
        case '\n': result += "\\n"; break;
        case '\\': result += "\\\\"; break;
        default: result += c; break;
        }
    }
    return result;
}
} // namespace

size_t Event::Bytes() const noexcept {
    return sizeof(Event) + sizeof(wchar_t) * (process.capacity() + name.capacity() + message.capacity());
}

bool Filter::Matches(const Event& event) const noexcept {
    return (!pid || event.pid == *pid) && (!level || event.level == level) &&
           (process.empty() || Contains(event.process, process)) &&
           (text.empty() || Contains(event.message, text));
}

#pragma warning(suppress : 26455) // Allocation and invalid-limit failures are intentionally reported by callers.
EventBuffer::EventBuffer(size_t max_events, size_t max_bytes)
    : max_events_(max_events), max_bytes_(max_bytes) {
    if (max_events == 0 || max_bytes < sizeof(Event)) {
        throw std::invalid_argument("Event buffer limits must be positive");
    }
}

bool EventBuffer::Push(Event event) {
    const size_t bytes = event.Bytes();
    if (bytes > max_bytes_) {
        ++evicted_;
        return false;
    }
    while (!events_.empty() && (events_.size() >= max_events_ || bytes_ > max_bytes_ - bytes)) {
        bytes_ -= events_.front().Bytes();
        events_.pop_front();
        ++evicted_;
    }
    event.id = next_id_;
    events_.push_back(std::move(event));
    ++next_id_;
    bytes_ += bytes;
    return true;
}

void EventBuffer::Clear() noexcept {
    events_.clear();
    bytes_ = 0;
}

const Event* EventBuffer::Find(uint64_t id) const noexcept {
    if (events_.empty() || id < events_.front().id || id > events_.back().id) return nullptr;
    return &gsl::at(events_, gsl::narrow_cast<size_t>(id - events_.front().id));
}

void EventQueue::Push(Event event) {
    const std::lock_guard lock(mutex_);
    buffer_.Push(std::move(event));
}

std::vector<Event> EventQueue::Take() {
    const std::lock_guard lock(mutex_);
    std::vector<Event> result(buffer_.Events().begin(), buffer_.Events().end());
    buffer_.Clear();
    return result;
}

uint64_t EventQueue::Evicted() const {
    const std::lock_guard lock(mutex_);
    return buffer_.Evicted();
}

std::optional<DWORD> ParsePid(const std::wstring& text) noexcept {
    if (text.empty()) return std::nullopt;
    uint64_t pid = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') return std::nullopt;
        pid = pid * 10 + gsl::narrow_cast<unsigned int>(c - L'0');
        if (pid > std::numeric_limits<DWORD>::max()) return std::nullopt;
    }
    return gsl::narrow_cast<DWORD>(pid);
}

std::wstring LevelName(UCHAR level) {
    switch (level) {
    case 1: return L"Critical";
    case 2: return L"Error";
    case 3: return L"Warning";
    case 4: return L"Info";
    case 5: return L"Verbose";
    default: return std::format(L"Level {}", level);
    }
}

std::wstring FormatTime(uint64_t time) {
    const FILETIME utc{gsl::narrow_cast<DWORD>(time), gsl::narrow_cast<DWORD>(time >> 32)};
    SYSTEMTIME system{};
    if (!::FileTimeToSystemTime(&utc, &system)) {
        throw std::runtime_error("FileTimeToSystemTime failed");
    }
    return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}Z", system.wYear, system.wMonth,
        system.wDay, system.wHour, system.wMinute, system.wSecond, system.wMilliseconds);
}

std::wstring FormatEventEntry(const Event& event, std::wstring_view level_text) {
    const std::wstring default_level = level_text.empty() ? LevelName(event.level) : std::wstring{};
    const std::wstring_view level = level_text.empty() ? std::wstring_view(default_level) : level_text;
    auto text = std::format(L"{} | {} | PID={} TID={} | {} | {}\r\n{}",
        FormatTime(event.time), event.process, event.pid, event.tid,
        level, event.name, event.message);
    std::replace(text.begin(), text.end(), L'\0', L'\x2400');
    return text;
}

std::wstring FormatEventsForClipboard(std::span<const Event> events) {
    std::wstring result;
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) result += L"\r\n";
        const auto& event = gsl::at(events, i);
        auto message = event.message;
        std::replace(message.begin(), message.end(), L'\0', L'\x2400');
        result += std::format(L"{}\t{}\tPID={}\tTID={}\t{}\t{}\t{}",
            FormatTime(event.time), event.process, event.pid, event.tid,
            LevelName(event.level), event.name, message);
    }
    return result;
}

std::string ExportEvents(const std::vector<Event>& events, std::wstring_view source) {
    std::string result = std::format("# QiQiIME Log Monitor {} ({})\r\n# Source: {}\r\n",
        version::kFullVersion, version::kArch, Escape(source));
    result += "TimeUTC\tProcess\tPID\tTID\tLevel\tEvent\tMessage\r\n";
    for (const auto& event : events) {
        result += std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\r\n", Escape(FormatTime(event.time)),
            Escape(event.process), event.pid, event.tid, Escape(LevelName(event.level)),
            Escape(event.name), Escape(event.message));
    }
    return result;
}

ULONG WriteExport(const wchar_t* path, std::string_view bytes) noexcept {
    if (bytes.size() > MAXDWORD) return ERROR_FILE_TOO_LARGE;
    const ScopedFileHandle file(::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr));
    if (!file) return ::GetLastError();
    if (::GetFileType(file.Get()) != FILE_TYPE_DISK) return ERROR_INVALID_NAME;
    DWORD written = 0;
    if (!::WriteFile(file.Get(), bytes.data(), gsl::narrow_cast<DWORD>(bytes.size()), &written, nullptr)) {
        return ::GetLastError();
    }
    if (written != bytes.size()) return ERROR_WRITE_FAULT;
    return ::FlushFileBuffers(file.Get()) ? ERROR_SUCCESS : ::GetLastError();
}
} // namespace qiqi::monitor
