#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

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

// Common & Engine test suites
void RunCommonTests();
void RunEngineDictTests();
void RunEngineInputTests();
void RunEngineModesPunctTests();

// UI & TSF test suites
void RunLangBarMenuTests();
void RunCandidateUITests();
void RunSettingsDialogTests();
void RunStatusBarUITests();
void RunTsfComTests();
void RunTsfServiceTests();

} // namespace qiqi::test
