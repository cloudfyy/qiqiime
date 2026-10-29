#pragma once
#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
#include <atomic>

namespace qiqi {

// Implements ITfCandidateListUIElement so both regular desktop apps (popup window)
// and fullscreen games in UILess mode (ITfUIElementSink) can render candidates.
class CandidateUI final : public ITfCandidateListUIElement {
public:
    explicit CandidateUI(HINSTANCE instance);
    ~CandidateUI();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* pbstrDescription) override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP Show(BOOL bShow) override;
    STDMETHODIMP IsShown(BOOL* pbShow) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim) override;
    STDMETHODIMP GetCount(UINT* puCount) override;
    STDMETHODIMP GetSelection(UINT* puIndex) override;
    STDMETHODIMP GetString(UINT uIndex, BSTR* pstr) override;
    STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) override;
    STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt) override;
    STDMETHODIMP GetCurrentPage(UINT* puPage) override;

    // State update from TIP
    void SetCandidates(std::wstring code,
                       std::vector<std::wstring> all_candidates,
                       size_t page_size,
                       size_t current_page,
                       size_t selected_in_page);

    void MoveWindowTo(const RECT& text_rect);
    void DestroyPopupWindow();

    DWORD UiElementId() const noexcept { return ui_element_id_; }
    void SetUiElementId(DWORD id) noexcept { ui_element_id_ = id; }

private:
    bool EnsurePopupWindow();
    void PaintPopup(HDC hdc, const RECT& client_rect) const;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    std::atomic<ULONG> ref_count_{1};
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    BOOL is_shown_ = FALSE;
    DWORD ui_element_id_ = TF_INVALID_UIELEMENTID;
    DWORD updated_flags_ = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                           TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
    size_t page_size_ = 5;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    POINT popup_pos_{100, 100};
};

} // namespace qiqi
