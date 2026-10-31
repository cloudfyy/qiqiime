#include "Ime/ui/candidate_ui.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "Ime/engine/fsm.h"
#include <gsl/gsl>
#include <oleauto.h>
#include <algorithm>
#include <cmath>
#include <span>

namespace qiqi {

CandidateUI::CandidateUI(HINSTANCE instance) noexcept : instance_(instance) {
    InitDirect2DAndDirectWrite();
    UpdateDpiAndFont(96);
}

CandidateUI::~CandidateUI() noexcept {
    DestroyPopupWindow();
}

void CandidateUI::InitDirect2DAndDirectWrite() noexcept {
    if (!CreateD2DDcTargetAndDWrite(d2d_factory_, dwrite_factory_, d2d_dc_target_)) {
        QIQI_LOG_WARN("CandidateUI::InitDirect2DAndDirectWrite incomplete (d2d={}, dwrite={}, target={}); GDI fallback active",
                      d2d_factory_.IsValid(), dwrite_factory_.IsValid(), d2d_dc_target_.IsValid());
    }
}

void CandidateUI::UpdateDpiAndFont(UINT new_dpi) noexcept {
    const UINT effective_dpi = new_dpi > 0 ? new_dpi : 96u;
    if (font_.IsValid() && dpi_ == effective_dpi && dwrite_text_format_) {
        return;
    }
    dpi_ = effective_dpi;
    popup_height_ = ScaleByDpi(kPopupHeight);

    font_ = CreateUiFont(ScaleByDpi(18), FW_NORMAL);

    if (dwrite_factory_) {
        if (SUCCEEDED(dwrite_factory_->CreateTextFormat(
                L"Microsoft YaHei UI",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(ScaleByDpi(16)),
                L"zh-CN",
                dwrite_text_format_.Put())) &&
            dwrite_text_format_) {
            dwrite_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            dwrite_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

int CandidateUI::MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept {
    if (text.empty()) {
        return 0;
    }
    if (dwrite_factory_ && dwrite_text_format_) {
        ScopedDWriteTextLayout layout;
        if (SUCCEEDED(dwrite_factory_->CreateTextLayout(
                text.data(),
                gsl::narrow_cast<UINT32>(text.size()),
                dwrite_text_format_.Get(),
                4096.0f,
                static_cast<float>(popup_height_),
                layout.Put())) &&
            layout) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                return gsl::narrow_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace));
            }
        }
    }
    if (fallback_dc) {
        SIZE sz{};
        if (::GetTextExtentPoint32W(
                fallback_dc, text.data(), gsl::narrow_cast<int>(text.size()), &sz)) {
            return sz.cx;
        }
    }
    return gsl::narrow_cast<int>(text.size()) * ScaleByDpi(12);
}

std::wstring CandidateUI::FormatHeaderText() const {
    const auto& str = GetUiStrings(ui_language_);
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    std::wstring header = std::wstring(str.candidate_header_prefix) + raw_code_ + L"]";
    if (total_pages > 1) {
        header += L"  (" + std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages) + L")";
    }
    return header;
}

std::wstring CandidateUI::FormatCandidateLabel(size_t index_in_page, std::wstring_view candidate_text) const {
    const std::wstring prefix = std::to_wstring(index_in_page + 1) + L"." + std::wstring(candidate_text);
    return (index_in_page == selected_in_page_) ? (L"[" + prefix + L"]") : prefix;
}

