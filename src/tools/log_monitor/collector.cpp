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

struct MetricField {
    std::wstring_view name;
    USHORT type;
};
ULONG ReadEventMetadata(EVENT_RECORD& record, std::vector<BYTE>& storage, ULONG maximum_size,
    const MetricDecodeApi& api = {}) {
    ULONG size = 0;
    ULONG error = api.information(&record, 0, nullptr, nullptr, &size);
    if (error != ERROR_INSUFFICIENT_BUFFER) return error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
    if (size < sizeof(TRACE_EVENT_INFO) || size > maximum_size) return ERROR_INVALID_DATA;
    storage.resize(size);
    const gsl::not_null<TRACE_EVENT_INFO*> info(reinterpret_cast<TRACE_EVENT_INFO*>(storage.data()));
    error = api.information(&record, 0, nullptr, info, &size);
    if (error != ERROR_SUCCESS) return error;
    if (size > storage.size() || size < sizeof(TRACE_EVENT_INFO)) return ERROR_INVALID_DATA;
    storage.resize(size);
    if (info->TopLevelPropertyCount >
        (storage.size() - offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray)) / sizeof(EVENT_PROPERTY_INFO)) {
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}
constexpr std::array<MetricField, 14> kMetricFields{{
    MetricField{L"schema_version", TDH_INTYPE_UINT16}, {L"instance_id", TDH_INTYPE_GUID},
    {L"capture_generation", TDH_INTYPE_UINT64}, {L"service_id", TDH_INTYPE_UINT64},
    {L"operation_id", TDH_INTYPE_UINT64}, {L"parent_operation_id", TDH_INTYPE_UINT64},
    {L"metric", TDH_INTYPE_UINT8}, {L"phase", TDH_INTYPE_UINT8}, {L"outcome", TDH_INTYPE_UINT8},
    {L"error_domain", TDH_INTYPE_UINT8}, {L"error_code", TDH_INTYPE_UINT32},
    {L"mode", TDH_INTYPE_UINT8}, {L"source", TDH_INTYPE_UINT8}, {L"duration_ns", TDH_INTYPE_UINT64}
}};
constexpr std::array<MetricField, 10> kInstanceFields{{
    MetricField{L"schema_version", TDH_INTYPE_UINT16}, {L"instance_id", TDH_INTYPE_GUID},
    {L"build_version", TDH_INTYPE_ANSISTRING}, {L"architecture", TDH_INTYPE_ANSISTRING},
    {L"capture_generation", TDH_INTYPE_UINT64}, {L"protocol_errors", TDH_INTYPE_UINT64},
    {L"clock_errors", TDH_INTYPE_UINT64}, {L"identity_errors", TDH_INTYPE_UINT64},
    {L"write_errors", TDH_INTYPE_UINT64}, {L"interrupted_samples", TDH_INTYPE_UINT64}
}};

ULONG ValidateMetricFields(const TRACE_EVENT_INFO& info, std::span<const BYTE> storage,
    std::span<const MetricField> expected) noexcept {
    if (info.TopLevelPropertyCount != expected.size() || info.PropertyCount != expected.size()) return ERROR_INVALID_DATA;
    std::array<bool, kMetricFields.size()> seen{};
    const auto fields = storage.subspan(offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray));
    const std::span<const EVENT_PROPERTY_INFO> properties(
        reinterpret_cast<const EVENT_PROPERTY_INFO*>(fields.data()), info.TopLevelPropertyCount);
    for (const auto& property : properties) {
        const auto name = MetadataString(storage, property.NameOffset);
        if (!name || property.Flags != 0) return ERROR_INVALID_DATA;
        const auto match = std::find_if(expected.begin(), expected.end(),
            [&](const MetricField& field) noexcept { return field.name == *name; });
        if (match == expected.end() || property.nonStructType.InType != match->type ||
            property.nonStructType.MapNameOffset != 0) return ERROR_INVALID_DATA;
        const size_t index = gsl::narrow_cast<size_t>(match - expected.begin());
        if (gsl::at(seen, index)) return ERROR_INVALID_DATA;
        gsl::at(seen, index) = true;
    }
    return ERROR_SUCCESS;
}

