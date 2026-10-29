#include "ui/candidate_ui.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "engine/wubi_fsm.h"
#include <gsl/gsl>
#include <oleauto.h>
#include <algorithm>
#include <span>

namespace qiqi {

CandidateUI::CandidateUI(HINSTANCE instance) noexcept : instance_(instance) {
    ++g_com_object_count;
    UpdateDpiAndFont(96);
}

CandidateUI::~CandidateUI() noexcept {
    DestroyPopupWindow();
    --g_com_object_count;
}

void CandidateUI::UpdateDpiAndFont(UINT new_dpi) noexcept {
    const UINT effective_dpi = new_dpi > 0 ? new_dpi : 96u;
    if (font_.IsValid() && dpi_ == effective_dpi) {
        return;
    }
    dpi_ = effective_dpi;
    popup_height_ = ScaleByDpi(kPopupHeight);

    LOGFONTW lf{};
    lf.lfHeight = -ScaleByDpi(18);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font_.Reset(::CreateFontIndirectW(&lf));
}

void CandidateUI::RecalculatePopupLayout() {
    candidate_hit_rects_.clear();
    const ScopedWindowDC screen_dc(hwnd_);
    if (!screen_dc) {
        popup_width_ = ScaleByDpi(kPopupWidth);
        popup_height_ = ScaleByDpi(kPopupHeight);
        return;
    }

    const ScopedSelectObject sel_font(screen_dc.Get(), font_.Get());

    const int margin_x = ScaleByDpi(10);
    const int item_gap = ScaleByDpi(16);
    const int row2_top = ScaleByDpi(32);
    const int row2_bottom = ScaleByDpi(kPopupHeight - 4);

    const auto& str = GetUiStrings(ui_language_);
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    std::wstring header = std::wstring(str.candidate_header_prefix) + raw_code_ + L"]";
    if (total_pages > 1) {
        header += L"  (" + std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages) + L")";
    }
    SIZE header_sz{};
    ::GetTextExtentPoint32W(screen_dc.Get(), header.c_str(), gsl::narrow_cast<int>(header.size()), &header_sz);

    int cursor_x = margin_x;
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        const auto& cand_str = gsl::at(all_candidates_, i);
        const std::wstring label = (idx_in_page == selected_in_page_)
            ? (L"[" + std::to_wstring(idx_in_page + 1) + L"." + cand_str + L"]")
            : (std::to_wstring(idx_in_page + 1) + L"." + cand_str);

        SIZE item_sz{};
        ::GetTextExtentPoint32W(screen_dc.Get(), label.c_str(), gsl::narrow_cast<int>(label.size()), &item_sz);
        const RECT hit_rc{cursor_x, row2_top, cursor_x + item_sz.cx, row2_bottom};
        candidate_hit_rects_.push_back(hit_rc);
        cursor_x += item_sz.cx + item_gap;
    }

    const int content_width = std::max<int>(header_sz.cx + margin_x * 2, cursor_x + margin_x);
    popup_width_ = std::max(ScaleByDpi(kPopupWidth), content_width);
    popup_height_ = ScaleByDpi(kPopupHeight);
}

void CandidateUI::OnLeftButtonDown(POINT pt) const {
    if (!select_callback_) {
        return;
    }
    for (size_t i = 0; i < candidate_hit_rects_.size(); ++i) {
        if (::PtInRect(&gsl::at(candidate_hit_rects_, i), pt)) {
            select_callback_(i);
            return;
        }
    }
}

STDMETHODIMP CandidateUI::QueryInterface(REFIID riid, void** ppvObj) noexcept {
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

STDMETHODIMP_(ULONG) CandidateUI::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) CandidateUI::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
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

