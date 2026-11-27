// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"
#include "common/ctrlutil.h"
#include "common/dpi.h"
#include "common/skin.h"

#include <gsl/util>
#include <span>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <cstddef>
#include <string_view>

namespace qiqi {

struct CandidatePaintItem {
    RECT rect{};
    std::wstring text;
    bool selected = false;
    bool accent = false;
    bool enabled = true;
    bool centered = false;
    SkinColor color_role = SkinColor::CandidateText;
};

struct CandidatePaintMetrics {
    HFONT font = nullptr;
    int line_height = 0;
    int horizontal_padding = 0;
    int vertical_padding = 0;
    int selection_border_width = 1;
    bool word_wrap = false;
};

struct CandidatePaintFrameColors {
    COLORREF background;
    COLORREF border;
    COLORREF selection_background;
    COLORREF selection_border;
};

inline CandidatePaintFrameColors ResolveCandidatePaintFrameColors(const SkinPalette& palette) noexcept {
    return {palette.Get(SkinColor::Background), palette.Get(SkinColor::Border),
        palette.Get(SkinColor::SelectedBackground), palette.Get(SkinColor::SelectedBorder)};
}

inline bool IsCandidatePaintRowVisible(const CandidatePaintItem& item, const RECT& viewport) noexcept {
    return item.rect.bottom > viewport.top && item.rect.top < viewport.bottom;
}

inline RECT CandidatePaintTextBounds(const CandidatePaintItem& item, const CandidatePaintMetrics& metrics) noexcept {
    if (item.accent) return item.rect;
    return {item.rect.left + metrics.horizontal_padding, item.rect.top + metrics.vertical_padding,
        item.rect.right - metrics.horizontal_padding, item.rect.bottom - metrics.vertical_padding};
}

template <typename DrawLine>
void ForEachCandidatePaintLine(const CandidatePaintItem& item, const CandidatePaintMetrics& metrics,
    DrawLine draw_line) noexcept {
    RECT rect = CandidatePaintTextBounds(item, metrics);
    if (item.centered) {
        const int lines = gsl::narrow_cast<int>(std::count(item.text.begin(), item.text.end(), L'\n')) + 1;
        rect.top += std::max(0, (gsl::narrow_cast<int>(item.rect.bottom - item.rect.top) - lines * metrics.line_height) / 2);
    }
    rect.bottom = rect.top + metrics.line_height;
    std::wstring_view remaining = item.text;
    do {
        const size_t end = remaining.find(L'\n');
        draw_line(remaining.substr(0, end), rect);
        if (end == std::wstring_view::npos) break;
        remaining.remove_prefix(end + 1);
        rect.top += metrics.line_height;
        rect.bottom += metrics.line_height;
    } while (!remaining.empty());
}

struct CandidatePageLabels {
    std::wstring_view previous;
    std::wstring_view next;
};

enum class CandidateSampleLayout : uint8_t { Normal, Compact };

CandidatePageLabels ResolveCandidatePageLabels(
    UiLanguage language, const DisplayPreferences& display) noexcept;

inline COLORREF CandidatePaintTextColor(const SkinPalette& palette, const CandidatePaintItem& item) noexcept {
    return palette.Get(item.selected ? SkinColor::SelectedText :
        !item.enabled ? SkinColor::PageDisabledText : item.color_role);
}

bool PaintCandidateItemsGdi(HDC dc, const RECT& bounds, std::span<const CandidatePaintItem> items,
    const CandidatePaintMetrics& metrics, const SkinPalette& palette) noexcept;
int MeasureCandidateSample(HDC dc, HFONT font, int width, UINT dpi, UiLanguage language,
    const DisplayPreferences& display,
    CandidateSampleLayout layout = CandidateSampleLayout::Normal) noexcept;
bool PaintCandidateSample(HDC dc, const RECT& bounds, HFONT font, UINT dpi,
    UiLanguage language, const DisplayPreferences& display, const SkinPalette& palette,
    CandidateSampleLayout layout = CandidateSampleLayout::Normal) noexcept;

inline constexpr int kFluentBodyFontSize = 14;
inline constexpr int kFluentComboItemHeight = 22;
inline constexpr int kCandidateFontDefaultHeightPx = 18;
inline constexpr int kFluentToggleTextInset = 4;
inline constexpr std::size_t kFluentComboTextCapacity = 128;
inline constexpr int kComfortMinimumTargetDip = 44;
inline constexpr int kComfortMinimumTextPercent = 150;

constexpr DisplayPreferences NormalizeDisplayPreferences(DisplayPreferences preferences) noexcept {
    if (preferences.easy_reading && TextScalePercent(preferences.text_scale) < kComfortMinimumTextPercent) {
        preferences.text_scale = UiTextScale::Percent150;
    }
    return preferences;
}

bool IsHighContrastEnabled() noexcept;
void SetHighContrastOverrideForTest(std::optional<bool> enabled) noexcept;

struct FluentCardSpec {
    D2D1_RECT_F rect{};
    const wchar_t* title_zh = nullptr;
    const wchar_t* title_en = nullptr;
};

struct FluentD2DThemeContext {
    UINT dpi = 96;
    DisplayPreferences display_preferences{};
    DisplayAppearance appearance{};
    bool clip_controls_to_dc = false;
    ScopedD2DFactory d2d_factory;
    ScopedDWriteFactory dwrite_factory;
    ScopedD2DDcTarget d2d_target;
    ScopedDWriteTextFormat fmt_body;
    ScopedDWriteTextFormat fmt_caption;
    ScopedDWriteTextFormat fmt_btn;
    ScopedGdiHandle<HBRUSH> surface_brush;
    ScopedGdiHandle<HBRUSH> input_brush;

