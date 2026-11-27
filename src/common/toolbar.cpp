// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/toolbar.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <system_error>
#include <cwctype>
#include <utility>
#include <uxtheme.h>

namespace qiqi {
namespace {
constexpr int kToolbarHeight = 40;
constexpr int kToolbarButtonSize = 36;
constexpr int kToolbarGroupGap = 8;
constexpr int kToolbarIconSize = 20;
constexpr int kCompactIconSize = 16;
constexpr int kCompactIconPadding = 4;
constexpr int kCompactIconStroke = 1;
constexpr int kToolbarIconStroke = 2;
constexpr int kFocusInsetPx = 2;
constexpr int kMenuPaddingX = 20;
constexpr int kMenuPaddingY = 8;
constexpr int kMenuCheckGutter = 28;
constexpr int kMenuSeparatorHeight = 12;
constexpr int kComfortMenuTarget = kComfortMinimumTargetDip;
constexpr int kToolbarTextGap = 8;
constexpr size_t kToolbarTextCapacity = 256;
constexpr int kMaxTooltipWidthPx = 400;
}

void ToolCommandUi::Report(const char* operation, ULONG error) const noexcept {
    error_sink_(context_, operation, error);
}

[[noreturn]] void ToolCommandUi::Fail(const char* operation, ULONG error) const {
    Report(operation, error);
    if (error != ERROR_SUCCESS)
        throw std::system_error(error, std::system_category(), operation);
    throw std::runtime_error(operation);
}

void ToolCommandUi::InitializeToolbar(HWND toolbar, bool compact) {
    ::SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    ::SendMessageW(toolbar, TB_SETMAXTEXTROWS, 0, 0);
    ::SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
    // Custom-drawn compact toolbars must not inherit the default bitmap's minimum size.
    if (compact)
        ::SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(1, 1));
    if (const auto tooltip = reinterpret_cast<HWND>(::SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0))) {
        ::SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, kMaxTooltipWidthPx);
    }
    toolbars_.push_back({toolbar, compact});
}

void ToolCommandUi::SetMenu(HWND window, std::span<const ToolMenu> menus, const FluentD2DThemeContext& theme) {
    ScopedMenu menu(::CreateMenu());
    if (!menu)
        Fail("CreateMenu", ::GetLastError());
    MenuItems items;
    RefreshMenuAppearance(window, theme.dpi, theme);
    for (const auto& group : menus) {
        ScopedMenu popup(::CreatePopupMenu());
        if (!popup)
            Fail("CreatePopupMenu", ::GetLastError());
        for (const auto& command : group.commands) {
            AppendMenuCommand(popup.Get(), command, group.radio, items);
        }
        const auto index = gsl::narrow_cast<UINT>(::GetMenuItemCount(menu.Get()));
        if (!::AppendMenuW(menu.Get(), MF_POPUP, reinterpret_cast<UINT_PTR>(popup.Get()), group.text)) {
            Fail("AppendMenuW(popup)", ::GetLastError());
        }
        std::ignore = popup.Detach();
        auto heading = std::make_unique<MenuItem>(
            MenuItem{menu.Get(), 0, group.text, group.mnemonic, true, false, false});
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA;
        info.fType = MFT_OWNERDRAW;
        info.dwItemData = reinterpret_cast<ULONG_PTR>(heading.get());
        if (!::SetMenuItemInfoW(menu.Get(), index, TRUE, &info)) {
            Fail("SetMenuItemInfoW(heading)", ::GetLastError());
        }
        items.push_back(std::move(heading));
    }
    const HMENU previous = ::GetMenu(window);
    menu_items_.swap(items);
    ApplyMenuBackground(menu.Get());
    if (!::SetMenu(window, menu.Get())) {
        const DWORD error = ::GetLastError();
        menu_items_.swap(items);
        Fail("SetMenu", error);
    }
    std::ignore = menu.Detach(); // The window owns the attached menu.
    const ScopedMenu old_menu(previous);
    ::DrawMenuBar(window);
}

void ToolCommandUi::AppendMenuCommand(
    HMENU menu, const ToolCommand& command, bool radio, MenuItems& items, HFONT font) {
    auto item = std::make_unique<MenuItem>(
        MenuItem{menu, command.id, command.text, 0, false, command.id == 0, radio || command.radio, font});
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_DATA;
    info.fType = MFT_OWNERDRAW | (item->separator ? MFT_SEPARATOR : 0) | (item->radio ? MFT_RADIOCHECK : 0);
    info.wID = command.id;
    info.dwTypeData = item->text.data();
    info.fState = (command.enabled ? MFS_ENABLED : MFS_GRAYED) | (command.checked ? MFS_CHECKED : 0);
    info.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
    if (!::InsertMenuItemW(menu, gsl::narrow_cast<UINT>(::GetMenuItemCount(menu)), TRUE, &info))
        Fail("InsertMenuItemW(command)", ::GetLastError());
    items.push_back(std::move(item));
}

