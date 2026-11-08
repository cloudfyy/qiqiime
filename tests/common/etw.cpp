#include "common/metric.h"
#include "etw.h"
#include "common/logger.h"
#include "common/version.h"

#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

namespace qiqi::test {
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckStatus(ULONG error, const char* operation) {
    if (error != ERROR_SUCCESS) {
        std::cerr << operation << " failed (error=" << error << ")\n";
        throw std::runtime_error(operation);
    }
}

void RemoveTestPath(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) std::fprintf(stderr, "Metrics test cleanup failed (error=%d)\n", error.value());
}

struct TraceProperties {
    EVENT_TRACE_PROPERTIES properties{};
    std::array<wchar_t, 1024> file{};
    std::array<wchar_t, 128> name{};
    TraceProperties() noexcept {
        properties.Wnode.BufferSize = sizeof(*this);
        properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties.Wnode.ClientContext = 1;
        properties.LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL |
            EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC;
        properties.BufferSize = 64;
        properties.MinimumBuffers = 2;
        properties.MaximumBuffers = 8;
        properties.MaximumFileSize = 64;
        properties.LogFileNameOffset = offsetof(TraceProperties, file);
        properties.LoggerNameOffset = offsetof(TraceProperties, name);
    }
};

std::optional<std::wstring_view> MetadataText(std::span<const BYTE> bytes, ULONG offset) noexcept {
    if (offset >= bytes.size() || offset % sizeof(wchar_t)) return std::nullopt;
    const auto tail = bytes.subspan(offset);
    const std::span<const wchar_t> text(reinterpret_cast<const wchar_t*>(tail.data()), tail.size() / sizeof(wchar_t));
    const auto end = std::find(text.begin(), text.end(), L'\0');
    if (end == text.end()) return std::nullopt;
    return std::wstring_view(text.begin(), end);
}

ULONG ReadField(EVENT_RECORD& record, const TRACE_EVENT_INFO& info, std::span<const BYTE> metadata,
    const wchar_t* name, USHORT type, std::span<BYTE> destination, bool string = false) {
    const auto fields = metadata.subspan(offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray));
    const std::span<const EVENT_PROPERTY_INFO> properties(
        reinterpret_cast<const EVENT_PROPERTY_INFO*>(fields.data()), info.TopLevelPropertyCount);
    const auto property = std::find_if(properties.begin(), properties.end(), [&](const auto& item) {
        const auto text = MetadataText(metadata, item.NameOffset);
        return text && *text == name;
    });
    if (property == properties.end()) return ERROR_NOT_FOUND;
    if (property->Flags != 0 || property->nonStructType.InType != type) return ERROR_INVALID_DATA;
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name);
    descriptor.ArrayIndex = (std::numeric_limits<ULONG>::max)();
    ULONG size = 0;
    ULONG error = ::TdhGetPropertySize(&record, 0, nullptr, 1, &descriptor, &size);
    if (error != ERROR_SUCCESS) return error;
    if ((!string && size != destination.size()) || size == 0 || size > destination.size()) return ERROR_INVALID_DATA;
    error = ::TdhGetProperty(&record, 0, nullptr, 1, &descriptor, size, destination.data());
    if (error == ERROR_SUCCESS && string && gsl::at(destination, size - 1) != 0) return ERROR_INVALID_DATA;
    return error;
}

template <typename T>
ULONG ReadField(EVENT_RECORD& record, const TRACE_EVENT_INFO& info, std::span<const BYTE> metadata,
    const wchar_t* name, USHORT type, T& value) {
    const std::span<BYTE> bytes(reinterpret_cast<BYTE*>(&value), sizeof(value));
    return ReadField(record, info, metadata, name, type, bytes);
}

struct Instance {
    uint16_t schema = 0;
    GUID id{};
    std::array<char, 128> version{};
    std::array<char, 16> architecture{};
    uint64_t generation = 0;
    metrics::DiagnosticCounts diagnostics;
};
struct Capture {
    std::vector<metrics::Event> events;
    std::vector<Instance> instances;
    ULONG error = ERROR_SUCCESS;
    MetricEventObserver observer = nullptr;
    bool retain_events = true;
};

