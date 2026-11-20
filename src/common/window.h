// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/controls.h"
#include "common/i18n.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace qiqi {

inline std::wstring FormatToolWindowFailureMessage(
    std::wstring_view operation, ULONG error, UiLanguage language) {
    return std::format(
        L"{}\r\n{}={} (0x{:08X})",
        operation,
        LocalizedUiText(language, L"错误码", L"Error"),
        error,
        error);
}

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
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON, theme.ScaleDipPx(size), theme.ScaleDipPx(size), LR_DEFAULTCOLOR)));
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

struct OwnerDataListSelection {
    std::vector<uint64_t> selected_ids;
    uint64_t active_id = 0;
};

inline OwnerDataListSelection ReadOwnerDataListSelection(
    HWND list, std::span<const uint64_t> row_ids) {
    OwnerDataListSelection result;
    if (!::IsWindow(list) || row_ids.empty()) return result;
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < row_ids.size()) {
            result.selected_ids.push_back(
                gsl::at(row_ids, gsl::narrow_cast<size_t>(index)));
        }
    }
    const int focused =
        ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
    if (focused >= 0 &&
        gsl::narrow_cast<size_t>(focused) < row_ids.size()) {
        result.active_id =
            gsl::at(row_ids, gsl::narrow_cast<size_t>(focused));
    } else if (!result.selected_ids.empty()) {
        result.active_id = result.selected_ids.back();
    }
    return result;
}

class FluentToolWindowViewport final {
public:
    int ContentHeight() const noexcept { return content_height_; }
    int Offset() const noexcept { return offset_; }
    void Configure(HWND window, int content_height, int viewport_height) noexcept {
        content_height_ = std::max({content_height, viewport_height, 1});
        viewport_height_ = std::max(1, viewport_height);
        offset_ = std::clamp(offset_, 0, std::max(0, content_height_ - viewport_height_));
        const bool scrollable = content_height_ > viewport_height_;
        const bool visible = (::GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) != 0;
        ::ShowScrollBar(window, SB_VERT, scrollable);
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMax = content_height_ - 1;
        info.nPage = gsl::narrow_cast<UINT>(viewport_height_);
        info.nPos = offset_;
        ::SetScrollInfo(window, SB_VERT, &info, TRUE);
        if (visible != scrollable && !::PostMessageW(window, WM_SIZE, 0, 0)) {
            QIQI_LOG_WARN("Tool viewport relayout notification failed (error={})", ::GetLastError());
        }
    }

    void Move(HWND child, int x, int y, int width, int height) const noexcept {
        width = std::max(1, width);
        height = std::max(1, height);
        const int top = y - offset_;
        if (!::SetWindowPos(child, nullptr, x, top, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW)) {
            QIQI_LOG_WARN("Tool viewport child positioning failed (error={})", ::GetLastError());
            return;
        }
        ScopedGdiHandle<HRGN> region(::CreateRectRgn(0, std::clamp(-top, 0, height),
            width, std::clamp(viewport_height_ - top, 0, height)));
        if (!region || !::SetWindowRgn(child, region.Get(), FALSE)) {
            QIQI_LOG_WARN("Tool viewport child clipping failed (error={})", ::GetLastError());
            return;
        }
        std::ignore = region.Detach();
    }

