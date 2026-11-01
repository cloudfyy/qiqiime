#include "common/test_support.h"
#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <iostream>

int main(int argc, char* argv[]) {
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    if (argc == 2 && std::string_view(argv[1]) == "--tsf-focus-diagnostics") {
        qiqi::test::RunTsfFocusDiagnosticsScenario();
        return 0;
    }

    qiqi::test::RunLangBarMenuTests();
    qiqi::test::RunCandidateUITests();
    qiqi::test::RunStatusBarUITests();
    qiqi::test::RunTsfComTests();
    qiqi::test::RunTsfServiceTests();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
