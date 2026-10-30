#include "common.h"
#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <iostream>

int main() {
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    qiqi::test::RunLangBarMenuTests();
    qiqi::test::RunCandidateUITests();
    qiqi::test::RunSettingsDialogTests();
    qiqi::test::RunStatusBarUITests();
    qiqi::test::RunTsfComTests();
    qiqi::test::RunTsfServiceTests();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
