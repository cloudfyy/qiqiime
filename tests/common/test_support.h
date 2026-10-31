#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#define QIQI_TEST_CHECK(expr)                                                                   \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";     \
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

} // namespace message_box_detail

template <typename Action>
void WithDismissedMessage(const wchar_t* expected, Action action) {
    message_box_detail::MessageExpectation message{expected};
    auto* previous = std::exchange(message_box_detail::current_message, &message);
    const auto restore = gsl::finally([previous]() noexcept { message_box_detail::current_message = previous; });
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND box = FindThreadWindowByClass(L"#32770");
        if (!box) {
            return;
        }
        ::KillTimer(nullptr, id);
        QIQI_TEST_CHECK(message_box_detail::current_message != nullptr);
        std::array<wchar_t, 512> text{};
        QIQI_TEST_CHECK(::GetDlgItemTextW(box, 0xFFFF, text.data(), gsl::narrow_cast<int>(text.size())) > 0);
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == message_box_detail::current_message->text);
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

// Settings tool test suites
void RunSettingsDialogTests();
void RunSettingsDialogEdgeTests();
void RunSettingsVersionTests();
void RunSettingsSaveTests();

} // namespace qiqi::test
