#include "common.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "ui/candidate_ui.h"
#include "ui/langbar_button.h"
#include "ui/settings_dialog.h"
#include "ui/status_bar.h"

#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <string>
#include <vector>

namespace qiqi::test {

namespace {

HWND FindQuickSettingsMenu() {
    HWND found = nullptr;
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND hwnd, LPARAM param) -> BOOL {
        std::array<wchar_t, 128> name{};
        ::GetClassNameW(hwnd, name.data(), gsl::narrow_cast<int>(name.size()));
        if (std::wstring_view(name.data()) == kMenuWndClass) {
            *reinterpret_cast<HWND*>(param) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

void CloseQuickSettingsMenu() {
    const HWND menu = FindQuickSettingsMenu();
    QIQI_TEST_CHECK(menu != nullptr);
    ::SendMessageW(menu, WM_CLOSE, 0, 0);
}

void TestStatusBarUI() {
    // Load uxtheme.dll, dwmapi.dll, and shcore.dll via ScopedModule RAII in the test process
    const ScopedModule uxtheme_mod(::LoadLibraryW(L"uxtheme.dll"));
    const ScopedModule dwmapi_mod(::LoadLibraryW(L"dwmapi.dll"));
    const ScopedModule shcore_mod(::LoadLibraryW(L"shcore.dll"));

    // 1. Test theme detection & palette switching (OS default, forced Light, forced Dark)
    SetThemeDarkOverrideForTest(std::nullopt);
    (void)IsSystemDarkTheme();
    SetThemeDarkOverrideForTest(false);
    QIQI_TEST_CHECK(!IsSystemDarkTheme());
    QIQI_TEST_CHECK(!GetCurrentThemePalette().is_dark);
    SetThemeDarkOverrideForTest(true);
    QIQI_TEST_CHECK(IsSystemDarkTheme());
    QIQI_TEST_CHECK(GetCurrentThemePalette().is_dark);

    LangBarMenuId clicked_action = static_cast<LangBarMenuId>(0);
    StatusBarUI bar(::GetModuleHandleW(nullptr), [&clicked_action](LangBarMenuId id) {
        clicked_action = id;
    });

    // Disabled by default, but all table-driven icon bitmaps are initialized
    QIQI_TEST_CHECK(!bar.IsVisible());
    for (size_t i = 0; i < StatusBarUI::kIconCount; ++i) {
        const auto kind = static_cast<StatusBarIconKind>(i);
        QIQI_TEST_CHECK(bar.GetIconBitmap(kind) != nullptr);
    }
    QIQI_TEST_CHECK(bar.GetIconBitmap(StatusBarIconKind::Count) == nullptr);
    QIQI_TEST_CHECK(bar.GetIconBitmap(static_cast<StatusBarIconKind>(99)) == nullptr);

    // Verify table-driven HitTestStatusBarIcon geometry and out-of-range bounds guard
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::HalfWidthMoon, 0.73, 0.0));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::HalfWidthMoon, 0.0, 0.0));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::FullWidthSun, 0.0, 0.0));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::FullWidthSun, 0.64, 0.0));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::FullWidthSun, 0.48, 0.0));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::ChinesePunct, 0.20, 0.08));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::ChinesePunct, -0.34, -0.28));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::EnglishPunct, -0.34, 0.22));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::EnglishPunct, 0.22, 0.06));
    QIQI_TEST_CHECK(StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::SettingsGear, 0.24, 0.0));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::SettingsGear, 0.0, 0.0));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind::Count, 0.0, 0.0));
    QIQI_TEST_CHECK(!StatusBarUI::HitTestStatusBarIcon(static_cast<StatusBarIconKind>(99), 0.0, 0.0));

    LangBarState st{};
    st.show_status_bar = true;
    st.ui_language = UiLanguage::ZhCn;
    bar.UpdateState(st);
    // Call Show(true) again when window already exists
    bar.Show(true);
    QIQI_TEST_CHECK(bar.IsVisible());
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);

    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::SendMessageW(bar.WindowHandle(), WM_MOUSEACTIVATE, 0, 0);

    // 2. Test leftmost vertical pill gripper [0..22) and blank margins/gaps hit-test (HTCAPTION), cursor, and window moving (WM_MOVE)
    POINT grip_screen{10, 18};
    ::ClientToScreen(bar.WindowHandle(), &grip_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(grip_screen.x, grip_screen.y)) == HTCAPTION);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_SETCURSOR, reinterpret_cast<WPARAM>(bar.WindowHandle()), MAKELPARAM(HTCAPTION, WM_MOUSEMOVE)) == TRUE);

    // Top/bottom blank border and inter-button gap also return HTCAPTION for blank-area dragging
    POINT top_margin_screen{40, 1};
    ::ClientToScreen(bar.WindowHandle(), &top_margin_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(top_margin_screen.x, top_margin_screen.y)) == HTCAPTION);

    POINT gap_screen{58, 18};
    ::ClientToScreen(bar.WindowHandle(), &gap_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(gap_screen.x, gap_screen.y)) == HTCAPTION);

    POINT btn_screen{40, 18};
    ::ClientToScreen(bar.WindowHandle(), &btn_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(btn_screen.x, btn_screen.y)) == HTCLIENT);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_SETCURSOR, reinterpret_cast<WPARAM>(bar.WindowHandle()), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == TRUE);

    ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(320, 480));
    QIQI_TEST_CHECK(bar.Position().x == 320 && bar.Position().y == 480);

    // 3. Test hover tooltips across Grip [0..22) and all 4 buttons [22..58, 58..94, 94..130, 130..166)
    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(-5)).empty());
    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(999)).empty());

    ::SendMessageW(bar.WindowHandle(), WM_NCMOUSEMOVE, HTCAPTION, MAKELPARAM(grip_screen.x, grip_screen.y));
    QIQI_TEST_CHECK(bar.HoveredZone() == 0);
    QIQI_TEST_CHECK(bar.ActiveTooltipText() == kZhCnStrings.tip_grip);
    QIQI_TEST_CHECK(bar.TooltipWindowHandle() != nullptr);
    ::SendMessageW(bar.TooltipWindowHandle(), WM_MOUSEACTIVATE, 0, 0);

    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(35, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 1 && bar.ActiveTooltipText() == kZhCnStrings.tip_mode);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(70, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 2 && bar.ActiveTooltipText() == kZhCnStrings.tip_width);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(105, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 3 && bar.ActiveTooltipText() == kZhCnStrings.tip_punct);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(145, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 4 && bar.ActiveTooltipText() == kZhCnStrings.tip_settings);
    // Move to out-of-range X coordinate while hovered_zone_ >= 0 (exercises UpdateTooltipForZone(-1) branch)
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(300, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == -1 && bar.ActiveTooltipText().empty());
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(145, 18));

    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::SendMessageW(bar.WindowHandle(), WM_MOUSELEAVE, 0, 0);
    QIQI_TEST_CHECK(bar.HoveredZone() == -1 && bar.ActiveTooltipText().empty());

    // 4. Test button clicks: [Grip:0..22] [Cell0:22..58] [Cell1:58..94] [Cell2:94..130] [Cell3:130..166]
    auto send_left_click = [](HWND hwnd, int x, int y) noexcept {
        ::SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    send_left_click(bar.WindowHandle(), 10, 18);
    send_left_click(bar.WindowHandle(), 300, 18);
    QIQI_TEST_CHECK(clicked_action == static_cast<LangBarMenuId>(0));

    send_left_click(bar.WindowHandle(), 35, 18);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleChinese);
    send_left_click(bar.WindowHandle(), 70, 18);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleFullWidth);
    send_left_click(bar.WindowHandle(), 105, 18);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::TogglePunctuation);
    send_left_click(bar.WindowHandle(), 145, 18);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::OpenSettings);

    // 4b. Test press-and-drag on a button cell: dragging beyond threshold moves the bar without firing click_action
    clicked_action = static_cast<LangBarMenuId>(0);
    const POINT before_drag = bar.Position();
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 18));
    // Sub-threshold movement (< 4px) does not start dragging
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(42, 18));
    QIQI_TEST_CHECK(bar.Position().x == before_drag.x && bar.Position().y == before_drag.y);
    // Movement beyond threshold (>= 4px) drags the window and sets IDC_SIZEALL cursor
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(70, 38));
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_SETCURSOR, reinterpret_cast<WPARAM>(bar.WindowHandle()), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == TRUE);
    QIQI_TEST_CHECK(bar.Position().x != before_drag.x || bar.Position().y != before_drag.y);
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONUP, 0, MAKELPARAM(70, 38));
    QIQI_TEST_CHECK(clicked_action == static_cast<LangBarMenuId>(0));

    // 4c. Test WM_CAPTURECHANGED and orphan WM_LBUTTONUP (without prior WM_LBUTTONDOWN)
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 18));
    ::SendMessageW(bar.WindowHandle(), WM_CAPTURECHANGED, 0, 0);
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 18));
    QIQI_TEST_CHECK(clicked_action == static_cast<LangBarMenuId>(0));

    // Also test out-of-bounds WM_NCHITTEST (negative coordinates return HTCLIENT)
    POINT neg_screen{-10, -10};
    ::ClientToScreen(bar.WindowHandle(), &neg_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(neg_screen.x, neg_screen.y)) == HTCLIENT);

    // 5. Test themed right-click context menu & WM_RBUTTONUP / WM_NCRBUTTONUP / WM_COMMAND / WM_SETTINGCHANGE
    ScopedGdiHandle<HBRUSH> menu_brush;
    ScopedMenu popup_menu = bar.BuildContextMenu(menu_brush);
    QIQI_TEST_CHECK(popup_menu.IsValid() && menu_brush.IsValid());
    QIQI_TEST_CHECK(::GetMenuItemCount(popup_menu.Get()) == 13); // 11 items + 2 separators
    QIQI_TEST_CHECK((::GetMenuState(popup_menu.Get(),
        static_cast<UINT>(LangBarMenuId::ToggleStatusBar), MF_BYCOMMAND) & MF_CHECKED) != 0);

    ::SetTimer(nullptr, 3, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        CloseQuickSettingsMenu();
    });
    ::SendMessageW(bar.WindowHandle(), WM_RBUTTONUP, 0, MAKELPARAM(35, 18));

    ::SetTimer(nullptr, 4, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        CloseQuickSettingsMenu();
    });
    ::SendMessageW(bar.WindowHandle(), WM_NCRBUTTONUP, HTCAPTION, MAKELPARAM(grip_screen.x, grip_screen.y));

    ::SendMessageW(bar.WindowHandle(), WM_COMMAND, MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage), 0), 0);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleUiLanguage);

    // Test DestroyBarWindow followed by re-Show(true) when pos_initialized_ is already true
    bar.DestroyBarWindow();
    bar.Show(true);
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);

    // Test Light theme + English UI rendering and tooltips
    SetThemeDarkOverrideForTest(false);
    ::SendMessageW(bar.WindowHandle(), WM_SETTINGCHANGE, 0, 0);
    st.ui_language = UiLanguage::EnUs;
    st.chinese_mode = false;
    st.full_width = true;
    st.chinese_punct = false;
    bar.UpdateState(st);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(35, 18));
    QIQI_TEST_CHECK(bar.ActiveTooltipText() == kEnUsStrings.tip_mode);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    SetThemeDarkOverrideForTest(std::nullopt);
    st.show_status_bar = false;
    bar.UpdateState(st);
    QIQI_TEST_CHECK(!bar.IsVisible());
    bar.DestroyBarWindow();
}