    bool Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        const FluentD2DThemeContext& theme) noexcept {
        int next = offset_;
        if (message == WM_VSCROLL && lparam == 0) {
            const int line = FluentControlHeight(theme, kFluentComboItemHeight);
            switch (LOWORD(wparam)) {
            case SB_LINEUP: next -= line; break;
            case SB_LINEDOWN: next += line; break;
            case SB_PAGEUP: next -= viewport_height_; break;
            case SB_PAGEDOWN: next += viewport_height_; break;
            case SB_TOP: next = 0; break;
            case SB_BOTTOM: next = content_height_; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = SIF_TRACKPOS;
                if (::GetScrollInfo(window, SB_VERT, &info)) next = info.nTrackPos;
                else QIQI_LOG_WARN("Tool viewport scrollbar query failed (error={})", ::GetLastError());
                break;
            }
            default: break;
            }
        } else if (message == WM_MOUSEWHEEL) {
            next -= ::MulDiv(GET_WHEEL_DELTA_WPARAM(wparam),
                FluentControlHeight(theme, kFluentComboItemHeight), WHEEL_DELTA);
        } else {
            HWND child = nullptr;
            if (message == WM_COMMAND &&
                (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == CBN_SETFOCUS)) {
                child = reinterpret_cast<HWND>(lparam);
            } else if (message == WM_NOTIFY && lparam) {
                const auto& header = *reinterpret_cast<const NMHDR*>(lparam);
                if (header.code == NM_SETFOCUS) child = header.hwndFrom;
            }
            if (child && ::IsChild(window, child)) {
                RECT rect{};
                if (!::GetWindowRect(child, &rect)) {
                    QIQI_LOG_WARN("Tool focused control bounds query failed (error={})", ::GetLastError());
                    return false;
                }
                ::SetLastError(ERROR_SUCCESS);
                if (!::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2) &&
                    ::GetLastError() != ERROR_SUCCESS) {
                    QIQI_LOG_WARN("Tool focused control coordinate mapping failed (error={})", ::GetLastError());
                    return false;
                }
                if (rect.top < 0) next += rect.top;
                else if (rect.bottom > viewport_height_) {
                    next += std::min(rect.top, rect.bottom - viewport_height_);
                }
            }
        }
        next = std::clamp(next, 0, std::max(0, content_height_ - viewport_height_));
        if (next == offset_) return false;
        offset_ = next;
        return true;
    }

private:
    int offset_ = 0;
    int content_height_ = 0;
    int viewport_height_ = 1;
};

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
            const ScopedSaveDC saved(item.hDC);
            const ScopedGdiHandle<HRGN> region(::CreateRectRgn(0, 0, 0, 0));
            if (!saved || !region) {
                QIQI_LOG_WARN("Tool control drawing state creation failed (error={})", ::GetLastError());
                return TRUE;
            }
            const bool popup_item = item.CtlType == ODT_COMBOBOX && item.itemID != gsl::narrow_cast<UINT>(-1);
            if (!popup_item && ::GetWindowRgn(item.hwndItem, region.Get()) != ERROR) {
                if (::ExtSelectClipRgn(item.hDC, region.Get(), RGN_AND) == ERROR) {
                    QIQI_LOG_WARN("Tool control drawing clip failed (error={})", ::GetLastError());
                    return TRUE;
                }
            }
            if (item.CtlType == ODT_COMBOBOX) {
                DrawFluentComboBoxItem(item, theme);
                return TRUE;
            }
            if (item.CtlType == ODT_BUTTON) {
                DrawFluentPushButton(
                    item,
                    theme,
                    item.CtlID == primary_button_id,
                    ColorRefToD2DColor(theme.appearance.palette.input_bg));
                return TRUE;
            }
        }
        break;
    case WM_MEASUREITEM:
        if (lparam && reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlType == ODT_COMBOBOX) {
            reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight =
                gsl::narrow_cast<UINT>(FluentControlHeight(theme, kFluentComboItemHeight));
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
    const bool contrast = theme.appearance.system_high_contrast;
    const bool dark = !contrast && theme.appearance.palette.is_dark;
    ApplyDisplayThemeToWindow(hwnd, theme.appearance);
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
        const COLORREF background = theme.appearance.palette.input_bg;
        const COLORREF foreground = theme.appearance.palette.text_primary;
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
    if (!lparam || theme.appearance.system_high_contrast ||
        (!theme.appearance.palette.is_dark && !theme.appearance.high_contrast)) {
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
        ::SetDCBrushColor(draw.hdc, (draw.uItemState & CDIS_HOT) ?
            theme.appearance.palette.hover_bg : theme.appearance.palette.surface_bg);
        constexpr int kHeaderBorderWidthDip = 1;
        constexpr int kHeaderTextPaddingDip = 6;
        ::FillRect(draw.hdc, &draw.rc, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
        const RECT border{draw.rc.right - theme.ScaleDipPx(kHeaderBorderWidthDip),
            draw.rc.top, draw.rc.right, draw.rc.bottom};
        ::SetDCBrushColor(draw.hdc, theme.appearance.palette.border_color);
        ::FillRect(draw.hdc, &border, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
        RECT label = draw.rc;
        label.left += theme.ScalePx(kHeaderTextPaddingDip);
        label.right -= theme.ScalePx(kHeaderTextPaddingDip);
        ::SetTextColor(draw.hdc, theme.appearance.palette.text_primary);
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