void CandidateUI::RecalculatePopupLayout() {
    candidate_hit_rects_.clear();
    const ScopedWindowDC screen_dc(hwnd_.Get());
    if (!screen_dc && !dwrite_text_format_) {
        popup_width_ = ScaleByDpi(kPopupWidth);
        popup_height_ = ScaleByDpi(kPopupHeight);
        return;
    }

    const ScopedSelectObject sel_font(screen_dc.Get(), font_.Get());

    const int margin_x = ScaleByDpi(10);
    const int item_gap = ScaleByDpi(16);
    const int row2_top = ScaleByDpi(32);
    const int row2_bottom = ScaleByDpi(kPopupHeight - 4);

    const int header_cx = MeasureTextWidth(screen_dc.Get(), FormatHeaderText());

    int cursor_x = margin_x;
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        const std::wstring label = FormatCandidateLabel(idx_in_page, gsl::at(all_candidates_, i));

        const int item_cx = MeasureTextWidth(screen_dc.Get(), label);
        const RECT hit_rc{cursor_x, row2_top, cursor_x + item_cx, row2_bottom};
        candidate_hit_rects_.push_back(hit_rc);
        cursor_x += item_cx + item_gap;
    }

    const int content_width = std::max<int>(header_cx + margin_x * 2, cursor_x + margin_x);
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
            ::ShowWindow(hwnd_.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
        }
    } else if (hwnd_) {
        ::ShowWindow(hwnd_.Get(), SW_HIDE);
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
        ::SetWindowPos(hwnd_.Get(), HWND_TOPMOST, popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
    }
}

