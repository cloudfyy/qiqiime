#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "ui/settings_dialog.h"

#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>

namespace qiqi {

HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};

} // namespace qiqi

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE /*prev_instance*/,
    _In_ LPWSTR /*cmd_line*/,
    _In_ int /*show_cmd*/) {
    // Single-instance check: if the settings dialog is already open, bring it to the foreground.
    const qiqi::ScopedKernelHandle single_instance_mutex(
        ::CreateMutexW(nullptr, TRUE, L"Local\\QiQiIMESettingsSingleInstanceMutex"));
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND existing = ::FindWindowW(qiqi::kSettingsWndClass, nullptr)) {
            if (::IsIconic(existing)) {
                ::ShowWindow(existing, SW_RESTORE);
            }
            ::SetForegroundWindow(existing);
        }
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
    qiqi::QiQiConfig cfg = qiqi::LoadConfig(instance);
    if (qiqi::ShowSettingsDialog(instance, nullptr, cfg)) {
        qiqi::SaveConfig(cfg);
        qiqi::BroadcastStandaloneGlobalCompartmentIncrement(qiqi::kGuidCompartmentConfigVersion);
    }
    qiqi::log::Shutdown();
    return 0;
}
