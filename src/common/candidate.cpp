// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/i18n.h"
#include "common/logger.h"
#include "common/uiutil.h"

#include <algorithm>
#include <array>
#include <gsl/gsl>
#include <new>
#include <vector>

namespace qiqi {
bool PaintCandidateItemsGdi(HDC dc, const RECT& bounds, std::span<const CandidatePaintItem> items,
    const CandidatePaintMetrics& metrics, const SkinPalette& palette) noexcept {
    const ScopedSaveDC saved(dc);
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(palette.Get(SkinColor::Background)));
    const ScopedGdiHandle<HBRUSH> selection(::CreateSolidBrush(palette.Get(SkinColor::SelectedBackground)));
    const ScopedGdiHandle<HBRUSH> outline(::CreateSolidBrush(palette.Get(SkinColor::SelectedBorder)));
    const ScopedGdiHandle<HBRUSH> border(::CreateSolidBrush(palette.Get(SkinColor::Border)));
    if (!saved || !background || !selection || !outline || !border) {
        QIQI_LOG_WARN("Candidate GDI drawing resource creation failed (error={})", ::GetLastError());
        return false;
    }
    ::FillRect(dc, &bounds, background.Get());
    const ScopedSelectObject selected_font(dc, metrics.font);
    ::SetBkMode(dc, TRANSPARENT);
    for (const auto& item : items) {
        if (item.rect.bottom <= bounds.top || item.rect.top >= bounds.bottom) continue;
        if (item.selected) {
            ::FillRect(dc, &item.rect, selection.Get());
            RECT frame = item.rect;
            for (int i = 0; i < metrics.selection_border_width; ++i) {
                ::FrameRect(dc, &frame, outline.Get());
                ::InflateRect(&frame, -1, -1);
            }
        }
        ::SetTextColor(dc, CandidatePaintTextColor(palette, item));
        const int padding = item.accent ? 0 : metrics.horizontal_padding;
        const int vertical_padding = item.accent ? 0 : metrics.vertical_padding;
        if (metrics.word_wrap) {
            RECT text = item.rect;
            ::InflateRect(&text, -padding, -vertical_padding);
            ::DrawTextW(dc, item.text.data(), gsl::narrow_cast<int>(item.text.size()), &text,
                DT_WORDBREAK | DT_NOPREFIX | (item.centered ? DT_CENTER : DT_LEFT));
            continue;
        }
        int top = item.rect.top + vertical_padding;
        if (item.centered) {
            const int lines = gsl::narrow_cast<int>(std::count(item.text.begin(), item.text.end(), L'\n')) + 1;
            top += std::max(0, (gsl::narrow_cast<int>(item.rect.bottom - item.rect.top) - lines * metrics.line_height) / 2);
        }
        std::wstring_view remaining = item.text;
        do {
            const size_t end = remaining.find(L'\n');
            const auto line = remaining.substr(0, end);
            RECT text{item.rect.left + padding, top, item.rect.right - padding, top + metrics.line_height};
            ::DrawTextW(dc, line.data(), gsl::narrow_cast<int>(line.size()), &text,
                DT_SINGLELINE | DT_NOPREFIX | (item.centered ? DT_CENTER : DT_LEFT));
            if (end == std::wstring_view::npos) break;
            remaining.remove_prefix(end + 1);
            top += metrics.line_height;
        } while (!remaining.empty());
    }
    ::FrameRect(dc, &bounds, border.Get());
    return true;
}

CandidatePageLabels ResolveCandidatePageLabels(
    UiLanguage language, const DisplayPreferences& display) noexcept {
    const auto& strings = GetUiStrings(language);
    return display.easy_reading
        ? CandidatePageLabels{strings.candidate_previous_page, strings.candidate_next_page}
        : CandidatePageLabels{L"‹", L"›"};
}

namespace {

template <typename PaintItem>
int LayoutCandidateSample(HDC dc, HFONT font, int width, UINT dpi, UiLanguage language,
    const DisplayPreferences& display, CandidateSampleLayout layout, PaintItem paint_item) {
    const ScopedSelectObject selected_font(dc, font);
    const int inset = ScalePxByDpi(12, dpi);
    const bool compact = layout == CandidateSampleLayout::Compact;
    const int padding = ScalePxByDpi(compact ? 3 : 4, dpi);
    const int gap = ScalePxByDpi(compact ? 4 : 8, dpi);
    const int available = std::max(1, width - 2 * inset);
    const int right = inset + available;
    int x = inset;
    int y = gap;
    int bottom = y;
    const auto page = ResolveCandidatePageLabels(language, display);
    const std::array<std::wstring_view, 6> lines{
        L"Wubi [ab] (1/2)", L"1.五笔", L"2.输入", L"3.你好", page.previous, page.next};
    for (size_t index = 0; index < lines.size(); ++index) {
        const auto line = gsl::at(lines, index);
        RECT measured{0, 0, std::max(1, available - 2 * padding), 0};
        if (!::DrawTextW(dc, line.data(), gsl::narrow_cast<int>(line.size()), &measured,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX)) {
            QIQI_LOG_WARN("Candidate sample measurement failed");
            return 0;
        }
        const int item_width = std::min(available, gsl::narrow_cast<int>(measured.right) + 2 * padding);
        if (index == 1 || (!compact && index == 4) ||
            (x > inset && x + item_width > right)) {
            y = bottom + padding;
            x = inset;
        }
        if (index == 5) x = std::max(x, right - item_width);
        const RECT row{x, y, x + item_width, y + measured.bottom + 2 * padding};
        paint_item(line, row, index);
        bottom = std::max(bottom, gsl::narrow_cast<int>(row.bottom));
        x = row.right + gap;
    }
    return bottom + gap;
}

} // namespace

int MeasureCandidateSample(HDC dc, HFONT font, int width, UINT dpi, UiLanguage language,
    const DisplayPreferences& display, CandidateSampleLayout layout) noexcept {
    return LayoutCandidateSample(dc, font, width, dpi, language, display, layout,
        [](std::wstring_view, const RECT&, size_t) noexcept {});
}

bool PaintCandidateSample(HDC dc, const RECT& bounds, HFONT font, UINT dpi,
    UiLanguage language, const DisplayPreferences& display, const SkinPalette& palette,
    CandidateSampleLayout layout) noexcept {
    try {
        std::vector<CandidatePaintItem> items;
        LayoutCandidateSample(dc, font, bounds.right - bounds.left, dpi, language, display, layout,
            [&](std::wstring_view text, RECT row, size_t index) {
                ::OffsetRect(&row, bounds.left, bounds.top);
                const bool accent = index == 0 || index >= 4;
                items.push_back({row, std::wstring(text), index == 1, accent, index != 4, accent,
                    index == 0 ? SkinColor::CodeText : index >= 4 ?
                        SkinColor::PageEnabledText : SkinColor::CandidateText});
            });
        const int padding = ScalePxByDpi(4, dpi);
        return PaintCandidateItemsGdi(dc, bounds, items,
            {font, 0, padding, padding, ScalePxByDpi(2, dpi), true}, palette);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Candidate sample allocation failed (hr=0x{:08X})", static_cast<unsigned long>(E_OUTOFMEMORY));
        return false;
    }
}

} // namespace qiqi
