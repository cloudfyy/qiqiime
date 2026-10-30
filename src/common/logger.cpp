#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <gsl/gsl>

#include <array>
#include <atomic>
#include <cstdlib>
#include <format>
#include <mutex>

// Windows ETW TraceLogging Provider for QiQiIME: {8E7B9D31-5A42-4C81-9F11-202610290099}
// Works inside UWP/AppContainer/Low-Integrity sandboxes without filesystem permissions and has zero cost when not enabled.
#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_etw_provider,
    "QiQiIME.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99));
#pragma warning(pop)

namespace qiqi::log {

namespace {

std::atomic<Level> g_log_level{Level::info};

// RAII guard for the Windows ETW TraceLogging provider registration.
class ScopedEtwProvider final {
public:
    ScopedEtwProvider() noexcept { EnsureRegistered(); }
    ~ScopedEtwProvider() noexcept { Unregister(); }

    ScopedEtwProvider(const ScopedEtwProvider&) = delete;
    ScopedEtwProvider& operator=(const ScopedEtwProvider&) = delete;
    ScopedEtwProvider(ScopedEtwProvider&&) = delete;
    ScopedEtwProvider& operator=(ScopedEtwProvider&&) = delete;

    void EnsureRegistered() noexcept {
        if (!registered_.exchange(true)) {
            if (FAILED(::TraceLoggingRegister(g_qiqi_etw_provider))) {
                registered_ = false;
            }
        }
    }

    void Unregister() noexcept {
        if (registered_.exchange(false)) {
            ::TraceLoggingUnregister(g_qiqi_etw_provider);
        }
    }

