#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <atomic>
#include <utility>

#include "common/guids.h"

namespace qiqi {

// Construct a reference-counted COM object (whose initial ref_count is 1) and attach it to a ComPtr<T>.
template <typename T, typename... Args>
Microsoft::WRL::ComPtr<T> MakeComObject(Args&&... args) {
    Microsoft::WRL::ComPtr<T> ptr;
#pragma warning(suppress : 26409)
    ptr.Attach(new T(std::forward<Args>(args)...));
    return ptr;
}

// Allocate a reference-counted COM object via std::nothrow + ComPtr RAII and detach it into a COM [out] parameter.
template <typename T, typename OutInterface, typename... Args>
HRESULT CreateComObjectToOut(OutInterface** ppOut, Args&&... args) noexcept {
    if (!ppOut) {
        return E_INVALIDARG;
    }
    *ppOut = nullptr;
    Microsoft::WRL::ComPtr<T> obj;
#pragma warning(suppress : 26409)
    obj.Attach(new (std::nothrow) T(std::forward<Args>(args)...));
    if (!obj) {
        return E_OUTOFMEMORY;
    }
    *ppOut = static_cast<OutInterface*>(obj.Detach());
    return S_OK;
}

// CRTP base template for in-proc COM objects:
// Provides thread-safe reference counting (AddRef/Release), automatic g_com_object_count tracking
// for DllCanUnloadNow, and variadic fold-expression QueryInterface across all supported COM interfaces.
// Note: If an ExtraInterface is a base class of PrimaryInterface (e.g. ITfUIElement is a base of
// ITfCandidateListUIElement), it is not inherited a second time to avoid diamond base ambiguity.
template <typename Derived, typename PrimaryInterface, typename... ExtraInterfaces>
class ComObjectBase : public PrimaryInterface, public ExtraInterfaces... {
public:
    ComObjectBase() noexcept {
        ++g_com_object_count;
    }

#pragma warning(push)
#pragma warning(disable : 26456)
    ComObjectBase(const ComObjectBase&) = delete;
    ComObjectBase& operator=(const ComObjectBase&) = delete;
    ComObjectBase(ComObjectBase&&) = delete;
    ComObjectBase& operator=(ComObjectBase&&) = delete;
#pragma warning(pop)

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) {
            return E_INVALIDARG;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(PrimaryInterface))) {
            *ppvObj = static_cast<PrimaryInterface*>(static_cast<Derived*>(this));
            AddRef();
            return S_OK;
        }
        if (TryMatchInterfaces<ExtraInterfaces...>(riid, ppvObj) ||
            static_cast<Derived*>(this)->QueryAdditionalInterfaces(riid, ppvObj)) {
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() noexcept override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete static_cast<Derived*>(this);
        }
        return count;
    }

protected:
    ~ComObjectBase() noexcept {
        --g_com_object_count;
    }

    // Derived classes can optionally expose base interfaces of PrimaryInterface (such as ITfUIElement,
    // ITfLangBarItem, ITfTextInputProcessor, ITfFunction) without duplicate inheritance.
    bool QueryAdditionalInterfaces(REFIID /*riid*/, void** /*ppvObj*/) noexcept {
        return false;
    }

    template <typename CastInterface, typename... BaseAliases>
    bool MatchBaseInterfaces(REFIID riid, void** ppvObj) noexcept {
        if (!ppvObj) {
            return false;
        }
        if ((IsEqualIID(riid, __uuidof(BaseAliases)) || ...)) {
            *ppvObj = static_cast<CastInterface*>(static_cast<Derived*>(this));
            return true;
        }
        return false;
    }

private:
    template <typename... Ifaces>
    bool TryMatchInterfaces(REFIID riid, void** ppvObj) noexcept {
        if (!ppvObj) {
            return false;
        }
        if constexpr (sizeof...(Ifaces) == 0) {
            return false;
        } else {
            bool matched = false;
            ((!matched && IsEqualIID(riid, __uuidof(Ifaces))
                  ? (*ppvObj = static_cast<Ifaces*>(static_cast<Derived*>(this)), matched = true)
                  : false),
             ...);
            return matched;
        }
    }

    std::atomic<ULONG> ref_count_{1};
};

} // namespace qiqi
