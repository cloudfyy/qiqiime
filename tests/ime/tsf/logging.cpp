// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "common/com_object.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/etw.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/aggregate.h"
#include "common/scoped_handle.h"
#include "mocks.h"

#include <evntrace.h>
#include <tdh.h>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace qiqi::test {
namespace {

constexpr GUID kProvider = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99}
};

void CheckEtwStatus(ULONG error, std::string_view operation) {
    if (error != ERROR_SUCCESS) {
        std::cerr << "[FAIL] " << operation << " failed (error=" << error << ")\n";
    }
    QIQI_TEST_CHECK(error == ERROR_SUCCESS);
}

struct TraceProperties {
    EVENT_TRACE_PROPERTIES properties{};
    std::array<wchar_t, 1024> file{};
    std::array<wchar_t, 128> name{};
};

TraceProperties MakeTraceProperties(const std::wstring& name, const std::filesystem::path& file) {
    TraceProperties buffer;
    buffer.properties.Wnode.BufferSize = sizeof(buffer);
    buffer.properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    buffer.properties.Wnode.ClientContext = 1;
    buffer.properties.LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    buffer.properties.MaximumFileSize = 8;
    buffer.properties.BufferSize = 64;
    buffer.properties.MinimumBuffers = 2;
    buffer.properties.MaximumBuffers = 8;
    buffer.properties.LogFileNameOffset = offsetof(TraceProperties, file);
    buffer.properties.LoggerNameOffset = offsetof(TraceProperties, name);
    QIQI_TEST_CHECK(wcscpy_s(buffer.file.data(), buffer.file.size(), file.c_str()) == 0);
    QIQI_TEST_CHECK(wcscpy_s(buffer.name.data(), buffer.name.size(), name.c_str()) == 0);
    return buffer;
}

TraceProperties StopSession(TRACEHANDLE session, const std::wstring& name) {
    TraceProperties buffer;
    buffer.properties.Wnode.BufferSize = sizeof(buffer);
    buffer.properties.LogFileNameOffset = offsetof(TraceProperties, file);
    buffer.properties.LoggerNameOffset = offsetof(TraceProperties, name);
    CheckEtwStatus(::ControlTraceW(session, name.c_str(), &buffer.properties, EVENT_TRACE_CONTROL_STOP),
                   "ControlTraceW(STOP)");
    return buffer;
}