ULONG ReadMetricField(EVENT_RECORD& record, const wchar_t* name, std::span<BYTE> destination,
    bool string, const MetricDecodeApi& api) {
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name);
    descriptor.ArrayIndex = (std::numeric_limits<ULONG>::max)();
    ULONG size = 0;
    ULONG error = api.property_size(&record, 0, nullptr, 1, &descriptor, &size);
    if (error != ERROR_SUCCESS) return error;
    if (size == 0 || size > destination.size() || (!string && size != destination.size())) return ERROR_INVALID_DATA;
    error = api.property(&record, 0, nullptr, 1, &descriptor, size, destination.data());
    if (error != ERROR_SUCCESS) return error;
    if (string && (gsl::at(destination, size - 1) != 0 ||
        std::find(destination.begin(), destination.begin() + size - 1, BYTE{0}) != destination.begin() + size - 1)) {
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
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
    if (record.EventHeader.EventDescriptor.Keyword & std::to_underlying(log::Keyword::Metrics)) return ERROR_NOT_FOUND;
    std::vector<BYTE> storage;
    ULONG error = ReadEventMetadata(record, storage, 1024 * 1024);
    if (error != ERROR_SUCCESS) return error;
    const gsl::not_null<TRACE_EVENT_INFO*> info(reinterpret_cast<TRACE_EVENT_INFO*>(storage.data()));
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

ULONG DecodeMetricEvent(EVENT_RECORD& record, uint64_t capture_generation, MetricRecord& event, MetricDecodeApi api) {
    if (record.EventHeader.ProviderId != log::kProviderId) return ERROR_NOT_FOUND;
    constexpr uint64_t provider_keywords = 0x0000FFFFFFFFFFFFULL;
    if ((record.EventHeader.EventDescriptor.Keyword & provider_keywords) != std::to_underlying(log::Keyword::Metrics) ||
        record.EventHeader.EventDescriptor.Level != WINEVENT_LEVEL_LOG_ALWAYS ||
        record.EventHeader.EventDescriptor.Opcode != WINEVENT_OPCODE_INFO || capture_generation == 0) {
        return ERROR_INVALID_DATA;
    }
    std::vector<BYTE> storage;
    ULONG error = ReadEventMetadata(record, storage, 65536, api);
    if (error != ERROR_SUCCESS) return error;
    const gsl::not_null<TRACE_EVENT_INFO*> info(reinterpret_cast<TRACE_EVENT_INFO*>(storage.data()));
    const auto name = MetadataString(storage, info->EventNameOffset);
    if (!name || (*name != L"QiQiMetric" && *name != L"QiQiMetricInstance")) return ERROR_NOT_SUPPORTED;
    MetricRecord result;
    result.capture_generation = capture_generation;
    result.pid = record.EventHeader.ProcessId;
    result.tid = record.EventHeader.ThreadId;
    result.time = gsl::narrow_cast<uint64_t>(record.EventHeader.TimeStamp.QuadPart);
    result.received_tick_ms = ::GetTickCount64();
    const auto read = [&]<typename T>(const wchar_t* field, T& value, bool string = false) {
        if (error == ERROR_SUCCESS) {
            error = ReadMetricField(record, field, std::span<BYTE>(reinterpret_cast<BYTE*>(&value), sizeof(value)), string, api);
        }
    };
    if (*name == L"QiQiMetricInstance") {
        error = ValidateMetricFields(*info, storage, kInstanceFields);
        MetricInstance instance;
        read(L"schema_version", instance.schema_version);
        read(L"instance_id", instance.instance_id);
        read(L"build_version", instance.build_version, true);
        read(L"architecture", instance.architecture, true);
        read(L"capture_generation", instance.capture_generation);
        read(L"protocol_errors", instance.diagnostics.protocol);
        read(L"clock_errors", instance.diagnostics.clock);
        read(L"identity_errors", instance.diagnostics.identity);
        read(L"write_errors", instance.diagnostics.write);
        read(L"interrupted_samples", instance.diagnostics.interrupted);
        if (!error && instance.schema_version != metrics::kSchemaVersion) error = ERROR_REVISION_MISMATCH;
        result.payload = instance;
    } else {
        if (info->TopLevelPropertyCount != 13 && info->TopLevelPropertyCount != 14) return ERROR_INVALID_DATA;
        error = ValidateMetricFields(*info, storage, std::span(kMetricFields).first(info->TopLevelPropertyCount));
        metrics::Event metric;
        metric.activity_id = record.EventHeader.ActivityId;
        if (record.ExtendedDataCount && !record.ExtendedData) return ERROR_INVALID_DATA;
        const std::span<const EVENT_HEADER_EXTENDED_DATA_ITEM> extended(record.ExtendedData, record.ExtendedDataCount);
        bool related = false;
        for (const auto& item : extended) {
            if (item.ExtType != EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID) continue;
            if (related || item.DataSize != sizeof(GUID) || !item.DataPtr) return ERROR_INVALID_DATA;
            std::memcpy(&metric.related_activity_id, reinterpret_cast<const void*>(item.DataPtr), sizeof(GUID));
            related = true;
        }
        read(L"schema_version", metric.schema_version);
        read(L"instance_id", metric.instance_id);
        read(L"capture_generation", metric.capture_generation);
        read(L"service_id", metric.service_id);
        read(L"operation_id", metric.operation_id);
        read(L"parent_operation_id", metric.parent_operation_id);
        read(L"metric", metric.metric);
        read(L"phase", metric.phase);
        read(L"outcome", metric.outcome);
        read(L"error_domain", metric.error_domain);
        read(L"error_code", metric.error_code);
        read(L"mode", metric.mode);
        read(L"source", metric.source);
        if (info->TopLevelPropertyCount == 14) {
            uint64_t duration = 0;
            read(L"duration_ns", duration);
            metric.duration_ns = duration;
        }
        if (!error && metric.schema_version != metrics::kSchemaVersion) error = ERROR_REVISION_MISMATCH;
        result.payload = metric;
    }
    if (error != ERROR_SUCCESS) return error;
    if (!ValidMetricRecord(result)) return ERROR_INVALID_DATA;
    event = result;
    return ERROR_SUCCESS;
}

Collector::~Collector() noexcept { Stop(); }

bool Collector::FailStart(std::wstring_view operation, ULONG error) noexcept {
    start_failure_ = CaptureStartFailure{operation, error};
    diagnostics_.Record(2, operation, error);
    return false;
}

bool Collector::Start(bool collect_metrics) {
    start_failure_.reset();
    if (Running()) return collect_metrics == collect_metrics_ ||
        FailStart(L"Change metric capture requires Stop", ERROR_INVALID_STATE);
    if (!Stop()) return false;
    collect_metrics_ = false;
    if (collect_metrics && metric_generation_ == (std::numeric_limits<uint64_t>::max)()) {
        return FailStart(L"Metric capture generation exhausted", ERROR_ARITHMETIC_OVERFLOW);
    }
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
        TRACE_LEVEL_VERBOSE, std::to_underlying(collect_metrics ? log::Keyword::All : log::Keyword::Logs), 0, 1000, nullptr);
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
    collect_metrics_ = collect_metrics;
    if (collect_metrics_) {
        error = metric_queue.BeginCapture(++metric_generation_);
        if (error != ERROR_SUCCESS) return FailStart(L"Begin metric capture", error);
    }
    running_ = true;
    try {
        worker_ = std::thread([this] { Consume(); });
    } catch (const std::system_error& failure) {
        diagnostics_.Record(2, L"Create trace consumer thread", gsl::narrow_cast<ULONG>(failure.code().value()));
        if (collect_metrics_) metric_queue.RecordCaptureError();
        return false;
    }
    if (collect_metrics_) {
        error = api_.enable_trace(session_, &kImeProvider, EVENT_CONTROL_CODE_CAPTURE_STATE,
            TRACE_LEVEL_VERBOSE, std::to_underlying(log::Keyword::All), 0, 1000, nullptr);
        if (error != ERROR_SUCCESS) {
            metric_queue.RecordCaptureError();
            diagnostics_.Record(3, L"Request metric instance metadata after OpenTraceW", error);
        }
    }
    diagnostics_.Record(4, std::format(L"Capture started: {}, QiQiIME.TraceLogging, all ETW levels", name_));
    started = true;
    return true;
}