void TestStatusBarBoundariesAtDifferentDpis() {
    const auto original_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(original_monitor);
    });
    const std::array<LangBarMenuId, 4> actions{
        LangBarMenuId::ToggleChinese, LangBarMenuId::ToggleFullWidth,
        LangBarMenuId::TogglePunctuation, LangBarMenuId::OpenSettings
    };

    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1920, 1080}, dpi});
        std::vector<LangBarMenuId> clicked;
        StatusBarUI bar(::GetModuleHandleW(nullptr), [&clicked](LangBarMenuId action) {
            clicked.push_back(action);
        });
        LangBarState state{};
        state.show_status_bar = true;
        for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
            state.ui_language = language;
            bar.UpdateState(state);
            QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);
            QIQI_TEST_CHECK(bar.CurrentDpi() == dpi);
            QIQI_TEST_CHECK(bar.CurrentWidth() == ScalePxByDpi(StatusBarUI::kBarWidth, dpi));
            QIQI_TEST_CHECK(bar.CurrentHeight() == ScalePxByDpi(StatusBarUI::kBarHeight, dpi));

            const auto& strings = GetUiStrings(language);
            const std::array<const wchar_t*, 5> tips{
                strings.tip_grip, strings.tip_mode, strings.tip_width,
                strings.tip_punct, strings.tip_settings
            };
            const int offset = ScalePxByDpi(StatusBarUI::kButtonsOffset, dpi);
            const int cell_width = ScalePxByDpi(StatusBarUI::kCellWidth, dpi);
            for (size_t zone = 0; zone < tips.size(); ++zone) {
                const int left = zone == 0 ? 0 : offset + (static_cast<int>(zone) - 1) * cell_width;
                const int right = zone == 0 ? offset : left + cell_width;
                for (const int x : {left, right - 1}) {
                    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(x)) == tips[zone]);
                    const size_t previous_clicks = clicked.size();
                    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
                    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONUP, 0, MAKELPARAM(x, 1));
                    QIQI_TEST_CHECK(clicked.size() == previous_clicks + (zone == 0 ? 0 : 1));
                    if (zone != 0) {
                        QIQI_TEST_CHECK(clicked.back() == actions[zone - 1]);
                    }
                }
            }
            for (const int x : {-1, bar.CurrentWidth(), bar.CurrentWidth() + cell_width}) {
                QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(x)).empty());
                const size_t previous_clicks = clicked.size();
                ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
                ::SendMessageW(bar.WindowHandle(), WM_LBUTTONUP, 0, MAKELPARAM(x, 1));
                QIQI_TEST_CHECK(clicked.size() == previous_clicks);
            }
        }
    }
}

