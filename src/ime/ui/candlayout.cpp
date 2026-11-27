// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "ime/fsm/fsm.h"
#include <gsl/gsl>
#include <algorithm>
#include <cmath>

namespace qiqi {
namespace {
constexpr float kTextMeasurementWidth = 4096.0f;
constexpr int kFallbackCharacterWidth = 12;
constexpr int kHorizontalMargin = 10;
constexpr int kCandidateGap = 16;
constexpr int kHeaderTop = 6;
constexpr int kCandidateRowTop = 32;
constexpr int kBottomPadding = 4;
constexpr int kPageButtonWidth = 20;
constexpr int kPageButtonGap = 4;
constexpr int kCaretGapPx = 4;
} // namespace

void CandidateUI::UpdateDpiAndFont(UINT new_dpi) noexcept {
    const UINT effective_dpi = new_dpi > 0 ? new_dpi : 96u;
    const int text_scale_percent = TextScalePercent(preferences_.text_scale);
    if (font_.IsValid() && dpi_ == effective_dpi && font_scale_percent_ == text_scale_percent) {
        return;
    }
    dpi_ = effective_dpi;
    font_scale_percent_ = text_scale_percent;
    popup_height_ = ScaleByDpi(kPopupHeight);

    font_ = CreateCandidateFont(
        font_preferences_, kCandidateFontDefaultHeightPx, dpi_, preferences_.text_scale);
    if (!font_) {
        QIQI_LOG_ERROR("CandidateUI custom font unavailable; using the system UI font");
        font_ = CreateUiFont(ScaleText(kCandidateFontDefaultHeightPx), FW_NORMAL);
    }
    const ScopedWindowDC dc(hwnd_.Get());
    const ScopedSelectObject select_font(dc.Get(), font_.Get());
    TEXTMETRICW metrics{};
    line_height_ = dc && ::GetTextMetricsW(dc.Get(), &metrics)
        ? std::max(ScaleText(24), gsl::narrow_cast<int>(metrics.tmHeight + metrics.tmExternalLeading))
        : ScaleText(24);

    if (dwrite_factory_) {
        LOGFONTW font_details{};
        std::ignore = ::GetObjectW(font_.Get(), sizeof(font_details), &font_details);
        if (log::CheckHResult(dwrite_factory_->CreateTextFormat(
                gsl::at(font_details.lfFaceName, 0) ? std::data(font_details.lfFaceName) : L"Microsoft YaHei UI",
                nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(font_details.lfWeight),
                font_details.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(std::abs(gsl::narrow_cast<int>(font_details.lfHeight))),
                L"zh-CN",
                dwrite_text_format_.Put()), "CandidateUI::CreateTextFormat") &&
            dwrite_text_format_) {
            std::ignore = log::CheckHResult(dwrite_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                                           "CandidateUI::SetWordWrapping");
            std::ignore = log::CheckHResult(dwrite_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR),
                                           "CandidateUI::SetParagraphAlignment");
        }
    }
}

int CandidateUI::MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept {
    if (text.empty()) {
        return 0;
    }
    int width = 0;
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
                width = gsl::narrow_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace));
            }
        }
    }
    if (fallback_dc) {
        SIZE sz{};
        if (::GetTextExtentPoint32W(
                fallback_dc, text.data(), gsl::narrow_cast<int>(text.size()), &sz)) {
            return std::max(width, gsl::narrow_cast<int>(sz.cx));
        }
    }
    return std::max(width, gsl::narrow_cast<int>(text.size()) * ScaleText(kFallbackCharacterWidth));
}

