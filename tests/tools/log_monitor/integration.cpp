// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

int Integration(const wchar_t* peer) {
    Diagnostics diagnostics;
    EventQueue queue{32, 128 * 1024};
    Collector collector(queue, diagnostics);
    // Initialize with default Level::info to verify that Collector's TRACE_LEVEL_VERBOSE session
    // receives Debug/Trace events without requiring SetLevel(Level::trace/debug).
    log::InitLogger(log::Level::info);
    const auto shutdown = gsl::finally([]() noexcept { log::Shutdown(); });
    if (!collector.Start()) {
        const auto errors = diagnostics.queue.Take();
        for (const auto& error : errors) std::wcerr << error.message << L"\n";
        const auto& failure = collector.LastStartFailure();
        if (failure && failure->IsPermissionDenied()) return 77;
        QIQI_TEST_CHECK(false);
    }
    const std::wstring session_name(collector.SessionName());
    const std::string marker = std::string("QiQi monitor exact UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd\tline\nend") + std::string("\0tail\0", 6);
    const GUID parent_act = log::CreateActivityId();
    const GUID child_act = log::CreateActivityId();
    log::WriteLogMessage(
        log::Level::info,
        marker,
        log::LogContext{log::Keyword::Sync, log::Opcode::Start, &child_act, &parent_act});
    QIQI_LOG_ACT_STOP(log::Level::debug, log::Keyword::Sync, &child_act, "monitor-verbose-with-default-info-level");
    diagnostics.Record(2, L"MONITOR_ONLY_SENTINEL", ERROR_INVALID_DATA);
    log::Shutdown();
    QIQI_TEST_CHECK(log::EnsureInitialized());
    log::WriteLogMessage(log::Level::warn, "after provider re-registration");

    const auto executable = peer ? std::filesystem::path(peer) : GetModuleFilePath(nullptr);
    std::wstring command = L"\"" + executable.wstring() + L"\" --emit";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const ScopedKernelHandle child(process.hProcess);
    const ScopedKernelHandle thread(process.hThread);
    QIQI_TEST_CHECK(::WaitForSingleObject(child.Get(), 10'000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    QIQI_TEST_CHECK(::GetExitCodeProcess(child.Get(), &exit) && exit == 0);
    std::vector<Event> received;
    for (int i = 0; i < 60; ++i) {
        auto batch = queue.Take();
        for (auto& event : batch) received.push_back(std::move(event));
        const bool own = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.message == Utf8ToWide(marker);
        });
        const bool own_verbose = std::any_of(received.begin(), received.end(), [](const Event& event) {
            return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5;
        });
        const bool remote = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-emitter";
        });
        const bool remote_verbose = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" && event.level == 5;
        });
        if (own && own_verbose && remote && remote_verbose) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    collector.PollLoss();
    QIQI_TEST_CHECK(collector.Stop());
    if (collector.decode_errors != 0 || collector.events_lost != 0) {
        std::cerr << "ETW counters: decode=" << collector.decode_errors.load()
                  << ", events_lost=" << collector.events_lost.load()
                  << ", buffers_lost=" << collector.buffers_lost.load() << "\n";
    }
    QIQI_TEST_CHECK(!collector.Running() && collector.decode_errors == 0 && collector.events_lost == 0);
    const auto own = std::find_if(received.begin(), received.end(), [&](const Event& event) {
        return event.message == Utf8ToWide(marker);
    });
    QIQI_TEST_CHECK(own != received.end());
    std::wcout << L"Decoded event: " << own->name << L", UTC=" << FormatTime(own->time)
               << L", now=" << FormatTime(MakeEvent().time) << std::endl;
    QIQI_TEST_CHECK(own->pid == ::GetCurrentProcessId() && own->tid == ::GetCurrentThreadId());
    QIQI_TEST_CHECK(own->level == 4 && !own->process.empty());
    QIQI_TEST_CHECK(own->keyword == std::to_underlying(log::Keyword::Sync));
    QIQI_TEST_CHECK(own->opcode == std::to_underlying(log::Opcode::Start));
    QIQI_TEST_CHECK(own->activity_id == child_act && own->related_activity_id == parent_act);
    QIQI_TEST_CHECK(own->name == L"QiQiLogInfo");
    QIQI_TEST_CHECK(own->time <= MakeEvent().time && MakeEvent().time - own->time < 60ULL * 10'000'000);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5 &&
               event.keyword == std::to_underlying(log::Keyword::Sync) &&
               event.opcode == std::to_underlying(log::Opcode::Stop) &&
               event.activity_id == child_act && event.related_activity_id == GUID_NULL &&
               event.name == L"QiQiLogVerbose";
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-emitter" &&
               event.keyword == std::to_underlying(log::Keyword::General) &&
               event.opcode == std::to_underlying(log::Opcode::Info) &&
               event.activity_id == GUID_NULL;
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" &&
               event.level == 5 && event.keyword == std::to_underlying(log::Keyword::Lifecycle) &&
               event.name == L"QiQiLogVerbose";
    }));
    QIQI_TEST_CHECK(std::none_of(received.begin(), received.end(), [](const Event& event) {
        return event.message.contains(L"MONITOR_ONLY_SENTINEL");
    }));
    struct Properties {
        EVENT_TRACE_PROPERTIES trace{};
        std::array<wchar_t, 256> name{};
    } query;
    query.trace.Wnode.BufferSize = sizeof(query);
    query.trace.LoggerNameOffset = offsetof(Properties, name);
    QIQI_TEST_CHECK(::ControlTraceW(0, session_name.c_str(), &query.trace, EVENT_TRACE_CONTROL_QUERY) == ERROR_WMI_INSTANCE_NOT_FOUND);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [](const Event& event) {
        return event.message == L"after provider re-registration" && event.level == 3;
    }));

    {
        MonitorWindow window;
        QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
        SelectLanguage(window, UiLanguage::EnUs);
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture (Logs)");
        Click(window, MetricsView);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture (Logs)");
        QIQI_TEST_CHECK(ControlText(Child(window, MetricSummaryStatus)).contains(L"Capturing Logs only"));
        Click(window, Source);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture (Logs)");
        GUID gui_activity{};
        QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&gui_activity)));
        log::WriteLogMessage(log::Level::info, "filter-parent",
            log::LogContext{log::Keyword::Sync, log::Opcode::Start, &gui_activity});
        SetProcessFilter(window, own->process.c_str());
        const auto parent_pid = std::to_wstring(::GetCurrentProcessId());
        for (int i = 0; i < 50 && ControlText(Child(window, Pid)) != parent_pid; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
        const LRESULT name_index = ::SendMessageW(Child(window, Process), CB_FINDSTRINGEXACT,
            gsl::narrow_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(own->process.c_str()));
        QIQI_TEST_CHECK(name_index != CB_ERR);
        ::SendMessageW(Child(window, Process), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(name_index), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)) == own->process);
        ::SetWindowTextW(Child(window, Search), L"filter-parent");
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(FilterActivity), MF_BYCOMMAND) & MF_GRAYED) == 0);
        Click(window, FilterActivity);
        QIQI_TEST_CHECK(ControlText(Child(window, Status)).contains(L"Activity filter"));
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(ClearActivity), MF_BYCOMMAND) & MF_GRAYED) == 0);
        Click(window, ClearActivity);
        QIQI_TEST_CHECK(!ControlText(Child(window, Status)).contains(L"Activity filter"));
        ::SetWindowTextW(Child(window, Search), L"");

        const HWND process_combo = Child(window, Process);
        QIQI_TEST_CHECK(::SetWindowSubclass(process_combo, SimulateOpenDropdown, 92, 0));
        const auto restore_dropdown = gsl::finally([=]() noexcept {
            ::RemoveWindowSubclass(process_combo, SimulateOpenDropdown, 92);
        });
        PROCESS_INFORMATION second_child{};
        QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &second_child));
        const ScopedKernelHandle second_process(second_child.hProcess);
        const ScopedKernelHandle second_thread(second_child.hThread);
        QIQI_TEST_CHECK(::WaitForSingleObject(second_process.Get(), 10'000) == WAIT_OBJECT_0);
        DWORD second_exit = 1;
        QIQI_TEST_CHECK(::GetExitCodeProcess(second_process.Get(), &second_exit) && second_exit == 0);
        // Drain captured child events while refresh is deferred; no new events are needed on close.
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_CLOSEUP), 0);
        QIQI_TEST_CHECK(::RemoveWindowSubclass(process_combo, SimulateOpenDropdown, 92));
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        for (int i = 0; i < 50 && ::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) != 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, 2, 0); // Lifecycle
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, 0, 0); // All
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        ::SetWindowTextW(Child(window, Search), L"child-debug");
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        SelectLanguage(window, UiLanguage::ZhCn);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"停止采集 (Logs)");
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        SelectLanguage(window, UiLanguage::EnUs);
        ::SetWindowTextW(Child(window, Search), L"");
        SetProcessFilter(window, L"");
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Start capture (Logs)");
        Click(window, Clear);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Process), CB_GETCOUNT, 0, 0) == 0);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        Click(window, DiagnosticsView);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)).empty());
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SetProcessFilter(window, L"QiQiIMELogMonitor.exe");
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
        Click(window, MetricsView);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Start capture (Logs + Metrics)");
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture (Logs + Metrics)");
        QIQI_TEST_CHECK((::SendMessageW(Child(window, Toolbar), TB_GETSTATE, std::to_underlying(Start), 0) & TBSTATE_CHECKED) != 0);
        Click(window, Source);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture (Logs + Metrics)");
        Click(window, MetricsView);
        {
            metrics::Scope metric(metrics::Metric::FocusTotal, 1);
            QIQI_TEST_CHECK(metric.Active());
            metric.SetActivity(gui_activity);
            metric.SetOutcome(metrics::Outcome::Success);
        }
        const HWND metric_list = Child(window, MetricList);
        for (int i = 0; i < 50 && ListView_GetItemCount(metric_list) == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(ListView_GetItemCount(metric_list) == 1);
        std::array<wchar_t, 128> metric_count{};
        ListView_GetItemText(metric_list, 0, 6, metric_count.data(), gsl::narrow_cast<int>(metric_count.size()));
        QIQI_TEST_CHECK(std::wstring_view(metric_count.data()) == L"1");
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Start capture (Logs + Metrics)");
        QIQI_TEST_CHECK((::SendMessageW(Child(window, Toolbar), TB_GETSTATE, std::to_underlying(Start), 0) & TBSTATE_CHECKED) == 0);
        QIQI_TEST_CHECK(ControlText(Child(window, MetricSummaryStatus)).contains(L"Metrics frozen"));
    }

    QIQI_TEST_CHECK(collector.Start());
    for (int i = 0; i < 1024; ++i) log::WriteLogMessage(log::Level::info, "burst-monitor");
    for (int i = 0; i < 30 && queue.Evicted() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    QIQI_TEST_CHECK(queue.Evicted() > 0 && queue.Take().size() == 32);
    QIQI_TEST_CHECK(collector.Stop());
    QIQI_TEST_CHECK(collector.decode_errors == 0);
    QIQI_TEST_CHECK(collector.Stop());
    qiqi::monitor::permission_tests::RunCaptureStartupRollbackTests();
    return 0;
}

} // namespace qiqi::monitor::tests
