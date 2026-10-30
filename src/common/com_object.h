#pragma once
#include <windows.h>
#include <msctf.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <atomic>
#include <concepts>
#include <string_view>
#include <utility>

#include "common/guids.h"
#include "common/scoped_handle.h"

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
    *ppOut = obj.Detach();
    return S_OK;
}

namespace detail {
template <typename T>
concept HasQueryAdditionalInterfaces = requires(T& obj, REFIID riid, void** ppvObj) {
    { obj.QueryAdditionalInterfaces(riid, ppvObj) } -> std::same_as<bool>;
};
} // namespace detail

// CRTP base template for in-proc COM objects:
// Provides thread-safe reference counting (AddRef/Release), automatic g_com_object_count tracking
// for DllCanUnloadNow, and variadic fold-expression QueryInterface across all supported COM interfaces.
// Note: If an ExtraInterface is a base class of PrimaryInterface (e.g. ITfUIElement is a base of
// ITfCandidateListUIElement), it is not inherited a second time to avoid diamond base ambiguity.
#pragma warning(push)
#pragma warning(disable : 26456 26466)
template <typename Derived, typename PrimaryInterface, typename... ExtraInterfaces>
class ComObjectBase : public PrimaryInterface, public ExtraInterfaces... {
public:
    ComObjectBase() noexcept {
        ++g_com_object_count;
    }