std::wstring CandidateUI::WrapText(HDC dc, std::wstring_view text, int width) const {
    std::wstring wrapped;
    while (!text.empty()) {
        const size_t newline = text.find(L'\n');
        const size_t limit = newline == std::wstring_view::npos ? text.size() : newline;
        size_t low = 0;
        size_t high = limit;
        while (low < high) {
            const size_t middle = low + (high - low + 1) / 2;
            if (MeasureTextWidth(dc, text.substr(0, middle)) <= width) low = middle;
            else high = middle - 1;
        }
        size_t count = std::max<size_t>(1, low);
        count = std::min(count, text.size());
        if (count < text.size() && IS_HIGH_SURROGATE(gsl::at(text, count - 1)) && IS_LOW_SURROGATE(gsl::at(text, count))) {
            count = count > 1 ? count - 1 : std::min<size_t>(2, text.size());
        }
        if (limit == 0) count = 0;
        wrapped.append(text.substr(0, count));
        text.remove_prefix(count);
        if (!text.empty()) {
            wrapped += L'\n';
            if (text.front() == L'\n') text.remove_prefix(1);
        }
    }
    return wrapped;
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
    candidate_labels_.clear();
    prev_page_hit_rect_ = {};
    next_page_hit_rect_ = {};
    const ScopedWindowDC screen_dc(hwnd_.Get());
    const ScopedSelectObject sel_font(screen_dc.Get(), font_.Get());

    const int margin_x = std::min(ScaleByDpi(kHorizontalMargin), std::max(0, available_width_ / 8));
    const int item_gap = ScaleByDpi(kCandidateGap);
    const auto metrics = PaintMetrics();
    const int padding = metrics.horizontal_padding;
    const int vertical_padding = metrics.vertical_padding;
    const int row_height = std::max(line_height_ + vertical_padding * 2, ScaleByDpi(preferences_.easy_reading ? 44 : 28));
    const bool vertical = layout_ == CandidateLayout::Vertical;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    const auto page_labels = ResolveCandidatePageLabels(ui_language_, preferences_);
    const int button_width = preferences_.easy_reading
        ? std::max(
              ScaleByDpi(44),
              std::max(MeasureTextWidth(screen_dc.Get(), page_labels.previous),
                       MeasureTextWidth(screen_dc.Get(), page_labels.next)) + padding * 2)
        : ScaleByDpi(kPageButtonWidth);
    const int button_gap = ScaleByDpi(kPageButtonGap);
    const int navigation_width = total_pages > 1 ? button_width * 2 + button_gap : 0;
    const int header_width = MeasureTextWidth(screen_dc.Get(), FormatHeaderText());
    int natural_width = header_width + navigation_width + margin_x * 2 + (navigation_width ? item_gap : 0);
    int candidate_width = vertical ? 0 : margin_x;
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    for (size_t i = start; i < end; ++i) {
        const int width = std::max(MeasureTextWidth(screen_dc.Get(), FormatCandidateLabel(i - start, gsl::at(all_candidates_, i))) + padding * 2,
                                   ScaleByDpi(preferences_.easy_reading ? 44 : 0));
        candidate_width = vertical ? std::max(candidate_width, width) : candidate_width + width + item_gap;
    }
    if (start >= end) candidate_width = MeasureTextWidth(screen_dc.Get(), GetUiStrings(ui_language_).candidate_empty);
    natural_width = std::max(natural_width, candidate_width + margin_x * 2);
    if (!vertical) natural_width = std::max(ScaleByDpi(kPopupWidth), natural_width);
    popup_width_ = std::clamp(natural_width, 1, available_width_);

    // A second pass reserves the native scrollbar before wrapping, not after sizing.
    for (const int pass : {0, 1}) {
        const int scrollbar = pass == 1 ? ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_) : 0;
        content_width_ = std::max(1, popup_width_ - scrollbar);
        const int inner_width = std::max(1, content_width_ - margin_x * 2);
        const int nav_top = ScaleByDpi(kHeaderTop);
        const bool nav_same_row = navigation_width > 0 && navigation_width + item_gap < inner_width;
        const int header_available = nav_same_row ? inner_width - navigation_width - item_gap : inner_width;
        header_label_ = WrapText(screen_dc.Get(), FormatHeaderText(), header_available);
        const int header_height = (gsl::narrow_cast<int>(std::count(header_label_.begin(), header_label_.end(), L'\n')) + 1) * line_height_;
        header_rect_ = {margin_x, nav_top, margin_x + header_available, nav_top + header_height};
        int row_top = std::max(ScaleByDpi(kCandidateRowTop), nav_top + header_height + vertical_padding);
        if (navigation_width) {
            const int top = nav_same_row ? nav_top : row_top;
            const bool stacked = navigation_width > inner_width;
            const int actual_button_width = stacked ? inner_width : button_width;
            const int right = content_width_ - margin_x;
            prev_page_label_ = WrapText(screen_dc.Get(), page_labels.previous,
                std::max(1, actual_button_width - padding * 2));
            next_page_label_ = WrapText(screen_dc.Get(), page_labels.next,
                std::max(1, actual_button_width - padding * 2));
            const int label_lines = 1 + std::max(
                gsl::narrow_cast<int>(std::count(prev_page_label_.begin(), prev_page_label_.end(), L'\n')),
                gsl::narrow_cast<int>(std::count(next_page_label_.begin(), next_page_label_.end(), L'\n')));
            const int navigation_height = preferences_.easy_reading
                ? std::max(row_height, label_lines * line_height_ + padding * 2) : line_height_;
            const int next_top = stacked ? top + navigation_height + vertical_padding : top;
            next_page_hit_rect_ = {right - actual_button_width, next_top, right, next_top + navigation_height};
            const int previous_right = stacked ? right : right - actual_button_width - button_gap;
            prev_page_hit_rect_ = {previous_right - actual_button_width, top, previous_right, top + navigation_height};
            row_top = std::max(row_top, next_top + navigation_height + vertical_padding);
        }
        candidate_hit_rects_.clear();
        candidate_labels_.clear();
        int cursor_x = margin_x;
        int row_bottom = row_top + row_height;
        for (size_t i = start; i < end; ++i) {
            const std::wstring label = FormatCandidateLabel(i - start, gsl::at(all_candidates_, i));
            const int width = vertical ? inner_width : std::min(inner_width,
                std::max(MeasureTextWidth(screen_dc.Get(), label) + padding * 2, ScaleByDpi(preferences_.easy_reading ? 44 : 0)));
            if (cursor_x > margin_x && (vertical || cursor_x + width > content_width_ - margin_x)) {
                row_top = row_bottom;
                cursor_x = margin_x;
            }
            std::wstring wrapped = WrapText(screen_dc.Get(), label, std::max(1, width - padding * 2));
            const int lines = gsl::narrow_cast<int>(std::count(wrapped.begin(), wrapped.end(), L'\n')) + 1;
            const int height = std::max(row_height, lines * line_height_ + vertical_padding * 2);
            candidate_hit_rects_.push_back(RECT{cursor_x, row_top, cursor_x + width, row_top + height});
            candidate_labels_.push_back(std::move(wrapped));
            row_bottom = std::max(row_bottom, row_top + height);
            cursor_x += width + item_gap;
        }
        if (start >= end) {
            empty_label_ = WrapText(screen_dc.Get(), GetUiStrings(ui_language_).candidate_empty, inner_width);
            const int empty_height = std::max(row_height,
                (gsl::narrow_cast<int>(std::count(empty_label_.begin(), empty_label_.end(), L'\n')) + 1) * line_height_ + vertical_padding * 2);
            empty_rect_ = {margin_x, row_top, margin_x + inner_width, row_top + empty_height};
            row_bottom = row_top + empty_height;
        }
        content_height_ = std::max(ScaleByDpi(kPopupHeight), row_bottom + ScaleByDpi(kBottomPadding));
        popup_height_ = std::min(content_height_, available_height_);
        if (content_height_ <= available_height_ || pass == 1) break;
    }
    scroll_offset_ = std::clamp(scroll_offset_, 0, std::max(0, content_height_ - popup_height_));
    if (selected_in_page_ < candidate_hit_rects_.size()) {
        const RECT selected = gsl::at(candidate_hit_rects_, selected_in_page_);
        if (selected.top < scroll_offset_ || selected.bottom - selected.top > popup_height_) {
            scroll_offset_ = selected.top;
        } else if (selected.bottom > scroll_offset_ + popup_height_) {
            scroll_offset_ = selected.bottom - popup_height_;
        }
    }
    scroll_offset_ = std::clamp(scroll_offset_, 0, std::max(0, content_height_ - popup_height_));
}


