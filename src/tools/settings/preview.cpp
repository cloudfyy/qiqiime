// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace qiqi::settings_detail {

void ApplySettingsDisplayPreview(HWND hwnd, DialogContext& ctx) noexcept {
    QiQiConfig edited;
    ReadSettingsFromWindowControls(hwnd, edited);
    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL,
                         static_cast<WPARAM>(edited.ui_text_scale), 0);
    ctx.preview_theme = edited.ui_theme;
    ctx.preview_scale = edited.ui_text_scale;
    ctx.preview_easy_reading = edited.easy_reading;
    const auto preferences = GetDisplayPreferences(edited);
    ctx.d2d.SetDisplayPreferences(preferences);
    UpdateSettingsSkinPreview(hwnd, ctx);
    ApplyDisplayThemeToWindow(hwnd, ctx.d2d.appearance);
    auto font = CreateUiFont(ctx.d2d.ScalePx(settings_layout::kFontSize),
                             FW_NORMAL, CLEARTYPE_QUALITY);
    if (font) {
        ::EnumChildWindows(hwnd, [](HWND child, LPARAM handle) noexcept -> BOOL {
            ::SendMessageW(child, WM_SETFONT, gsl::narrow_cast<WPARAM>(handle), FALSE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font.Get()));
        ctx.font = std::move(font);
    } else {
        QIQI_LOG_ERROR("Settings preview font creation failed (error={})", ::GetLastError());
    }
    auto candidate_font = CreateCandidateFont(
        ctx.preview_candidate_font, kCandidateFontDefaultHeightPx,
        ctx.d2d.dpi, edited.ui_text_scale);
    if (candidate_font) {
        ctx.candidate_preview_font = std::move(candidate_font);
    } else {
        QIQI_LOG_ERROR("Settings candidate preview font creation failed (error={})", ::GetLastError());
    }
    UpdateCandidateFontLabel(hwnd, ctx);
    UpdateSettingsPageLabels(hwnd, ctx);
    LayoutSettingsControls(hwnd, ctx);
    EnsureSettingsControlVisible(hwnd, ::GetFocus(), ctx);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

bool PromptSettingsCandidateFont(
    HWND hwnd, DialogContext& ctx, CandidateFontChooser chooser) noexcept {
    if (!chooser) {
        QIQI_LOG_WARN("Candidate font chooser is unavailable");
        return false;
    }
    LOGFONTW selection{};
    if (!TryBuildCandidateFontWithFallback(
            ctx.preview_candidate_font, kCandidateFontDefaultHeightPx,
            ctx.d2d.dpi, UiTextScale::Percent100, selection)) {
        QIQI_LOG_WARN("Candidate font chooser initial font is unavailable");
        return false;
    }
    selection.lfCharSet = GB2312_CHARSET;
    CHOOSEFONTW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpLogFont = &selection;
    dialog.iPointSize = ctx.preview_candidate_font.point_size != 0
        ? gsl::narrow_cast<int>(ctx.preview_candidate_font.point_size) * 10
        : std::clamp(
            ::MulDiv(std::abs(gsl::narrow_cast<int>(selection.lfHeight)), 720,
                gsl::narrow_cast<int>(std::max(1u, ctx.d2d.dpi))),
            60, 720);
    dialog.Flags =
        CF_SCREENFONTS | CF_FORCEFONTEXIST |
        CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE |
        CF_SCALABLEONLY | CF_SCRIPTSONLY |
        CF_NOVERTFONTS | CF_NOVECTORFONTS | CF_NOOEMFONTS |
        CF_SELECTSCRIPT | CF_NOSCRIPTSEL;
    dialog.nSizeMin = 6;
    dialog.nSizeMax = 72;
    if (!chooser(&dialog)) {
        const DWORD error = ::CommDlgExtendedError();
        if (error != 0) {
            QIQI_LOG_WARN("Candidate font dialog failed (error={})", error);
        }
        return false;
    }
    const std::wstring_view selected_family =
        LogFontFaceNameView(selection);
    if (selected_family.empty() ||
        selected_family.size() >= LF_FACESIZE) {
        QIQI_LOG_WARN(
            "Candidate font dialog returned an invalid family (length={})",
            selected_family.size());
        return false;
    }
    if (!CandidateFontFamilyAvailable(selected_family)) {
        QIQI_LOG_WARN("Candidate font dialog returned a family that is no longer installed");
        return false;
    }
    try {
        CandidateFontPreferences selected{
            std::wstring(selected_family),
            gsl::narrow_cast<size_t>(std::clamp((dialog.iPointSize + 5) / 10, 6, 72)),
            CandidateFontWeightFromGdi(selection.lfWeight),
            selection.lfItalic != FALSE,
        };
        ctx.preview_candidate_font = std::move(selected);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Candidate font selection allocation failed");
        return false;
    }
    UpdateCandidateFontLabel(hwnd, ctx);
    ApplySettingsDisplayPreview(hwnd, ctx);
    UpdateSettingsWindowTitle(hwnd, ctx);
    ::InvalidateRect(::GetDlgItem(hwnd, kIdBtnCandidateFont), nullptr, FALSE);
    return true;
}

int MeasureSettingsCandidatePreviewHeight(HWND preview, int width, const DialogContext& ctx) noexcept {
    const ScopedWindowDC dc(preview);
    if (!dc) {
        QIQI_LOG_WARN("Settings preview measurement DC acquisition failed");
        return 0;
    }
    const HFONT font = ctx.candidate_preview_font ? ctx.candidate_preview_font.Get() : ctx.font.Get();
    return MeasureCandidateSample(dc.Get(), font, width, ctx.d2d.dpi,
        GetSelectedUiLanguageFromCombo(::GetParent(preview), ctx),
        ctx.d2d.display_preferences, CandidateSampleLayout::Compact);
}

void DrawSettingsCandidatePreview(const DRAWITEMSTRUCT& item, const DialogContext& ctx) noexcept {
    const HFONT font = ctx.candidate_preview_font ? ctx.candidate_preview_font.Get() : ctx.font.Get();
    std::ignore = PaintCandidateSample(item.hDC, item.rcItem, font, ctx.d2d.dpi,
        GetSelectedUiLanguageFromCombo(::GetParent(item.hwndItem), ctx),
        ctx.d2d.display_preferences, ctx.candidate_skin_colors,
        CandidateSampleLayout::Compact);
}

void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept {
    std::ignore = hwnd;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(ctx.d2d.appearance.palette.window_bg));
    if (!background) {
        QIQI_LOG_WARN("Settings background brush creation failed (error={})", ::GetLastError());
        return;
    }
    ::FillRect(hdc, &client_rc, background.Get());
    if (ctx.d2d.surface_brush) ::FillRect(hdc, &ctx.page_bounds, ctx.d2d.surface_brush.Get());
}

} // namespace qiqi::settings_detail
