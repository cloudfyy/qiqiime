#include "Ime/tsf/class_factory.h"
#include "common/logger.h"
#include "Ime/tsf/tip_service.h"

#include <wrl/client.h>
#include <new>

namespace qiqi {

using Microsoft::WRL::ComPtr;

STDMETHODIMP QiQiClassFactory::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppvObj = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) QiQiClassFactory::AddRef() noexcept {
    ++g_com_object_count;
    return 2;
}

STDMETHODIMP_(ULONG) QiQiClassFactory::Release() noexcept {
    --g_com_object_count;
    return 1;
}

STDMETHODIMP QiQiClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;
    if (pUnkOuter != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }

    try {
        const ComPtr<QiQiTextService> service = MakeComObject<QiQiTextService>(g_dll_instance);
        return service->QueryInterface(riid, ppvObj);
    } catch (...) {
        QIQI_LOG_CRITICAL("QiQiClassFactory::CreateInstance failed due to unhandled exception / OOM");
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP QiQiClassFactory::LockServer(BOOL fLock) noexcept {
    if (fLock) {
        ++g_com_object_count;
    } else {
        --g_com_object_count;
    }
    return S_OK;
}

} // namespace qiqi
