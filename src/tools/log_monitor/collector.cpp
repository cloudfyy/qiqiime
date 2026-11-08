#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/permissions.h"
#include "common/etw_registration.h"
#include "common/logger.h"
#include "common/version.h"

#include <TraceLoggingProvider.h>
#include <tdh.h>
#include <winmeta.h>
#include <gsl/gsl>
#include <array>
#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <system_error>
#include <span>

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(g_monitor_provider, "QiQiIME.LogMonitor",
    (0x6ab897b4, 0xd148, 0x4df9, 0x8e, 0xb7, 0x47, 0x90, 0xa2, 0x74, 0x95, 0x1e));
#pragma warning(pop)

namespace qiqi::monitor {
namespace {
ULONG RegisterDiagnostics() noexcept { return ::TraceLoggingRegister(g_monitor_provider); }
void UnregisterDiagnostics() noexcept { ::TraceLoggingUnregister(g_monitor_provider); }
constinit log::detail::EtwRegistration diagnostic_registration{RegisterDiagnostics, UnregisterDiagnostics};

struct TraceProperties {
    EVENT_TRACE_PROPERTIES properties{};
    std::array<wchar_t, 128> name{};
    TraceProperties() noexcept {
        properties.Wnode.BufferSize = sizeof(*this);
        properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties.Wnode.ClientContext = 2;
        properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        properties.BufferSize = 64;
        properties.MinimumBuffers = 4;
        properties.MaximumBuffers = 32;
        properties.FlushTimer = 1;
        properties.LoggerNameOffset = offsetof(TraceProperties, name);
    }
};

ULONG ReadString(EVENT_RECORD& record, const wchar_t* name, USHORT type, std::wstring& result) {
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name);
    descriptor.ArrayIndex = std::numeric_limits<ULONG>::max();
    ULONG size = 0;
    ULONG error = ::TdhGetPropertySize(&record, 0, nullptr, 1, &descriptor, &size);
    if (error != ERROR_SUCCESS) return error;
    if (size > 65536) return ERROR_INVALID_DATA;
    std::vector<BYTE> data(size);
    error = ::TdhGetProperty(&record, 0, nullptr, 1, &descriptor, size, data.data());
    if (error != ERROR_SUCCESS) return error;
    std::span<const BYTE> bytes(data);
    const bool counted = type == TDH_INTYPE_COUNTEDANSISTRING || type == TDH_INTYPE_COUNTEDSTRING;
    if (counted) {
        if (bytes.size() < sizeof(USHORT)) return ERROR_INVALID_DATA;
        USHORT count = 0;
        std::memcpy(&count, bytes.data(), sizeof(count));
        bytes = bytes.subspan(sizeof(count));
        if (count > bytes.size()) return ERROR_INVALID_DATA;
        bytes = bytes.first(count);
    }
    const size_t length = bytes.size();
    if (type == TDH_INTYPE_UNICODESTRING || type == TDH_INTYPE_COUNTEDSTRING) {
        if (length % sizeof(wchar_t) != 0) return ERROR_INVALID_DATA;
        result.resize(length / sizeof(wchar_t));
        if (length) std::memcpy(result.data(), bytes.data(), length);
    } else if (type == TDH_INTYPE_ANSISTRING || type == TDH_INTYPE_COUNTEDANSISTRING) {
        if (length == 0) { result.clear(); return ERROR_SUCCESS; }
        const auto* text = reinterpret_cast<const char*>(bytes.data());
        const int chars = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
            gsl::narrow_cast<int>(length), nullptr, 0);
        if (!chars) return ::GetLastError();
        result.resize(gsl::narrow_cast<size_t>(chars));
        if (!::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                gsl::narrow_cast<int>(length), result.data(), chars)) return ::GetLastError();
    } else {
        return ERROR_NOT_SUPPORTED;
    }
    if (!counted && !result.empty() && result.back() == L'\0') result.pop_back();
    return ERROR_SUCCESS;
}

