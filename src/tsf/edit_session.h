#pragma once
#include <windows.h>
#include <msctf.h>
#include <functional>
#include <atomic>

#include "common/guids.h"

namespace qiqi {

// Generic modern C++ wrapper for ITfEditSession
// TSF requires all document reads/writes and GetTextExt calls to happen inside DoEditSession.
class LambdaEditSession final : public ITfEditSession {
public:
    using Callback = std::function<HRESULT(TfEditCookie)>;

    explicit LambdaEditSession(Callback cb) noexcept : callback_(std::move(cb)) {
        ++g_com_object_count;
    }

    LambdaEditSession(const LambdaEditSession&) = delete;
    LambdaEditSession& operator=(const LambdaEditSession&) = delete;
    LambdaEditSession(LambdaEditSession&&) = delete;
    LambdaEditSession& operator=(LambdaEditSession&&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) {
            return E_INVALIDARG;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppvObj = static_cast<ITfEditSession*>(this);
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
            delete this;
        }
        return count;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        return callback_ ? callback_(ec) : E_FAIL;
    }

private:
    ~LambdaEditSession() noexcept {
        --g_com_object_count;
    }

    std::atomic<ULONG> ref_count_{1};
    Callback callback_;
};

} // namespace qiqi