void Collector::Consume() noexcept {
    const ULONG error = ::ProcessTrace(&reader_, 1, nullptr, nullptr);
    if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) diagnostics_.Record(2, L"ProcessTrace", error);
    if (collect_metrics_) {
        if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) metric_queue.RecordCaptureError();
        metric_queue.EndCapture();
    }
    running_ = false;
}

void Collector::PollLoss() noexcept {
    if (!session_) return;
    TraceProperties properties;
    const ULONG error = ::ControlTraceW(session_, name_.c_str(), &properties.properties, EVENT_TRACE_CONTROL_QUERY);
    if (error != ERROR_SUCCESS) {
        diagnostics_.Record(3, L"ControlTrace(QUERY)", error);
        if (collect_metrics_) metric_queue.RecordCaptureError();
        return;
    }
    events_lost = lost_baseline_ + properties.properties.EventsLost;
    buffers_lost = buffers_baseline_ + properties.properties.RealTimeBuffersLost;
    if (collect_metrics_) metric_queue.RecordLoss(properties.properties.EventsLost, properties.properties.RealTimeBuffersLost);
}

bool Collector::Stop() noexcept {
    if (session_) {
        TraceProperties properties;
        const ULONG error = ::ControlTraceW(session_, name_.c_str(), &properties.properties, EVENT_TRACE_CONTROL_STOP);
        if (error != ERROR_SUCCESS && error != ERROR_WMI_INSTANCE_NOT_FOUND) {
            diagnostics_.Record(2, L"ControlTrace(STOP own session); retry Stop before restarting", error);
            if (collect_metrics_) metric_queue.RecordCaptureError();
        } else {
            if (error == ERROR_SUCCESS) {
                events_lost = lost_baseline_ + properties.properties.EventsLost;
                buffers_lost = buffers_baseline_ + properties.properties.RealTimeBuffersLost;
                if (collect_metrics_) metric_queue.RecordLoss(properties.properties.EventsLost, properties.properties.RealTimeBuffersLost);
            }
            session_ = 0;
            diagnostics_.Record(4, L"Capture stopped");
        }
    }
    if (reader_ != INVALID_PROCESSTRACE_HANDLE) {
        const ULONG error = ::CloseTrace(reader_);
        if (error != ERROR_SUCCESS && error != ERROR_CTX_CLOSE_PENDING) {
            diagnostics_.Record(2, L"CloseTrace", error);
            if (collect_metrics_) metric_queue.RecordCaptureError();
        }
    }
    // Lifecycle methods run on the GUI thread, never the consumer; joinable rules out invalid joins.
#pragma warning(suppress : 26447)
    if (worker_.joinable()) worker_.join();
    reader_ = INVALID_PROCESSTRACE_HANDLE;
    running_ = false;
    if (collect_metrics_) metric_queue.EndCapture();
    return session_ == 0;
}

