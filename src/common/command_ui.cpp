#include "common/command_ui.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <system_error>
#include <uxtheme.h>

namespace qiqi {
namespace {
constexpr int kToolbarHeight = 40;
constexpr int kToolbarButtonSize = 36;
constexpr int kToolbarIconSize = 20;
constexpr int kCompactIconSize = 16;
constexpr int kCompactIconPadding = 4;
constexpr int kCompactIconStroke = 1;
constexpr int kToolbarIconStroke = 2;
constexpr int kFocusInsetPx = 2;
constexpr int kMenuHeadingPaddingX = 20;
constexpr int kMenuHeadingPaddingY = 8;
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

void ToolCommandUi::SetMenu(HWND window, std::span<const ToolMenu> menus) {
    ScopedMenu menu(::CreateMenu());
    if (!menu)
        Fail("CreateMenu", ::GetLastError());
    std::vector<std::wstring> headings;
    std::vector<wchar_t> mnemonics;
    for (const auto& group : menus) {
        ScopedMenu popup(::CreatePopupMenu());
        if (!popup)
            Fail("CreatePopupMenu", ::GetLastError());
        for (const auto& command : group.commands) {
            const UINT flags = command.id == 0 ? MF_SEPARATOR
                                               : MF_STRING | (command.enabled ? MF_ENABLED : MF_GRAYED) |
                                                     (command.checked ? MF_CHECKED : 0);
            if (!::AppendMenuW(popup.Get(), flags, command.id, command.id == 0 ? nullptr : command.text)) {
                Fail("AppendMenuW", ::GetLastError());
            }
            if (group.radio && command.id != 0) {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_FTYPE;
                info.fType = MFT_RADIOCHECK;
                if (!::SetMenuItemInfoW(popup.Get(), command.id, FALSE, &info)) {
                    Fail("SetMenuItemInfoW(radio)", ::GetLastError());
                }
            }
        }
        const auto index = gsl::narrow_cast<UINT>(headings.size());
        if (!::AppendMenuW(menu.Get(), MF_POPUP, reinterpret_cast<UINT_PTR>(popup.Get()), group.text)) {
            Fail("AppendMenuW(popup)", ::GetLastError());
        }
        std::ignore = popup.Detach();
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA;
        info.fType = MFT_OWNERDRAW;
        info.dwItemData = index;
        if (!::SetMenuItemInfoW(menu.Get(), index, TRUE, &info)) {
            Fail("SetMenuItemInfoW(heading)", ::GetLastError());
        }
        headings.emplace_back(group.text);
        mnemonics.push_back(group.mnemonic);
    }
    const HMENU previous = ::GetMenu(window);
    headings_.swap(headings);
    mnemonics_.swap(mnemonics);
    if (!::SetMenu(window, menu.Get())) {
        const DWORD error = ::GetLastError();
        headings_.swap(headings);
        mnemonics_.swap(mnemonics);
        Fail("SetMenu", error);
    }
    std::ignore = menu.Detach(); // The window owns the attached menu.
    const ScopedMenu old_menu(previous);
    ::DrawMenuBar(window);
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
        item.fsState = (command.enabled ? TBSTATE_ENABLED : 0) | (command.checked ? TBSTATE_CHECKED : 0);
        item.fsStyle = BTNS_BUTTON;
        item.dwData = std::to_underlying(command.icon);
        item.iString = reinterpret_cast<INT_PTR>(command.text);
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
    ::MoveWindow(toolbar, 0, 0, width, theme.ScalePx(kToolbarHeight), TRUE);
    const int size = theme.ScalePx(kToolbarButtonSize);
    ::SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    ::SendMessageW(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(size, size));
    ::SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    RECT rect{};
    ::GetWindowRect(toolbar, &rect);
    return rect.bottom - rect.top;
}

void ToolCommandUi::ApplyTheme(HWND window) noexcept {
    const bool contrast = IsHighContrastEnabled();
    const auto& palette = GetCurrentThemePalette();
    ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(contrast ? ::GetSysColor(COLOR_MENUBAR) : palette.window_bg));
    if (background && ::GetMenu(window)) {
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = background.Get();
        if (::SetMenuInfo(::GetMenu(window), &info))
            menu_brush_ = std::move(background);
        else
            Report("SetMenuInfo", ::GetLastError());
    } else if (!background) {
        Report("CreateSolidBrush(menu)");
    }
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
    const bool contrast = IsHighContrastEnabled();
    const auto& palette = GetCurrentThemePalette();
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
    button.dwMask = TBIF_LPARAM;
    if (::SendMessageW(toolbar.window, TB_GETBUTTONINFOW, draw.dwItemSpec, reinterpret_cast<LPARAM>(&button)) < 0) {
        Report("TB_GETBUTTONINFOW(draw)", ERROR_NOT_FOUND);
        return CDRF_DODEFAULT;
    }
    const bool hot = (draw.uItemState & (CDIS_HOT | CDIS_SELECTED | CDIS_CHECKED)) != 0;
    const bool disabled = (draw.uItemState & CDIS_DISABLED) != 0;
    const COLORREF background =
        contrast ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE) : (hot ? palette.hover_bg : palette.window_bg);
    const COLORREF foreground =
        contrast ? ::GetSysColor(disabled ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT))
                 : (disabled ? palette.grip_dot : palette.text_primary);
    fill(draw.rc, background);
    const int size = toolbar.compact
                         ? std::max(1, std::min(theme.ScalePx(kCompactIconSize),
                                                gsl::narrow_cast<int>(draw.rc.bottom - draw.rc.top) - theme.ScalePx(kCompactIconPadding)))
                         : theme.ScalePx(kToolbarIconSize);
    DrawIcon(draw.hdc, draw.rc, gsl::narrow_cast<ToolIcon>(button.lParam), size,
             std::max(1, theme.ScalePx(toolbar.compact ? kCompactIconStroke : kToolbarIconStroke)), foreground);
    if (draw.uItemState & CDIS_FOCUS) {
        RECT focus = draw.rc;
        ::InflateRect(&focus, -kFocusInsetPx, -kFocusInsetPx);
        ::DrawFocusRect(draw.hdc, &focus);
    }
    return CDRF_SKIPDEFAULT;
}

