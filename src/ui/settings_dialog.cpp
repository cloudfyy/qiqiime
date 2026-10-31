#include "ui/settings_dialog_internal.h"
#include "ui/dialog_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <memory>
#include <new>

namespace qiqi {

using namespace settings_detail;

namespace {

void HandleSettingsCommand(HWND hwnd, WPARAM wparam, DialogContext& ctx) {
    switch (LOWORD(wparam)) {
        case kIdRadioJidian:
        case kIdRadioHaifeng:
        case kIdRadioBuiltin:
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdComboUiLang:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                ApplyLiveSettingsLanguage(hwnd, ctx);
            }
            return;
        case kIdChkLogKeystrokes:
            if (HIWORD(wparam) == BN_CLICKED && ::IsDlgButtonChecked(hwnd, kIdChkLogKeystrokes) == BST_CHECKED) {
                const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
                const auto& str = GetUiStrings(lang);
                const int choice = ::MessageBoxW(
                    hwnd,
                    str.dlg_confirm_log_msg,
                    str.dlg_confirm_log_title,
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (choice != IDYES) {
                    ::CheckDlgButton(hwnd, kIdChkLogKeystrokes, BST_UNCHECKED);
                }
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnOk:
            if (ctx.config) {
                ReadSettingsFromWindowControls(hwnd, *ctx.config);
            }
            ctx.accepted = true;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnCancel:
            ctx.accepted = false;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnAbout: {
            const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
            ShowAboutDialog(g_dll_instance, hwnd, lang);
            return;
        }
        default:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<DialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [hwnd, ctx](HDC hdc, const RECT& rc) {
                PaintFluentSettingsWindow(hwnd, hdc, rc, *ctx);
            });
        case WM_MEASUREITEM: {
            if (lparam != 0) {
                auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (mis->CtlType == ODT_COMBOBOX) {
                    mis->itemHeight = gsl::narrow_cast<UINT>(ctx->d2d.ScalePx(22));
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_DRAWITEM: {
            if (lparam != 0) {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (dis->CtlType == ODT_BUTTON) {
                    if (dis->CtlID == kIdBtnAbout || dis->CtlID == kIdBtnOk || dis->CtlID == kIdBtnCancel) {
                        DrawFluentPushButton(
                            *dis, ctx->d2d, dis->CtlID == kIdBtnOk,
                            IsSystemDarkTheme() ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));
                    } else {
                        const bool is_radio = dis->CtlID >= kIdRadioJidian && dis->CtlID <= kIdRadioBuiltin;
                        DrawFluentToggleOrRadioRow(*dis, ctx->d2d, is_radio);
                    }
                    return TRUE;
                }
                if (dis->CtlType == ODT_COMBOBOX) {
                    DrawFluentComboBoxItem(*dis, ctx->d2d);
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            const bool is_dark = IsSystemDarkTheme();
            ::SetTextColor(hdc, is_dark ? RGB(238, 240, 245) : RGB(28, 32, 40));
            ::SetBkColor(hdc, is_dark ? RGB(54, 58, 68) : RGB(255, 255, 255));
            if (ctx->d2d.input_brush) {
                return reinterpret_cast<LRESULT>(ctx->d2d.input_brush.Get());
            }
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const HWND ctrl = reinterpret_cast<HWND>(lparam);
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            const bool is_dark = IsSystemDarkTheme();
            if (ctrl && ::GetDlgCtrlID(ctrl) == kIdLblPrivacyNotice) {
                ::SetTextColor(hdc, is_dark ? RGB(255, 128, 114) : RGB(196, 43, 28));
            } else {
                ::SetTextColor(hdc, is_dark ? RGB(238, 240, 245) : RGB(28, 32, 40));
            }
            ::SetBkMode(hdc, TRANSPARENT);
            if (ctx->d2d.surface_brush) {
                return reinterpret_cast<LRESULT>(ctx->d2d.surface_brush.Get());
            }
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            const bool is_dark = IsSystemDarkTheme();
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, is_dark);
            ApplyLiveSettingsLanguage(hwnd, *ctx);
            return 0;
        }
        case WM_COMMAND:
            HandleSettingsCommand(hwnd, wparam, *ctx);
            return 0;
        case WM_CLOSE:
            ctx->accepted = false;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                const std::unique_ptr<DialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace

ScopedWindow CreateSettingsWindow(HINSTANCE instance, HWND parent, QiQiConfig* bound_config) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWndClass;
    ::RegisterClassExW(&wc);

    std::unique_ptr<DialogContext> ctx;
    try {
        ctx = std::make_unique<DialogContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings dialog context allocation failed");
        return {};
    }
    ctx->config = bound_config;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->d2d.Init(mon.dpi);
    const bool is_dark = IsSystemDarkTheme();
    ctx->font = CreateUiFont(S(15), FW_NORMAL, CLEARTYPE_QUALITY);
    ctx->bold_font = CreateUiFont(S(12), FW_SEMIBOLD, CLEARTYPE_QUALITY);
    ctx->icon_small = dialog_detail::LoadOrCreateBrandIcon(S(16));
    ctx->icon_big = dialog_detail::LoadOrCreateBrandIcon(S(48));

    const auto& str = GetUiStrings(bound_config ? bound_config->ui_language : UiLanguage::Auto);

    const DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const DWORD wnd_ex_style = WS_EX_APPWINDOW;
    RECT desired_rc{0, 0, S(820), S(444)};
    ::AdjustWindowRectEx(&desired_rc, wnd_style, FALSE, wnd_ex_style);
    const int dlg_w = desired_rc.right - desired_rc.left;
    const int dlg_h = desired_rc.bottom - desired_rc.top;
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    ScopedWindow hwnd(::CreateWindowExW(
        wnd_ex_style,
        kSettingsWndClass,
        str.dlg_title,
        wnd_style,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateSettingsWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), is_dark);
    QIQI_LOG_DEBUG("SettingsWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   dlg_pos.x, dlg_pos.y, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (owned_ctx->icon_small) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(owned_ctx->icon_small.Get()));
    }
    if (owned_ctx->icon_big) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(owned_ctx->icon_big.Get()));
    }

    CreateSettingsControls(hwnd.Get(), instance, *owned_ctx);
    if (bound_config) {
        WriteSettingsToWindowControls(hwnd.Get(), *bound_config);
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), is_dark);
    return hwnd;
}

bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept {
    const ScopedWindow hwnd(CreateSettingsWindow(instance, parent, &in_out_config));
    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    if (!hwnd || !ctx) {
        return false;
    }
    return dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) && ctx->accepted;
}

} // namespace qiqi
