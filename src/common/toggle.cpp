// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/paintutil.h"
#include "common/uiutil.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <gsl/gsl>

namespace qiqi {
namespace {
constexpr size_t kToggleLabelCapacity = 128;
constexpr int kRadioOuterRadius = 8;
constexpr int kRadioCenterX = 10;
constexpr int kRadioInnerRadius = 4;
constexpr int kRadioTextInset = 26;
constexpr int kToggleRightInset = 6;
constexpr int kToggleTrackWidth = 36;
constexpr int kToggleTrackRadius = 9;
constexpr int kToggleKnobRadius = 6;
constexpr int kToggleLabelGap = 8;
constexpr int kToggleFocusPadding = 2;
} // namespace

RECT FluentToggleBounds(const RECT& row, const FluentD2DThemeContext& d2d) noexcept {
    const int center = row.top + (row.bottom - row.top) / 2;
    if (!d2d.d2d_target || !d2d.fmt_body || d2d.appearance.system_high_contrast) {
        const int size = d2d.ScalePx(18);
        return RECT{row.left, center - size / 2, row.left + size, center - size / 2 + size};
    }
    const int right = row.right - d2d.ScalePx(kToggleRightInset);
    const int radius = d2d.ScalePx(kToggleTrackRadius);
    return RECT{right - d2d.ScalePx(kToggleTrackWidth), center - radius, right, center + radius};
}

void DrawFluentToggleOrRadioRow(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_radio, const wchar_t* disabled_suffix) noexcept {
    if (!dis.hDC) {
        return;
    }
    const auto& palette = d2d.appearance.palette;
    const bool is_checked = (::SendMessageW(dis.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool is_enabled = (::IsWindowEnabled(dis.hwndItem) != FALSE);
    const COLORREF disabled_text = DisabledControlText(d2d);
    RECT focus_bounds = dis.rcItem;
    if (!is_radio) {
        focus_bounds = FluentToggleBounds(dis.rcItem, d2d);
        const int padding = d2d.ScalePx(kToggleFocusPadding);
        ::InflateRect(&focus_bounds, padding, padding);
        ::IntersectRect(&focus_bounds, &focus_bounds, &dis.rcItem);
    }
    const bool show_focus = (dis.itemState & (ODS_FOCUS | ODS_NOFOCUSRECT)) == ODS_FOCUS;
    if (!d2d.d2d_target || !d2d.fmt_body || d2d.appearance.system_high_contrast) {
        const ScopedSaveDC saved(dis.hDC);
        const ScopedSelectObject font(dis.hDC, reinterpret_cast<HFONT>(::SendMessageW(dis.hwndItem, WM_GETFONT, 0, 0)));
        const auto background = paint::CreateThemeBrush(palette.surface_bg);
        const auto border = paint::CreateThemeBrush(is_enabled ? palette.text_primary : disabled_text);
        if (background) ::FillRect(dis.hDC, &dis.rcItem, background.Get());
        RECT mark = dis.rcItem;
        mark.right = mark.left + d2d.ScalePx(18);
        mark.top += std::max(0L, (mark.bottom - mark.top - d2d.ScalePx(18)) / 2);
        mark.bottom = mark.top + d2d.ScalePx(18);
        if (!is_radio) mark = FluentToggleBounds(dis.rcItem, d2d);
        if (border && (is_enabled || !d2d.appearance.high_contrast)) ::FrameRect(dis.hDC, &mark, border.Get());
        ::SetTextColor(dis.hDC, is_enabled ? palette.text_primary : disabled_text);
        ::SetBkMode(dis.hDC, TRANSPARENT);
        if (is_checked) ::DrawTextW(dis.hDC, L"X", 1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        std::array<wchar_t, kToggleLabelCapacity> text{};
        ::GetWindowTextW(dis.hwndItem, text.data(), gsl::narrow_cast<int>(text.size()));
        if (!is_enabled && d2d.appearance.high_contrast) paint::AppendDisabledSuffix(text, disabled_suffix);
        RECT label = dis.rcItem;
        label.left += d2d.ScalePx(26);
        ::DrawTextW(dis.hDC, text.data(), -1, &label, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        if (show_focus) ::DrawFocusRect(dis.hDC, &focus_bounds);
        return;
    }

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem, nullptr, d2d.clip_controls_to_dc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(ColorRefToD2DColor(palette.surface_bg));

    const float h = static_cast<float>(dis.rcItem.bottom - dis.rcItem.top);
    const float w = static_cast<float>(dis.rcItem.right - dis.rcItem.left);
    const float cy = h * 0.5f;

    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(is_enabled ? palette.text_accent : disabled_text));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(is_enabled ? palette.border_color : disabled_text));
    const auto knob_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        ColorRefToD2DColor(!is_enabled ? disabled_text :
            (is_checked ? palette.selection_text : palette.text_secondary)));
    const auto text_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        ColorRefToD2DColor(is_enabled ? palette.text_primary : disabled_text));

    float text_left = d2d.ScaleF(kFluentToggleTextInset);
    float text_right = w - d2d.ScaleF(kFluentToggleTextInset);

    if (is_radio) {
        const float r_outer = d2d.ScaleF(kRadioOuterRadius);
        const D2D1_ELLIPSE outer = D2D1::Ellipse(D2D1::Point2F(d2d.ScaleF(kRadioCenterX), cy), r_outer, r_outer);
        if (is_checked && accent_brush) {
            d2d.d2d_target->FillEllipse(outer, accent_brush.Get());
            if (knob_brush) {
                d2d.d2d_target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(d2d.ScaleF(kRadioCenterX), cy),
                                  d2d.ScaleF(kRadioInnerRadius), d2d.ScaleF(kRadioInnerRadius)), knob_brush.Get());
            }
        } else if (border_brush && (is_enabled || !d2d.appearance.high_contrast)) {
            d2d.d2d_target->DrawEllipse(outer, border_brush.Get(), 1.5f);
        }
        text_left = d2d.ScaleF(kRadioTextInset);
    } else {
        const RECT bounds = FluentToggleBounds(
            RECT{0, 0, dis.rcItem.right - dis.rcItem.left, dis.rcItem.bottom - dis.rcItem.top}, d2d);
        const float sw_right = static_cast<float>(bounds.right);
        const float sw_left = static_cast<float>(bounds.left);
        const D2D1_ROUNDED_RECT track = D2D1::RoundedRect(
            D2D1::RectF(sw_left, cy - d2d.ScaleF(kToggleTrackRadius), sw_right, cy + d2d.ScaleF(kToggleTrackRadius)),
            d2d.ScaleF(kToggleTrackRadius), d2d.ScaleF(kToggleTrackRadius));
        if (is_checked && accent_brush) {
            d2d.d2d_target->FillRoundedRectangle(track, accent_brush.Get());
        } else if (border_brush && (is_enabled || !d2d.appearance.high_contrast)) {
            d2d.d2d_target->DrawRoundedRectangle(track, border_brush.Get(), 1.4f);
        }
        const float knob_x = is_checked ? (sw_right - d2d.ScaleF(kToggleTrackRadius)) :
                                         (sw_left + d2d.ScaleF(kToggleTrackRadius));
        if (knob_brush) {
            d2d.d2d_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(knob_x, cy),
                              d2d.ScaleF(kToggleKnobRadius), d2d.ScaleF(kToggleKnobRadius)), knob_brush.Get());
        }
        text_right = sw_left - d2d.ScaleF(kToggleLabelGap);
    }

    std::array<wchar_t, kToggleLabelCapacity> label_text{};
    ::GetWindowTextW(dis.hwndItem, label_text.data(), gsl::narrow_cast<int>(label_text.size()));
    if (!is_enabled && d2d.appearance.high_contrast) paint::AppendDisabledSuffix(label_text, disabled_suffix);
    if (d2d.fmt_body && text_brush && gsl::at(label_text, 0) != L'\0') {
        const D2D1_RECT_F txt_rc = D2D1::RectF(text_left, 0.0f, text_right, h);
        d2d.d2d_target->DrawTextW(
            label_text.data(),
            gsl::narrow_cast<UINT32>(wcslen(label_text.data())),
            d2d.fmt_body.Get(),
            txt_rc,
            text_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (show_focus) {
        const auto focus = CreateSolidBrush(d2d.d2d_target.Get(), ColorRefToD2DColor(palette.focus_color));
        if (focus) {
            if (is_radio) d2d.d2d_target->DrawRectangle(D2D1::RectF(1, 1, w - 1, h - 1), focus.Get(), 2);
            else d2d.d2d_target->DrawRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(static_cast<float>(focus_bounds.left - dis.rcItem.left),
                    static_cast<float>(focus_bounds.top - dis.rcItem.top),
                    static_cast<float>(focus_bounds.right - dis.rcItem.left),
                    static_cast<float>(focus_bounds.bottom - dis.rcItem.top)),
                d2d.ScaleF(kToggleTrackRadius), d2d.ScaleF(kToggleTrackRadius)), focus.Get(), 2);
        }
    }
}

} // namespace qiqi
