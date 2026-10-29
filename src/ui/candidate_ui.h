#pragma once
#include <windows.h>
#include <msctf.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/scoped_handle.h"

namespace qiqi {

// Implements ITfCandidateListUIElement so both regular desktop apps (popup window)
// and fullscreen games in UILess mode (ITfUIElementSink) can render candidates.
class CandidateUI final : public ITfCandidateListUIElement {
public:
    using SelectCallback = std::function<void(size_t index_in_page)>;

    static constexpr int kPopupWidth = 360;
    static constexpr int kPopupHeight = 64;

    explicit CandidateUI(HINSTANCE instance) noexcept;
    ~CandidateUI() noexcept;

    CandidateUI(const CandidateUI&) = delete;
    CandidateUI& operator=(const CandidateUI&) = delete;
    CandidateUI(CandidateUI&&) = delete;
    CandidateUI& operator=(CandidateUI&&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override;
    STDMETHODIMP_(ULONG) Release() noexcept override;

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
    HWND WindowHandle() const noexcept { return hwnd_; }

    DWORD UiElementId() const noexcept { return ui_element_id_; }
    void SetUiElementId(DWORD id) noexcept { ui_element_id_ = id; }

private:
    int ScaleByDpi(int px) const noexcept { return ::MulDiv(px, dpi_, 96); }
    void UpdateDpiAndFont(UINT new_dpi) noexcept;
    void RecalculatePopupLayout();
    void OnLeftButtonDown(POINT pt) const;
    bool EnsurePopupWindow() noexcept;
    void PaintPopup(HDC hdc, const RECT& client_rect) const;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    std::atomic<ULONG> ref_count_{1};
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ScopedGdiHandle<HFONT> font_;
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
};

} // namespace qiqi