    int ScalePx(int px) const noexcept;
    float ScaleF(int px) const noexcept;
    int ScaleDipPx(int px) const noexcept { return ScalePxByDpi(px, dpi); }
    void SetDisplayPreferences(DisplayPreferences preferences) noexcept;
    void Init(UINT monitor_dpi) noexcept;
    void RefreshThemeBrushes() noexcept;
};

inline int FluentControlHeight(const FluentD2DThemeContext& theme, int normal_height) noexcept {
    return std::max(theme.ScalePx(normal_height), theme.display_preferences.easy_reading ?
        theme.ScaleDipPx(kComfortMinimumTargetDip) : 0);
}
COLORREF DisabledControlText(const FluentD2DThemeContext& theme) noexcept;

// Shared WM_CTLCOLOR handling; input=false is for labels/buttons.
HBRUSH ApplyFluentControlColors(HDC dc, const FluentD2DThemeContext& theme, bool input) noexcept;

// Draws into an already active ScopedD2DDcDraw session.
void DrawFluentCardGroup(
    const FluentD2DThemeContext& d2d,
    std::span<const FluentCardSpec> cards,
    bool is_en,
    bool is_dark) noexcept;

void DrawFluentPushButton(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_primary,
    D2D1_COLOR_F outer_bg, const wchar_t* disabled_suffix = nullptr) noexcept;

void DrawFluentToggleOrRadioRow(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_radio, const wchar_t* disabled_suffix = nullptr) noexcept;

void PaintFluentComboBoxFrame(HWND hwnd, HDC hdc, const FluentD2DThemeContext& d2d) noexcept;
void DrawFluentComboBoxItem(const DRAWITEMSTRUCT& dis, const FluentD2DThemeContext& d2d,
                           bool disabled = false, const wchar_t* disabled_suffix = nullptr) noexcept;
RECT FluentToggleBounds(const RECT& row, const FluentD2DThemeContext& d2d) noexcept;
void DrawFluentUnderlineStylePreview(
    const FluentD2DThemeContext& d2d,
    CompositionUnderlineStyle style,
    bool bold_line,
    const D2D1_RECT_F& sample_rect,
    const D2D1_COLOR_F& color) noexcept;
void EnableFluentUnderlineComboPreview(HWND combo_hwnd, int bold_checkbox_id) noexcept;

// The parent owns the returned child. A combo's context must outlive the child
// and stay at the same address. Radio groups use MAKELONG(first_id, last_id).
HWND CreateFluentChildControl(
    HWND parent,
    HINSTANCE inst,
    HFONT font,
    const FluentD2DThemeContext* d2d_ctx,
    const wchar_t* cls,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    int id,
    DWORD_PTR radio_group_range = 0) noexcept;

} // namespace qiqi