STDMETHODIMP CandidateUI::Show(BOOL bShow) noexcept {
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

STDMETHODIMP CandidateUI::IsShown(BOOL* pbShow) noexcept {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = is_shown_;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* pdwFlags) noexcept {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = updated_flags_;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** ppdim) noexcept {
    if (!ppdim) {
        return E_INVALIDARG;
    }
    *ppdim = nullptr;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCount(UINT* puCount) noexcept {
    if (!puCount) {
        return E_INVALIDARG;
    }
    *puCount = gsl::narrow_cast<UINT>(all_candidates_.size());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* puIndex) noexcept {
    if (!puIndex) {
        return E_INVALIDARG;
    }
    const size_t global_idx = current_page_ * page_size_ + selected_in_page_;
    *puIndex = gsl::narrow_cast<UINT>(std::min(global_idx, all_candidates_.empty() ? 0 : all_candidates_.size() - 1));
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT uIndex, BSTR* pstr) noexcept {
    if (!pstr) {
        return E_INVALIDARG;
    }
    *pstr = nullptr;
    if (uIndex >= all_candidates_.size()) {
        return E_INVALIDARG;
    }
    return CopyToBstr(gsl::at(all_candidates_, uIndex), pstr);
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) noexcept {
    if (!puPageCnt) {
        return E_INVALIDARG;
    }
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
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
    *puPage = gsl::narrow_cast<UINT>(current_page_);
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

    RecalculatePopupLayout();
    if (is_shown_ && EnsurePopupWindow()) {
        ::SetWindowPos(hwnd_, HWND_TOPMOST, popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void CandidateUI::MoveWindowTo(const RECT& text_rect) {
    popup_pos_.x = text_rect.left;
    popup_pos_.y = text_rect.bottom + 4;

    if (hwnd_) {
        const UINT wnd_dpi = ::GetDpiForWindow(hwnd_);
        if (wnd_dpi > 0 && wnd_dpi != dpi_) {
            UpdateDpiAndFont(wnd_dpi);
            RecalculatePopupLayout();
        }
    }

    // Clamp to monitor work area so popup doesn't go off-screen
    HMONITOR mon = ::MonitorFromPoint(popup_pos_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(mon, &mi)) {
        if (popup_pos_.x + popup_width_ > mi.rcWork.right) {
            popup_pos_.x = std::max(mi.rcWork.left, mi.rcWork.right - popup_width_);
        }
        if (popup_pos_.y + popup_height_ > mi.rcWork.bottom) {
            popup_pos_.y = std::max(mi.rcWork.top, text_rect.top - popup_height_ - 4);
        }
    }

    if (hwnd_) {
        ::SetWindowPos(hwnd_, HWND_TOPMOST, popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
                       SWP_NOACTIVATE | (is_shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

bool CandidateUI::EnsurePopupWindow() noexcept {
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
        popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
        nullptr, nullptr, instance_, this);

    if (!hwnd_) {
        try {
            QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
        } catch (...) {
        }
    }
    return hwnd_ != nullptr;
}

void CandidateUI::DestroyPopupWindow() noexcept {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    is_shown_ = FALSE;
}

void CandidateUI::PaintPopup(HDC hdc, const RECT& client_rect) const {
    const auto& palette = GetCurrentThemePalette();
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(palette.window_bg));
    ::FillRect(hdc, &client_rect, bg_brush.Get());

    const ScopedSelectObject sel_font(hdc, font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    // Line 1: Wubi code + page indicator
    const auto& str = GetUiStrings(ui_language_);
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    std::wstring header = std::wstring(str.candidate_header_prefix) + raw_code_ + L"]";
    if (total_pages > 1) {
        header += L"  (" + std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages) + L")";
    }
    RECT top_rect = {ScaleByDpi(10), ScaleByDpi(6), client_rect.right - ScaleByDpi(10), ScaleByDpi(30)};
    ::SetTextColor(hdc, palette.text_accent);
    ::DrawTextW(hdc, header.c_str(), -1, &top_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Line 2: Horizontal candidates positioned using pre-measured hit rects
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    if (start >= end) {
        RECT bottom_rect = {ScaleByDpi(10), ScaleByDpi(32), client_rect.right - ScaleByDpi(10), client_rect.bottom - ScaleByDpi(4)};
        ::SetTextColor(hdc, palette.text_primary);
        ::DrawTextW(hdc, str.candidate_empty, -1, &bottom_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        const auto& cand_str = gsl::at(all_candidates_, i);
        const std::wstring label = (idx_in_page == selected_in_page_)
            ? (L"[" + std::to_wstring(idx_in_page + 1) + L"." + cand_str + L"]")
            : (std::to_wstring(idx_in_page + 1) + L"." + cand_str);

        RECT item_rc = (idx_in_page < candidate_hit_rects_.size())
            ? gsl::at(candidate_hit_rects_, idx_in_page)
            : RECT{ScaleByDpi(10), ScaleByDpi(32), client_rect.right - ScaleByDpi(10), client_rect.bottom - ScaleByDpi(4)};
        ::SetTextColor(hdc, idx_in_page == selected_in_page_ ? palette.text_accent : palette.text_primary);
        ::DrawTextW(hdc, label.c_str(), -1, &item_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
    }
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* self = reinterpret_cast<CandidateUI*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (msg == WM_LBUTTONDOWN) {
        self->OnLeftButtonDown(PointFromLParam(lparam));
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        self->UpdateDpiAndFont(HIWORD(wparam));
        self->RecalculatePopupLayout();
        return 0;
    }
    if (msg == WM_PAINT) {
        const ScopedPaintDC paint_dc(hwnd);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        self->PaintPopup(paint_dc.Get(), rc);
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace qiqi
