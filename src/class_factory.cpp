#include "class_factory.h"
#include "tip_service.h"

#include <new>

namespace qiqi {

STDMETHODIMP QiQiClassFactory::QueryInterface(REFIID riid, void** ppvObj) {
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

STDMETHODIMP_(ULONG) QiQiClassFactory::AddRef() {
    ++g_com_object_count;
    return 2;
}

STDMETHODIMP_(ULONG) QiQiClassFactory::Release() {
    --g_com_object_count;
    return 1;
}

STDMETHODIMP QiQiClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;
    if (pUnkOuter != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }

    auto* service = new (std::nothrow) QiQiTextService();
    if (!service) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = service->QueryInterface(riid, ppvObj);
    service->Release();
    return hr;
}

STDMETHODIMP QiQiClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        ++g_com_object_count;
    } else {
        --g_com_object_count;
    }
    return S_OK;
}

} // namespace qiqi
