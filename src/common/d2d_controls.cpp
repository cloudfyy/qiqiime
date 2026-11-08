#include "common/d2d_controls.h"

#include "common/logger.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

#include <commctrl.h>
#include <array>
#include <gsl/gsl>
#include <cwchar>
#include <limits>
#include <tuple>

namespace qiqi {
namespace {
std::optional<bool> high_contrast_override;
constexpr size_t kButtonTextCapacity = 64;
constexpr size_t kFallbackButtonTextCapacity = 256;
constexpr size_t kToggleLabelCapacity = 128;
constexpr int kCaptionFontSize = 12;
constexpr int kButtonFontSize = 14;
constexpr int kCardCornerRadius = 8;
constexpr RECT kCardAccentOffsets{12, 10, 16, 26};
constexpr int kCardAccentRadius = 2;
constexpr int kCardHeaderLeftInset = 24;
constexpr int kCardHeaderTopOffset = 9;
constexpr int kCardHeaderRightInset = 12;
constexpr int kCardHeaderBottomOffset = 29;
constexpr int kButtonCornerRadius = 6;
constexpr int kToggleTextInset = 4;
constexpr int kRadioOuterRadius = 8;
constexpr int kRadioCenterX = 10;
constexpr int kRadioInnerRadius = 4;
constexpr int kRadioTextInset = 26;
constexpr int kToggleRightInset = 6;
constexpr int kToggleTrackWidth = 36;
constexpr int kToggleTrackRadius = 9;
constexpr int kToggleKnobRadius = 6;
constexpr int kToggleLabelGap = 8;
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
}

void SetHighContrastOverrideForTest(std::optional<bool> enabled) noexcept {
    high_contrast_override = enabled;
}

bool IsHighContrastEnabled() noexcept {
    if (high_contrast_override) return *high_contrast_override;
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

HBRUSH ApplyFluentControlColors(HDC dc, const FluentD2DThemeContext& theme, bool input) noexcept {
    if (IsHighContrastEnabled()) {
        ::SetTextColor(dc, ::GetSysColor(input ? COLOR_WINDOWTEXT : COLOR_BTNTEXT));
        ::SetBkColor(dc, ::GetSysColor(input ? COLOR_WINDOW : COLOR_BTNFACE));
        ::SetBkMode(dc, input ? OPAQUE : TRANSPARENT);
        return ::GetSysColorBrush(input ? COLOR_WINDOW : COLOR_BTNFACE);
    }
    const bool dark = IsSystemDarkTheme();
    ::SetTextColor(dc, dark ? kDarkTextColor : kLightTextColor);
    if (input) {
        ::SetBkColor(dc, dark ? RGB(54, 58, 68) : RGB(255, 255, 255));
    } else {
        ::SetBkMode(dc, TRANSPARENT);
    }
    const HBRUSH brush = input ? theme.input_brush.Get() : theme.surface_brush.Get();
    return brush ? brush : ::GetSysColorBrush(input ? COLOR_WINDOW : COLOR_BTNFACE);
}

int FluentD2DThemeContext::ScalePx(int px) const noexcept {
    return ScalePxByDpi(px, dpi);
}

float FluentD2DThemeContext::ScaleF(int px) const noexcept {
    return static_cast<float>(ScalePx(px));
}

void FluentD2DThemeContext::Init(UINT monitor_dpi) noexcept {
    dpi = monitor_dpi > 0 ? monitor_dpi : 96u;
    fmt_body.Reset();
    fmt_caption.Reset();
    fmt_btn.Reset();
    if (!CreateD2DDcTargetAndDWrite(d2d_factory, dwrite_factory, d2d_target)) {
        QIQI_LOG_ERROR("Fluent Direct2D/DirectWrite context creation failed");
    }
    LOGFONTW system_font{};
    if (dwrite_factory && TryGetSystemUiFont(system_font)) {
        std::ignore = log::CheckHResult(dwrite_factory->CreateTextFormat(
            std::data(system_font.lfFaceName), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(kFluentBodyFontSize), L"zh-CN", fmt_body.Put()), "Fluent::CreateTextFormat(body)");
        std::ignore = log::CheckHResult(dwrite_factory->CreateTextFormat(
            std::data(system_font.lfFaceName), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(kCaptionFontSize), L"zh-CN", fmt_caption.Put()), "Fluent::CreateTextFormat(caption)");
        std::ignore = log::CheckHResult(dwrite_factory->CreateTextFormat(
            std::data(system_font.lfFaceName), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(kButtonFontSize), L"zh-CN", fmt_btn.Put()), "Fluent::CreateTextFormat(button)");
        if (!fmt_body || !fmt_caption || !fmt_btn) {
            QIQI_LOG_ERROR("Fluent DirectWrite text format creation failed");
        }
        if (fmt_body) {
            std::ignore = log::CheckHResult(fmt_body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                                           "Fluent::SetParagraphAlignment(body)");
            std::ignore = log::CheckHResult(fmt_body->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                                           "Fluent::SetWordWrapping(body)");
        }
        if (fmt_btn) {
            std::ignore = log::CheckHResult(fmt_btn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER),
                                           "Fluent::SetTextAlignment(button)");
            std::ignore = log::CheckHResult(fmt_btn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                                           "Fluent::SetParagraphAlignment(button)");
            std::ignore = log::CheckHResult(fmt_btn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                                           "Fluent::SetWordWrapping(button)");
        }
    }
    RefreshThemeBrushes();
}

void FluentD2DThemeContext::RefreshThemeBrushes() noexcept {
    const bool is_dark = IsSystemDarkTheme();
    surface_brush.Reset(::CreateSolidBrush(IsHighContrastEnabled() ? ::GetSysColor(COLOR_BTNFACE) :
        (is_dark ? RGB(43, 46, 52) : RGB(255, 255, 255))));
    input_brush.Reset(::CreateSolidBrush(IsHighContrastEnabled() ? ::GetSysColor(COLOR_WINDOW) :
        (is_dark ? RGB(54, 58, 68) : RGB(255, 255, 255))));
    if (!surface_brush || !input_brush) {
        QIQI_LOG_ERROR("Fluent theme brush creation failed (error={})", ::GetLastError());
    }
}

void DrawFluentCardGroup(
    const FluentD2DThemeContext& d2d,
    std::span<const FluentCardSpec> cards,
    bool is_en,
    bool is_dark) noexcept {
    if (!d2d.d2d_target) {
        return;
    }
    const auto card_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? ColorRefToD2DColor(kDarkDividerColor) : MakeD2DColor(222, 227, 236));
    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(76, 160, 255) : MakeD2DColor(0, 103, 192));
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
                    card.rect.left + d2d.ScaleF(kCardHeaderLeftInset), card.rect.top + d2d.ScaleF(kCardHeaderTopOffset),
                    card.rect.right - d2d.ScaleF(kCardHeaderRightInset), card.rect.top + d2d.ScaleF(kCardHeaderBottomOffset));
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
    D2D1_COLOR_F outer_bg) noexcept {
    if (!dis.hDC) {
        return;
    }
    if (IsHighContrastEnabled() || !d2d.d2d_target || !d2d.fmt_btn) {
        const ScopedSaveDC saved(dis.hDC);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(dis.hwndItem, WM_GETFONT, 0, 0));
        const ScopedSelectObject selected_font(dis.hDC, font);
        RECT rect = dis.rcItem;
        ::DrawFrameControl(dis.hDC, &rect, DFC_BUTTON,
            DFCS_BUTTONPUSH | ((dis.itemState & ODS_SELECTED) ? DFCS_PUSHED : 0) |
            ((dis.itemState & ODS_DISABLED) ? DFCS_INACTIVE : 0));
        std::array<wchar_t, kFallbackButtonTextCapacity> text{};
        ::GetWindowTextW(dis.hwndItem, text.data(), gsl::narrow_cast<int>(text.size()));
        ::SetTextColor(dis.hDC, ::GetSysColor((dis.itemState & ODS_DISABLED) ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
        ::SetBkMode(dis.hDC, TRANSPARENT);
        ::DrawTextW(dis.hDC, text.data(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (dis.itemState & ODS_FOCUS) {
            ::InflateRect(&rect, -3, -3);
            ::DrawFocusRect(dis.hDC, &rect);
        }
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_pressed = (dis.itemState & ODS_SELECTED) != 0;

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

    const D2D1_RECT_F rc = D2D1::RectF(
        1.0f, 1.0f,
        static_cast<float>(dis.rcItem.right - dis.rcItem.left) - 1.0f,
        static_cast<float>(dis.rcItem.bottom - dis.rcItem.top) - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, d2d.ScaleF(kButtonCornerRadius), d2d.ScaleF(kButtonCornerRadius));

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
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
    if (border_brush) {
        d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
    }

    std::array<wchar_t, kButtonTextCapacity> btn_text{};
    ::GetWindowTextW(dis.hwndItem, btn_text.data(), gsl::narrow_cast<int>(btn_text.size()));
    if (d2d.fmt_btn && txt_brush && gsl::at(btn_text, 0) != L'\0') {
        d2d.d2d_target->DrawTextW(
            btn_text.data(),
            gsl::narrow_cast<UINT32>(wcslen(btn_text.data())),
            d2d.fmt_btn.Get(),
            rc,
            txt_brush.Get());
    }
}

void DrawFluentToggleOrRadioRow(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_radio) noexcept {
    if (!d2d.d2d_target || !dis.hDC) {
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_checked = (::SendMessageW(dis.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool is_enabled = (::IsWindowEnabled(dis.hwndItem) != FALSE);

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));

    const float h = static_cast<float>(dis.rcItem.bottom - dis.rcItem.top);
    const float w = static_cast<float>(dis.rcItem.right - dis.rcItem.left);
    const float cy = h * 0.5f;

    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(76, 160, 255) : MakeD2DColor(0, 103, 192));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(130, 138, 152) : MakeD2DColor(138, 146, 158));
    const auto knob_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        is_checked ? MakeD2DColor(255, 255, 255)
                   : (is_dark ? MakeD2DColor(186, 194, 206) : MakeD2DColor(96, 104, 118)));
    const auto text_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        !is_enabled ? (is_dark ? MakeD2DColor(110, 116, 128) : MakeD2DColor(150, 156, 166))
                    : ColorRefToD2DColor(is_dark ? kDarkTextColor : kLightTextColor));

    float text_left = d2d.ScaleF(kToggleTextInset);
    float text_right = w - d2d.ScaleF(kToggleTextInset);

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
        } else if (border_brush) {
            d2d.d2d_target->DrawEllipse(outer, border_brush.Get(), 1.5f);
        }
        text_left = d2d.ScaleF(kRadioTextInset);
    } else {
        const float sw_right = w - d2d.ScaleF(kToggleRightInset);
        const float sw_left = sw_right - d2d.ScaleF(kToggleTrackWidth);
        const D2D1_ROUNDED_RECT track = D2D1::RoundedRect(
            D2D1::RectF(sw_left, cy - d2d.ScaleF(kToggleTrackRadius), sw_right, cy + d2d.ScaleF(kToggleTrackRadius)),
            d2d.ScaleF(kToggleTrackRadius), d2d.ScaleF(kToggleTrackRadius));
        if (is_checked && accent_brush) {
            d2d.d2d_target->FillRoundedRectangle(track, accent_brush.Get());
        } else if (border_brush) {
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
}

namespace {

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
    ::GetClientRect(hwnd, &rc);
    const bool is_dark = IsSystemDarkTheme();

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), hdc, rc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));

    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    const D2D1_RECT_F box_rc = D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box_rc, d2d.ScaleF(kComboCornerRadius), d2d.ScaleF(kComboCornerRadius));

    const auto fill_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(56, 60, 70) : ColorRefToD2DColor(kLightSurfaceBg));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(86, 92, 106) : MakeD2DColor(204, 210, 222));
    const auto txt_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), ColorRefToD2DColor(is_dark ? kDarkTextColor : kLightTextColor));
    const auto chev_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(186, 194, 208) : MakeD2DColor(96, 104, 118));

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
                const D2D1_COLOR_F accent_col = is_dark ? MakeD2DColor(96, 178, 255) : MakeD2DColor(0, 103, 192);
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