void TestStatusBarContextMenuState() {
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const PrimaryDict dict : {PrimaryDict::Jidian, PrimaryDict::Haifeng, PrimaryDict::BuiltinOnly}) {
            for (const bool enabled : {false, true}) {
                LangBarState state{};
                state.ui_language = language;
                state.primary_dict = dict;
                state.chinese_mode = enabled;
                state.full_width = enabled;
                state.chinese_punct = enabled;
                state.enable_jidian_extra = enabled;
                // Menu construction must also work without creating a status bar window.
                bar.UpdateState(state);
                const auto& strings = GetUiStrings(language);
                struct ExpectedItem {
                    LangBarMenuId id;
                    const wchar_t* text;
                    bool checked;
                    bool enabled;
                };
                const bool is_jidian = dict == PrimaryDict::Jidian;
                const std::array<ExpectedItem, 11> items{{
                    {LangBarMenuId::ToggleChinese, strings.menu_toggle_chinese, enabled, true},
                    {LangBarMenuId::ToggleFullWidth, strings.menu_toggle_full_width, enabled, true},
                    {LangBarMenuId::TogglePunctuation, strings.menu_toggle_punct, enabled, true},
                    {LangBarMenuId::DictJidian, strings.menu_dict_jidian, is_jidian, true},
                    {LangBarMenuId::DictHaifeng, strings.menu_dict_haifeng, dict == PrimaryDict::Haifeng, true},
                    {LangBarMenuId::ToggleExtraDict, strings.menu_toggle_extra_dict, is_jidian && enabled, is_jidian},
                    {LangBarMenuId::ToggleStatusBar, strings.menu_toggle_status_bar, false, true},
                    {LangBarMenuId::ToggleUiLanguage, strings.menu_toggle_ui_lang, false, true},
                    {LangBarMenuId::OpenSettings, strings.menu_open_settings, false, true},
                    {LangBarMenuId::OpenConfigFile, strings.menu_open_config_file, false, true},
                    {LangBarMenuId::OpenAbout, strings.menu_open_about, false, true}
                }};

                ScopedGdiHandle<HBRUSH> brush;
                ScopedMenu menu = bar.BuildContextMenu(brush);
                QIQI_TEST_CHECK(menu.IsValid() && brush.IsValid());
                QIQI_TEST_CHECK(::GetMenuItemCount(menu.Get()) == 13);
                QIQI_TEST_CHECK((::GetMenuState(menu.Get(), 3, MF_BYPOSITION) & MF_SEPARATOR) != 0);
                QIQI_TEST_CHECK((::GetMenuState(menu.Get(), 7, MF_BYPOSITION) & MF_SEPARATOR) != 0);
                for (const auto& item : items) {
                    const UINT flags = ::GetMenuState(menu.Get(), static_cast<UINT>(item.id), MF_BYCOMMAND);
                    QIQI_TEST_CHECK(flags != static_cast<UINT>(-1));
                    QIQI_TEST_CHECK(((flags & MF_CHECKED) != 0) == item.checked);
                    QIQI_TEST_CHECK(((flags & (MF_GRAYED | MF_DISABLED)) == 0) == item.enabled);
                    wchar_t text[256]{};
                    QIQI_TEST_CHECK(::GetMenuStringW(menu.Get(), static_cast<UINT>(item.id), text, 256, MF_BYCOMMAND) > 0);
                    QIQI_TEST_CHECK(std::wstring(text) == item.text);
                }
                MENUINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = MIM_BACKGROUND;
                QIQI_TEST_CHECK(::GetMenuInfo(menu.Get(), &info));
                QIQI_TEST_CHECK(info.hbrBack == brush.Get());
                QIQI_TEST_CHECK(bar.WindowHandle() == nullptr);
            }
        }
    }
}

