// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
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

void TestQuickSettingsMenuWorkArea() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    for (const UINT dpi : {96u, 120u, 144u, 168u, 192u, 288u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1280, -720, 0, 0}, dpi});
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(menu, &rect));
            QIQI_TEST_CHECK(rect.left >= -1280 && rect.top >= -720);
            QIQI_TEST_CHECK(rect.right <= 0 && rect.bottom <= 0);
            const auto monitor = QueryMonitorForPoint(POINT{-1, -1}, menu);
            const auto model = LangBarButton::BuildSharedMenuItems(LangBarState{});
            int base_height = 8;
            for (const auto& item : model) base_height += item.IsSeparator() ? 8 : 28;
            const int available_height = std::max(1, gsl::narrow_cast<int>(
                monitor.work_area.bottom - monitor.work_area.top) - 2);
            const UINT expected_dpi = std::min(monitor.dpi, gsl::narrow_cast<UINT>(
                std::max(1, ::MulDiv(std::max(1, available_height -
                    gsl::narrow_cast<int>(model.size()) - 2), 96, base_height))));
            int row_top = ScalePxByDpi(4, expected_dpi);
            for (const auto& item : model) {
                const int row_height = ScalePxByDpi(item.IsSeparator() ? 8 : 28, expected_dpi);
                if (!item.IsSeparator()) {
                    const HWND button = ::GetDlgItem(menu, gsl::narrow_cast<int>(std::to_underlying(item.id)));
                    QIQI_TEST_CHECK(button);
                    RECT button_rect{};
                    QIQI_TEST_CHECK(::GetWindowRect(button, &button_rect));
                    POINT origin{button_rect.left, button_rect.top};
                    QIQI_TEST_CHECK(::ScreenToClient(menu, &origin));
                    QIQI_TEST_CHECK(origin.x == 0 && origin.y == row_top);
                    QIQI_TEST_CHECK(button_rect.bottom - button_rect.top == row_height);
                    const auto font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
                    LOGFONTW description{};
                    QIQI_TEST_CHECK(::GetObjectW(font, sizeof(description), &description) == sizeof(description));
                    QIQI_TEST_CHECK(description.lfHeight == -ScalePxByDpi(14, expected_dpi));
                }
                row_top += row_height;
            }
            ::SendMessageW(menu, WM_CLOSE, 0, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(
            LangBarState{}, POINT{-1, -1}, [](LangBarMenuId) { QIQI_TEST_CHECK(false); }, true);
        ::KillTimer(nullptr, timer);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

void TestQuickSettingsMenuComfortScrolling() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1280, -320, 0, 0}, 144});
    for (const auto scale : {UiTextScale::Percent150, UiTextScale::Percent200}) {
        for (const auto theme : {UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
            static DisplayPreferences display{};
            display = DisplayPreferences{theme, scale, true};
            static int step = 0;
            step = 0;
            static LONG first_top = 0;
            const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
                const HWND menu = FindQuickSettingsMenu();
                QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
                const HWND first = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
                const HWND last = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenAbout));
                RECT client{};
                RECT first_rect{};
                RECT last_rect{};
                QIQI_TEST_CHECK(::GetClientRect(menu, &client));
                QIQI_TEST_CHECK(::GetWindowRect(first, &first_rect));
                QIQI_TEST_CHECK(::GetWindowRect(last, &last_rect));
                POINT first_origin{first_rect.left, first_rect.top};
                POINT last_origin{last_rect.left, last_rect.top};
                QIQI_TEST_CHECK(::ScreenToClient(menu, &first_origin));
                QIQI_TEST_CHECK(::ScreenToClient(menu, &last_origin));
                switch (step++) {
                    case 0: {
                        RECT menu_rect{};
                        QIQI_TEST_CHECK(::GetWindowRect(menu, &menu_rect));
                        QIQI_TEST_CHECK(menu_rect.top >= -320 && menu_rect.bottom <= 0);
                        QIQI_TEST_CHECK(menu_rect.left >= -1280 && menu_rect.right <= 0);
                        const auto font = reinterpret_cast<HFONT>(::SendMessageW(first, WM_GETFONT, 0, 0));
                        LOGFONTW description{};
                        QIQI_TEST_CHECK(::GetObjectW(font, sizeof(description), &description) == sizeof(description));
                        QIQI_TEST_CHECK(description.lfHeight == -ScaleDisplayPx(14, 144, display.text_scale));
                        QIQI_TEST_CHECK(first_rect.bottom - first_rect.top == ScaleDisplayPx(44, 144, display.text_scale));
                        QIQI_TEST_CHECK(first_rect.bottom - first_rect.top >= ScalePxByDpi(44, 144));
                        std::array<wchar_t, 256> label{};
                        ::GetWindowTextW(first, label.data(), gsl::narrow_cast<int>(label.size()));
                        QIQI_TEST_CHECK(std::wstring_view(label.data()).find(L"Ctrl") == std::wstring_view::npos);
                        const ScopedWindowDC screen;
                        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
                        const int row_width = first_rect.right - first_rect.left;
                        const int row_height = first_rect.bottom - first_rect.top;
                        const ScopedGdiHandle<HBITMAP> bitmap(
                            ::CreateCompatibleBitmap(screen.Get(), row_width, row_height));
                        QIQI_TEST_CHECK(screen && dc && bitmap);
                        const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
                        DRAWITEMSTRUCT draw{};
                        draw.CtlType = ODT_BUTTON;
                        draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
                        draw.hwndItem = first;
                        draw.hDC = dc.Get();
                        draw.rcItem = RECT{0, 0, row_width, row_height};
                        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
                        const auto appearance = ResolveDisplayAppearance(display);
                        QIQI_TEST_CHECK(::GetPixel(dc.Get(), row_width - 2, row_height / 2) == (appearance.system_high_contrast ?
                            ::GetSysColor(COLOR_MENU) : appearance.palette.window_bg));
                        const HWND scrollbar = ::FindWindowExW(menu, nullptr, L"SCROLLBAR", nullptr);
                        QIQI_TEST_CHECK(scrollbar && ::IsWindowVisible(scrollbar));
                        ::SendMessageW(menu, WM_VSCROLL, SB_BOTTOM, reinterpret_cast<LPARAM>(scrollbar));
                        ::PostMessageW(first, WM_KEYDOWN, VK_UP, 0);
                        break;
                    }
                    case 1:
                        QIQI_TEST_CHECK(::GetFocus() == last);
                        QIQI_TEST_CHECK(last_origin.y >= 0 &&
                            last_origin.y + last_rect.bottom - last_rect.top <= client.bottom);
                        ::PostMessageW(last, WM_KEYDOWN, VK_DOWN, 0);
                        break;
                    case 2:
                        QIQI_TEST_CHECK(::GetFocus() == first);
                        QIQI_TEST_CHECK(first_origin.y >= 0 &&
                            first_origin.y + first_rect.bottom - first_rect.top <= client.bottom);
                        first_top = first_origin.y;
                        ::PostMessageW(menu, WM_MOUSEWHEEL,
                            MAKEWPARAM(0, gsl::narrow_cast<WORD>(-WHEEL_DELTA)), 0);
                        break;
                    default: {
                        QIQI_TEST_CHECK(first_origin.y < first_top);
                        RECT clip{};
                        QIQI_TEST_CHECK(::GetWindowRgnBox(first, &clip) != ERROR && clip.top > 0);
                        ::KillTimer(nullptr, id);
                        ::SendMessageW(menu, WM_CLOSE, 0, 0);
                        break;
                    }
                }
            });
            QIQI_TEST_CHECK(timer != 0);
            const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
            LangBarState state{};
            state.display = display;
            LangBarButton::ShowSharedPopupMenu(state, POINT{-1, -1}, [](LangBarMenuId) {
                QIQI_TEST_CHECK(false);
            }, true);
        }
    }
}