    bool IsRegistered() const noexcept { return registered_.load(); }

private:
    std::atomic<bool> registered_{false};
};

ScopedEtwProvider& GetEtwProvider() noexcept {
    static ScopedEtwProvider provider;
    return provider;
}

std::string_view GetProcessName() noexcept {
    static const std::string kProcName = []() noexcept -> std::string {
        try {
            std::string name = WideToUtf8(GetModuleFilePath(nullptr).filename().wstring());
            if (!name.empty()) {
                return name;
            }
        } catch (...) {
        }
        return "qiqiime";
    }();
    return kProcName;
}

constexpr std::string_view LevelTag(Level level) noexcept {
    switch (level) {
        case Level::trace:    return "trace";
        case Level::debug:    return "debug";
        case Level::info:     return "info";
        case Level::warn:     return "warning";
        case Level::err:      return "error";
        case Level::critical: return "critical";
        case Level::off:
        default:              return "off";
    }
}

#pragma warning(push)
#pragma warning(disable : 26447 26812)
void EmitEtwTraceEvent(Level level, std::string_view proc_name, std::string_view message) noexcept {
    const int msg_len = gsl::narrow_cast<int>(message.size());
    const int proc_len = gsl::narrow_cast<int>(proc_name.size());
    switch (level) {
        case Level::trace:
        case Level::debug:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogVerbose",
                TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::info:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogInfo",
                TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::warn:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogWarning",
                TraceLoggingLevel(WINEVENT_LEVEL_WARNING),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::err:
        case Level::critical:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogError",
                TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::off:
        default:
            break;
    }
}
#pragma warning(pop)

} // namespace

void InitLogger(Level level) noexcept {
    g_log_level = level;
    GetEtwProvider().EnsureRegistered();
}

Level GetLevel() noexcept {
    return g_log_level.load();
}

void SetLevel(Level level) noexcept {
    g_log_level = level;
}

bool IsEtwProviderRegistered() noexcept {
    auto& provider = GetEtwProvider();
    provider.EnsureRegistered();
    return provider.IsRegistered();
}

bool IsLogSinkActive() noexcept {
    return (::TraceLoggingProviderEnabled(g_qiqi_etw_provider, 0, 0) != FALSE) ||
           (::IsDebuggerPresent() != FALSE);
}

void WriteLogMessage(Level level, std::string_view message) noexcept {
    if (level < GetLevel() || level == Level::off) {
        return;
    }
    auto& provider = GetEtwProvider();
    provider.EnsureRegistered();
    const std::string_view proc_name = GetProcessName();

    // 1. Emit to Windows ETW TraceLogging (100% lock-free kernel write, zero heap allocation)
    EmitEtwTraceEvent(level, proc_name, message);

    // 2. Output to debugger only when attached (avoids kernel exception overhead in normal typing)
    if (::IsDebuggerPresent() != FALSE) {
        try {
            const std::string line = std::format("[{}] [{}] {}\r\n", proc_name, LevelTag(level), message);
            const std::wstring wide_line = Utf8ToWide(line);
            ::OutputDebugStringW(wide_line.c_str());
        } catch (...) {
        }
    }
}

void Shutdown() noexcept {
    GetEtwProvider().Unregister();
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(gsl::narrow_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(gsl::narrow_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), result.data(), len);
    return result;
}

std::filesystem::path GetModuleFilePath(void* module_handle) noexcept {
    try {
        std::array<wchar_t, MAX_PATH> module_path{};
        if (::GetModuleFileNameW(
                static_cast<HMODULE>(module_handle),
                module_path.data(),
                gsl::narrow_cast<DWORD>(module_path.size())) == 0) {
            return {};
        }
        return std::filesystem::path(module_path.data());
    } catch (...) {
        return {};
    }
}

std::filesystem::path GetAppDataDirectory() {
    std::array<wchar_t, MAX_PATH> env_buf{};
    const DWORD env_len = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", env_buf.data(), gsl::narrow_cast<DWORD>(env_buf.size()));
    std::filesystem::path base_dir;
    if (env_len > 0 && env_len < env_buf.size()) {
        base_dir = env_buf.data();
    } else {
        std::array<wchar_t, MAX_PATH> temp_path{};
        if (::GetTempPathW(gsl::narrow_cast<DWORD>(temp_path.size()), temp_path.data()) > 0) {
            base_dir = temp_path.data();
        }
    }
    if (base_dir.empty()) {
        return {};
    }
    return base_dir / L"QiQiIME";
}

bool ReadFileToUtf8String(const std::filesystem::path& file_path, std::string& out_content) {
    // Open with least privilege: read-only access, read-only sharing, anonymous SQOS to prevent pipe impersonation,
    // and verify the target is a regular disk file.
    const ScopedFileHandle file(::CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
        nullptr));
    if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
        return false;
    }

    LARGE_INTEGER file_size{};
    constexpr LONGLONG kMaxAllowedReadBytes = 64LL * 1024LL * 1024LL; // 64 MB safety ceiling
    if (!::GetFileSizeEx(file.Get(), &file_size) ||
        file_size.QuadPart < 0 ||
        file_size.QuadPart > kMaxAllowedReadBytes) {
        return false;
    }

    out_content.clear();
    if (file_size.QuadPart == 0) {
        return true;
    }

    out_content.resize(gsl::narrow_cast<size_t>(file_size.QuadPart));
    DWORD total_read = 0;
    const DWORD to_read = gsl::narrow_cast<DWORD>(file_size.QuadPart);
    if (!::ReadFile(file.Get(), out_content.data(), to_read, &total_read, nullptr) ||
        total_read != to_read) {
        out_content.clear();
        return false;
    }
    return true;
}

bool WriteFileAtomically(const std::filesystem::path& target_path, std::span<const uint8_t> bytes) {
    try {
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            return false;
        }
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path(), ec);
        }

        const std::filesystem::path tmp_path =
            target_path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId());
        bool moved_ok = false;
        const auto cleanup_tmp = gsl::finally([&tmp_path, &moved_ok]() noexcept {
            if (!moved_ok) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
            }
        });

        {
            const ScopedFileHandle file(::CreateFileW(
                tmp_path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                nullptr));
            if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                return false;
            }

            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(bytes.size());
            if (to_write > 0 &&
                (!::WriteFile(file.Get(), bytes.data(), to_write, &written, nullptr) || written != to_write)) {
                return false;
            }
        }

        moved_ok = (::MoveFileExW(tmp_path.c_str(), target_path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE);
        return moved_ok;
    } catch (...) {
        return false;
    }
}

} // namespace qiqi::log
