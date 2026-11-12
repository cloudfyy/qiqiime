// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dict_manager/window.h"
#include "common/i18n.h"
#include "common/logger.h"

#include <windows.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::ignore = qiqi::log::EnsureInitialized();
    const auto cleanup_log = gsl::finally([]() noexcept { qiqi::log::Shutdown(); });
    try {
        return qiqi::dict_manager::RunDictManager(instance, show);
    } catch (const std::exception&) {
        ::MessageBoxW(
            nullptr,
            qiqi::ResolveUiLanguage(qiqi::UiLanguage::Auto) == qiqi::UiLanguage::EnUs
                ? L"Cannot initialize QiQiIME Dictionary Manager (allocation or system error)."
                : L"无法初始化奇奇词库管理（内存分配或系统错误）。",
            L"QiQiIME Dictionary Manager",
            MB_OK | MB_ICONERROR);
        return 1;
    }
}
