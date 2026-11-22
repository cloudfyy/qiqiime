// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/logger.h"
#include "common/paintutil.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <gsl/gsl>
#include <limits>
#include <tuple>

namespace qiqi {
namespace {
constexpr int kUnderlineBaselineOffset = 1;
constexpr int kUnderlineDotStep = 3;
constexpr int kComboCornerRadius = 5;
constexpr int kComboTextInset = 10;
constexpr int kComboPreviewTextRightInset = 90;
constexpr int kComboTextRightInset = 26;
constexpr int kComboPreviewLeftInset = 84;
constexpr int kComboPreviewRightInset = 28;
constexpr int kComboChevronRightInset = 14;
constexpr int kComboChevronVerticalOffset = 1;
constexpr int kComboChevronArm = 4;
constexpr int kComboFallbackTextInset = 4;
constexpr int kComboItemPreviewTextRightInset = 74;
constexpr int kComboItemPreviewLeftInset = 68;
constexpr int kComboItemPreviewRightInset = 10;
constexpr float kComboItemTextLeftInsetPx = 6.0f;
constexpr float kComboItemTextRightInsetPx = 4.0f;
constexpr const wchar_t* kUnderlineComboBoldIdProp = L"QiQiUnderlineComboBoldId";

bool QueryUnderlineComboBoldState(HWND combo_hwnd, CompositionUnderlineStyle& out_style, UINT item_index, bool& out_bold) noexcept {
    const HANDLE prop = ::GetPropW(combo_hwnd, kUnderlineComboBoldIdProp);
    if (!prop) {
        return false;
    }
    if (item_index > static_cast<UINT>(CompositionUnderlineStyle::None)) {
        return false;
    }
    out_style = static_cast<CompositionUnderlineStyle>(item_index);
    const int bold_id = gsl::narrow_cast<int>(reinterpret_cast<INT_PTR>(prop));
    const HWND parent = ::GetParent(combo_hwnd);
    out_bold = parent && (::IsDlgButtonChecked(parent, bold_id) == BST_CHECKED);
    return true;
}

} // namespace

void DrawFluentUnderlineStylePreview(
    const FluentD2DThemeContext& d2d,
    CompositionUnderlineStyle style,
    bool bold_line,
    const D2D1_RECT_F& sample_rect,
    const D2D1_COLOR_F& color) noexcept {
    if (!d2d.d2d_target || !d2d.d2d_factory || style == CompositionUnderlineStyle::None) {
        return;
    }
    const float x0 = sample_rect.left;
    const float x1 = sample_rect.right;
    if (x1 <= x0 + 4.0f) {
        return;
    }
    const float y = (sample_rect.top + sample_rect.bottom) * 0.5f + d2d.ScaleF(kUnderlineBaselineOffset);
    const float stroke_w = d2d.ScaleF(bold_line ? 2 : 1) + (bold_line ? 0.6f : 0.3f);
    const auto brush = CreateSolidBrush(d2d.d2d_target.Get(), color);
    if (!brush) {
        return;
    }

    if (style == CompositionUnderlineStyle::Squiggle) {
        const float step = (std::max)(2.0f, d2d.ScaleF(kUnderlineDotStep));
        const float amp = d2d.ScaleF(bold_line ? 2 : 1) + 0.8f;
        float x = x0;
        bool up = true;
        while (x < x1) {
            const float next_x = (std::min)(x + step, x1);
            const float y0 = up ? (y - amp) : (y + amp);
            const float y1 = up ? (y + amp) : (y - amp);
            d2d.d2d_target->DrawLine(D2D1::Point2F(x, y0), D2D1::Point2F(next_x, y1), brush.Get(), stroke_w);
            x = next_x;
            up = !up;
        }
        return;
    }

    ScopedD2DStrokeStyle stroke_style;
    if (style == CompositionUnderlineStyle::Dot || style == CompositionUnderlineStyle::Dash) {
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND,
            10.0f,
            style == CompositionUnderlineStyle::Dot ? D2D1_DASH_STYLE_DOT : D2D1_DASH_STYLE_DASH,
            0.0f);
        std::ignore = log::CheckHResult(d2d.d2d_factory->CreateStrokeStyle(&props, nullptr, 0, stroke_style.Put()),
                                       "Fluent::CreateStrokeStyle", log::Level::debug);
    }
    d2d.d2d_target->DrawLine(
        D2D1::Point2F(x0, y),
        D2D1::Point2F(x1, y),
        brush.Get(),
        stroke_w,
        stroke_style.Get());
}

void EnableFluentUnderlineComboPreview(HWND combo_hwnd, int bold_checkbox_id) noexcept {
    if (combo_hwnd && bold_checkbox_id > 0) {
        ::SetPropW(combo_hwnd, kUnderlineComboBoldIdProp, reinterpret_cast<HANDLE>(gsl::narrow_cast<INT_PTR>(bold_checkbox_id)));
    }
}