UINT ToolCommandUi::TrackContextMenu(
    HWND window, POINT point, std::span<const ToolCommand> commands,
    const FluentD2DThemeContext& theme, TPMPARAMS* placement,
    gsl::not_null<decltype(&::TrackPopupMenuEx)> track_menu) {
    // Item storage and background outlive the synchronous native menu loop, including its draw callbacks.
    MenuItems items;
    const ScopedMenu menu(::CreatePopupMenu());
    if (!menu)
        Fail("CreatePopupMenu", ::GetLastError());
    RefreshMenuAppearance(window, theme.dpi, theme);
    for (const auto& command : commands)
        AppendMenuCommand(menu.Get(), command, false, items, command.popup_font);
    const auto* previous = context_items_;
    context_items_ = &items;
    const auto restore = gsl::finally([this, previous]() noexcept { context_items_ = previous; });
    ApplyMenuBackground(menu.Get());
    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON |
        (placement ? TPM_RIGHTALIGN | TPM_VERTICAL : 0);
    ::SetLastError(ERROR_SUCCESS);
    const BOOL choice = track_menu(menu.Get(), flags, point.x, point.y, window, placement);
    const DWORD error = ::GetLastError();
    if (choice == 0 && error != ERROR_SUCCESS) Report("TrackPopupMenuEx", error);
    return gsl::narrow_cast<UINT>(choice);
}

void ToolCommandUi::SetToolbar(HWND toolbar, std::span<const ToolCommand> commands) {
    while (::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) > 0) {
        if (!::SendMessageW(toolbar, TB_DELETEBUTTON, 0, 0))
            Fail("TB_DELETEBUTTON", ERROR_SUCCESS);
    }
    for (const auto& command : commands) {
        TBBUTTON item{};
        item.iBitmap = I_IMAGENONE;
        item.idCommand = gsl::narrow_cast<int>(command.id);
        item.fsState = command.id == 0 ? 0 :
            (command.enabled ? TBSTATE_ENABLED : 0) | (command.checked ? TBSTATE_CHECKED : 0);
        item.fsStyle = command.id == 0 ? BTNS_SEP : command.radio ? BTNS_CHECKGROUP : BTNS_BUTTON;
        if (command.id == 0) item.iBitmap = kToolbarGroupGap;
        item.dwData = std::to_underlying(command.icon);
        item.iString = command.id == 0 ? 0 : reinterpret_cast<INT_PTR>(command.text);
        if (!::SendMessageW(toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&item))) {
            Fail("TB_ADDBUTTONSW", ERROR_SUCCESS);
        }
    }
}

void ToolCommandUi::UpdateCommand(HWND window, const ToolCommand& command) {
    std::wstring text(command.text);
    MENUITEMINFOW menu{};
    menu.cbSize = sizeof(menu);
    menu.fMask = MIIM_STRING | MIIM_STATE;
    menu.dwTypeData = text.data();
    menu.fState = (command.enabled ? MFS_ENABLED : MFS_GRAYED) | (command.checked ? MFS_CHECKED : 0);
    if (!::SetMenuItemInfoW(::GetMenu(window), command.id, FALSE, &menu)) {
        Fail("SetMenuItemInfoW(command)", ::GetLastError());
    }
    for (const auto& item : menu_items_) {
        if (!item->heading && item->id == command.id)
            item->text = text;
    }
    for (const auto& toolbar : toolbars_) {
        if (::SendMessageW(toolbar.window, TB_COMMANDTOINDEX, command.id, 0) < 0)
            continue;
        TBBUTTONINFOW button{};
        button.cbSize = sizeof(button);
        button.dwMask = TBIF_TEXT | TBIF_STATE | TBIF_LPARAM;
        button.pszText = text.data();
        button.fsState = (command.enabled ? TBSTATE_ENABLED : 0) | (command.checked ? TBSTATE_CHECKED : 0);
        button.lParam = std::to_underlying(command.icon);
        if (!::SendMessageW(toolbar.window, TB_SETBUTTONINFOW, command.id, reinterpret_cast<LPARAM>(&button))) {
            Fail("TB_SETBUTTONINFOW", ERROR_SUCCESS);
        }
        ::InvalidateRect(toolbar.window, nullptr, FALSE);
    }
}

