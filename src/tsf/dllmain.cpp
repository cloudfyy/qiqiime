#include <windows.h>
#include "common/guids.h"
#include "tsf/class_factory.h"
#include "tsf/register.h"
#include "tsf/tip_service.h"

namespace qiqi {
HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};
static QiQiClassFactory g_class_factory;
} // namespace qiqi

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        qiqi::g_dll_instance = hModule;
        ::DisableThreadLibraryCalls(hModule);
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
    return qiqi::RegisterServer(qiqi::g_dll_instance);
}

STDAPI DllUnregisterServer(void) {
    return qiqi::UnregisterServer();
}
