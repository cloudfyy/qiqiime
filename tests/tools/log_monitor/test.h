// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/version.h"
#include "common/d2d_controls.h"
#include "common/theme.h"
#include "common/i18n.h"
#include "common/win32_ui_util.h"
#include "common/command_ui.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/internal.h"
#include "tools/log_monitor/permissions.h"
#include "tools/log_monitor/window.h"

#include <commctrl.h>
#include <winver.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <span>
#include <system_error>
#include <cstring>

namespace qiqi::monitor::permission_tests {
void RunMonitorStartupTests();
void RunPermissionRecoveryTests();
void RunCaptureStartupRollbackTests();
}

namespace qiqi::monitor { void RunMetricsViewTests(); }

namespace qiqi::monitor::tests {

using namespace qiqi;
using namespace qiqi::monitor;
using enum qiqi::monitor::ControlId;

HWND Child(const MonitorWindow& window, ControlId id) noexcept;
void Click(const MonitorWindow& window, ControlId id);
std::wstring CommandName(const MonitorWindow& window, ControlId id);
void SetProcessFilter(const MonitorWindow& window, const wchar_t* name);
void SelectPid(const MonitorWindow& window, DWORD pid);
LRESULT CALLBACK SimulateOpenDropdown(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                      DWORD_PTR) noexcept;
using qiqi::test::ControlText;
void SelectLanguage(const MonitorWindow& window, UiLanguage language) noexcept;
void CheckComboEditFont(HWND combo, UINT dpi);
std::wstring ReadClipboardText(HWND owner = nullptr);
void TestDarkHeader(HWND list);
int IconWidth(HICON icon);
Event MakeEvent(std::wstring text = L"message");
void TestScrollIcons();
void TestMenusAndToolbar(const RECT& work_area);
void TestAboutDialogs();
void TestBuffersAndFilters();
void TestExport();
void TestWindowAndVersion();
void TestDesktopIntegration();
int Integration(const wchar_t* peer);

} // namespace qiqi::monitor::tests
