// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/window.h"
#include "common/i18n.h"
#include "common/logger.h"

#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <atomic>
#include <exception>

namespace qiqi {

HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};

} // namespace qiqi

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    qiqi::g_dll_instance = instance;
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::ignore = qiqi::log::EnsureInitialized();
    const auto cleanup_log = gsl::finally([]() noexcept { qiqi::log::Shutdown(); });
    const HRESULT co_result =
        ::CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED);
    if (FAILED(co_result)) {
        QIQI_LOG_ERROR(
            "Dictionary Manager COM "
            "initialization failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                co_result));
        return 1;
    }
    const auto uninitialize =
        gsl::finally(
            []() noexcept {
                ::CoUninitialize();
            });
    try {
        return qiqi::dict_manager::RunDictManager(instance, show);
    } catch (const std::exception&) {
        const auto language = qiqi::ResolveUiLanguage(qiqi::LoadConfig().ui_language);
        ::MessageBoxW(
            nullptr,
            language == qiqi::UiLanguage::EnUs
                ? L"Cannot initialize Dictionary Manager (allocation or system error)."
                : L"无法初始化词典管理器（内存分配或系统错误）。",
            qiqi::GetUiStrings(language).dict_manager_name,
            MB_OK | MB_ICONERROR);
        return 1;
    }
}
