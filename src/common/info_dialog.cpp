// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/info_dialog.h"
#include "common/d2d_controls.h"
#include "common/dialog_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "common/version.h"

#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <new>
#include <array>
#include <cstddef>
#include <cwchar>

namespace qiqi {
namespace {

constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr POINT kContentOrigin{24, 20};
constexpr int kHeaderRightMargin = 20;
constexpr int kIconSize = 56;
constexpr int kSmallIconSize = 16;
constexpr int kAppIconResourceId = 1;
constexpr int kHeaderGap = 14;
constexpr int kHeaderInset = 2;
constexpr int kTitleHeight = 28;
constexpr int kSubtitleGap = 2;
constexpr int kSubtitleHeight = 24;
constexpr int kSeparatorGap = 16;
constexpr int kBodyGap = 12;
constexpr int kButtonGap = 12;
constexpr int kButtonHeight = 32;
constexpr int kBottomMargin = 14;
constexpr int kGitHubButtonWidth = 124;
constexpr int kOkButtonWidth = 98;
constexpr int kAboutClientWidth = 440;
constexpr int kHelpClientWidth = 530;
constexpr int kMinimumBodyHeight = 94;
constexpr int kBodyFontSize = 14;
constexpr int kTitleFontSize = 19;
constexpr int kDividerStrokeWidthPx = 1;
constexpr std::size_t kVersionTextCapacity = 256;

enum class InfoDialogKind : uint8_t {
    About = 0,
    Help = 1
};

struct InfoDialogLayout {
    RECT icon{};
    RECT title{};
    RECT subtitle{};
    RECT separator{};
    RECT body{};
    RECT github_button{};
    RECT ok_button{};
    SIZE client_size{};
};

#pragma warning(suppress : 26497) // DPI scaling calls Win32 MulDiv and cannot be constant-evaluated.
InfoDialogLayout BuildInfoDialogLayout(UINT dpi, int client_width, int body_height) noexcept {
    const auto scale = [dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    const POINT origin{scale(kContentOrigin.x), scale(kContentOrigin.y)};
    const int icon_size = scale(kIconSize);
    const int header_gap = scale(kHeaderGap);
    const int header_inset = scale(kHeaderInset);
    const int title_height = scale(kTitleHeight);
    const int subtitle_gap = scale(kSubtitleGap);
    const int subtitle_height = scale(kSubtitleHeight);
    const int separator_gap = scale(kSeparatorGap);
    const int body_gap = scale(kBodyGap);
    const int button_gap = scale(kButtonGap);
    const int button_height = scale(kButtonHeight);
    const int bottom_margin = scale(kBottomMargin);

    InfoDialogLayout layout;
    layout.icon = {origin.x, origin.y, origin.x + icon_size, origin.y + icon_size};
    layout.title = {
        layout.icon.right + header_gap, layout.icon.top + header_inset,
        client_width - scale(kHeaderRightMargin), layout.icon.top + header_inset + title_height};
    layout.subtitle = {
        layout.title.left, layout.title.bottom + subtitle_gap,
        layout.title.right, layout.title.bottom + subtitle_gap + subtitle_height};
    const LONG separator_y = std::max(layout.icon.bottom, layout.subtitle.bottom) + separator_gap;
    layout.separator = {origin.x, separator_y, client_width - origin.x, separator_y};
    const LONG body_top = layout.separator.bottom + body_gap;
    layout.body = {layout.separator.left, body_top, layout.separator.right, body_top + body_height};
    const LONG button_top = layout.body.bottom + button_gap;
    layout.github_button = {
        layout.body.left, button_top, layout.body.left + scale(kGitHubButtonWidth), button_top + button_height};
    layout.ok_button = {
        layout.body.right - scale(kOkButtonWidth), button_top, layout.body.right, button_top + button_height};
    layout.client_size = {client_width, layout.ok_button.bottom + bottom_margin};
    return layout;
}

struct InfoDialogContext {
    InfoDialogKind kind = InfoDialogKind::About;
    UiLanguage ui_language = UiLanguage::Auto;
    bool done = false;
    InfoDialogLayout layout;
    ScopedGdiHandle<HFONT> body_font;
    ScopedGdiHandle<HFONT> title_font;
    ScopedIcon icon_small;
    ScopedIcon icon;
    FluentD2DThemeContext d2d;
};

void ApplyInfoLanguage(HWND hwnd, const InfoDialogContext& ctx) noexcept {
    const auto& str = GetUiStrings(ctx.ui_language);
    ::SetWindowTextW(hwnd, ctx.kind == InfoDialogKind::Help ? str.help_title : str.about_title);
    if (ctx.kind == InfoDialogKind::About) {
        ::SetDlgItemTextW(hwnd, kIdBtnOpenGitHub, str.about_btn_github);
    }
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void PaintInfoDialog(HDC hdc, const RECT& client_rc, const InfoDialogContext& ctx) noexcept {
    const ScopedSaveDC save_dc(hdc);
    const bool is_dark = IsSystemDarkTheme();
    const auto& layout = ctx.layout;
    const auto& str = GetUiStrings(ctx.ui_language);

    const ScopedGdiHandle<HBRUSH> bg_brush(
        ::CreateSolidBrush(is_dark ? kDarkWindowBg : kLightSurfaceBg));
    ::FillRect(hdc, &client_rc, bg_brush.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    if (ctx.icon) {
        ::DrawIconEx(
            hdc, layout.icon.left, layout.icon.top, ctx.icon.Get(),
            layout.icon.right - layout.icon.left, layout.icon.bottom - layout.icon.top, 0, nullptr, DI_NORMAL);
    }
    {
        const ScopedSelectObject sel_title_font(hdc, ctx.title_font.Get());
        ::SetTextColor(hdc, is_dark ? kDarkTextColor : kLightTitleColor);
        RECT title_rc = layout.title;
        ::DrawTextW(
            hdc,
            ctx.kind == InfoDialogKind::Help ? str.help_header_title : str.about_app_name,
            -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const ScopedSelectObject sel_body_font(hdc, ctx.body_font.Get());
        ::SetTextColor(hdc, is_dark ? kDarkSecondaryTextColor : kLightSecondaryTextColor);
        RECT subtitle_rc = layout.subtitle;
        if (ctx.kind == InfoDialogKind::Help) {
            ::DrawTextW(hdc, str.help_header_subtitle, -1, &subtitle_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            std::array<wchar_t, kVersionTextCapacity> ver_text{};
            const int version_length = ::swprintf_s(
                ver_text.data(), ver_text.size(), L"%ls%ls (%ls)",
                str.about_version_prefix, version::kFullVersionW.data(), version::kArchW.data());
            if (version_length < 0) {
                QIQI_LOG_ERROR("About version text formatting failed");
                return;
            }
            ::DrawTextW(hdc, ver_text.data(), version_length, &subtitle_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        const ScopedGdiHandle<HPEN> div_pen(
            ::CreatePen(PS_SOLID, kDividerStrokeWidthPx, is_dark ? kDarkDividerColor : kLightDividerColor));
        const ScopedSelectObject sel_div(hdc, div_pen.Get());
        ::MoveToEx(hdc, layout.separator.left, layout.separator.top, nullptr);
        ::LineTo(hdc, layout.separator.right, layout.separator.top);

        ::SetTextColor(hdc, is_dark ? kDarkBodyTextColor : kLightBodyTextColor);
        RECT body_rc = layout.body;
        ::DrawTextW(
            hdc,
            ctx.kind == InfoDialogKind::Help ? str.help_shortcuts_info : str.about_license_info,
            -1, &body_rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
}

LRESULT CALLBACK InfoWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<InfoDialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [ctx](HDC hdc, const RECT& rc) { PaintInfoDialog(hdc, rc, *ctx); });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintInfoDialog(reinterpret_cast<HDC>(wparam), rect, *ctx);
            return 0;
        }
        case WM_DRAWITEM: {
            if (lparam != 0) {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (dis->CtlType == ODT_BUTTON) {
                    const bool is_dark = IsSystemDarkTheme();
                    DrawFluentPushButton(
                        *dis,
                        ctx->d2d,
                        dis->CtlID == kIdBtnOk,
                        ColorRefToD2DColor(is_dark ? kDarkWindowBg : kLightSurfaceBg));
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            ApplyInfoLanguage(hwnd, *ctx);
            return 0;
        case kSettingsLanguageChangedMessage:
            ctx->ui_language = static_cast<UiLanguage>(wparam);
            ApplyInfoLanguage(hwnd, *ctx);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case kIdBtnOk:
                case kIdBtnCancel:
                    ctx->done = true;
                    ::PostMessageW(hwnd, WM_NULL, 0, 0);
                    return 0;
                case kIdBtnOpenGitHub:
                    ::ShellExecuteW(hwnd, L"open", kGitHubRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                default:
                    return 0;
            }
        case WM_CLOSE:
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                const std::unique_ptr<InfoDialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

ScopedWindow CreateInfoWindow(
    InfoDialogKind kind, HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    if (!dialog_detail::RegisterDialogWindowClass(
            instance, kInfoWndClass, &InfoWndProc,
            CS_HREDRAW | CS_VREDRAW, nullptr, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1))) {
        QIQI_LOG_ERROR("About/Help dialog class registration failed (error={})", ::GetLastError());
        return {};
    }

    std::unique_ptr<InfoDialogContext> ctx;
    try {
        ctx = std::make_unique<InfoDialogContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("About/Help dialog context allocation failed");
        return {};
    }
    ctx->kind = kind;
    ctx->ui_language = ui_language;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    ctx->d2d.Init(mon.dpi);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->body_font = CreateUiFont(S(kBodyFontSize), FW_NORMAL);
    ctx->title_font = CreateUiFont(S(kTitleFontSize), FW_BOLD);
    const auto load_icon = [&](int size) noexcept {
        return use_embedded_icon ? LoadAppIconOrBrandFallback(instance, S(size), kAppIconResourceId, true) :
            dialog_detail::LoadOrCreateBrandIcon(S(size), instance);
    };
    ctx->icon_small = load_icon(kSmallIconSize);
    ctx->icon = load_icon(kIconSize);

    const int client_width_logical = (kind == InfoDialogKind::Help) ? kHelpClientWidth : kAboutClientWidth;
    int body_height = S(kMinimumBodyHeight);
    ctx->layout = BuildInfoDialogLayout(mon.dpi, S(client_width_logical), body_height);
    const int text_width_px = ctx->layout.body.right - ctx->layout.body.left;
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        const auto& lang_str = GetUiStrings(language);
        const wchar_t* const body_text =
            (kind == InfoDialogKind::Help) ? lang_str.help_shortcuts_info : lang_str.about_license_info;
        const int measured = dialog_detail::MeasureDialogTextHeight(
            ctx->body_font.Get(), body_text, text_width_px);
        if (measured <= 0) {
            return {};
        }
        body_height = std::max(body_height, measured);
    }
    ctx->layout = BuildInfoDialogLayout(mon.dpi, S(client_width_logical), body_height);

    const auto& str = GetUiStrings(ui_language);
    constexpr DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr DWORD wnd_ex_style = 0;
    RECT bounds = ComputeCenteredWindowBounds(
        ctx->layout.client_size.cx, ctx->layout.client_size.cy, wnd_style, wnd_ex_style, mon.work_area);
    const int dlg_w = bounds.right - bounds.left;
    const int dlg_h = bounds.bottom - bounds.top;
    if (parent) {
        RECT parent_rect{};
        if (!::GetWindowRect(parent, &parent_rect)) {
            QIQI_LOG_ERROR("About/Help dialog owner bounds unavailable (error={})", ::GetLastError());
            return {};
        }
        const POINT position = ClampPointToWorkArea({
            parent_rect.left + (parent_rect.right - parent_rect.left - dlg_w) / 2,
            parent_rect.top + (parent_rect.bottom - parent_rect.top - dlg_h) / 2},
            dlg_w, dlg_h, mon.work_area);
        bounds = {position.x, position.y, position.x + dlg_w, position.y + dlg_h};
    }

    const HFONT f = ctx->body_font.Get();
    ScopedWindow hwnd(::CreateWindowExW(
        wnd_ex_style,
        kInfoWndClass,
        kind == InfoDialogKind::Help ? str.help_title : str.about_title,
        wnd_style,
        bounds.left, bounds.top, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateInfoWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), IsSystemDarkTheme());
    QIQI_LOG_DEBUG("InfoWindow created (kind={}, pos=({}, {}), size={}x{}, dpi={})",
                   static_cast<unsigned int>(kind), bounds.left, bounds.top, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    dialog_detail::ApplyDialogIcons(hwnd.Get(), owned_ctx->icon_small.Get(), owned_ctx->icon.Get());

    const auto& layout = owned_ctx->layout;
    if (kind == InfoDialogKind::About) {
        if (!CreateFluentChildControl(
            hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.about_btn_github,
            BS_OWNERDRAW | WS_TABSTOP,
            layout.github_button.left, layout.github_button.top,
            layout.github_button.right - layout.github_button.left,
            layout.github_button.bottom - layout.github_button.top, kIdBtnOpenGitHub)) {
            return {};
        }
    }
    if (!CreateFluentChildControl(
        hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.dlg_btn_ok,
        BS_OWNERDRAW | WS_TABSTOP,
        layout.ok_button.left, layout.ok_button.top,
        layout.ok_button.right - layout.ok_button.left,
        layout.ok_button.bottom - layout.ok_button.top, kIdBtnOk)) {
        return {};
    }
    return hwnd;
}

} // namespace

ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    return CreateInfoWindow(InfoDialogKind::About, instance, parent, ui_language, use_embedded_icon);
}

bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    const ScopedWindow hwnd(CreateAboutWindow(instance, parent, ui_language, use_embedded_icon));
    const auto* ctx = reinterpret_cast<const InfoDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

ScopedWindow CreateHelpWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    return CreateInfoWindow(InfoDialogKind::Help, instance, parent, ui_language, use_embedded_icon);
}

bool ShowHelpDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    const ScopedWindow hwnd(CreateHelpWindow(instance, parent, ui_language, use_embedded_icon));
    const auto* ctx = reinterpret_cast<const InfoDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

} // namespace qiqi
