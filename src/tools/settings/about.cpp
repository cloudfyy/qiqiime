#include "tools/settings/window.h"
#include "common/d2d_controls.h"
#include "tools/settings/util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "common/version.h"

#include <shellapi.h>
#include <memory>
#include <new>
#include <string>

namespace qiqi {
namespace {

constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

struct AboutDialogContext {
    UiLanguage ui_language = UiLanguage::Auto;
    bool done = false;
    ScopedGdiHandle<HFONT> body_font;
    ScopedGdiHandle<HFONT> title_font;
    ScopedIcon icon;
    FluentD2DThemeContext d2d;
};

void PaintAboutDialog(HDC hdc, const RECT& client_rc, const AboutDialogContext& ctx) {
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
        ::DrawTextW(hdc, str.about_app_name, -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const ScopedSelectObject sel_body_font(hdc, ctx.body_font.Get());
        ::SetTextColor(hdc, is_dark ? RGB(168, 176, 190) : RGB(85, 95, 110));
        const std::wstring ver_text = std::wstring(str.about_version_prefix) +
            std::wstring(version::kFullVersionW) + L" (" + std::wstring(version::kArchW) + L")";
        RECT ver_rc{S(94), S(52), client_rc.right - S(20), S(76)};
        ::DrawTextW(hdc, ver_text.c_str(), -1, &ver_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const ScopedGdiHandle<HPEN> div_pen(
            ::CreatePen(PS_SOLID, 1, is_dark ? RGB(64, 68, 78) : RGB(220, 225, 232)));
        const ScopedSelectObject sel_div(hdc, div_pen.Get());
        ::MoveToEx(hdc, S(24), S(92), nullptr);
        ::LineTo(hdc, client_rc.right - S(24), S(92));

        ::SetTextColor(hdc, is_dark ? RGB(215, 220, 228) : RGB(45, 52, 64));
        RECT lic_rc{S(24), S(104), client_rc.right - S(24), S(198)};
        ::DrawTextW(hdc, str.about_license_info, -1, &lic_rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
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
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            ::InvalidateRect(hwnd, nullptr, TRUE);
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

} // namespace

ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AboutWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kAboutWndClass;
    ::RegisterClassExW(&wc);

    std::unique_ptr<AboutDialogContext> ctx;
    try {
        ctx = std::make_unique<AboutDialogContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("About dialog context allocation failed");
        return {};
    }
    ctx->ui_language = ui_language;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    ctx->d2d.Init(mon.dpi);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->body_font = CreateUiFont(S(14), FW_NORMAL);
    ctx->title_font = CreateUiFont(S(19), FW_BOLD);
    ctx->icon = dialog_detail::LoadOrCreateBrandIcon(S(56));

    const auto& str = GetUiStrings(ui_language);
    const DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD wnd_ex_style = 0;
    RECT desired_rc{0, 0, S(440), S(254)};
    ::AdjustWindowRectEx(&desired_rc, wnd_style, FALSE, wnd_ex_style);
    const int dlg_w = desired_rc.right - desired_rc.left;
    const int dlg_h = desired_rc.bottom - desired_rc.top;
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->body_font.Get();
    ScopedWindow hwnd(::CreateWindowExW(
        wnd_ex_style,
        kAboutWndClass,
        str.about_title,
        wnd_style,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateAboutWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), IsSystemDarkTheme());
    QIQI_LOG_DEBUG("AboutWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   dlg_pos.x, dlg_pos.y, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (owned_ctx->icon) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(owned_ctx->icon.Get()));
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(owned_ctx->icon.Get()));
    }

    CreateFluentChildControl(hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.about_btn_github, BS_OWNERDRAW | WS_TABSTOP, S(24), S(208), S(124), S(32), kIdBtnOpenGitHub);
    CreateFluentChildControl(hwnd.Get(), instance, f, &owned_ctx->d2d, L"BUTTON", str.dlg_btn_ok, BS_OWNERDRAW | WS_TABSTOP, S(318), S(208), S(98), S(32), kIdBtnOk);
    return hwnd;
}

bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language) noexcept {
    const ScopedWindow hwnd(CreateAboutWindow(instance, parent, ui_language));
    const auto* ctx = reinterpret_cast<const AboutDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

} // namespace qiqi
