#pragma once

#include "common/d2d_controls.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>

namespace qiqi {

// Read the full UTF-16 text of a Win32 control into a std::wstring.
inline std::wstring ReadWindowTextString(HWND control) {
    if (!control) return {};
    const int size = ::GetWindowTextLengthW(control);
    if (size <= 0) return {};
    std::wstring result(gsl::narrow_cast<size_t>(size) + 1, L'\0');
    const int copied = ::GetWindowTextW(control, result.data(), size + 1);
    result.resize(gsl::narrow_cast<size_t>(std::max(0, copied)));
    return result;
}

// Load DPI-scaled 16px and 48px icons from embedded resource 1 and apply them to `hwnd`.
inline bool UpdateWindowBrandIcons(
    HWND hwnd,
    HINSTANCE instance,
    const FluentD2DThemeContext& theme,
    ScopedIcon& out_small_icon,
    ScopedIcon& out_big_icon) noexcept {
    const auto load = [&](int size) noexcept {
        return ScopedIcon(static_cast<HICON>(::LoadImageW(
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON, theme.ScalePx(size), theme.ScalePx(size), LR_DEFAULTCOLOR)));
    };
    auto loaded_small = load(kSmallBrandIconSize);
    auto loaded_big = load(kLargeBrandIconSize);
    if (!loaded_small || !loaded_big) {
        return false;
    }
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(loaded_small.Get()));
    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(loaded_big.Get()));
    out_small_icon = std::move(loaded_small);
    out_big_icon = std::move(loaded_big);
    return true;
}

// Compute a centered initial window RECT on the active monitor's work area for the given logical size.
inline RECT ComputeCenteredToolWindowRect(
    const MonitorMetrics& monitor,
    int logical_width,
    int logical_height,
    UINT dpi) noexcept {
    const LONG width = std::min<LONG>(
        ScalePxByDpi(logical_width, dpi), monitor.work_area.right - monitor.work_area.left);
    const LONG height = std::min<LONG>(
        ScalePxByDpi(logical_height, dpi), monitor.work_area.bottom - monitor.work_area.top);
    const LONG left = monitor.work_area.left + (monitor.work_area.right - monitor.work_area.left - width) / 2;
    const LONG top = monitor.work_area.top + (monitor.work_area.bottom - monitor.work_area.top - height) / 2;
    return RECT{left, top, left + width, top + height};
}

