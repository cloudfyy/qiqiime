// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <ctffunc.h>
#include <msctf.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <uiautomationclient.h>
#include <gsl/gsl>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/comobj.h"
#include "common/config.h"
#include "common/controls.h"
#include "common/ctrlutil.h"
#include "common/dpi.h"
#include "common/RAII.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "ime/ui/cadifsm.h"

namespace qiqi {

using Microsoft::WRL::ComPtr;
class CandidateAutomationItem;
class CandidatePageButtonAutomationItem;
template <typename Derived, typename PatternInterface>
class CandidateAutomationFragment;

namespace detail {
inline bool& UiAutomationEventsEnabledForTest() noexcept {
    static bool enabled = true;
    return enabled;
}

inline constexpr bool ShouldRetryCandidatePopupWithoutOwner(
    bool is_app_container, bool require_owned_window,
    bool has_owner, DWORD error) noexcept {
    return is_app_container && require_owned_window && has_owner &&
           error == ERROR_ACCESS_DENIED;
}
} // namespace detail

inline void SetUiAutomationEventsEnabledForTest(bool enabled) noexcept {
    detail::UiAutomationEventsEnabledForTest() = enabled;
}

enum class CandidateUiAction : uint8_t {
    SetSelection,
    Finalize,
    Abort,
    FinalizeExact
};

std::wstring_view StripCodeAnnotation(std::wstring_view text) noexcept;

// Implements ITfCandidateListUIElement so both regular desktop apps (popup window)
// and fullscreen games in UILess mode (ITfUIElementSink) can render candidates.
class CandidateUI final : public ComObjectBase<
    CandidateUI,
    ITfCandidateListUIElementBehavior,
    ITfIntegratableCandidateListUIElement,
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    IRawElementProviderFragmentRoot,
    ISelectionProvider> {
public:
    using SelectCallback = std::function<void(size_t index_in_page)>;
    using PageTurnCallback = std::function<void(bool next_page)>;
    using ActionCallback =
        std::function<HRESULT(CandidateUiAction action, size_t index)>;
    using IntegratedKeyDownCallback =
        std::function<HRESULT(WPARAM wparam, LPARAM lparam, BOOL* eaten)>;

    static constexpr int kPopupWidth = 360;
    static constexpr int kPopupHeight = 64;
    static constexpr DWORD kAllCandidateUpdateFlags =
        CandidateUiElementLifecycle::kAllCandidateUpdateFlags;

    explicit CandidateUI(HINSTANCE instance) noexcept;
    STDMETHODIMP_(ULONG) Release() noexcept override;

#pragma warning(push)
#pragma warning(disable : 26456)
    CandidateUI(const CandidateUI&) = delete;
    CandidateUI& operator=(const CandidateUI&) = delete;
    CandidateUI(CandidateUI&&) = delete;
    CandidateUI& operator=(CandidateUI&&) = delete;
#pragma warning(pop)

    bool QueryAdditionalInterfaces(REFIID riid, void** ppvObj) noexcept {
        return MatchBaseInterfaces<
            ITfCandidateListUIElementBehavior,
            ITfCandidateListUIElement,
            ITfUIElement>(riid, ppvObj);
    }

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* pbstrDescription) noexcept override;
    STDMETHODIMP GetGUID(GUID* pguid) noexcept override;
    STDMETHODIMP Show(BOOL bShow) noexcept override;
    STDMETHODIMP IsShown(BOOL* pbShow) noexcept override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags) noexcept override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim) noexcept override;
    STDMETHODIMP GetCount(UINT* puCount) noexcept override;
    STDMETHODIMP GetSelection(UINT* puIndex) noexcept override;
    STDMETHODIMP GetString(UINT uIndex, BSTR* pstr) noexcept override;
    STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) noexcept override;
    STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt) noexcept override;
    STDMETHODIMP GetCurrentPage(UINT* puPage) noexcept override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT index) noexcept override;
    STDMETHODIMP Finalize() noexcept override;
    STDMETHODIMP Abort() noexcept override;

    // ITfIntegratableCandidateListUIElement
    STDMETHODIMP SetIntegrationStyle(GUID integration_style) noexcept override;
    STDMETHODIMP GetSelectionStyle(
        TfIntegratableCandidateListSelectionStyle* selection_style) noexcept override;
    STDMETHODIMP OnKeyDown(
        WPARAM wparam, LPARAM lparam, BOOL* eaten) noexcept override;
    STDMETHODIMP ShowCandidateNumbers(BOOL* show) noexcept override;
    STDMETHODIMP FinalizeExactCompositionString() noexcept override;

    // UI Automation provider
    STDMETHODIMP get_ProviderOptions(ProviderOptions* options) noexcept override;
    STDMETHODIMP GetPatternProvider(PATTERNID pattern_id, IUnknown** provider) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID property_id, VARIANT* value) noexcept override;
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** fragment) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* bounds) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override;
    STDMETHODIMP ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** fragment) noexcept override;
    STDMETHODIMP GetFocus(IRawElementProviderFragment** fragment) noexcept override;
    STDMETHODIMP GetSelection(SAFEARRAY** selection) noexcept override;
    STDMETHODIMP get_CanSelectMultiple(BOOL* can_select_multiple) noexcept override;
    STDMETHODIMP get_IsSelectionRequired(BOOL* selection_required) noexcept override;

    // State update from TIP
    void SetCandidates(std::wstring code,
                       std::vector<std::wstring> all_candidates,
                       size_t page_size,
                       size_t current_page,
                       size_t selected_in_page,
                       ITfDocumentMgr* document_manager = nullptr,
                       const std::vector<std::wstring>* raw_candidates = nullptr);

    void MoveWindowTo(
        const RECT& text_rect, HWND host_window = nullptr, bool require_owned_window = false);
    void SetLayout(CandidateLayout layout);
    void SetDisplayPreferences(DisplayPreferences preferences);
    void SetFontPreferences(const CandidateFontPreferences& preferences);
    void SetSkinSnapshot(const std::optional<ImeSkinSnapshot>& snapshot) noexcept;
    void SetDisplayPreferences(UiTheme theme, UiTextScale scale, bool easy_reading) {
        SetDisplayPreferences(DisplayPreferences{theme, scale, easy_reading});
    }
    const DisplayAppearance& Appearance() const noexcept { return appearance_; }
    bool DestroyPopupWindow() noexcept;
    void SetPresentationEnabled(bool enabled) noexcept { presentation_enabled_ = enabled; }

    void SetSelectCallback(SelectCallback cb) noexcept { select_callback_ = std::move(cb); }
    void SetPageTurnCallback(PageTurnCallback cb) noexcept { page_turn_callback_ = std::move(cb); }
    void SetActionCallback(ActionCallback callback) noexcept {
        action_callback_ = std::move(callback);
    }
    void SetIntegratedKeyDownCallback(IntegratedKeyDownCallback callback) noexcept {
        integrated_key_down_callback_ = std::move(callback);
    }
    void ResetIntegrationState() noexcept {
        searchbox_integration_ = false;
    }
    void SetUiLanguage(UiLanguage lang);
    int CurrentWidth() const noexcept { return popup_width_; }
    int CurrentHeight() const noexcept { return popup_height_; }
    RECT PrevPageButtonRect() const noexcept { return ViewportRect(prev_page_hit_rect_); }
    RECT NextPageButtonRect() const noexcept { return ViewportRect(next_page_hit_rect_); }
    RECT CandidateRect(size_t index) const noexcept {
        return index < candidate_hit_rects_.size() ? ViewportRect(gsl::at(candidate_hit_rects_, index)) : RECT{};
    }
    int ContentHeight() const noexcept { return content_height_; }
    int ScrollOffset() const noexcept { return scroll_offset_; }
    HWND WindowHandle() const noexcept { return hwnd_.Get(); }

    CandidateUiElementLifecycle& UiElementLifecycle() noexcept {
        return ui_element_lifecycle_;
    }
    DWORD UiElementId() const noexcept { return ui_element_lifecycle_.ActiveId(); }
    void SetUiElementId(
        DWORD id, ITfUIElementMgr* manager = nullptr) noexcept {
        ui_element_lifecycle_.SetActiveRegistrationForTest(id, manager);
    }
    bool HasPendingUiElementRetirements() const noexcept {
        return ui_element_lifecycle_.HasPendingRetirements();
    }
    void ClearUiElementDocument() noexcept {
        ui_element_lifecycle_.ClearDocumentManager();
    }
    uint64_t UiElementUpdateRevision() const noexcept {
        return ui_element_lifecycle_.UpdateRevision();
    }
    void AcknowledgeUiElementUpdate(uint64_t revision) noexcept {
        ui_element_lifecycle_.AcknowledgeUpdate(revision);
    }
    bool IsDirectWriteActive() const noexcept { return dwrite_text_format_.IsValid() && d2d_dc_target_.IsValid(); }
    LOGFONTW FontDescriptionForTest() const noexcept {
        LOGFONTW description{};
        if (font_) ::GetObjectW(font_.Get(), sizeof(description), &description);
        return description;
    }
    void DisableDirect2DForTest() noexcept { d2d_dc_target_.Reset(); }
    std::vector<CandidatePaintItem> PaintItemsForTest() const { return BuildPaintItems(); }
    void SetAccessibilityEventObserverForTest(std::function<void(EVENTID)> observer) noexcept {
        accessibility_event_observer_for_test_ = std::move(observer);
    }
    void SetAccessibilityStructureObserverForTest(std::function<void()> observer) noexcept {
        accessibility_structure_observer_for_test_ = std::move(observer);
    }
    void SetAccessibilityDispatchObserverForTest(
        std::function<void(EVENTID, bool)> observer) noexcept {
        accessibility_dispatch_observer_for_test_ = std::move(observer);
    }
    void SetAccessibilityPropertyObserverForTest(
        std::function<void(PROPERTYID)> observer) noexcept {
        accessibility_property_observer_for_test_ = std::move(observer);
    }

