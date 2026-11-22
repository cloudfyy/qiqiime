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

void TestQuickSettingsMenuKeyboardAndDismissal() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    for (const int dismissal : {0, 1, 2}) {
        LangBarState state{};
        int actions = 0;
        const ScopedWindow outside(::CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(outside);
        static int step = 0;
        static int dismiss_mode = 0;
        static HWND outside_window = nullptr;
        step = 0;
        dismiss_mode = dismissal;
        outside_window = outside.Get();
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            const HWND focus = ::GetFocus();
            switch (step++) {
                case 0:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::ToggleChinese));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_UP, 0);
                    break;
                case 1:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::OpenAbout));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_DOWN, 0);
                    break;
                case 2:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::ToggleChinese));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_SPACE, 0);
                    break;
                default:
                    ::KillTimer(nullptr, id);
                    if (dismiss_mode == 1) {
                        ::SendMessageW(menu, WM_ACTIVATE, WA_INACTIVE, 0);
                    } else if (dismiss_mode == 2) {
                        ::PostMessageW(outside_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
                    } else {
                        ::PostMessageW(focus, WM_KEYDOWN, VK_ESCAPE, 0);
                    }
                    break;
            }
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            QIQI_TEST_CHECK(action == LangBarMenuId::ToggleChinese);
            state.chinese_mode = !state.chinese_mode;
            ++actions;
        });
        ::KillTimer(nullptr, timer);
        QIQI_TEST_CHECK(actions == 1 && !state.chinese_mode);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

void TestQuickSettingsMenuCommandValidation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, {});
    QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    for (const auto action : {LangBarMenuId::OpenSettings, LangBarMenuId::OpenAbout}) {
        static LangBarMenuId closing_action{};
        closing_action = action;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            for (const UINT command : {0u, 0xffffu, static_cast<UINT>(LangBarMenuId::ToggleExtraDict)}) {
                ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
                QIQI_TEST_CHECK(FindQuickSettingsMenu() == menu && ::IsWindowVisible(menu));
            }
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleChinese), BN_DOUBLECLICKED), 0);
            QIQI_TEST_CHECK(::IsWindowVisible(menu));
            ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(static_cast<UINT>(closing_action), BN_CLICKED), 0);
            QIQI_TEST_CHECK(!::IsWindowVisible(menu));
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarState state{};
        state.primary_dict = PrimaryDict::Haifeng;
        int callbacks = 0;
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId selected) {
            QIQI_TEST_CHECK(selected == action);
            QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
            ++callbacks;
        });
        QIQI_TEST_CHECK(callbacks == 1 && state.chinese_mode);
    }
}

void TestQuickSettingsMenuFocusRestoration() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    for (const bool activate_other : {false, true}) {
        const ScopedWindow previous(::CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            20, 20, 160, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        const ScopedWindow other(::CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            40, 40, 160, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(previous && other);
        ::ShowWindow(previous.Get(), SW_SHOW);
        if (!::SetForegroundWindow(previous.Get()) || ::GetForegroundWindow() != previous.Get()) {
            // Windows ForegroundLockTimeout prevents background CTest processes from taking foreground focus
            // while the user is actively interacting with another window; skip foreground-only assertion.
            return;
        }
        static HWND next_foreground = nullptr;
        next_foreground = activate_other ? other.Get() : nullptr;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const auto* menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::GetForegroundWindow() == menu);
            if (next_foreground) {
                ::ShowWindow(next_foreground, SW_SHOW);
                ::SetForegroundWindow(next_foreground);
                QIQI_TEST_CHECK(::GetForegroundWindow() == next_foreground);
            } else {
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
            }
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, [](LangBarMenuId) {
            QIQI_TEST_CHECK(false);
        });
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
        QIQI_TEST_CHECK(::GetForegroundWindow() == (activate_other ? other.Get() : previous.Get()));
    }
}

} // namespace qiqi::test::langbar_cases
