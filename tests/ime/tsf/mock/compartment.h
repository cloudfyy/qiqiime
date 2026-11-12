// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base.h"

namespace qiqi::test {

class MockTfCompartment final : public ITfCompartment, public ITfSource {
public:
    explicit MockTfCompartment(REFGUID guid = GUID_NULL) noexcept : guid_(guid) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompartment)) {
            *ppvObj = static_cast<ITfCompartment*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfSource)) {
            *ppvObj = static_cast<ITfSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) {
            delete this;
        }
        return c;
    }
    STDMETHODIMP SetValue(TfClientId, const VARIANT* pvarValue) override {
        ++set_value_calls_;
        if (fail_set_value_) return E_FAIL;
        if (pvarValue && pvarValue->vt == VT_I4) {
            last_val_ = pvarValue->lVal;
            if (set_value_callback_) set_value_callback_();
            if (sink_) {
                sink_->OnChange(guid_);
            }
        }
        return S_OK;
    }
    STDMETHODIMP GetValue(VARIANT* pvarValue) override {
        if (!pvarValue) return E_INVALIDARG;
        if (get_value_result_ != S_OK) {
            ::VariantInit(pvarValue);
            return get_value_result_;
        }
        if (value_vt_ != VT_I4) {
            pvarValue->vt = value_vt_;
            return S_OK;
        }
        pvarValue->vt = VT_I4;
        pvarValue->lVal = last_val_;
        if (get_value_callback_) get_value_callback_();
        return S_OK;
    }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override {
        if (!punk || !pdwCookie) return E_INVALIDARG;
        if (!IsEqualIID(riid, IID_ITfCompartmentEventSink)) return CONNECT_E_CANNOTCONNECT;
        if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&sink_))) || !sink_) return E_NOINTERFACE;
        if (advise_callback_) advise_callback_();
        *pdwCookie = 101;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD) override {
        sink_.Reset();
        return S_OK;
    }

    GUID guid_{};
    LONG last_val_ = 0;
    VARTYPE value_vt_ = VT_I4;
    HRESULT get_value_result_ = S_OK;
    bool fail_set_value_ = false;
    unsigned int set_value_calls_ = 0;
    std::function<void()> advise_callback_;
    std::function<void()> set_value_callback_;
    std::function<void()> get_value_callback_;
    ComPtr<ITfCompartmentEventSink> sink_;
private:
    ULONG ref_ = 1;
};

} // namespace qiqi::test