int ToolCommandUi::LayoutToolbar(HWND toolbar, int width, const FluentD2DThemeContext& theme) const noexcept {
    ::MoveWindow(toolbar, 0, 0, width, FluentControlHeight(theme, kToolbarHeight), TRUE);
    const int size = FluentControlHeight(theme, kToolbarButtonSize);
    ::SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    const auto found = std::ranges::find(toolbars_, toolbar, &Toolbar::window);
    const bool show_text = theme.display_preferences.easy_reading &&
        found != toolbars_.end() && !found->compact;
    ::SendMessageW(toolbar, TB_SETMAXTEXTROWS, show_text ? 1 : 0, 0);
    ::SendMessageW(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(size, show_text ? std::max(size, width) : size));
    const ScopedWindowDC dc(toolbar);
    const ScopedSelectObject font(dc.Get(), reinterpret_cast<HFONT>(::SendMessageW(toolbar, WM_GETFONT, 0, 0)));
    const LRESULT count = ::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        TBBUTTON button{};
        if (!::SendMessageW(toolbar, TB_GETBUTTON, gsl::narrow_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&button))) {
            Report("TB_GETBUTTON(layout)", ERROR_NOT_FOUND);
            continue;
        }
        if ((button.fsStyle & BTNS_SEP) != 0) {
            TBBUTTONINFO separator{};
            separator.cbSize = sizeof(separator);
            separator.dwMask = TBIF_BYINDEX | TBIF_SIZE;
            separator.cx = gsl::narrow_cast<WORD>(theme.ScalePx(kToolbarGroupGap));
            if (!::SendMessageW(toolbar, TB_SETBUTTONINFOW, gsl::narrow_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&separator)))
                Report("TB_SETBUTTONINFOW(separator size)", ERROR_INVALID_DATA);
        } else {
            TBBUTTONINFOW info{};
            info.cbSize = sizeof(info);
            info.dwMask = TBIF_SIZE | TBIF_STYLE;
            info.fsStyle = gsl::narrow_cast<BYTE>(show_text ?
                button.fsStyle | BTNS_SHOWTEXT : button.fsStyle & ~BTNS_SHOWTEXT);
            int button_width = size;
            if (show_text) {
                std::array<wchar_t, kToolbarTextCapacity> text{};
                TBBUTTONINFOW label{};
                label.cbSize = sizeof(label);
                label.dwMask = TBIF_TEXT;
                label.pszText = text.data();
                label.cchText = gsl::narrow_cast<int>(text.size());
                SIZE extent{};
                if (::SendMessageW(toolbar, TB_GETBUTTONINFOW, button.idCommand, reinterpret_cast<LPARAM>(&label)) < 0 ||
                    !::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(std::wcslen(text.data())), &extent)) {
                    Report("Measure toolbar command");
                } else {
                    button_width = std::max(size, std::min(width, gsl::narrow_cast<int>(extent.cx) +
                        theme.ScalePx(kToolbarIconSize + 3 * kToolbarTextGap)));
                }
            }
            info.cx = gsl::narrow_cast<WORD>(button_width);
            if (!::SendMessageW(toolbar, TB_SETBUTTONINFOW, button.idCommand, reinterpret_cast<LPARAM>(&info))) {
                Report("TB_SETBUTTONINFOW(command size)", ERROR_INVALID_DATA);
            }
        }
    }
    ::SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    ::SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    RECT rect{};
    ::GetWindowRect(toolbar, &rect);
    return rect.bottom - rect.top;
}

void ToolCommandUi::ApplyTheme(HWND window, const FluentD2DThemeContext& theme) noexcept {
    RefreshMenuAppearance(window, theme.dpi, theme);
    ApplyMenuBackground(::GetMenu(window));
    const bool contrast = theme.appearance.system_high_contrast;
    const auto& palette = theme.appearance.palette;
    ::DrawMenuBar(window);
    for (const auto& toolbar : toolbars_) {
        ::InvalidateRect(toolbar.window, nullptr, TRUE);
        const auto tooltip = reinterpret_cast<HWND>(::SendMessageW(toolbar.window, TB_GETTOOLTIPS, 0, 0));
        if (tooltip) {
            const HRESULT result = ::SetWindowTheme(
                tooltip, contrast ? nullptr : (palette.is_dark ? L"DarkMode_Explorer" : L"Explorer"), nullptr);
            if (FAILED(result))
                Report("SetWindowTheme(toolbar tooltip)", gsl::narrow_cast<ULONG>(result));
        }
    }
}

