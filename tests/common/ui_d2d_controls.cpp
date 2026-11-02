#include "common/test_support.h"
#include "common/about_dialog.h"
#include "common/dialog_util.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "common/d2d_controls.h"

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

LRESULT CALLBACK FailComboMutation(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR failure) noexcept {
    if (msg == CB_ADDSTRING && failure == 1) return CB_ERRSPACE;
    if ((msg == CB_ADDSTRING && failure == 2) || (msg == CB_SETCURSEL && failure == 3)) return CB_ERR;
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

void TestComboItemReplacement(HWND combo, HWND label) {
    const std::array original{L"First", L"Second"};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, original) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 2);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 0);
    ::SendMessageW(combo, CB_SETCURSEL, 1, 0);
    const std::array translated{L"第一项", L"第二项", L"第三项"};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, translated) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);
    std::array<wchar_t, 16> selected{};
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(selected.data())) == 3);
    QIQI_TEST_CHECK(std::wstring_view(selected.data()) == gsl::at(translated, 1));
    const std::array<const wchar_t*, 1> invalid{nullptr};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, invalid) == E_INVALIDARG);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ReplaceComboBoxItems(label, original) == E_INVALIDARG);
    QIQI_TEST_CHECK(ReplaceComboBoxItems(nullptr, original) == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, std::span(original).first(1)) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 0);
    for (DWORD_PTR failure = 1; failure <= 3; ++failure) {
        QIQI_TEST_CHECK(::SetWindowSubclass(combo, FailComboMutation, 88, failure));
        const auto restore = gsl::finally([combo]() noexcept {
            ::RemoveWindowSubclass(combo, FailComboMutation, 88);
        });
        QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, original) == (failure == 1 ? E_OUTOFMEMORY : E_FAIL));
    }
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, {}) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 0);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR);
}

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
    TestComboItemReplacement(combo, label);
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

        EnableFluentUnderlineComboPreview(combo, 502);
        PaintFluentComboBoxFrame(combo, dc.Get(), d2d);
        DrawFluentComboBoxItem(dis, d2d);
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

void TestCommonDialogsAndUtils() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore = gsl::finally([original_theme, &original_monitor]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    const ScopedGdiHandle<HFONT> font = CreateUiFont(14, FW_NORMAL);
    QIQI_TEST_CHECK(font.IsValid());
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(nullptr, L"test", 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), nullptr, 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"", 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"test", 0) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"Line 1\nLine 2", 200) > 0);

    const ScopedIcon brand_icon = dialog_detail::LoadOrCreateBrandIcon(32, instance);
    QIQI_TEST_CHECK(brand_icon.IsValid());
    dialog_detail::ApplyDialogIcons(nullptr, brand_icon.Get(), brand_icon.Get());

    constexpr bool done = true;
    QIQI_TEST_CHECK(!dialog_detail::RunModalDialogLoop(nullptr, done));

    // Verify owner-relative centering and work-area clamping on a secondary/negative-coordinate monitor
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1920, -1080, 0, 0}, 96});
    const ScopedWindow owner(::CreateWindowExW(
        0, L"STATIC", L"Owner", WS_POPUP | WS_VISIBLE,
        -1880, -1040, 220, 160, nullptr, nullptr, instance, nullptr));
    QIQI_TEST_CHECK(owner.IsValid());

    {
        const ScopedWindow about_zh(CreateAboutWindow(instance, owner.Get(), UiLanguage::ZhCn, false));
        QIQI_TEST_CHECK(about_zh.IsValid());
        QIQI_TEST_CHECK(::GetWindow(about_zh.Get(), GW_OWNER) == owner.Get());
        RECT about_rc{};
        QIQI_TEST_CHECK(::GetWindowRect(about_zh.Get(), &about_rc));
        // Owner is near (-1880, -1040) so centered dialog would overflow left/top of (-1920, -1080) and must clamp to -1920, -1080
        QIQI_TEST_CHECK(about_rc.left == -1920 && about_rc.top == -1080);
        QIQI_TEST_CHECK(about_rc.right <= 0 && about_rc.bottom <= 0);

        std::array<wchar_t, 128> text{};
        ::GetWindowTextW(about_zh.Get(), text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kZhCnStrings.about_title);

        // Live language update via kSettingsLanguageChangedMessage
        ::SendMessageW(about_zh.Get(), kSettingsLanguageChangedMessage, static_cast<WPARAM>(UiLanguage::EnUs), 0);
        ::GetWindowTextW(about_zh.Get(), text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.about_title);
        ::GetDlgItemTextW(about_zh.Get(), 201, text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.about_btn_github);
        ::GetDlgItemTextW(about_zh.Get(), IDOK, text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.dlg_btn_ok);

        // Theme switching and WM_PRINTCLIENT rendering in both light and dark modes
        const ScopedWindowDC screen_dc;
        const ScopedMemoryDC mem_dc(::CreateCompatibleDC(screen_dc.Get()));
        const ScopedGdiHandle<HBITMAP> bmp(::CreateCompatibleBitmap(screen_dc.Get(), 480, 360));
        QIQI_TEST_CHECK(screen_dc && mem_dc && bmp);
        const ScopedSelectObject sel_bmp(mem_dc.Get(), bmp.Get());
        for (const bool dark : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(about_zh.Get(), WM_THEMECHANGED, 0, 0);
            ::SendMessageW(about_zh.Get(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem_dc.Get()), PRF_CLIENT);
            const COLORREF expected_bg = dark ? RGB(32, 34, 38) : RGB(248, 250, 253);
            QIQI_TEST_CHECK(::GetPixel(mem_dc.Get(), 10, 10) == expected_bg);
        }

        // Embedded icon mode fallback
        const ScopedWindow about_embedded(CreateAboutWindow(instance, owner.Get(), UiLanguage::EnUs, true));
        QIQI_TEST_CHECK(about_embedded.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(about_embedded.Get(), WM_GETICON, ICON_BIG, 0) != 0);
    }

    // Modal ShowAboutDialog execution
    ::SetTimer(nullptr, 9, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        if (const HWND dlg = FindThreadWindowByClass(kAboutWndClass)) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(instance, owner.Get(), UiLanguage::EnUs, true));
    QIQI_TEST_CHECK(::IsWindowEnabled(owner.Get()) != FALSE);
}

} // namespace

void RunD2DControlsTests() {
    TestReusableControls();
    TestCommonDialogsAndUtils();
}

} // namespace qiqi::test
