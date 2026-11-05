#include "common/test_support.h"
#include "common/guids.h"
#include "tools/settings/window.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, int show_cmd);

namespace {

thread_local bool startup_window_seen = false;

void TestStandaloneSettingsStartup() {
    using namespace qiqi;
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsStartupTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
        const LSTATUS result = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    });
    QIQI_TEST_CHECK(SaveConfigToRegistry(QiQiConfig{}));
    const auto sequence = QueryRegistryConfigSequence();
    for (int attempt = 0; attempt < 2; ++attempt) {
        startup_window_seen = false;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND window = test::FindThreadWindowByClass(kSettingsWndClass);
            if (!window) return;
            ::KillTimer(nullptr, id);
            startup_window_seen = true;
            QIQI_TEST_CHECK(::IsWindowVisible(window));
            ::ShowWindow(window, SW_MINIMIZE);
            QIQI_TEST_CHECK(::IsIconic(window));
            std::array<wchar_t, 1> command{};
            QIQI_TEST_CHECK(::wWinMain(::GetModuleHandleW(nullptr), nullptr, command.data(), SW_SHOWNORMAL) == 0);
            QIQI_TEST_CHECK(test::FindThreadWindowByClass(kSettingsWndClass) == window);
            QIQI_TEST_CHECK(!::IsIconic(window));
            ::SendMessageW(window, WM_CLOSE, 0, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto stop_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
        std::array<wchar_t, 1> command{};
        QIQI_TEST_CHECK(::wWinMain(::GetModuleHandleW(nullptr), nullptr, command.data(), SW_SHOWNORMAL) == 0);
        QIQI_TEST_CHECK(startup_window_seen);
        QIQI_TEST_CHECK(!test::FindThreadWindowByClass(kSettingsWndClass));
    }
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence);
}

} // namespace

int main() {
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QIQI_TEST_CHECK(SUCCEEDED(co_hr));
    const auto co_cleanup = gsl::finally([]() noexcept {
        ::CoUninitialize();
    });
    qiqi::g_dll_instance = ::GetModuleHandleW(nullptr);
    QIQI_TEST_CHECK(qiqi::g_dll_instance != nullptr);

    TestStandaloneSettingsStartup();
    qiqi::test::RunSettingsVersionTests();
    qiqi::test::RunSettingsSaveTests();
    qiqi::test::RunSettingsMessageTests();
    qiqi::test::RunSettingsDialogTests();
    qiqi::test::RunSettingsDialogEdgeTests();
    std::cout << "[PASS] All QiQi Settings tool tests succeeded.\n";
    return 0;
}
