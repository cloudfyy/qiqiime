#include "ime/ui/candidate_ui.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/fsm/fsm.h"
#include <gsl/gsl>
#include <oleauto.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <span>

namespace qiqi {
namespace {
constexpr float kTextMeasurementWidth = 4096.0f;
constexpr int kGdiFontHeight = 18;
constexpr int kDirectWriteFontSize = 16;
constexpr int kFallbackCharacterWidth = 12;
constexpr int kHorizontalMargin = 10;
constexpr int kCandidateGap = 16;
constexpr int kHeaderTop = 6;
constexpr int kHeaderBottom = 30;
constexpr int kCandidateRowTop = 32;
constexpr int kBottomPadding = 4;
constexpr int kNavigationWidthReserve = 52;
constexpr int kPageButtonWidth = 20;
constexpr int kPageButtonGap = 4;
constexpr int kTextOverhangAllowance = 8;
constexpr int kCaretGapPx = 4;
constexpr float kDisabledChevronOpacity = 0.55f;
constexpr float kChevronMinimumStrokePx = 1.4f;
constexpr int kChevronStrokeWidth = 2;
constexpr float kChevronStrokeScale = 0.85f;
constexpr int kChevronArmHeight = 4;
constexpr float kChevronArmWidthRatio = 0.55f;
}

CandidateUI::CandidateUI(HINSTANCE instance) noexcept : instance_(instance) {
    InitDirect2DAndDirectWrite();
    UpdateDpiAndFont(96);
}

CandidateUI::~CandidateUI() noexcept {
    std::ignore = DestroyPopupWindow();
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

    font_ = CreateUiFont(ScaleByDpi(kGdiFontHeight), FW_NORMAL);

    if (dwrite_factory_) {
        if (log::CheckHResult(dwrite_factory_->CreateTextFormat(
                L"Microsoft YaHei UI",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(ScaleByDpi(kDirectWriteFontSize)),
                L"zh-CN",
                dwrite_text_format_.Put()), "CandidateUI::CreateTextFormat") &&
            dwrite_text_format_) {
            std::ignore = log::CheckHResult(dwrite_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                                           "CandidateUI::SetWordWrapping");
            std::ignore = log::CheckHResult(dwrite_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                                           "CandidateUI::SetParagraphAlignment");
        }
    }
}

int CandidateUI::MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept {
    if (text.empty()) {
        return 0;
    }
    if (dwrite_factory_ && dwrite_text_format_) {
        ScopedDWriteTextLayout layout;
        if (log::CheckHResult(dwrite_factory_->CreateTextLayout(
                text.data(),
                gsl::narrow_cast<UINT32>(text.size()),
                dwrite_text_format_.Get(),
                kTextMeasurementWidth,
                static_cast<float>(popup_height_),
                layout.Put()), "CandidateUI::CreateTextLayout", log::Level::debug) &&
            layout) {
            DWRITE_TEXT_METRICS metrics{};
            if (log::CheckHResult(layout->GetMetrics(&metrics), "CandidateUI::GetMetrics", log::Level::debug)) {
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
    return gsl::narrow_cast<int>(text.size()) * ScaleByDpi(kFallbackCharacterWidth);
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
    prev_page_hit_rect_ = {};
    next_page_hit_rect_ = {};
    const ScopedWindowDC screen_dc(hwnd_.Get());
    if (!screen_dc && !dwrite_text_format_) {
        popup_width_ = ScaleByDpi(kPopupWidth);
        popup_height_ = ScaleByDpi(kPopupHeight);
        return;
    }

    const ScopedSelectObject sel_font(screen_dc.Get(), font_.Get());

    const int margin_x = ScaleByDpi(kHorizontalMargin);
    const int item_gap = ScaleByDpi(kCandidateGap);
    const int row2_top = ScaleByDpi(kCandidateRowTop);
    const int row2_bottom = ScaleByDpi(kPopupHeight - kBottomPadding);
    const int row_height = row2_bottom - row2_top;
    const bool vertical = layout_ == CandidateLayout::Vertical;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    const int nav_reserve_cx = (total_pages > 1) ? ScaleByDpi(kNavigationWidthReserve) : 0;

    const int header_cx = MeasureTextWidth(screen_dc.Get(), FormatHeaderText());

    int cursor_x = margin_x;
    int widest_item = 0;
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    if (vertical && start >= end) {
        widest_item = MeasureTextWidth(screen_dc.Get(), GetUiStrings(ui_language_).candidate_empty);
    }
    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        const std::wstring label = FormatCandidateLabel(idx_in_page, gsl::at(all_candidates_, i));

        const int item_cx = MeasureTextWidth(screen_dc.Get(), label);
        const int row_top = vertical ? row2_top + gsl::narrow_cast<int>(idx_in_page) * row_height : row2_top;
        const int item_left = vertical ? margin_x : cursor_x;
        const RECT hit_rc{item_left, row_top, item_left + item_cx, row_top + row_height};
        candidate_hit_rects_.push_back(hit_rc);
        cursor_x += item_cx + item_gap;
        widest_item = std::max(widest_item, item_cx);
    }

    const int content_width = std::max<int>(
        header_cx + margin_x * 2 + nav_reserve_cx,
        vertical ? widest_item + margin_x * 2 : cursor_x + margin_x + nav_reserve_cx);
    popup_width_ = vertical ? content_width : std::max(ScaleByDpi(kPopupWidth), content_width);
    popup_height_ = vertical
        ? row2_top + gsl::narrow_cast<int>(std::max<size_t>(1, end - start)) * row_height + ScaleByDpi(kBottomPadding)
        : ScaleByDpi(kPopupHeight);
    if (vertical) {
        for (auto& rect : candidate_hit_rects_) {
            rect.right = popup_width_ - margin_x;
        }
    }

    if (total_pages > 1) {
        const int btn_w = ScaleByDpi(kPageButtonWidth);
        const int btn_gap = ScaleByDpi(kPageButtonGap);
        const int next_right = popup_width_ - margin_x;
        const int next_left = next_right - btn_w;
        const int prev_right = next_left - btn_gap;
        const int prev_left = prev_right - btn_w;
        const int nav_top = vertical ? ScaleByDpi(kHeaderTop) : row2_top;
        const int nav_bottom = vertical ? ScaleByDpi(kHeaderBottom) : row2_bottom;
        prev_page_hit_rect_ = RECT{prev_left, nav_top, prev_right, nav_bottom};
        next_page_hit_rect_ = RECT{next_left, nav_top, next_right, nav_bottom};
    }
}

void CandidateUI::OnLeftButtonDown(POINT pt) const {
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    if (total_pages > 1 && page_turn_callback_) {
        if (current_page_ > 0 && ::PtInRect(&prev_page_hit_rect_, pt)) {
            page_turn_callback_(false);
            return;
        }
        if (current_page_ + 1 < total_pages && ::PtInRect(&next_page_hit_rect_, pt)) {
            page_turn_callback_(true);
            return;
        }
    }
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
    const bool blocked = bShow && (!presentation_enabled_ || destroying_window_);
    if (blocked) bShow = FALSE;
    is_shown_ = bShow;
    if (bShow) {
        if (EnsurePopupWindow()) {
            ::ShowWindow(hwnd_.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
            LogPopupVisibility();
        }
    } else if (hwnd_) {
        ::ShowWindow(hwnd_.Get(), SW_HIDE);
    }
    return blocked ? S_FALSE : S_OK;
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

    RefreshPopupLayout();
}

void CandidateUI::MoveWindowTo(const RECT& text_rect, HWND host_window) {
    const HWND owner = host_window ? ::GetAncestor(host_window, GA_ROOT) : nullptr;
    if (host_window && !owner) {
        QIQI_LOG_WARN("CandidateUI::MoveWindowTo host window has no root; using unowned popup");
    }
    if (owner != popup_owner_ && hwnd_) {
        // Establish the host relationship at creation, including for immersive hosts.
        const BOOL was_shown = is_shown_;
        if (!DestroyPopupWindow()) return;
        is_shown_ = was_shown;
    }
    popup_owner_ = owner;
    text_rect_ = text_rect;
    RefreshPopupLayout();
    if (is_shown_ && !hwnd_) {
        std::ignore = Show(TRUE);
    }
}

void CandidateUI::SetLayout(CandidateLayout layout) {
    if (layout_ == layout) return;
    layout_ = layout;
    RefreshPopupLayout();
}

void CandidateUI::RefreshPopupLayout(UINT dpi) {
    const POINT anchor = text_rect_
        ? POINT{text_rect_->left, text_rect_->bottom + kCaretGapPx}
        : popup_pos_;
    const MonitorMetrics mon = QueryMonitorForPoint(anchor, hwnd_.Get());
    UpdateDpiAndFont(dpi > 0 ? dpi : mon.dpi);
    RecalculatePopupLayout();
    popup_pos_ = anchor;
    if (text_rect_ && popup_pos_.y + popup_height_ > mon.work_area.bottom) {
        popup_pos_.y = text_rect_->top - popup_height_ - kCaretGapPx;
    }
    popup_pos_ = ClampPointToWorkArea(popup_pos_, popup_width_, popup_height_, mon.work_area);

    const RECT text_rect = text_rect_.value_or(RECT{anchor.x, anchor.y, anchor.x, anchor.y});
    QIQI_LOG_DEBUG("CandidatePositionPopup(stage=move, rect=({}, {}, {}, {}), target=({}, {}), size={}x{}, dpi={}, has_window={}, shown={})",
                   text_rect.left, text_rect.top, text_rect.right, text_rect.bottom,
                   popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_, hwnd_.Get() != nullptr, is_shown_ != FALSE);
    if (hwnd_) {
        if (!::SetWindowPos(hwnd_.Get(), HWND_TOPMOST, popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
                            SWP_NOACTIVATE | (is_shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW))) {
            QIQI_LOG_ERROR("CandidatePositionPopup::SetWindowPos failed (error={})", ::GetLastError());
        } else if (log::ShouldLog(log::Level::debug)) {
            RECT actual{};
            if (::GetWindowRect(hwnd_.Get(), &actual)) {
                QIQI_LOG_DEBUG("CandidatePositionPopup(stage=moved, rect=({}, {}, {}, {}))",
                               actual.left, actual.top, actual.right, actual.bottom);
            } else {
                QIQI_LOG_ERROR("CandidatePositionPopup::GetWindowRect failed (error={})", ::GetLastError());
            }
        }
        ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
    }
}

bool CandidateUI::EnsurePopupWindow() noexcept {
    if (!presentation_enabled_ || destroying_window_) return false;
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

    if (popup_owner_ && !::IsWindow(popup_owner_)) {
        QIQI_LOG_WARN("CandidateUI::EnsurePopupWindow host window was destroyed; using unowned popup");
        popup_owner_ = nullptr;
    }
    hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandidateWndClass,
        L"",
        WS_POPUP | WS_BORDER,
        popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
        popup_owner_, nullptr, instance_, this));

    if (!hwnd_) {
        QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
    } else {
        QIQI_LOG_DEBUG("CandidateUI popup window created (pos=({}, {}), size={}x{}, dpi={})",
                       popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_);
    }
    return hwnd_.IsValid();
}

void CandidateUI::LogPopupVisibility() const noexcept {
    if (!log::ShouldLog(log::Level::debug) || !hwnd_) return;
    WINDOWINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetWindowInfo(hwnd_.Get(), &info)) {
        QIQI_LOG_ERROR("CandidateUI::LogPopupVisibility GetWindowInfo failed (error={})", ::GetLastError());
        return;
    }
    DWORD cloaked = 0;
    const HRESULT cloak_hr = ::DwmGetWindowAttribute(hwnd_.Get(), DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    std::ignore = log::CheckHResult(cloak_hr, "CandidateUI::DwmGetWindowAttribute(DWMWA_CLOAKED)", log::Level::debug);
    QIQI_LOG_DEBUG("CandidateVisibility(visible={}, topmost={}, has_owner={}, cloaked={}, cloak_hr=0x{:08x}, rect=({}, {}, {}, {}))",
                   ::IsWindowVisible(hwnd_.Get()) != FALSE, (info.dwExStyle & WS_EX_TOPMOST) != 0,
                   ::GetWindow(hwnd_.Get(), GW_OWNER) != nullptr,
                   SUCCEEDED(cloak_hr) ? gsl::narrow_cast<int>(cloaked) : -1,
                   gsl::narrow_cast<unsigned int>(cloak_hr),
                   info.rcWindow.left, info.rcWindow.top, info.rcWindow.right, info.rcWindow.bottom);
}

bool CandidateUI::DestroyPopupWindow() noexcept {
    if (destroying_window_) return true;
    if (!CheckWindowOwnerThread(hwnd_.Get(), "CandidateUI::DestroyPopupWindow")) return false;
    destroying_window_ = true;
    const auto finish = gsl::finally([this]() noexcept { destroying_window_ = false; });
    is_shown_ = FALSE;
    return DestroyWindowOnOwnerThread(hwnd_, "CandidateUI::DestroyPopupWindow");
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
            static_cast<float>(ScaleByDpi(kHorizontalMargin)),
            static_cast<float>(ScaleByDpi(kHeaderTop)),
            static_cast<float>(client_rect.right - ScaleByDpi(kHorizontalMargin)),
            static_cast<float>(ScaleByDpi(kHeaderBottom)));
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
                static_cast<float>(ScaleByDpi(kHorizontalMargin)),
                static_cast<float>(ScaleByDpi(kCandidateRowTop)),
                static_cast<float>(client_rect.right - ScaleByDpi(kHorizontalMargin)),
                static_cast<float>(client_rect.bottom - ScaleByDpi(kBottomPadding)));
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
                    : RECT{ScaleByDpi(kHorizontalMargin), ScaleByDpi(kCandidateRowTop),
                           client_rect.right - ScaleByDpi(kHorizontalMargin), client_rect.bottom - ScaleByDpi(kBottomPadding)};
                const D2D1_RECT_F d2d_rc = D2D1::RectF(
                    static_cast<float>(item_rc.left),
                    static_cast<float>(item_rc.top),
                    static_cast<float>(item_rc.right + ScaleByDpi(kTextOverhangAllowance)),
                    static_cast<float>(item_rc.bottom));
                d2d_dc_target_->DrawTextW(
                    label.c_str(),
                    gsl::narrow_cast<UINT32>(label.size()),
                    dwrite_text_format_.Get(),
                    d2d_rc,
                    (idx_in_page == selected_in_page_) ? accent_brush.Get() : primary_brush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }

