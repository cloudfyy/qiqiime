// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "context.h"

namespace qiqi::test {

class MockTfDocumentMgr final : public ComObjectBase<MockTfDocumentMgr, ITfDocumentMgr> {
public:
    // The weak parent link is allocated and can fail with std::bad_alloc.
#pragma warning(suppress : 26455)
    MockTfDocumentMgr() : link_(std::make_shared<MockDocumentLink>()) { link_->document = this; }

    ComPtr<MockTfContext> CreateTestContext(const std::shared_ptr<MockEditSessionQueue>& queue = {}) {
        auto context = MakeComObject<MockTfContext>(queue);
        context->document_link_ = link_;
        return context;
    }

    STDMETHODIMP CreateContext(TfClientId, DWORD, IUnknown*, ITfContext** context, TfEditCookie* cookie) override {
        if (!context || !cookie) return E_INVALIDARG;
        *context = CreateTestContext().Detach();
        *cookie = 1;
        return S_OK;
    }
    STDMETHODIMP Push(ITfContext* context) override {
        if (!context) return E_INVALIDARG;
        if (popping_) return E_UNEXPECTED;
        if (stack_.size() == 2) return TF_E_STACKFULL;
        ComPtr<ITfDocumentMgr> owner;
        const HRESULT hr = context->GetDocumentMgr(&owner);
        if (FAILED(hr)) return hr;
        if (owner.Get() != this) return E_INVALIDARG;
        stack_.emplace_back(context);
        if (events && stack_.size() == 1) {
            const HRESULT init_hr = events->OnInitDocumentMgr(this);
            if (FAILED(init_hr)) return init_hr;
        }
        return events ? events->OnPushContext(context) : S_OK;
    }
    STDMETHODIMP Pop(DWORD flags) override {
        if (flags != 0 && flags != TF_POPF_ALL) return E_INVALIDARG;
        if (popping_) return E_UNEXPECTED;
        if (stack_.empty() || (flags == 0 && stack_.size() == 1)) return E_FAIL;
        popping_ = true;
        const auto finish = gsl::finally([this]() noexcept { popping_ = false; });
        const size_t count = flags == TF_POPF_ALL ? stack_.size() : 1;
        for (size_t i = 0; i < count; ++i) {
            const auto context = std::move(stack_.back());
            stack_.pop_back();
            if (events) {
                const HRESULT hr = events->OnPopContext(context.Get());
                if (FAILED(hr)) return hr;
            }
        }
        return events && stack_.empty() ? events->OnUninitDocumentMgr(this) : S_OK;
    }
    STDMETHODIMP GetTop(ITfContext** context) override {
        if (!context) return E_INVALIDARG;
        *context = nullptr;
        ++get_top_calls;
        if (FAILED(get_top_result)) return get_top_result;
        if (get_top_callback) get_top_callback();
        if (!stack_.empty()) {
            *context = stack_.back().Get();
            (*context)->AddRef();
        }
        return get_top_result;
    }
    STDMETHODIMP GetBase(ITfContext** context) override {
        if (!context) return E_INVALIDARG;
        *context = nullptr;
        ++get_base_calls;
        if (FAILED(get_base_result)) return get_base_result;
        if (get_base_callback) get_base_callback();
        if (!stack_.empty()) {
            *context = stack_.front().Get();
            (*context)->AddRef();
        }
        return get_base_result;
    }
    STDMETHODIMP EnumContexts(IEnumTfContexts** contexts) noexcept override {
        if (contexts) *contexts = nullptr;
        return E_NOTIMPL;
    }

    // Non-owning observer: tests keep the sink alive and disconnect it before teardown.
    ITfThreadMgrEventSink* events = nullptr;
    HRESULT get_top_result = S_OK;
    HRESULT get_base_result = S_OK;
    unsigned int get_top_calls = 0;
    unsigned int get_base_calls = 0;
    std::function<void()> get_top_callback;
    std::function<void()> get_base_callback;

private:
    const std::shared_ptr<MockDocumentLink> link_;
    std::vector<ComPtr<ITfContext>> stack_;
    bool popping_ = false;
};

class MockTfDocumentMgrAlias final : public ComObjectBase<MockTfDocumentMgrAlias, ITfDocumentMgr> {
public:
    explicit MockTfDocumentMgrAlias(gsl::not_null<ITfDocumentMgr*> document) noexcept : document_(document.get()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) noexcept override {
        if (IsEqualIID(riid, IID_IUnknown)) {
            if (!object) return E_INVALIDARG;
            *object = nullptr;
            if (identity_observer) identity_observer();
            if (FAILED(identity_result)) return identity_result;
#pragma warning(suppress : 26447) // COM identity queries report failure through HRESULT.
            return document_->QueryInterface(riid, object);
        }
        return ComObjectBase<MockTfDocumentMgrAlias, ITfDocumentMgr>::QueryInterface(riid, object);
    }
    STDMETHODIMP CreateContext(TfClientId client, DWORD flags, IUnknown* owner,
                              ITfContext** context, TfEditCookie* cookie) override {
        return document_->CreateContext(client, flags, owner, context, cookie);
    }
    STDMETHODIMP Push(ITfContext* context) override { return document_->Push(context); }
    STDMETHODIMP Pop(DWORD flags) override { return document_->Pop(flags); }
    STDMETHODIMP GetTop(ITfContext** context) override { return document_->GetTop(context); }
    STDMETHODIMP GetBase(ITfContext** context) override { return document_->GetBase(context); }
    STDMETHODIMP EnumContexts(IEnumTfContexts** contexts) override { return document_->EnumContexts(contexts); }

    HRESULT identity_result = S_OK;
    std::move_only_function<void() noexcept> identity_observer;
private:
    const ComPtr<ITfDocumentMgr> document_;
};

} // namespace qiqi::test
