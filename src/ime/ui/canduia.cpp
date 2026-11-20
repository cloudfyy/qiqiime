// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "ime/fsm/fsm.h"
#include <gsl/gsl>
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <exception>
#include <string>
#include <string_view>

namespace qiqi {
namespace {
constexpr long kPreviousPageRuntimeId = 0x40000000L;
constexpr long kNextPageRuntimeId = kPreviousPageRuntimeId + 1;
constexpr size_t kMaxCandidateRuntimeId =
    static_cast<size_t>(kPreviousPageRuntimeId - 1);

std::wstring FormatPageStatus(
    UiLanguage language, size_t page_index, size_t page_count) {
    if (page_count == 0) return {};
    const size_t displayed_page =
        std::min(page_index, page_count - 1) + 1;
    return ResolveUiLanguage(language) == UiLanguage::EnUs
        ? L"Page " + std::to_wstring(displayed_page) +
            L" of " + std::to_wstring(page_count)
        : L"第 " + std::to_wstring(displayed_page) +
            L" / " + std::to_wstring(page_count) + L" 页";
}

HRESULT MakePageStatusVariant(
    UiLanguage language, size_t page_index, size_t page_count, VARIANT* value) noexcept {
    if (!value) return E_POINTER;
    ::VariantInit(value);
    if (page_count == 0) return S_OK;
    try {
        return MakeBstrVariant(
            FormatPageStatus(language, page_index, page_count), value);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI page status allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI page status formatting failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_FAIL));
        return E_FAIL;
    }
}

HRESULT MakeRuntimeId(HWND hwnd, long child_id, SAFEARRAY** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    const bool has_child = child_id >= 0;
    SAFEARRAYBOUND bound{has_child ? 3ul : 2ul, 0};
    *out = ::SafeArrayCreate(VT_I4, 1, &bound);
    if (!*out) return E_OUTOFMEMORY;
    const std::array<long, 3> indices{
        UiaAppendRuntimeId,
        gsl::narrow_cast<long>(
            reinterpret_cast<uintptr_t>(hwnd) & 0x7fffffff),
        child_id
    };
    for (LONG i = 0; i < gsl::narrow_cast<LONG>(bound.cElements); ++i) {
        LONG index = i;
        long element = gsl::at(indices, gsl::narrow_cast<size_t>(i));
        const HRESULT hr =
            ::SafeArrayPutElement(*out, &index, &element);
        if (FAILED(hr)) {
            ::SafeArrayDestroy(*out);
            *out = nullptr;
            return hr;
        }
    }
    return S_OK;
}

HRESULT PreserveFirstFailure(HRESULT current, HRESULT next) noexcept {
    return SUCCEEDED(current) ? next : current;
}

void SetUiaBounds(const RECT& screen_rect, UiaRect* bounds) noexcept {
    bounds->left = static_cast<double>(screen_rect.left);
    bounds->top = static_cast<double>(screen_rect.top);
    bounds->width =
        static_cast<double>(screen_rect.right - screen_rect.left);
    bounds->height =
        static_cast<double>(screen_rect.bottom - screen_rect.top);
}
} // namespace