std::optional<LRESULT> ToolCommandUi::HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam, HFONT font,
                                                    const FluentD2DThemeContext& theme) {
    if (message == WM_MENUCHAR && reinterpret_cast<HMENU>(lparam) == ::GetMenu(window)) {
        const auto key = gsl::narrow_cast<wchar_t>(LOWORD(wparam));
        for (size_t i = 0; i < mnemonics_.size(); ++i) {
            const auto mnemonic = gsl::at(mnemonics_, i);
            if (key == mnemonic || key == mnemonic - L'a' + L'A')
                return MAKELRESULT(i, MNC_EXECUTE);
        }
    }
    if (message == WM_MEASUREITEM && lparam) {
        auto& measure = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure.CtlType == ODT_MENU && measure.itemData < headings_.size()) {
            const ScopedWindowDC dc(window);
            const ScopedSelectObject selected(dc.Get(), font);
            RECT bounds{};
            if (!dc || !selected ||
                !::DrawTextW(dc.Get(), gsl::at(headings_, measure.itemData).c_str(), -1, &bounds,
                             DT_CALCRECT | DT_SINGLELINE)) {
                Report("Measure menu heading");
                return FALSE;
            }
            measure.itemWidth = gsl::narrow_cast<UINT>(bounds.right + theme.ScalePx(kMenuHeadingPaddingX));
            measure.itemHeight = gsl::narrow_cast<UINT>(bounds.bottom + theme.ScalePx(kMenuHeadingPaddingY));
            return TRUE;
        }
    }
    if (message == WM_DRAWITEM && lparam) {
        const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (draw.CtlType == ODT_MENU && draw.itemData < headings_.size()) {
            const bool contrast = IsHighContrastEnabled();
            const auto& palette = GetCurrentThemePalette();
            const bool hot = (draw.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
            const ScopedSaveDC saved(draw.hDC);
            const ScopedSelectObject selected(draw.hDC, font);
            ::SetDCBrushColor(draw.hDC, contrast ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_MENUBAR)
                                                 : (hot ? palette.hover_bg : palette.window_bg));
            ::FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
            ::SetTextColor(draw.hDC,
                           contrast ? ::GetSysColor(hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT) : palette.text_primary);
            ::SetBkMode(draw.hDC, TRANSPARENT);
            RECT rect = draw.rcItem;
            ::DrawTextW(draw.hDC, gsl::at(headings_, draw.itemData).c_str(), -1, &rect,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | ((draw.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
            return TRUE;
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