void PaintFluentComboBoxFrame(HWND hwnd, HDC hdc, const FluentD2DThemeContext& d2d) noexcept {
    if (!d2d.d2d_target || !hdc) {
        return;
    }
    RECT rc{};
    if (!::GetClientRect(hwnd, &rc)) {
        QIQI_LOG_WARN("Fluent combo client bounds query failed (error={})", ::GetLastError());
        return;
    }
    const ScopedSaveDC saved(hdc);
    if (!saved) {
        QIQI_LOG_WARN("Fluent combo drawing DC save failed");
        return;
    }
    if (d2d.clip_controls_to_dc) {
        RECT window_clip{};
        // GetDC and owner-draw notifications need not carry the window's scrolling region.
        const int region = ::GetWindowRgnBox(hwnd, &window_clip);
        if (region == NULLREGION) return;
        if (region != ERROR) {
            const int clip = ::IntersectClipRect(hdc, window_clip.left, window_clip.top,
                                                window_clip.right, window_clip.bottom);
            if (clip == ERROR) {
                QIQI_LOG_WARN("Fluent combo window-region clipping failed");
                return;
            }
            if (clip == NULLREGION) return;
        }
    }
    const auto& palette = d2d.appearance.palette;

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), hdc, rc, nullptr, d2d.clip_controls_to_dc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(ColorRefToD2DColor(palette.surface_bg));

    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    const D2D1_RECT_F box_rc = D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box_rc, d2d.ScaleF(kComboCornerRadius), d2d.ScaleF(kComboCornerRadius));

    const auto fill_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(d2d.appearance.high_contrast ? palette.input_bg :
            (palette.is_dark ? RGB(56, 60, 70) : kLightSurfaceBg)));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.border_color));
    const auto txt_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.text_primary));
    const auto chev_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(palette.text_secondary));

    if (fill_brush) {
        d2d.d2d_target->FillRoundedRectangle(rr, fill_brush.Get());
    }
    if (border_brush) {
        d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
    }

    const LRESULT cur_sel = ::SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    if (cur_sel >= 0 && d2d.fmt_body && txt_brush) {
        std::array<wchar_t, kFluentComboTextCapacity> item_text{};
        const LRESULT len = ::SendMessageW(hwnd, CB_GETLBTEXTLEN, gsl::narrow_cast<WPARAM>(cur_sel), 0);
        if (len > 0 && len < gsl::narrow_cast<LRESULT>(item_text.size() - 1)) {
            ::SendMessageW(
                hwnd, CB_GETLBTEXT, gsl::narrow_cast<WPARAM>(cur_sel),
                reinterpret_cast<LPARAM>(item_text.data()));
            CompositionUnderlineStyle preview_style = CompositionUnderlineStyle::Dot;
            bool preview_bold = false;
            const bool has_preview = QueryUnderlineComboBoldState(
                hwnd, preview_style, gsl::narrow_cast<UINT>(cur_sel), preview_bold);
            const float right_limit = w - d2d.ScaleF(
                has_preview ? kComboPreviewTextRightInset : kComboTextRightInset);
            const D2D1_RECT_F txt_rc = D2D1::RectF(d2d.ScaleF(kComboTextInset), 0.0f, right_limit, h);
            d2d.d2d_target->DrawTextW(
                item_text.data(),
                gsl::narrow_cast<UINT32>(wcslen(item_text.data())),
                d2d.fmt_body.Get(),
                txt_rc,
                txt_brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (has_preview) {
                const D2D1_COLOR_F accent_col = ColorRefToD2DColor(palette.text_accent);
                DrawFluentUnderlineStylePreview(
                    d2d,
                    preview_style,
                    preview_bold,
                    D2D1::RectF(w - d2d.ScaleF(kComboPreviewLeftInset), 0.0f,
                                w - d2d.ScaleF(kComboPreviewRightInset), h),
                    accent_col);
            }
        }
    }

    if (chev_brush) {
        const float cx = w - d2d.ScaleF(kComboChevronRightInset);
        const float cy = h * 0.5f + d2d.ScaleF(kComboChevronVerticalOffset);
        const float arm = d2d.ScaleF(kComboChevronArm);
        d2d.d2d_target->DrawLine(
            D2D1::Point2F(cx - arm, cy - arm * 0.6f),
            D2D1::Point2F(cx, cy + arm * 0.5f),
            chev_brush.Get(),
            1.6f);
        d2d.d2d_target->DrawLine(
            D2D1::Point2F(cx, cy + arm * 0.5f),
            D2D1::Point2F(cx + arm, cy - arm * 0.6f),
            chev_brush.Get(),
            1.6f);
    }
}