private:
    friend ComObjectBase;
    friend class CandidateAutomationItem;
    friend class CandidatePageButtonAutomationItem;
    template <typename Derived, typename PatternInterface>
    friend class CandidateAutomationFragment;
    ~CandidateUI() noexcept = default;

    using PaintItem = CandidatePaintItem;
    void RefreshAppearance() noexcept;
    std::vector<PaintItem> BuildPaintItems() const;
    int ScaleByDpi(int px) const noexcept { return ScalePxByDpi(px, dpi_); }
    int ScaleText(int px) const noexcept { return ScaleDisplayPx(px, dpi_, preferences_.text_scale); }
    void InitDirect2DAndDirectWrite() noexcept;
    void UpdateDpiAndFont(UINT new_dpi) noexcept;
    int MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept;
    std::wstring WrapText(HDC dc, std::wstring_view text, int width) const;
    std::wstring FormatHeaderText() const;
    std::wstring FormatCandidateLabel(size_t index_in_page, std::wstring_view candidate_text) const;
    void RecalculatePopupLayout();
    void RefreshPopupLayout(UINT dpi = 0);
    RECT ViewportRect(RECT rect) const noexcept;
    void ScrollContent(int position);
    void UpdateScrollBar() const noexcept;
    void OnLeftButtonDown(POINT pt);
    bool EnsurePopupWindow() noexcept;
    HRESULT InvokeSelectionCallback(size_t index) noexcept;
    bool IsPageTurnAvailable(bool next_page) const noexcept;
    HRESULT InvokePageTurnCallback(bool next_page) noexcept;
    void CloseAccessibilityMenu() noexcept;
    void DisconnectAccessibilityProvider() noexcept;
    HRESULT RaiseAccessibilityEvent(EVENTID event_id, size_t page_index = SIZE_MAX) noexcept;
    HRESULT RaiseAccessibilityStructureChanged() noexcept;
    HRESULT RaiseAccessibilityPropertyChanged(
        IRawElementProviderSimple* provider,
        PROPERTYID property_id,
        VARIANT old_value,
        VARIANT new_value) noexcept;
    HRESULT RaiseAccessibilityPageChanged(
        size_t previous_page,
        size_t previous_page_count,
        bool previous_page_button_enabled,
        bool next_page_button_enabled) noexcept;
    HRESULT InvokeActionCallback(
        CandidateUiAction action, size_t index) noexcept;
    HRESULT CreateAutomationItem(
        size_t page_index, IRawElementProviderFragment** fragment) noexcept;
    HRESULT CreateAutomationPageButton(
        bool next_page, IRawElementProviderFragment** fragment) noexcept;
    HRESULT CreateAutomationChild(
        size_t child_index, IRawElementProviderFragment** fragment) noexcept;
    size_t AutomationChildCount() const noexcept;
    bool HasPageAutomationButtons() const noexcept;
    uint64_t AutomationTreeRevision() const noexcept {
        return accessibility_tree_revision_;
    }
    std::wstring_view AccessibleCandidateName(size_t page_index) const noexcept;
    std::wstring_view AccessiblePageButtonName(bool next_page) const noexcept;
    std::wstring AccessiblePageStatus() const;
    size_t AccessibleCandidateCount() const noexcept { return candidate_hit_rects_.size(); }
    size_t SelectedPageIndex() const noexcept;
    void NotifyPopupWindowState(HWND window, bool destroyed = false) noexcept;
    void LogPopupVisibility() const noexcept;
    bool PaintPopupDirect2D(HDC hdc, const RECT& client_rect) const;
    void PaintPopupContent(HDC hdc, const RECT& client_rect) const;
    void PaintPopup(HDC hdc, const RECT& client_rect) const;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    ScopedGdiHandle<HFONT> font_;
    ScopedD2DFactory d2d_factory_;
    mutable ScopedD2DDcTarget d2d_dc_target_;
    ScopedDWriteFactory dwrite_factory_;
    ScopedDWriteTextFormat dwrite_text_format_;
    UINT dpi_ = 96;
    int popup_width_ = kPopupWidth;
    int popup_height_ = kPopupHeight;
    BOOL is_shown_ = FALSE;
    bool show_requested_ = false;
    bool require_owned_window_ = false;
    bool presentation_enabled_ = true;
    bool destroying_window_ = false;
    HWND popup_owner_ = nullptr; // Borrowed; only hwnd_ owns a window.
    CandidateUiElementLifecycle ui_element_lifecycle_;

    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
    std::vector<std::wstring> integrated_candidates_;
    std::vector<RECT> candidate_hit_rects_;
    std::vector<std::wstring> candidate_labels_;
    std::wstring header_label_;
    std::wstring prev_page_label_;
    std::wstring next_page_label_;
    std::wstring empty_label_;
    RECT empty_rect_{};
    RECT header_rect_{};
    RECT prev_page_hit_rect_{};
    RECT next_page_hit_rect_{};
    int available_width_ = 1280;
    int available_height_ = 720;
    int content_height_ = kPopupHeight;
    int scroll_offset_ = 0;
    int wheel_delta_ = 0;
    int line_height_ = 24;
    int content_width_ = kPopupWidth;
    int font_scale_percent_ = 0;
    DisplayPreferences preferences_{};
    CandidateFontPreferences font_preferences_{};
    DisplayAppearance appearance_{};
    SkinPalette skin_colors_{};
    std::optional<ImeSkinSnapshot> skin_snapshot_;
    size_t page_size_ = 5;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    POINT popup_pos_{100, 100};
    std::optional<RECT> text_rect_;
    std::optional<RECT> announced_popup_rect_;
    bool accessibility_open_announced_ = false;
    bool accessibility_provider_needs_disconnect_ = false;
    uint64_t accessibility_tree_revision_ = 0;
    CandidateLayout layout_ = CandidateLayout::Horizontal;
    UiLanguage ui_language_ = UiLanguage::Auto;
    SelectCallback select_callback_;
    PageTurnCallback page_turn_callback_;
    ActionCallback action_callback_;
    IntegratedKeyDownCallback integrated_key_down_callback_;
    bool searchbox_integration_ = false;
    std::function<void(EVENTID)> accessibility_event_observer_for_test_;
    std::function<void()> accessibility_structure_observer_for_test_;
    std::function<void(EVENTID, bool)> accessibility_dispatch_observer_for_test_;
    std::function<void(PROPERTYID)> accessibility_property_observer_for_test_;
    // Destroy the popup window first before releasing fonts, D2D/DWrite targets, and candidate state.
    ScopedWindow hwnd_;
};

} // namespace qiqi