ULONG DecodeMetric(EVENT_RECORD& record, const TRACE_EVENT_INFO& info, std::span<const BYTE> metadata,
    metrics::Event& event) {
    event.activity_id = record.EventHeader.ActivityId;
    if (record.ExtendedData) {
        const std::span<const EVENT_HEADER_EXTENDED_DATA_ITEM> extended(record.ExtendedData, record.ExtendedDataCount);
        for (const auto& item : extended) {
            if (item.ExtType == EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID && item.DataSize == sizeof(GUID) && item.DataPtr) {
                std::memcpy(&event.related_activity_id, reinterpret_cast<const void*>(item.DataPtr), sizeof(GUID));
            }
        }
    }
    ULONG error = ERROR_SUCCESS;
    const auto read = [&]<typename T>(const wchar_t* name, USHORT type, T& value) {
        if (error == ERROR_SUCCESS) error = ReadField(record, info, metadata, name, type, value);
    };
    read(L"schema_version", TDH_INTYPE_UINT16, event.schema_version);
    read(L"instance_id", TDH_INTYPE_GUID, event.instance_id);
    read(L"capture_generation", TDH_INTYPE_UINT64, event.capture_generation);
    read(L"service_id", TDH_INTYPE_UINT64, event.service_id);
    read(L"operation_id", TDH_INTYPE_UINT64, event.operation_id);
    read(L"parent_operation_id", TDH_INTYPE_UINT64, event.parent_operation_id);
    read(L"metric", TDH_INTYPE_UINT8, event.metric);
    read(L"phase", TDH_INTYPE_UINT8, event.phase);
    read(L"outcome", TDH_INTYPE_UINT8, event.outcome);
    read(L"error_domain", TDH_INTYPE_UINT8, event.error_domain);
    read(L"error_code", TDH_INTYPE_UINT32, event.error_code);
    read(L"mode", TDH_INTYPE_UINT8, event.mode);
    read(L"source", TDH_INTYPE_UINT8, event.source);
    if (event.phase != metrics::Phase::Begin) {
        uint64_t duration = 0;
        read(L"duration_ns", TDH_INTYPE_UINT64, duration);
        event.duration_ns = duration;
    }
    if (error != ERROR_SUCCESS) return error;
    if (info.TopLevelPropertyCount != (event.phase == metrics::Phase::Begin ? 13u : 14u)) return ERROR_INVALID_DATA;
    return metrics::Validate(event) == metrics::ValidationError::None ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

ULONG DecodeInstance(EVENT_RECORD& record, const TRACE_EVENT_INFO& info, std::span<const BYTE> metadata,
    Instance& instance) {
    ULONG error = ReadField(record, info, metadata, L"schema_version", TDH_INTYPE_UINT16, instance.schema);
    if (!error) error = ReadField(record, info, metadata, L"instance_id", TDH_INTYPE_GUID, instance.id);
    if (!error) error = ReadField(record, info, metadata, L"capture_generation", TDH_INTYPE_UINT64, instance.generation);
    if (!error) error = ReadField(record, info, metadata, L"protocol_errors", TDH_INTYPE_UINT64, instance.diagnostics.protocol);
    if (!error) error = ReadField(record, info, metadata, L"clock_errors", TDH_INTYPE_UINT64, instance.diagnostics.clock);
    if (!error) error = ReadField(record, info, metadata, L"identity_errors", TDH_INTYPE_UINT64, instance.diagnostics.identity);
    if (!error) error = ReadField(record, info, metadata, L"write_errors", TDH_INTYPE_UINT64, instance.diagnostics.write);
    if (!error) error = ReadField(record, info, metadata, L"interrupted_samples", TDH_INTYPE_UINT64, instance.diagnostics.interrupted);
    if (!error) error = ReadField(record, info, metadata, L"build_version", TDH_INTYPE_ANSISTRING,
        std::span<BYTE>(reinterpret_cast<BYTE*>(instance.version.data()), instance.version.size()), true);
    if (!error) error = ReadField(record, info, metadata, L"architecture", TDH_INTYPE_ANSISTRING,
        std::span<BYTE>(reinterpret_cast<BYTE*>(instance.architecture.data()), instance.architecture.size()), true);
    if (!error && (info.TopLevelPropertyCount != 10 || instance.schema != metrics::kSchemaVersion ||
        instance.id == GUID_NULL)) error = ERROR_INVALID_DATA;
    return error;
}

void WINAPI OnEvent(EVENT_RECORD* record) noexcept {
    if (!record || record->EventHeader.ProviderId != log::kProviderId) return;
    auto* capture = static_cast<Capture*>(record->UserContext);
    if (!capture || capture->error != ERROR_SUCCESS) return;
    try {
        if (record->EventHeader.ProcessId != ::GetCurrentProcessId() ||
            record->EventHeader.EventDescriptor.Keyword != std::to_underlying(log::Keyword::Metrics) ||
            record->EventHeader.EventDescriptor.Level != 0) {
            capture->error = ERROR_INVALID_DATA;
            return;
        }
        ULONG size = 0;
        ULONG error = ::TdhGetEventInformation(record, 0, nullptr, nullptr, &size);
        if (error != ERROR_INSUFFICIENT_BUFFER || size < sizeof(TRACE_EVENT_INFO) || size > 65536) {
            capture->error = error != ERROR_SUCCESS ? error : ERROR_INVALID_DATA;
            return;
        }
        std::vector<BYTE> metadata(size);
        const gsl::not_null<TRACE_EVENT_INFO*> info(reinterpret_cast<TRACE_EVENT_INFO*>(metadata.data()));
        error = ::TdhGetEventInformation(record, 0, nullptr, info, &size);
        if (error != ERROR_SUCCESS) { capture->error = error; return; }
        if (info->TopLevelPropertyCount >
            (metadata.size() - offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray)) / sizeof(EVENT_PROPERTY_INFO)) {
            capture->error = ERROR_INVALID_DATA;
            return;
        }
        const auto name = MetadataText(metadata, info->EventNameOffset);
        if (name && *name == L"QiQiMetric") {
            metrics::Event event;
            error = DecodeMetric(*record, *info, metadata, event);
            if (!error && capture->retain_events) {
                if (capture->events.size() < 64) capture->events.push_back(event);
                else error = ERROR_BUFFER_OVERFLOW;
            }
        } else if (name && *name == L"QiQiMetricInstance") {
            Instance instance;
            error = DecodeInstance(*record, *info, metadata, instance);
            if (!error && capture->instances.size() < 16) capture->instances.push_back(instance);
            else if (!error) error = ERROR_BUFFER_OVERFLOW;
        } else {
            error = ERROR_NOT_SUPPORTED;
        }
        if (error == ERROR_SUCCESS && capture->observer) error = capture->observer(*record);
        capture->error = error;
    } catch (const std::bad_alloc&) {
        capture->error = ERROR_NOT_ENOUGH_MEMORY;
    }
}