void ToolCommandUi::DrawIcon(HDC dc, const RECT& rect, ToolIcon icon, int size, int stroke,
                             COLORREF color) const noexcept {
    const ScopedGdiHandle<HPEN> pen(::CreatePen(PS_SOLID, stroke, color));
    if (!pen) {
        Report("CreatePen(command icon)");
        return;
    }
    const ScopedSelectObject selected_pen(dc, pen.Get());
    const ScopedSelectObject selected_brush(dc, ::GetStockObject(NULL_BRUSH));
    if (!selected_pen || !selected_brush) {
        Report("SelectObject(command icon)");
        return;
    }
    const int left = rect.left + (rect.right - rect.left - size) / 2;
    const int top = rect.top + (rect.bottom - rect.top - size) / 2;
    const auto x = [&](int value) noexcept { return left + ::MulDiv(value, size, 24); };
    const auto y = [&](int value) noexcept { return top + ::MulDiv(value, size, 24); };
    bool drawn = true;
    const auto path = [&](std::initializer_list<POINT> points) noexcept {
        std::array<POINT, 8> scaled{};
        size_t count = 0;
        for (const auto& point : points)
            gsl::at(scaled, count++) = {x(point.x), y(point.y)};
        if (!::Polyline(dc, scaled.data(), gsl::narrow_cast<int>(count)))
            drawn = false;
    };
    const auto box = [&](int l, int t, int r, int b) noexcept {
        if (!::Rectangle(dc, x(l), y(t), x(r), y(b)))
            drawn = false;
    };
    switch (icon) {
    case ToolIcon::Edit:
        path({{4, 16}, {16, 4}, {21, 9}, {9, 21}, {3, 22}, {4, 16}});
        path({{13, 7}, {18, 12}});
        path({{4, 16}, {9, 21}});
        break;
    case ToolIcon::New:
        path({{12, 3}, {12, 21}});
        path({{3, 12}, {21, 12}});
        break;
    case ToolIcon::Reload:
        if (!::Arc(dc, x(4), y(4), x(20), y(20), x(20), y(4), x(20), y(12)))
            drawn = false;
        path({{16, 8}, {20, 12}, {23, 8}});
        break;
    case ToolIcon::Copy:
        box(4, 8, 16, 21);
        path({{9, 8}, {9, 3}, {21, 3}, {21, 16}, {16, 16}});
        break;
    case ToolIcon::Export:
        path({{4, 16}, {4, 21}, {20, 21}, {20, 16}});
        path({{12, 3}, {12, 16}});
        path({{7, 8}, {12, 3}, {17, 8}});
        break;
    case ToolIcon::Import:
        path({{13, 3}, {21, 3}, {21, 21}, {13, 21}});
        path({{13, 3}, {13, 7}});
        path({{13, 17}, {13, 21}});
        path({{2, 12}, {17, 12}});
        path({{12, 7}, {17, 12}, {12, 17}});
        break;
    case ToolIcon::Save:
        path({{4, 3}, {17, 3}, {21, 7}, {21, 21}, {4, 21}, {4, 3}});
        box(8, 3, 16, 10);
        box(8, 14, 17, 21);
        break;
    case ToolIcon::Pin:
        path({{8, 3}, {16, 3}, {15, 11}, {19, 16}, {5, 16}, {9, 11}, {8, 3}});
        path({{12, 16}, {12, 23}});
        break;
    case ToolIcon::Delete:
        path({{3, 6}, {21, 6}});
        path({{8, 6}, {8, 3}, {16, 3}, {16, 6}});
        path({{6, 6}, {7, 21}, {17, 21}, {18, 6}});
        path({{10, 10}, {10, 17}});
        path({{14, 10}, {14, 17}});
        break;
    case ToolIcon::Promote:
        path({{4, 21}, {4, 15}, {8, 15}, {8, 21}});
        path({{11, 21}, {11, 11}, {15, 11}, {15, 21}});
        path({{20, 21}, {20, 3}});
        path({{16, 7}, {20, 3}, {24, 7}});
        break;
    case ToolIcon::Start:
        path({{7, 3}, {21, 12}, {7, 21}, {7, 3}});
        break;
    case ToolIcon::Stop:
        box(5, 5, 20, 20);
        break;
    case ToolIcon::Pause:
    case ToolIcon::Resume:
        box(3, 3, 14, 21);
        path({{6, 8}, {11, 8}});
        path({{6, 12}, {11, 12}});
        path({{6, 16}, {9, 16}});
        if (icon == ToolIcon::Pause) {
            path({{18, 6}, {18, 18}});
            path({{22, 6}, {22, 18}});
        } else {
            path({{20, 4}, {20, 19}});
            path({{17, 16}, {20, 19}, {23, 16}});
        }
        break;
    case ToolIcon::Diagnostics:
        box(3, 4, 22, 21);
        path({{6, 15}, {9, 15}, {11, 8}, {14, 18}, {16, 12}, {20, 12}});
        break;
    case ToolIcon::Events:
        box(4, 3, 21, 22);
        path({{8, 8}, {17, 8}});
        path({{8, 12}, {17, 12}});
        path({{8, 16}, {14, 16}});
        break;
    case ToolIcon::Metrics:
        path({{2, 3}, {2, 22}, {23, 22}});
        box(5, 13, 9, 20);
        box(11, 8, 15, 20);
        box(17, 3, 21, 20);
        break;
    default:
        Report("Unsupported command icon", ERROR_INVALID_PARAMETER);
        return;
    }
    if (!drawn)
        Report("Draw command icon");
}

