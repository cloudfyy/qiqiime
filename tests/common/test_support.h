// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include "common/guids.h"

namespace qiqi::test {
// Borrowed from the TSF runner; other test executables keep stderr-only diagnostics.
inline std::ostream* retained_failure_log = nullptr;

inline void ReportTestFailure(const char* file, int line, const char* expression) {
    std::cerr << "[FAIL] " << file << ":" << line << ": " << expression << std::endl;
    if (retained_failure_log) {
        *retained_failure_log << "[FAIL] " << file << ":" << line << ": " << expression
                              << ", shift=" << ::GetKeyState(VK_SHIFT)
                              << ", ctrl=" << ::GetKeyState(VK_CONTROL)
                              << ", alt=" << ::GetKeyState(VK_MENU)
                              << ", caps=" << ::GetKeyState(VK_CAPITAL) << std::endl;
        if (!*retained_failure_log) std::cerr << "[FAIL] Writing retained test failure log failed." << std::endl;
    }
}
} // namespace qiqi::test

#define QIQI_TEST_CHECK(expr)                                                                   \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            ::qiqi::test::ReportTestFailure(__FILE__, __LINE__, #expr);                          \
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);                         \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

namespace qiqi::test {

inline HWND FindThreadWindowByClass(std::wstring_view class_name) noexcept {
    struct SearchCtx {
        std::wstring_view target;
        HWND found = nullptr;
    } ctx{class_name, nullptr};

    ::EnumThreadWindows(
        ::GetCurrentThreadId(),
        [](HWND hwnd, LPARAM param) noexcept -> BOOL {
            auto* s = reinterpret_cast<SearchCtx*>(param);
            std::array<wchar_t, 128> name{};
            ::GetClassNameW(hwnd, name.data(), gsl::narrow_cast<int>(name.size()));
            if (std::wstring_view(name.data()) == s->target) {
                s->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

namespace message_box_detail {

struct MessageExpectation {
    const wchar_t* text;
    bool seen = false;
};

inline thread_local MessageExpectation* current_message = nullptr;

inline bool MessageTextMatches(HWND box) noexcept {
    if (!current_message || !current_message->text) return false;
    std::array<wchar_t, 512> text{};
    if (::GetDlgItemTextW(box, 0xFFFF, text.data(), gsl::narrow_cast<int>(text.size())) <= 0) return false;
    return std::wstring_view(text.data()) == current_message->text;
}

} // namespace message_box_detail

template <typename Action>
void WithDismissedMessage(const wchar_t* expected, Action action) {
    message_box_detail::MessageExpectation message{expected};
    auto* previous = std::exchange(message_box_detail::current_message, &message);
    const auto restore = gsl::finally([previous]() noexcept { message_box_detail::current_message = previous; });
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND box = FindThreadWindowByClass(kSettingsMessageWndClass);
        if (!box) {
            return;
        }
        ::KillTimer(nullptr, id);
        QIQI_TEST_CHECK(message_box_detail::MessageTextMatches(box));
        message_box_detail::current_message->seen = true;
        const HWND button = ::FindWindowExW(box, nullptr, L"Button", nullptr);
        QIQI_TEST_CHECK(button != nullptr);
        ::SendMessageW(button, BM_CLICK, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    action();
    QIQI_TEST_CHECK(message.seen);
}

template <typename Action>
void WithDismissedNativeMessage(const wchar_t* expected, Action action) {
    message_box_detail::MessageExpectation message{expected};
    auto* previous = std::exchange(message_box_detail::current_message, &message);
    const auto restore = gsl::finally([previous]() noexcept { message_box_detail::current_message = previous; });
    // Close native OK dialogs before activation instead of waiting for a timer.
    const HHOOK hook = ::SetWindowsHookExW(WH_CBT, [](int code, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
        if (code == HCBT_ACTIVATE) {
            const HWND box = reinterpret_cast<HWND>(wparam);
            std::array<wchar_t, 32> name{};
            QIQI_TEST_CHECK(::GetClassNameW(box, name.data(), gsl::narrow_cast<int>(name.size())) > 0);
            if (std::wstring_view(name.data()) == L"#32770") {
                QIQI_TEST_CHECK(message_box_detail::MessageTextMatches(box));
                message_box_detail::current_message->seen = true;
                QIQI_TEST_CHECK(::GetDlgItem(box, IDOK) != nullptr);
                ::SendMessageW(box, WM_COMMAND, IDOK, 0);
                return 1;
            }
        }
        return ::CallNextHookEx(nullptr, code, wparam, lparam);
    }, nullptr, ::GetCurrentThreadId());
    QIQI_TEST_CHECK(hook != nullptr);
    const auto unhook = gsl::finally([hook]() noexcept { QIQI_TEST_CHECK(::UnhookWindowsHookEx(hook)); });
    action();
    QIQI_TEST_CHECK(message.seen);
}

// Common module test suites
void RunCommonTests();
void RunD2DControlsTests();

// IME test suites
void RunEngineDictTests();
void RunEngineInputTests();
void RunEngineModesPunctTests();

void RunLangBarMenuTests();
void RunCandidateUITests();
void RunStatusBarUITests();
void RunTsfComTests();
void RunTsfServiceTests();
void RunTsfFocusDiagnosticsScenario();
void RunTsfEtwHostScenario(const wchar_t* event_base, const wchar_t* parent_directory);
int RunTsfEtwIntegrationTests();

// Settings tool test suites
void RunSettingsDialogTests();
void RunSettingsDialogEdgeTests();
void RunSettingsVersionTests();
void RunSettingsSaveTests();
void RunSettingsMessageTests();

} // namespace qiqi::test
