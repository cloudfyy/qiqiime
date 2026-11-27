// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/paintutil.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <gsl/gsl>

namespace qiqi {
namespace {
constexpr size_t kButtonTextCapacity = 64;
constexpr size_t kFallbackButtonTextCapacity = 256;
constexpr int kCardCornerRadius = 8;
constexpr RECT kCardAccentOffsets{12, 10, 16, 26};
constexpr int kCardAccentRadius = 2;
constexpr int kCardHeaderLeftInset = 24;
constexpr int kCardHeaderTopOffset = 9;
constexpr int kCardHeaderRightInset = 12;
constexpr int kButtonCornerRadius = 6;
constexpr int kDropdownButtonGutter = 24;

std::array<POINT, 3> DropdownChevron(
    const RECT& bounds, int gutter, const FluentD2DThemeContext& d2d) noexcept {
    const LONG x = bounds.right - gutter / 2 - d2d.ScaleDipPx(2);
    const LONG y = bounds.top + (bounds.bottom - bounds.top) / 2;
    const int arm = std::min(gutter / 3, d2d.ScaleDipPx(4));
    return {{{x - arm, y - arm / 2}, {x, y + arm / 2}, {x + arm, y - arm / 2}}};
}

void PaintGdiDropdownChevron(
    HDC dc, const RECT& bounds, int gutter, COLORREF color, const FluentD2DThemeContext& d2d) noexcept {
    const ScopedGdiHandle<HPEN> pen(::CreatePen(PS_SOLID, std::max(1, d2d.ScaleDipPx(1)), color));
    if (!pen) {
        QIQI_LOG_WARN("Fluent dropdown arrow pen creation failed");
        return;
    }
    const ScopedSelectObject selected(dc, pen.Get());
    if (!selected) {
        QIQI_LOG_WARN("Fluent dropdown arrow pen selection failed");
        return;
    }
    const auto points = DropdownChevron(bounds, gutter, d2d);
    if (!::Polyline(dc, points.data(), gsl::narrow_cast<int>(points.size()))) {
        QIQI_LOG_WARN("Fluent dropdown arrow painting failed");
    }
}
} // namespace

void DrawFluentCardGroup(
    const FluentD2DThemeContext& d2d,
    std::span<const FluentCardSpec> cards,
    bool is_en,
    bool /*is_dark*/) noexcept {
    if (!d2d.d2d_target) {
        return;
    }
    const auto& palette = d2d.appearance.palette;
    const auto card_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.surface_bg));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.border_color));
    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.text_accent));
    const float radius = d2d.ScaleF(kCardCornerRadius);

    for (const auto& card : cards) {
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(card.rect, radius, radius);
        if (card_brush) {
            d2d.d2d_target->FillRoundedRectangle(rr, card_brush.Get());
        }
        if (border_brush) {
            d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
        }
        if (accent_brush) {
            const D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
                D2D1::RectF(card.rect.left + d2d.ScaleF(kCardAccentOffsets.left),
                            card.rect.top + d2d.ScaleF(kCardAccentOffsets.top),
                            card.rect.left + d2d.ScaleF(kCardAccentOffsets.right),
                            card.rect.top + d2d.ScaleF(kCardAccentOffsets.bottom)),
                d2d.ScaleF(kCardAccentRadius), d2d.ScaleF(kCardAccentRadius));
            d2d.d2d_target->FillRoundedRectangle(pill, accent_brush.Get());
        }
        if (d2d.fmt_caption && accent_brush) {
            const wchar_t* hdr = is_en ? card.title_en : card.title_zh;
            if (hdr) {
                const D2D1_RECT_F hdr_rc = D2D1::RectF(
                    card.rect.left + static_cast<float>(d2d.ScaleDipPx(kCardHeaderLeftInset)),
                    card.rect.top + static_cast<float>(d2d.ScaleDipPx(kCardHeaderTopOffset)),
                    card.rect.right - static_cast<float>(d2d.ScaleDipPx(kCardHeaderRightInset)),
                    card.rect.bottom - static_cast<float>(d2d.ScaleDipPx(kCardHeaderRightInset)));
                d2d.d2d_target->DrawTextW(
                    hdr, gsl::narrow_cast<UINT32>(wcslen(hdr)), d2d.fmt_caption.Get(), hdr_rc, accent_brush.Get());
            }
        }
    }
}

