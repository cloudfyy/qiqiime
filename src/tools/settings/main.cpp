// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "common/logger.h"
#include "tools/settings/window.h"
#include "tools/settings/cleanup.h"

#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <new>
#include <string_view>

namespace qiqi {

HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};

} // namespace qiqi

#pragma warning(suppress : 26461) // Preserve the Windows entry point's LPWSTR signature.
int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE /*prev_instance*/,
    _In_ LPWSTR cmd_line,
    _In_ int /*show_cmd*/) {
    if (cmd_line && std::wstring_view(cmd_line) == L"--remove-user-data") {
        qiqi::log::InitLogger();
        HRESULT result = E_UNEXPECTED;
        try {
            result = qiqi::settings::RemoveCurrentUserData();
        } catch (const std::bad_alloc&) {
            result = E_OUTOFMEMORY;
        } catch (const std::filesystem::filesystem_error& error) {
            result = HRESULT_FROM_WIN32(error.code().value());
        }
        std::ignore = qiqi::log::CheckHResult(result, "Settings::RemoveUserData");
        qiqi::log::Shutdown();
        return SUCCEEDED(result) ? 0 : 1;
    }
    // Single-instance check: if the settings dialog is already open, bring it to the foreground.
    if (qiqi::ActivateExistingSettingsWindow()) {
        return 0;
    }
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_uninit = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    qiqi::g_dll_instance = instance;
    qiqi::log::InitLogger();
    std::ignore = qiqi::log::CheckHResult(co_hr, "Settings::CoInitializeEx");
    qiqi::QiQiConfig cfg = qiqi::LoadConfig(instance);
    if (qiqi::ShowSettingsDialog(instance, nullptr, cfg, true)) {
        if (!qiqi::BroadcastStandaloneGlobalCompartmentIncrement(qiqi::kGuidCompartmentConfigVersion)) {
            qiqi::ReportSettingsNotificationFailure(nullptr, cfg.ui_language);
        }
    }
    qiqi::log::Shutdown();
    return 0;
}