template <typename Derived, typename PatternInterface>
class CandidateAutomationFragment : public ComObjectBase<
    Derived,
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    PatternInterface> {
public:
    STDMETHODIMP get_ProviderOptions(ProviderOptions* options) noexcept override {
        if (!options) return E_INVALIDARG;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    STDMETHODIMP get_HostRawElementProvider(
        IRawElementProviderSimple** provider) noexcept override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        return S_OK;
    }

    STDMETHODIMP Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** fragment) noexcept override {
        if (!fragment) return E_INVALIDARG;
        *fragment = nullptr;
        if (!owner_) return S_OK;
        switch (direction) {
            case NavigateDirection_Parent:
                return owner_->QueryInterface(
                    __uuidof(IRawElementProviderFragment),
                    reinterpret_cast<void**>(fragment));
            case NavigateDirection_PreviousSibling:
                return child_index_ > 0
                    ? owner_->CreateAutomationChild(child_index_ - 1, fragment)
                    : S_OK;
            case NavigateDirection_NextSibling:
                return child_index_ + 1 < owner_->AutomationChildCount()
                    ? owner_->CreateAutomationChild(child_index_ + 1, fragment)
                    : S_OK;
            default:
                return S_OK;
        }
    }

    STDMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) noexcept override {
        return MakeRuntimeId(owner_->WindowHandle(), runtime_id_, runtime_id);
    }

    STDMETHODIMP get_BoundingRectangle(UiaRect* bounds) noexcept override {
        if (!bounds) return E_INVALIDARG;
        *bounds = {};
        if (!owner_) return S_OK;
        const RECT rect =
            static_cast<Derived*>(this)->AutomationRect();
        POINT top_left{rect.left, rect.top};
        POINT bottom_right{rect.right, rect.bottom};
        if (owner_->WindowHandle() &&
            ::ClientToScreen(owner_->WindowHandle(), &top_left) &&
            ::ClientToScreen(owner_->WindowHandle(), &bottom_right)) {
            SetUiaBounds(
                RECT{
                    top_left.x,
                    top_left.y,
                    bottom_right.x,
                    bottom_right.y
                },
                bounds);
        }
        return S_OK;
    }

    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override {
        if (!roots) return E_INVALIDARG;
        *roots = nullptr;
        return S_OK;
    }

    STDMETHODIMP get_FragmentRoot(
        IRawElementProviderFragmentRoot** root) noexcept override {
        if (!root) return E_INVALIDARG;
        *root = nullptr;
        if (!owner_) return S_OK;
        return owner_->QueryInterface(
            __uuidof(IRawElementProviderFragmentRoot),
            reinterpret_cast<void**>(root));
    }

protected:
    CandidateAutomationFragment(
        const ComPtr<CandidateUI>& owner,
        size_t child_index,
        long runtime_id) noexcept
        : owner_(owner), child_index_(child_index), runtime_id_(runtime_id) {}

    [[nodiscard]] CandidateUI* Owner() const noexcept { return owner_.Get(); }

    [[nodiscard]] bool IsOffscreen(const RECT& rect) const noexcept {
        return !owner_ || !owner_->is_shown_ || !owner_->WindowHandle() ||
            !::IsWindowVisible(owner_->WindowHandle()) ||
            rect.bottom <= 0 || rect.top >= owner_->CurrentHeight();
    }

private:
    ComPtr<CandidateUI> owner_;
    size_t child_index_ = 0;
    long runtime_id_ = 0;
};

class CandidateAutomationItem final
    : public CandidateAutomationFragment<
          CandidateAutomationItem, ISelectionItemProvider> {
public:
    CandidateAutomationItem(
        const ComPtr<CandidateUI>& owner,
        size_t page_index,
        long runtime_id) noexcept
        : CandidateAutomationFragment(
              owner,
              (owner && owner->HasPageAutomationButtons() ? 2u : 0u) + page_index,
              runtime_id),
          page_index_(page_index) {}

    STDMETHODIMP GetPatternProvider(
        PATTERNID pattern_id, IUnknown** provider) noexcept override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        if (pattern_id == UIA_SelectionItemPatternId) {
            return QueryInterface(
                __uuidof(ISelectionItemProvider),
                reinterpret_cast<void**>(provider));
        }
        return S_OK;
    }

    STDMETHODIMP GetPropertyValue(
        PROPERTYID property_id, VARIANT* value) noexcept override {
        if (!value) return E_INVALIDARG;
        ::VariantInit(value);
        CandidateUI* const owner = Owner();
        if (!owner) return S_OK;
        switch (property_id) {
            case UIA_ControlTypePropertyId:
                value->vt = VT_I4;
                value->lVal = UIA_ListItemControlTypeId;
                return S_OK;
            case UIA_NamePropertyId:
                return MakeBstrVariant(
                    owner->AccessibleCandidateName(page_index_), value);
            case UIA_IsEnabledPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = VARIANT_TRUE;
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = IsOffscreen(AutomationRect())
                    ? VARIANT_TRUE
                    : VARIANT_FALSE;
                return S_OK;
            case UIA_SelectionItemIsSelectedPropertyId:
                value->vt = VT_BOOL;
                value->boolVal =
                    owner->SelectedPageIndex() == page_index_
                    ? VARIANT_TRUE
                    : VARIANT_FALSE;
                return S_OK;
            default:
                return S_OK;
        }
    }

    STDMETHODIMP SetFocus() noexcept override { return Select(); }

    STDMETHODIMP Select() noexcept override {
        CandidateUI* const owner = Owner();
        if (!owner || page_index_ >= owner->AccessibleCandidateCount()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return owner->InvokeSelectionCallback(page_index_);
    }

    STDMETHODIMP AddToSelection() noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RemoveFromSelection() noexcept override { return E_NOTIMPL; }

    STDMETHODIMP get_IsSelected(BOOL* selected) noexcept override {
        if (!selected) return E_INVALIDARG;
        CandidateUI* const owner = Owner();
        if (!owner) return UIA_E_ELEMENTNOTAVAILABLE;
        *selected = owner->SelectedPageIndex() == page_index_;
        return S_OK;
    }

    STDMETHODIMP get_SelectionContainer(
        IRawElementProviderSimple** container) noexcept override {
        if (!container) return E_INVALIDARG;
        *container = nullptr;
        CandidateUI* const owner = Owner();
        if (!owner) return S_OK;
        return owner->QueryInterface(
            __uuidof(IRawElementProviderSimple),
            reinterpret_cast<void**>(container));
    }

    RECT AutomationRect() const noexcept {
        CandidateUI* const owner = Owner();
        return owner ? owner->CandidateRect(page_index_) : RECT{};
    }

private:
    size_t page_index_ = 0;
};