// Handle standard Fluent D2D owner-draw, control color, background erase, and min-track-size messages
// shared across standalone tool windows. Returns std::nullopt if the message should be handled by the caller.
[[gsl::suppress("type.1")]]
inline std::optional<LRESULT> HandleFluentToolWindowMessage(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    const FluentD2DThemeContext& theme,
    UINT primary_button_id,
    int min_logical_width = 760,
    int min_logical_height = 560) noexcept {
    switch (message) {
    case WM_DRAWITEM:
        if (lparam) {
            const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (item.CtlType == ODT_COMBOBOX) {
                DrawFluentComboBoxItem(item, theme);
                return TRUE;
            }
            if (item.CtlType == ODT_BUTTON) {
                DrawFluentPushButton(
                    item,
                    theme,
                    item.CtlID == primary_button_id,
                    IsSystemDarkTheme() ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));
                return TRUE;
            }
        }
        break;
    case WM_MEASUREITEM:
        if (lparam && reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlType == ODT_COMBOBOX) {
            reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight =
                gsl::narrow_cast<UINT>(theme.ScalePx(kFluentComboItemHeight));
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return reinterpret_cast<LRESULT>(ApplyFluentControlColors(reinterpret_cast<HDC>(wparam), theme, true));
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(ApplyFluentControlColors(reinterpret_cast<HDC>(wparam), theme, false));
    case WM_ERASEBKGND: {
        RECT rect{};
        ::GetClientRect(window, &rect);
        const HBRUSH brush =
            IsHighContrastEnabled() ? ::GetSysColorBrush(COLOR_BTNFACE) : theme.surface_brush.Get();
        ::FillRect(reinterpret_cast<HDC>(wparam), &rect, brush ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_GETMINMAXINFO:
        if (lparam) {
            auto& info = *reinterpret_cast<MINMAXINFO*>(lparam);
            const auto monitor = QueryActiveMonitor(window);
            info.ptMinTrackSize = {
                std::min<LONG>(theme.ScalePx(min_logical_width), monitor.work_area.right - monitor.work_area.left),
                std::min<LONG>(theme.ScalePx(min_logical_height), monitor.work_area.bottom - monitor.work_area.top)};
            return 0;
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

// Apply Explorer / DarkMode_Explorer themes and ListView colors to a tool window and its controls.
inline bool ApplyToolWindowListAndControlsTheme(
    HWND hwnd,
    HWND list_hwnd,
    FluentD2DThemeContext& theme,
    std::span<const HWND> themed_controls) noexcept {
    theme.RefreshThemeBrushes();
    const bool contrast = IsHighContrastEnabled();
    const bool dark = !contrast && IsSystemDarkTheme();
    ApplySystemThemeToMenuAndWindow(hwnd, dark);
    bool all_ok = true;
    for (const HWND ctrl : themed_controls) {
        if (ctrl && FAILED(::SetWindowTheme(ctrl, contrast ? nullptr : (dark ? L"DarkMode_Explorer" : L"Explorer"), nullptr))) {
            all_ok = false;
        }
    }
    if (list_hwnd) {
        if (FAILED(::SetWindowTheme(
                ListView_GetHeader(list_hwnd),
                contrast ? nullptr : (dark ? L"DarkMode_ItemsView" : L"ItemsView"),
                nullptr))) {
            all_ok = false;
        }
        const COLORREF background =
            contrast ? ::GetSysColor(COLOR_WINDOW) : (dark ? RGB(43, 46, 52) : RGB(255, 255, 255));
        const COLORREF foreground =
            contrast ? ::GetSysColor(COLOR_WINDOWTEXT) : (dark ? kDarkTextColor : kLightTextColor);
        ListView_SetBkColor(list_hwnd, background);
        ListView_SetTextBkColor(list_hwnd, background);
        ListView_SetTextColor(list_hwnd, foreground);
    }
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    return all_ok;
}

// Custom-draw dark mode ListView header items so header text remains light on Windows dark themes.
[[gsl::suppress("type.1")]]
inline std::optional<LRESULT> TryCustomDrawDarkListViewHeader(
    HWND list_window,
    LPARAM lparam,
    HFONT font,
    const FluentD2DThemeContext& theme,
    bool* out_header_read_failed = nullptr) noexcept {
    if (out_header_read_failed) *out_header_read_failed = false;
    if (!lparam || IsHighContrastEnabled() || !IsSystemDarkTheme()) {
        return std::nullopt;
    }
    const auto& notification = *reinterpret_cast<const NMHDR*>(lparam);
    if (notification.hwndFrom != ListView_GetHeader(list_window) || notification.code != NM_CUSTOMDRAW) {
        return std::nullopt;
    }
    const auto& draw = *reinterpret_cast<const NMCUSTOMDRAW*>(lparam);
    if (draw.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.dwDrawStage == CDDS_ITEMPREPAINT) {
        std::array<wchar_t, 256> text{};
        HDITEMW item{};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text.data();
        item.cchTextMax = gsl::narrow_cast<int>(text.size());
        if (!Header_GetItem(notification.hwndFrom, gsl::narrow_cast<int>(draw.dwItemSpec), &item)) {
            if (out_header_read_failed) *out_header_read_failed = true;
            return CDRF_DODEFAULT;
        }
        const ScopedSaveDC saved(draw.hdc);
        const ScopedSelectObject selected_font(draw.hdc, font);
        ::SetDCBrushColor(draw.hdc, (draw.uItemState & CDIS_HOT) ? RGB(66, 70, 78) : RGB(54, 58, 66));
        const auto brush = ::GetSysColorBrush(COLOR_WINDOWFRAME);
        ::FillRect(draw.hdc, &draw.rc, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
        const RECT border{draw.rc.right - 1, draw.rc.top, draw.rc.right, draw.rc.bottom};
        ::FillRect(draw.hdc, &border, brush);
        RECT label = draw.rc;
        label.left += theme.ScalePx(6);
        label.right -= theme.ScalePx(6);
        ::SetTextColor(draw.hdc, kDarkTextColor);
        ::SetBkMode(draw.hdc, TRANSPARENT);
        const UINT alignment =
            (item.fmt & HDF_RIGHT) ? DT_RIGHT : ((item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT);
        ::DrawTextW(
            draw.hdc,
            text.data(),
            -1,
            &label,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | alignment);
        return CDRF_SKIPDEFAULT;
    }
    return std::nullopt;
}

// Compute screen coordinates for a ListView context menu (handling Shift+F10 / Apps key (-1,-1) coordinates).
inline POINT ResolveListViewContextMenuPoint(
    HWND list_hwnd, POINT screen_pt, const FluentD2DThemeContext& theme) noexcept {
    if (screen_pt.x == -1 && screen_pt.y == -1) {
        RECT rc{};
        const int sel = ListView_GetNextItem(list_hwnd, -1, LVNI_FOCUSED | LVNI_SELECTED);
        if (sel >= 0 &&
            ::SendMessageW(list_hwnd, LVM_GETITEMRECT, gsl::narrow_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(&rc))) {
            screen_pt = {rc.left + theme.ScalePx(16), rc.bottom};
            ::ClientToScreen(list_hwnd, &screen_pt);
        } else if (::GetWindowRect(list_hwnd, &rc)) {
            screen_pt = {rc.left + theme.ScalePx(16), rc.top + theme.ScalePx(28)};
        }
    }
    return screen_pt;
}

// Standard dialog-aware message loop for standalone tool windows.
inline int RunToolWindowMessageLoop(HWND window, int show) noexcept {
    ::ShowWindow(window, show);
    MSG message{};
    BOOL result = 0;
    while ((result = ::GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!::IsDialogMessageW(window, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    return result < 0 ? 1 : gsl::narrow_cast<int>(message.wParam);
}

} // namespace qiqi