LRESULT ToolCommandUi::DrawToolbar(NMCUSTOMDRAW& draw, const Toolbar& toolbar,
                                   const FluentD2DThemeContext& theme) const noexcept {
    const ScopedSaveDC save(draw.hdc);
    const bool contrast = theme.appearance.system_high_contrast;
    const auto& palette = theme.appearance.palette;
    const auto fill = [&](const RECT& rect, COLORREF color) noexcept {
        ::SetDCBrushColor(draw.hdc, color);
        ::FillRect(draw.hdc, &rect, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
    };
    if (draw.dwDrawStage == CDDS_PREPAINT) {
        fill(draw.rc, contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.dwDrawStage != CDDS_ITEMPREPAINT)
        return CDRF_DODEFAULT;
    TBBUTTONINFOW button{};
    button.cbSize = sizeof(button);
    button.dwMask = TBIF_LPARAM | TBIF_STYLE;
    if (::SendMessageW(toolbar.window, TB_GETBUTTONINFOW, draw.dwItemSpec, reinterpret_cast<LPARAM>(&button)) < 0) {
        Report("TB_GETBUTTONINFOW(draw)", ERROR_NOT_FOUND);
        return CDRF_DODEFAULT;
    }
    if ((button.fsStyle & BTNS_SEP) != 0) return CDRF_DODEFAULT;
    const bool hot = (draw.uItemState & (CDIS_HOT | CDIS_SELECTED | CDIS_CHECKED)) != 0;
    const bool disabled = (draw.uItemState & CDIS_DISABLED) != 0;
    const COLORREF background =
        contrast ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE) : (hot ? palette.hover_bg : palette.window_bg);
    const COLORREF foreground =
        contrast ? ::GetSysColor(disabled ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT))
                 : (disabled ? DisabledControlText(theme) :
                    (hot && theme.appearance.high_contrast ? palette.selection_text : palette.text_primary));
    fill(draw.rc, background);
    const int size = toolbar.compact
                         ? std::max(1, std::min(theme.ScalePx(kCompactIconSize),
                                                gsl::narrow_cast<int>(draw.rc.bottom - draw.rc.top) - theme.ScalePx(kCompactIconPadding)))
                         : theme.ScalePx(kToolbarIconSize);
    RECT icon_rect = draw.rc;
    const bool show_text = theme.display_preferences.easy_reading && !toolbar.compact;
    if (show_text) {
        icon_rect.left += theme.ScalePx(kToolbarTextGap);
        icon_rect.right = icon_rect.left + size;
        std::array<wchar_t, kToolbarTextCapacity> text{};
        TBBUTTONINFOW label{};
        label.cbSize = sizeof(label);
        label.dwMask = TBIF_TEXT;
        label.pszText = text.data();
        label.cchText = gsl::narrow_cast<int>(text.size());
        if (::SendMessageW(toolbar.window, TB_GETBUTTONINFOW, draw.dwItemSpec, reinterpret_cast<LPARAM>(&label)) >= 0) {
            RECT text_rect = draw.rc;
            text_rect.left = icon_rect.right + theme.ScalePx(kToolbarTextGap);
            text_rect.right -= theme.ScalePx(kToolbarTextGap);
            const ScopedSelectObject selected_font(draw.hdc,
                reinterpret_cast<HFONT>(::SendMessageW(toolbar.window, WM_GETFONT, 0, 0)));
            ::SetTextColor(draw.hdc, foreground);
            ::SetBkMode(draw.hdc, TRANSPARENT);
            ::DrawTextW(draw.hdc, text.data(), -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            Report("TB_GETBUTTONINFOW(command text)", ERROR_INVALID_DATA);
        }
    }
    DrawIcon(draw.hdc, icon_rect, gsl::narrow_cast<ToolIcon>(button.lParam), size,
             std::max(1, theme.ScalePx(toolbar.compact ? kCompactIconStroke : kToolbarIconStroke)), foreground);
    if (disabled && theme.appearance.high_contrast) {
        ::SetTextColor(draw.hdc, foreground);
        ::SetBkMode(draw.hdc, TRANSPARENT);
        ::DrawTextW(draw.hdc, L"\x00D7", 1, &icon_rect, DT_SINGLELINE | DT_RIGHT | DT_TOP);
    }
    if (draw.uItemState & CDIS_FOCUS) {
        RECT focus = draw.rc;
        ::InflateRect(&focus, -kFocusInsetPx, -kFocusInsetPx);
        ::DrawFocusRect(draw.hdc, &focus);
    }
    return CDRF_SKIPDEFAULT;
}

int ToolCommandUi::MenuScale(int px) const noexcept {
    return ScaleDisplayPx(px, menu_dpi_, menu_preferences_.text_scale);
}

bool ToolCommandUi::MenuActive() const noexcept {
    if (context_items_) return true;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetGUIThreadInfo(::GetCurrentThreadId(), &info)) {
        Report("GetGUIThreadInfo(menu)", ::GetLastError());
        return false;
    }
    return (info.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE)) != 0;
}

