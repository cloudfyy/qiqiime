// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "ime/ui/fltbarbtn.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "ime/ui/langtest.h"

namespace qiqi::test::langbar_cases {

using Microsoft::WRL::ComPtr;

HWND FindQuickSettingsMenu() noexcept {
    return FindThreadWindowByClass(kMenuWndClass);
}

void CloseQuickSettingsMenu() {
    const HWND menu = FindQuickSettingsMenu();
    QIQI_TEST_CHECK(menu != nullptr);
    ::SendMessageW(menu, WM_CLOSE, 0, 0);
}

void TestQuickSettingsMenu() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
    });
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        SetTaskbarDarkOverrideForTest(!dark);
        LangBarState state{};
        state.ui_language = UiLanguage::ZhCn;
        std::vector<LangBarMenuId> actions;
        bool closed_before_opening_window = false;
        static int step = 0;
        step = 0;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu != nullptr && ::IsWindowVisible(menu));
            const auto check_menu_font = [menu](UiLanguage language) {
                NONCLIENTMETRICSW metrics{};
                metrics.cbSize = sizeof(metrics);
                QIQI_TEST_CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0));
                const auto expected = language == UiLanguage::ZhCn && CandidateFontFamilyAvailable(kYaHeiUiFamily)
                    ? std::wstring_view(kYaHeiUiFamily) : LogFontFaceNameView(metrics.lfMenuFont);
                const auto font = reinterpret_cast<HFONT>(::SendDlgItemMessageW(
                    menu, static_cast<int>(LangBarMenuId::ToggleChinese), WM_GETFONT, 0, 0));
                LOGFONTW actual{};
                QIQI_TEST_CHECK(::GetObjectW(font, sizeof(actual), &actual) == sizeof(actual));
                QIQI_TEST_CHECK(LogFontFaceNameView(actual) == expected);
            };
            if (step == 0) check_menu_font(UiLanguage::ZhCn);
            static constexpr std::array commands{LangBarMenuId::ToggleChinese,     LangBarMenuId::ToggleFullWidth,
                                                 LangBarMenuId::TogglePunctuation, LangBarMenuId::ToggleStatusBar,
                                                 LangBarMenuId::ToggleUiLanguage,  LangBarMenuId::OpenSettings};
            const HWND original = menu;
            const auto command = commands.at(gsl::narrow_cast<size_t>(step));
            if (command == LangBarMenuId::OpenSettings)
                ::KillTimer(nullptr, id);
            ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(static_cast<UINT>(command), BN_CLICKED), 0);
            if (LangBarButton::KeepsMenuOpen(command)) {
                QIQI_TEST_CHECK(FindQuickSettingsMenu() == original && ::IsWindowVisible(original));
            } else {
                QIQI_TEST_CHECK(!::IsWindowVisible(original));
            }
            if (step == 4) {
                check_menu_font(UiLanguage::EnUs);
                std::array<wchar_t, 256> label{};
                ::GetDlgItemTextW(menu, static_cast<int>(LangBarMenuId::ToggleChinese), label.data(),
                                  gsl::narrow_cast<int>(label.size()));
                QIQI_TEST_CHECK(std::wstring_view(label.data()) == kEnUsStrings.menu_toggle_chinese);
                ::SendMessageW(menu, WM_THEMECHANGED, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetTaskbarThemePalette().window_bg);
            }
            ++step;
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            actions.push_back(action);
            switch (action) {
                case LangBarMenuId::ToggleChinese:
                    state.chinese_mode = !state.chinese_mode;
                    break;
                case LangBarMenuId::ToggleFullWidth:
                    state.full_width = !state.full_width;
                    break;
                case LangBarMenuId::TogglePunctuation:
                    state.chinese_punct = !state.chinese_punct;
                    break;
                case LangBarMenuId::ToggleStatusBar:
                    state.show_status_bar = !state.show_status_bar;
                    break;
                case LangBarMenuId::ToggleUiLanguage:
                    state.ui_language = UiLanguage::EnUs;
                    break;
                case LangBarMenuId::OpenSettings:
                    closed_before_opening_window = FindQuickSettingsMenu() == nullptr;
                    break;
                default:
                    QIQI_TEST_CHECK(false);
                    break;
            }
        });
        ::KillTimer(nullptr, timer);
        if (actions.size() != 6) {
            std::cerr << "[MENU-DIAGNOSTIC] actions=" << actions.size() << ", timer_step=" << step << ", ids=";
            for (const auto action : actions) std::cerr << static_cast<UINT>(action) << ',';
            std::cerr << '\n';
        }
        QIQI_TEST_CHECK(actions.size() == 6);
        QIQI_TEST_CHECK(!state.chinese_mode && state.full_width && !state.chinese_punct);
        QIQI_TEST_CHECK(state.primary_dict == PrimaryDict::Jidian && state.enable_jidian_extra);
        QIQI_TEST_CHECK(state.show_status_bar && state.ui_language == UiLanguage::EnUs);
        QIQI_TEST_CHECK(closed_before_opening_window);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
    SetThemeDarkOverrideForTest(std::nullopt);
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenHelp));
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenAbout));
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenSettings));
}

