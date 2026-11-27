// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/logger.h"
#include "ime/fsm/fsm.h"
#include <gsl/gsl>
#include <algorithm>

namespace qiqi {
void CandidateUI::InitDirect2DAndDirectWrite() noexcept {
    if (!CreateD2DDcTargetAndDWrite(d2d_factory_, dwrite_factory_, d2d_dc_target_)) {
        QIQI_LOG_WARN("CandidateUI::InitDirect2DAndDirectWrite incomplete (d2d={}, dwrite={}, target={}); GDI fallback active",
                      d2d_factory_.IsValid(), dwrite_factory_.IsValid(), d2d_dc_target_.IsValid());
    }
}

CandidatePaintMetrics CandidateUI::PaintMetrics() const noexcept {
    return {font_.Get(), line_height_, ScaleByDpi(preferences_.easy_reading ? 8 : 2),
        ScaleByDpi(preferences_.easy_reading ? 8 : 1), ScaleByDpi(2)};
}

std::vector<CandidateUI::PaintItem> CandidateUI::BuildPaintItems() const {
    std::vector<PaintItem> items;
    items.push_back({ViewportRect(header_rect_), header_label_, false, true, true, false, SkinColor::CodeText});
    for (size_t i = 0; i < candidate_labels_.size(); ++i) {
        items.push_back({ViewportRect(gsl::at(candidate_hit_rects_, i)), gsl::at(candidate_labels_, i),
                         i == selected_in_page_});
    }
    if (candidate_labels_.empty()) {
        items.push_back({ViewportRect(empty_rect_), empty_label_, false, false, true, false, SkinColor::EmptyText});
    }
    if (const size_t page_count = ComputeTotalPages(all_candidates_.size(), page_size_); page_count > 1) {
        items.push_back({PrevPageButtonRect(), prev_page_label_,
                         false, true, current_page_ > 0, true, SkinColor::PageEnabledText});
        items.push_back({NextPageButtonRect(), next_page_label_,
                         false, true, current_page_ + 1 < page_count, true,
                         SkinColor::PageEnabledText});
    }
    return items;
}

bool CandidateUI::PaintPopupDirect2D(HDC hdc, const RECT& client_rect, std::span<const PaintItem> items,
    const CandidatePaintMetrics& metrics) const noexcept {
    if (!hdc || !d2d_dc_target_ || !dwrite_text_format_) return false;
    const ScopedSelectObject select_font(hdc, metrics.font);
    const auto colors = ResolveCandidatePaintFrameColors(skin_colors_);
    const auto brush = CreateSolidBrush(d2d_dc_target_.Get(),
        ColorRefToD2DColor(skin_colors_.Get(SkinColor::CandidateText)));
    if (!brush) return false;
    const auto as_d2d = [](const RECT& rect) noexcept {
        return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                           static_cast<float>(rect.right), static_cast<float>(rect.bottom));
    };
    HRESULT end_hr = S_OK;
    {
        const ScopedD2DDcDraw draw_guard(d2d_dc_target_.Get(), hdc, client_rect, &end_hr);
        if (!draw_guard) return false;
        d2d_dc_target_->Clear(ColorRefToD2DColor(colors.background));
        for (const PaintItem& item : items) {
            if (!IsCandidatePaintRowVisible(item, client_rect)) continue;
            if (item.selected) {
                brush->SetColor(ColorRefToD2DColor(colors.selection_background));
                d2d_dc_target_->FillRectangle(as_d2d(item.rect), brush.Get());
                brush->SetColor(ColorRefToD2DColor(colors.selection_border));
                d2d_dc_target_->DrawRectangle(as_d2d(item.rect), brush.Get(),
                    static_cast<float>(metrics.selection_border_width));
            }
            const COLORREF color = CandidatePaintTextColor(skin_colors_, item);
            brush->SetColor(ColorRefToD2DColor(color));
            ForEachCandidatePaintLine(item, metrics, [&](std::wstring_view line, RECT line_rect) noexcept {
                if (item.centered) {
                    line_rect.left += std::max(0, (gsl::narrow_cast<int>(line_rect.right - line_rect.left) -
                        MeasureTextWidth(hdc, line)) / 2);
                }
                d2d_dc_target_->DrawTextW(line.data(), gsl::narrow_cast<UINT32>(line.size()),
                    dwrite_text_format_.Get(), as_d2d(line_rect), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            });
        }
        brush->SetColor(ColorRefToD2DColor(colors.border));
        d2d_dc_target_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, static_cast<float>(client_rect.right) - 0.5f,
            static_cast<float>(client_rect.bottom) - 0.5f), brush.Get());
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
    const int width = client_rect.right - client_rect.left;
    const int height = client_rect.bottom - client_rect.top;
    if (width <= 0 || height <= 0) return;
    const ScopedMemoryDC buffer(::CreateCompatibleDC(hdc));
    if (!buffer) {
        QIQI_LOG_WARN("CandidateUI::CreateCompatibleDC failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(hdc, width, height));
    if (!bitmap) {
        QIQI_LOG_WARN("CandidateUI::CreateCompatibleBitmap failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    const ScopedSelectObject select_bitmap(buffer.Get(), bitmap.Get());
    if (!select_bitmap) {
        QIQI_LOG_WARN("CandidateUI::SelectObject(buffer) failed (error={}); painting without buffer", select_bitmap.Error());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    // Both renderers finish the complete frame before touching the visible DC.
    PaintPopupContent(buffer.Get(), client_rect);
    if (!::BitBlt(hdc, client_rect.left, client_rect.top, width, height,
                  buffer.Get(), client_rect.left, client_rect.top, SRCCOPY)) {
        QIQI_LOG_WARN("CandidateUI::BitBlt failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
    }
}

void CandidateUI::PaintPopupContent(HDC hdc, const RECT& client_rect) const {
    const auto items = BuildPaintItems();
    const auto metrics = PaintMetrics();
    if (PaintPopupDirect2D(hdc, client_rect, items, metrics)) return;
    std::ignore = PaintCandidateItemsGdi(hdc, client_rect, items, metrics, skin_colors_);
}


} // namespace qiqi