std::optional<std::wstring_view> MetadataString(std::span<const BYTE> storage, ULONG offset) noexcept {
    if (offset >= storage.size() || offset % sizeof(wchar_t)) return std::nullopt;
    const auto remainder = storage.subspan(offset);
    const std::span<const wchar_t> text(reinterpret_cast<const wchar_t*>(remainder.data()), remainder.size() / sizeof(wchar_t));
    const auto end = std::find(text.begin(), text.end(), L'\0');
    if (end == text.end()) return std::nullopt;
    return std::wstring_view(text.begin(), end);
}
} // namespace

#pragma warning(suppress : 26455) // Initialization allocates; failure reaches the GUI startup error handler.
Diagnostics::Diagnostics() {
    const ULONG error = diagnostic_registration.EnsureRegistered();
    if (error) Record(2, L"TraceLoggingRegister(QiQiIME.LogMonitor)", error);
    Record(4, std::format(L"Monitor initialized: {} ({})", version::kFullVersionW, version::kArchW));
}
Diagnostics::~Diagnostics() noexcept = default;

void Diagnostics::Record(UCHAR level, std::wstring_view operation, ULONG error) noexcept {
    try {
        const std::wstring message = std::format(L"{} (error={}, 0x{:08X})", operation, error, error);
        if (diagnostic_registration.IsRegistered()) {
#pragma warning(push)
#pragma warning(disable : 26447 26812)
            if (level <= 2) {
                TraceLoggingWrite(g_monitor_provider, "Error", TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                    TraceLoggingWideString(message.c_str(), "message"), TraceLoggingUInt32(error, "error"));
            } else if (level == 3) {
                TraceLoggingWrite(g_monitor_provider, "Warning", TraceLoggingLevel(WINEVENT_LEVEL_WARNING),
                    TraceLoggingWideString(message.c_str(), "message"), TraceLoggingUInt32(error, "error"));
            } else {
                TraceLoggingWrite(g_monitor_provider, "Info", TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                    TraceLoggingWideString(message.c_str(), "message"), TraceLoggingUInt32(error, "error"));
            }
#pragma warning(pop)
        }
        FILETIME time{};
        ::GetSystemTimeAsFileTime(&time);
        Event event;
        event.time = (gsl::narrow_cast<uint64_t>(time.dwHighDateTime) << 32) + gsl::narrow_cast<uint64_t>(time.dwLowDateTime);
        event.pid = ::GetCurrentProcessId();
        event.tid = ::GetCurrentThreadId();
        event.level = level;
        event.process = L"QiQiIMELogMonitor.exe";
        event.name = L"Diagnostic";
        event.message = message;
        queue.Push(std::move(event));
    } catch (const std::exception&) {
        ++allocation_errors;
        ::OutputDebugStringW(L"QiQiIME Log Monitor: diagnostic formatting/buffering failed\n");
    }
}