RECT CandidateUI::ViewportRect(RECT rect) const noexcept {
    if (rect.right > rect.left) ::OffsetRect(&rect, 0, -scroll_offset_);
    return rect;
}

void CandidateUI::UpdateScrollBar() const noexcept {
    if (!hwnd_) return;
    if (content_height_ <= popup_height_) {
        return;
    }
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    info.nMin = 0;
    info.nMax = std::max(0, content_height_ - 1);
    info.nPage = gsl::narrow_cast<UINT>(popup_height_);
    info.nPos = scroll_offset_;
    SCROLLINFO current{};
    current.cbSize = sizeof(current);
    current.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (::GetScrollInfo(hwnd_.Get(), SB_VERT, &current)) {
        if (current.nMin == info.nMin && current.nMax == info.nMax &&
            current.nPage == info.nPage && current.nPos == info.nPos) return;
    } else {
        QIQI_LOG_WARN("CandidateUI::GetScrollInfo failed (error={})", ::GetLastError());
    }
    ::SetScrollInfo(hwnd_.Get(), SB_VERT, &info, TRUE);
}

void CandidateUI::ScrollContent(int position) {
    const int next = std::clamp(position, 0, std::max(0, content_height_ - popup_height_));
    if (scroll_offset_ == next) return;
    scroll_offset_ = next;
    UpdateScrollBar();
    if (hwnd_) ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
}