namespace {

LRESULT CALLBACK CheckMenuButtonFontLifetime(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    if (message == WM_NCDESTROY) {
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(hwnd, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(::GetObjectType(font) == OBJ_FONT);
        ++*reinterpret_cast<int*>(data);
        ::RemoveWindowSubclass(hwnd, CheckMenuButtonFontLifetime, id);
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
}

} // namespace

void TestQuickSettingsMenuResourceLifetime() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    auto monitor = QueryMonitorForPoint(POINT{200, 200});
    monitor.dpi = 96;
    SetMonitorMetricsOverrideForTest(monitor);
    LangBarState state{};
    int actions = 0;
    static int destroyed_buttons = 0;
    destroyed_buttons = 0;
    LangBarButton::ActionCallback callback = [&](LangBarMenuId action) {
        QIQI_TEST_CHECK(action == LangBarMenuId::ToggleFullWidth);
        state.full_width = !state.full_width;
        ++actions;
        callback = {};
    };
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
        QIQI_TEST_CHECK(::SetWindowSubclass(button, CheckMenuButtonFontLifetime, 1,
                                           reinterpret_cast<DWORD_PTR>(&destroyed_buttons)));
        ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        const DWORD gdi_before = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
        QIQI_TEST_CHECK(gdi_before > 0);
        for (int i = 0; i < 16; ++i) {
            const auto old_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
            QIQI_TEST_CHECK(::GetObjectType(old_font) == OBJ_FONT);
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleFullWidth), BN_CLICKED), 0);
            const auto new_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
            QIQI_TEST_CHECK(::GetObjectType(new_font) == OBJ_FONT);
            ::GdiFlush();
            QIQI_TEST_CHECK(new_font == old_font);
            ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        ::GdiFlush();
        QIQI_TEST_CHECK(::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before);

        const auto* old_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        auto monitor = QueryMonitorForPoint(POINT{200, 200});
        monitor.dpi = 144;
        SetMonitorMetricsOverrideForTest(monitor);
        ::SendMessageW(menu, WM_COMMAND,
                       MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleFullWidth), BN_CLICKED), 0);
        const auto new_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(new_font != old_font && ::GetObjectType(new_font) == OBJ_FONT);

        const ScopedMemoryDC dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(dc);
        constexpr COLORREF text_color = RGB(17, 33, 65);
        ::SetTextColor(dc.Get(), text_color);
        ::SetBkMode(dc.Get(), OPAQUE);
        const HGDIOBJ original_font = ::GetCurrentObject(dc.Get(), OBJ_FONT);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
        draw.hwndItem = button;
        draw.hDC = dc.Get();
        ::GetClientRect(button, &draw.rcItem);
        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
        QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == text_color);
        QIQI_TEST_CHECK(::GetBkMode(dc.Get()) == OPAQUE);
        QIQI_TEST_CHECK(::GetCurrentObject(dc.Get(), OBJ_FONT) == original_font);
        ::SendMessageW(menu, WM_CLOSE, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, callback);
    QIQI_TEST_CHECK(actions == 17 && state.full_width && !callback);
    QIQI_TEST_CHECK(destroyed_buttons == 1 && FindQuickSettingsMenu() == nullptr);
}


