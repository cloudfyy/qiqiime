// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

static_assert(kRefreshTimerId == 1 && kRefreshIntervalMs == 500);
static_assert(layout::kInitialWindowSize.cx == 1020 && layout::kInitialWindowSize.cy == 680);
static_assert(layout::kSourceLabel.left == 12 && layout::kSourceLabel.top == 4 &&
              layout::kSourceLabel.right == 68 && layout::kSourceLabel.bottom == 28);
static_assert(layout::kSourceCombo.left == 72 && layout::kSourceCombo.top == 0 &&
              layout::kSourceCombo.right == 237 && layout::kSourceCombo.bottom == 220);
static_assert(layout::kSearchLabel.left == 250 && layout::kSearchLabel.top == 4 &&
              layout::kSearchLabel.right == 314 && layout::kSearchLabel.bottom == 28);
static_assert(layout::kSearchEditLeft == 318 && layout::kSearchEditRightReserve == 330);
static_assert(layout::kSearchEditHeight == 28 && layout::kWindowMargin == 12);
static_assert(layout::kFilterSectionHeight == 40 && layout::kListMinimumHeight == 80);
static_assert(layout::kListBottomInset == 40 && layout::kStatusBottomOffset == 32 && layout::kStatusHeight == 24);
static_assert(layout::kInlineSaveButtonSize == 26 && layout::kEditIconSize == 16);
static_assert(layout::kEditIconPadding == 4 && layout::kEditIconStrokeWidth == 1 && layout::kEditorCellInsetPx == 1);

