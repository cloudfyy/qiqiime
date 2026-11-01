#include "common/test_support.h"
#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    if (argc == 2 && std::wstring_view(argv[1]) == L"--tsf-focus-diagnostics") {
        qiqi::test::RunTsfFocusDiagnosticsScenario();
        return 0;
    }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--etw-host") {
        qiqi::test::RunTsfEtwHostScenario(argv[2], argv[3]);
        return 0;
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--etw-integration") {
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