void CandidateUI::MoveWindowTo(const RECT& text_rect) {
    popup_pos_.x = text_rect.left;
    popup_pos_.y = text_rect.bottom + 4;

    const MonitorMetrics mon = QueryMonitorForPoint(popup_pos_, hwnd_.Get());
    if (mon.dpi != dpi_) {
        UpdateDpiAndFont(mon.dpi);
        RecalculatePopupLayout();
    }

    // Flip above composition text if popup would overflow bottom of target monitor's work area
    if (popup_pos_.y + popup_height_ > mon.work_area.bottom) {
        popup_pos_.y = text_rect.top - popup_height_ - 4;
    }
    // Clamp all 4 edges (left/top/right/bottom, including negative multi-monitor coordinates)
    popup_pos_ = ClampPointToWorkArea(popup_pos_, popup_width_, popup_height_, mon.work_area);

    if (hwnd_) {
        ::SetWindowPos(hwnd_.Get(), HWND_TOPMOST, popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
                       SWP_NOACTIVATE | (is_shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

bool CandidateUI::EnsurePopupWindow() noexcept {
    if (hwnd_ && ::IsWindow(hwnd_.Get())) {
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

    hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandidateWndClass,
        L"",
        WS_POPUP | WS_BORDER,
        popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
        nullptr, nullptr, instance_, this));

    if (!hwnd_) {
        QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
    } else {
        QIQI_LOG_DEBUG("CandidateUI popup window created (pos=({}, {}), size={}x{}, dpi={})",
                       popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_);
    }
    return hwnd_.IsValid();
}

void CandidateUI::DestroyPopupWindow() noexcept {
    hwnd_.Reset();
    is_shown_ = FALSE;
}

bool CandidateUI::PaintPopupDirect2D(HDC hdc, const RECT& client_rect) const {
    if (!hdc || !d2d_dc_target_ || !dwrite_text_format_) {
        return false;
    }

    const auto& palette = GetCurrentThemePalette();
    const auto accent_brush = CreateSolidBrush(d2d_dc_target_.Get(), ColorRefToD2DColor(palette.text_accent));
    const auto primary_brush = CreateSolidBrush(d2d_dc_target_.Get(), ColorRefToD2DColor(palette.text_primary));
    if (!accent_brush || !primary_brush) {
        return false;
    }

    HRESULT end_hr = S_OK;
    {
        const ScopedD2DDcDraw draw_guard(d2d_dc_target_.Get(), hdc, client_rect, &end_hr);
        if (!draw_guard) {
            return false;
        }
        d2d_dc_target_->Clear(ColorRefToD2DColor(palette.window_bg));

        const auto& str = GetUiStrings(ui_language_);
        const std::wstring header = FormatHeaderText();
        const D2D1_RECT_F top_rc = D2D1::RectF(
            static_cast<float>(ScaleByDpi(10)),
            static_cast<float>(ScaleByDpi(6)),
            static_cast<float>(client_rect.right - ScaleByDpi(10)),
            static_cast<float>(ScaleByDpi(30)));
        d2d_dc_target_->DrawTextW(
            header.c_str(),
            gsl::narrow_cast<UINT32>(header.size()),
            dwrite_text_format_.Get(),
            top_rc,
            accent_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

        const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
        if (start >= end) {
            const D2D1_RECT_F empty_rc = D2D1::RectF(
                static_cast<float>(ScaleByDpi(10)),
                static_cast<float>(ScaleByDpi(32)),
                static_cast<float>(client_rect.right - ScaleByDpi(10)),
                static_cast<float>(client_rect.bottom - ScaleByDpi(4)));
            d2d_dc_target_->DrawTextW(
                str.candidate_empty,
                gsl::narrow_cast<UINT32>(wcslen(str.candidate_empty)),
                dwrite_text_format_.Get(),
                empty_rc,
                primary_brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        } else {
            for (size_t i = start; i < end; ++i) {
                const size_t idx_in_page = i - start;
                const std::wstring label = FormatCandidateLabel(idx_in_page, gsl::at(all_candidates_, i));

                const RECT item_rc = (idx_in_page < candidate_hit_rects_.size())
                    ? gsl::at(candidate_hit_rects_, idx_in_page)
                    : RECT{ScaleByDpi(10), ScaleByDpi(32), client_rect.right - ScaleByDpi(10), client_rect.bottom - ScaleByDpi(4)};
                const D2D1_RECT_F d2d_rc = D2D1::RectF(
                    static_cast<float>(item_rc.left),
                    static_cast<float>(item_rc.top),
                    static_cast<float>(item_rc.right + ScaleByDpi(8)),
                    static_cast<float>(item_rc.bottom));
                d2d_dc_target_->DrawTextW(
                    label.c_str(),
                    gsl::narrow_cast<UINT32>(label.size()),
                    dwrite_text_format_.Get(),
                    d2d_rc,
                    (idx_in_page == selected_in_page_) ? accent_brush.Get() : primary_brush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
        }
    }

    if (FAILED(end_hr)) {
        QIQI_LOG_WARN("CandidateUI::PaintPopupDirect2D EndDraw failed; falling back to GDI rendering");
        d2d_dc_target_.Reset();
        return false;
    }
    return true;
}

void CandidateUI::PaintPopup(HDC hdc, const RECT& client_rect) const {
    if (PaintPopupDirect2D(hdc, client_rect)) {
        return;
    }
    const ScopedSaveDC save_dc(hdc);
    const auto& palette = GetCurrentThemePalette();
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(palette.window_bg));
    ::FillRect(hdc, &client_rect, bg_brush.Get());

    const ScopedSelectObject sel_font(hdc, font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    // Line 1: Wubi code + page indicator
    const auto& str = GetUiStrings(ui_language_);
    const std::wstring header = FormatHeaderText();
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
        const std::wstring label = FormatCandidateLabel(idx_in_page, gsl::at(all_candidates_, i));

        RECT item_rc = (idx_in_page < candidate_hit_rects_.size())
            ? gsl::at(candidate_hit_rects_, idx_in_page)
            : RECT{ScaleByDpi(10), ScaleByDpi(32), client_rect.right - ScaleByDpi(10), client_rect.bottom - ScaleByDpi(4)};
        ::SetTextColor(hdc, idx_in_page == selected_in_page_ ? palette.text_accent : palette.text_primary);
        ::DrawTextW(hdc, label.c_str(), -1, &item_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
    }
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = BindOrGetWindowUserData<CandidateUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            self->OnLeftButtonDown(PointFromLParam(lparam));
            return 0;
        case WM_DPICHANGED:
            self->UpdateDpiAndFont(HIWORD(wparam));
            self->RecalculatePopupLayout();
            if (lparam != 0) {
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                self->popup_pos_.x = suggested->left;
                self->popup_pos_.y = suggested->top;
                ::SetWindowPos(
                    hwnd,
                    HWND_TOPMOST,
                    self->popup_pos_.x,
                    self->popup_pos_.y,
                    self->popup_width_,
                    self->popup_height_,
                    SWP_NOACTIVATE | (self->is_shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            self->RecalculatePopupLayout();
            ::InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [self](HDC hdc, const RECT& rc) { self->PaintPopup(hdc, rc); });
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace qiqi