void CheckLayout(const DictManagerWindow& window, UINT dpi) {
    QIQI_TEST_CHECK(Header_GetItemCount(ListView_GetHeader(Child(window, List))) ==
        std::to_underlying(ColumnId::Count));
    const int edit_width = ListView_GetColumnWidth(Child(window, List), std::to_underlying(ColumnId::Edit));
    const int expected_edit_width = ScalePxByDpi(40, dpi);
    if (edit_width != expected_edit_width) {
        std::cerr << std::format(
            "[DPI] Edit column mismatch: requested={}, window={}, list={}, width={}, expected={}\n",
            dpi, ::GetDpiForWindow(window.Handle()), ::GetDpiForWindow(Child(window, List)),
            edit_width, expected_edit_width);
    }
    QIQI_TEST_CHECK(edit_width == expected_edit_width);
    constexpr std::array labels{SourceLabel, SearchLabel};
    for (const auto id : labels) {
        const HWND child = Child(window, id);
        const ScopedWindowDC dc(child);
        QIQI_TEST_CHECK(dc);
        const ScopedSelectObject font(dc.Get(), reinterpret_cast<HFONT>(::SendMessageW(child, WM_GETFONT, 0, 0)));
        QIQI_TEST_CHECK(font);
        const auto text = ControlText(child);
        SIZE size{};
        QIQI_TEST_CHECK(::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(text.size()), &size));
        RECT rect{};
        QIQI_TEST_CHECK(::GetClientRect(child, &rect));
        QIQI_TEST_CHECK(size.cx <= rect.right && size.cy <= rect.bottom);
    }
    const HWND toolbar = Child(window, Toolbar);
    QIQI_TEST_CHECK(toolbar != nullptr);
    const ScopedWindowDC dc(toolbar);
    QIQI_TEST_CHECK(dc);
    const auto toolbar_font = reinterpret_cast<HFONT>(::SendMessageW(toolbar, WM_GETFONT, 0, 0));
    const ScopedSelectObject font(dc.Get(), toolbar_font);
    QIQI_TEST_CHECK(font);
    LOGFONTW logical_font{};
    QIQI_TEST_CHECK(::GetObjectW(toolbar_font, sizeof(logical_font), &logical_font) == sizeof(logical_font));
    QIQI_TEST_CHECK(-logical_font.lfHeight == ScalePxByDpi(14, dpi));
    const HMENU menu = ::GetMenu(window.Handle());
    QIQI_TEST_CHECK(menu && ::GetMenuItemCount(menu) == 4);
    for (UINT i = 0; i < 4; ++i) {
        std::array<wchar_t, 64> heading{};
        QIQI_TEST_CHECK(::GetMenuStringW(menu, i, heading.data(), gsl::narrow_cast<int>(heading.size()), MF_BYPOSITION));
        RECT bounds{};
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), heading.data(), -1, &bounds, DT_CALCRECT | DT_SINGLELINE));
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemData = i;
        QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)));
        QIQI_TEST_CHECK(measure.itemWidth >= gsl::narrow_cast<UINT>(bounds.right));
        QIQI_TEST_CHECK(measure.itemHeight >= gsl::narrow_cast<UINT>(bounds.bottom));
        constexpr std::array keys{L'f', L'e', L'l', L'h'};
        QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MENUCHAR, gsl::at(keys, i),
            reinterpret_cast<LPARAM>(menu)) == MAKELRESULT(i, MNC_EXECUTE));
    }
    constexpr std::array commands{
        Reload, NewWord, Copy, Import, Export, Pin, Delete, Promote};
    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) == gsl::narrow_cast<LRESULT>(commands.size()));
    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETTEXTROWS, 0, 0) == 0);
    std::vector<RECT> items;
    for (const auto id : commands) {
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) ==
            gsl::narrow_cast<LRESULT>(items.size()));
        QIQI_TEST_CHECK(Child(window, id) == nullptr);
        std::array<wchar_t, 256> text{};
        const LRESULT length = ::SendMessageW(toolbar, TB_GETBUTTONTEXTW,
            std::to_underlying(id), reinterpret_cast<LPARAM>(text.data()));
        QIQI_TEST_CHECK(length > 0 && length < gsl::narrow_cast<LRESULT>(text.size()));
        QIQI_TEST_CHECK(MenuText(menu, id) == text.data());
        TBBUTTONINFO button{};
        button.cbSize = sizeof(button);
        button.dwMask = TBIF_STYLE | TBIF_LPARAM;
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTONINFOW,
            std::to_underlying(id), reinterpret_cast<LPARAM>(&button)) >= 0);
        QIQI_TEST_CHECK((button.fsStyle & (BTNS_SHOWTEXT | BTNS_AUTOSIZE)) == 0);
        if (id == Export) {
            QIQI_TEST_CHECK(gsl::narrow_cast<ToolIcon>(button.lParam) == ToolIcon::Save);
        } else if (id == Import) {
            QIQI_TEST_CHECK(gsl::narrow_cast<ToolIcon>(button.lParam) == ToolIcon::Import);
        }
        std::array<wchar_t, 256> tooltip{};
        NMTBGETINFOTIPW tip{};
        tip.hdr.hwndFrom = toolbar;
        tip.hdr.idFrom = std::to_underlying(Toolbar);
        tip.hdr.code = TBN_GETINFOTIPW;
        tip.iItem = std::to_underlying(id);
        tip.pszText = tooltip.data();
        tip.cchTextMax = gsl::narrow_cast<int>(tooltip.size());
        ::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar), reinterpret_cast<LPARAM>(&tip));
        QIQI_TEST_CHECK(std::wstring_view(tooltip.data()) == text.data());
        const auto rect = ToolbarItemRect(window, id);
        QIQI_TEST_CHECK(rect.right - rect.left == ScalePxByDpi(36, dpi));
        QIQI_TEST_CHECK(rect.bottom - rect.top == ScalePxByDpi(36, dpi));
        RECT toolbar_client{};
        QIQI_TEST_CHECK(::GetClientRect(toolbar, &toolbar_client));
        QIQI_TEST_CHECK(rect.left >= 0 && rect.top >= 0 &&
            rect.right <= toolbar_client.right && rect.bottom <= toolbar_client.bottom);
        for (const auto& other : items) {
            RECT intersection{};
            QIQI_TEST_CHECK(!::IntersectRect(&intersection, &rect, &other));
        }
        items.push_back(rect);
    }
    for (const auto id : {Backup, Restore, ClearFreq, About, AutoCode, SelectAll, Exit,
             LanguageAuto, LanguageZh, LanguageEn}) {
        QIQI_TEST_CHECK(!MenuText(menu, id).empty());
    }

    RECT client{};
    QIQI_TEST_CHECK(::GetClientRect(window.Handle(), &client));
    POINT origin{};
    QIQI_TEST_CHECK(::ClientToScreen(window.Handle(), &origin));
    ::OffsetRect(&client, origin.x, origin.y);
    std::vector<RECT> controls;
    for (HWND child = ::GetWindow(window.Handle(), GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
        std::array<wchar_t, 64> class_name{};
        QIQI_TEST_CHECK(::GetClassNameW(child, class_name.data(), gsl::narrow_cast<int>(class_name.size())));
        QIQI_TEST_CHECK(_wcsicmp(class_name.data(), L"BUTTON") != 0);
        const int id = ::GetDlgCtrlID(child);
        if (id == std::to_underlying(EditCodeInput) || id == std::to_underlying(EditWordInput) ||
            id == std::to_underlying(InlineSave)) continue;
        RECT rect{};
        QIQI_TEST_CHECK(::GetWindowRect(child, &rect));
        QIQI_TEST_CHECK(rect.left >= client.left && rect.top >= client.top);
        QIQI_TEST_CHECK(rect.right <= client.right && rect.bottom <= client.bottom);
        for (const auto& other : controls) {
            RECT intersection{};
            QIQI_TEST_CHECK(!::IntersectRect(&intersection, &rect, &other));
        }
        controls.push_back(rect);
    }
}

