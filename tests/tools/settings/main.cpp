#include "common/test_support.h"
#include "common/guids.h"

#include <objbase.h>

namespace qiqi {

HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};

} // namespace qiqi

int main() {
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QIQI_TEST_CHECK(SUCCEEDED(co_hr));
    const auto co_cleanup = gsl::finally([]() noexcept {
        ::CoUninitialize();
    });
    qiqi::g_dll_instance = ::GetModuleHandleW(nullptr);
    QIQI_TEST_CHECK(qiqi::g_dll_instance != nullptr);

    qiqi::test::RunSettingsDialogTests();
    qiqi::test::RunSettingsDialogEdgeTests();
    std::cout << "[PASS] All QiQi Settings tool tests succeeded.\n";
    return 0;
}
