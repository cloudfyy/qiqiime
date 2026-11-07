#include "common/about_dialog.h"
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
#include <cwchar>

namespace qiqi {
namespace {

constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

enum class InfoDialogKind : uint8_t {
    About = 0,
    Help = 1
};

struct AboutDialogContext {
    InfoDialogKind kind = InfoDialogKind::About;
    UiLanguage ui_language = UiLanguage::Auto;
    bool done = false;
    int license_bottom = 198;
    int button_top = 208;
    ScopedGdiHandle<HFONT> body_font;
    ScopedGdiHandle<HFONT> title_font;
    ScopedIcon icon_small;
    ScopedIcon icon;
    FluentD2DThemeContext d2d;
};

void ApplyAboutLanguage(HWND hwnd, const AboutDialogContext& ctx) noexcept {
    const auto& str = GetUiStrings(ctx.ui_language);
    ::SetWindowTextW(hwnd, ctx.kind == InfoDialogKind::Help ? str.help_title : str.about_title);
    if (ctx.kind == InfoDialogKind::About) {
        ::SetDlgItemTextW(hwnd, kIdBtnOpenGitHub, str.about_btn_github);
    }
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void PaintAboutDialog(HDC hdc, const RECT& client_rc, const AboutDialogContext& ctx) noexcept {
    const ScopedSaveDC save_dc(hdc);
    const bool is_dark = IsSystemDarkTheme();
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScalePx(px); };
    const auto& str = GetUiStrings(ctx.ui_language);

    const ScopedGdiHandle<HBRUSH> bg_brush(
        ::CreateSolidBrush(is_dark ? RGB(32, 34, 38) : RGB(248, 250, 253)));
    ::FillRect(hdc, &client_rc, bg_brush.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    if (ctx.icon) {
        ::DrawIconEx(hdc, S(24), S(20), ctx.icon.Get(), S(56), S(56), 0, nullptr, DI_NORMAL);
    }
    {
        const ScopedSelectObject sel_title_font(hdc, ctx.title_font.Get());
        ::SetTextColor(hdc, is_dark ? RGB(238, 240, 245) : RGB(24, 30, 42));
        RECT title_rc{S(94), S(22), client_rc.right - S(20), S(50)};
        ::DrawTextW(
            hdc,
            ctx.kind == InfoDialogKind::Help ? str.help_header_title : str.about_app_name,
            -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const ScopedSelectObject sel_body_font(hdc, ctx.body_font.Get());
        ::SetTextColor(hdc, is_dark ? RGB(168, 176, 190) : RGB(85, 95, 110));
        RECT ver_rc{S(94), S(52), client_rc.right - S(20), S(76)};
        if (ctx.kind == InfoDialogKind::Help) {
            ::DrawTextW(hdc, str.help_header_subtitle, -1, &ver_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            std::array<wchar_t, 256> ver_text{};
            const int version_length = ::swprintf_s(
                ver_text.data(), ver_text.size(), L"%ls%ls (%ls)",
                str.about_version_prefix, version::kFullVersionW.data(), version::kArchW.data());
            if (version_length < 0) {
                QIQI_LOG_ERROR("About version text formatting failed");
                return;
            }
            ::DrawTextW(hdc, ver_text.data(), version_length, &ver_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        const ScopedGdiHandle<HPEN> div_pen(
            ::CreatePen(PS_SOLID, 1, is_dark ? RGB(64, 68, 78) : RGB(220, 225, 232)));
        const ScopedSelectObject sel_div(hdc, div_pen.Get());
        ::MoveToEx(hdc, S(24), S(92), nullptr);
        ::LineTo(hdc, client_rc.right - S(24), S(92));

        ::SetTextColor(hdc, is_dark ? RGB(215, 220, 228) : RGB(45, 52, 64));
        RECT lic_rc{S(24), S(104), client_rc.right - S(24), ctx.license_bottom};
        ::DrawTextW(
            hdc,
            ctx.kind == InfoDialogKind::Help ? str.help_shortcuts_info : str.about_license_info,
            -1, &lic_rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<AboutDialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [ctx](HDC hdc, const RECT& rc) { PaintAboutDialog(hdc, rc, *ctx); });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintAboutDialog(reinterpret_cast<HDC>(wparam), rect, *ctx);
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
                        is_dark ? MakeD2DColor(32, 34, 38) : MakeD2DColor(248, 250, 253));
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            ApplyAboutLanguage(hwnd, *ctx);
            return 0;
        case kSettingsLanguageChangedMessage:
            ctx->ui_language = static_cast<UiLanguage>(wparam);
            ApplyAboutLanguage(hwnd, *ctx);
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
                const std::unique_ptr<AboutDialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

ScopedWindow CreateInfoWindow(
    InfoDialogKind kind, HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    if (!dialog_detail::RegisterDialogWindowClass(
            instance, kAboutWndClass, &AboutWndProc,
            CS_HREDRAW | CS_VREDRAW, nullptr, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1))) {
        QIQI_LOG_ERROR("About/Help dialog class registration failed (error={})", ::GetLastError());
        return {};
    }

    std::unique_ptr<AboutDialogContext> ctx;
    try {
        ctx = std::make_unique<AboutDialogContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("About/Help dialog context allocation failed");
        return {};
    }
    ctx->kind = kind;
    ctx->ui_language = ui_language;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    ctx->d2d.Init(mon.dpi);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->body_font = CreateUiFont(S(14), FW_NORMAL);
    ctx->title_font = CreateUiFont(S(19), FW_BOLD);
    const auto load_icon = [&](int size) noexcept {
        return use_embedded_icon ? LoadAppIconOrBrandFallback(instance, S(size), 1, true) :
            dialog_detail::LoadOrCreateBrandIcon(S(size), instance);
    };
    ctx->icon_small = load_icon(16);
    ctx->icon = load_icon(56);

    const int client_width_logical = (kind == InfoDialogKind::Help) ? 530 : 440;
    const int text_width_px = S(client_width_logical - 48);
    int body_height = S(94);
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
    ctx->license_bottom = S(104) + body_height;
    ctx->button_top = ctx->license_bottom + S(12);

    const auto& str = GetUiStrings(ui_language);
    constexpr DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr DWORD wnd_ex_style = 0;
    RECT bounds = ComputeCenteredWindowBounds(
        S(client_width_logical), ctx->button_top + S(46), wnd_style, wnd_ex_style, mon.work_area);
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
        kAboutWndClass,
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

    const int button_top = owned_ctx->button_top;
    if (kind == InfoDialogKind::About) {
        CreateFluentChildControl(
            hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.about_btn_github,
            BS_OWNERDRAW | WS_TABSTOP, S(24), button_top, S(124), S(32), kIdBtnOpenGitHub);
    }
    const int ok_x = S(client_width_logical - 24 - 98);
    CreateFluentChildControl(
        hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.dlg_btn_ok,
        BS_OWNERDRAW | WS_TABSTOP, ok_x, button_top, S(98), S(32), kIdBtnOk);
    return hwnd;
}

} // namespace

ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    return CreateInfoWindow(InfoDialogKind::About, instance, parent, ui_language, use_embedded_icon);
}

bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    const ScopedWindow hwnd(CreateAboutWindow(instance, parent, ui_language, use_embedded_icon));
    const auto* ctx = reinterpret_cast<const AboutDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

ScopedWindow CreateHelpWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    return CreateInfoWindow(InfoDialogKind::Help, instance, parent, ui_language, use_embedded_icon);
}

bool ShowHelpDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language, bool use_embedded_icon) noexcept {
    const ScopedWindow hwnd(CreateHelpWindow(instance, parent, ui_language, use_embedded_icon));
    const auto* ctx = reinterpret_cast<const AboutDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

} // namespace qiqi
