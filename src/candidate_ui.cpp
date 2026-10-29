#include "candidate_ui.h"
#include "guids.h"
#include "logger.h"
#include <oleauto.h>
#include <algorithm>

namespace qiqi {

namespace {
constexpr const wchar_t* kCandidateWndClass = L"QiQiIMECandidatePopupClass";
} // namespace

CandidateUI::CandidateUI(HINSTANCE instance) : instance_(instance) {
    LOGFONTW lf{};
    lf.lfHeight = -18;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font_ = ::CreateFontIndirectW(&lf);
}

CandidateUI::~CandidateUI() {
    DestroyPopupWindow();
    if (font_) {
        ::DeleteObject(font_);
        font_ = nullptr;
    }
}

STDMETHODIMP CandidateUI::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElement)) {
        *ppvObj = static_cast<ITfCandidateListUIElement*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateUI::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) CandidateUI::Release() {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP CandidateUI::GetDescription(BSTR* pbstrDescription) {
    if (!pbstrDescription) {
        return E_INVALIDARG;
    }
    *pbstrDescription = ::SysAllocString(L"QiQi Wubi Candidate List");
    return *pbstrDescription ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetGUID(GUID* pguid) {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = kGuidCandidateUIElement;
    return S_OK;
}

STDMETHODIMP CandidateUI::Show(BOOL bShow) {
    is_shown_ = bShow;
    if (bShow) {
        if (EnsurePopupWindow()) {
            ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_, nullptr, TRUE);
        }
    } else if (hwnd_) {
        ::ShowWindow(hwnd_, SW_HIDE);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* pbShow) {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = is_shown_;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* pdwFlags) {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = updated_flags_;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** ppdim) {
    if (!ppdim) {
        return E_INVALIDARG;
    }
    *ppdim = nullptr;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCount(UINT* puCount) {
    if (!puCount) {
        return E_INVALIDARG;
    }
    *puCount = static_cast<UINT>(all_candidates_.size());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* puIndex) {
    if (!puIndex) {
        return E_INVALIDARG;
    }
    const size_t global_idx = current_page_ * page_size_ + selected_in_page_;
    *puIndex = static_cast<UINT>(std::min(global_idx, all_candidates_.empty() ? 0 : all_candidates_.size() - 1));
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT uIndex, BSTR* pstr) {
    if (!pstr) {
        return E_INVALIDARG;
    }
    if (uIndex >= all_candidates_.size()) {
        return E_INVALIDARG;
    }
    *pstr = ::SysAllocString(all_candidates_[uIndex].c_str());
    return *pstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) {
    if (!puPageCnt) {
        return E_INVALIDARG;
    }
    const size_t total_pages = all_candidates_.empty()
        ? 0
        : (all_candidates_.size() + page_size_ - 1) / page_size_;
    *puPageCnt = static_cast<UINT>(total_pages);
    if (!pIndex) {
        return S_OK;
    }
    const UINT count_to_copy = std::min(uSize, static_cast<UINT>(total_pages));
    for (UINT i = 0; i < count_to_copy; ++i) {
        pIndex[i] = static_cast<UINT>(i * page_size_);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT* /*pIndex*/, UINT /*uPageCnt*/) {
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* puPage) {
    if (!puPage) {
        return E_INVALIDARG;
    }
    *puPage = static_cast<UINT>(current_page_);
    return S_OK;
}

void CandidateUI::SetCandidates(std::wstring code,
                                std::vector<std::wstring> all_candidates,
                                size_t page_size,
                                size_t current_page,
                                size_t selected_in_page) {
    raw_code_ = std::move(code);
    all_candidates_ = std::move(all_candidates);
    page_size_ = page_size > 0 ? page_size : 5;
    current_page_ = current_page;
    selected_in_page_ = selected_in_page;
    updated_flags_ = TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING |
                     TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    if (is_shown_ && EnsurePopupWindow()) {
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void CandidateUI::MoveWindowTo(const RECT& text_rect) {
    popup_pos_.x = text_rect.left;
    popup_pos_.y = text_rect.bottom + 4;

    // Clamp to monitor work area so popup doesn't go off-screen
    HMONITOR mon = ::MonitorFromPoint(popup_pos_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(mon, &mi)) {
        const int width = 360;
        const int height = 64;
        if (popup_pos_.x + width > mi.rcWork.right) {
            popup_pos_.x = std::max(mi.rcWork.left, mi.rcWork.right - width);
        }
        if (popup_pos_.y + height > mi.rcWork.bottom) {
            popup_pos_.y = std::max(mi.rcWork.top, text_rect.top - height - 4);
        }
    }

    if (hwnd_) {
        ::SetWindowPos(hwnd_, HWND_TOPMOST, popup_pos_.x, popup_pos_.y, 360, 64,
                       SWP_NOACTIVATE | (is_shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

bool CandidateUI::EnsurePopupWindow() {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &CandidateUI::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kCandidateWndClass;
    ::RegisterClassExW(&wc);

    hwnd_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandidateWndClass,
        L"",
        WS_POPUP | WS_BORDER,
        popup_pos_.x, popup_pos_.y, 360, 64,
        nullptr, nullptr, instance_, this);

    if (!hwnd_) {
        QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
    }
    return hwnd_ != nullptr;
}

void CandidateUI::DestroyPopupWindow() {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    is_shown_ = FALSE;
}

void CandidateUI::PaintPopup(HDC hdc, const RECT& client_rect) const {
    HBRUSH bg_brush = ::CreateSolidBrush(RGB(250, 251, 253));
    ::FillRect(hdc, &client_rect, bg_brush);
    ::DeleteObject(bg_brush);

    HGDIOBJ old_font = font_ ? ::SelectObject(hdc, font_) : nullptr;
    ::SetBkMode(hdc, TRANSPARENT);

    // Line 1: Wubi code + page indicator
    const size_t total_pages = all_candidates_.empty()
        ? 0
        : (all_candidates_.size() + page_size_ - 1) / page_size_;
    std::wstring header = L"五笔 [" + raw_code_ + L"]";
    if (total_pages > 1) {
        header += L"  (" + std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages) + L")";
    }
    RECT top_rect = {10, 6, client_rect.right - 10, 30};
    ::SetTextColor(hdc, RGB(35, 105, 200));
    ::DrawTextW(hdc, header.c_str(), -1, &top_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Line 2: Horizontal candidates
    std::wstring line;
    const size_t start = current_page_ * page_size_;
    const size_t end = std::min(start + page_size_, all_candidates_.size());
    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        if (!line.empty()) {
            line += L"   ";
        }
        if (idx_in_page == selected_in_page_) {
            line += L"[" + std::to_wstring(idx_in_page + 1) + L"." + all_candidates_[i] + L"]";
        } else {
            line += std::to_wstring(idx_in_page + 1) + L"." + all_candidates_[i];
        }
    }
    if (line.empty()) {
        line = L"(无候选)";
    }

    RECT bottom_rect = {10, 32, client_rect.right - 10, client_rect.bottom - 4};
    ::SetTextColor(hdc, RGB(30, 30, 30));
    ::DrawTextW(hdc, line.c_str(), -1, &bottom_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    if (old_font) {
        ::SelectObject(hdc, old_font);
    }
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }

    auto* self = reinterpret_cast<CandidateUI*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_PAINT && self) {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(hwnd, &ps);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        self->PaintPopup(hdc, rc);
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace qiqi
