// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

#include <objbase.h>

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(gsl::at(gsl::span(argv, argc), 1)) == "--module-callback-test") {
        qiqi::test::common_cases::TestModuleCallbackLifetime();
        return 0;
    }

    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QIQI_TEST_CHECK(SUCCEEDED(co_hr));
    const auto co_cleanup = gsl::finally([]() noexcept {
        ::CoUninitialize();
    });

    qiqi::test::RunCommonTests();
    qiqi::test::RunD2DControlsTests();
    std::cout << "[PASS] All QiQi common module tests succeeded.\n";
    return 0;
}