void TestStatusBarTooltipLifecycle() {
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    bar.Show(true);
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);
    QIQI_TEST_CHECK(::IsWindowVisible(bar.WindowHandle()));
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    const int x = ScalePxByDpi(StatusBarUI::kButtonsOffset + 1, bar.CurrentDpi());
    const auto show_tooltip = [&bar, x]() {
        ::SendMessageW(bar.WindowHandle(), WM_MOUSELEAVE, 0, 0);
        ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(x, 1));
        QIQI_TEST_CHECK(bar.HoveredZone() == 1);
        QIQI_TEST_CHECK(!bar.ActiveTooltipText().empty());
        QIQI_TEST_CHECK(bar.TooltipWindowHandle() != nullptr);
        QIQI_TEST_CHECK(::IsWindowVisible(bar.TooltipWindowHandle()));
        QIQI_TEST_CHECK(::SendMessageW(bar.TooltipWindowHandle(), WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    };
    const auto check_tooltip_hidden = [&bar]() {
        QIQI_TEST_CHECK(bar.ActiveTooltipText().empty());
        QIQI_TEST_CHECK(!::IsWindowVisible(bar.TooltipWindowHandle()));
    };

    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONUP, 0, MAKELPARAM(x, 1));
    check_tooltip_hidden();
    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(100, 100));
    check_tooltip_hidden();
    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_NCMOUSELEAVE, 0, 0);
    QIQI_TEST_CHECK(bar.HoveredZone() == -1);
    check_tooltip_hidden();
    show_tooltip();
    bar.Show(false);
    QIQI_TEST_CHECK(!bar.IsVisible() && !::IsWindowVisible(bar.WindowHandle()));
    check_tooltip_hidden();

    bar.Show(true);
    show_tooltip();
    const HWND old_window = bar.WindowHandle();
    const HWND old_tooltip = bar.TooltipWindowHandle();
    bar.DestroyBarWindow();
    QIQI_TEST_CHECK(!::IsWindow(old_window) && !::IsWindow(old_tooltip));
    QIQI_TEST_CHECK(bar.WindowHandle() == nullptr && bar.TooltipWindowHandle() == nullptr);
    QIQI_TEST_CHECK(!bar.IsVisible() && bar.HoveredZone() == -1);
    QIQI_TEST_CHECK(bar.ActiveTooltipText().empty());
    bar.DestroyBarWindow();
    bar.Show(true);
    show_tooltip();
}

