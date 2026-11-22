// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test::controltest {
namespace {

struct ControlHostState {
    int clicks = 0;
    LRESULT checked_on_click = BST_UNCHECKED;
    bool destroy_on_mouse_leave = false;
    std::unique_ptr<FluentD2DThemeContext> d2d;
};
LRESULT CALLBACK DestroyHostOnMouseLeave(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref) noexcept {
    if (msg == WM_MOUSELEAVE) {
        auto* state = reinterpret_cast<ControlHostState*>(ref);
        ::DestroyWindow(::GetParent(hwnd));
        state->d2d.reset();
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hwnd, &DestroyHostOnMouseLeave, id);
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ControlHostProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = BindOrGetWindowUserData<ControlHostState>(hwnd, msg, lparam);
    if (state && msg == WM_PARENTNOTIFY && LOWORD(wparam) == WM_CREATE && state->destroy_on_mouse_leave) {
        QIQI_TEST_CHECK(::SetWindowSubclass(
            reinterpret_cast<HWND>(lparam), &DestroyHostOnMouseLeave, 77, reinterpret_cast<DWORD_PTR>(state)));
        return 0;
    }
    if (state && msg == WM_COMMAND && HIWORD(wparam) == BN_CLICKED && lparam != 0) {
        ++state->clicks;
        state->checked_on_click = ::SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0);
        return 0;
    }
    if (msg == WM_MEASUREITEM) {
        reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = 24;
        return TRUE;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

void TestReusableControls() {
    static_assert(kDarkWindowBg == RGB(32, 34, 38));
    static_assert(kLightSurfaceBg == RGB(248, 250, 253));
    static_assert(kDarkTextColor == RGB(238, 240, 245));
    static_assert(kLightTitleColor == RGB(24, 30, 42));
    static_assert(kDarkSecondaryTextColor == RGB(168, 176, 190));
    static_assert(kLightSecondaryTextColor == RGB(85, 95, 110));
    static_assert(kDarkDividerColor == RGB(64, 68, 78));
    static_assert(kLightDividerColor == RGB(220, 225, 232));
    static_assert(kDarkBodyTextColor == RGB(215, 220, 228));
    static_assert(kLightBodyTextColor == RGB(45, 52, 64));
    static_assert(kLightCaptionBg == RGB(243, 245, 249));
    static_assert(kLightTextColor == RGB(28, 32, 40));
    static_assert(!std::is_copy_constructible_v<FluentD2DThemeContext>);
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto restore_theme = gsl::finally([original_theme]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
    });

    FluentD2DThemeContext d2d;
    d2d.Init(0);
    QIQI_TEST_CHECK(d2d.dpi == 96 && d2d.ScalePx(24) == 24);
    QIQI_TEST_CHECK(d2d.d2d_factory && d2d.dwrite_factory && d2d.d2d_target);
    QIQI_TEST_CHECK(d2d.fmt_body && d2d.fmt_caption && d2d.fmt_btn);
    const auto check_fonts = [&d2d]() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        QIQI_TEST_CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0));
        const std::array formats{d2d.fmt_body.Get(), d2d.fmt_caption.Get(), d2d.fmt_btn.Get()};
        for (const auto format : formats) {
            QIQI_TEST_CHECK(format != nullptr);
            std::array<wchar_t, LF_FACESIZE> family{};
            QIQI_TEST_CHECK(SUCCEEDED(format->GetFontFamilyName(
                family.data(), gsl::narrow_cast<UINT32>(family.size()))));
            QIQI_TEST_CHECK(std::wcscmp(family.data(), std::data(metrics.lfMessageFont.lfFaceName)) == 0);
        }
        QIQI_TEST_CHECK(d2d.fmt_body->GetFontSize() == d2d.ScaleF(14));
        QIQI_TEST_CHECK(d2d.fmt_caption->GetFontSize() == d2d.ScaleF(12));
        QIQI_TEST_CHECK(d2d.fmt_btn->GetFontSize() == d2d.ScaleF(14));
        QIQI_TEST_CHECK(d2d.fmt_body->GetFontWeight() == DWRITE_FONT_WEIGHT_NORMAL);
        QIQI_TEST_CHECK(d2d.fmt_caption->GetFontWeight() == DWRITE_FONT_WEIGHT_SEMI_BOLD);
        QIQI_TEST_CHECK(d2d.fmt_btn->GetFontWeight() == DWRITE_FONT_WEIGHT_SEMI_BOLD);
    };
    check_fonts();
    QIQI_TEST_CHECK(d2d.fmt_btn->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_CENTER);
    d2d.Init(144);
    check_fonts();
    QIQI_TEST_CHECK(d2d.ScalePx(24) == 36 && d2d.ScaleF(24) == 36.0f);
    QIQI_TEST_CHECK(d2d.fmt_body->GetFontSize() == d2d.ScaleF(14));

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &ControlHostProc;
    wc.lpszClassName = L"QiQiD2DControlTestHost";
    QIQI_TEST_CHECK(::RegisterClassExW(&wc) != 0);
    const auto unregister_class = gsl::finally([instance]() noexcept {
        ::UnregisterClassW(L"QiQiD2DControlTestHost", instance);
    });
    ControlHostState state;
    ScopedWindow parent(::CreateWindowExW(
        0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 320, 320, nullptr, nullptr, instance, &state));
    QIQI_TEST_CHECK(parent);
    const auto create = [&](const wchar_t* cls, DWORD style, int id, DWORD_PTR group = 0) {
        const HWND child = CreateFluentChildControl(
            parent.Get(), instance, nullptr, &d2d, cls, L"Control", style, 0, 0, 240, 32, id, group);
        QIQI_TEST_CHECK(child);
        return child;
    };

    const HWND action = create(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, 501);
    const HWND toggle = create(L"BUTTON", BS_AUTOCHECKBOX, 502);
    const HWND radio_a = create(L"BUTTON", BS_AUTORADIOBUTTON | WS_GROUP, 601, MAKELONG(601, 602));
    const HWND radio_b = create(L"BUTTON", BS_AUTORADIOBUTTON, 602, MAKELONG(601, 602));
    const HWND other_radio = create(L"BUTTON", BS_AUTORADIOBUTTON | WS_GROUP, 701, MAKELONG(701, 701));
    const HWND label = create(L"STATIC", SS_LEFTNOWORDWRAP, 503);
    const HWND combo = create(L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, 504);
    TestComboItemReplacement(combo, label);
    TestFileDialogs(parent.Get());
    TestListViewColumns(parent.Get(), label);
    {
        ScopedGdiHandle<HRGN> region(::CreateRectRgn(0, 0, 240, 32));
        QIQI_TEST_CHECK(region && ::SetWindowRgn(combo, region.Get(), FALSE));
        std::ignore = region.Detach();
        const auto remove_region = gsl::finally([combo]() noexcept { ::SetWindowRgn(combo, nullptr, FALSE); });
        const ScopedWindowDC screen(nullptr);
        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
        const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), 240, 160));
        const ScopedSelectObject selected(dc.Get(), bitmap.Get());
        QIQI_TEST_CHECK(screen && dc && bitmap && selected);
        const RECT bounds{0, 0, 240, 160};
        const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(RGB(255, 0, 255)));
        QIQI_TEST_CHECK(background && ::FillRect(dc.Get(), &bounds, background.Get()));
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_COMBOBOX;
        item.hwndItem = combo;
        item.hDC = dc.Get();
        item.itemID = 0;
        item.rcItem = {0, 80, 240, 112};
        QIQI_TEST_CHECK(HandleFluentToolWindowMessage(parent.Get(), WM_DRAWITEM, 0,
            reinterpret_cast<LPARAM>(&item), d2d, 501));
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 90) != RGB(255, 0, 255));
    }
    const HWND sorted = create(L"COMBOBOX", CBS_DROPDOWNLIST | CBS_SORT, 505);
    const HWND item_data = create(L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED, 506);
    const std::array labels{L"Item"};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(sorted, labels) == E_INVALIDARG);
    QIQI_TEST_CHECK(ReplaceComboBoxItems(item_data, labels) == E_INVALIDARG);
    {
        const ScopedMemoryDC dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(dc);
        QIQI_TEST_CHECK(ApplyFluentControlColors(dc.Get(), d2d, true) != nullptr);
        QIQI_TEST_CHECK(ApplyFluentControlColors(dc.Get(), d2d, false) != nullptr);
        if (IsHighContrastEnabled()) {
            QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == ::GetSysColor(COLOR_BTNTEXT));
        } else {
            QIQI_TEST_CHECK(::GetBkMode(dc.Get()) == TRANSPARENT);
        }
        const auto color = ::GetTextColor(dc.Get());
        const auto font = ::GetCurrentObject(dc.Get(), OBJ_FONT);
        const FluentD2DThemeContext native;
        DRAWITEMSTRUCT item{};
        item.hwndItem = action;
        item.hDC = dc.Get();
        item.rcItem = {0, 0, 240, 32};
        DrawFluentPushButton(item, native, true, MakeD2DColor(255, 255, 255));
        QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == color);
        QIQI_TEST_CHECK(::GetCurrentObject(dc.Get(), OBJ_FONT) == font);
    }
    const auto type = [](HWND hwnd) { return ::GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK; };
    QIQI_TEST_CHECK(type(action) == BS_OWNERDRAW);
    QIQI_TEST_CHECK(type(toggle) == BS_OWNERDRAW && type(radio_a) == BS_OWNERDRAW);
    QIQI_TEST_CHECK((::GetWindowLongPtrW(toggle, GWL_STYLE) & WS_TABSTOP) != 0);
    QIQI_TEST_CHECK((::GetWindowLongPtrW(label, GWL_STYLE) & SS_TYPEMASK) == SS_LEFTNOWORDWRAP);
    QIQI_TEST_CHECK((::GetWindowLongPtrW(combo, GWL_STYLE) & CBS_OWNERDRAWFIXED) != 0);
    QIQI_TEST_CHECK(!CreateFluentChildControl(
        parent.Get(), instance, nullptr, &d2d, L"BUTTON", L"", BS_AUTORADIOBUTTON,
        0, 0, 240, 32, 800));
    QIQI_TEST_CHECK(!::GetDlgItem(parent.Get(), 800));

    ::SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(4, 4));
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    ::SendMessageW(toggle, WM_KEYUP, VK_SPACE, 0);
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    ::SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4));
    ::SendMessageW(toggle, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(300, 4));
    ::SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(300, 4));
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    ::SendMessageW(toggle, WM_KEYUP, VK_RETURN, 0);
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    QIQI_TEST_CHECK(state.clicks == 0);

    ::SendMessageW(toggle, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED);
    QIQI_TEST_CHECK(state.clicks == 1 && state.checked_on_click == BST_CHECKED);
    ::SendMessageW(toggle, WM_KEYDOWN, VK_SPACE, 0);
    ::SendMessageW(toggle, WM_KEYUP, VK_SPACE, 0);
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    ::EnableWindow(toggle, FALSE);
    ::SendMessageW(toggle, WM_KEYUP, VK_SPACE, 0);
    QIQI_TEST_CHECK(::SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    ::EnableWindow(toggle, TRUE);
    ::SendMessageW(toggle, BM_SETCHECK, BST_CHECKED, 0);

    ::SendMessageW(other_radio, BM_SETCHECK, BST_CHECKED, 0);
    ::SendMessageW(radio_a, BM_CLICK, 0, 0);
    ::SendMessageW(radio_b, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::SendMessageW(radio_a, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    QIQI_TEST_CHECK(::SendMessageW(radio_b, BM_GETCHECK, 0, 0) == BST_CHECKED);
    QIQI_TEST_CHECK(::SendMessageW(other_radio, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ::SendMessageW(action, BM_CLICK, 0, 0);
    ::SendMessageW(action, BM_SETCHECK, BST_CHECKED, 0);
    QIQI_TEST_CHECK(!::GetPropW(action, L"QiQiChecked"));
    QIQI_TEST_CHECK(::SendMessageW(radio_b, BM_GETCHECK, 0, 0) == BST_CHECKED);

    QIQI_TEST_CHECK(::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"First")) == 0);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Second")) == 1);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_SETCURSEL, 1, 0) == 1);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);

    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), 240, 100));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
    DRAWITEMSTRUCT dis{};
    dis.CtlType = ODT_BUTTON;
    dis.hwndItem = action;
    dis.hDC = dc.Get();
    dis.rcItem = {0, 0, 240, 32};

    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        d2d.RefreshThemeBrushes();
        const COLORREF surface = dark ? RGB(43, 46, 52) : RGB(255, 255, 255);
        LOGBRUSH brush{};
        QIQI_TEST_CHECK(::GetObjectW(d2d.surface_brush.Get(), sizeof(brush), &brush) == sizeof(brush));
        QIQI_TEST_CHECK(brush.lbColor == surface);

        dis.hwndItem = action;
        for (const bool primary : {false, true}) {
            for (const bool pressed : {false, true}) {
                dis.itemState = pressed ? ODS_SELECTED : 0;
                DrawFluentPushButton(dis, d2d, primary, MakeD2DColor(1, 2, 3));
                const COLORREF fill = primary
                    ? (pressed ? RGB(0, 84, 166) : RGB(0, 103, 192))
                    : (dark ? (pressed ? RGB(48, 52, 60) : RGB(56, 60, 70))
                            : (pressed ? RGB(232, 236, 242) : RGB(255, 255, 255)));
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 20, 16) == fill);
            }
        }
        {
            ::EnableWindow(action, FALSE);
            const auto reenable = gsl::finally([action]() noexcept { ::EnableWindow(action, TRUE); });
            dis.itemState = ODS_DISABLED | ODS_SELECTED;
            for (const bool primary : {false, true}) {
                DrawFluentPushButton(dis, d2d, primary, MakeD2DColor(1, 2, 3));
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 20, 16) == d2d.appearance.palette.surface_bg);
            }
        }
        dis.itemState = 0;
        dis.hwndItem = toggle;
        DrawFluentToggleOrRadioRow(dis, d2d, false);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 100, 2) == surface);
        {
            const auto preferences = d2d.display_preferences;
            const auto restore_preferences = gsl::finally([&]() noexcept {
                d2d.SetDisplayPreferences(preferences);
            });
            d2d.SetDisplayPreferences({dark ? UiTheme::Dark : UiTheme::Light, UiTextScale::Percent150, true});
            auto focused = dis;
            focused.rcItem.bottom = FluentControlHeight(d2d, 32);
            const auto check_focus = [&]() {
                focused.itemState = 0;
                DrawFluentToggleOrRadioRow(focused, d2d, false);
                std::vector<COLORREF> pixels;
                for (int y = 0; y < focused.rcItem.bottom; ++y)
                    for (int x = 0; x < focused.rcItem.right; ++x)
                        pixels.push_back(::GetPixel(dc.Get(), x, y));
                RECT allowed = FluentToggleBounds(focused.rcItem, d2d);
                constexpr int kFocusOutlineAllowance = 4;
                ::InflateRect(&allowed, d2d.ScalePx(kFocusOutlineAllowance), d2d.ScalePx(kFocusOutlineAllowance));
                focused.itemState = ODS_FOCUS;
                DrawFluentToggleOrRadioRow(focused, d2d, false);
                bool visible_focus = false;
                size_t index = 0;
                for (int y = 0; y < focused.rcItem.bottom; ++y) {
                    for (int x = 0; x < focused.rcItem.right; ++x, ++index) {
                        if (::GetPixel(dc.Get(), x, y) == gsl::at(pixels, index)) continue;
                        QIQI_TEST_CHECK(::PtInRect(&allowed, POINT{x, y}));
                        visible_focus = true;
                    }
                }
                QIQI_TEST_CHECK(visible_focus);
                focused.itemState = ODS_FOCUS | ODS_NOFOCUSRECT;
                DrawFluentToggleOrRadioRow(focused, d2d, false);
                index = 0;
                for (int y = 0; y < focused.rcItem.bottom; ++y)
                    for (int x = 0; x < focused.rcItem.right; ++x, ++index)
                        QIQI_TEST_CHECK(::GetPixel(dc.Get(), x, y) == gsl::at(pixels, index));
            };
            check_focus();
            SetHighContrastOverrideForTest(true);
            const auto restore_contrast = gsl::finally([&]() noexcept {
                SetHighContrastOverrideForTest(std::nullopt);
                d2d.RefreshThemeBrushes();
            });
            d2d.RefreshThemeBrushes();
            check_focus();
        }
        {
            ::EnableWindow(toggle, FALSE);
            const auto reenable = gsl::finally([toggle]() noexcept { ::EnableWindow(toggle, TRUE); });
            ::SendMessageW(toggle, BM_SETCHECK, BST_UNCHECKED, 0);
            DrawFluentToggleOrRadioRow(dis, d2d, false);
            const RECT track = FluentToggleBounds(dis.rcItem, d2d);
            const int radius = (track.bottom - track.top) / 2;
            const COLORREF knob = ::GetPixel(dc.Get(), track.left + radius,
                                             (track.top + track.bottom) / 2);
            QIQI_TEST_CHECK(GetRValue(knob) == GetGValue(knob) && GetGValue(knob) == GetBValue(knob));
            QIQI_TEST_CHECK(knob != surface && knob != d2d.appearance.palette.text_primary);
        }
        dis.hwndItem = radio_b;
        DrawFluentToggleOrRadioRow(dis, d2d, true);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 100, 2) == surface);

        PaintFluentComboBoxFrame(combo, dc.Get(), d2d);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == (dark ? RGB(56, 60, 70) : kLightSurfaceBg));
        {
            const bool previous_clip = d2d.clip_controls_to_dc;
            const auto restore_clip = gsl::finally([&]() noexcept {
                d2d.clip_controls_to_dc = previous_clip;
                ::SetWindowRgn(combo, nullptr, FALSE);
            });
            d2d.clip_controls_to_dc = true;
            RECT client{};
            QIQI_TEST_CHECK(::GetClientRect(combo, &client));
            const int clip_top = client.bottom / 2;
            const ScopedGdiHandle<HBRUSH> untouched(::CreateSolidBrush(RGB(255, 0, 255)));
            QIQI_TEST_CHECK(untouched);
            for (const bool empty : {false, true}) {
                ::FillRect(dc.Get(), &client, untouched.Get());
                ScopedGdiHandle<HRGN> region(::CreateRectRgn(
                    0, empty ? client.bottom : clip_top, client.right, client.bottom));
                QIQI_TEST_CHECK(region && ::SetWindowRgn(combo, region.Get(), FALSE));
                std::ignore = region.Detach();
                PaintFluentComboBoxFrame(combo, dc.Get(), d2d);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == RGB(255, 0, 255));
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, client.bottom - 4) ==
                    (empty ? RGB(255, 0, 255) : (dark ? RGB(56, 60, 70) : kLightSurfaceBg)));
            }
        }
        dis.CtlType = ODT_COMBOBOX;
        dis.hwndItem = combo;
        dis.itemID = 1;
        for (const bool selected : {false, true}) {
            dis.itemState = selected ? ODS_SELECTED : 0;
            DrawFluentComboBoxItem(dis, d2d);
            const COLORREF fill = selected
                ? (dark ? RGB(0, 96, 180) : RGB(220, 234, 252))
                : (dark ? RGB(54, 58, 68) : RGB(255, 255, 255));
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == fill);
        }
        dis.itemState = ODS_SELECTED | ODS_FOCUS;
        DrawFluentComboBoxItem(dis, d2d, true);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == d2d.appearance.palette.input_bg);
        {
            SetHighContrastOverrideForTest(true);
            const auto restore_contrast = gsl::finally([&d2d]() noexcept {
                SetHighContrastOverrideForTest(std::nullopt);
                d2d.RefreshThemeBrushes();
            });
            d2d.RefreshThemeBrushes();
            QIQI_TEST_CHECK(ApplyFluentControlColors(dc.Get(), d2d, true));
            QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == ::GetSysColor(COLOR_WINDOWTEXT));
            QIQI_TEST_CHECK(::GetBkColor(dc.Get()) == ::GetSysColor(COLOR_WINDOW));
            const auto text_color = ::GetTextColor(dc.Get());
            dis.itemState = ODS_SELECTED | ODS_FOCUS;
            DrawFluentComboBoxItem(dis, d2d);
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == ::GetSysColor(COLOR_HIGHLIGHT));
            QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == text_color);
            {
                ::EnableWindow(action, FALSE);
                const auto reenable = gsl::finally([action]() noexcept { ::EnableWindow(action, TRUE); });
                DRAWITEMSTRUCT button = dis;
                button.CtlType = ODT_BUTTON;
                button.hwndItem = action;
                button.itemState = ODS_DISABLED;
                DrawFluentPushButton(button, d2d, false, ColorRefToD2DColor(surface), L"\nUnavailable");
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 0, 16) == d2d.appearance.palette.surface_bg);
            }
        }

        const RECT rect{0, 0, 240, 100};
        const std::array<FluentCardSpec, 1> cards = {{
            {D2D1::RectF(1, 1, 239, 99), L"Card", L"Card"}
        }};
        HRESULT end_hr = E_FAIL;
        {
            const ScopedD2DDcDraw draw(d2d.d2d_target.Get(), dc.Get(), rect, &end_hr);
            QIQI_TEST_CHECK(draw);
            DrawFluentCardGroup(d2d, cards, !dark, dark);
            for (const CompositionUnderlineStyle style : {
                     CompositionUnderlineStyle::Dot,
                     CompositionUnderlineStyle::Solid,
                     CompositionUnderlineStyle::Dash,
                     CompositionUnderlineStyle::Squiggle,
                     CompositionUnderlineStyle::None}) {
                DrawFluentUnderlineStylePreview(
                    d2d, style, dark, D2D1::RectF(20.0f, 30.0f, 180.0f, 54.0f), MakeD2DColor(0, 103, 192));
            }
            // Degenerate narrow rect guard
            DrawFluentUnderlineStylePreview(
                d2d, CompositionUnderlineStyle::Solid, false, D2D1::RectF(10.0f, 10.0f, 12.0f, 20.0f), MakeD2DColor(0, 103, 192));
        }
        QIQI_TEST_CHECK(SUCCEEDED(end_hr));
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 70) == surface);

        {
            const ScopedSaveDC saved(dc.Get());
            const ScopedGdiHandle<HBRUSH> untouched(::CreateSolidBrush(RGB(255, 0, 255)));
            QIQI_TEST_CHECK(untouched);
            ::FillRect(dc.Get(), &rect, untouched.Get());
            QIQI_TEST_CHECK(::IntersectClipRect(dc.Get(), 15, 20, 225, 80) != ERROR);
            D2D1_MATRIX_3X2_F original{};
            d2d.d2d_target->GetTransform(&original);
            const auto restore_transform = gsl::finally([&]() noexcept { d2d.d2d_target->SetTransform(original); });
            d2d.d2d_target->SetTransform(D2D1::Matrix3x2F::Translation(4, 5));
            {
                const ScopedD2DDcDraw draw(d2d.d2d_target.Get(), dc.Get(), rect, &end_hr, true);
                QIQI_TEST_CHECK(draw);
                d2d.d2d_target->Clear(MakeD2DColor(0, 255, 0));
                const auto blue = CreateSolidBrush(d2d.d2d_target.Get(), MakeD2DColor(0, 0, 255));
                QIQI_TEST_CHECK(blue);
                d2d.d2d_target->FillRectangle(D2D1::RectF(30, 30, 50, 50), blue.Get());
            }
            QIQI_TEST_CHECK(SUCCEEDED(end_hr));
            // GetPixel returns CLR_INVALID outside the DC clip; inspect the underlying bitmap.
            QIQI_TEST_CHECK(::SelectClipRgn(dc.Get(), nullptr) != ERROR);
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 10) == RGB(255, 0, 255));
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 90) == RGB(255, 0, 255));
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 50) == RGB(0, 255, 0));
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 35, 36) == RGB(0, 0, 255));
            D2D1_MATRIX_3X2_F restored{};
            d2d.d2d_target->GetTransform(&restored);
            QIQI_TEST_CHECK(restored._31 == 4 && restored._32 == 5);
        }

        EnableFluentUnderlineComboPreview(combo, 502);
        PaintFluentComboBoxFrame(combo, dc.Get(), d2d);
        DrawFluentComboBoxItem(dis, d2d);
    }

    TestComboTextReadBounds(combo, dc.Get(), d2d);
    parent.Reset();
    QIQI_TEST_CHECK(!::IsWindow(toggle) && !::IsWindow(combo));

    ControlHostState closing_state;
    closing_state.destroy_on_mouse_leave = true;
    closing_state.d2d = std::make_unique<FluentD2DThemeContext>();
    closing_state.d2d->Init(96);
    const ScopedWindow closing_parent(::CreateWindowExW(
        0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 320, 320, nullptr, nullptr, instance, &closing_state));
    QIQI_TEST_CHECK(closing_parent);
    const HWND closing_combo = CreateFluentChildControl(
        closing_parent.Get(), instance, nullptr, closing_state.d2d.get(), L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, 0, 0, 240, 200, 900);
    QIQI_TEST_CHECK(closing_combo);
    ::SendMessageW(closing_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Item"));
    ::SendMessageW(closing_combo, WM_MOUSELEAVE, 0, 0);
    QIQI_TEST_CHECK(!::IsWindow(closing_parent.Get()) && !closing_state.d2d);
}
} // namespace

void RunPaintTests() {
    TestReusableControls();
}

} // namespace qiqi::test::controltest
