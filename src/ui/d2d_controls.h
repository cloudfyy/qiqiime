#pragma once

#include "common/d2d_util.h"
#include "common/monitor_dpi.h"

#include <span>

namespace qiqi {

struct FluentCardSpec {
    D2D1_RECT_F rect{};
    const wchar_t* title_zh = nullptr;
    const wchar_t* title_en = nullptr;
};

struct FluentD2DThemeContext {
    UINT dpi = 96;
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
    void Init(UINT monitor_dpi) noexcept;
    void RefreshThemeBrushes() noexcept;
};

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
    D2D1_COLOR_F outer_bg) noexcept;

void DrawFluentToggleOrRadioRow(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_radio) noexcept;

void PaintFluentComboBoxFrame(HWND hwnd, HDC hdc, const FluentD2DThemeContext& d2d) noexcept;
void DrawFluentComboBoxItem(const DRAWITEMSTRUCT& dis, const FluentD2DThemeContext& d2d) noexcept;

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
