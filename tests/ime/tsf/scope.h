// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/support.h"
#include "mocks.h"
#include "ime/comp/policy.h"

#include <inputscope.h>
#include <algorithm>
#include <functional>
#include <span>
#include <vector>

namespace qiqi::test::restricted_detail {

class ScopeOwner final : public ComObjectBase<ScopeOwner, ITfContextOwner, ITfInputScope> {
public:
    STDMETHODIMP GetACPFromPoint(const POINT*, DWORD, LONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(LONG, LONG, RECT*, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetScreenExt(RECT*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetWnd(HWND* window) noexcept override {
        if (!window) return E_INVALIDARG;
        *window = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetStatus(TF_STATUS* status) noexcept override {
        if (!status) return E_INVALIDARG;
        *status = {};
        status->dwDynamicFlags = read_only ? TF_SD_READONLY : 0;
        return S_OK;
    }
    STDMETHODIMP GetAttribute(REFGUID guid, VARIANT* value) noexcept override {
        if (!value) return E_INVALIDARG;
        ::VariantInit(value);
        if (has_scope && IsEqualGUID(guid, kGuidPropInputScope)) {
            value->vt = VT_UNKNOWN;
            ITfInputScope* input_scope = this;
            value->punkVal = input_scope;
            AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP GetInputScopes(InputScope** scopes, UINT* count) noexcept override {
        if (!scopes || !count) return E_INVALIDARG;
        *scopes = nullptr;
        *count = 0;
        auto* const result = static_cast<InputScope*>(::CoTaskMemAlloc(sizeof(scope)));
        if (!result) return E_OUTOFMEMORY;
        *result = scope;
        *scopes = result;
        *count = 1;
        return S_OK;
    }
    STDMETHODIMP GetPhrase(BSTR** phrases, UINT* count) noexcept override {
        if (!phrases || !count) return E_INVALIDARG;
        *phrases = nullptr;
        *count = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetRegularExpression(BSTR* value) noexcept override { return NoString(value); }
    STDMETHODIMP GetSRGS(BSTR* value) noexcept override { return NoString(value); }
    STDMETHODIMP GetXML(BSTR* value) noexcept override { return NoString(value); }

    InputScope scope = IS_DEFAULT;
    bool has_scope = true;
    bool read_only = false;
private:
    static HRESULT NoString(BSTR* value) noexcept {
        if (!value) return E_INVALIDARG;
        *value = nullptr;
        return E_NOTIMPL;
    }
};

class MockInputScopeProperty final
    : public ComObjectBase<MockInputScopeProperty, ITfReadOnlyProperty, ITfInputScope> {
public:
    STDMETHODIMP GetType(GUID* guid) noexcept override {
        if (!guid) return E_INVALIDARG;
        *guid = kGuidPropInputScope;
        return S_OK;
    }
    STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges** ppEnum, ITfRange*) noexcept override {
        if (ppEnum) *ppEnum = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetValue(TfEditCookie, ITfRange*, VARIANT* pvarValue) override {
        if (!pvarValue) return E_INVALIDARG;
        ::VariantInit(pvarValue);
        if (get_value_callback) get_value_callback();
        if (get_value_result != S_OK) {
            return get_value_result;
        }
        if (value_vt == VT_EMPTY) {
            return S_OK;
        }
        if (value_vt != VT_UNKNOWN) {
            pvarValue->vt = value_vt;
            pvarValue->lVal = 1;
            return S_OK;
        }
        if (return_null_unknown) {
            pvarValue->vt = VT_UNKNOWN;
            pvarValue->punkVal = nullptr;
            return S_OK;
        }
        if (fail_qi_input_scope) {
            auto dummy = MakeComObject<MockTfRange>();
            pvarValue->vt = VT_UNKNOWN;
            pvarValue->punkVal = dummy.Detach();
            return S_OK;
        }
        pvarValue->vt = VT_UNKNOWN;
        ITfInputScope* sc = this;
        pvarValue->punkVal = sc;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP GetContext(ITfContext** ppCtx) noexcept override {
        if (ppCtx) *ppCtx = nullptr;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetInputScopes(InputScope** pprgInputScopes, UINT* pcCount) noexcept override {
        if (!pprgInputScopes || !pcCount) return E_INVALIDARG;
        *pprgInputScopes = nullptr;
        *pcCount = 0;
        if (FAILED(get_scopes_result)) {
            return get_scopes_result;
        }
        if (inconsistent_null_buffer_with_count) {
            *pcCount = 2;
            return S_OK;
        }
        if (scopes.empty()) {
            return S_OK;
        }
        const size_t bytes = scopes.size() * sizeof(InputScope);
        auto* const buf = static_cast<InputScope*>(::CoTaskMemAlloc(bytes));
        if (!buf) return E_OUTOFMEMORY;
        const std::span<InputScope> out_span(buf, scopes.size());
        std::copy(scopes.begin(), scopes.end(), out_span.begin());
        *pprgInputScopes = buf;
        *pcCount = gsl::narrow_cast<UINT>(scopes.size());
        return get_scopes_result;
    }
    STDMETHODIMP GetPhrase(BSTR** ppbstrPhrases, UINT* pcCount) noexcept override {
        if (ppbstrPhrases) *ppbstrPhrases = nullptr;
        if (pcCount) *pcCount = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetRegularExpression(BSTR* pbstrRegExp) noexcept override {
        if (pbstrRegExp) *pbstrRegExp = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetSRGS(BSTR* pbstrSRGS) noexcept override {
        if (pbstrSRGS) *pbstrSRGS = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetXML(BSTR* pbstrXML) noexcept override {
        if (pbstrXML) *pbstrXML = nullptr;
        return E_NOTIMPL;
    }

    std::vector<InputScope> scopes{IS_DEFAULT};
    HRESULT get_value_result = S_OK;
    VARTYPE value_vt = VT_UNKNOWN;
    bool return_null_unknown = false;
    bool fail_qi_input_scope = false;
    HRESULT get_scopes_result = S_OK;
    bool inconsistent_null_buffer_with_count = false;
    std::function<void()> get_value_callback;
};

void RunRestrictedInputNativeTests();
void RunRestrictedInputPolicyTests();
void ObserveCurrentService();

} // namespace qiqi::test::restricted_detail
