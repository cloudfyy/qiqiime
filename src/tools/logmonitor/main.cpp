// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/logmonitor/window.h"
#include <windows.h>
#include <exception>
#include "common/i18n.h"

#pragma warning(suppress : 26461) // Preserve the Win32 entry point's LPWSTR signature.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command_line, int show) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        return qiqi::monitor::RunMonitor(instance, show, command_line);
    } catch (const std::exception&) {
        const auto language = qiqi::ResolveUiLanguage(qiqi::LoadConfig().ui_language);
        ::MessageBoxW(nullptr, language == qiqi::UiLanguage::EnUs ?
            L"Cannot initialize QiQi Log Monitor (allocation or system error)." :
            L"无法初始化奇奇五笔日志监视器（内存分配或系统错误）。",
            qiqi::GetUiStrings(language).log_monitor_name, MB_OK | MB_ICONERROR);
        return 1;
    }
}