ULONG DecodeEvent(EVENT_RECORD& record, Event& event) {
    if (!::IsEqualGUID(record.EventHeader.ProviderId, kImeProvider)) return ERROR_NOT_FOUND;
    ULONG size = 0;
    ULONG error = ::TdhGetEventInformation(&record, 0, nullptr, nullptr, &size);
    if (error != ERROR_INSUFFICIENT_BUFFER) return error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
    if (size < sizeof(TRACE_EVENT_INFO) || size > 1024 * 1024) return ERROR_INVALID_DATA;
    std::vector<BYTE> storage(size);
    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(storage.data());
    error = ::TdhGetEventInformation(&record, 0, nullptr, info, &size);
    if (error != ERROR_SUCCESS) return error;
    if (info->TopLevelPropertyCount >
        (storage.size() - offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray)) / sizeof(EVENT_PROPERTY_INFO)) {
        return ERROR_INVALID_DATA;
    }
    event.time = gsl::narrow_cast<uint64_t>(record.EventHeader.TimeStamp.QuadPart);
    event.pid = record.EventHeader.ProcessId;
    event.tid = record.EventHeader.ThreadId;
    event.level = record.EventHeader.EventDescriptor.Level;
    event.opcode = record.EventHeader.EventDescriptor.Opcode;
    event.keyword = record.EventHeader.EventDescriptor.Keyword != 0
        ? record.EventHeader.EventDescriptor.Keyword
        : std::to_underlying(log::Keyword::General);
    event.activity_id = record.EventHeader.ActivityId;
    event.related_activity_id = GUID_NULL;
    if (record.ExtendedData != nullptr && record.ExtendedDataCount > 0) {
        const std::span<const EVENT_HEADER_EXTENDED_DATA_ITEM> ext_items(
            record.ExtendedData, record.ExtendedDataCount);
        for (const auto& item : ext_items) {
            if (item.ExtType == EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID &&
                item.DataSize == sizeof(EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID) &&
                item.DataPtr != 0) {
                const auto* rel = reinterpret_cast<const EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID*>(item.DataPtr);
                event.related_activity_id = rel->RelatedActivityId;
            }
        }
    }
    event.name = L"TraceLogging";
    if (info->EventNameOffset) {
        const auto name = MetadataString(storage, info->EventNameOffset);
        if (!name) return ERROR_INVALID_DATA;
        event.name = *name;
    }
    bool process = false;
    bool message = false;
    const std::span<const EVENT_PROPERTY_INFO> properties(std::data(info->EventPropertyInfoArray), info->TopLevelPropertyCount);
    for (const auto& property : properties) {
        const auto property_name = MetadataString(storage, property.NameOffset);
        if (!property_name) return ERROR_INVALID_DATA;
        if (property.Flags & PropertyStruct) return ERROR_NOT_SUPPORTED;
        if (*property_name == L"process") {
            error = ReadString(record, property_name->data(), property.nonStructType.InType, event.process);
            process = error == ERROR_SUCCESS;
        } else if (*property_name == L"message") {
            error = ReadString(record, property_name->data(), property.nonStructType.InType, event.message);
            message = error == ERROR_SUCCESS;
        }
        if (error != ERROR_SUCCESS) return error;
    }
    return process && message ? ERROR_SUCCESS : ERROR_NOT_FOUND;
}

Collector::~Collector() noexcept { Stop(); }

bool Collector::FailStart(std::wstring_view operation, ULONG error) noexcept {
    start_failure_ = CaptureStartFailure{operation, error};
    diagnostics_.Record(2, operation, error);
    return false;
}

bool Collector::Start() {
    start_failure_.reset();
    if (Running()) return true;
    if (!Stop()) return false;
    CapturePermissions permissions;
    const ULONG permission_error = QueryCapturePermissions(permissions);
    if (permission_error != ERROR_SUCCESS) {
        diagnostics_.Record(3, L"Query capture token permissions; attempting ETW access directly", permission_error);
    } else {
        diagnostics_.Record(4, std::format(
            L"Capture permissions: elevated={}, Performance Log Users enabled={}; ETW calls determine access",
            permissions.elevated, permissions.performance_log_user));
    }
    static std::atomic<uint64_t> sequence{0};
    name_ = std::format(L"QiQiIME.LogMonitor.{}.{}.{}", ::GetCurrentProcessId(),
        ::GetTickCount64(), ++sequence);
    TraceProperties properties;
    ULONG error = api_.start_trace(&session_, name_.c_str(), &properties.properties);
    if (error != ERROR_SUCCESS) {
        session_ = 0;
        return FailStart(L"StartTraceW", error);
    }
    bool started = false;
    const auto rollback = gsl::finally([this, &started]() noexcept {
        if (!started) Stop();
    });
    lost_baseline_ = events_lost.load();
    buffers_baseline_ = buffers_lost.load();
    error = api_.enable_trace(session_, &kImeProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_VERBOSE, std::to_underlying(log::Keyword::Logs), 0, 1000, nullptr);
    if (error != ERROR_SUCCESS) {
        return FailStart(L"EnableTraceEx2(QiQiIME.TraceLogging)", error);
    }
#pragma warning(push)
#pragma warning(disable : 26476) // ETW chooses EventRecordCallback through ProcessTraceMode.
    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = name_.data();
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = &OnEvent;
    logfile.Context = this;
#pragma warning(pop)
    reader_ = api_.open_trace(&logfile);
    if (reader_ == INVALID_PROCESSTRACE_HANDLE) {
        error = ::GetLastError();
        return FailStart(L"OpenTraceW", error);
    }
    running_ = true;
    try {
        worker_ = std::thread([this] { Consume(); });
    } catch (const std::system_error& failure) {
        diagnostics_.Record(2, L"Create trace consumer thread", gsl::narrow_cast<ULONG>(failure.code().value()));
        return false;
    }
    diagnostics_.Record(4, std::format(L"Capture started: {}, QiQiIME.TraceLogging, all ETW levels", name_));
    started = true;
    return true;
}

