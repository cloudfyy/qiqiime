#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "common/logger.h"
#include "tools/settings/window.h"

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
