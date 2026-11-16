// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include "common/guids.h"

namespace qiqi::test {
// Borrowed from the TSF runner; other test executables keep stderr-only diagnostics.
inline std::ostream* retained_failure_log = nullptr;

inline void WriteTestFailureLine(
    std::ostream& output, const char* file, int line, const char* expression, bool keyboard_state) {
    output << "[FAIL] " << file << ":" << line << ": " << expression;
    if (keyboard_state) {
        output << ", shift=" << ::GetKeyState(VK_SHIFT)
               << ", ctrl=" << ::GetKeyState(VK_CONTROL)
               << ", alt=" << ::GetKeyState(VK_MENU)
               << ", caps=" << ::GetKeyState(VK_CAPITAL);
    }
    output << std::endl;
}

inline void ReportTestFailure(const char* file, int line, const char* expression) {
    WriteTestFailureLine(std::cerr, file, line, expression, false);
    if (retained_failure_log) {
        WriteTestFailureLine(*retained_failure_log, file, line, expression, true);
        if (!*retained_failure_log) std::cerr << "[FAIL] Writing retained test failure log failed." << std::endl;
    }
}

inline void ReportTestCleanupFailure(const char* file, int line, const char* expression) noexcept {
    const auto fallback = [file, line, expression]() noexcept {
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expression);
        std::fflush(stderr);
    };
    // A streambuf may throw any exception; diagnostics must still reach the assertion's abort.
    try {
        WriteTestFailureLine(std::cerr, file, line, expression, false);
        if (!std::cerr) fallback();
    } catch (...) {
        fallback();
    }
    if (!retained_failure_log) return;
    bool retained_failed = false;
    try {
        WriteTestFailureLine(*retained_failure_log, file, line, expression, true);
        retained_failed = !*retained_failure_log;
    } catch (...) {
        retained_failed = true;
    }
    if (retained_failed) {
        constexpr const char* message = "[FAIL] Writing retained test failure log failed.\n";
        const auto fallback_retained = [message]() noexcept {
            std::fputs(message, stderr);
            std::fflush(stderr);
        };
        try {
            std::cerr << message << std::flush;
            if (!std::cerr) fallback_retained();
        } catch (...) {
            fallback_retained();
        }
    }
}

[[noreturn]] inline void FailTestCleanup(const char* file, int line, const char* expression) noexcept {
    ReportTestCleanupFailure(file, line, expression);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::abort();
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

#define QIQI_TEST_CHECK_CLEANUP(expr)                                                            \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            ::qiqi::test::FailTestCleanup(__FILE__, __LINE__, #expr);                            \
        }                                                                                       \
    } while (false)

namespace qiqi::test {

inline std::wstring ControlText(HWND control) {
    QIQI_TEST_CHECK(::IsWindow(control));
    ::SetLastError(ERROR_SUCCESS);
    const int length = ::GetWindowTextLengthW(control);
    QIQI_TEST_CHECK(length != 0 || ::GetLastError() == ERROR_SUCCESS);
    std::wstring text(gsl::narrow_cast<size_t>(length) + 1, L'\0');
    ::SetLastError(ERROR_SUCCESS);
    const int copied = ::GetWindowTextW(control, text.data(), gsl::narrow_cast<int>(text.size()));
    QIQI_TEST_CHECK(copied != 0 || ::GetLastError() == ERROR_SUCCESS);
    text.resize(gsl::narrow_cast<size_t>(copied));
    return text;
}

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