void CandidateUI::RefreshPopupLayout(UINT dpi) {
    const ComPtr<CandidateUI> keep_alive(this);
    const POINT anchor = text_rect_
        ? POINT{text_rect_->left, text_rect_->bottom + kCaretGapPx}
        : popup_pos_;
    const MonitorMetrics mon = QueryMonitorForPoint(anchor, hwnd_.Get());
    available_width_ = std::max(1L, mon.work_area.right - mon.work_area.left);
    available_height_ = std::max(1L, mon.work_area.bottom - mon.work_area.top);
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
        const LONG_PTR style = ::GetWindowLongPtrW(hwnd_.Get(), GWL_STYLE);
        const LONG_PTR desired_style = content_height_ > popup_height_ ? style | WS_VSCROLL : style & ~WS_VSCROLL;
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
        if (style != desired_style) {
            ::SetLastError(ERROR_SUCCESS);
            if (!::SetWindowLongPtrW(hwnd_.Get(), GWL_STYLE, desired_style) &&
                ::GetLastError() != ERROR_SUCCESS) {
                QIQI_LOG_ERROR("CandidateUI::SetWindowLongPtrW(style) failed (error={})", ::GetLastError());
                return;
            }
            flags |= SWP_FRAMECHANGED;
        }
        RECT current{};
        if (::GetWindowRect(hwnd_.Get(), &current)) {
            if (current.left == popup_pos_.x && current.top == popup_pos_.y) flags |= SWP_NOMOVE;
            if (current.right - current.left == popup_width_ &&
                current.bottom - current.top == popup_height_) flags |= SWP_NOSIZE;
        } else {
            QIQI_LOG_WARN("CandidateUI::GetWindowRect failed (error={})", ::GetLastError());
        }
        const bool update_window = (flags & SWP_FRAMECHANGED) ||
            !(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE);
        if (update_window && !::SetWindowPos(hwnd_.Get(), nullptr, popup_pos_.x, popup_pos_.y,
                                            popup_width_, popup_height_, flags)) {
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
        UpdateScrollBar();
        ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
    }
}


} // namespace qiqi
