#include "common/test_support.h"
#include "common/com_object.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "mocks.h"

#include <evntrace.h>
#include <tdh.h>
#include <filesystem>
#include <limits>
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

ScopedModule LoadImeModule() {
    const auto executable = GetModuleFilePath(nullptr);
    QIQI_TEST_CHECK(!executable.empty());
    const auto dll = executable.parent_path() / L"qiqiime.dll";
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

        config.log_level = log::Level::off;
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
    // Keep default log_level (Level::info) to verify that an active ETW session requesting TRACE_LEVEL_VERBOSE
    // receives Debug/Verbose events without requiring a manual registry LogLevel=1 override.
    QIQI_TEST_CHECK(config.log_level == log::Level::info);
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));

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
    config.log_level = log::Level::info;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    ExerciseLoadedIme(config, false);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
}

int RunTsfEtwIntegrationTests() {
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
                                   0, 0, 5'000, &enable), "EnableTraceEx2");
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
            after_module_reload |= focus_count == 4 && record.message.contains("id=1,");
        }
        QIQI_TEST_CHECK(focus_count == 4 && after_instance_deactivation && after_reactivation && after_module_reload);
        QIQI_TEST_CHECK(saw_cleanup_start && saw_cleanup_async && saw_cleanup_stop && cleanup_related != GUID_NULL);
    }
    std::cout << "[PASS] ETW captured real IME DLL events without a debugger from PIDs "
              << late.pid << " and " << early.pid << "; late enable, instance/module lifetime and Off verified.\n";
    return 0;
}

} // namespace qiqi::test