void CheckCommandAppearance(const DictManagerWindow& window, UINT dpi, bool dark, bool contrast) {
    SetThemeDarkOverrideForTest(dark);
    SetHighContrastOverrideForTest(contrast);
    const auto restore = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    ::SendMessageW(window.Handle(), WM_SYSCOLORCHANGE, 0, 0);
    CheckLayout(window, dpi);
    const ScopedWindowDC window_dc(window.Handle());
    QIQI_TEST_CHECK(window_dc);
    const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
    QIQI_TEST_CHECK(dc);
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(window_dc.Get(), 512, 96));
    QIQI_TEST_CHECK(bitmap);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(selected);
    const HWND list = Child(window, List);
    RECT edit_cell{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
    QIQI_TEST_CHECK(ListView_GetItemRect(list, 0, &edit_cell, LVIR_BOUNDS));
    edit_cell.right = edit_cell.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit));
    ::SendMessageW(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    size_t edit_ink = 0;
    for (int y = edit_cell.top + 2; y < edit_cell.bottom - 2; ++y) {
        for (int x = edit_cell.left + 2; x < edit_cell.right - 2; ++x) {
            if (::GetPixel(dc.Get(), x, y) == ListView_GetTextColor(list)) ++edit_ink;
        }
    }
    QIQI_TEST_CHECK(edit_ink > 10);
    const auto& palette = GetThemePalette(dark);
    NMTBCUSTOMDRAW toolbar{};
    toolbar.nmcd.hdr.hwndFrom = Child(window, Toolbar);
    toolbar.nmcd.hdr.idFrom = std::to_underlying(Toolbar);
    toolbar.nmcd.hdr.code = NM_CUSTOMDRAW;
    toolbar.nmcd.hdc = dc.Get();
    toolbar.nmcd.rc = {0, 0, 512, 96};
    toolbar.nmcd.dwDrawStage = CDDS_PREPAINT;
    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar),
        reinterpret_cast<LPARAM>(&toolbar)) == CDRF_NOTIFYITEMDRAW);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) ==
        (contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg));
    toolbar.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
    constexpr std::array commands{
        Reload, Copy, Import, Export, NewWord, Pin, Delete, Promote, SaveWord};
    for (const UINT state : {0u, UINT{CDIS_HOT}, UINT{CDIS_SELECTED}, UINT{CDIS_DISABLED}, UINT{CDIS_FOCUS}}) {
        toolbar.nmcd.uItemState = state;
        const bool hot = (state & (CDIS_HOT | CDIS_SELECTED)) != 0;
        const COLORREF background = contrast
            ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE)
            : (hot ? palette.hover_bg : palette.window_bg);
        const COLORREF foreground = contrast
            ? ::GetSysColor((state & CDIS_DISABLED) ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT))
            : ((state & CDIS_DISABLED) ? palette.grip_dot : palette.text_primary);
        std::vector<uint64_t> shapes;
        for (const auto id : commands) {
            toolbar.nmcd.hdr.hwndFrom = Child(window, id == SaveWord ? InlineSave : Toolbar);
            toolbar.nmcd.dwItemSpec = std::to_underlying(id);
            QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar),
                reinterpret_cast<LPARAM>(&toolbar)) == CDRF_SKIPDEFAULT);
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == background);
            const int size = ScalePxByDpi(20, dpi);
            const int stroke = ScalePxByDpi(2, dpi);
            const int left = (512 - size) / 2 - stroke;
            const int top = (96 - size) / 2 - stroke;
            size_t ink = 0;
            uint64_t shape = 14695981039346656037ull;
            for (int y = top; y < top + size + 2 * stroke; ++y) {
                for (int x = left; x < left + size + 2 * stroke; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel == background || pixel == foreground);
                    const bool filled = pixel == foreground;
                    if (filled) ++ink;
                    shape = (shape ^ gsl::narrow_cast<uint64_t>(filled)) * 1099511628211ull;
                }
            }
            QIQI_TEST_CHECK(ink > 10);
            QIQI_TEST_CHECK(std::ranges::find(shapes, shape) == shapes.end());
            shapes.push_back(shape);
        }
    }

    MENUINFO menu_info{};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_BACKGROUND;
    QIQI_TEST_CHECK(::GetMenuInfo(::GetMenu(window.Handle()), &menu_info));
    LOGBRUSH brush{};
    QIQI_TEST_CHECK(::GetObjectW(menu_info.hbrBack, sizeof(brush), &brush) == sizeof(brush));
    QIQI_TEST_CHECK(brush.lbColor == (contrast ? ::GetSysColor(COLOR_MENUBAR) : palette.window_bg));
    DRAWITEMSTRUCT menu{};
    menu.CtlType = ODT_MENU;
    menu.itemData = 2;
    menu.itemState = ODS_SELECTED;
    menu.hDC = dc.Get();
    menu.rcItem = {0, 0, 512, 96};
    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&menu)));
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) ==
        (contrast ? ::GetSysColor(COLOR_HIGHLIGHT) : palette.hover_bg));
}

} // namespace qiqi::dict_manager::tests
