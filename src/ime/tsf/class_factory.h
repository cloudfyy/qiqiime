#pragma once
#include <windows.h>
#include <unknwn.h>

namespace qiqi {

class QiQiClassFactory final : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override;
    STDMETHODIMP_(ULONG) Release() noexcept override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP LockServer(BOOL fLock) noexcept override;
};

} // namespace qiqi
