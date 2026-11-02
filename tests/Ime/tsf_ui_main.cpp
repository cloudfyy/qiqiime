#include "common/test_support.h"
#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <iostream>
#include <span>

int wmain(int argc, wchar_t* argv[]) {
    QIQI_TEST_CHECK(argv != nullptr);
    const std::span<wchar_t* const> args(argv, gsl::narrow_cast<size_t>(argc));
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--tsf-focus-diagnostics") {
        qiqi::test::RunTsfFocusDiagnosticsScenario();
        return 0;
    }
    if (args.size() == 4 && std::wstring_view(gsl::at(args, 1)) == L"--etw-host") {
        qiqi::test::RunTsfEtwHostScenario(gsl::at(args, 2), gsl::at(args, 3));
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--etw-integration") {
        return qiqi::test::RunTsfEtwIntegrationTests();
    }

    qiqi::test::RunLangBarMenuTests();
    qiqi::test::RunCandidateUITests();
    qiqi::test::RunStatusBarUITests();
    qiqi::test::RunTsfComTests();
    qiqi::test::RunTsfServiceTests();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