void Collector::Consume() noexcept {
    const ULONG error = ::ProcessTrace(&reader_, 1, nullptr, nullptr);
    if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) diagnostics_.Record(2, L"ProcessTrace", error);
    running_ = false;
}

void Collector::PollLoss() noexcept {
    if (!session_) return;
    TraceProperties properties;
    const ULONG error = ::ControlTraceW(session_, name_.c_str(), &properties.properties, EVENT_TRACE_CONTROL_QUERY);
    if (error != ERROR_SUCCESS) {
        diagnostics_.Record(3, L"ControlTrace(QUERY)", error);
        return;
    }
    events_lost = lost_baseline_ + properties.properties.EventsLost;
    buffers_lost = buffers_baseline_ + properties.properties.RealTimeBuffersLost;
}

bool Collector::Stop() noexcept {
    if (session_) {
        TraceProperties properties;
        const ULONG error = ::ControlTraceW(session_, name_.c_str(), &properties.properties, EVENT_TRACE_CONTROL_STOP);
        if (error != ERROR_SUCCESS && error != ERROR_WMI_INSTANCE_NOT_FOUND) {
            diagnostics_.Record(2, L"ControlTrace(STOP own session); retry Stop before restarting", error);
        } else {
            if (error == ERROR_SUCCESS) {
                events_lost = lost_baseline_ + properties.properties.EventsLost;
                buffers_lost = buffers_baseline_ + properties.properties.RealTimeBuffersLost;
            }
            session_ = 0;
            diagnostics_.Record(4, L"Capture stopped");
        }
    }
    if (reader_ != INVALID_PROCESSTRACE_HANDLE) {
        const ULONG error = ::CloseTrace(reader_);
        if (error != ERROR_SUCCESS && error != ERROR_CTX_CLOSE_PENDING) diagnostics_.Record(2, L"CloseTrace", error);
    }
    // Lifecycle methods run on the GUI thread, never the consumer; joinable rules out invalid joins.
#pragma warning(suppress : 26447)
    if (worker_.joinable()) worker_.join();
    reader_ = INVALID_PROCESSTRACE_HANDLE;
    running_ = false;
    return session_ == 0;
}

void WINAPI Collector::OnEvent(EVENT_RECORD* record) noexcept {
    if (!record || !record->UserContext || !::IsEqualGUID(record->EventHeader.ProviderId, kImeProvider)) return;
    auto& collector = *static_cast<Collector*>(record->UserContext);
    try {
        Event event;
        const ULONG error = DecodeEvent(*record, event);
        if (error != ERROR_SUCCESS) {
            const uint64_t count = ++collector.decode_errors;
            if (count == 1 || count % 1024 == 0) collector.diagnostics_.Record(3, L"TDH decode (aggregated)", error);
            return;
        }
        collector.queue_.Push(std::move(event));
    } catch (const std::exception&) {
        ++collector.allocation_errors;
        collector.diagnostics_.Record(2, L"Trace event allocation/decoding exception", ERROR_NOT_ENOUGH_MEMORY);
    }
}
} // namespace qiqi::monitor
