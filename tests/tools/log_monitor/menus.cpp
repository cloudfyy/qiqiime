// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

void TestScrollIcons() {
    const ToolCommandUi commands(
        +[](void*, const char*, ULONG) noexcept { QIQI_TEST_CHECK(false); }, nullptr);
    const ScopedWindowDC screen(nullptr);
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), 64, 64));
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(RGB(255, 255, 255)));
    QIQI_TEST_CHECK(screen && dc && bitmap && background);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(selected);
    constexpr RECT rect{0, 0, 64, 64};
    constexpr std::array icons{ToolIcon::Stop, ToolIcon::Pause, ToolIcon::Resume};
    for (const int size : {16, 20, 24, 32}) {
        std::array<std::vector<uint8_t>, icons.size()> masks;
        for (size_t index = 0; index < icons.size(); ++index) {
            QIQI_TEST_CHECK(::FillRect(dc.Get(), &rect, background.Get()));
            commands.DrawIcon(dc.Get(), rect, gsl::at(icons, index), size,
                ::MulDiv(2, size, 20), RGB(0, 0, 0));
            auto& mask = gsl::at(masks, index);
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel == RGB(0, 0, 0) || pixel == RGB(255, 255, 255));
                    mask.push_back(pixel == RGB(0, 0, 0));
                }
            }
        }
        for (size_t first = 0; first < masks.size(); ++first) {
            for (size_t second = first + 1; second < masks.size(); ++second) {
                size_t difference = 0;
                for (size_t pixel = 0; pixel < gsl::at(masks, first).size(); ++pixel) {
                    if (gsl::at(gsl::at(masks, first), pixel) != gsl::at(gsl::at(masks, second), pixel)) {
                        ++difference;
                    }
                }
                QIQI_TEST_CHECK(difference >= 10);
            }
        }
    }
}

void TestMenusAndToolbar(const RECT& work_area) {
    SetMonitorMetricsOverrideForTest(MonitorMetrics{work_area, 96});
    const auto restore = gsl::finally([]() noexcept {
        SetMonitorMetricsOverrideForTest(std::nullopt);
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
        }
    });
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND toolbar = Child(window, Toolbar);
    constexpr std::array commands{Start, Export, Clear, Copy, Pause, Source, MetricsView, DiagnosticsView};
    constexpr size_t view_group_begin = 5;
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        for (const int width : {760, 920, 1160}) {
            RECT proposed{0, 0, ScalePxByDpi(width, dpi), ScalePxByDpi(780, dpi)};
            ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&proposed));
            for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn, UiLanguage::Auto}) {
                SelectLanguage(window, language);
                const HMENU menu = ::GetMenu(window.Handle());
                QIQI_TEST_CHECK(menu && ::GetMenuItemCount(menu) == 5);
                const HMENU view_menu = ::GetSubMenu(menu, 2);
                constexpr std::array views{Source, MetricsView, DiagnosticsView};
                for (size_t i = 0; i < views.size(); ++i) {
                    QIQI_TEST_CHECK(::GetMenuItemID(view_menu, gsl::narrow_cast<int>(i)) ==
                        gsl::narrow_cast<UINT>(std::to_underlying(gsl::at(views, i))));
                    const bool checked = gsl::at(views, i) == Source;
                    QIQI_TEST_CHECK(((::GetMenuState(menu, std::to_underlying(gsl::at(views, i)), MF_BYCOMMAND) &
                        MF_CHECKED) != 0) == checked);
                    QIQI_TEST_CHECK(((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(gsl::at(views, i)), 0) &
                        TBSTATE_CHECKED) != 0) == checked);
                }
                constexpr std::array keys{L'f', L'e', L'v', L'l', L'h'};
                for (size_t i = 0; i < keys.size(); ++i) {
                    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MENUCHAR, gsl::at(keys, i),
                                                   reinterpret_cast<LPARAM>(menu)) == MAKELRESULT(i, MNC_EXECUTE));
                    MEASUREITEMSTRUCT measure{};
                    measure.CtlType = ODT_MENU;
                    measure.itemData = i;
                    QIQI_TEST_CHECK(
                        ::SendMessageW(window.Handle(), WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)));
                    QIQI_TEST_CHECK(measure.itemWidth > 0 &&
                                    measure.itemHeight >= gsl::narrow_cast<UINT>(ScalePxByDpi(14, dpi)));
                }
                for (const auto id : {LanguageAuto, LanguageZh, LanguageEn}) {
                    QIQI_TEST_CHECK(
                        (::GetMenuState(menu, std::to_underlying(id), MF_BYCOMMAND) & MF_CHECKED) ==
                        (std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(language)
                             ? UINT{MF_CHECKED}
                             : 0u));
                }
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) ==
                                gsl::narrow_cast<LRESULT>(commands.size() + 1));