void TestQuickSettingsMenuSkin() {
    const TsfTestCaseTrace trace(__func__);
    const auto old_app = detail::ThemeDarkOverrideStorage();
    const auto old_windows = detail::TaskbarDarkOverrideStorage();
    const auto restore = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(old_app);
        SetTaskbarDarkOverrideForTest(old_windows);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    SetHighContrastOverrideForTest(false);
    const auto& skin = GetBuiltinImeSkins().front();
    static SkinPalette expected_palette;
    static bool observed = false;
    for (const bool app_dark : {false, true}) {
        for (const bool windows_dark : {false, true}) {
            SetThemeDarkOverrideForTest(app_dark);
            SetTaskbarDarkOverrideForTest(windows_dark);
            expected_palette = windows_dark ? skin.palettes.dark : skin.palettes.light;
            const auto snapshot = std::optional<ImeSkinSnapshot>{
                ImeSkinSnapshot{skin.id, skin.palettes}};
            const auto windows_appearance = ResolveImeSkinAppearance(
                {UiTheme::System, UiTextScale::Percent100, false},
                snapshot, SystemThemeSource::Windows);
            QIQI_TEST_CHECK(windows_appearance.colors == expected_palette);
            if (app_dark) continue;
            observed = false;
            const UINT_PTR timer = ::SetTimer(nullptr, 0, 15,
                [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                const HWND menu = FindQuickSettingsMenu();
                if (!menu) return;
                observed = true;
                ::KillTimer(nullptr, id);
                const auto& palette = expected_palette;
                const ScopedWindowDC screen(menu);
                QIQI_TEST_CHECK(::GetPixel(
                    screen.Get(), 1, 1) == palette.Get(SkinColor::Background));
                const HWND button = ::GetDlgItem(
                    menu, static_cast<int>(LangBarMenuId::ToggleChinese));
                RECT bounds{};
                QIQI_TEST_CHECK(::GetClientRect(button, &bounds));
                const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
                const ScopedGdiHandle<HBITMAP> bitmap(
                    ::CreateCompatibleBitmap(screen.Get(), bounds.right, bounds.bottom));
                QIQI_TEST_CHECK(dc && bitmap);
                const auto count_painted_pixels =
                    [&](UINT state, COLORREF expected, const RECT& area,
                        bool exclude_expected = false) {
                    const ScopedSelectObject selected_bitmap(dc.Get(), bitmap.Get());
                    DRAWITEMSTRUCT draw{};
                    draw.CtlType = ODT_BUTTON;
                    draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
                    draw.hwndItem = button;
                    draw.hDC = dc.Get();
                    draw.rcItem = bounds;
                    draw.itemState = state;
                    ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID,
                        reinterpret_cast<LPARAM>(&draw));
                    const int left = std::max(0L, area.left);
                    const int right = std::min(bounds.right, area.right);
                    const int top = std::max(0L, area.top);
                    const int bottom = std::min(bounds.bottom, area.bottom);
                    size_t matches = 0;
                    for (int y = top; y < bottom; ++y) {
                        for (int x = left; x < right; ++x) {
                            const bool match = ::GetPixel(dc.Get(), x, y) == expected;
                            if (match != exclude_expected) ++matches;
                        }
                    }
                    return matches;
                };
                QIQI_TEST_CHECK(count_painted_pixels(ODS_SELECTED,
                    palette.Get(SkinColor::MenuHoverBackground), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_SELECTED,
                    palette.Get(SkinColor::MenuHoverText), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_DISABLED,
                    palette.Get(SkinColor::PageDisabledText), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_FOCUS,
                    palette.Get(SkinColor::SelectedBorder), bounds) > 0);
                const RECT gutter{0, 0, bounds.bottom, bounds.bottom};
                QIQI_TEST_CHECK(count_painted_pixels(0,
                    palette.Get(SkinColor::Background), gutter, true) > 0);

                SetHighContrastOverrideForTest(true);
                ::SendMessageW(menu, WM_SETTINGCHANGE, 0, 0);
                QIQI_TEST_CHECK(count_painted_pixels(
                    ODS_SELECTED, ::GetSysColor(COLOR_HIGHLIGHT), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(
                    ODS_SELECTED, ::GetSysColor(COLOR_HIGHLIGHTTEXT), bounds) > 0);
                SetHighContrastOverrideForTest(false);
                ::SendMessageW(menu, WM_CLOSE, 0, 0);
            });
            QIQI_TEST_CHECK(timer != 0);
            const auto stop = gsl::finally([timer]() noexcept {
                ::KillTimer(nullptr, timer);
            });
            LangBarState state;
            state.chinese_mode = true;
            state.skin_snapshot = ImeSkinSnapshot{skin.id, skin.palettes};
            LangBarButton::ShowSharedPopupMenu(
                state, POINT{200, 200}, [](LangBarMenuId) noexcept {});
            QIQI_TEST_CHECK(observed);
        }
    }
}