ScopedModule LoadImeModule(std::filesystem::path dll = {}) {
    if (dll.empty()) {
        const auto executable = GetModuleFilePath(nullptr);
        QIQI_TEST_CHECK(!executable.empty());
        dll = executable.parent_path() / L"qiqiime.dll";
    }
    ScopedModule module(::LoadLibraryExW(dll.c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
    QIQI_TEST_CHECK(module);
    return module;
}

ComPtr<IClassFactory> GetImeFactory(HMODULE module) {
    using GetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    const auto get_class = reinterpret_cast<GetClassObject>(::GetProcAddress(module, "DllGetClassObject"));
    QIQI_TEST_CHECK(get_class);
    ComPtr<IClassFactory> factory;
    QIQI_TEST_CHECK(get_class(kClsidQiQiTextService, IID_PPV_ARGS(&factory)) == S_OK);
    return factory;
}

thread_local monitor::MetricQueue* focus_metric_queue = nullptr;

constexpr size_t kFocusBenchmarkSamples = 10'000;
constexpr size_t kFocusBenchmarkWarmup = 1'000;
struct FocusBenchmarkState {
    std::filesystem::path dll;
    std::vector<uint64_t> durations;
    monitor::MetricAggregator aggregator;
    uint64_t events = 0;
};
thread_local FocusBenchmarkState* focus_benchmark = nullptr;

class FocusTestEnvironment final {
public:
    explicit FocusTestEnvironment(bool show_status_bar)
        : directory(std::filesystem::temp_directory_path() /
            std::format(L"qiqi-focus-test-{}-{}", ::GetCurrentProcessId(), ::GetTickCount64())),
          saved_(LoadConfig(nullptr)) {
        QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
        wchar_t* previous = nullptr;
        size_t length = 0;
        QIQI_TEST_CHECK(_wdupenv_s(&previous, &length, L"LOCALAPPDATA") == 0);
        appdata_.reset(previous);
        QIQI_TEST_CHECK(_wputenv_s(L"LOCALAPPDATA", directory.c_str()) == 0);
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.enable_jidian_extra = false;
        config.dynamic_phrase_freq = false;
        config.show_status_bar = show_status_bar;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    }
    ~FocusTestEnvironment() noexcept {
        if (!SaveConfigToRegistry(saved_)) {
            std::fputs("[FAIL] Restoring focus test configuration failed.\n", stderr);
            std::abort();
        }
        const int environment_error = _wputenv_s(L"LOCALAPPDATA", appdata_ ? appdata_.get() : L"");
        if (environment_error != 0) {
            std::fprintf(stderr, "[FAIL] Restoring focus test environment failed (error=%d).\n", environment_error);
            std::abort();
        }
        std::error_code error;
        try {
            std::filesystem::remove_all(directory, error);
        } catch (const std::bad_alloc&) {
            std::fputs("[FAIL] Focus test directory cleanup allocation failed.\n", stderr);
            std::abort();
        } catch (const std::filesystem::filesystem_error& failure) {
            std::fprintf(stderr, "[FAIL] Focus test cleanup failed (error=%d).\n", failure.code().value());
            std::abort();
        }
        if (error) {
            std::fprintf(stderr, "[FAIL] Focus test cleanup failed (error=%d).\n", error.value());
            std::abort();
        }
    }
    FocusTestEnvironment(const FocusTestEnvironment&) = delete;
    FocusTestEnvironment& operator=(const FocusTestEnvironment&) = delete;
    FocusTestEnvironment(FocusTestEnvironment&&) = delete;
    FocusTestEnvironment& operator=(FocusTestEnvironment&&) = delete;
    const std::filesystem::path directory;
    QiQiConfig config{};
private:
    const QiQiConfig saved_;
    std::unique_ptr<wchar_t, decltype(&std::free)> appdata_{nullptr, &std::free};
};

ULONG ObserveBenchmarkMetric(EVENT_RECORD& record) {
    monitor::MetricRecord decoded;
    const ULONG error = monitor::DecodeMetricEvent(record, 1, decoded);
    if (error != ERROR_SUCCESS) return error;
    if (const auto* event = std::get_if<metrics::Event>(&decoded.payload)) {
        if (event->phase != metrics::Phase::Complete || event->outcome != metrics::Outcome::Success ||
            event->mode != metrics::Mode::FocusGained || event->source != metrics::Source::KeyEvent)
            return ERROR_INVALID_DATA;
        ++focus_benchmark->events;
    }
    return focus_benchmark->aggregator.Consume(decoded);
}

void ExerciseFocusBenchmark() {
    const FocusTestEnvironment environment(false);
    const auto module = LoadImeModule(focus_benchmark->dll);
    const auto factory = GetImeFactory(module.Get());
    ComPtr<ITfTextInputProcessorEx> service;
    QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)) == S_OK);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
    ComPtr<ITfKeyEventSink> keys;
    QIQI_TEST_CHECK(service.As(&keys) == S_OK);
    for (size_t index = 0; index < kFocusBenchmarkWarmup; ++index)
        QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    auto& durations = focus_benchmark->durations;
    durations.resize(kFocusBenchmarkSamples);
    for (auto& duration : durations) {
        metrics::ClockReading start{};
        metrics::ClockReading end{};
        QIQI_TEST_CHECK(metrics::ReadClock(start) == S_OK);
        const HRESULT result = keys->OnSetFocus(TRUE);
        QIQI_TEST_CHECK(metrics::ReadClock(end) == S_OK);
        QIQI_TEST_CHECK(result == S_OK);
        QIQI_TEST_CHECK(metrics::ElapsedNanoseconds(start, end, duration) == S_OK);
    }
}

ULONG ObserveFocusMetric(EVENT_RECORD& record) {
    monitor::MetricRecord decoded;
    const ULONG error = monitor::DecodeMetricEvent(record, 1, decoded);
    if (error != ERROR_SUCCESS) return error;
    return focus_metric_queue->Push(decoded);
}