void TestMultiMonitorAndPerMonitorDpi() {
    // Simulate a secondary monitor located to the left/top of primary monitor (negative coordinates) at 144 DPI (150%)
    MonitorMetrics secondary_mon{};
    secondary_mon.work_area = RECT{-1920, -1080, 0, 0};
    secondary_mon.dpi = 144;
    SetMonitorMetricsOverrideForTest(secondary_mon);

    // 1. CandidateUI on negative-coordinate high-DPI secondary monitor
    auto* cand_ui = new CandidateUI(::GetModuleHandleW(nullptr));
    QIQI_TEST_CHECK(SUCCEEDED(cand_ui->Show(TRUE)));
    cand_ui->SetCandidates(L"abcd", {L"一", L"二", L"三"}, 5, 0, 0);

    // Move near bottom-right of secondary monitor -> should flip above and clamp within [-1920..0, -1080..0]
    RECT near_bottom_right{-50, -20, -10, -2};
    cand_ui->MoveWindowTo(near_bottom_right);
    QIQI_TEST_CHECK(cand_ui->CurrentHeight() == ScalePxByDpi(CandidateUI::kPopupHeight, 144));

    // Also test WM_DPICHANGED with suggested RECT* on CandidateUI
    RECT suggested_cand{-1500, -800, -1000, -700};
    if (cand_ui->WindowHandle()) {
        ::SendMessageW(
            cand_ui->WindowHandle(),
            WM_DPICHANGED,
            MAKEWPARAM(192, 192),
            reinterpret_cast<LPARAM>(&suggested_cand));
        QIQI_TEST_CHECK(cand_ui->CurrentHeight() == ScalePxByDpi(CandidateUI::kPopupHeight, 192));
    }
    cand_ui->DestroyPopupWindow();
    QIQI_TEST_CHECK(cand_ui->Release() == 0);

    // 2. StatusBarUI on negative-coordinate high-DPI secondary monitor
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    LangBarState st{};
    st.show_status_bar = true;
    bar.UpdateState(st);
    QIQI_TEST_CHECK(bar.CurrentDpi() == 144);
    QIQI_TEST_CHECK(bar.CurrentWidth() == ScalePxByDpi(StatusBarUI::kBarWidth, 144));
    QIQI_TEST_CHECK(bar.Position().x >= secondary_mon.work_area.left &&
                    bar.Position().x + bar.CurrentWidth() <= secondary_mon.work_area.right);
    QIQI_TEST_CHECK(bar.Position().y >= secondary_mon.work_area.top &&
                    bar.Position().y + bar.CurrentHeight() <= secondary_mon.work_area.bottom);

    // Drag status bar near top of secondary monitor and trigger tooltip (flips below bar and clamps to work_area)
    if (bar.WindowHandle()) {
        ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(-1910, -1075));
        ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(45, 18));
        QIQI_TEST_CHECK(!bar.ActiveTooltipText().empty());

        // Simulate cross-monitor WM_DPICHANGED with suggested rect and WM_DISPLAYCHANGE re-clamping
        RECT suggested_bar{-1200, -600, -900, -550};
        ::SendMessageW(
            bar.WindowHandle(),
            WM_DPICHANGED,
            MAKEWPARAM(192, 192),
            reinterpret_cast<LPARAM>(&suggested_bar));
        ::SendMessageW(bar.WindowHandle(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    }
    bar.DestroyBarWindow();

    // 3. SettingsDialog & AboutDialog centered & DPI-scaled on secondary monitor
    QiQiConfig cfg{};
    {
        const ScopedWindow dlg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(dlg.IsValid());
        RECT dlg_rc{};
        ::GetWindowRect(dlg.Get(), &dlg_rc);
        QIQI_TEST_CHECK(dlg_rc.left >= secondary_mon.work_area.left &&
                        dlg_rc.right <= secondary_mon.work_area.right);

        const ScopedWindow about_dlg(CreateAboutWindow(::GetModuleHandleW(nullptr), dlg.Get(), UiLanguage::ZhCn));
        QIQI_TEST_CHECK(about_dlg.IsValid());
        RECT about_rc{};
        ::GetWindowRect(about_dlg.Get(), &about_rc);
        QIQI_TEST_CHECK(about_rc.left >= secondary_mon.work_area.left &&
                        about_rc.right <= secondary_mon.work_area.right);
    }

    SetMonitorMetricsOverrideForTest(std::nullopt);

    // Also test QueryActiveMonitor with a non-null parent window and fallback_hwnd DPI query
    {
        const ScopedWindow parent_dlg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(parent_dlg.IsValid());
        const MonitorMetrics parent_mon = QueryActiveMonitor(parent_dlg.Get());
        QIQI_TEST_CHECK(parent_mon.dpi > 0);
        QIQI_TEST_CHECK(GetDpiForMonitorHandle(nullptr, parent_dlg.Get()) > 0);
    }
}

} // namespace

void RunStatusBarUITests() {
    TestStatusBarUI();
    TestStatusBarBoundariesAtDifferentDpis();
    TestStatusBarContextMenuState();
    TestStatusBarTooltipLifecycle();
    TestMultiMonitorAndPerMonitorDpi();
}

} // namespace qiqi::test