class CandidatePageButtonAutomationItem final
    : public CandidateAutomationFragment<
          CandidatePageButtonAutomationItem, IInvokeProvider> {
public:
    CandidatePageButtonAutomationItem(
        const ComPtr<CandidateUI>& owner, bool next_page) noexcept
        : CandidateAutomationFragment(
              owner,
              next_page ? 1u : 0u,
              next_page ? kNextPageRuntimeId : kPreviousPageRuntimeId),
          next_page_(next_page) {}

    STDMETHODIMP GetPatternProvider(
        PATTERNID pattern_id, IUnknown** provider) noexcept override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        if (pattern_id == UIA_InvokePatternId) {
            return QueryInterface(
                __uuidof(IInvokeProvider),
                reinterpret_cast<void**>(provider));
        }
        return S_OK;
    }

    STDMETHODIMP GetPropertyValue(
        PROPERTYID property_id, VARIANT* value) noexcept override {
        if (!value) return E_INVALIDARG;
        ::VariantInit(value);
        CandidateUI* const owner = Owner();
        if (!owner) return S_OK;
        switch (property_id) {
            case UIA_AutomationIdPropertyId:
                return MakeBstrVariant(
                    next_page_
                        ? L"IME_Candidate_NextPage"
                        : L"IME_Candidate_PreviousPage",
                    value);
            case UIA_ControlTypePropertyId:
                value->vt = VT_I4;
                value->lVal = UIA_ButtonControlTypeId;
                return S_OK;
            case UIA_NamePropertyId:
                return MakeBstrVariant(
                    owner->AccessiblePageButtonName(next_page_), value);
            case UIA_IsEnabledPropertyId:
                value->vt = VT_BOOL;
                value->boolVal =
                    owner->HasPageAutomationButtons() &&
                    owner->IsPageTurnAvailable(next_page_)
                    ? VARIANT_TRUE
                    : VARIANT_FALSE;
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = IsOffscreen(AutomationRect())
                    ? VARIANT_TRUE
                    : VARIANT_FALSE;
                return S_OK;
            default:
                return S_OK;
        }
    }

    STDMETHODIMP SetFocus() noexcept override { return S_OK; }

    STDMETHODIMP Invoke() noexcept override {
        CandidateUI* const owner = Owner();
        if (!owner || !owner->HasPageAutomationButtons()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (!owner->IsPageTurnAvailable(next_page_)) {
            return UIA_E_ELEMENTNOTENABLED;
        }
        return owner->InvokePageTurnCallback(next_page_);
    }

    RECT AutomationRect() const noexcept {
        CandidateUI* const owner = Owner();
        if (!owner) return RECT{};
        return next_page_
            ? owner->NextPageButtonRect()
            : owner->PrevPageButtonRect();
    }

private:
    bool next_page_ = false;
};

