#include "common/test_support.h"
#include "common/file_util.h"
#include "tools/log_monitor/permissions.h"

#include <commctrl.h>
#include <algorithm>
#include <array>
#include <format>

namespace qiqi::monitor::permission_tests {
namespace {
thread_local ULONG start_error = ERROR_SUCCESS;
thread_local ULONG enable_error = ERROR_SUCCESS;
thread_local ULONG open_error = ERROR_SUCCESS;
thread_local ULONG launch_error = ERROR_SUCCESS;
thread_local HANDLE launched_process = nullptr;

ULONG WINAPI StartCaptureSession(PTRACEHANDLE session, LPCWSTR name, PEVENT_TRACE_PROPERTIES properties) noexcept {
    if (start_error != ERROR_SUCCESS) return start_error;
    return ::StartTraceW(session, name, properties);
}

ULONG WINAPI EnableCaptureProvider(TRACEHANDLE session, LPCGUID provider, ULONG control, UCHAR level,
    ULONGLONG any, ULONGLONG all, ULONG timeout, PENABLE_TRACE_PARAMETERS parameters) noexcept {
    if (enable_error != ERROR_SUCCESS) return enable_error;
    return ::EnableTraceEx2(session, provider, control, level, any, all, timeout, parameters);
}

TRACEHANDLE WINAPI OpenCaptureReader(PEVENT_TRACE_LOGFILEW logfile) noexcept {
    if (open_error != ERROR_SUCCESS) {
        ::SetLastError(open_error);
        return INVALID_PROCESSTRACE_HANDLE;
    }
    return ::OpenTraceW(logfile);
}

BOOL WINAPI ExecuteElevated(SHELLEXECUTEINFOW* info) {
    QIQI_TEST_CHECK(info && info->cbSize == sizeof(*info));
    QIQI_TEST_CHECK(std::wstring_view(info->lpVerb) == L"runas");
    QIQI_TEST_CHECK(std::filesystem::path(info->lpFile) == GetModuleFilePath(nullptr));
    QIQI_TEST_CHECK(info->lpParameters == nullptr && info->nShow == SW_SHOWNORMAL);
    QIQI_TEST_CHECK((info->fMask & SEE_MASK_NOCLOSEPROCESS) != 0 &&
                    (info->fMask & SEE_MASK_NOASYNC) != 0 && (info->fMask & SEE_MASK_FLAG_NO_UI) != 0);
    if (launch_error != ERROR_SUCCESS) {
        ::SetLastError(launch_error);
        return FALSE;
    }
    launched_process = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QIQI_TEST_CHECK(launched_process);
    info->hProcess = launched_process;
    return TRUE;
}

struct PromptExpectation {
    int button = IDCANCEL;
    std::wstring_view label;
    bool seen = false;
    int attempts = 0;
};
thread_local PromptExpectation* prompt = nullptr;

void CALLBACK AnswerPrompt(HWND, UINT, UINT_PTR id, DWORD) {
    QIQI_TEST_CHECK(prompt && ++prompt->attempts < 300);
    const HWND dialog = qiqi::test::FindThreadWindowByClass(L"#32770");
    if (!dialog) return;
    std::wstring content;
    ::EnumChildWindows(dialog, [](HWND child, LPARAM context) noexcept -> BOOL {
        std::array<wchar_t, 4096> text{};
        const int length = ::GetWindowTextW(child, text.data(), gsl::narrow_cast<int>(text.size()));
        auto* output = reinterpret_cast<std::wstring*>(context);
        output->append(text.data(), gsl::narrow_cast<size_t>(length));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&content));
    if (!content.contains(prompt->label)) return;
    prompt->seen = true;
    ::KillTimer(nullptr, id);
    ::SendMessageW(dialog, TDM_CLICK_BUTTON, gsl::narrow_cast<WPARAM>(prompt->button), 0);
}

void CheckSessionStopped(std::wstring_view name) {
    struct Properties {
        EVENT_TRACE_PROPERTIES trace{};
        std::array<wchar_t, 256> name{};
    } properties;
    properties.trace.Wnode.BufferSize = sizeof(properties);
    properties.trace.LoggerNameOffset = offsetof(Properties, name);
    const std::wstring session_name(name);
    QIQI_TEST_CHECK(::ControlTraceW(0, session_name.c_str(), &properties.trace,
                                  EVENT_TRACE_CONTROL_QUERY) == ERROR_WMI_INSTANCE_NOT_FOUND);
}
} // namespace

void RunPermissionRecoveryTests() {
    CapturePermissions permissions;
    QIQI_TEST_CHECK(QueryCapturePermissions(permissions) == ERROR_SUCCESS);
    Diagnostics diagnostics;
    EventQueue queue;
    Collector collector(queue, diagnostics, {StartCaptureSession, EnableCaptureProvider, OpenCaptureReader});
    const auto restore = gsl::finally([]() noexcept {
        start_error = ERROR_SUCCESS;
        launch_error = ERROR_SUCCESS;
        prompt = nullptr;
    });
    for (const ULONG error : {ERROR_ACCESS_DENIED, ERROR_PRIVILEGE_NOT_HELD, ERROR_NO_SYSTEM_RESOURCES}) {
        start_error = error;
        QIQI_TEST_CHECK(!collector.Start() && !collector.Running());
        const auto& failure = collector.LastStartFailure();
        QIQI_TEST_CHECK(failure && failure->operation == L"StartTraceW" && failure->error == error);
        QIQI_TEST_CHECK(failure->IsPermissionDenied() == (error != ERROR_NO_SYSTEM_RESOURCES));
        QIQI_TEST_CHECK(collector.Stop() && collector.LastStartFailure()->error == error);
        const auto records = diagnostics.queue.Take();
        QIQI_TEST_CHECK(std::any_of(records.begin(), records.end(), [error](const Event& event) {
            return event.level == 2 && event.message == std::format(L"StartTraceW (error={}, 0x{:08X})", error, error);
        }));
    }
    bool accepted = true;
    QIQI_TEST_CHECK(ConfirmElevatedRestart(nullptr, UiLanguage::EnUs,
        {L"StartTraceW", ERROR_NO_SYSTEM_RESOURCES}, accepted) == E_INVALIDARG && !accepted);
    for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
        for (const bool has_captured_logs : {false, true}) {
            const auto content = CapturePermissionMessage(language, has_captured_logs);
            QIQI_TEST_CHECK(content.contains(language == UiLanguage::EnUs ? L"administrator" : L"管理员"));
            QIQI_TEST_CHECK(content.contains(language == UiLanguage::EnUs ? L"captured logs" : L"已采集的日志") == has_captured_logs);
            QIQI_TEST_CHECK(content.contains(language == UiLanguage::EnUs ? L"export" : L"导出") == has_captured_logs);
            QIQI_TEST_CHECK(!content.contains(L"0x") && !content.contains(L"ETW") &&
                            !content.contains(L"StartTrace") && !content.contains(L"EnableTrace") &&
                            !content.contains(L"OpenTrace"));
            for (const auto operation : {L"StartTraceW", L"EnableTraceEx2(QiQiIME.TraceLogging)", L"OpenTraceW"}) {
                for (const int button : {IDCANCEL, IDYES}) {
                    PromptExpectation expectation{button,
                        language == UiLanguage::EnUs ? L"Restart as administrator" : L"以管理员身份重新启动"};
                    prompt = &expectation;
                    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, AnswerPrompt);
                    QIQI_TEST_CHECK(timer);
                    const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
                    QIQI_TEST_CHECK(ConfirmElevatedRestart(
                        nullptr, language, {operation, ERROR_ACCESS_DENIED}, accepted, has_captured_logs) == S_OK);
                    QIQI_TEST_CHECK(expectation.seen && accepted == (button == IDYES));
                    prompt = nullptr;
                }
            }
        }
    }
    for (const ULONG error : {ERROR_CANCELLED, ERROR_ACCESS_DENIED, ERROR_FILE_NOT_FOUND, ERROR_SUCCESS}) {
        launch_error = error;
        QIQI_TEST_CHECK(LaunchElevatedMonitor(nullptr, ExecuteElevated) == error);
        if (error == ERROR_SUCCESS) {
            DWORD flags = 0;
            QIQI_TEST_CHECK(!::GetHandleInformation(launched_process, &flags) && ::GetLastError() == ERROR_INVALID_HANDLE);
        }
    }
}

