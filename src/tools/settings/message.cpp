#include "tools/settings/window.h"
#include "common/d2d_controls.h"
#include "common/dialog_util.h"
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
constexpr UINT kUpdateToastLanguageMessage = WM_APP + 1;

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

struct ToastContext {
    UiLanguage language = UiLanguage::Auto;
    SettingsToastKind kind = SettingsToastKind::SaveFailure;
    FluentD2DThemeContext d2d;
    bool holds_module_reference = false;
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
        case SettingsMessageKind::NotificationFailure:
            return {str.dlg_title, str.dlg_notify_failed};
        default:
            QIQI_LOG_ERROR("Unknown Settings message kind");
            return {};
    }
}

const wchar_t* GetToastText(UiLanguage language, SettingsToastKind kind) noexcept {
    const auto& str = GetUiStrings(language);
    switch (kind) {
        case SettingsToastKind::SaveFailure:
            return str.quick_settings_save_failed;
        case SettingsToastKind::NotificationFailure:
            return str.quick_settings_notify_failed;
        default:
            QIQI_LOG_ERROR("Unknown Settings toast kind");
            return nullptr;
    }
}

void ApplyToastLanguage(HWND hwnd, ToastContext& ctx, UiLanguage language) noexcept {
    ctx.language = language;
    ::SetWindowTextW(hwnd, GetToastText(ctx.language, ctx.kind));
    ::InvalidateRect(hwnd, nullptr, TRUE);
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

void PaintToast(HDC dc, const RECT& rect, const ToastContext& ctx) noexcept {
    if (!ctx.d2d.d2d_target) {
        return;
    }
    const ScopedD2DDcDraw draw(ctx.d2d.d2d_target.Get(), dc, rect);
    if (!draw) {
        return;
    }
    const bool dark = IsSystemDarkTheme();
    const auto scale = [&ctx](int px) noexcept { return ctx.d2d.ScaleF(px); };
    ctx.d2d.d2d_target->Clear(dark ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));
    const std::array<FluentCardSpec, 1> cards = {{
        {D2D1::RectF(scale(8), scale(8), static_cast<float>(rect.right) - scale(8),
                     static_cast<float>(rect.bottom) - scale(8)), nullptr, nullptr}
    }};
    DrawFluentCardGroup(ctx.d2d, cards, false, dark);
    const auto text_brush = CreateSolidBrush(
        ctx.d2d.d2d_target.Get(),
        dark ? MakeD2DColor(238, 240, 245) : MakeD2DColor(28, 32, 40));
    const wchar_t* const text = GetToastText(ctx.language, ctx.kind);
    if (ctx.d2d.fmt_body && text_brush && text) {
        ctx.d2d.d2d_target->DrawText(
            text,
            gsl::narrow_cast<UINT32>(std::wcslen(text)),
            ctx.d2d.fmt_body.Get(),
            D2D1::RectF(scale(34), scale(18), static_cast<float>(rect.right) - scale(24),
                        static_cast<float>(rect.bottom) - scale(18)),
            text_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<ToastContext>(hwnd, message, lparam);
    if (!ctx || message == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [ctx](HDC dc, const RECT& rect) noexcept {
                PaintToast(dc, rect, *ctx);
            });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintToast(reinterpret_cast<HDC>(wparam), rect, *ctx);
            return 0;
        }
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            ctx->d2d.RefreshThemeBrushes();
            ::SetWindowTextW(hwnd, GetToastText(ctx->language, ctx->kind));
            ::InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case kUpdateToastLanguageMessage:
            ApplyToastLanguage(hwnd, *ctx, static_cast<UiLanguage>(wparam));
            return 0;
        case WM_TIMER:
            if (wparam == 1) {
                ::KillTimer(hwnd, 1);
                ::DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_NCDESTROY: {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            const std::unique_ptr<ToastContext> owned(ctx);
            if (owned->holds_module_reference) {
                --g_com_object_count;
            }
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
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
    ctx->d2d.Init(monitor.dpi);
    const auto scale = [dpi = monitor.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    ctx->font = CreateUiFont(scale(14), FW_NORMAL, CLEARTYPE_QUALITY);
    ctx->icon_small = dialog_detail::LoadOrCreateBrandIcon(scale(16), instance);
    ctx->icon_big = dialog_detail::LoadOrCreateBrandIcon(scale(48), instance);
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
        L"STATIC", strings.text, SS_LEFT | SS_NOPREFIX, scale(32), scale(52),
        scale(kWindowWidth - 64), body_height, kMessageTextId);
    if (!body || !::IsWindow(hwnd.Get())) {
        QIQI_LOG_ERROR("Settings message body creation failed");
        return {};
    }
    const auto* primary = CreateFluentChildControl(hwnd.Get(), instance, font, theme,
        L"BUTTON", GetUiStrings(ui_language).dlg_btn_ok,
        BS_OWNERDRAW | WS_TABSTOP, scale(436), height - scale(42),
        scale(100), scale(32), IDOK);
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

void ShowSettingsToast(UiLanguage ui_language, SettingsToastKind kind) noexcept {
    const wchar_t* const text = GetToastText(ui_language, kind);
    if (!text) {
        return;
    }
    const HINSTANCE instance = g_dll_instance ? g_dll_instance : ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ToastWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kSettingsToastWndClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        QIQI_LOG_ERROR("Settings toast window registration failed (error={})", ::GetLastError());
        return;
    }

    std::unique_ptr<ToastContext> ctx;
    try {
        ctx = std::make_unique<ToastContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings toast context allocation failed");
        return;
    }
    const MonitorMetrics monitor = QueryActiveMonitor();
    ctx->language = ui_language;
    ctx->kind = kind;
    ctx->d2d.Init(monitor.dpi);
    if (!ctx->d2d.d2d_target || !ctx->d2d.fmt_body) {
        QIQI_LOG_ERROR("Settings toast requires Direct2D and DirectWrite");
        return;
    }
    std::ignore = log::CheckHResult(ctx->d2d.fmt_body->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),
                                   "SettingsToast::SetWordWrapping");
    std::ignore = log::CheckHResult(ctx->d2d.fmt_body->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING),
                                   "SettingsToast::SetTextAlignment");
    std::ignore = log::CheckHResult(ctx->d2d.fmt_body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                                   "SettingsToast::SetParagraphAlignment");

    const int width = ScalePxByDpi(440, monitor.dpi);
    const int height = ScalePxByDpi(88, monitor.dpi);
    const POINT position = ClampPointToWorkArea(
        POINT{monitor.work_area.right - width - ScalePxByDpi(20, monitor.dpi),
              monitor.work_area.bottom - height - ScalePxByDpi(20, monitor.dpi)},
        width, height, monitor.work_area);
    auto* const owned = ctx.get();
    ScopedWindow hwnd(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kSettingsToastWndClass,
        text,
        WS_POPUP,
        position.x, position.y, width, height,
        nullptr, nullptr, instance, owned));
    if (!hwnd) {
        QIQI_LOG_ERROR("Settings toast window creation failed (error={})", ::GetLastError());
        return;
    }
    std::ignore = ctx.release();
    if (!::SetTimer(hwnd.Get(), 1, 3000, nullptr)) {
        QIQI_LOG_ERROR("Settings toast timer creation failed (error={})", ::GetLastError());
        return;
    }
    owned->holds_module_reference = true;
    ++g_com_object_count;
    const HWND raw_hwnd = hwnd.Detach();
    ::ShowWindow(raw_hwnd, SW_SHOWNOACTIVATE);
    ::UpdateWindow(raw_hwnd);
}

void UpdateSettingsToastLanguage(UiLanguage ui_language) noexcept {
    const auto callback = [](HWND hwnd, LPARAM language_param) noexcept -> BOOL {
        std::array<wchar_t, 128> class_name{};
        if (::GetClassNameW(hwnd, class_name.data(), gsl::narrow_cast<int>(class_name.size())) > 0 &&
            std::wcscmp(class_name.data(), kSettingsToastWndClass) == 0) {
            ::SendMessageW(hwnd, kUpdateToastLanguageMessage, language_param, 0);
        }
        return TRUE;
    };
    if (!::EnumThreadWindows(::GetCurrentThreadId(), callback, static_cast<LPARAM>(ui_language))) {
        QIQI_LOG_WARN("Enumerating Settings toast windows for language update failed (error={})",
                      ::GetLastError());
    }
}

} // namespace qiqi
