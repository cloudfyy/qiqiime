// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base.h"

namespace qiqi::test {

// Minimal mock ITfRange
class MockTfRange final : public ITfRange {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfRange)) {
            *ppvObj = static_cast<ITfRange*>(this);
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
            if (release_observer_) release_observer_();
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) noexcept override { return S_OK; }
    STDMETHODIMP SetText(TfEditCookie ec, DWORD flags, const WCHAR* pch, LONG cch) override {
        ++set_text_calls_;
        last_set_text_cookie_ = ec;
        last_set_text_flags_ = flags;
        last_set_text_length_ = cch;
        if (SUCCEEDED(set_text_result_) && pch && cch >= 0) {
            text_.assign(pch, gsl::narrow_cast<size_t>(cch));
        }
        if (set_text_callback_) set_text_callback_();
        return set_text_result_;
    }
    STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) noexcept override { return S_OK; }
    STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP IsEmpty(TfEditCookie, BOOL* pfEmpty) noexcept override {
        if (pfEmpty) *pfEmpty = text_.empty() ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP Collapse(TfEditCookie ec, TfAnchor anchor) override {
        if (collapse_observer_) collapse_observer_(ec, anchor);
        if (collapse_callback_) collapse_callback_();
        return collapse_result_;
    }
    STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) noexcept override { return S_OK; }
    STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) noexcept override { return S_OK; }
    STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP GetGravity(TfGravity*, TfGravity*) noexcept override { return S_OK; }
    STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) noexcept override { return S_OK; }
    STDMETHODIMP Clone(ITfRange** ppClone) override {
        if (!ppClone) return E_INVALIDARG;
        ++clone_calls_;
        *ppClone = nullptr;
        if (SUCCEEDED(clone_result_) && !return_null_clone_) {
            auto clone = MakeComObject<MockTfRange>();
            clone->text_ = text_;
            clone->collapse_callback_ = collapse_callback_;
            clone->collapse_observer_ = collapse_observer_;
            clone->collapse_result_ = collapse_result_;
            *ppClone = clone.Detach();
        }
        if (clone_callback_) clone_callback_();
        return clone_result_;
    }
    STDMETHODIMP GetContext(ITfContext**) noexcept override { return E_NOTIMPL; }

    std::wstring text_;
    unsigned int set_text_calls_ = 0;
    unsigned int clone_calls_ = 0;
    TfEditCookie last_set_text_cookie_ = 0;
    DWORD last_set_text_flags_ = 0;
    LONG last_set_text_length_ = 0;
    HRESULT set_text_result_ = S_OK;
    HRESULT clone_result_ = S_OK;
    HRESULT collapse_result_ = S_OK;
    bool return_null_clone_ = false;
    std::function<void()> set_text_callback_;
    std::function<void()> clone_callback_;
    std::function<void()> collapse_callback_;
    std::function<void(TfEditCookie, TfAnchor)> collapse_observer_;
    std::move_only_function<void() noexcept> release_observer_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfComposition
class MockTfComposition final : public ITfComposition {
public:
    explicit MockTfComposition(ComPtr<MockTfRange> range, ITfCompositionSink* sink = nullptr) noexcept
        : range_(std::move(range)), sink_(sink) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfComposition)) {
            *ppvObj = static_cast<ITfComposition*>(this);
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
            if (release_observer_) release_observer_();
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetRange(ITfRange** ppRange) override {
        ++get_range_calls_;
        if (get_range_observer_) get_range_observer_(ppRange);
        if (!ppRange || fail_get_range_) return E_FAIL;
        if (get_range_callback_) get_range_callback_();
        if (return_null_range_) {
            *ppRange = nullptr;
            return get_range_result_;
        }
        *ppRange = range_.Get();
        (*ppRange)->AddRef();
        return get_range_result_;
    }
    STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) noexcept override { return S_OK; }
    STDMETHODIMP EndComposition(TfEditCookie ec) override {
        ++end_composition_calls_;
        last_end_cookie_ = ec;
        if (end_callback_) end_callback_(ec);
        if (FAILED(end_result_)) return end_result_;
        ended_ = true;
        sink_.Reset();
        return S_OK;
    }
    HRESULT NotifyHostTermination(TfEditCookie ec) {
        ended_ = true;
        const auto sink = std::move(sink_);
        return sink ? sink->OnCompositionTerminated(ec, this) : S_OK;
    }

    ComPtr<MockTfRange> range_;
    bool ended_ = false;
    bool fail_get_range_ = false;
    bool return_null_range_ = false;
    HRESULT get_range_result_ = S_OK;
    unsigned int get_range_calls_ = 0;
    std::function<void(ITfRange**)> get_range_observer_;
    unsigned int end_composition_calls_ = 0;
    TfEditCookie last_end_cookie_ = 0;
    HRESULT end_result_ = S_OK;
    std::function<void(TfEditCookie)> end_callback_;
    std::function<void()> get_range_callback_;
    std::move_only_function<void() noexcept> release_observer_;
private:
    ComPtr<ITfCompositionSink> sink_;
    ULONG ref_ = 1;
};

// Minimal mock ITfContextView

} // namespace qiqi::test
