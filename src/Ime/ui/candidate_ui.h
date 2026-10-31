#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "common/com_object.h"
#include "common/config.h"
#include "common/d2d_util.h"
#include "common/monitor_dpi.h"
#include "common/scoped_handle.h"
#include "common/win32_ui_util.h"

namespace qiqi {

using Microsoft::WRL::ComPtr;

// Implements ITfCandidateListUIElement so both regular desktop apps (popup window)
// and fullscreen games in UILess mode (ITfUIElementSink) can render candidates.
class CandidateUI final : public ComObjectBase<CandidateUI, ITfCandidateListUIElement> {
public:
    using SelectCallback = std::function<void(size_t index_in_page)>;

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

    void MoveWindowTo(const RECT& text_rect);
    void DestroyPopupWindow() noexcept;

    void SetSelectCallback(SelectCallback cb) noexcept { select_callback_ = std::move(cb); }
    void SetUiLanguage(UiLanguage lang) noexcept { ui_language_ = lang; }
    int CurrentWidth() const noexcept { return popup_width_; }
    int CurrentHeight() const noexcept { return popup_height_; }
    HWND WindowHandle() const noexcept { return hwnd_.Get(); }

    DWORD UiElementId() const noexcept { return ui_element_id_; }
    void SetUiElementId(DWORD id) noexcept { ui_element_id_ = id; }
    bool IsDirectWriteActive() const noexcept { return dwrite_text_format_.IsValid() && d2d_dc_target_.IsValid(); }
    void DisableDirect2DForTest() { d2d_dc_target_.Reset(); }

private:
    int ScaleByDpi(int px) const noexcept { return ScalePxByDpi(px, dpi_); }
    void InitDirect2DAndDirectWrite() noexcept;
    void UpdateDpiAndFont(UINT new_dpi) noexcept;
    int MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept;
    std::wstring FormatHeaderText() const;
    std::wstring FormatCandidateLabel(size_t index_in_page, std::wstring_view candidate_text) const;
    void RecalculatePopupLayout();
    void OnLeftButtonDown(POINT pt) const;
    bool EnsurePopupWindow() noexcept;
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
    DWORD ui_element_id_ = TF_INVALID_UIELEMENTID;
    DWORD updated_flags_ = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                           TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
    std::vector<RECT> candidate_hit_rects_;
    size_t page_size_ = 5;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    POINT popup_pos_{100, 100};
    UiLanguage ui_language_ = UiLanguage::Auto;
    SelectCallback select_callback_;
    // Destroy the popup window first before releasing fonts, D2D/DWrite targets, and candidate state.
    ScopedWindow hwnd_;
};

} // namespace qiqi