void DrawFluentPushButton(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_primary,
    D2D1_COLOR_F outer_bg, const wchar_t* disabled_suffix, bool show_dropdown) noexcept {
    if (!dis.hDC) {
        return;
    }
    const auto& palette = d2d.appearance.palette;
    const bool is_enabled = ::IsWindowEnabled(dis.hwndItem) != FALSE;
    const COLORREF disabled_text = DisabledControlText(d2d);
    const int dropdown_gutter = show_dropdown
        ? std::min(d2d.ScaleDipPx(kDropdownButtonGutter),
            std::max(0, gsl::narrow_cast<int>(dis.rcItem.right - dis.rcItem.left) / 3))
        : 0;
    if (d2d.appearance.system_high_contrast || !d2d.d2d_target || !d2d.ButtonTextFormat(is_primary)) {
        const ScopedSaveDC saved(dis.hDC);
        if (!saved) {
            QIQI_LOG_WARN("Fluent button DC save failed");
            return;
        }
        const HFONT font = is_primary && d2d.primary_button_font ? d2d.primary_button_font.Get()
            : reinterpret_cast<HFONT>(::SendMessageW(dis.hwndItem, WM_GETFONT, 0, 0));
        const ScopedSelectObject selected_font(dis.hDC, font);
        RECT rect = dis.rcItem;
        const auto background = paint::CreateThemeBrush(is_primary && is_enabled ? palette.selection_bg : palette.surface_bg);
        const auto border = paint::CreateThemeBrush(palette.border_color);
        if (background) ::FillRect(dis.hDC, &rect, background.Get());
        if (border && (is_enabled || !d2d.appearance.high_contrast)) ::FrameRect(dis.hDC, &rect, border.Get());
        std::array<wchar_t, kFallbackButtonTextCapacity> text{};
        ::GetWindowTextW(dis.hwndItem, text.data(), gsl::narrow_cast<int>(text.size()));
        if (!is_enabled && d2d.appearance.high_contrast) paint::AppendDisabledSuffix(text, disabled_suffix);
        const COLORREF text_color = !is_enabled ? disabled_text :
            (is_primary ? palette.selection_text : palette.text_primary);
        ::SetTextColor(dis.hDC, text_color);
        ::SetBkMode(dis.hDC, TRANSPARENT);
        ::InflateRect(&rect, -4, -4);
        RECT text_rect = rect;
        text_rect.right -= dropdown_gutter;
        ::DrawTextW(dis.hDC, text.data(), -1, &text_rect, DT_CENTER | DT_WORDBREAK);
        if (dropdown_gutter > 0) {
            PaintGdiDropdownChevron(dis.hDC, dis.rcItem, dropdown_gutter, text_color, d2d);
        }
        if (is_enabled && (dis.itemState & ODS_FOCUS)) {
            ::InflateRect(&rect, -3, -3);
            ::DrawFocusRect(dis.hDC, &rect);
        }
        return;
    }
    const bool is_dark = palette.is_dark;
    const bool is_pressed = is_enabled && (dis.itemState & ODS_SELECTED) != 0;

    D2D1_COLOR_F fill_col{};
    D2D1_COLOR_F border_col{};
    D2D1_COLOR_F text_col{};
    if (is_primary) {
        fill_col = is_pressed ? MakeD2DColor(0, 84, 166) : MakeD2DColor(0, 103, 192);
        border_col = MakeD2DColor(40, 130, 220);
        text_col = MakeD2DColor(255, 255, 255);
    } else if (is_dark) {
        fill_col = is_pressed ? MakeD2DColor(48, 52, 60) : MakeD2DColor(56, 60, 70);
        border_col = MakeD2DColor(82, 88, 100);
        text_col = ColorRefToD2DColor(kDarkTextColor);
    } else {
        fill_col = is_pressed ? MakeD2DColor(232, 236, 242) : MakeD2DColor(255, 255, 255);
        border_col = MakeD2DColor(204, 210, 220);
        text_col = ColorRefToD2DColor(kLightTextColor);
    }
    if (d2d.appearance.high_contrast) {
        fill_col = ColorRefToD2DColor(is_primary ? palette.selection_bg : palette.surface_bg);
        border_col = ColorRefToD2DColor(palette.border_color);
        text_col = ColorRefToD2DColor(is_primary ? palette.selection_text : palette.text_primary);
    }
    if (!is_enabled) {
        fill_col = ColorRefToD2DColor(palette.surface_bg);
        border_col = ColorRefToD2DColor(palette.border_color);
        text_col = ColorRefToD2DColor(disabled_text);
    }

    const D2D1_RECT_F rc = D2D1::RectF(
        1.0f, 1.0f,
        static_cast<float>(dis.rcItem.right - dis.rcItem.left) - 1.0f,
        static_cast<float>(dis.rcItem.bottom - dis.rcItem.top) - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, d2d.ScaleF(kButtonCornerRadius), d2d.ScaleF(kButtonCornerRadius));

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem, nullptr, d2d.clip_controls_to_dc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(outer_bg);

    const auto fill_brush = CreateSolidBrush(d2d.d2d_target.Get(), fill_col);
    const auto border_brush = CreateSolidBrush(d2d.d2d_target.Get(), border_col);
    const auto txt_brush = CreateSolidBrush(d2d.d2d_target.Get(), text_col);
    if (fill_brush) {
        d2d.d2d_target->FillRoundedRectangle(rr, fill_brush.Get());
    }
    if (border_brush && (is_enabled || !d2d.appearance.high_contrast)) {
        d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
    }

    std::array<wchar_t, kButtonTextCapacity> btn_text{};
    ::GetWindowTextW(dis.hwndItem, btn_text.data(), gsl::narrow_cast<int>(btn_text.size()));
    if (!is_enabled && d2d.appearance.high_contrast) paint::AppendDisabledSuffix(btn_text, disabled_suffix);
    if (auto* const text_format = d2d.ButtonTextFormat(is_primary);
            text_format && txt_brush && gsl::at(btn_text, 0) != L'\0') {
        auto text_rect = rc;
        text_rect.right -= static_cast<float>(dropdown_gutter);
        d2d.d2d_target->DrawTextW(
            btn_text.data(),
            gsl::narrow_cast<UINT32>(wcslen(btn_text.data())),
            text_format,
            text_rect,
            txt_brush.Get(),
            show_dropdown ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    if (dropdown_gutter > 0 && txt_brush) {
        const auto [first, middle, last] = DropdownChevron(
            RECT{0, 0, dis.rcItem.right - dis.rcItem.left, dis.rcItem.bottom - dis.rcItem.top},
            dropdown_gutter, d2d);
        const auto point = [](POINT value) noexcept {
            return D2D1::Point2F(static_cast<float>(value.x), static_cast<float>(value.y));
        };
        const float stroke = static_cast<float>(std::max(1, d2d.ScaleDipPx(1))) + 0.4f;
        d2d.d2d_target->DrawLine(point(first), point(middle), txt_brush.Get(), stroke);
        d2d.d2d_target->DrawLine(point(middle), point(last), txt_brush.Get(), stroke);
    }
    if (is_enabled && (dis.itemState & ODS_FOCUS) != 0 && border_brush) {
        const auto focus = CreateSolidBrush(d2d.d2d_target.Get(), ColorRefToD2DColor(palette.focus_color));
        if (focus) d2d.d2d_target->DrawRectangle(D2D1::RectF(4, 4, rc.right - 3, rc.bottom - 3), focus.Get(), 2);
    }
}

} // namespace qiqi