void ExerciseFocusMetrics() {
    FocusTestEnvironment environment(true);
    auto& config = environment.config;
    const auto module = LoadImeModule();
    const auto factory = GetImeFactory(module.Get());
    ComPtr<ITfTextInputProcessorEx> service;
    QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)) == S_OK);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
    ComPtr<ITfKeyEventSink> keys;
    ComPtr<ITfThreadMgrEventSink> documents;
    QIQI_TEST_CHECK(service.As(&keys) == S_OK && service.As(&documents) == S_OK);
    const auto document = MakeComObject<MockTfDocumentMgr>();
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    QIQI_TEST_CHECK(manager->global_pos_compartment_);
    manager->global_pos_compartment_->last_val_ = gsl::narrow_cast<LONG>(MAKELONG(137, 149));
    QIQI_TEST_CHECK(documents->OnSetFocus(document.Get(), nullptr) == S_OK);
    ++config.page_size;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    config.user_dict_path = (environment.directory / L"missing.txt").wstring();
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    config.user_dict_path.clear();
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    const auto position = manager->global_pos_compartment_;
    QIQI_TEST_CHECK(position);
    position->get_value_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
    position->get_value_result_ = S_OK;
    bool nested = false;
    position->get_value_callback_ = [&]() {
        if (std::exchange(nested, true)) return;
        QIQI_TEST_CHECK(keys->OnSetFocus(FALSE) == S_OK);
    };
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK && nested);
    bool cancelled = false;
    position->get_value_callback_ = [&]() {
        if (std::exchange(cancelled, true)) return;
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    };
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK && cancelled);
    position->get_value_callback_ = {};
    QIQI_TEST_CHECK(keys->OnSetFocus(TRUE) == S_OK);
}

struct HostProcess {
    ScopedKernelHandle ready;
    ScopedKernelHandle proceed;
    ScopedKernelHandle process;
    ScopedKernelHandle thread;
    DWORD pid = 0;
};

HostProcess StartHost(const std::wstring& base, const std::filesystem::path& directory, HANDLE job) {
    HostProcess host;
    host.ready.Reset(::CreateEventW(nullptr, TRUE, FALSE, (L"Local\\" + base + L".ready").c_str()));
    host.proceed.Reset(::CreateEventW(nullptr, FALSE, FALSE, (L"Local\\" + base + L".proceed").c_str()));
    QIQI_TEST_CHECK(host.ready && host.proceed);
    const auto executable = GetModuleFilePath(nullptr);
    std::wstring command = std::format(L"\"{}\" --etw-host \"{}\" \"{}\"",
                                       executable.wstring(), base, directory.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                    CREATE_SUSPENDED, nullptr, nullptr, &startup, &info));
    host.process.Reset(info.hProcess);
    host.thread.Reset(info.hThread);
    host.pid = info.dwProcessId;
    QIQI_TEST_CHECK(::AssignProcessToJobObject(job, host.process.Get()));
    return host;
}

