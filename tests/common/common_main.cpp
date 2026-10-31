#include "common/test_support.h"

#include <objbase.h>

int main() {
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
