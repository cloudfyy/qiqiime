// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/window.h"
#include "common/controls.h"
#include "common/dlgutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/theme.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <new>

namespace qiqi {
namespace {

constexpr int kMessageTextId = 0xFFFF;
constexpr int kWindowWidth = 560;
constexpr int kMessageBodyInset = 32;
constexpr int kMessageBodyTop = 52;
constexpr int kMessageExtraHeight = 124;
constexpr int kMessageButtonRightInset = 24;
constexpr int kMessageButtonBottomOffset = 42;
constexpr int kMessageButtonWidth = 100;
constexpr int kMessageButtonHeight = 32;
constexpr RECT kMessageCardInsets{18, 16, 18, 58};
constexpr int kMessageMinimumBodyHeight = 48;

struct MessageContext {
    UiLanguage language = UiLanguage::Auto;
    SettingsMessageKind kind = SettingsMessageKind::SaveFailure;
    bool done = false;
    int result = IDCANCEL;
    ScopedGdiHandle<HFONT> font;
    ScopedIcon icon_small;
    ScopedIcon icon_big;
    FluentD2DThemeContext d2d;
};

struct MessageStrings {
    const wchar_t* title;
    const wchar_t* text;
};

MessageStrings GetMessageStrings(UiLanguage language, SettingsMessageKind kind) noexcept {
    const auto& str = GetUiStrings(language);
    switch (kind) {
        case SettingsMessageKind::SaveFailure:
            return {str.dlg_title, str.dlg_save_failed};
        case SettingsMessageKind::SaveConflict:
            return {str.dlg_title, str.dlg_save_conflict};
        case SettingsMessageKind::DraftUnavailable:
            return {str.dlg_title, str.dlg_draft_unavailable};
        case SettingsMessageKind::FullCodeUnavailable:
            return {str.dlg_title, str.dlg_full_code_unavailable};
        case SettingsMessageKind::NotificationFailure:
            return {str.dlg_title, str.dlg_notify_failed};
        case SettingsMessageKind::DictManagerLaunchFailure:
            return {str.dlg_title, str.dlg_dict_manager_launch_failed};
        case SettingsMessageKind::InvalidPrimaryDict:
            return {str.dlg_title, str.dlg_invalid_primary_dict};
        default:
            QIQI_LOG_ERROR("Unknown Settings message kind");
            return {};
    }
}

void ApplyMessageLanguage(HWND hwnd, const MessageContext& ctx) noexcept {
    const auto strings = GetMessageStrings(ctx.language, ctx.kind);
    const auto& str = GetUiStrings(ctx.language);
    ::SetWindowTextW(hwnd, strings.title);
    ::SetDlgItemTextW(hwnd, kMessageTextId, strings.text);
    ::SetDlgItemTextW(hwnd, IDOK, str.dlg_btn_ok);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CloseMessage(HWND hwnd, MessageContext& ctx, int result) noexcept {
    ctx.result = result;
    ctx.done = true;
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
}

void PaintMessage(HDC dc, const RECT& rect, const MessageContext& ctx) noexcept {
    const ScopedD2DDcDraw draw(ctx.d2d.d2d_target.Get(), dc, rect);
    if (!draw) return;
    const bool dark = IsSystemDarkTheme();
    ctx.d2d.d2d_target->Clear(
        ColorRefToD2DColor(dark ? kDarkWindowBg : kLightCaptionBg));
    const auto zh = GetMessageStrings(UiLanguage::ZhCn, ctx.kind);
    const auto en = GetMessageStrings(UiLanguage::EnUs, ctx.kind);
    const std::array<FluentCardSpec, 1> cards = {{
        {D2D1::RectF(ctx.d2d.ScaleF(kMessageCardInsets.left), ctx.d2d.ScaleF(kMessageCardInsets.top),
                     static_cast<float>(rect.right) - ctx.d2d.ScaleF(kMessageCardInsets.right),
                     static_cast<float>(rect.bottom) - ctx.d2d.ScaleF(kMessageCardInsets.bottom)),
         zh.title, en.title}
    }};
    DrawFluentCardGroup(ctx.d2d, cards,
                        ResolveUiLanguage(ctx.language) == UiLanguage::EnUs, dark);
}

LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<MessageContext>(hwnd, message, lparam);
    if (!ctx || message == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
    switch (message) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [ctx](HDC dc, const RECT& rect) noexcept {
                PaintMessage(dc, rect, *ctx);
            });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintMessage(reinterpret_cast<HDC>(wparam), rect, *ctx);
            return 0;
        }
        case WM_DRAWITEM:
            if (lparam != 0) {
                const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (item->CtlType == ODT_BUTTON) {
                    DrawFluentPushButton(*item, ctx->d2d, item->CtlID == IDOK,
                        ColorRefToD2DColor(IsSystemDarkTheme() ? kDarkWindowBg : kLightCaptionBg));
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            ::SetTextColor(dc, IsSystemDarkTheme() ? kDarkTextColor : kLightTextColor);
            ::SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(ctx->d2d.surface_brush.Get());
        }
        case WM_SETTINGCHANGE:
            std::ignore = UpdateToolWindowDisplay(hwnd, ctx->d2d, ctx->font,
                ctx->d2d.display_preferences, ctx->d2d.dpi, ctx->language);
            [[fallthrough]];
        case WM_THEMECHANGED:
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            ApplyMessageLanguage(hwnd, *ctx);
            return 0;
        case kSettingsLanguageChangedMessage:
            if (const auto language = dialog_detail::DecodeDialogLanguage(wparam)) {
                ctx->language = *language;
                std::ignore = UpdateToolWindowDisplay(hwnd, ctx->d2d, ctx->font,
                    ctx->d2d.display_preferences, ctx->d2d.dpi, ctx->language);
                ApplyMessageLanguage(hwnd, *ctx);
            }
            return 0;
        case WM_KEYDOWN: {
            if (wparam == VK_ESCAPE || wparam == VK_RETURN) {
                CloseMessage(hwnd, *ctx, IDOK);
                return 0;
            }
            if (wparam == VK_TAB) {
                ::SetFocus(::GetDlgItem(hwnd, IDOK));
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
            ::SetFocus(::GetDlgItem(hwnd, IDOK));
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == IDOK || id == IDCANCEL) {
                CloseMessage(hwnd, *ctx, id);
            }
            return 0;
        }
        case WM_CLOSE:
            CloseMessage(hwnd, *ctx, IDOK);
            return 0;
        case WM_NCDESTROY: {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            const std::unique_ptr<MessageContext> owned(ctx);
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

ScopedWindow CreateSettingsMessageWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept {
    const auto strings = GetMessageStrings(ui_language, kind);
    if (!strings.title || !strings.text) {
        return {};
    }
    if (!dialog_detail::RegisterDialogWindowClass(instance, kSettingsMessageWndClass, &MessageWndProc)) {
        QIQI_LOG_ERROR("Settings message window registration failed (error={})", ::GetLastError());
        return {};
    }
    std::unique_ptr<MessageContext> ctx;
    try {
        ctx = std::make_unique<MessageContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings message context allocation failed");
        return {};
    }
    ctx->language = ui_language;
    ctx->kind = kind;
    const auto monitor = QueryActiveMonitor(parent);
    const auto scale = [dpi = monitor.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    if (!UpdateToolWindowDisplay(nullptr, ctx->d2d, ctx->font, {}, monitor.dpi, ui_language))
        return {};
    ctx->icon_small = dialog_detail::LoadOrCreateBrandIcon(scale(kSmallBrandIconSize), instance);
    ctx->icon_big = dialog_detail::LoadOrCreateBrandIcon(scale(kLargeBrandIconSize), instance);
    if (!ctx->font || !ctx->d2d.d2d_target || !ctx->d2d.surface_brush ||
        !ctx->d2d.fmt_caption || !ctx->d2d.ButtonTextFormat(true)) {
        QIQI_LOG_ERROR("Settings message drawing resources unavailable");
        return {};
    }
    const int body_height = dialog_detail::MeasureLocalizedDialogTextHeight(
        scale(kWindowWidth - 2 * kMessageBodyInset), scale(kFluentBodyFontSize), scale(kMessageMinimumBodyHeight),
        [kind](UiLanguage language) noexcept { return GetMessageStrings(language, kind).text; });
    if (body_height <= 0) return {};
    const int height = body_height + scale(kMessageExtraHeight);
    constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr DWORD ex_style = 0;
    const RECT bounds = ComputeCenteredWindowBounds(
        scale(kWindowWidth), height, style, ex_style, monitor.work_area);
    ScopedWindow hwnd(::CreateWindowExW(ex_style, kSettingsMessageWndClass, strings.title, style,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("Settings message creation failed (error={})", ::GetLastError());
        return {};
    }
    const auto* owned = ctx.release();
    dialog_detail::ApplyDialogIcons(hwnd.Get(), owned->icon_small.Get(), owned->icon_big.Get());
    const HFONT font = owned->font.Get();
    const auto* theme = &owned->d2d;
    const auto* body = CreateFluentChildControl(hwnd.Get(), instance, font, theme,
        L"STATIC", strings.text, SS_LEFT | SS_NOPREFIX, scale(kMessageBodyInset), scale(kMessageBodyTop),
        scale(kWindowWidth - 2 * kMessageBodyInset), body_height, kMessageTextId);
    if (!body || !::IsWindow(hwnd.Get())) {
        QIQI_LOG_ERROR("Settings message body creation failed");
        return {};
    }
    const auto* primary = CreateFluentChildControl(hwnd.Get(), instance, font, theme,
        L"BUTTON", GetUiStrings(ui_language).dlg_btn_ok,
        BS_OWNERDRAW | WS_TABSTOP, scale(kWindowWidth - kMessageButtonRightInset - kMessageButtonWidth),
        height - scale(kMessageButtonBottomOffset), scale(kMessageButtonWidth), scale(kMessageButtonHeight), IDOK);
    if (!primary || !::IsWindow(hwnd.Get())) {
        QIQI_LOG_ERROR("Settings message button creation failed");
        return {};
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), IsSystemDarkTheme());
    return hwnd;
}

int ShowSettingsMessage(HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept {
    const ScopedWindow hwnd(CreateSettingsMessageWindow(g_dll_instance, parent, ui_language, kind));
    if (!hwnd) {
        return 0;
    }
    const auto* ctx = reinterpret_cast<const MessageContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    if (!ctx) {
        QIQI_LOG_ERROR("Settings message context missing");
        return 0;
    }
    if (!dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done, true)) {
        QIQI_LOG_WARN("Settings message modal loop did not complete");
        return 0;
    }
    return ctx->result;
}

} // namespace qiqi
