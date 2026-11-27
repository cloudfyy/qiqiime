// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/toolbar.h"
#include "tools/settings/transfer.h"
#include "common/dlgutil.h"
#include "common/dictui.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/engine/dict.h"

#include "common/configclient.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <gsl/gsl>
#include <memory>
#include <new>
#include <utility>

namespace qiqi::settings_detail {
namespace {

void DestroySettingsControls(HWND hwnd) noexcept {
    ::EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM) noexcept -> BOOL {
            ::DestroyWindow(child);
            return TRUE;
        },
        0);
}

void UpdateSettingsWindowForMonitor(HWND hwnd, DialogContext& ctx, UINT dpi, const RECT* suggested_rect) noexcept {
    if (ctx.applying_draft) return;
    const UINT effective_dpi = dpi > 0 ? dpi : 96u;
    RECT current_rect{};
    if (!::GetWindowRect(hwnd, &current_rect)) {
        QIQI_LOG_ERROR("Settings DPI update could not query window bounds (error={})", ::GetLastError());
        return;
    }

    RECT target_rect = current_rect;
    if (suggested_rect && suggested_rect->right > suggested_rect->left &&
        suggested_rect->bottom > suggested_rect->top) {
        target_rect = *suggested_rect;
    } else if (ctx.d2d.dpi != effective_dpi) {
        RECT desired_rect{0, 0, ScalePxByDpi(settings_layout::kClientSize.cx, effective_dpi),
                         ScalePxByDpi(settings_layout::kClientSize.cy, effective_dpi)};
        const DWORD style = gsl::narrow_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD ex_style = gsl::narrow_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (!::AdjustWindowRectEx(&desired_rect, style, FALSE, ex_style)) {
            QIQI_LOG_ERROR("Settings DPI update could not calculate window frame (error={})", ::GetLastError());
            return;
        }
        target_rect.right = target_rect.left + desired_rect.right - desired_rect.left;
        target_rect.bottom = target_rect.top + desired_rect.bottom - desired_rect.top;
    }

    const POINT center{
        target_rect.left + (target_rect.right - target_rect.left) / 2,
        target_rect.top + (target_rect.bottom - target_rect.top) / 2
    };
    const MonitorMetrics monitor = QueryMonitorForPoint(center, hwnd);
    const int width = std::min(target_rect.right - target_rect.left, monitor.work_area.right - monitor.work_area.left);
    const int height = std::min(target_rect.bottom - target_rect.top, monitor.work_area.bottom - monitor.work_area.top);
    const POINT position = ClampPointToWorkArea(
        POINT{target_rect.left, target_rect.top}, width, height, monitor.work_area);
    target_rect.left = position.x;
    target_rect.top = position.y;
    target_rect.right = position.x + width;
    target_rect.bottom = position.y + height;

    const bool dpi_changed = ctx.d2d.dpi != effective_dpi;
    QiQiConfig current_config;
    int focused_control_id = 0;
    if (dpi_changed) {
        if (ctx.controls_initialized) {
            const HWND focused = ::GetFocus();
            if (focused && ::IsChild(hwnd, focused)) {
                focused_control_id = ::GetDlgCtrlID(focused);
            }
            if (!log::CheckHResult(CaptureSettingsDraft(hwnd, ctx, current_config), "Settings::ReadDraftForDpiChange"))
                return;
        }
        const auto& appearance_config = ctx.controls_initialized ? current_config : ctx.baseline_config;
        if (!RefreshSettingsUiAppearance(
                hwnd, ctx, appearance_config.ui_language, GetDisplayPreferences(appearance_config), effective_dpi)) {
            return;
        }
        if (ctx.controls_initialized) {
            DestroySettingsControls(hwnd);
        }

        ctx.icon_small = dialog_detail::LoadOrCreateBrandIcon(ctx.d2d.ScalePx(kSmallBrandIconSize), ctx.instance);
        ctx.icon_big = dialog_detail::LoadOrCreateBrandIcon(ctx.d2d.ScalePx(kLargeBrandIconSize), ctx.instance);
        if (!ctx.icon_small || !ctx.icon_big) {
            QIQI_LOG_ERROR("Settings DPI update could not recreate scaled window resources");
        }
        dialog_detail::ApplyDialogIcons(hwnd, ctx.icon_small.Get(), ctx.icon_big.Get());
    }

    if (!::SetWindowPos(
            hwnd,
            nullptr,
            target_rect.left,
            target_rect.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE)) {
        QIQI_LOG_ERROR("Settings DPI update could not move window (error={})", ::GetLastError());
    }

    if (dpi_changed && ctx.controls_initialized) {
        CreateSettingsControls(hwnd, ctx.instance, ctx);
        std::ignore = ReapplySettingsDraft(hwnd, ctx, std::move(current_config));
        if (focused_control_id != 0) {
            if (const HWND focused_control = ::GetDlgItem(hwnd, focused_control_id)) {
                ::SetFocus(focused_control);
            }
        }
    }

    LayoutSettingsControls(hwnd, ctx);
    ::InvalidateRect(hwnd, nullptr, TRUE);
    ::UpdateWindow(hwnd);
}

} // namespace

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<DialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (ctx->active_font_menu) {
        if (msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE || msg == WM_DPICHANGED) {
            if (!::EndMenu()) {
                QIQI_LOG_WARN("Candidate font menu dismissal failed (error={})", ::GetLastError());
            }
            if (reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)) != ctx) return 0;
        } else if (msg == WM_DRAWITEM || msg == WM_MEASUREITEM || msg == WM_MENUCHAR) {
            try {
                if (const auto handled = ctx->active_font_menu->HandleMessage(
                        hwnd, msg, wparam, lparam, ctx->d2d)) {
                    return *handled;
                }
            } catch (const std::bad_alloc&) {
                QIQI_LOG_WARN("Candidate font menu message allocation failed");
                return 0;
            }
        }
    }
    switch (msg) {
        case WM_GETMINMAXINFO:
            if (lparam != 0) {
                auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
                info->ptMinTrackSize = POINT{ctx->d2d.ScaleDipPx(320), ctx->d2d.ScaleDipPx(280)};
            }
            return 0;
        case WM_NOTIFY:
            if (lparam != 0) {
                const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
                if (notification->idFrom == kIdSettingsTabs && notification->code == TCN_SELCHANGE) {
                    const LRESULT selection = ::SendDlgItemMessageW(hwnd, kIdSettingsTabs, TCM_GETCURSEL, 0, 0);
                    if (selection >= 0 && selection < std::to_underlying(SettingsPage::Count)) {
                        SelectSettingsPage(hwnd, *ctx, SettingsPage{gsl::narrow_cast<int>(selection)});
                    }
                    return 0;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case kSettingsCyclePage: {
            constexpr int count = std::to_underlying(SettingsPage::Count);
            const int next = (std::to_underlying(ctx->active_page) + (wparam ? count - 1 : 1)) % count;
            SelectSettingsPage(hwnd, *ctx, SettingsPage{next});
            return 0;
        }
        case WM_SIZE:
            LayoutSettingsControls(hwnd, *ctx);
            return 0;
        case kSettingsEnsureFocusVisible:
            EnsureSettingsControlVisible(hwnd, reinterpret_cast<HWND>(wparam), *ctx);
            return 0;
        case WM_MOUSEWHEEL:
            ctx->scroll_offset = std::clamp(
                ctx->scroll_offset - GET_WHEEL_DELTA_WPARAM(wparam) * ctx->d2d.ScalePx(48) / WHEEL_DELTA,
                0, std::max(0, ctx->content_height - ctx->viewport_height));
            LayoutSettingsControls(hwnd, *ctx);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            if (!::GetScrollInfo(::GetDlgItem(hwnd, kIdPageScroll), SB_CTL, &info)) {
                QIQI_LOG_WARN("Settings page scroll query failed (error={})", ::GetLastError());
                return 0;
            }
            int position = ctx->scroll_offset;
            switch (LOWORD(wparam)) {
                case SB_LINEUP: position -= ctx->d2d.ScalePx(44); break;
                case SB_LINEDOWN: position += ctx->d2d.ScalePx(44); break;
                case SB_PAGEUP: position -= ctx->viewport_height; break;
                case SB_PAGEDOWN: position += ctx->viewport_height; break;
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = ctx->content_height; break;
                case SB_THUMBTRACK: position = info.nTrackPos; break;
                default: return 0;
            }
            ctx->scroll_offset = std::clamp(position, 0, std::max(0, ctx->content_height - ctx->viewport_height));
            LayoutSettingsControls(hwnd, *ctx);
            return 0;
        }
        case WM_DPICHANGED:
            UpdateSettingsWindowForMonitor(
                hwnd, *ctx, HIWORD(wparam), reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE: {
            RECT rect{};
            if (::GetWindowRect(hwnd, &rect)) {
                const POINT center{
                    rect.left + (rect.right - rect.left) / 2,
                    rect.top + (rect.bottom - rect.top) / 2
                };
                const MonitorMetrics monitor = QueryMonitorForPoint(center, hwnd);
                UpdateSettingsWindowForMonitor(hwnd, *ctx, monitor.dpi, nullptr);
            }
            return 0;
        }
        case WM_PAINT:
            return PaintWindowClient(hwnd, [hwnd, ctx](HDC hdc, const RECT& rc) {
                PaintFluentSettingsWindow(hwnd, hdc, rc, *ctx);
            });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintFluentSettingsWindow(hwnd, reinterpret_cast<HDC>(wparam), rect, *ctx);
            return 0;
        }
        case WM_MEASUREITEM: {
            if (lparam != 0) {
                auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (mis->CtlType == ODT_COMBOBOX) {
                    mis->itemHeight = gsl::narrow_cast<UINT>(ctx->d2d.ScalePx(44));
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_DRAWITEM: {
            if (lparam != 0) {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                const ScopedSaveDC saved(dis->hDC);
                if (!saved) {
                    QIQI_LOG_WARN("Settings owner-draw DC save failed");
                    return TRUE;
                }
                if (!IsSettingsFooterControl(gsl::narrow_cast<int>(dis->CtlID)) && dis->CtlType != ODT_COMBOBOX) {
                    RECT bounds{};
                    if (!::GetWindowRect(dis->hwndItem, &bounds)) {
                        QIQI_LOG_WARN("Settings owner-draw bounds query failed (error={})", ::GetLastError());
                        return TRUE;
                    }
                    ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds), 2);
                    const int clip = ::IntersectClipRect(dis->hDC,
                        ctx->page_bounds.left - bounds.left, ctx->page_bounds.top - bounds.top,
                        ctx->page_bounds.right - bounds.left, ctx->page_bounds.bottom - bounds.top);
                    if (clip == ERROR) {
                        QIQI_LOG_WARN("Settings owner-draw clipping failed");
                        return TRUE;
                    }
                    if (clip == NULLREGION) return TRUE;
                }
                if (dis->CtlID == kIdCandidatePreview) {
                    DrawSettingsCandidatePreview(*dis, *ctx);
                    return TRUE;
                }
                if (dis->CtlType == ODT_STATIC) {
                    const HBRUSH background = ApplyFluentControlColors(dis->hDC, ctx->d2d, false);
                    if (!background) {
                        QIQI_LOG_WARN("Settings label background brush unavailable");
                        return TRUE;
                    }
                    ::FillRect(dis->hDC, &dis->rcItem, background);
                    if (dis->CtlID == kIdAboutIcon) {
                        dialog_detail::DrawCenteredDialogIcon(dis->hDC, ctx->icon_big.Get(), dis->rcItem);
                        return TRUE;
                    }
                    const bool title =
                        dis->CtlID == kIdLblAboutTitle ||
                        dis->CtlID == kIdLblShortcutsTitle;
                    const bool centered =
                        dis->CtlID == kIdLblShortcutsTitle ||
                        dis->CtlID == kIdLblShortcutsSubtitle;
                    const bool secondary =
                        dis->CtlID == kIdLblAboutVersion ||
                        dis->CtlID == kIdLblShortcutsSubtitle;
                    std::array<wchar_t, 4096> text{};
                    const int length = ::GetWindowTextW(
                        dis->hwndItem, text.data(), gsl::narrow_cast<int>(text.size()));
                    if (length > 0) {
                        const auto& palette = ctx->d2d.appearance.palette;
                        dialog_detail::DrawDialogText(dis->hDC, title ? ctx->bold_font.Get() : ctx->font.Get(),
                            secondary ? palette.text_secondary : palette.text_primary, dis->rcItem,
                            std::wstring_view(text.data(), gsl::narrow_cast<size_t>(length)),
                            (centered ? DT_CENTER : DT_LEFT) |
                                SettingsLabelWrapFlags(gsl::narrow_cast<int>(dis->CtlID)));
                    }
                    return TRUE;
                }
                if (dis->CtlType == ODT_BUTTON) {
                    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, *ctx)) == UiLanguage::EnUs;
                    const wchar_t* disabled_suffix = english ? L"\nUnavailable" : L"\n不可用";
                    if (IsSettingsFooterControl(gsl::narrow_cast<int>(dis->CtlID)) ||
                        dis->CtlID == kIdBtnDictManager ||
                        dis->CtlID == kIdBtnRestoreDefaults ||
                        dis->CtlID == kIdBtnImportConfig || dis->CtlID == kIdBtnExportConfig ||
                        dis->CtlID == kIdBtnBrowsePrimaryDict ||
                        dis->CtlID == kIdBtnCandidateFont ||
                        dis->CtlID == kIdBtnOpenGitHub ||
                        dis->CtlID == kIdBtnOk || dis->CtlID == kIdBtnCancel) {
                        const bool page_button = !IsSettingsFooterControl(gsl::narrow_cast<int>(dis->CtlID));
                        const D2D1_COLOR_F outer_bg = ColorRefToD2DColor(page_button
                            ? ctx->d2d.appearance.palette.surface_bg
                            : ctx->d2d.appearance.palette.window_bg);
                        DrawFluentPushButton(*dis, ctx->d2d, dis->CtlID == kIdBtnOk, outer_bg, disabled_suffix,
                            dis->CtlID == kIdBtnCandidateFont);
                    } else {
                        const bool is_radio =
                            (dis->CtlID >= kIdRadioJidian && dis->CtlID <= kIdRadioBuiltin) ||
                            dis->CtlID == kIdRadioCustom;
                        DrawFluentToggleOrRadioRow(*dis, ctx->d2d, is_radio, disabled_suffix);
                    }
                    return TRUE;
                }
                if (dis->CtlType == ODT_COMBOBOX) {
                    const bool disabled = dis->CtlID == kIdComboUiTextScale &&
                        dis->itemID < gsl::narrow_cast<UINT>(std::to_underlying(UiTextScale::Percent150)) &&
                        ::IsDlgButtonChecked(hwnd, kIdChkEasyReading) == BST_CHECKED;
                    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, *ctx)) == UiLanguage::EnUs;
                    DrawFluentComboBoxItem(*dis, ctx->d2d, disabled,
                                          english ? L" - Unavailable" : L" - 不可用");
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            return reinterpret_cast<LRESULT>(ApplyFluentControlColors(hdc, ctx->d2d, true));
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            return reinterpret_cast<LRESULT>(ApplyFluentControlColors(hdc, ctx->d2d, false));
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            RefreshSettingsLanguage(hwnd, *ctx);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) != WA_INACTIVE) {
                RequestSettingsConfigRefresh(
                    hwnd,
                    *ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case kSettingsExternalConfigChangedMessage:
            RequestSettingsConfigRefresh(
                hwnd,
                *ctx);
            return 0;
        case kSettingsConfigLoadedMessage:
        {
            const bool refresh_pending =
                std::exchange(
                    ctx->config_refresh_pending,
                    false);
            ctx->config_refresh_inflight =
                false;
            const HRESULT refresh_result =
                config::DecodeAsyncResult(
                    wparam);
            if (SUCCEEDED(refresh_result)) {
                SyncSettingsWindowFromExternalConfig(
                    hwnd,
                    *ctx);
            } else {
                if (ctx->save_on_accept) {
                    ctx->skin_catalog_result =
                        refresh_result;
                }
                QIQI_LOG_ERROR(
                    "Settings broker refresh "
                    "failed (hr=0x{:08X})",
                    gsl::narrow_cast<
                        unsigned long>(
                            refresh_result));
            }
            if (refresh_pending) {
                RequestSettingsConfigRefresh(
                    hwnd,
                    *ctx);
            }
            return 0;
        }
        case WM_COMMAND: {
            HandleSettingsCommand(hwnd, wparam, *ctx);
            const auto* current = reinterpret_cast<const DialogContext*>(
                ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (current && current == ctx) {
                UpdateSettingsWindowTitle(hwnd, *current);
            }
            return 0;
        }
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


} // namespace qiqi::settings_detail
