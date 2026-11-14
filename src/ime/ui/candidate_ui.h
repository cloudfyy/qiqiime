// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <msctf.h>
#include <gsl/gsl>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common/com_object.h"
#include "common/config.h"
#include "common/d2d_util.h"
#include "common/monitor_dpi.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

namespace qiqi {

using Microsoft::WRL::ComPtr;

// Implements ITfCandidateListUIElement so both regular desktop apps (popup window)
// and fullscreen games in UILess mode (ITfUIElementSink) can render candidates.
class CandidateUI final : public ComObjectBase<CandidateUI, ITfCandidateListUIElement> {
public:
    using SelectCallback = std::function<void(size_t index_in_page)>;
    using PageTurnCallback = std::function<void(bool next_page)>;

    static constexpr int kPopupWidth = 360;
    static constexpr int kPopupHeight = 64;

    explicit CandidateUI(HINSTANCE instance) noexcept;
    ~CandidateUI() noexcept;

#pragma warning(push)
#pragma warning(disable : 26456)
    CandidateUI(const CandidateUI&) = delete;
    CandidateUI& operator=(const CandidateUI&) = delete;
    CandidateUI(CandidateUI&&) = delete;
    CandidateUI& operator=(CandidateUI&&) = delete;
#pragma warning(pop)

    bool QueryAdditionalInterfaces(REFIID riid, void** ppvObj) noexcept {
        return MatchBaseInterfaces<ITfCandidateListUIElement, ITfUIElement>(riid, ppvObj);
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

    // State update from TIP
    void SetCandidates(std::wstring code,
                       std::vector<std::wstring> all_candidates,
                       size_t page_size,
                       size_t current_page,
                       size_t selected_in_page);

    void MoveWindowTo(const RECT& text_rect, HWND host_window = nullptr);
    void SetLayout(CandidateLayout layout);
    void SetDisplayPreferences(DisplayPreferences preferences);
    void SetDisplayPreferences(UiTheme theme, UiTextScale scale, bool easy_reading) {
        SetDisplayPreferences(DisplayPreferences{theme, scale, easy_reading});
    }
    const DisplayAppearance& Appearance() const noexcept { return appearance_; }
    bool DestroyPopupWindow() noexcept;
    void SetPresentationEnabled(bool enabled) noexcept { presentation_enabled_ = enabled; }

    void SetSelectCallback(SelectCallback cb) noexcept { select_callback_ = std::move(cb); }
    void SetPageTurnCallback(PageTurnCallback cb) noexcept { page_turn_callback_ = std::move(cb); }
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

    DWORD UiElementId() const noexcept { return ui_element_id_; }
    void SetUiElementId(DWORD id) noexcept { ui_element_id_ = id; }
    bool IsDirectWriteActive() const noexcept { return dwrite_text_format_.IsValid() && d2d_dc_target_.IsValid(); }
    void DisableDirect2DForTest() noexcept { d2d_dc_target_.Reset(); }

private:
    struct PaintItem {
        RECT rect{};
        std::wstring text;
        bool selected = false;
        bool accent = false;
        bool enabled = true;
        bool centered = false;
    };
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
    void OnLeftButtonDown(POINT pt) const;
    bool EnsurePopupWindow() noexcept;
    void LogPopupVisibility() const noexcept;
    bool PaintPopupDirect2D(HDC hdc, const RECT& client_rect) const;
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
    bool presentation_enabled_ = true;
    bool destroying_window_ = false;
    HWND popup_owner_ = nullptr; // Borrowed; only hwnd_ owns a window.
    DWORD ui_element_id_ = TF_INVALID_UIELEMENTID;
    DWORD updated_flags_ = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                           TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
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
    DisplayAppearance appearance_{};
    size_t page_size_ = 5;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    POINT popup_pos_{100, 100};
    std::optional<RECT> text_rect_;
    CandidateLayout layout_ = CandidateLayout::Horizontal;
    UiLanguage ui_language_ = UiLanguage::Auto;
    SelectCallback select_callback_;
    PageTurnCallback page_turn_callback_;
    // Destroy the popup window first before releasing fonts, D2D/DWrite targets, and candidate state.
    ScopedWindow hwnd_;
};

} // namespace qiqi