void TestQuickSettingsMenuNavigation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    static int step = 0;
    step = 0;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        switch (step++) {
            case 0:
                QIQI_TEST_CHECK(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleExtraDict)) == nullptr);
                ::SetFocus(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::TogglePunctuation)));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_DOWN, 0);
                break;
            case 1:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::ToggleStatusBar));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_UP, 0);
                break;
            case 2:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::TogglePunctuation));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_TAB, 0);
                break;
            default:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::ToggleStatusBar));
                ::KillTimer(nullptr, id);
                ::SetFocus(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenSettings)));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
                break;
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarState state{};
    state.primary_dict = PrimaryDict::Haifeng;
    int callbacks = 0;
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
        QIQI_TEST_CHECK(action == LangBarMenuId::OpenSettings);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
        ++callbacks;
    });
    QIQI_TEST_CHECK(callbacks == 1 && step == 4);
}

namespace {

int CountQuickSettingsCheckPixels(HWND menu) {
    const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
    RECT rect{};
    QIQI_TEST_CHECK(::GetClientRect(button, &rect));
    QIQI_TEST_CHECK(rect.right > rect.bottom && rect.bottom > 0);
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_BUTTON;
    draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
    draw.hwndItem = button;
    draw.hDC = dc.Get();
    draw.rcItem = rect;
    ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    const COLORREF background = ::GetPixel(dc.Get(), 0, 0);
    QIQI_TEST_CHECK(background != CLR_INVALID);
    int pixels = 0;
    for (int y = 0; y < rect.bottom; ++y) {
        for (int x = 0; x < rect.bottom; ++x) {
            if (::GetPixel(dc.Get(), x, y) != background) {
                ++pixels;
            }
        }
    }
    return pixels;
}

} // namespace

void TestQuickSettingsMenuLivePainting() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
    });
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        SetTaskbarDarkOverrideForTest(!dark);
        static bool updated_dark = false;
        updated_dark = dark;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            {
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(!updated_dark).window_bg);
            }
            QIQI_TEST_CHECK(CountQuickSettingsCheckPixels(menu) > 0);
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleChinese), BN_CLICKED), 0);
            QIQI_TEST_CHECK(::IsWindowVisible(menu));
            QIQI_TEST_CHECK(CountQuickSettingsCheckPixels(menu) == 0);
            SetThemeDarkOverrideForTest(!updated_dark);
            for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                ::SendMessageW(menu, message, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(!updated_dark).window_bg);
            }
            SetTaskbarDarkOverrideForTest(updated_dark);
            for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                ::SendMessageW(menu, message, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(updated_dark).window_bg);
            }
            ::SendMessageW(menu, WM_CLOSE, 0, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarState state{};
        int callbacks = 0;
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            QIQI_TEST_CHECK(action == LangBarMenuId::ToggleChinese);
            state.chinese_mode = !state.chinese_mode;
            ++callbacks;
        });
        QIQI_TEST_CHECK(callbacks == 1 && !state.chinese_mode);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

} // namespace qiqi::test::langbar_cases