void RunCaptureStartupRollbackTests() {
    Diagnostics diagnostics;
    EventQueue queue;
    Collector collector(queue, diagnostics, {StartCaptureSession, EnableCaptureProvider, OpenCaptureReader});
    const auto restore = gsl::finally([]() noexcept {
        enable_error = ERROR_SUCCESS;
        open_error = ERROR_SUCCESS;
    });
    for (const bool deny_open : {false, true}) {
        enable_error = deny_open ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
        open_error = deny_open ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
        QIQI_TEST_CHECK(!collector.Start() && !collector.Running());
        const auto& failure = collector.LastStartFailure();
        QIQI_TEST_CHECK(failure && failure->IsPermissionDenied() && failure->error == ERROR_ACCESS_DENIED);
        QIQI_TEST_CHECK(failure->operation == (deny_open ? L"OpenTraceW" : L"EnableTraceEx2(QiQiIME.TraceLogging)"));
        CheckSessionStopped(collector.SessionName());
        QIQI_TEST_CHECK(collector.Stop() && collector.LastStartFailure()->error == ERROR_ACCESS_DENIED);
        enable_error = ERROR_SUCCESS;
        open_error = ERROR_SUCCESS;
        QIQI_TEST_CHECK(collector.Start() && !collector.LastStartFailure());
        const std::wstring session(collector.SessionName());
        QIQI_TEST_CHECK(collector.Stop());
        CheckSessionStopped(session);
    }
}
} // namespace qiqi::monitor::permission_tests
