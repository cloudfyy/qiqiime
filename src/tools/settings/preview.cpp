// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/toolbar.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace qiqi::settings_detail {
namespace {

struct CommonCandidateFont {
    const wchar_t* family;
    const wchar_t* name_zh;

    const wchar_t* DisplayName(UiLanguage language) const noexcept {
        return LocalizedUiText(language, name_zh, family);
    }

    bool MatchesFamily(const std::wstring& name) const noexcept {
        return _wcsicmp(name.c_str(), family) == 0 || name == name_zh;
    }
};

constexpr std::array kCommonCandidateFonts{
    CommonCandidateFont{L"Microsoft YaHei UI", L"微软雅黑 UI"},
    CommonCandidateFont{L"SimSun", L"宋体"},
    CommonCandidateFont{L"DengXian", L"等线"},
    CommonCandidateFont{L"FangSong", L"仿宋"},
    CommonCandidateFont{L"KaiTi", L"楷体"},
    CommonCandidateFont{L"SimHei", L"黑体"},
};
constexpr UINT kMoreFonts = gsl::narrow_cast<UINT>(kCommonCandidateFonts.size() + 1);

DialogContext* BoundSettingsContext(HWND hwnd) noexcept {
    return reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void RefreshCandidateFontDraft(HWND hwnd, DialogContext& ctx) noexcept {
    ApplySettingsDisplayPreview(hwnd, ctx);
    UpdateSettingsWindowTitle(hwnd, ctx);
}

void ReportCandidateFontMenuError(void*, const char* operation, ULONG error) noexcept {
    if (error) QIQI_LOG_WARN("Candidate font menu {} failed (error={})", operation, error);
    else QIQI_LOG_WARN("Candidate font menu {} failed", operation);
}

UINT_PTR CALLBACK CenterCandidateFontDialog(HWND dialog, UINT message, WPARAM, LPARAM lparam) noexcept {
    if (message != WM_INITDIALOG) return 0;
    const auto* choice = reinterpret_cast<const CHOOSEFONTW*>(lparam);
    const HWND owner = choice ? choice->hwndOwner : nullptr;
    if (!owner) {
        QIQI_LOG_WARN("Candidate font dialog has no owner for positioning");
        return 0;
    }
    RECT owner_bounds{}, dialog_bounds{};
    if (!::GetWindowRect(owner, &owner_bounds) || !::GetWindowRect(dialog, &dialog_bounds)) {
        QIQI_LOG_WARN("Candidate font dialog bounds query failed (error={})", ::GetLastError());
        return 0;
    }
    const int width = dialog_bounds.right - dialog_bounds.left;
    const int height = dialog_bounds.bottom - dialog_bounds.top;
    const POINT center{
        owner_bounds.left + (owner_bounds.right - owner_bounds.left) / 2,
        owner_bounds.top + (owner_bounds.bottom - owner_bounds.top) / 2
    };
    const auto monitor = QueryMonitorForPoint(center, owner);
    const POINT position = ClampPointToWorkArea(
        {center.x - width / 2, center.y - height / 2}, width, height, monitor.work_area);
    if (!::SetWindowPos(dialog, nullptr, position.x, position.y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        QIQI_LOG_WARN("Candidate font dialog positioning failed (error={})", ::GetLastError());
    }
    return 0;
}

} // namespace

const wchar_t* SettingsCandidateFontDisplayName(const std::wstring& family, UiLanguage language) noexcept {
    for (const auto& font : kCommonCandidateFonts) {
        if (font.MatchesFamily(family)) return font.DisplayName(language);
    }
    return family.c_str();
}

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
        CF_SCREENFONTS | CF_FORCEFONTEXIST | CF_ENABLEHOOK |
        CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE |
        CF_SCALABLEONLY | CF_SCRIPTSONLY |
        CF_NOVERTFONTS | CF_NOVECTORFONTS | CF_NOOEMFONTS |
        CF_SELECTSCRIPT | CF_NOSCRIPTSEL;
    dialog.lpfnHook = CenterCandidateFontDialog;
    dialog.nSizeMin = 6;
    dialog.nSizeMax = 72;
    const auto* const original = &ctx;
    if (!chooser(&dialog)) {
        const DWORD error = ::CommDlgExtendedError();
        if (error != 0) {
            QIQI_LOG_WARN("Candidate font dialog failed (error={})", error);
        }
        return false;
    }
    if (BoundSettingsContext(hwnd) != original || ctx.done) return false;
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
    RefreshCandidateFontDraft(hwnd, ctx);
    return true;
}

bool ShowSettingsCandidateFontMenu(
    HWND hwnd, DialogContext& ctx, CandidateFontChooser chooser,
    gsl::not_null<decltype(&::TrackPopupMenuEx)> track_menu) noexcept {
    auto* const original = &ctx;
    if (BoundSettingsContext(hwnd) != original || ctx.done || ctx.transfer_in_progress) {
        QIQI_LOG_WARN("Candidate font menu requires an idle bound settings window");
        return false;
    }
    const HWND button = ::GetDlgItem(hwnd, kIdBtnCandidateFont);
    RECT bounds{};
    if (!button || !::GetWindowRect(button, &bounds)) {
        QIQI_LOG_WARN("Candidate font button bounds query failed (error={})",
            button ? ::GetLastError() : ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    std::array<ToolCommand, kCommonCandidateFonts.size() + 2> commands{};
    std::array<ScopedGdiHandle<HFONT>, kCommonCandidateFonts.size()> fonts;
    const auto language = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    size_t count = 0;
    try {
        for (size_t i = 0; i < kCommonCandidateFonts.size(); ++i) {
            const auto& choice = gsl::at(kCommonCandidateFonts, i);
            if (!CandidateFontFamilyAvailable(choice.family)) continue;
            auto& font = gsl::at(fonts, i);
            font = CreateCandidateFont({.family = std::wstring(choice.family)}, kFluentBodyFontSize, ctx.d2d.dpi,
                ctx.d2d.display_preferences.text_scale);
            gsl::at(commands, count++) = {
                .id = gsl::narrow_cast<UINT>(i + 1),
                .text = choice.DisplayName(language),
                .checked = choice.MatchesFamily(ctx.preview_candidate_font.family),
                .popup_font = font.Get()};
        }
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Candidate font menu sample allocation failed");
        return false;
    }
    if (count > 0) ++count; // Leave a separator before the advanced chooser.
    gsl::at(commands, count++) = {.id = kMoreFonts, .text = LocalizedUiText(language, L"更多…", L"More...")};

    ctx.transfer_in_progress = true;
    const auto finish = gsl::finally([hwnd, original]() noexcept {
        if (BoundSettingsContext(hwnd) != original) return;
        original->transfer_in_progress = false;
        if (!original->done && !::PostMessageW(hwnd, kSettingsExternalConfigChangedMessage, 0, 0)) {
            QIQI_LOG_WARN("Settings post-font-menu sync failed (error={})", ::GetLastError());
        }
    });
    UINT picked = 0;
    try {
        ToolCommandUi menu(ReportCandidateFontMenuError, nullptr);
        ctx.active_font_menu = &menu;
        const auto clear_menu = gsl::finally([hwnd, original]() noexcept {
            if (BoundSettingsContext(hwnd) == original) original->active_font_menu = nullptr;
        });
        TPMPARAMS placement{sizeof(placement), bounds};
        picked = menu.TrackContextMenu(hwnd, POINT{bounds.right, bounds.bottom},
            std::span(commands).first(count), &ctx.d2d, &placement, track_menu);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Candidate font menu allocation failed");
        return false;
    } catch (const std::runtime_error&) {
        // ToolCommandUi already reported the failing API and its error.
        return false;
    }
    if (picked == 0) return false;
    if (BoundSettingsContext(hwnd) != original || ctx.done) return false;
    if (picked == kMoreFonts) {
        return PromptSettingsCandidateFont(hwnd, ctx, chooser);
    }
    if (picked >= kMoreFonts) {
        QIQI_LOG_WARN("Candidate font menu returned an invalid selection (command={})", picked);
        return false;
    }
    const auto* family = gsl::at(kCommonCandidateFonts, gsl::narrow_cast<size_t>(picked - 1)).family;
    if (!CandidateFontFamilyAvailable(family)) {
        QIQI_LOG_WARN("Selected candidate font is no longer available");
        return false;
    }
    try {
        ctx.preview_candidate_font.family.assign(family);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Candidate font selection allocation failed");
        return false;
    }
    RefreshCandidateFontDraft(hwnd, ctx);
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
