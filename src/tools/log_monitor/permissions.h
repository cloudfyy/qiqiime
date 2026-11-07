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
std::wstring CapturePermissionMessage(UiLanguage language, bool has_captured_logs);
HRESULT ConfirmElevatedRestart(HWND owner, UiLanguage language, const CaptureStartFailure& failure,
    bool& accepted, bool has_captured_logs = false);
ULONG LaunchElevatedMonitor(HWND owner,
    gsl::not_null<decltype(&::ShellExecuteExW)> execute = &::ShellExecuteExW);

} // namespace qiqi::monitor
