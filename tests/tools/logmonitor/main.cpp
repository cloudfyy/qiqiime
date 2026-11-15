// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

using namespace qiqi::monitor::tests;

int wmain(int argc, wchar_t* argv[]) {
    QIQI_TEST_CHECK(argv != nullptr);
    const std::span<wchar_t*> args(argv, gsl::narrow_cast<size_t>(argc));
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--startup") {
        qiqi::monitor::permission_tests::RunMonitorStartupTests();
        std::cout << "[PASS] Log monitor startup tests\n";
        return 0;
    }
    if (args.size() == 2 && (std::wstring_view(gsl::at(args, 1)) == L"--preview-dark" ||
        std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast")) {
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetThemeDarkOverrideForTest(true);
        SetHighContrastOverrideForTest(std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast");
        return RunMonitor(::GetModuleHandleW(nullptr), SW_SHOWNORMAL);
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--emit") {
        qiqi::log::InitLogger();
        QIQI_LOG_CAT_DEBUG(qiqi::log::Keyword::Lifecycle, "child-debug-session-controlled");
        qiqi::log::WriteLogMessage(qiqi::log::Level::info, "child-emitter");
        qiqi::log::Shutdown();
        return 0;
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--integration") {
        return Integration(args.size() == 3 ? gsl::at(args, 2) : nullptr);
    }
    TestBuffersAndFilters();
    qiqi::monitor::permission_tests::RunMonitorStartupTests();
    qiqi::monitor::permission_tests::RunPermissionRecoveryTests();
    TestExport();
    TestScrollIcons();
    TestAboutDialogs();
    TestWindowAndVersion();
    qiqi::monitor::RunMetricsViewTests();
    TestMenusAndToolbar({0, 0, 3840, 2160});
    TestMenusAndToolbar({0, 0, 1024, 728});
    TestDesktopIntegration();
    std::cout << "[PASS] Log monitor model, GUI and version tests\n";
    return 0;
}