    ComObjectBase(const ComObjectBase&) = delete;
    ComObjectBase& operator=(const ComObjectBase&) = delete;
    ComObjectBase(ComObjectBase&&) = delete;
    ComObjectBase& operator=(ComObjectBase&&) = delete;

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
            TryDerivedAdditionalInterfaces(riid, ppvObj)) {
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
    bool TryDerivedAdditionalInterfaces(REFIID riid, void** ppvObj) noexcept {
        if constexpr (detail::HasQueryAdditionalInterfaces<Derived>) {
            return static_cast<Derived*>(this)->QueryAdditionalInterfaces(riid, ppvObj);
        } else {
            (void)riid;
            (void)ppvObj;
            return false;
        }
    }

    template <typename... Ifaces>
    bool TryMatchInterfaces(REFIID riid, void** ppvObj) noexcept {
        if constexpr (sizeof...(Ifaces) == 0) {
            (void)riid;
            return false;
        } else {
            if (!ppvObj) {
                return false;
            }
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
#pragma warning(pop)

// Allocate a BSTR of known length via ::SysAllocStringLen using ScopedBstr RAII and detach to [out] BSTR*.
// Always initializes *pbstr = nullptr per COM [out] parameter rules.
inline HRESULT CopyToBstr(std::wstring_view sv, BSTR* pbstr) noexcept {
    if (!pbstr) {
        return E_INVALIDARG;
    }
    *pbstr = nullptr;
    ScopedBstr bstr(::SysAllocStringLen(sv.data(), gsl::narrow_cast<UINT>(sv.size())));
    if (!bstr) {
        return E_OUTOFMEMORY;
    }
    *pbstr = bstr.Detach();
    return S_OK;
}

// Construct a VT_I4 Win32 VARIANT for TSF compartment / property calls.
constexpr VARIANT MakeInt4Variant(LONG val) noexcept {
#pragma warning(suppress : 26476)
    VARIANT var{};
    var.vt = VT_I4;
#pragma warning(suppress : 26476)
    var.lVal = val;
    return var;
}

#pragma warning(push)
#pragma warning(disable : 26447 26476)
// Read a VT_I4 LONG value from a TSF ITfCompartment, returning default_val if unset or failed.
inline LONG ReadCompartmentInt4(ITfCompartment* comp, LONG default_val = 0) noexcept {
    if (!comp) {
        return default_val;
    }
    VARIANT var{};
    if (SUCCEEDED(comp->GetValue(&var)) && var.vt == VT_I4) {
        return var.lVal;
    }
    return default_val;
}

// Write a VT_I4 LONG value into a TSF ITfCompartment.
inline HRESULT WriteCompartmentInt4(ITfCompartment* comp, TfClientId tid, LONG val) noexcept {
    if (!comp) {
        return E_INVALIDARG;
    }
    const VARIANT var = MakeInt4Variant(val);
    return comp->SetValue(tid, &var);
}

// RAII wrapper for ITfSource::AdviseSink / UnadviseSink cookie lifetime management.
class ScopedTfSinkConnection final {
public:
    ScopedTfSinkConnection() noexcept = default;
    ~ScopedTfSinkConnection() noexcept { Reset(); }

    ScopedTfSinkConnection(const ScopedTfSinkConnection&) = delete;
    ScopedTfSinkConnection& operator=(const ScopedTfSinkConnection&) = delete;
    ScopedTfSinkConnection(ScopedTfSinkConnection&&) = delete;
    ScopedTfSinkConnection& operator=(ScopedTfSinkConnection&&) = delete;

    bool Advise(IUnknown* source_owner, REFIID riid, IUnknown* sink) noexcept {
        Reset();
        if (!source_owner || !sink) {
            return false;
        }
        Microsoft::WRL::ComPtr<ITfSource> src;
        if (FAILED(source_owner->QueryInterface(IID_PPV_ARGS(&src))) || !src) {
            return false;
        }
        DWORD cookie = TF_INVALID_COOKIE;
        if (FAILED(src->AdviseSink(riid, sink, &cookie)) || cookie == TF_INVALID_COOKIE) {
            return false;
        }
        source_ = std::move(src);
        cookie_ = cookie;
        return true;
    }

    void Reset() noexcept {
        if (source_ && cookie_ != TF_INVALID_COOKIE) {
            source_->UnadviseSink(cookie_);
        }
        source_.Reset();
        cookie_ = TF_INVALID_COOKIE;
    }

    bool IsConnected() const noexcept { return source_ != nullptr && cookie_ != TF_INVALID_COOKIE; }

private:
    Microsoft::WRL::ComPtr<ITfSource> source_;
    DWORD cookie_ = TF_INVALID_COOKIE;
};

// RAII wrapper for ITfCompartment + optional ITfCompartmentEventSink subscription.
class ScopedTfCompartment final {
public:
    ScopedTfCompartment() noexcept = default;
    ~ScopedTfCompartment() noexcept = default;

    ScopedTfCompartment(const ScopedTfCompartment&) = delete;
    ScopedTfCompartment& operator=(const ScopedTfCompartment&) = delete;
    ScopedTfCompartment(ScopedTfCompartment&&) = default;
    ScopedTfCompartment& operator=(ScopedTfCompartment&&) = default;

    bool Bind(ITfCompartmentMgr* mgr, REFGUID guid, ITfCompartmentEventSink* sink = nullptr) noexcept {
        Reset();
        if (!mgr) {
            return false;
        }
        if (FAILED(mgr->GetCompartment(guid, &comp_)) || !comp_) {
            return false;
        }
        if (sink) {
            return sink_conn_.Advise(comp_.Get(), IID_ITfCompartmentEventSink, sink);
        }
        return true;
    }

    void Reset() noexcept {
        sink_conn_.Reset();
        comp_.Reset();
    }

    LONG ReadInt4(LONG default_val = 0) const noexcept {
        return ReadCompartmentInt4(comp_.Get(), default_val);
    }

    HRESULT WriteInt4(TfClientId tid, LONG val) const noexcept {
        return WriteCompartmentInt4(comp_.Get(), tid, val);
    }

    ITfCompartment* Get() const noexcept { return comp_.Get(); }
    bool IsValid() const noexcept { return comp_ != nullptr; }
    explicit operator bool() const noexcept { return IsValid(); }

private:
    Microsoft::WRL::ComPtr<ITfCompartment> comp_;
    ScopedTfSinkConnection sink_conn_;
};
#pragma warning(pop)

} // namespace qiqi
