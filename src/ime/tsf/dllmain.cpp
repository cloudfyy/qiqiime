#include <windows.h>
#include <string_view>
#include "common/guids.h"
#include "common/logger.h"
#include "ime/tsf/class_factory.h"
#include "ime/tsf/register.h"
#include "ime/tsf/tip_service.h"

namespace qiqi {
HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};
static QiQiClassFactory g_class_factory;
} // namespace qiqi

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) noexcept {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        qiqi::g_dll_instance = hModule;
        ::DisableThreadLibraryCalls(hModule);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH && lpReserved == nullptr) {
        // Dynamic unload via FreeLibrary (not process termination):
        // release cached mapped dictionary handles, unregister window classes, and unregister ETW provider safely.
        qiqi::WubiMappedDict::ClearProcessCache();
        ::UnregisterClassW(qiqi::kCandidateWndClass, hModule);
        ::UnregisterClassW(qiqi::kStatusBarWndClass, hModule);
        ::UnregisterClassW(qiqi::kStatusBarTooltipWndClass, hModule);
        ::UnregisterClassW(qiqi::kSettingsWndClass, hModule);
        ::UnregisterClassW(qiqi::kAboutWndClass, hModule);
        ::UnregisterClassW(qiqi::kSettingsMessageWndClass, hModule);
        ::UnregisterClassW(qiqi::kSettingsToastWndClass, hModule);
        ::UnregisterClassW(qiqi::kMenuWndClass, hModule);
        qiqi::log::Shutdown();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, qiqi::kClsidQiQiTextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return qiqi::g_class_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow(void) {
    return (qiqi::g_com_object_count.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void) {
    std::ignore = qiqi::log::EnsureInitialized();
    return qiqi::RegisterServer(qiqi::g_dll_instance);
}

STDAPI DllUnregisterServer(void) {
    std::ignore = qiqi::log::EnsureInitialized();
    return qiqi::UnregisterServer();
}

STDAPI DllInstall(BOOL install, LPCWSTR command_line) {
    std::ignore = qiqi::log::EnsureInitialized();
    if (!command_line || std::wstring_view(command_line) != L"user") {
        QIQI_LOG_ERROR("DllInstall supports only /i:user");
        return E_INVALIDARG;
    }
    return install ? qiqi::EnableCurrentUserProfile() : qiqi::DeactivateCurrentUserProfile();
}