int CaptureMetrics(MetricEventObserver observer, MetricCaptureAction action) {
    Check(!log::IsEtwProviderRegistered(), "Metrics query must not register the provider");
    const auto directory = std::filesystem::temp_directory_path() /
        std::format(L"qiqi-metrics-{}-{}", ::GetCurrentProcessId(), ::GetTickCount64());
    Check(std::filesystem::create_directory(directory), "Create metrics test directory");
    const auto file = directory / L"capture.etl";
    const auto cleanup_files = gsl::finally([&]() noexcept {
        RemoveTestPath(file);
        RemoveTestPath(directory);
    });
    const auto name = std::format(L"QiQiIME.Metrics.Test.{}.{}", ::GetCurrentProcessId(), ::GetTickCount64());
    TraceProperties properties;
    Check(log::EnsureInitialized(), "Register metrics provider");
    const auto shutdown = gsl::finally([]() noexcept { log::Shutdown(); });
    properties.properties.Wnode.Guid = log::CreateActivityId();
    Check(wcscpy_s(properties.name.data(), properties.name.size(), name.c_str()) == 0, "Trace name");
    Check(wcscpy_s(properties.file.data(), properties.file.size(), file.c_str()) == 0, "Trace file");
    TRACEHANDLE session = 0;
    const ULONG start_error = ::StartTraceW(&session, name.c_str(), &properties.properties);
    if (start_error == ERROR_ACCESS_DENIED || start_error == ERROR_PRIVILEGE_NOT_HELD) {
        std::cerr << "[SKIP] Metrics ETW access denied (error=" << start_error << ")\n";
        return 77;
    }
    CheckStatus(start_error, "StartTraceW");
    bool stopped = false;
    const auto stop_session = gsl::finally([&]() noexcept {
        if (!stopped) {
            TraceProperties stop;
            stop.properties.Wnode.Guid = properties.properties.Wnode.Guid;
            stop.properties.LoggerNameOffset = 0;
            stop.properties.LogFileNameOffset = 0;
            const ULONG error = ::ControlTraceW(session, name.c_str(), &stop.properties, EVENT_TRACE_CONTROL_STOP);
            if (error) std::cerr << "Metrics session cleanup failed (error=" << error << ")\n";
        }
    });
    log::SetLevel(log::Level::off);
    const auto enable = [&](uint64_t keyword, ULONG control = EVENT_CONTROL_CODE_ENABLE_PROVIDER) {
        CheckStatus(::EnableTraceEx2(session, &log::kProviderId, control, TRACE_LEVEL_VERBOSE,
            keyword, 0, 1000, nullptr), "EnableTraceEx2");
    };
    enable(std::to_underlying(log::Keyword::Logs));
    Check(!log::MetricsEnabled(), "Ordinary log capture must not enable metrics");
    enable(std::to_underlying(log::Keyword::Metrics));
    Check(log::MetricsEnabled() && !log::ShouldLog(log::Level::info), "Metrics must be independent of LogLevel");
    const auto instance = metrics::ModuleInstance();
    metrics::Event event;
    if (action) {
        action();
    } else {
        log::SetLevel(log::Level::info);
        Check(!log::ShouldLog(log::Level::info, log::Keyword::Metrics), "Metrics keyword is reserved for typed events");
        log::WriteLogMessage(log::Level::info, "forbidden-free-text", log::LogContext{log::Keyword::Metrics});
        Check(metrics::Diagnostics().protocol == 1, "Reject free text under the Metrics keyword");
        log::SetLevel(log::Level::off);
        Check(SUCCEEDED(instance.error), "Metric instance identity");
        const auto service = metrics::NextServiceId();
        GUID activity_before{};
        CheckStatus(::EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, &activity_before), "Read ambient activity");
        for (const auto outcome : {metrics::Outcome::Success, metrics::Outcome::Failure, metrics::Outcome::Incomplete}) {
            metrics::Scope scope(metrics::Metric::FocusTotal, service);
            if (outcome == metrics::Outcome::Failure) scope.SetOutcome(outcome,
                metrics::ErrorDomain::HResult, gsl::narrow_cast<uint32_t>(E_ACCESSDENIED));
            else if (outcome == metrics::Outcome::Success) scope.SetOutcome(outcome);
        }
        event.instance_id = instance.id;
        event.capture_generation = log::MetricsCaptureGeneration();
        event.activity_id = log::CreateActivityId();
        event.related_activity_id = log::CreateActivityId();
        event.service_id = service;
        event.operation_id = metrics::NextOperationId();
        event.phase = metrics::Phase::Begin;
        event.outcome = metrics::Outcome::Unspecified;
        metrics::Emit(event);
        event.phase = metrics::Phase::End;
        event.outcome = metrics::Outcome::Cancelled;
        event.duration_ns = 1'000;
        metrics::Emit(event);
        const auto generation = log::MetricsCaptureGeneration();
        enable(std::to_underlying(log::Keyword::Metrics), EVENT_CONTROL_CODE_CAPTURE_STATE);
        Check(log::MetricsCaptureGeneration() == generation, "Capture-state metadata must not interrupt scopes");
        {
            const metrics::Scope interrupted(metrics::Metric::FocusTotal, service);
            enable(0, EVENT_CONTROL_CODE_DISABLE_PROVIDER);
            Check(!log::MetricsEnabled(), "Metrics disabled");
            {
                const metrics::Scope disabled(metrics::Metric::FocusTotal, service);
                Check(!disabled.Active(), "Disabled scope");
            }
            enable(std::to_underlying(log::Keyword::Metrics));
        }
        Check(metrics::Diagnostics().interrupted == 1, "Interrupted scope must not complete in the next capture");
        metrics::Emit(event);
        Check(metrics::Diagnostics().interrupted == 2, "Stale explicit end must not enter the next capture");
        log::Shutdown();
        Check(log::EnsureInitialized(), "Provider re-registration");
        Check(metrics::ModuleInstance().id == instance.id, "Registration is not module reload");
        {
            metrics::Scope scope(metrics::Metric::FocusTotal, service);
            scope.SetOutcome(metrics::Outcome::Success);
        }
        GUID activity_after{};
        CheckStatus(::EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, &activity_after), "Read ambient activity");
        Check(activity_before == activity_after, "Metrics must not modify the implicit activity ID");
    }
    TraceProperties stop;
    stop.properties.Wnode.Guid = properties.properties.Wnode.Guid;
    stop.properties.LoggerNameOffset = 0;
    stop.properties.LogFileNameOffset = 0;
    CheckStatus(::ControlTraceW(session, name.c_str(), &stop.properties, EVENT_TRACE_CONTROL_STOP), "Stop metrics session");
    stopped = true;
    Check(stop.properties.EventsLost == 0 && stop.properties.LogBuffersLost == 0, "Loss-free test capture");
    std::wstring filename = file.wstring();
    Capture capture;
    capture.observer = observer;
    capture.retain_events = action == nullptr;