void ToolCommandUi::RefreshMenuAppearance(HWND window, UINT dpi, const FluentD2DThemeContext& theme) noexcept {
    const bool window_preferences = preferences_source_ == PreferenceSource::Window;
    const auto preferences = window_preferences ? theme.display_preferences :
        NormalizeDisplayPreferences(GetDisplayPreferences(LoadConfig()));
    dpi = dpi ? dpi : 96u;
    bool font_changed = false;
    const auto& face = theme.ui_font_face;
    if (!menu_font_ || preferences.text_scale != menu_preferences_.text_scale || dpi != menu_dpi_ ||
        LogFontFaceNameView(face) != LogFontFaceNameView(menu_font_face_)) {
        const int height = ScaleDisplayPx(kFluentBodyFontSize, dpi, preferences.text_scale);
        auto font = LogFontFaceNameView(face).empty() ? CreateUiFont(height) : CreateUiFont(face, height);
        if (font) {
            menu_font_ = std::move(font);
            menu_font_face_ = face;
            font_changed = true;
        }
        else Report("CreateUiFont(menu)");
    }
    menu_preferences_ = preferences;
    menu_dpi_ = dpi;
    menu_appearance_ = window_preferences ? theme.appearance : ResolveDisplayAppearance(preferences);
    if (font_changed) {
        InvalidateMenuMeasurements(menu_items_);
        if (context_items_) InvalidateMenuMeasurements(*context_items_);
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        menu_max_height_ = gsl::narrow_cast<UINT>(std::max(1L, monitor.rcWork.bottom - monitor.rcWork.top));
    const COLORREF background = menu_appearance_.system_high_contrast ? ::GetSysColor(COLOR_MENU) :
        menu_appearance_.palette.window_bg;
    auto brush = ScopedGdiHandle<HBRUSH>(::CreateSolidBrush(background));
    if (brush) {
        // Update every menu before releasing its previous borrowed background brush.
        auto previous = std::move(menu_brush_);
        menu_brush_ = std::move(brush);
        ApplyMenuBackground(::GetMenu(window));
        if (context_items_) {
            for (const auto& item : *context_items_)
                ApplyMenuBackground(item->menu);
        }
    } else Report("CreateSolidBrush(menu)");
}

void ToolCommandUi::InvalidateMenuMeasurements(const MenuItems& items) noexcept {
    for (const auto& item : items) {
        const int count = ::GetMenuItemCount(item->menu);
        if (count < 0) {
            Report("GetMenuItemCount", ::GetLastError());
            continue;
        }
        for (int index = 0; index < count; ++index) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID | MIIM_STATE | MIIM_DATA;
            const auto position = gsl::narrow_cast<UINT>(index);
            if (!::GetMenuItemInfoW(item->menu, position, TRUE, &info)) {
                Report("GetMenuItemInfoW(measurements)", ::GetLastError());
                break;
            }
            if (info.dwItemData != reinterpret_cast<ULONG_PTR>(item.get())) continue;
            info.fMask |= MIIM_STRING;
            info.dwTypeData = item->text.data();
            // Recreate the native item to discard cached dimensions, preserving its submenu and state.
            if (!::RemoveMenu(item->menu, position, MF_BYPOSITION)) {
                Report("RemoveMenu(measurements)", ::GetLastError());
                break;
            }
            ScopedMenu submenu(info.hSubMenu);
            if (!::InsertMenuItemW(item->menu, position, TRUE, &info))
                Report("InsertMenuItemW(measurements)", ::GetLastError());
            else std::ignore = submenu.Detach();
            break;
        }
    }
}

void ToolCommandUi::ApplyMenuBackground(HMENU menu) noexcept {
    if (!menu || !menu_brush_) return;
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND | MIM_MAXHEIGHT | MIM_APPLYTOSUBMENUS;
    info.hbrBack = menu_brush_.Get();
    info.cyMax = menu_max_height_;
    if (!::SetMenuInfo(menu, &info))
        Report("SetMenuInfo", ::GetLastError());
}

const ToolCommandUi::MenuItem* ToolCommandUi::FindMenuItem(ULONG_PTR data) const noexcept {
    const auto find = [data](const MenuItems& items) noexcept -> const MenuItem* {
        for (const auto& item : items) {
            if (reinterpret_cast<ULONG_PTR>(item.get()) == data) return item.get();
        }
        return nullptr;
    };
    if (context_items_) {
        if (const auto* item = find(*context_items_)) return item;
    }
    return find(menu_items_);
}