void DrawFluentComboBoxItem(const DRAWITEMSTRUCT& dis, const FluentD2DThemeContext& d2d) noexcept {
    if (!dis.hDC || dis.itemID == std::numeric_limits<UINT>::max()) {
        return;
    }
    std::array<wchar_t, kFluentComboTextCapacity> item_text{};
    const LRESULT len = ::SendMessageW(dis.hwndItem, CB_GETLBTEXTLEN, dis.itemID, 0);
    if (len > 0 && len < gsl::narrow_cast<LRESULT>(item_text.size())) {
        ::SendMessageW(dis.hwndItem, CB_GETLBTEXT, dis.itemID, reinterpret_cast<LPARAM>(item_text.data()));
    }
    if (IsHighContrastEnabled() || !d2d.d2d_target || !d2d.fmt_body) {
        const ScopedSaveDC saved(dis.hDC);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(dis.hwndItem, WM_GETFONT, 0, 0));
        const ScopedSelectObject selected_font(dis.hDC, font);
        const bool selected = (dis.itemState & ODS_SELECTED) != 0;
        ::FillRect(dis.hDC, &dis.rcItem, ::GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        ::SetTextColor(dis.hDC, ::GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        ::SetBkMode(dis.hDC, TRANSPARENT);
        RECT text = dis.rcItem;
        text.left += d2d.ScalePx(kComboFallbackTextInset);
        ::DrawTextW(dis.hDC, item_text.data(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (dis.itemState & ODS_FOCUS) ::DrawFocusRect(dis.hDC, &dis.rcItem);
        return;
    }
    if ((dis.itemState & ODS_COMBOBOXEDIT) != 0) {
        const ScopedWindowDC full_dc(dis.hwndItem);
        PaintFluentComboBoxFrame(dis.hwndItem, full_dc.Get(), d2d);
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_selected = (dis.itemState & ODS_SELECTED) != 0;

    const D2D1_COLOR_F bg_col = is_selected
        ? (is_dark ? MakeD2DColor(0, 96, 180) : MakeD2DColor(220, 234, 252))
        : (is_dark ? MakeD2DColor(54, 58, 68) : MakeD2DColor(255, 255, 255));
    const D2D1_COLOR_F txt_col = ColorRefToD2DColor(is_dark ? kDarkTextColor : kLightTextColor);

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
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
                : (is_dark ? MakeD2DColor(96, 178, 255) : MakeD2DColor(0, 103, 192));
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

namespace {

LRESULT CALLBACK FluentToggleSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const int radio_range_pack = gsl::narrow_cast<int>(dwRefData);
    const bool is_radio = (radio_range_pack != 0);
    switch (msg) {
        case BM_GETCHECK:
            return ::GetPropW(hwnd, L"QiQiChecked") ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK: {
            if (wparam == BST_CHECKED) {
                if (!::SetPropW(hwnd, L"QiQiChecked", reinterpret_cast<HANDLE>(1))) {
                    QIQI_LOG_ERROR("Fluent toggle SetPropW failed (error={})", ::GetLastError());
                }
            } else {
                ::RemovePropW(hwnd, L"QiQiChecked");
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            if (msg == WM_KEYUP && wparam != VK_SPACE) {
                break;
            }
            if ((::SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) == 0) {
                break;
            }
            if (msg == WM_LBUTTONUP) {
                RECT rect{};
                ::GetClientRect(hwnd, &rect);
                if (!::PtInRect(&rect, PointFromLParam(lparam))) {
                    break;
                }
            }
            if (::IsWindowEnabled(hwnd)) {
                const HWND parent = ::GetParent(hwnd);
                const int id = ::GetDlgCtrlID(hwnd);
                if (is_radio) {
                    const int first_id = LOWORD(radio_range_pack);
                    const int last_id = HIWORD(radio_range_pack);
                    for (int rid = first_id; rid <= last_id; ++rid) {
                        if (const HWND rctrl = ::GetDlgItem(parent, rid)) {
                            DWORD_PTR rref = 0;
                            if (::GetWindowSubclass(rctrl, &FluentToggleSubclassProc, uIdSubclass, &rref) &&
                                rref == dwRefData) {
                                ::SendMessageW(rctrl, BM_SETCHECK, (rid == id) ? BST_CHECKED : BST_UNCHECKED, 0);
                            }
                        }
                    }
                } else {
                    const bool cur = (::SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    ::SendMessageW(hwnd, BM_SETCHECK, cur ? BST_UNCHECKED : BST_CHECKED, 0);
                }
            }
            break;
        }
        case WM_NCDESTROY:
            ::RemovePropW(hwnd, L"QiQiChecked");
            ::RemoveWindowSubclass(hwnd, &FluentToggleSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK FluentComboBoxSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const auto* d2d = reinterpret_cast<const FluentD2DThemeContext*>(dwRefData);
    const bool fluent = d2d && d2d->d2d_target && d2d->fmt_body && !IsHighContrastEnabled();
    switch (msg) {
        case WM_ERASEBKGND:
            if (fluent) return 1;
            break;
        case WM_PAINT: {
            if (!fluent) {
                break;
            }
            const ScopedPaintDC paint_dc(hwnd);
            PaintFluentComboBoxFrame(hwnd, paint_dc.Get(), *d2d);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case CB_SETCURSEL: {
            const LRESULT res = ::DefSubclassProc(hwnd, msg, wparam, lparam);
            DWORD_PTR current_ref = 0;
            // A parent notification may destroy the child and its borrowed context.
            if (::GetWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass, &current_ref) &&
                current_ref == dwRefData && fluent) {
                const ScopedWindowDC dc(hwnd);
                PaintFluentComboBoxFrame(hwnd, dc.Get(), *d2d);
            }
            return res;
        }
        case WM_NCDESTROY:
            ::RemovePropW(hwnd, kUnderlineComboBoldIdProp);
            ::RemoveWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

} // namespace

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
    DWORD_PTR radio_group_range) noexcept {
    const bool is_button = (_wcsicmp(cls, L"BUTTON") == 0);
    const DWORD button_type = style & BS_TYPEMASK;
    const bool is_radio = is_button && button_type == BS_AUTORADIOBUTTON;
    const bool is_check = is_button && button_type == BS_AUTOCHECKBOX;
    const bool is_combo = (_wcsicmp(cls, L"COMBOBOX") == 0);
    if (is_radio && (radio_group_range == 0 || id < LOWORD(radio_group_range) || id > HIWORD(radio_group_range))) {
        QIQI_LOG_ERROR("Fluent radio control {} has an invalid group range", id);
        return nullptr;
    }
    DWORD actual_style = style;
    if (is_radio || is_check) {
        actual_style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW | WS_TABSTOP;
    }
    ScopedWindow ctrl(::CreateWindowExW(
        0,
        cls,
        text,
        WS_CHILD | WS_VISIBLE | actual_style,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(gsl::narrow_cast<INT_PTR>(id)),
        inst,
        nullptr));
    if (!ctrl) {
        QIQI_LOG_ERROR("CreateFluentChildControl {} failed (error={})", id, ::GetLastError());
        return nullptr;
    }
    if (font) {
        ::SendMessageW(ctrl.Get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (is_radio || is_check) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentToggleSubclassProc, 1, is_radio ? radio_group_range : 0)) {
            QIQI_LOG_ERROR("Fluent toggle {} SetWindowSubclass failed", id);
            return nullptr;
        }
    } else if (is_combo && d2d_ctx) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentComboBoxSubclassProc, 2, reinterpret_cast<DWORD_PTR>(d2d_ctx))) {
            QIQI_LOG_ERROR("Fluent combo {} SetWindowSubclass failed", id);
            return nullptr;
        }
    }
    return ctrl.Detach();
}

} // namespace qiqi