STDMETHODIMP CandidateUI::get_ProviderOptions(
    ProviderOptions* options) noexcept {
    if (!options) return E_INVALIDARG;
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetPatternProvider(
    PATTERNID pattern_id, IUnknown** provider) noexcept {
    if (!provider) return E_INVALIDARG;
    *provider = nullptr;
    if (pattern_id == UIA_SelectionPatternId) {
        return QueryInterface(
            __uuidof(ISelectionProvider),
            reinterpret_cast<void**>(provider));
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetPropertyValue(
    PROPERTYID property_id, VARIANT* value) noexcept {
    if (!value) return E_INVALIDARG;
    ::VariantInit(value);
    switch (property_id) {
        case UIA_AutomationIdPropertyId:
            return MakeBstrVariant(L"IME_Candidate_Window", value);
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = UIA_ListControlTypeId;
            return S_OK;
        case UIA_NamePropertyId:
            return MakeBstrVariant(L"IME Candidate Window", value);
        case UIA_ItemStatusPropertyId: {
            try {
                const std::wstring status = AccessiblePageStatus();
                return status.empty()
                    ? S_OK
                    : MakeBstrVariant(status, value);
            } catch (const std::bad_alloc&) {
                QIQI_LOG_ERROR(
                    "CandidateUI item status allocation failed (hr=0x{:08x})",
                    gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
                return E_OUTOFMEMORY;
            } catch (...) {
                QIQI_LOG_ERROR(
                    "CandidateUI item status formatting failed (hr=0x{:08x})",
                    gsl::narrow_cast<unsigned int>(E_FAIL));
                return E_FAIL;
            }
        }
        case UIA_IsEnabledPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            value->vt = VT_BOOL;
            value->boolVal =
                !is_shown_ || !hwnd_ || !::IsWindowVisible(hwnd_.Get())
                ? VARIANT_TRUE
                : VARIANT_FALSE;
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP CandidateUI::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) noexcept {
    if (!provider) return E_INVALIDARG;
    *provider = nullptr;
    if (!hwnd_) return S_OK;
    const HRESULT hr = ::UiaHostProviderFromHwnd(hwnd_.Get(), provider);
    if (FAILED(hr)) {
        QIQI_LOG_WARN(
            "CandidateUI::UiaHostProviderFromHwnd failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

STDMETHODIMP CandidateUI::Navigate(
    NavigateDirection direction,
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    const size_t child_count = AutomationChildCount();
    if (direction == NavigateDirection_FirstChild && child_count > 0) {
        return CreateAutomationChild(0, fragment);
    }
    if (direction == NavigateDirection_LastChild && child_count > 0) {
        return CreateAutomationChild(child_count - 1, fragment);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetRuntimeId(SAFEARRAY** runtime_id) noexcept {
    return MakeRuntimeId(hwnd_.Get(), -1, runtime_id);
}

STDMETHODIMP CandidateUI::get_BoundingRectangle(UiaRect* bounds) noexcept {
    if (!bounds) return E_INVALIDARG;
    *bounds = {};
    RECT rect{};
    if (hwnd_ && ::GetWindowRect(hwnd_.Get(), &rect)) {
        SetUiaBounds(rect, bounds);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetEmbeddedFragmentRoots(
    SAFEARRAY** roots) noexcept {
    if (!roots) return E_INVALIDARG;
    *roots = nullptr;
    return S_OK;
}

STDMETHODIMP CandidateUI::SetFocus() noexcept {
    return S_OK;
}

STDMETHODIMP CandidateUI::get_FragmentRoot(
    IRawElementProviderFragmentRoot** root) noexcept {
    if (!root) return E_INVALIDARG;
    *root = nullptr;
    return QueryInterface(
        __uuidof(IRawElementProviderFragmentRoot),
        reinterpret_cast<void**>(root));
}

STDMETHODIMP CandidateUI::ElementProviderFromPoint(
    double x,
    double y,
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (!hwnd_) return S_OK;
    const POINT point{
        gsl::narrow_cast<LONG>(x),
        gsl::narrow_cast<LONG>(y)
    };
    POINT client = point;
    if (!::ScreenToClient(hwnd_.Get(), &client)) return S_OK;
    POINT content_point = client;
    content_point.y += scroll_offset_;
    if (HasPageAutomationButtons() &&
        ::PtInRect(&prev_page_hit_rect_, content_point)) {
        return CreateAutomationPageButton(false, fragment);
    }
    if (HasPageAutomationButtons() &&
        ::PtInRect(&next_page_hit_rect_, content_point)) {
        return CreateAutomationPageButton(true, fragment);
    }
    for (size_t i = 0; i < AccessibleCandidateCount(); ++i) {
        if (::PtInRect(&gsl::at(candidate_hit_rects_, i),
                       content_point)) {
            return CreateAutomationItem(i, fragment);
        }
    }
    return QueryInterface(
        __uuidof(IRawElementProviderFragment),
        reinterpret_cast<void**>(fragment));
}

STDMETHODIMP CandidateUI::GetFocus(
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (AccessibleCandidateCount() == 0) return S_OK;
    return CreateAutomationItem(SelectedPageIndex(), fragment);
}

STDMETHODIMP CandidateUI::GetSelection(SAFEARRAY** selection) noexcept {
    if (!selection) return E_INVALIDARG;
    *selection = nullptr;
    SAFEARRAYBOUND bound{
        AccessibleCandidateCount() > 0 ? 1ul : 0ul,
        0
    };
    *selection = ::SafeArrayCreate(VT_UNKNOWN, 1, &bound);
    if (!*selection) return E_OUTOFMEMORY;
    if (bound.cElements == 0) return S_OK;
    ComPtr<IRawElementProviderFragment> fragment;
    ComPtr<IRawElementProviderSimple> selected;
    const HRESULT hr = CreateAutomationItem(
        SelectedPageIndex(), fragment.GetAddressOf());
    const HRESULT conversion_hr =
        SUCCEEDED(hr) && fragment ? fragment.As(&selected) : hr;
    if (FAILED(conversion_hr) || !selected) {
        ::SafeArrayDestroy(*selection);
        *selection = nullptr;
        return FAILED(conversion_hr) ? conversion_hr : E_FAIL;
    }
    LONG index = 0;
    IUnknown* selected_item = selected.Get();
    const HRESULT put_hr =
        ::SafeArrayPutElement(*selection, &index, selected_item);
    if (FAILED(put_hr)) {
        ::SafeArrayDestroy(*selection);
        *selection = nullptr;
        return put_hr;
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::get_CanSelectMultiple(
    BOOL* can_select_multiple) noexcept {
    if (!can_select_multiple) return E_INVALIDARG;
    *can_select_multiple = FALSE;
    return S_OK;
}

STDMETHODIMP CandidateUI::get_IsSelectionRequired(
    BOOL* selection_required) noexcept {
    if (!selection_required) return E_INVALIDARG;
    *selection_required = AccessibleCandidateCount() > 0;
    return S_OK;
}

HRESULT CandidateUI::CreateAutomationItem(
    size_t page_index,
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (page_index >= AccessibleCandidateCount()) return S_OK;
    if (page_size_ != 0 &&
        current_page_ > kMaxCandidateRuntimeId / page_size_) {
        return E_BOUNDS;
    }
    const size_t page_offset = current_page_ * page_size_;
    if (page_index > kMaxCandidateRuntimeId - page_offset) {
        return E_BOUNDS;
    }
    return CreateComObjectToOut<CandidateAutomationItem>(
        fragment,
        ComPtr<CandidateUI>(this),
        page_index,
        gsl::narrow_cast<long>(page_offset + page_index));
}

HRESULT CandidateUI::CreateAutomationPageButton(
    bool next_page,
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (!HasPageAutomationButtons()) return S_OK;
    return CreateComObjectToOut<CandidatePageButtonAutomationItem>(
        fragment, ComPtr<CandidateUI>(this), next_page);
}

HRESULT CandidateUI::CreateAutomationChild(
    size_t child_index,
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (child_index >= AutomationChildCount()) return S_OK;
    if (HasPageAutomationButtons()) {
        if (child_index < 2) {
            return CreateAutomationPageButton(child_index == 1, fragment);
        }
        child_index -= 2;
    }
    return CreateAutomationItem(child_index, fragment);
}

size_t CandidateUI::AutomationChildCount() const noexcept {
    return AccessibleCandidateCount() +
        (HasPageAutomationButtons() ? 2u : 0u);
}

bool CandidateUI::HasPageAutomationButtons() const noexcept {
    return ComputeTotalPages(all_candidates_.size(), page_size_) > 1 &&
        prev_page_hit_rect_.right > prev_page_hit_rect_.left &&
        next_page_hit_rect_.right > next_page_hit_rect_.left;
}

size_t CandidateUI::SelectedPageIndex() const noexcept {
    const size_t count = AccessibleCandidateCount();
    return count > 0 ? std::min(selected_in_page_, count - 1) : 0;
}

std::wstring_view CandidateUI::AccessibleCandidateName(
    size_t page_index) const noexcept {
    const size_t global_index =
        current_page_ * page_size_ + page_index;
    if (global_index >= all_candidates_.size()) return {};
    return StripCodeAnnotation(gsl::at(all_candidates_, global_index));
}

std::wstring_view CandidateUI::AccessiblePageButtonName(
    bool next_page) const noexcept {
    const UiStrings& strings = GetUiStrings(ui_language_);
    return next_page
        ? strings.candidate_next_page
        : strings.candidate_previous_page;
}

std::wstring CandidateUI::AccessiblePageStatus() const {
    const size_t page_count =
        ComputeTotalPages(all_candidates_.size(), page_size_);
    return FormatPageStatus(ui_language_, current_page_, page_count);
}

HRESULT CandidateUI::RaiseAccessibilityEvent(
    EVENTID event_id, size_t page_index) noexcept {
    try {
        if (accessibility_event_observer_for_test_) {
            accessibility_event_observer_for_test_(event_id);
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityEvent observer failed (event={})",
            event_id);
    }
    if (!hwnd_ || (!is_shown_ && event_id != UIA_MenuClosedEventId)) return S_OK;
    const bool observe_dispatch =
        accessibility_dispatch_observer_for_test_ != nullptr;
    if ((!detail::UiAutomationEventsEnabledForTest() ||
         !::UiaClientsAreListening()) &&
        !observe_dispatch) {
        return S_OK;
    }
    ComPtr<IRawElementProviderSimple> provider;
    HRESULT hr = E_FAIL;
    if (event_id == UIA_SelectionItem_ElementSelectedEventId) {
        if (page_index >= AccessibleCandidateCount()) return S_OK;
        ComPtr<IRawElementProviderFragment> item;
        hr = CreateAutomationItem(page_index, item.GetAddressOf());
        if (SUCCEEDED(hr) && item) {
            hr = item.As(&provider);
        }
    } else {
        hr = QueryInterface(
            __uuidof(IRawElementProviderSimple),
            reinterpret_cast<void**>(provider.GetAddressOf()));
    }
    if (FAILED(hr) || !provider) {
        QIQI_LOG_WARN(
            "CandidateUI::RaiseAccessibilityEvent provider failed "
            "(event={}, hr=0x{:08x})",
            event_id, gsl::narrow_cast<unsigned int>(hr));
        return hr;
    }
    try {
        if (accessibility_dispatch_observer_for_test_) {
            accessibility_dispatch_observer_for_test_(
                event_id, provider.Get() == this);
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityEvent dispatch observer failed "
            "(event={})",
            event_id);
    }
    if (!detail::UiAutomationEventsEnabledForTest() ||
        !::UiaClientsAreListening()) {
        return S_OK;
    }
    hr = ::UiaRaiseAutomationEvent(provider.Get(), event_id);
    if (FAILED(hr)) {
        QIQI_LOG_DEBUG(
            "CandidateUI::UiaRaiseAutomationEvent failed "
            "(event={}, hr=0x{:08x})",
            event_id, gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

HRESULT CandidateUI::RaiseAccessibilityStructureChanged() noexcept {
    try {
        if (accessibility_structure_observer_for_test_) {
            accessibility_structure_observer_for_test_();
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityStructureChanged observer failed");
    }
    if (!hwnd_ || !is_shown_ ||
        !detail::UiAutomationEventsEnabledForTest() ||
        !::UiaClientsAreListening()) {
        return S_OK;
    }
    const HRESULT hr = ::UiaRaiseStructureChangedEvent(
        this, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    if (FAILED(hr)) {
        QIQI_LOG_DEBUG(
            "CandidateUI::UiaRaiseStructureChangedEvent failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

HRESULT CandidateUI::RaiseAccessibilityPropertyChanged(
    IRawElementProviderSimple* provider,
    PROPERTYID property_id,
    VARIANT old_value,
    VARIANT new_value) noexcept {
    if (!provider) return E_INVALIDARG;
    try {
        if (accessibility_property_observer_for_test_) {
            accessibility_property_observer_for_test_(property_id);
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityPropertyChanged observer failed "
            "(property={})",
            property_id);
    }
    if (!detail::UiAutomationEventsEnabledForTest() ||
        !::UiaClientsAreListening()) {
        return S_OK;
    }
    const HRESULT hr = ::UiaRaiseAutomationPropertyChangedEvent(
        provider, property_id, old_value, new_value);
    if (FAILED(hr)) {
        QIQI_LOG_DEBUG(
            "CandidateUI::UiaRaiseAutomationPropertyChangedEvent failed "
            "(property={}, hr=0x{:08x})",
            property_id, gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

HRESULT CandidateUI::RaiseAccessibilityPageChanged(
    size_t previous_page,
    size_t previous_page_count,
    bool previous_page_button_enabled,
    bool next_page_button_enabled) noexcept {
    const size_t page_count =
        ComputeTotalPages(all_candidates_.size(), page_size_);
    if (previous_page == current_page_ &&
        previous_page_count == page_count) {
        return S_OK;
    }

    HRESULT result = S_OK;
    VARIANT old_status{};
    VARIANT new_status{};
    ::VariantInit(&old_status);
    ::VariantInit(&new_status);
    const auto clear_variants = gsl::finally([&]() noexcept {
        std::ignore = ::VariantClear(&old_status);
        std::ignore = ::VariantClear(&new_status);
    });
    HRESULT hr = MakePageStatusVariant(
        ui_language_, previous_page, previous_page_count, &old_status);
    if (SUCCEEDED(hr)) {
        hr = MakePageStatusVariant(
            ui_language_, current_page_, page_count, &new_status);
    }
    if (FAILED(hr)) return hr;
    result = PreserveFirstFailure(
        result,
        RaiseAccessibilityPropertyChanged(
            this, UIA_ItemStatusPropertyId, old_status, new_status));

    const auto raise_enabled = [&](bool next_page, VARIANT_BOOL old_enabled) {
        if (previous_page_count <= 1 || page_count <= 1) return;
        const VARIANT_BOOL new_enabled = IsPageTurnAvailable(next_page)
            ? VARIANT_TRUE
            : VARIANT_FALSE;
        if (old_enabled == new_enabled) return;
        ComPtr<IRawElementProviderFragment> fragment;
        ComPtr<IRawElementProviderSimple> provider;
        HRESULT operation_hr = CreateAutomationPageButton(
            next_page, fragment.GetAddressOf());
        if (SUCCEEDED(operation_hr) && fragment) {
            operation_hr = fragment.As(&provider);
        }
        if (FAILED(operation_hr) || !provider) {
            result = PreserveFirstFailure(
                result,
                FAILED(operation_hr) ? operation_hr : E_FAIL);
            return;
        }
        VARIANT old_value{};
        VARIANT new_value{};
        ::VariantInit(&old_value);
        ::VariantInit(&new_value);
        old_value.vt = VT_BOOL;
        old_value.boolVal = old_enabled;
        new_value.vt = VT_BOOL;
        new_value.boolVal = new_enabled;
        result = PreserveFirstFailure(
            result,
            RaiseAccessibilityPropertyChanged(
                provider.Get(),
                UIA_IsEnabledPropertyId,
                old_value,
                new_value));
    };
    raise_enabled(
        false, previous_page_button_enabled ? VARIANT_TRUE : VARIANT_FALSE);
    raise_enabled(
        true, next_page_button_enabled ? VARIANT_TRUE : VARIANT_FALSE);
    return result;
}
} // namespace qiqi