HFONT ToolCommandUi::MenuItemFont(const MenuItem& item) const noexcept {
    if (item.font) return item.font;
    return menu_font_ ? menu_font_.Get() : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

LRESULT ToolCommandUi::MeasureMenuItem(HWND window, MEASUREITEMSTRUCT& measure, const MenuItem& item) const noexcept {
    if (item.separator) {
        measure.itemWidth = gsl::narrow_cast<UINT>(MenuScale(kMenuCheckGutter + kMenuPaddingX));
        measure.itemHeight = gsl::narrow_cast<UINT>(MenuScale(kMenuSeparatorHeight));
        return TRUE;
    }
    const ScopedWindowDC dc(window);
    const ScopedSelectObject selected(dc.Get(), MenuItemFont(item));
    RECT bounds{};
    if (!dc || !selected ||
        !::DrawTextW(dc.Get(), item.text.c_str(), -1, &bounds, DT_CALCRECT | DT_SINGLELINE)) {
        Report("Measure menu item");
        return FALSE;
    }
    const int target = menu_preferences_.easy_reading ? ::MulDiv(kComfortMenuTarget, gsl::narrow_cast<int>(menu_dpi_), 96) : 0;
    measure.itemWidth = gsl::narrow_cast<UINT>(std::max(target,
        gsl::narrow_cast<int>(bounds.right) + MenuScale(kMenuPaddingX + (item.heading ? 0 : kMenuCheckGutter))));
    measure.itemHeight = gsl::narrow_cast<UINT>(std::max(target,
        gsl::narrow_cast<int>(bounds.bottom) + MenuScale(kMenuPaddingY)));
    return TRUE;
}

LRESULT ToolCommandUi::DrawMenuItem(const DRAWITEMSTRUCT& draw, const MenuItem& item) const noexcept {
    const auto& palette = menu_appearance_.palette;
    const bool system_contrast = menu_appearance_.system_high_contrast;
    const bool disabled = (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    const bool hot = (draw.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
    const COLORREF background = system_contrast ?
        ::GetSysColor(hot ? COLOR_HIGHLIGHT : item.heading ? COLOR_MENUBAR : COLOR_MENU) :
        hot ? palette.selection_bg : palette.window_bg;
    const COLORREF foreground = system_contrast ?
        ::GetSysColor(disabled ? COLOR_GRAYTEXT : hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT) :
        hot && menu_appearance_.high_contrast ? palette.selection_text :
        disabled ? palette.text_secondary : hot ? palette.selection_text : palette.text_primary;
    const ScopedSaveDC saved(draw.hDC);
    const ScopedSelectObject selected(draw.hDC, MenuItemFont(item));
    ::SetDCBrushColor(draw.hDC, background);
    ::FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
    if (item.separator) {
        RECT line = draw.rcItem;
        line.left += MenuScale(kMenuCheckGutter);
        line.right -= MenuScale(kMenuPaddingX / 2);
        line.top = (line.top + line.bottom) / 2;
        line.bottom = line.top + std::max(1, ::MulDiv(1, gsl::narrow_cast<int>(menu_dpi_), 96));
        ::SetDCBrushColor(draw.hDC, system_contrast ? ::GetSysColor(COLOR_MENUTEXT) : palette.border_color);
        ::FillRect(draw.hDC, &line, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
        return TRUE;
    }
    ::SetTextColor(draw.hDC, foreground);
    ::SetBkMode(draw.hDC, TRANSPARENT);
    RECT text = draw.rcItem;
    if (!item.heading) {
        text.left += MenuScale(kMenuCheckGutter);
        text.right -= MenuScale(kMenuPaddingX / 2);
    }
    ::DrawTextW(draw.hDC, item.text.c_str(), -1, &text,
        (item.heading ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE |
        ((draw.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    const bool checked = (draw.itemState & ODS_CHECKED) != 0;
    const bool disabled_marker = disabled && menu_appearance_.high_contrast && !checked;
    if (!item.heading && (checked || disabled_marker)) {
        const int size = MenuScale(checked && item.radio ? 6 : 12);
        const int x = draw.rcItem.left + (MenuScale(kMenuCheckGutter) - size) / 2;
        const int y = draw.rcItem.top + (draw.rcItem.bottom - draw.rcItem.top - size) / 2;
        const ScopedGdiHandle<HPEN> pen(::CreatePen(PS_SOLID, std::max(1, MenuScale(2)), foreground));
        const ScopedSelectObject selected_pen(draw.hDC, pen.Get());
        ::SetDCBrushColor(draw.hDC, foreground);
        const ScopedSelectObject brush(draw.hDC, ::GetStockObject(DC_BRUSH));
        if (pen && selected_pen && brush) {
            // A slash supplements native disabled state when high-contrast colors cannot dim the label.
            if (disabled_marker) {
                const std::array<POINT, 2> slash{{{x, y + size}, {x + size, y}}};
                ::Polyline(draw.hDC, slash.data(), gsl::narrow_cast<int>(slash.size()));
            } else if (item.radio) ::Ellipse(draw.hDC, x, y, x + size, y + size);
            else {
                const std::array<POINT, 3> check{{{x, y + size / 2}, {x + size / 3, y + size},
                    {x + size, y}}};
                ::Polyline(draw.hDC, check.data(), gsl::narrow_cast<int>(check.size()));
            }
        }
    }
    if ((draw.itemState & ODS_FOCUS) != 0 && (draw.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT focus = draw.rcItem;
        ::InflateRect(&focus, -2, -2);
        ::DrawFocusRect(draw.hDC, &focus);
    }
    return TRUE;
}

std::optional<LRESULT> ToolCommandUi::HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                                    const FluentD2DThemeContext& theme) {
    if (message == WM_ENTERMENULOOP || message == WM_DPICHANGED) {
        RefreshMenuAppearance(window, message == WM_DPICHANGED ? HIWORD(wparam) : theme.dpi, theme);
        if (message == WM_ENTERMENULOOP) ::DrawMenuBar(window);
    }
    if (message == WM_MENUCHAR) {
        const HMENU menu = reinterpret_cast<HMENU>(lparam);
        const auto key = std::towupper(gsl::narrow_cast<wchar_t>(LOWORD(wparam)));
        std::vector<UINT> matches;
        int selected = -1;
        const int count = ::GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_DATA | MIIM_STATE;
            if (!::GetMenuItemInfoW(menu, gsl::narrow_cast<UINT>(i), TRUE, &info)) continue;
            const auto* item = FindMenuItem(info.dwItemData);
            if (!item || item->separator || (info.fState & MFS_DISABLED) != 0) continue;
            if ((info.fState & MFS_HILITE) != 0) selected = i;
            wchar_t mnemonic = item->mnemonic;
            if (!mnemonic) {
                for (size_t j = 0; j + 1 < item->text.size(); ++j) {
                    if (gsl::at(item->text, j) != L'&') continue;
                    if (gsl::at(item->text, j + 1) == L'&') { ++j; continue; }
                    mnemonic = gsl::at(item->text, j + 1);
                    break;
                }
                if (!mnemonic && !item->text.empty()) mnemonic = item->text.front();
            }
            if (mnemonic && key == std::towupper(mnemonic))
                matches.push_back(gsl::narrow_cast<UINT>(i));
        }
        if (!matches.empty()) {
            const auto next = std::ranges::find_if(matches, [selected](UINT index) noexcept {
                return gsl::narrow_cast<int>(index) > selected;
            });
            return MAKELRESULT(next == matches.end() ? matches.front() : *next,
                matches.size() == 1 ? MNC_EXECUTE : MNC_SELECT);
        }
    }
    if (message == WM_MEASUREITEM && lparam) {
        auto& measure = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure.CtlType == ODT_MENU) {
            if (const auto* item = FindMenuItem(measure.itemData)) {
                if (menu_dpi_ != theme.dpi) RefreshMenuAppearance(window, theme.dpi, theme);
                return MeasureMenuItem(window, measure, *item);
            }
        }
    }
    if (message == WM_DRAWITEM && lparam) {
        const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (draw.CtlType == ODT_MENU) {
            if (const auto* item = FindMenuItem(draw.itemData)) return DrawMenuItem(draw, *item);
        }
    }
    if (message == WM_NOTIFY && lparam) {
        const auto& header = *reinterpret_cast<const NMHDR*>(lparam);
        if (header.code == TTN_GETDISPINFOA || header.code == TTN_GETDISPINFOW) {
            for (const auto& toolbar : toolbars_) {
                const auto tooltip = reinterpret_cast<const HWND__*>(
                    ::SendMessageW(toolbar.window, TB_GETTOOLTIPS, 0, 0));
                if (tooltip && header.hwndFrom == tooltip) {
                    const auto length = ::SendMessageW(toolbar.window, TB_GETBUTTONTEXTW, header.idFrom, 0);
                    if (length < 0) {
                        return 0;
                    }
                    tooltip_buffer_.assign(gsl::narrow_cast<size_t>(length) + 1, L'\0');
                    ::SendMessageW(toolbar.window, TB_GETBUTTONTEXTW, header.idFrom,
                                   reinterpret_cast<LPARAM>(tooltip_buffer_.data()));
                    tooltip_buffer_.resize(gsl::narrow_cast<size_t>(length));
                    if (header.code == TTN_GETDISPINFOW) {
                        auto& disp = *reinterpret_cast<NMTTDISPINFOW*>(lparam);
                        disp.lpszText = tooltip_buffer_.data();
                    }
                    return 0;
                }
            }
        }
        const auto toolbar = std::ranges::find(toolbars_, header.hwndFrom, &Toolbar::window);
        if (toolbar == toolbars_.end())
            return std::nullopt;
        if (header.code == NM_CUSTOMDRAW)
            return DrawToolbar(*reinterpret_cast<NMCUSTOMDRAW*>(lparam), *toolbar, theme);
        if (header.code == TBN_GETINFOTIPW) {
            auto& tip = *reinterpret_cast<NMTBGETINFOTIPW*>(lparam);
            const auto length = ::SendMessageW(header.hwndFrom, TB_GETBUTTONTEXTW, tip.iItem, 0);
            if (length < 0) {
                Report("TB_GETBUTTONTEXTW", ERROR_NOT_FOUND);
                return 0;
            }
            std::wstring text(gsl::narrow_cast<size_t>(length) + 1, L'\0');
            ::SendMessageW(header.hwndFrom, TB_GETBUTTONTEXTW, tip.iItem, reinterpret_cast<LPARAM>(text.data()));
            if (tip.pszText && tip.cchTextMax > 0) {
                wcsncpy_s(tip.pszText, gsl::narrow_cast<size_t>(tip.cchTextMax), text.c_str(), _TRUNCATE);
            }
            return 0;
        }
    }
    return std::nullopt;
}

} // namespace qiqi