void DrawFluentComboBoxItem(const DRAWITEMSTRUCT& dis, const FluentD2DThemeContext& d2d,
                           bool disabled, const wchar_t* disabled_suffix) noexcept {
    if (!dis.hDC || dis.itemID == std::numeric_limits<UINT>::max()) {
        return;
    }
    std::array<wchar_t, kFluentComboTextCapacity> item_text{};
    const LRESULT len = ::SendMessageW(dis.hwndItem, CB_GETLBTEXTLEN, dis.itemID, 0);
    if (len > 0 && len < gsl::narrow_cast<LRESULT>(item_text.size())) {
        ::SendMessageW(dis.hwndItem, CB_GETLBTEXT, dis.itemID, reinterpret_cast<LPARAM>(item_text.data()));
    }
    const auto& palette = d2d.appearance.palette;
    if (disabled && d2d.appearance.high_contrast) paint::AppendDisabledSuffix(item_text, disabled_suffix);
    const COLORREF disabled_text = DisabledControlText(d2d);
    if (d2d.appearance.system_high_contrast || !d2d.d2d_target || !d2d.fmt_body) {
        const ScopedSaveDC saved(dis.hDC);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(dis.hwndItem, WM_GETFONT, 0, 0));
        const ScopedSelectObject selected_font(dis.hDC, font);
        const bool selected = !disabled && (dis.itemState & ODS_SELECTED) != 0;
        const auto background = paint::CreateThemeBrush(selected ? palette.selection_bg : palette.input_bg);
        if (background) ::FillRect(dis.hDC, &dis.rcItem, background.Get());
        ::SetTextColor(dis.hDC, disabled ? disabled_text : (selected ? palette.selection_text : palette.text_primary));
        ::SetBkMode(dis.hDC, TRANSPARENT);
        RECT text = dis.rcItem;
        text.left += d2d.ScalePx(kComboFallbackTextInset);
        ::DrawTextW(dis.hDC, item_text.data(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!disabled && (dis.itemState & ODS_FOCUS)) ::DrawFocusRect(dis.hDC, &dis.rcItem);
        return;
    }
    if ((dis.itemState & ODS_COMBOBOXEDIT) != 0) {
        const ScopedWindowDC full_dc(dis.hwndItem);
        PaintFluentComboBoxFrame(dis.hwndItem, full_dc.Get(), d2d);
        return;
    }
    const bool is_selected = !disabled && (dis.itemState & ODS_SELECTED) != 0;

    const COLORREF selection_bg = d2d.appearance.high_contrast ? palette.selection_bg :
        (palette.is_dark ? RGB(0, 96, 180) : RGB(220, 234, 252));
    const D2D1_COLOR_F bg_col = ColorRefToD2DColor(is_selected ? selection_bg : palette.input_bg);
    const D2D1_COLOR_F txt_col = ColorRefToD2DColor(
        disabled ? disabled_text :
        (is_selected && d2d.appearance.high_contrast ? palette.selection_text : palette.text_primary));

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem, nullptr, d2d.clip_controls_to_dc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(bg_col);

    const auto txt_brush = CreateSolidBrush(d2d.d2d_target.Get(), txt_col);
    if (d2d.fmt_body && txt_brush && gsl::at(item_text, 0) != L'\0') {
        const float item_w = static_cast<float>(dis.rcItem.right - dis.rcItem.left);
        const float item_h = static_cast<float>(dis.rcItem.bottom - dis.rcItem.top);
        CompositionUnderlineStyle preview_style = CompositionUnderlineStyle::Dot;
        bool preview_bold = false;
        const bool has_preview = QueryUnderlineComboBoldState(
            dis.hwndItem, preview_style, dis.itemID, preview_bold);
        const D2D1_RECT_F rc = D2D1::RectF(
            kComboItemTextLeftInsetPx, 0.0f,
            item_w - (has_preview ? d2d.ScaleF(kComboItemPreviewTextRightInset) : kComboItemTextRightInsetPx),
            item_h);
        d2d.d2d_target->DrawTextW(
            item_text.data(),
            gsl::narrow_cast<UINT32>(wcslen(item_text.data())),
            d2d.fmt_body.Get(),
            rc,
            txt_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (has_preview) {
            const D2D1_COLOR_F line_col = is_selected
                ? txt_col
                : ColorRefToD2DColor(palette.text_accent);
            DrawFluentUnderlineStylePreview(
                d2d,
                preview_style,
                preview_bold,
                D2D1::RectF(item_w - d2d.ScaleF(kComboItemPreviewLeftInset), 0.0f,
                            item_w - d2d.ScaleF(kComboItemPreviewRightInset), item_h),
                line_col);
        }
    }
}

} // namespace qiqi