void WINAPI Collector::OnEvent(EVENT_RECORD* record) noexcept {
    if (!record || !record->UserContext || !::IsEqualGUID(record->EventHeader.ProviderId, kImeProvider)) return;
    auto& collector = *static_cast<Collector*>(record->UserContext);
    const bool metric = (record->EventHeader.EventDescriptor.Keyword & std::to_underlying(log::Keyword::Metrics)) != 0;
    try {
        if (metric) {
            if (!collector.collect_metrics_) return;
            MetricRecord event;
            ULONG error = DecodeMetricEvent(*record, collector.metric_generation_, event);
            if (error != ERROR_SUCCESS) collector.metric_queue.RecordDecodeError();
            else error = collector.metric_queue.Push(event);
            if (error != ERROR_SUCCESS) {
                const auto status = collector.metric_queue.Status();
                const uint64_t count = status.protocol_errors + status.generation_errors +
                    status.queue_dropped + status.source_limit_errors + status.allocation_errors;
                if (count == 1 || count % 1024 == 0) {
                    collector.diagnostics_.Record(3, L"Metric decode/admission (aggregated)", error);
                }
            }
            return;
        }
        Event event;
        const ULONG error = DecodeEvent(*record, event);
        if (error != ERROR_SUCCESS) {
            const uint64_t count = ++collector.decode_errors;
            if (count == 1 || count % 1024 == 0) collector.diagnostics_.Record(3, L"TDH decode (aggregated)", error);
            return;
        }
        collector.queue_.Push(std::move(event));
    } catch (const std::bad_alloc&) {
        if (metric) collector.metric_queue.RecordAllocationError();
        else ++collector.allocation_errors;
        collector.diagnostics_.Record(2, metric ? L"Metric allocation/decoding exception" :
            L"Trace event allocation/decoding exception", ERROR_NOT_ENOUGH_MEMORY);
    } catch (const std::system_error& error) {
        if (metric) collector.metric_queue.RecordCaptureError();
        else ++collector.decode_errors;
        collector.diagnostics_.Record(2, L"Trace queue synchronization system error",
            gsl::narrow_cast<ULONG>(error.code().value()));
    }
}
} // namespace qiqi::monitor
