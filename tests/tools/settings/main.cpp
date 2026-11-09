#include "common/test_support.h"
#include "common/guids.h"
#include "tools/settings/internal.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, int show_cmd);

namespace {

thread_local bool startup_window_seen = false;
thread_local bool accept_default_settings = false;

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

void TestStandaloneRestoreDefaults() {
    using namespace qiqi;
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsResetStartupTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });

    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr;
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateInstance(
        CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr))));
    TfClientId client = TF_CLIENTID_NULL;
    QIQI_TEST_CHECK(SUCCEEDED(thread_mgr->Activate(&client)) && client != TF_CLIENTID_NULL);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SUCCEEDED(thread_mgr->Deactivate()));
    });
    Microsoft::WRL::ComPtr<ITfCompartmentMgr> global_mgr;
    QIQI_TEST_CHECK(SUCCEEDED(thread_mgr->GetGlobalCompartment(&global_mgr)));
    ScopedTfCompartment notification;
    QIQI_TEST_CHECK(notification.Bind(global_mgr.Get(), kGuidCompartmentConfigVersion));

    const QiQiConfig defaults;
    for (const bool accept : {false, true}) {
        QiQiConfig initial;
        initial.primary_dict = PrimaryDict::Haifeng;
        initial.page_size = 9;
        initial.enable_key_sound = true;
        initial.ui_language = UiLanguage::EnUs;
        initial.user_dict_path = L"preserved_startup_words.txt";
        QIQI_TEST_CHECK(SaveConfig(initial));
        const auto sequence = QueryRegistryConfigSequence();
        const LONG notification_before = notification.ReadInt4();
        startup_window_seen = false;
        accept_default_settings = accept;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND window = test::FindThreadWindowByClass(kSettingsWndClass);
            if (!window) return;
            ::KillTimer(nullptr, id);
            startup_window_seen = true;
            const auto before = QueryRegistryConfigSequence();
            ::SendDlgItemMessageW(window, settings_detail::kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
            QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before);
            std::array<wchar_t, 256> title{};
            QIQI_TEST_CHECK(::GetWindowTextW(
                window, title.data(), gsl::narrow_cast<int>(title.size())) > 0);
            QIQI_TEST_CHECK(std::wstring_view(title.data()).ends_with(L" *"));
            ::SendMessageW(window, WM_COMMAND,
                           MAKEWPARAM(accept_default_settings ? IDOK : IDCANCEL, BN_CLICKED), 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto stop_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
        std::array<wchar_t, 1> command{};
        QIQI_TEST_CHECK(::wWinMain(::GetModuleHandleW(nullptr), nullptr, command.data(), SW_SHOWNORMAL) == 0);
        QIQI_TEST_CHECK(startup_window_seen && !test::FindThreadWindowByClass(kSettingsWndClass));
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence + (accept ? 1 : 0));
        QIQI_TEST_CHECK((notification.ReadInt4() != notification_before) == accept);
        QiQiConfig stored;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(stored));
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(stored) == field.Value(accept ? defaults : initial));
        });
        QIQI_TEST_CHECK(stored.user_dict_path == initial.user_dict_path);
    }
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
    TestStandaloneRestoreDefaults();
    qiqi::test::RunSettingsVersionTests();
    qiqi::test::RunSettingsSaveTests();
    qiqi::test::RunSettingsMessageTests();
    qiqi::test::RunSettingsDialogTests();
    qiqi::test::RunSettingsDialogEdgeTests();
    std::cout << "[PASS] All QiQi Settings tool tests succeeded.\n";
    return 0;
}