#pragma warning(suppress : 26496) // TB_GETBUTTON writes through LPARAM.
                TBBUTTON separator{};
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTON, view_group_begin, reinterpret_cast<LPARAM>(&separator)));
                QIQI_TEST_CHECK((separator.fsStyle & BTNS_SEP) != 0 && separator.idCommand == 0 && separator.fsState == 0);
#pragma warning(suppress : 26496) // TB_GETITEMRECT writes through LPARAM.
                RECT separator_rect{};
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETITEMRECT, view_group_begin, reinterpret_cast<LPARAM>(&separator_rect)));
                QIQI_TEST_CHECK(separator_rect.right - separator_rect.left == ScalePxByDpi(8, dpi));
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETTEXTROWS, 0, 0) == 0);
                for (size_t i = 0; i < commands.size(); ++i) {
                    const auto id = gsl::at(commands, i);
                    QIQI_TEST_CHECK(Child(window, id) == nullptr);
                    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) ==
                                    gsl::narrow_cast<LRESULT>(i < view_group_begin ? i : i + 1));
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
                    RECT rect{};
                    QIQI_TEST_CHECK(
                        ::SendMessageW(toolbar, TB_GETRECT, std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
                    QIQI_TEST_CHECK(rect.right - rect.left == ScalePxByDpi(36, dpi));
                    QIQI_TEST_CHECK(rect.bottom - rect.top == ScalePxByDpi(36, dpi));
                    std::array<wchar_t, 256> tooltip{};
                    NMTBGETINFOTIPW tip{};
                    tip.hdr = {toolbar, std::to_underlying(Toolbar), TBN_GETINFOTIPW};
                    tip.iItem = std::to_underlying(id);
                    tip.pszText = tooltip.data();
                    tip.cchTextMax = gsl::narrow_cast<int>(tooltip.size());
                    ::SendMessageW(window.Handle(), WM_NOTIFY, tip.hdr.idFrom, reinterpret_cast<LPARAM>(&tip));
                    QIQI_TEST_CHECK(std::wstring_view(tooltip.data()) == CommandName(window, id));

                    const auto tooltip_hwnd = reinterpret_cast<HWND>(::SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0));
                    QIQI_TEST_CHECK(tooltip_hwnd != nullptr);
                    NMTTDISPINFOW disp{};
                    disp.hdr = {tooltip_hwnd, gsl::narrow_cast<UINT_PTR>(std::to_underlying(id)), TTN_GETDISPINFOW};
                    ::SendMessageW(window.Handle(), WM_NOTIFY, disp.hdr.idFrom, reinterpret_cast<LPARAM>(&disp));
                    QIQI_TEST_CHECK(disp.lpszText && std::wstring_view(disp.lpszText) == CommandName(window, id));
                    TBBUTTONINFO button{};
                    button.cbSize = sizeof(button);
                    button.dwMask = TBIF_STYLE;
                    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTONINFOW, std::to_underlying(id),
                                                   reinterpret_cast<LPARAM>(&button)) >= 0);
                    QIQI_TEST_CHECK((button.fsStyle & (BTNS_SHOWTEXT | BTNS_AUTOSIZE)) == 0);
                }
                RECT client{};
                ::GetClientRect(window.Handle(), &client);
                ::MapWindowPoints(window.Handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
                std::vector<RECT> controls;
                for (HWND child = ::GetWindow(window.Handle(), GW_CHILD); child;
                     child = ::GetWindow(child, GW_HWNDNEXT)) {
                    std::array<wchar_t, 64> name{};
                    ::GetClassNameW(child, name.data(), gsl::narrow_cast<int>(name.size()));
                    QIQI_TEST_CHECK(_wcsicmp(name.data(), L"BUTTON") != 0);
                    if ((::GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0) continue;
                    RECT bounds{}, overlap{};
                    ::GetWindowRect(child, &bounds);
                    QIQI_TEST_CHECK(bounds.left >= client.left && bounds.top >= client.top &&
                                    bounds.right <= client.right && bounds.bottom <= client.bottom);
                    for (const auto& other : controls)
                        QIQI_TEST_CHECK(!::IntersectRect(&overlap, &bounds, &other));
                    controls.push_back(bounds);
                }
                for (const auto id : {ProcessLabel, PidLabel, LevelLabel, CategoryLabel, SearchLabel}) {
                    const HWND label = Child(window, id);
                    const ScopedWindowDC dc(label);
                    const ScopedSelectObject font(dc.Get(),
                                                  reinterpret_cast<HFONT>(::SendMessageW(label, WM_GETFONT, 0, 0)));
                    const auto text = ControlText(label);
                    SIZE size{};
                    RECT bounds{};
                    ::GetClientRect(label, &bounds);
                    QIQI_TEST_CHECK(
                        ::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(text.size()), &size));
                    QIQI_TEST_CHECK(size.cx <= bounds.right && size.cy <= bounds.bottom);
                }
            }
        }
    }
    SelectLanguage(window, UiLanguage::EnUs);
    const auto icon = [&](ControlId id) {
        TBBUTTONINFO button{};
        button.cbSize = sizeof(button);
        button.dwMask = TBIF_LPARAM;
        QIQI_TEST_CHECK(
            ::SendMessageW(toolbar, TB_GETBUTTONINFOW, std::to_underlying(id), reinterpret_cast<LPARAM>(&button)) >= 0);
        return gsl::narrow_cast<ToolIcon>(button.lParam);
    };
    QIQI_TEST_CHECK(icon(Start) == ToolIcon::Start && icon(Export) == ToolIcon::Save &&
                    icon(Pause) == ToolIcon::Pause && icon(Source) == ToolIcon::Events &&
                    icon(DiagnosticsView) == ToolIcon::Diagnostics);

    // Verify elevated / non-elevated title bar indicator across languages.
    SetElevatedOverrideForTest(false);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(!ControlText(window.Handle()).contains(L"[Administrator]"));
    SetElevatedOverrideForTest(true);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(ControlText(window.Handle()).ends_with(L" [Administrator]"));
    SelectLanguage(window, UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ControlText(window.Handle()).ends_with(L" [管理员]"));
    SetElevatedOverrideForTest(std::nullopt);
    SelectLanguage(window, UiLanguage::EnUs);
    Click(window, Pause);
    QIQI_TEST_CHECK(icon(Pause) == ToolIcon::Resume);
    QIQI_TEST_CHECK(CommandName(window, Pause) == L"Resume auto-scroll");
    QIQI_TEST_CHECK((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(Pause), 0) & TBSTATE_CHECKED) != 0);
    Click(window, DiagnosticsView);
    QIQI_TEST_CHECK(icon(Source) == ToolIcon::Events);
    QIQI_TEST_CHECK(CommandName(window, Source) == L"IME events");
    QIQI_TEST_CHECK((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(DiagnosticsView), 0) & TBSTATE_CHECKED) != 0);
    const auto check_view = [&](ControlId selected_view) {
        for (const auto id : {Source, DiagnosticsView, MetricsView}) {
            const bool checked = id == selected_view;
            QIQI_TEST_CHECK(((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(id), 0) & TBSTATE_CHECKED) != 0) == checked);
            MENUITEMINFOW menu_item{};
            menu_item.cbSize = sizeof(menu_item);
            menu_item.fMask = MIIM_FTYPE | MIIM_STATE;
            QIQI_TEST_CHECK(::GetMenuItemInfoW(::GetMenu(window.Handle()), std::to_underlying(id), FALSE, &menu_item));
            QIQI_TEST_CHECK((menu_item.fType & MFT_RADIOCHECK) != 0);
            QIQI_TEST_CHECK(((menu_item.fState & MFS_CHECKED) != 0) == checked);
            TBBUTTONINFO button{};
            button.cbSize = sizeof(button);
            button.dwMask = TBIF_STYLE;
            QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTONINFOW, std::to_underlying(id),
                reinterpret_cast<LPARAM>(&button)) >= 0);
            QIQI_TEST_CHECK((button.fsStyle & BTNS_CHECKGROUP) == BTNS_CHECKGROUP);
        }
        const HMENU view_menu = ::GetSubMenu(::GetMenu(window.Handle()), 2);
        QIQI_TEST_CHECK(::GetMenuItemCount(view_menu) == 6);
    };
    for (const auto id : {Source, DiagnosticsView, MetricsView, DiagnosticsView, Source}) {
        Click(window, id);
        check_view(id);
        Click(window, id);
        check_view(id);
        ::SendMessageW(window.Handle(), WM_COMMAND, std::to_underlying(id), 0);
        check_view(id);
        for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
            SelectLanguage(window, language);
            check_view(id);
        }
    }
    Click(window, DiagnosticsView);
    check_view(DiagnosticsView);
    for (const bool dark : {false, true}) {
        for (const bool contrast : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            SetHighContrastOverrideForTest(contrast);
            ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
            const ScopedWindowDC window_dc(window.Handle());
            const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
            const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(window_dc.Get(), 80, 80));
            const ScopedSelectObject selected(dc.Get(), bitmap.Get());
            NMCUSTOMDRAW separator_draw{};
            separator_draw.hdr = {toolbar, std::to_underlying(Toolbar), NM_CUSTOMDRAW};
            separator_draw.hdc = dc.Get();
            separator_draw.rc = {0, 0, 8, 36};
            separator_draw.dwDrawStage = CDDS_ITEMPREPAINT;
            separator_draw.dwItemSpec = 0;
            QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, separator_draw.hdr.idFrom,
                reinterpret_cast<LPARAM>(&separator_draw)) == CDRF_DODEFAULT);
            std::vector<uint64_t> shapes;
            for (const auto id : commands) {
                NMCUSTOMDRAW draw{};
                draw.hdr = {toolbar, std::to_underlying(Toolbar), NM_CUSTOMDRAW};
                draw.hdc = dc.Get();
                draw.rc = {0, 0, 80, 80};
                draw.dwDrawStage = CDDS_ITEMPREPAINT;
                draw.dwItemSpec = std::to_underlying(id);
                const COLORREF brush_color = ::GetDCBrushColor(dc.Get());
                QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, draw.hdr.idFrom,
                                               reinterpret_cast<LPARAM>(&draw)) == CDRF_SKIPDEFAULT);
                QIQI_TEST_CHECK(::GetDCBrushColor(dc.Get()) == brush_color);
                const auto& palette = GetThemePalette(dark);
                const COLORREF background = contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg;
                const COLORREF foreground = contrast ? ::GetSysColor(COLOR_BTNTEXT) : palette.text_primary;
                size_t ink = 0;
                uint64_t shape = 14695981039346656037ull;
                for (int y = 16; y < 64; ++y) {
                    for (int x = 16; x < 64; ++x) {
                        const auto pixel = ::GetPixel(dc.Get(), x, y);
                        QIQI_TEST_CHECK(pixel == background || pixel == foreground);
                        if (pixel == foreground)
                            ++ink;
                        shape = (shape ^ gsl::narrow_cast<uint64_t>(pixel == foreground)) * 1099511628211ull;
                    }
                }
                QIQI_TEST_CHECK(ink > 10 && std::ranges::find(shapes, shape) == shapes.end());
                shapes.push_back(shape);
            }
        }
    }
    std::array<wchar_t, 16> text{};
    NMTBGETINFOTIPW invalid_tip{};
    invalid_tip.hdr = {toolbar, std::to_underlying(Toolbar), TBN_GETINFOTIPW};
    invalid_tip.iItem = 0x7fff;
    invalid_tip.pszText = text.data();
    invalid_tip.cchTextMax = gsl::narrow_cast<int>(text.size());
    ::SendMessageW(window.Handle(), WM_NOTIFY, invalid_tip.hdr.idFrom, reinterpret_cast<LPARAM>(&invalid_tip));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"TB_GETBUTTONTEXTW");
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    Click(window, Exit);
}

} // namespace qiqi::monitor::tests