void ResumeHost(const HostProcess& host) {
    QIQI_TEST_CHECK(::ResumeThread(host.thread.Get()) != std::numeric_limits<DWORD>::max());
    QIQI_TEST_CHECK(::WaitForSingleObject(host.ready.Get(), 30'000) == WAIT_OBJECT_0);
}

void FinishHost(const HostProcess& host) {
    QIQI_TEST_CHECK(::SetEvent(host.proceed.Get()));
    QIQI_TEST_CHECK(::WaitForSingleObject(host.process.Get(), 30'000) == WAIT_OBJECT_0);
    DWORD exit_code = 1;
    QIQI_TEST_CHECK(::GetExitCodeProcess(host.process.Get(), &exit_code) && exit_code == 0);
}

struct LogRecord {
    DWORD pid;
    UCHAR level;
    UCHAR opcode;
    uint64_t keyword;
    GUID activity_id;
    GUID related_activity_id;
    std::string message;
};

struct Collector {
    std::array<DWORD, 2> pids{};
    std::vector<LogRecord> records;
    ULONG error = ERROR_SUCCESS;
};

void WINAPI CollectEvent(EVENT_RECORD* event) {
    if (!::IsEqualGUID(event->EventHeader.ProviderId, kProvider)) return;
    auto& collector = *static_cast<Collector*>(event->UserContext);
    if (event->EventHeader.ProcessId != gsl::at(collector.pids, 0) &&
        event->EventHeader.ProcessId != gsl::at(collector.pids, 1)) {
        collector.error = ERROR_INVALID_DATA;
        return;
    }
    PROPERTY_DATA_DESCRIPTOR property{};
    property.PropertyName = reinterpret_cast<ULONGLONG>(L"message");
    property.ArrayIndex = std::numeric_limits<ULONG>::max();
    ULONG size = 0;
    ULONG error = ::TdhGetPropertySize(event, 0, nullptr, 1, &property, &size);
    if (error != ERROR_SUCCESS) {
        collector.error = error;
        return;
    }
    std::vector<BYTE> bytes(size);
    error = ::TdhGetProperty(event, 0, nullptr, 1, &property, size, bytes.data());
    if (error != ERROR_SUCCESS) {
        collector.error = error;
        return;
    }
    GUID related_id = GUID_NULL;
    if (event->ExtendedData != nullptr && event->ExtendedDataCount > 0) {
        const std::span<const EVENT_HEADER_EXTENDED_DATA_ITEM> ext_items(
            event->ExtendedData, event->ExtendedDataCount);
        for (const auto& item : ext_items) {
            if (item.ExtType == EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID &&
                item.DataSize == sizeof(EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID) &&
                item.DataPtr != 0) {
                const auto* rel = reinterpret_cast<const EVENT_EXTENDED_ITEM_RELATED_ACTIVITYID*>(item.DataPtr);
                related_id = rel->RelatedActivityId;
            }
        }
    }
    QIQI_TEST_CHECK(event->EventHeader.TimeStamp.QuadPart > 0);
    collector.records.push_back({
        event->EventHeader.ProcessId,
        event->EventHeader.EventDescriptor.Level,
        event->EventHeader.EventDescriptor.Opcode,
        event->EventHeader.EventDescriptor.Keyword,
        event->EventHeader.ActivityId,
        related_id,
        std::string(bytes.begin(), bytes.end())});
}

void ExerciseLoadedIme(QiQiConfig& config, bool multiple_instances) {
    const auto module = LoadImeModule();
    auto factory = GetImeFactory(module.Get());
    ComPtr<ITfTextInputProcessorEx> first;
    QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&first)) == S_OK);
    auto first_manager = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(first->Activate(first_manager.Get(), 1) == S_OK);
    ComPtr<ITfKeyEventSink> first_keys;
    QIQI_TEST_CHECK(first.As(&first_keys) == S_OK);
    QIQI_TEST_CHECK(first_keys->OnSetFocus(TRUE) == S_OK);
    if (multiple_instances) {
        ComPtr<ITfTextInputProcessorEx> second;
        QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&second)) == S_OK);
        auto second_manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(second->Activate(second_manager.Get(), 2) == S_OK);
        ComPtr<ITfKeyEventSink> second_keys;
        QIQI_TEST_CHECK(second.As(&second_keys) == S_OK);
        QIQI_TEST_CHECK(first->Deactivate() == S_OK);
        QIQI_TEST_CHECK(second_keys->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(first->Activate(first_manager.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(first_keys->OnSetFocus(TRUE) == S_OK);

        ++config.page_size;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
        QIQI_TEST_CHECK(first_keys->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(second_keys->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(first->Deactivate() == S_OK);
        QIQI_TEST_CHECK(first->Activate(first_manager.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(first_keys->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(second->Deactivate() == S_OK);
    } else {
        std::array<BYTE, 256> clean_kb{};
        QIQI_TEST_CHECK(::SetKeyboardState(clean_kb.data()));
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto context = MakeComObject<MockTfContext>(queue);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(first_keys->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        context->allow_sync_edit_sessions_ = false;
        QIQI_TEST_CHECK(first->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        return;
    }
    QIQI_TEST_CHECK(first->Deactivate() == S_OK);
}

} // namespace

int RunFocusBenchmark(const std::filesystem::path& dll, std::wstring_view mode) {
    if (mode != L"off" && mode != L"file") {
        std::cerr << "[FAIL] Focus benchmark mode must be off or file.\n";
        return 1;
    }
    FocusBenchmarkState state;
    state.dll = dll;
    focus_benchmark = &state;
    const auto reset = gsl::finally([]() noexcept { focus_benchmark = nullptr; });
    if (mode == L"file") {
        QIQI_TEST_CHECK(state.aggregator.BeginCapture(1) == ERROR_SUCCESS);
        const int result = RunMetricsEtwTest(ObserveBenchmarkMetric, ExerciseFocusBenchmark);
        if (result != 0) return result;
        QIQI_TEST_CHECK(state.events == 3 * (kFocusBenchmarkSamples + kFocusBenchmarkWarmup));
        QIQI_TEST_CHECK(state.aggregator.Stop(::GetTickCount64()) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(!state.aggregator.Status().incomplete);
    } else {
        QIQI_TEST_CHECK(log::EnsureInitialized());
        const auto shutdown = gsl::finally([]() noexcept { log::Shutdown(); });
        QIQI_TEST_CHECK(!log::MetricsEnabled());
        const uint64_t generation = log::MetricsCaptureGeneration();
        ExerciseFocusBenchmark();
        QIQI_TEST_CHECK(!log::MetricsEnabled() && log::MetricsCaptureGeneration() == generation);
    }
    auto& durations = state.durations;
    QIQI_TEST_CHECK(durations.size() == kFocusBenchmarkSamples);
    std::sort(durations.begin(), durations.end());
    const auto percentile = [&](size_t percent) noexcept {
        return gsl::at(durations, (durations.size() * percent + 99) / 100 - 1);
    };
    std::cout << std::format(
        "[BENCH] {{\"schema\":1,\"metric\":\"focus-gained\",\"mode\":\"{}\",\"samples\":{},"
        "\"warmup\":{},\"p50_ns\":{},\"p95_ns\":{},\"p99_ns\":{},\"maximum_ns\":{},"
        "\"events\":{},\"incomplete\":false}}\n",
        mode == L"off" ? "off" : "private-etl", durations.size(), kFocusBenchmarkWarmup,
        percentile(50), percentile(95), percentile(99), durations.back(), state.events);
    return 0;
}

int RunFocusMetricsEtwTest() {
    monitor::MetricQueue queue;
    QIQI_TEST_CHECK(queue.BeginCapture(1) == ERROR_SUCCESS);
    focus_metric_queue = &queue;
    const auto reset = gsl::finally([]() noexcept { focus_metric_queue = nullptr; });
    const int result = RunMetricsEtwTest(ObserveFocusMetric, ExerciseFocusMetrics);
    if (result != 0) return result;
    queue.EndCapture();
    const auto records = queue.Take();
    monitor::MetricAggregator aggregator;
    QIQI_TEST_CHECK(aggregator.BeginCapture(1) == ERROR_SUCCESS);
    std::vector<metrics::Event> events;
    GUID instance{};
    uint64_t service_id = 0;
    for (const auto& record : records) {
        QIQI_TEST_CHECK(record.pid == ::GetCurrentProcessId());
        QIQI_TEST_CHECK(aggregator.Consume(record) == ERROR_SUCCESS);
        if (const auto* event = std::get_if<metrics::Event>(&record.payload)) {
            QIQI_TEST_CHECK(event->phase == metrics::Phase::Complete && event->duration_ns.has_value());
            QIQI_TEST_CHECK(event->operation_id != 0 && event->activity_id != GUID_NULL);
            if (events.empty()) {
                instance = event->instance_id;
                service_id = event->service_id;
            }
            QIQI_TEST_CHECK(event->instance_id == instance && event->service_id == service_id);
            events.push_back(*event);
        }
    }
    QIQI_TEST_CHECK(instance != metrics::ModuleInstance().id && service_id != 0);
    QIQI_TEST_CHECK(events.size() == 25);
    size_t successes = 0;
    size_t failures = 0;
    size_t coded_failures = 0;
    size_t stale = 0;
    size_t cancelled = 0;
    for (const auto& event : events) {
        switch (event.outcome) {
            case metrics::Outcome::Success: ++successes; break;
            case metrics::Outcome::Failure:
                ++failures;
                if (event.error_domain == metrics::ErrorDomain::HResult) {
                    ++coded_failures;
                    QIQI_TEST_CHECK(event.error_code == gsl::narrow_cast<uint32_t>(E_ACCESSDENIED));
                } else {
                    QIQI_TEST_CHECK(event.error_domain == metrics::ErrorDomain::None && event.error_code == 0);
                }
                break;
            case metrics::Outcome::Stale: ++stale; break;
            case metrics::Outcome::Cancelled: ++cancelled; break;
            default: QIQI_TEST_CHECK(false); break;
        }
        if (event.metric == metrics::Metric::FocusTotal) {
            QIQI_TEST_CHECK(event.parent_operation_id == 0);
        } else {
            QIQI_TEST_CHECK(event.mode == metrics::Mode::FocusGained);
            const auto parent = std::find_if(events.begin(), events.end(), [&](const auto& candidate) {
                return candidate.operation_id == event.parent_operation_id;
            });
            QIQI_TEST_CHECK(parent != events.end() && parent->metric == metrics::Metric::FocusTotal &&
                parent->source == event.source && parent->mode == event.mode &&
                parent->activity_id == event.activity_id);
        }
    }
    QIQI_TEST_CHECK(successes == 17 && failures == 4 && coded_failures == 2 && stale == 2 && cancelled == 2);
    QIQI_TEST_CHECK(aggregator.Stop(::GetTickCount64()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.UpdateQueueStatus(queue.Status()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(!aggregator.Status().incomplete && aggregator.Status().remembered_operations == 25);
    const monitor::MetricDimension gained{::GetCurrentProcessId(), instance, service_id,
        metrics::Metric::FocusTotal, metrics::Mode::FocusGained, metrics::Source::KeyEvent};
    const auto summary = aggregator.Snapshot(gained, monitor::MetricView::Cumulative);
    QIQI_TEST_CHECK(summary && summary->intents == 7 && summary->success.Samples() == 3 &&
        summary->failure.Samples() == 2 && summary->Count(metrics::Outcome::Stale) == 1 &&
        summary->Count(metrics::Outcome::Cancelled) == 1 && summary->FailureRate().has_value());
    std::cout << "[PASS] Real IME DLL focus metrics -> TDH -> queue -> aggregation.\n";
    return 0;
}

void RunTsfEtwHostScenario(const wchar_t* event_base, const wchar_t* parent_directory) {
    QIQI_TEST_CHECK(!::IsDebuggerPresent());
    const std::wstring base(event_base);
    const ScopedKernelHandle ready(::OpenEventW(EVENT_MODIFY_STATE, FALSE, (L"Local\\" + base + L".ready").c_str()));
    const ScopedKernelHandle proceed(::OpenEventW(SYNCHRONIZE, FALSE, (L"Local\\" + base + L".proceed").c_str()));
    QIQI_TEST_CHECK(ready && proceed);
    const auto directory = std::filesystem::path(parent_directory) / std::to_wstring(::GetCurrentProcessId());
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    QIQI_TEST_CHECK(_wputenv_s(L"LOCALAPPDATA", directory.c_str()) == 0);

    const auto key_name = L"Software\\" + base;
    ScopedRegKey isolated_user;
    DWORD disposition = 0;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, key_name.c_str(), 0, nullptr, 0,
                                     KEY_ALL_ACCESS, nullptr, isolated_user.Put(), &disposition) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(disposition == REG_CREATED_NEW_KEY);
    QIQI_TEST_CHECK(::RegOverridePredefKey(HKEY_CURRENT_USER, isolated_user.Get()) == ERROR_SUCCESS);
    const auto restore_registry = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS);
        isolated_user.Reset();
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, key_name.c_str()) == ERROR_SUCCESS);
    });
    QiQiConfig config{};
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    constexpr DWORD legacy_off = std::to_underlying(log::Level::off);
    QIQI_TEST_CHECK(::RegSetKeyValueW(HKEY_CURRENT_USER, GetActiveConfigRegistrySubKey(), L"LogLevel",
        REG_DWORD, &legacy_off, sizeof(legacy_off)) == ERROR_SUCCESS);

    // Load/construct before signalling readiness, so the parent can start ETW after initialization.
    {
        const auto module = LoadImeModule();
        auto factory = GetImeFactory(module.Get());
        ComPtr<ITfTextInputProcessorEx> instance;
        QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&instance)) == S_OK);
        QIQI_TEST_CHECK(::SetEvent(ready.Get()));
        QIQI_TEST_CHECK(::WaitForSingleObject(proceed.Get(), 30'000) == WAIT_OBJECT_0);
        ExerciseLoadedIme(config, true);
    }
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    ExerciseLoadedIme(config, false);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
}

int RunTsfEtwIntegrationTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    GUID unique{};
    QIQI_TEST_CHECK(::CoCreateGuid(&unique) == S_OK);
    std::array<wchar_t, 40> guid{};
    QIQI_TEST_CHECK(::StringFromGUID2(unique, guid.data(), gsl::narrow_cast<int>(guid.size())) != 0);
    const std::wstring name = std::format(L"QiQiIME.EtwTest.{}.{}", ::GetCurrentProcessId(), guid.data());
    const auto directory = std::filesystem::temp_directory_path() / name;
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        QIQI_TEST_CHECK(!error);
    });
    const auto file = directory / L"capture.etl";
    auto properties = MakeTraceProperties(name, file);
    TRACEHANDLE session = 0;
    const ULONG probe = ::StartTraceW(&session, name.c_str(), &properties.properties);
    if (probe == ERROR_ACCESS_DENIED) {
        std::cerr << "[SKIP] ETW integration requires trace-control permissions (StartTraceW error=5).\n";
        return 77;
    }
    CheckEtwStatus(probe, "StartTraceW(probe)");
    std::ignore = StopSession(session, name);

    const ScopedKernelHandle job(::CreateJobObjectW(nullptr, nullptr));
    QIQI_TEST_CHECK(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    QIQI_TEST_CHECK(::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    auto late = StartHost(name + L".late", directory, job.Get());
    ResumeHost(late);
    properties = MakeTraceProperties(name, file);
    CheckEtwStatus(::StartTraceW(&session, name.c_str(), &properties.properties), "StartTraceW");
    bool active = true;
    const auto stop = gsl::finally([&]() noexcept {
        if (active) {
            std::ignore = StopSession(session, name);
        }
    });
    auto early = StartHost(name + L".early", directory, job.Get());
    std::array<DWORD, 2> pids{late.pid, early.pid};
    EVENT_FILTER_DESCRIPTOR filter{};
    filter.Ptr = reinterpret_cast<ULONGLONG>(pids.data());
    filter.Size = sizeof(pids);
    filter.Type = EVENT_FILTER_TYPE_PID;
    ENABLE_TRACE_PARAMETERS enable{};
    enable.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    enable.EnableFilterDesc = &filter;
    enable.FilterDescCount = 1;
    CheckEtwStatus(::EnableTraceEx2(session, &kProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                                   std::to_underlying(log::Keyword::Logs), 0, 5'000, &enable), "EnableTraceEx2");
    ResumeHost(early);
    FinishHost(early);
    FinishHost(late);
    properties = StopSession(session, name);
    active = false;
    if (properties.properties.EventsLost != 0 || properties.properties.LogBuffersLost != 0) {
        std::cerr << "ETW counters: events_lost=" << properties.properties.EventsLost
                  << ", buffers_lost=" << properties.properties.LogBuffersLost
                  << ", buffers=" << properties.properties.NumberOfBuffers
                  << ", minimum=" << properties.properties.MinimumBuffers
                  << ", maximum=" << properties.properties.MaximumBuffers << "\n";
    }
    QIQI_TEST_CHECK(properties.properties.EventsLost == 0 && properties.properties.LogBuffersLost == 0);

    Collector collector{pids};
    // The ETW SDK requires this callback union; ProcessTraceMode selects EventRecordCallback.
#pragma warning(suppress : 26476)
    EVENT_TRACE_LOGFILEW logfile{};
    std::wstring filename = file.wstring();
    logfile.LogFileName = filename.data();
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = CollectEvent;
    logfile.Context = &collector;
    TRACEHANDLE reader = ::OpenTraceW(&logfile);
    QIQI_TEST_CHECK(reader != INVALID_PROCESSTRACE_HANDLE);
    const auto close_reader = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(::CloseTrace(reader) == ERROR_SUCCESS); });
    QIQI_TEST_CHECK(::ProcessTrace(&reader, 1, nullptr, nullptr) == ERROR_SUCCESS);
    CheckEtwStatus(collector.error, "TDH decoding/PID filtering");
    for (const DWORD pid : pids) {
        unsigned int focus_count = 0;
        bool after_instance_deactivation = false;
        bool after_reactivation = false;
        bool after_module_reload = false;
        GUID last_deactivate_activity = GUID_NULL;
        GUID cleanup_activity = GUID_NULL;
        GUID cleanup_related = GUID_NULL;
        bool saw_cleanup_start = false;
        bool saw_cleanup_async = false;
        bool saw_cleanup_stop = false;
        for (const auto& record : collector.records) {
            if (record.pid != pid) continue;
            QIQI_TEST_CHECK(record.keyword != 0);
            if (record.message.contains("QiQiTextService::ActivateEx(")) {
                QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Lifecycle));
                QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Start));
                QIQI_TEST_CHECK(record.activity_id != GUID_NULL);
            }
            if (record.message.contains("stage=deactivate-begin")) {
                QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Lifecycle));
                QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Start));
                QIQI_TEST_CHECK(record.activity_id != GUID_NULL);
                last_deactivate_activity = record.activity_id;
            }
            if (record.message.contains("TsfCleanup(") && record.message.contains("stage=created")) {
                QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Composition));
                QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Start));
                QIQI_TEST_CHECK(record.activity_id != GUID_NULL);
                QIQI_TEST_CHECK(record.related_activity_id == last_deactivate_activity);
                cleanup_activity = record.activity_id;
                cleanup_related = record.related_activity_id;
                saw_cleanup_start = true;
            }
            if (record.message.contains("TsfCleanup(") && record.message.contains("stage=async-result, request_hr=0x00000000, session_hr=0x00040300, queued=true")) {
                QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Composition));
                QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Info));
                QIQI_TEST_CHECK(record.activity_id == cleanup_activity);
                saw_cleanup_async = true;
            }
            if (record.message.contains("TsfCleanup(") && record.message.contains("stage=callback-exit")) {
                QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Composition));
                QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Stop));
                QIQI_TEST_CHECK(record.activity_id == cleanup_activity);
                saw_cleanup_stop = true;
            }
            if (!record.message.contains("FocusPerformance(")) continue;
            QIQI_TEST_CHECK(record.level == TRACE_LEVEL_VERBOSE);
            QIQI_TEST_CHECK(record.keyword == std::to_underlying(log::Keyword::Lifecycle));
            QIQI_TEST_CHECK(record.opcode == std::to_underlying(log::Opcode::Stop));
            QIQI_TEST_CHECK(record.activity_id != GUID_NULL);
            ++focus_count;
            after_instance_deactivation |= record.message.contains("id=2,");
            after_reactivation |= record.message.contains("id=3,");
            after_module_reload |= focus_count == 7 && record.message.contains("id=1,");
        }
        QIQI_TEST_CHECK(focus_count == 7 && after_instance_deactivation && after_reactivation && after_module_reload);
        QIQI_TEST_CHECK(saw_cleanup_start && saw_cleanup_async && saw_cleanup_stop && cleanup_related != GUID_NULL);
    }
    std::cout << "[PASS] ETW captured real IME DLL events without a debugger from PIDs "
              << late.pid << " and " << early.pid << "; late enable, instance/module lifetime and ignored legacy LogLevel verified.\n";
    return 0;
}

} // namespace qiqi::test