            const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
            if (total_pages > 1) {
                const auto disabled_brush = CreateSolidBrush(
                    d2d_dc_target_.Get(), ColorRefToD2DColor(palette.grip_dot, kDisabledChevronOpacity));
                const float stroke_w = std::max(
                    kChevronMinimumStrokePx, static_cast<float>(ScaleByDpi(kChevronStrokeWidth)) * kChevronStrokeScale);
                const float arm = static_cast<float>(ScaleByDpi(kChevronArmHeight));
                const auto draw_chevron = [&](const RECT& btn_rc, bool is_next, bool enabled) noexcept {
                    ID2D1SolidColorBrush* const br = enabled ? accent_brush.Get() : disabled_brush.Get();
                    if (!br) {
                        return;
                    }
                    const float cx = static_cast<float>(btn_rc.left + btn_rc.right) * 0.5f;
                    const float cy = static_cast<float>(btn_rc.top + btn_rc.bottom) * 0.5f;
                    const float dx = is_next ? arm * kChevronArmWidthRatio : -arm * kChevronArmWidthRatio;
                    d2d_dc_target_->DrawLine(
                        D2D1::Point2F(cx - dx, cy - arm),
                        D2D1::Point2F(cx + dx, cy),
                        br,
                        stroke_w);
                    d2d_dc_target_->DrawLine(
                        D2D1::Point2F(cx + dx, cy),
                        D2D1::Point2F(cx - dx, cy + arm),
                        br,
                        stroke_w);
                };
                draw_chevron(prev_page_hit_rect_, false, current_page_ > 0);
                draw_chevron(next_page_hit_rect_, true, current_page_ + 1 < total_pages);
            }
        }
    }

    if (FAILED(end_hr)) {
        QIQI_LOG_WARN("CandidateUI::PaintPopupDirect2D EndDraw failed (hr=0x{:08x}); falling back to GDI rendering",
                      gsl::narrow_cast<unsigned int>(end_hr));
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
    RECT top_rect = {
        ScaleByDpi(kHorizontalMargin), ScaleByDpi(kHeaderTop),
        client_rect.right - ScaleByDpi(kHorizontalMargin), ScaleByDpi(kHeaderBottom)};
    ::SetTextColor(hdc, palette.text_accent);
    ::DrawTextW(hdc, header.c_str(), -1, &top_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Both layouts paint and hit-test the same measured rectangles.
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    if (start >= end) {
        RECT bottom_rect = {
            ScaleByDpi(kHorizontalMargin), ScaleByDpi(kCandidateRowTop),
            client_rect.right - ScaleByDpi(kHorizontalMargin), client_rect.bottom - ScaleByDpi(kBottomPadding)};
        ::SetTextColor(hdc, palette.text_primary);
        ::DrawTextW(hdc, str.candidate_empty, -1, &bottom_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    for (size_t i = start; i < end; ++i) {
        const size_t idx_in_page = i - start;
        const std::wstring label = FormatCandidateLabel(idx_in_page, gsl::at(all_candidates_, i));

        RECT item_rc = (idx_in_page < candidate_hit_rects_.size())
            ? gsl::at(candidate_hit_rects_, idx_in_page)
            : RECT{ScaleByDpi(kHorizontalMargin), ScaleByDpi(kCandidateRowTop),
                   client_rect.right - ScaleByDpi(kHorizontalMargin), client_rect.bottom - ScaleByDpi(kBottomPadding)};
        ::SetTextColor(hdc, idx_in_page == selected_in_page_ ? palette.text_accent : palette.text_primary);
        ::DrawTextW(hdc, label.c_str(), -1, &item_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
    }

    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    if (total_pages > 1) {
        RECT prev_rc = prev_page_hit_rect_;
        RECT next_rc = next_page_hit_rect_;
        ::SetTextColor(hdc, current_page_ > 0 ? palette.text_accent : palette.grip_dot);
        ::DrawTextW(hdc, L"‹", -1, &prev_rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        ::SetTextColor(hdc, (current_page_ + 1 < total_pages) ? palette.text_accent : palette.grip_dot);
        ::DrawTextW(hdc, L"›", -1, &next_rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = BindOrGetWindowUserData<CandidateUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
        case WM_NCDESTROY:
            if (self->hwnd_.Get() == hwnd) {
                std::ignore = self->hwnd_.Detach();
                self->is_shown_ = FALSE;
                self->popup_owner_ = nullptr;
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            self->OnLeftButtonDown(PointFromLParam(lparam));
            return 0;
        case WM_DPICHANGED:
            if (!self->text_rect_ && lparam != 0) {
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                self->popup_pos_.x = suggested->left;
                self->popup_pos_.y = suggested->top;
            }
            self->RefreshPopupLayout(HIWORD(wparam));
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            self->RefreshPopupLayout();
            return 0;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [self](HDC hdc, const RECT& rc) { self->PaintPopup(hdc, rc); });
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace qiqi
