// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/log_monitor/collector.h"
#include "common/i18n.h"

#include <shellapi.h>

namespace qiqi::monitor {

struct CapturePermissions {
    bool elevated = false;
    bool performance_log_user = false;
};

ULONG QueryCapturePermissions(CapturePermissions& permissions) noexcept;
void SetElevatedOverrideForTest(std::optional<bool> elevated) noexcept;
bool IsMonitorProcessElevated() noexcept;
std::wstring FormatMonitorWindowTitle(UiLanguage language, bool elevated = IsMonitorProcessElevated());
std::wstring CapturePermissionMessage(UiLanguage language, bool has_captured_logs);
HRESULT ConfirmElevatedRestart(HWND owner, UiLanguage language, const CaptureStartFailure& failure,
    bool& accepted, bool has_captured_logs = false);
ULONG LaunchElevatedMonitor(HWND owner,
    gsl::not_null<decltype(&::ShellExecuteExW)> execute = &::ShellExecuteExW);
ULONG ParseMonitorStartupPoint(std::wstring_view command_line, std::optional<POINT>& point);

} // namespace qiqi::monitor
