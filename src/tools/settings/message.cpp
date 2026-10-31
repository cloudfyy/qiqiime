#include "tools/settings/window.h"
#include "tools/settings/util.h"
#include "common/d2d_controls.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/theme.h"

#include <algorithm>
#include <array>
#include <memory>
#include <new>

namespace qiqi {
namespace {

constexpr int kMessageTextId = 0xFFFF;
constexpr int kWindowWidth = 560;

struct MessageContext {
    UiLanguage language = UiLanguage::Auto;
    SettingsMessageKind kind = SettingsMessageKind::SaveFailure;
    bool done = false;
    int result = IDCANCEL;
    ScopedGdiHandle<HFONT> font;
    FluentD2DThemeContext d2d;
};

struct MessageStrings {
    const wchar_t* title;
    const wchar_t* text;
};

MessageStrings GetMessageStrings(UiLanguage language, SettingsMessageKind kind) noexcept {
    const auto& str = GetUiStrings(language);
    switch (kind) {
        case SettingsMessageKind::PrivacyConfirmation:
            return {str.dlg_confirm_log_title, str.dlg_confirm_log_msg};
        case SettingsMessageKind::SaveFailure:
            return {str.dlg_title, str.dlg_save_failed};
        case SettingsMessageKind::NotificationFailure:
            return {str.dlg_title, str.dlg_notify_failed};
        default:
            QIQI_LOG_ERROR("Unknown Settings message kind");
            return {};
    }
}

void ApplyMessageLanguage(HWND hwnd, const MessageContext& ctx) noexcept {
    const auto strings = GetMessageStrings(ctx.language, ctx.kind);
    const auto& str = GetUiStrings(ctx.language);
    const bool confirmation = ctx.kind == SettingsMessageKind::PrivacyConfirmation;
    ::SetWindowTextW(hwnd, strings.title);
    ::SetDlgItemTextW(hwnd, kMessageTextId, strings.text);
    if (confirmation) {
        ::SetDlgItemTextW(hwnd, IDYES, str.dlg_btn_yes);
        ::SetDlgItemTextW(hwnd, IDNO, str.dlg_btn_no);
    } else {
        ::SetDlgItemTextW(hwnd, IDOK, str.dlg_btn_ok);
    }
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
        dark ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));
    const auto zh = GetMessageStrings(UiLanguage::ZhCn, ctx.kind);
    const auto en = GetMessageStrings(UiLanguage::EnUs, ctx.kind);
    const std::array<FluentCardSpec, 1> cards = {{
        {D2D1::RectF(ctx.d2d.ScaleF(18), ctx.d2d.ScaleF(16),
                     static_cast<float>(rect.right) - ctx.d2d.ScaleF(18),
                     static_cast<float>(rect.bottom) - ctx.d2d.ScaleF(58)),
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
                        IsSystemDarkTheme() ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            ::SetTextColor(dc, IsSystemDarkTheme() ? RGB(238, 240, 245) : RGB(28, 32, 40));
            ::SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(ctx->d2d.surface_brush.Get());
        }
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            ApplyMessageLanguage(hwnd, *ctx);
            return 0;
        case kSettingsLanguageChangedMessage:
            ctx->language = static_cast<UiLanguage>(wparam);
            ApplyMessageLanguage(hwnd, *ctx);
            return 0;
        case WM_KEYDOWN: {
            const bool confirmation = ctx->kind == SettingsMessageKind::PrivacyConfirmation;
            if (wparam == VK_ESCAPE) {
                CloseMessage(hwnd, *ctx, confirmation ? IDCANCEL : IDOK);
                return 0;
            }
            if (wparam == VK_TAB) {
                const int focused = ::GetDlgCtrlID(::GetFocus());
                ::SetFocus(::GetDlgItem(hwnd, confirmation ? (focused == IDYES ? IDNO : IDYES) : IDOK));
                return 0;
            }
            if (wparam == VK_RETURN) {
                const int focused = ::GetDlgCtrlID(::GetFocus());
                CloseMessage(hwnd, *ctx, confirmation ? (focused == IDYES ? IDYES : IDNO) : IDOK);
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
            ::SetFocus(::GetDlgItem(hwnd,
                ctx->kind == SettingsMessageKind::PrivacyConfirmation ? IDNO : IDOK));
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const bool confirmation = ctx->kind == SettingsMessageKind::PrivacyConfirmation;
            if ((confirmation && (id == IDYES || id == IDNO || id == IDCANCEL)) ||
                (!confirmation && (id == IDOK || id == IDCANCEL))) {
                CloseMessage(hwnd, *ctx, id);
            }
            return 0;
        }
        case WM_CLOSE:
            CloseMessage(hwnd, *ctx, ctx->kind == SettingsMessageKind::PrivacyConfirmation ? IDCANCEL : IDOK);
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
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MessageWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsMessageWndClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
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
    ctx->d2d.Init(monitor.dpi);
    const auto scale = [dpi = monitor.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    ctx->font = CreateUiFont(scale(14), FW_NORMAL, CLEARTYPE_QUALITY);
    if (!ctx->font || !ctx->d2d.d2d_target || !ctx->d2d.surface_brush ||
        !ctx->d2d.fmt_caption || !ctx->d2d.fmt_btn) {
        QIQI_LOG_ERROR("Settings message drawing resources unavailable");
        return {};
    }
    int body_height = scale(48);
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        const int measured = dialog_detail::MeasureDialogTextHeight(
            ctx->font.Get(), GetMessageStrings(language, kind).text, scale(kWindowWidth - 64));
        if (measured <= 0) {
            return {};
        }
        body_height = std::max(body_height, measured);
    }
    const int height = body_height + scale(124);
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD ex_style = WS_EX_DLGMODALFRAME;
    RECT rect{0, 0, scale(kWindowWidth), height};
    ::AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    const int width = rect.right - rect.left;
    const int window_height = rect.bottom - rect.top;
    const POINT centered{
        (monitor.work_area.left + monitor.work_area.right - width) / 2,
        (monitor.work_area.top + monitor.work_area.bottom - window_height) / 2
    };
    const POINT position = ClampPointToWorkArea(centered, width, window_height, monitor.work_area);
    ScopedWindow hwnd(::CreateWindowExW(ex_style, kSettingsMessageWndClass, strings.title, style,
        position.x, position.y, width, window_height, parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("Settings message creation failed (error={})", ::GetLastError());
        return {};
    }
    const auto* owned = ctx.release();
    const HFONT font = owned->font.Get();
    const auto* theme = &owned->d2d;
    const HWND body = CreateFluentChildControl(hwnd.Get(), instance, font, theme,
        L"STATIC", strings.text, SS_LEFT | SS_NOPREFIX, scale(32), scale(52),
        scale(kWindowWidth - 64), body_height, kMessageTextId);
    if (!body || !::IsWindow(hwnd.Get())) {
        QIQI_LOG_ERROR("Settings message body creation failed");
        return {};
    }
    const bool confirmation = kind == SettingsMessageKind::PrivacyConfirmation;
    const HWND primary = CreateFluentChildControl(hwnd.Get(), instance, font, theme,
        L"BUTTON", confirmation ? GetUiStrings(ui_language).dlg_btn_yes : GetUiStrings(ui_language).dlg_btn_ok,
        BS_OWNERDRAW | WS_TABSTOP, scale(confirmation ? 324 : 436), height - scale(42),
        scale(100), scale(32), confirmation ? IDYES : IDOK);
    if (!primary || !::IsWindow(hwnd.Get())) {
        QIQI_LOG_ERROR("Settings message button creation failed");
        return {};
    }
    const HWND negative = confirmation
        ? CreateFluentChildControl(hwnd.Get(), instance, font, theme,
            L"BUTTON", GetUiStrings(ui_language).dlg_btn_no, BS_OWNERDRAW | WS_TABSTOP,
            scale(436), height - scale(42), scale(100), scale(32), IDNO)
        : nullptr;
    if (!::IsWindow(hwnd.Get()) || (confirmation && !negative)) {
        QIQI_LOG_ERROR("Settings message control creation failed");
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
