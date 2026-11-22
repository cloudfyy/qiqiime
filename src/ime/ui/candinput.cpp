// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/logger.h"
#include "ime/fsm/fsm.h"
#include <exception>

namespace qiqi {
namespace {
const char* ExceptionMessageNoThrow(const std::exception& error) noexcept {
    try {
        return error.what();
    } catch (...) {
        return "exception message unavailable";
    }
}
} // namespace

void CandidateUI::OnLeftButtonDown(POINT pt) {
    pt.y += scroll_offset_;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    if (total_pages > 1 && page_turn_callback_) {
        if (current_page_ > 0 && ::PtInRect(&prev_page_hit_rect_, pt)) {
            std::ignore = InvokePageTurnCallback(false);
            return;
        }
        if (current_page_ + 1 < total_pages && ::PtInRect(&next_page_hit_rect_, pt)) {
            std::ignore = InvokePageTurnCallback(true);
            return;
        }
    }
    for (size_t i = 0; i < candidate_hit_rects_.size(); ++i) {
        if (::PtInRect(&gsl::at(candidate_hit_rects_, i), pt)) {
            std::ignore = InvokeSelectionCallback(i);
            return;
        }
    }
}

HRESULT CandidateUI::InvokeSelectionCallback(size_t index) noexcept {
    if (index >= AccessibleCandidateCount()) return E_INVALIDARG;
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = select_callback_;
        if (!callback) return E_NOTIMPL;
        callback(index);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI::InvokeSelectionCallback allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    } catch (const std::exception& exception) {
        QIQI_LOG_ERROR(
            "CandidateUI::InvokeSelectionCallback failed (error={})",
            ExceptionMessageNoThrow(exception));
        return E_FAIL;
    } catch (...) {
        QIQI_LOG_ERROR("CandidateUI::InvokeSelectionCallback failed with unknown exception");
        return E_FAIL;
    }
}

bool CandidateUI::IsPageTurnAvailable(bool next_page) const noexcept {
    if (!page_turn_callback_) return false;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    return total_pages > 1 &&
        (next_page ? current_page_ + 1 < total_pages : current_page_ > 0);
}

HRESULT CandidateUI::InvokePageTurnCallback(bool next_page) noexcept {
    if (!IsPageTurnAvailable(next_page)) return UIA_E_ELEMENTNOTENABLED;
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = page_turn_callback_;
        if (!callback) return UIA_E_ELEMENTNOTENABLED;
        callback(next_page);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI::InvokePageTurnCallback allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    } catch (const std::exception& exception) {
        QIQI_LOG_ERROR(
            "CandidateUI::InvokePageTurnCallback failed (error={})",
            ExceptionMessageNoThrow(exception));
        return E_FAIL;
    } catch (...) {
        QIQI_LOG_ERROR("CandidateUI::InvokePageTurnCallback failed with unknown exception");
        return E_FAIL;
    }
}

HRESULT CandidateUI::InvokeActionCallback(
    CandidateUiAction action, size_t index) noexcept {
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = action_callback_;
        if (!callback) return E_NOTIMPL;
        return callback(action, index);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI action callback allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP CandidateUI::SetSelection(UINT index) noexcept {
    if (index >= all_candidates_.size()) return E_INVALIDARG;
    return InvokeActionCallback(CandidateUiAction::SetSelection, index);
}

STDMETHODIMP CandidateUI::Finalize() noexcept {
    return InvokeActionCallback(CandidateUiAction::Finalize, 0);
}

STDMETHODIMP CandidateUI::Abort() noexcept {
    return InvokeActionCallback(CandidateUiAction::Abort, 0);
}

STDMETHODIMP CandidateUI::SetIntegrationStyle(
    GUID integration_style) noexcept {
    const ComPtr<CandidateUI> keep_alive(this);
    searchbox_integration_ =
        IsEqualGUID(integration_style, kGuidIntegrationStyleSearchbox);
    HRESULT hr = E_NOTIMPL;
    if (searchbox_integration_) {
        const HRESULT hide_hr = Show(FALSE);
        hr = FAILED(hide_hr) ? hide_hr : S_OK;
    }
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::UI,
        "SearchIntegration(stage=set-integration-style, element_id={}, style_data1=0x{:08x}, "
        "searchbox={}, shown={}, hr=0x{:08x})",
        UiElementId(), integration_style.Data1, searchbox_integration_,
        is_shown_ != FALSE, gsl::narrow_cast<unsigned int>(hr));
    return hr;
}

STDMETHODIMP CandidateUI::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* selection_style) noexcept {
    if (!selection_style) return E_INVALIDARG;
    *selection_style = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

STDMETHODIMP CandidateUI::OnKeyDown(
    WPARAM wparam, LPARAM lparam, BOOL* eaten) noexcept {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    if (!searchbox_integration_) return E_NOTIMPL;
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = integrated_key_down_callback_;
        if (!callback) return E_NOTIMPL;
        return callback(wparam, lparam, eaten);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI::OnKeyDown allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP CandidateUI::ShowCandidateNumbers(BOOL* show) noexcept {
    if (!show) return E_INVALIDARG;
    *show = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUI::FinalizeExactCompositionString() noexcept {
    return InvokeActionCallback(CandidateUiAction::FinalizeExact, 0);
}


} // namespace qiqi
