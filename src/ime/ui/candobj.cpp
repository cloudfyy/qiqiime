// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/guids.h"
#include "ime/fsm/fsm.h"
#include <algorithm>

namespace qiqi {
std::wstring_view StripCodeAnnotation(std::wstring_view text) noexcept {
    if (text.size() < 3 || text.back() != L')') return text;
    const size_t open = text.rfind(L'(');
    if (open == std::wstring_view::npos || open == 0) return text;
    std::wstring_view code = text.substr(open + 1, text.size() - open - 2);
    if (code.starts_with(L'~')) code.remove_prefix(1);
    if (code.empty() || code.size() > 4) return text;
    if (!std::ranges::all_of(code, [](wchar_t ch) noexcept {
            return ch >= L'a' && ch <= L'y';
        })) {
        return text;
    }
    return text.substr(0, open);
}

CandidateUI::CandidateUI(HINSTANCE instance) noexcept : instance_(instance) {
    RefreshAppearance();
    InitDirect2DAndDirectWrite();
    UpdateDpiAndFont(96);
}


STDMETHODIMP CandidateUI::GetDescription(BSTR* pbstrDescription) noexcept {
    return CopyToBstr(L"QiQi Wubi Candidate List", pbstrDescription);
}

STDMETHODIMP CandidateUI::GetGUID(GUID* pguid) noexcept {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = kGuidCandidateUIElement;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* pdwFlags) noexcept {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = ui_element_lifecycle_.UpdatedFlags();
    if (searchbox_integration_) {
        *pdwFlags &= ~TF_CLUIE_PAGEINDEX;
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** ppdim) noexcept {
    if (!ppdim) {
        return E_INVALIDARG;
    }
    *ppdim = nullptr;
    if (!ui_element_lifecycle_.DocumentManager()) {
        return E_FAIL;
    }
    *ppdim = ui_element_lifecycle_.DocumentManager();
#pragma warning(suppress : 26447)
    (*ppdim)->AddRef();
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCount(UINT* puCount) noexcept {
    if (!puCount) {
        return E_INVALIDARG;
    }
    *puCount = gsl::narrow_cast<UINT>(CandidateCountForTsf());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* puIndex) noexcept {
    if (!puIndex) {
        return E_INVALIDARG;
    }
    if (all_candidates_.empty()) {
        return S_FALSE;
    }
    const size_t exposed_count = CandidateCountForTsf();
    if (exposed_count == 0) {
        return S_FALSE;
    }
    const size_t selected_index = searchbox_integration_
        ? std::min(selected_in_page_, exposed_count - 1)
        : std::min(current_page_ * page_size_ + selected_in_page_, all_candidates_.size() - 1);
    *puIndex = gsl::narrow_cast<UINT>(selected_index);
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT uIndex, BSTR* pstr) noexcept {
    if (!pstr) {
        return E_INVALIDARG;
    }
    *pstr = nullptr;
    const auto& candidates =
        searchbox_integration_ &&
                integrated_candidates_.size() == all_candidates_.size()
        ? integrated_candidates_
        : all_candidates_;
    if (uIndex >= CandidateCountForTsf()) {
        return E_INVALIDARG;
    }
    const size_t candidate_index = CandidateIndexForTsf(uIndex);
    if (candidate_index >= candidates.size()) {
        return E_INVALIDARG;
    }
    return CopyToBstr(gsl::at(candidates, candidate_index), pstr);
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) noexcept {
    if (!puPageCnt) {
        return E_INVALIDARG;
    }
    const size_t total_pages = searchbox_integration_
        ? 1
        : ComputeTotalPages(all_candidates_.size(), page_size_);
    *puPageCnt = gsl::narrow_cast<UINT>(total_pages);
    if (!pIndex) {
        return S_OK;
    }
    const std::span<UINT> out_indices(pIndex, uSize);
    const size_t count_to_copy = std::min<size_t>(out_indices.size(), total_pages);
    for (size_t i = 0; i < count_to_copy; ++i) {
        gsl::at(out_indices, i) = gsl::narrow_cast<UINT>(i * page_size_);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT* /*pIndex*/, UINT /*uPageCnt*/) noexcept {
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* puPage) noexcept {
    if (!puPage) {
        return E_INVALIDARG;
    }
    *puPage = searchbox_integration_ ? 0 : gsl::narrow_cast<UINT>(current_page_);
    return S_OK;
}

std::pair<size_t, size_t> CandidateUI::CandidateRangeForTsf() const noexcept {
    return searchbox_integration_
        ? ComputePageRange(all_candidates_.size(), page_size_, current_page_)
        : std::pair<size_t, size_t>{0, all_candidates_.size()};
}

size_t CandidateUI::CandidateIndexForTsf(size_t index) const noexcept {
    return CandidateRangeForTsf().first + index;
}

size_t CandidateUI::CandidateCountForTsf() const noexcept {
    const auto [begin, end] = CandidateRangeForTsf();
    return end - begin;
}

void CandidateUI::SetCandidates(std::wstring code,
                                std::vector<std::wstring> all_candidates,
                                size_t page_size,
                                size_t current_page,
                                size_t selected_in_page,
                                ITfDocumentMgr* document_manager,
                                const std::vector<std::wstring>* raw_candidates) {
    const size_t effective_page_size = page_size > 0 ? page_size : 5;
    const size_t old_page_count = ComputeTotalPages(all_candidates_.size(), page_size_);
    const size_t old_current_page = current_page_;
    const bool old_previous_page_enabled = IsPageTurnAvailable(false);
    const bool old_next_page_enabled = IsPageTurnAvailable(true);
    const size_t new_page_count = ComputeTotalPages(all_candidates.size(), effective_page_size);
    const size_t previous_global_selection =
        current_page_ * page_size_ + selected_in_page_;
    const size_t new_global_selection =
        current_page * effective_page_size + selected_in_page;
    const bool previous_had_selection = !all_candidates_.empty();
    const bool new_had_selection = !all_candidates.empty();
    std::vector<std::wstring> integrated_candidates;
    integrated_candidates.reserve(all_candidates.size());
    if (raw_candidates &&
        raw_candidates->size() == all_candidates.size()) {
        integrated_candidates = *raw_candidates;
    } else {
        for (const auto& candidate : all_candidates) {
            integrated_candidates.emplace_back(
                StripCodeAnnotation(candidate));
        }
    }
    const bool integrated_strings_changed =
        integrated_candidates_ != integrated_candidates;
    const bool candidate_tree_changed =
        all_candidates_ != all_candidates ||
        page_size_ != effective_page_size ||
        current_page_ != current_page;
    const bool document_changed =
        ui_element_lifecycle_.DocumentManager() != document_manager;
    const bool count_changed = all_candidates_.size() != all_candidates.size();
    const bool strings_changed =
        all_candidates_ != all_candidates || integrated_strings_changed;
    const bool page_index_changed =
        page_size_ != effective_page_size || old_page_count != new_page_count;
    const bool page_changed = current_page_ != current_page;
    const bool selection_changed =
        previous_had_selection != new_had_selection ||
        (new_had_selection &&
         std::min(previous_global_selection, all_candidates_.empty()
             ? 0 : all_candidates_.size() - 1) !=
         std::min(new_global_selection, all_candidates.empty()
             ? 0 : all_candidates.size() - 1));
    raw_code_ = std::move(code);
    all_candidates_ = std::move(all_candidates);
    integrated_candidates_ = std::move(integrated_candidates);
    page_size_ = effective_page_size;
    current_page_ = current_page;
    selected_in_page_ = selected_in_page;
    ui_element_lifecycle_.SetDocumentManager(document_manager);
    if (candidate_tree_changed) {
        ++accessibility_tree_revision_;
    }
    DWORD changed_flags = 0;
    if (document_changed) changed_flags |= TF_CLUIE_DOCUMENTMGR;
    if (count_changed) changed_flags |= TF_CLUIE_COUNT | TF_CLUIE_PAGEINDEX;
    if (selection_changed) changed_flags |= TF_CLUIE_SELECTION;
    if (strings_changed) changed_flags |= TF_CLUIE_STRING;
    if (page_index_changed) changed_flags |= TF_CLUIE_PAGEINDEX;
    if (page_changed) changed_flags |= TF_CLUIE_CURRENTPAGE;
    ui_element_lifecycle_.MarkCandidateUpdate(changed_flags);

    RefreshPopupLayout();
    if (is_shown_ && hwnd_ && candidate_tree_changed) {
        std::ignore = RaiseAccessibilityStructureChanged();
    }
    if (is_shown_ && hwnd_) {
        std::ignore = RaiseAccessibilityPageChanged(
            old_current_page,
            old_page_count,
            old_previous_page_enabled,
            old_next_page_enabled);
    }
    if (is_shown_ && hwnd_ && AccessibleCandidateCount() > 0) {
        if (candidate_tree_changed || selection_changed) {
            std::ignore = RaiseAccessibilityEvent(
                UIA_SelectionItem_ElementSelectedEventId, SelectedPageIndex());
        }
    }
}


void CandidateUI::SetLayout(CandidateLayout layout) {
    if (layout_ == layout) return;
    layout_ = layout;
    RefreshPopupLayout();
}

void CandidateUI::SetDisplayPreferences(DisplayPreferences preferences) {
    const ComPtr<CandidateUI> keep_alive(this);
    if (preferences_ == preferences) return;
    preferences_ = preferences;
    RefreshAppearance();
    RefreshPopupLayout();
    if (hwnd_) ApplyDisplayThemeToWindow(hwnd_.Get(), appearance_);
}

void CandidateUI::SetFontPreferences(const CandidateFontPreferences& preferences) {
    const ComPtr<CandidateUI> keep_alive(this);
    if (font_preferences_ == preferences) return;
    font_preferences_ = preferences;
    font_.Reset();
    dwrite_text_format_.Reset();
    RefreshPopupLayout();
}

void CandidateUI::RefreshAppearance() noexcept {
    const auto appearance = ResolveImeSkinAppearance(preferences_, skin_snapshot_);
    appearance_ = appearance.display;
    skin_colors_ = appearance.colors;
}

void CandidateUI::SetSkinSnapshot(const std::optional<ImeSkinSnapshot>& snapshot) noexcept {
    const ComPtr<CandidateUI> keep_alive(this);
    if (skin_snapshot_ == snapshot) return;
    skin_snapshot_ = snapshot;
    RefreshAppearance();
    if (hwnd_) {
        ApplyDisplayThemeToWindow(hwnd_.Get(), appearance_);
        ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
    }
}

void CandidateUI::SetUiLanguage(UiLanguage lang) {
    if (ui_language_ == lang) return;
    ui_language_ = lang;
    RefreshPopupLayout();
    if (is_shown_ && hwnd_) {
        std::ignore = RaiseAccessibilityStructureChanged();
    }
}

} // namespace qiqi