void TestQuickSettingsMenuLiteralAmpersand() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    LangBarState state{};
    state.ui_language = UiLanguage::EnUs;
    state.display.theme = UiTheme::Light;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenHelp));
        RECT rect{};
        QIQI_TEST_CHECK(button && ::GetClientRect(button, &rect));
        const ScopedWindowDC screen;
        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
        const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
        QIQI_TEST_CHECK(screen && dc && bitmap);
        const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = static_cast<UINT>(LangBarMenuId::OpenHelp);
        draw.hwndItem = button;
        draw.hDC = dc.Get();
        draw.rcItem = rect;
        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
        const auto pixels = [&]() {
            std::vector<COLORREF> result;
            result.reserve(gsl::narrow_cast<size_t>(rect.right * rect.bottom));
            for (int y = 0; y < rect.bottom; ++y) {
                for (int x = 0; x < rect.right; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel != CLR_INVALID);
                    result.push_back(pixel);
                }
            }
            return result;
        };
        const auto actual = pixels();
        const auto appearance = ResolveDisplayAppearance({UiTheme::Light});
        const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(appearance.palette.window_bg));
        QIQI_TEST_CHECK(background && ::FillRect(dc.Get(), &rect, background.Get()));
        ::SetBkMode(dc.Get(), TRANSPARENT);
        ::SetTextColor(dc.Get(), appearance.palette.text_primary);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        const ScopedSelectObject select_font(dc.Get(), font);
        RECT text_rect = rect;
        text_rect.left += rect.bottom + rect.bottom / 4;
        text_rect.right -= rect.bottom / 2;
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), GetUiStrings(UiLanguage::EnUs).menu_open_help, -1, &text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX));
        QIQI_TEST_CHECK(actual == pixels());
        QIQI_TEST_CHECK(::FillRect(dc.Get(), &rect, background.Get()));
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), GetUiStrings(UiLanguage::EnUs).menu_open_help, -1, &text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
        QIQI_TEST_CHECK(actual != pixels());
        ::SendMessageW(menu, WM_CLOSE, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [](LangBarMenuId) {
        QIQI_TEST_CHECK(false);
    });
}

void TestQuickSettingsMenuQuitMessage() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() != nullptr);
        ::PostQuitMessage(42);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, [](LangBarMenuId) {
        QIQI_TEST_CHECK(false);
    });
    QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    QIQI_TEST_CHECK(::PostThreadMessageW(::GetCurrentThreadId(), WM_APP, 0, 0));
    CheckQuitMessage(42);
    MSG message{};
    QIQI_TEST_CHECK(!::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
}

} // namespace qiqi::test::langbar_cases
