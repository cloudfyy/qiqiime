// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/file_util.h"
#include "common/window.h"
#include "tools/log_monitor/permissions.h"
#include "tools/log_monitor/window.h"

#include <commctrl.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <numeric>
#include <utility>
#include <vector>

namespace qiqi::monitor::permission_tests {
namespace {
thread_local ULONG start_error = ERROR_SUCCESS;
thread_local ULONG enable_error = ERROR_SUCCESS;
thread_local ULONG open_error = ERROR_SUCCESS;
thread_local ULONG launch_error = ERROR_SUCCESS;
thread_local HANDLE launched_process = nullptr;
thread_local std::optional<POINT> launch_point;
thread_local std::wstring launch_parameters;

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
    QIQI_TEST_CHECK(info->nShow == SW_SHOWNORMAL);
    if (launch_point) {
        QIQI_TEST_CHECK(info->lpParameters);
        launch_parameters = info->lpParameters;
        std::optional<POINT> parsed;
        QIQI_TEST_CHECK(ParseMonitorStartupPoint(info->lpParameters, parsed) == ERROR_SUCCESS && parsed);
        QIQI_TEST_CHECK(parsed->x == launch_point->x && parsed->y == launch_point->y);
    } else {
        QIQI_TEST_CHECK(info->lpParameters == nullptr);
        launch_parameters.clear();
    }
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

void CheckElevatedLaunch(HWND owner, ULONG error) {
    const ULONG previous = std::exchange(launch_error, error);
    const auto restore = gsl::finally([previous]() noexcept { launch_error = previous; });
    QIQI_TEST_CHECK(LaunchElevatedMonitor(owner, ExecuteElevated) == error);
    if (error == ERROR_SUCCESS) {
        DWORD flags = 0;
        QIQI_TEST_CHECK(!::GetHandleInformation(launched_process, &flags) && ::GetLastError() == ERROR_INVALID_HANDLE);
    }
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

void TestStartupPointArguments() {
    std::optional<POINT> point = POINT{1, 2};
    for (const auto arguments : {L"", L" \t\r\n"}) {
        QIQI_TEST_CHECK(ParseMonitorStartupPoint(arguments, point) == ERROR_SUCCESS && !point);
    }
    for (const POINT expected : {POINT{0, 0}, POINT{-1920, -1080}, POINT{2560, 1440},
            POINT{LONG_MIN, LONG_MAX}, POINT{LONG_MAX, LONG_MIN}}) {
        const auto arguments = std::format(L"  --monitor-point {} {} \t", expected.x, expected.y);
        QIQI_TEST_CHECK(ParseMonitorStartupPoint(arguments, point) == ERROR_SUCCESS && point);
        QIQI_TEST_CHECK(point->x == expected.x && point->y == expected.y);
    }
    QIQI_TEST_CHECK(ParseMonitorStartupPoint(L"--monitor-point\t+1\r\n-2", point) == ERROR_SUCCESS && point);
    QIQI_TEST_CHECK(point->x == 1 && point->y == -2);
    for (const auto arguments : {L"--monitor-point", L"--monitor-point 1", L"--monitor-point x 2",
            L"--monitor-point 1 x", L"--monitor-point 1.5 2", L"--monitor-point 1 2 --monitor-point 3 4",
            L"--monitor-point 1 2 extra", L"--monitor-point 2147483648 0",
            L"--monitor-point -2147483649 0", L"--monitor-point 0 2147483648",
            L"--monitor-point 0 -2147483649", L"--monitor-point 1 2.5", L"--monitor-point 1-2",
            L"--unknown 1 2"}) {
        point = POINT{1, 2};
        QIQI_TEST_CHECK(ParseMonitorStartupPoint(arguments, point) == ERROR_INVALID_PARAMETER && !point);
    }
}

void TestStartupProcess(std::wstring_view parameters, gsl::not_null<HMONITOR> monitor) {
    const ScopedKernelHandle job(::CreateJobObjectW(nullptr, nullptr));
    QIQI_TEST_CHECK(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    QIQI_TEST_CHECK(::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    const auto executable = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMELogMonitor.exe";
    auto command = std::format(L"\"{}\" {}", executable.wstring(), parameters);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process));
    const ScopedKernelHandle child(process.hProcess);
    const ScopedKernelHandle thread(process.hThread);
    const BOOL assigned = ::AssignProcessToJobObject(job.Get(), child.Get());
    if (!assigned) QIQI_TEST_CHECK(::TerminateProcess(child.Get(), ERROR_PROCESS_ABORTED));
    QIQI_TEST_CHECK(assigned);
    QIQI_TEST_CHECK(::ResumeThread(thread.Get()) != MAXDWORD);
    QIQI_TEST_CHECK(::WaitForInputIdle(child.Get(), 10'000) == 0);
    HWND window = nullptr;
    ::EnumThreadWindows(process.dwThreadId, [](HWND candidate, LPARAM context) noexcept -> BOOL {
        std::array<wchar_t, 128> name{};
        if (::GetClassNameW(candidate, name.data(), gsl::narrow_cast<int>(name.size())) &&
            std::wstring_view(name.data()) == kWindowClass) {
            *reinterpret_cast<HWND*>(context) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    QIQI_TEST_CHECK(window && ::IsWindowVisible(window));
    QIQI_TEST_CHECK(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor.get());
    QIQI_TEST_CHECK(::PostMessageW(window, WM_CLOSE, 0, 0));
    QIQI_TEST_CHECK(::WaitForSingleObject(child.Get(), 10'000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    QIQI_TEST_CHECK(::GetExitCodeProcess(child.Get(), &exit) && exit == 0);
}

void TestRestartMonitorPlacement() {
    const auto restore = gsl::finally([]() noexcept {
        launch_point.reset();
        launch_parameters.clear();
        MSG message{};
        while (::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    });
    std::vector<HMONITOR> monitors;
    QIQI_TEST_CHECK(::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors)));
    QIQI_TEST_CHECK(!monitors.empty());
    for (const auto monitor : monitors) {
        MONITORINFO metrics{};
        metrics.cbSize = sizeof(metrics);
        QIQI_TEST_CHECK(::GetMonitorInfoW(monitor, &metrics));
        launch_point = POINT{std::midpoint(metrics.rcWork.left, metrics.rcWork.right),
            std::midpoint(metrics.rcWork.top, metrics.rcWork.bottom)};
        MonitorWindow window;
        QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr), launch_point));
        QIQI_TEST_CHECK(::MonitorFromWindow(window.Handle(), MONITOR_DEFAULTTONEAREST) == monitor);
        RECT actual{};
        QIQI_TEST_CHECK(::GetWindowRect(window.Handle(), &actual));
        const auto expected = ComputeCenteredToolWindowRect(
            QueryMonitorForPoint(*launch_point), 1160, 780, ::GetDpiForWindow(window.Handle()));
        QIQI_TEST_CHECK(::EqualRect(&actual, &expected));
        for (const ULONG error : {ERROR_CANCELLED, ERROR_ACCESS_DENIED, ERROR_FILE_NOT_FOUND, ERROR_SUCCESS}) {
            CheckElevatedLaunch(window.Handle(), error);
        }
        TestStartupProcess(launch_parameters, monitor);
    }
    const POINT disconnected_point{LONG_MIN, LONG_MIN};
    MonitorWindow recovered;
    QIQI_TEST_CHECK(recovered.Create(::GetModuleHandleW(nullptr), disconnected_point));
    const auto nearest = ::MonitorFromPoint(disconnected_point, MONITOR_DEFAULTTONEAREST);
    QIQI_TEST_CHECK(::MonitorFromWindow(recovered.Handle(), MONITOR_DEFAULTTONEAREST) == nearest);
    TestStartupProcess(std::format(L"--monitor-point {} {}", disconnected_point.x, disconnected_point.y), nearest);
}

void TestInvalidStartupArguments() {
    const bool english = ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs;
    const auto expected = std::format(L"{}\r\n{}=87 (0x00000057)",
        english ? L"Invalid log monitor startup arguments." : L"日志监控启动参数无效。",
        english ? L"Error" : L"错误码");
    qiqi::test::WithDismissedNativeMessage(expected.c_str(), []() {
        QIQI_TEST_CHECK(RunMonitor(::GetModuleHandleW(nullptr), SW_SHOWNORMAL, L"--monitor-point 1") == 1);
    });
    QIQI_TEST_CHECK(!qiqi::test::FindThreadWindowByClass(kWindowClass));
}
} // namespace

void RunMonitorStartupTests() {
    const auto previous = ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QIQI_TEST_CHECK(previous);
    const auto restore = gsl::finally([previous]() noexcept {
        QIQI_TEST_CHECK(::SetThreadDpiAwarenessContext(previous));
    });
    TestStartupPointArguments();
    TestRestartMonitorPlacement();
    TestInvalidStartupArguments();
}

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
        CheckElevatedLaunch(nullptr, error);
    }
    QIQI_TEST_CHECK(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const auto uninitialize = gsl::finally([]() noexcept { ::CoUninitialize(); });
    QIQI_TEST_CHECK(LaunchElevatedMonitor(nullptr, ExecuteElevated) == gsl::narrow_cast<ULONG>(RPC_E_CHANGED_MODE));
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