#pragma warning(suppress : 26476) // ETW selects its callback union through ProcessTraceMode.
    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LogFileName = filename.data();
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = OnEvent;
    logfile.Context = &capture;
    TRACEHANDLE reader = ::OpenTraceW(&logfile);
    if (reader == INVALID_PROCESSTRACE_HANDLE) CheckStatus(::GetLastError(), "OpenTraceW");
    const auto close_reader = gsl::finally([reader]() noexcept {
        const ULONG error = ::CloseTrace(reader);
        if (error) std::cerr << "CloseTrace failed (error=" << error << ")\n";
    });
    CheckStatus(::ProcessTrace(&reader, 1, nullptr, nullptr), "ProcessTrace");
    CheckStatus(capture.error, "Decode typed metric fields");
    if (action) return 0;
    if (capture.events.size() != 6 || capture.instances.size() != 4) {
        std::cerr << "Observed metric events=" << capture.events.size()
                  << ", instance metadata events=" << capture.instances.size() << "\n";
    }
    Check(capture.events.size() == 6 && capture.instances.size() == 4, "Exact metric and metadata event counts");
    for (const auto& metadata : capture.instances) {
        Check(metadata.id == instance.id && std::string_view(metadata.version.data()) == version::kFullVersion &&
            std::string_view(metadata.architecture.data()) == version::kArch, "Instance metadata");
    }
    Check(gsl::at(capture.instances, 0).generation == gsl::at(capture.instances, 1).generation &&
        gsl::at(capture.instances, 2).generation > gsl::at(capture.instances, 1).generation &&
        gsl::at(capture.instances, 3).generation > gsl::at(capture.instances, 2).generation, "Metadata generations");
    Check(gsl::at(capture.instances, 0).diagnostics.interrupted == 0 &&
        gsl::at(capture.instances, 3).diagnostics.interrupted == 2 &&
        gsl::at(capture.instances, 3).diagnostics.protocol == 1, "Module-lifetime diagnostic snapshots");
    Check(gsl::at(capture.events, 0).outcome == metrics::Outcome::Success &&
        gsl::at(capture.events, 1).outcome == metrics::Outcome::Failure &&
        gsl::at(capture.events, 1).error_code == gsl::narrow_cast<uint32_t>(E_ACCESSDENIED) &&
        gsl::at(capture.events, 2).outcome == metrics::Outcome::Incomplete, "Explicit scope outcomes");
    Check(gsl::at(capture.events, 0).capture_generation == gsl::at(capture.instances, 0).generation &&
        gsl::at(capture.events, 5).capture_generation == gsl::at(capture.instances, 3).generation, "Sample entry generations");
    Check(gsl::at(capture.events, 3).phase == metrics::Phase::Begin &&
        !gsl::at(capture.events, 3).duration_ns &&
        gsl::at(capture.events, 4).phase == metrics::Phase::End &&
        gsl::at(capture.events, 4).duration_ns == 1'000 &&
        gsl::at(capture.events, 3).operation_id == gsl::at(capture.events, 4).operation_id &&
        gsl::at(capture.events, 3).activity_id == event.activity_id &&
        gsl::at(capture.events, 4).related_activity_id == event.related_activity_id, "Async protocol fields");
    const auto diagnostics = metrics::Diagnostics();
    Check(diagnostics.clock == 0 && diagnostics.identity == 0 && diagnostics.protocol == 1 && diagnostics.write == 0,
        "Metrics capture must have no suppressed timing, identity, protocol or write errors");
    std::cout << "[PASS] Real typed ETW metrics and capture-state metadata.\n";
    return 0;
}
} // namespace

int RunMetricsEtwTest(MetricEventObserver observer, MetricCaptureAction action) {
    try {
        return CaptureMetrics(observer, action);
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << "\n";
        return 1;
    }
}
} // namespace qiqi::test
