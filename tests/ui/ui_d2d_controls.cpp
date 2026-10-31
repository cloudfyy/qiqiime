#include "common.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "ui/d2d_controls.h"

#include <commctrl.h>
#include <memory>
#include <type_traits>

namespace qiqi::test {
namespace {

struct ControlHostState {
    int clicks = 0;
    LRESULT checked_on_click = BST_UNCHECKED;
    bool destroy_on_mouse_leave = false;
    std::unique_ptr<FluentD2DThemeContext> d2d;
};

LRESULT CALLBACK DestroyHostOnMouseLeave(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref) {
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
    QIQI_TEST_CHECK(d2d.fmt_btn->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_CENTER);
    d2d.Init(144);
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
        dis.hwndItem = toggle;
        DrawFluentToggleOrRadioRow(dis, d2d, false);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 100, 2) == surface);
        dis.hwndItem = radio_b;
        DrawFluentToggleOrRadioRow(dis, d2d, true);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 100, 2) == surface);

        PaintFluentComboBoxFrame(combo, dc.Get(), d2d);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 4) == (dark ? RGB(56, 60, 70) : RGB(248, 250, 253)));
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

        const RECT rect{0, 0, 240, 100};
        const std::array<FluentCardSpec, 1> cards = {{
            {D2D1::RectF(1, 1, 239, 99), L"Card", L"Card"}
        }};
        HRESULT end_hr = E_FAIL;
        {
            const ScopedD2DDcDraw draw(d2d.d2d_target.Get(), dc.Get(), rect, &end_hr);
            QIQI_TEST_CHECK(draw);
            DrawFluentCardGroup(d2d, cards, !dark, dark);
        }
        QIQI_TEST_CHECK(SUCCEEDED(end_hr));
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 120, 70) == surface);
    }

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

void RunD2DControlsTests() {
    TestReusableControls();
}

} // namespace qiqi::test
