// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/logger.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "common/paintutil.h"

#include <optional>
#include <tuple>

namespace qiqi {
namespace {
std::optional<bool> high_contrast_override;
constexpr int kCaptionFontSize = 12;
constexpr int kButtonFontSize = 14;
} // namespace

COLORREF DisabledControlText(const FluentD2DThemeContext& theme) noexcept {
    if (theme.appearance.system_high_contrast) return ::GetSysColor(COLOR_GRAYTEXT);
    return theme.appearance.high_contrast ? theme.appearance.palette.text_secondary : RGB(145, 145, 145);
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
    const auto& palette = theme.appearance.palette;
    ::SetTextColor(dc, palette.text_primary);
    if (input) {
        ::SetBkColor(dc, palette.input_bg);
    } else {
        ::SetBkMode(dc, TRANSPARENT);
    }
    const HBRUSH brush = input ? theme.input_brush.Get() : theme.surface_brush.Get();
    return brush ? brush : ::GetSysColorBrush(input ? COLOR_WINDOW : COLOR_BTNFACE);
}

int FluentD2DThemeContext::ScalePx(int px) const noexcept {
    return ScaleDisplayPx(px, dpi, display_preferences.text_scale);
}

float FluentD2DThemeContext::ScaleF(int px) const noexcept {
    return static_cast<float>(ScalePx(px));
}

void FluentD2DThemeContext::SetDisplayPreferences(DisplayPreferences preferences) noexcept {
    if (display_preferences == preferences) {
        RefreshThemeBrushes();
        return;
    }
    display_preferences = preferences;
    Init(dpi);
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
            std::ignore = log::CheckHResult(fmt_body->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),
                                           "Fluent::SetWordWrapping(body)");
        }
        if (fmt_btn) {
            std::ignore = log::CheckHResult(fmt_btn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER),
                                           "Fluent::SetTextAlignment(button)");
            std::ignore = log::CheckHResult(fmt_btn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                                           "Fluent::SetParagraphAlignment(button)");
            std::ignore = log::CheckHResult(fmt_btn->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),
                                           "Fluent::SetWordWrapping(button)");
        }
    }
    RefreshThemeBrushes();
}

void FluentD2DThemeContext::RefreshThemeBrushes() noexcept {
    appearance = ResolveDisplayAppearance(display_preferences);
    surface_brush = paint::CreateThemeBrush(appearance.palette.surface_bg);
    input_brush = paint::CreateThemeBrush(appearance.palette.input_bg);
}

} // namespace qiqi
