#include "common.h"
#include "tsf_mocks.h"
#include "common/guids.h"
#include "common/scoped_handle.h"
#include "tsf/class_factory.h"
#include "tsf/edit_session.h"
#include "tsf/register.h"

#include <windows.h>
#include <gsl/gsl>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) noexcept;
extern "C" HRESULT STDAPICALLTYPE DllInstall(BOOL install, LPCWSTR command_line);

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

void TestLambdaEditSession() {
    ITfEditSession** null_out = nullptr;
    QIQI_TEST_CHECK(CreateComObjectToOut<LambdaEditSession>(null_out, LambdaEditSession::Callback{}) == E_INVALIDARG);
    bool called = false;
    auto* session = new LambdaEditSession([&called](TfEditCookie ec) -> HRESULT {
        called = (ec == 99);
        return S_OK;
    });
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_FALSE);

    QIQI_TEST_CHECK(session->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* obj = nullptr;
    QIQI_TEST_CHECK(session->QueryInterface(IID_IClassFactory, &obj) == E_NOINTERFACE && obj == nullptr);

    ComPtr<ITfEditSession> edit_session;
    QIQI_TEST_CHECK(SUCCEEDED(session->QueryInterface(IID_ITfEditSession, &edit_session)) && edit_session);
    QIQI_TEST_CHECK(edit_session->DoEditSession(99) == S_OK);
    QIQI_TEST_CHECK(called);

    edit_session.Reset();
    QIQI_TEST_CHECK(session->Release() == 0);
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_OK);

    // Empty callback returns E_FAIL
    auto* empty_session = new LambdaEditSession({});
    QIQI_TEST_CHECK(empty_session->DoEditSession(1) == E_FAIL);
    QIQI_TEST_CHECK(empty_session->Release() == 0);
}

void TestClassFactoryAndDllExports() {
    if constexpr (sizeof(void*) == 8) {
        QIQI_TEST_CHECK(!IsRunningUnderWow64());
    }
    HMODULE hmod = ::GetModuleHandleW(nullptr);
    QIQI_TEST_CHECK(DllMain(hmod, DLL_PROCESS_ATTACH, nullptr) == TRUE);
    QIQI_TEST_CHECK(DllMain(hmod, DLL_PROCESS_DETACH, nullptr) == TRUE);
    QIQI_TEST_CHECK(DllMain(hmod, DLL_PROCESS_ATTACH, nullptr) == TRUE);
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_OK);

    QIQI_TEST_CHECK(DllGetClassObject(kClsidQiQiTextService, IID_IClassFactory, nullptr) == E_INVALIDARG);
    void* ppv = nullptr;
    QIQI_TEST_CHECK(DllGetClassObject(kGuidProfileWubi86, IID_IClassFactory, &ppv) == CLASS_E_CLASSNOTAVAILABLE);

    ComPtr<IClassFactory> factory;
    QIQI_TEST_CHECK(SUCCEEDED(DllGetClassObject(kClsidQiQiTextService, IID_PPV_ARGS(&factory))) && factory);
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_FALSE);

    QIQI_TEST_CHECK(factory->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(factory->QueryInterface(IID_ITfTextInputProcessor, &ppv) == E_NOINTERFACE);

    QIQI_TEST_CHECK(SUCCEEDED(factory->LockServer(TRUE)));
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(factory->LockServer(FALSE)));

    QIQI_TEST_CHECK(factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(factory->CreateInstance(factory.Get(), IID_ITfTextInputProcessorEx, &ppv) == CLASS_E_NOAGGREGATION);

    ComPtr<ITfTextInputProcessorEx> tip;
    QIQI_TEST_CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&tip))) && tip);

    tip.Reset();
    factory.Reset();
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_OK);

    // Test RegisterServer with invalid HINSTANCE (GetModuleFileNameW failure branch -> SELFREG_E_CLASS)
    QIQI_TEST_CHECK(RegisterServer(reinterpret_cast<HINSTANCE>(0x12345678)) == SELFREG_E_CLASS);
    QIQI_TEST_CHECK(DllInstall(TRUE, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(DllInstall(TRUE, L"machine") == E_INVALIDARG);
    QIQI_TEST_CHECK(DllInstall(FALSE, L"user") == E_INVALIDARG);

    // Redirect COM writes and mock TSF so tests never change the real user's profiles.
    const wchar_t* kTempRegPath = L"Software\\QiQiIME_UnitTest_TempHKCR";
    ScopedRegKey temp_hkcr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kTempRegPath, 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, temp_hkcr.Put(), nullptr) == ERROR_SUCCESS) {
        const auto cleanup_temp_reg = gsl::finally([&]() noexcept {
            temp_hkcr.Reset();
            ::RegDeleteTreeW(HKEY_CURRENT_USER, kTempRegPath);
        });
        if (::RegOverridePredefKey(HKEY_CLASSES_ROOT, temp_hkcr.Get()) == ERROR_SUCCESS) {
            const auto restore_hkcr = gsl::finally([]() noexcept {
                ::RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
            });
            QIQI_TEST_CHECK(!IsNative64BitClsidRegistered());
            // 2. Test full RegisterServerWithManagers / UnregisterServerWithManagers flow using mocks
            ComPtr<MockTfInputProcessorProfiles> mock_profiles(new MockTfInputProcessorProfiles());
            ComPtr<MockTfCategoryMgr> mock_cat_mgr(new MockTfCategoryMgr());

            // 2a. Failure on profiles->Register
            mock_profiles->fail_register_ = true;
            QIQI_TEST_CHECK(FAILED(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            mock_profiles->fail_register_ = false;

            // 2b. Failure on profiles->AddLanguageProfile
            mock_profiles->fail_add_profile_ = true;
            QIQI_TEST_CHECK(FAILED(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            mock_profiles->fail_add_profile_ = false;

            mock_profiles->fail_register_profile_ = true;
            QIQI_TEST_CHECK(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false) == E_FAIL);
            mock_profiles->fail_register_profile_ = false;

            mock_profiles->fail_enable_default_ = true;
            QIQI_TEST_CHECK(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false) == E_ACCESSDENIED);
            mock_profiles->fail_enable_default_ = false;

            mock_profiles->fail_enable_ = true;
            QIQI_TEST_CHECK(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false) == E_ACCESSDENIED);
            mock_profiles->fail_enable_ = false;

            // 2c. Full success of RegisterServerWithManagers (AddLanguageProfile + RegisterProfile + 7 Categories)
            QIQI_TEST_CHECK(SUCCEEDED(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            QIQI_TEST_CHECK(mock_profiles->registered_);
            QIQI_TEST_CHECK(mock_profiles->profile_added_);
            QIQI_TEST_CHECK(mock_profiles->profile_mgr_registered_);
            QIQI_TEST_CHECK(mock_profiles->enabled_);
            QIQI_TEST_CHECK(mock_profiles->enabled_by_default_);
            QIQI_TEST_CHECK(mock_profiles->activate_calls_ == 0);
            if constexpr (sizeof(void*) == 8) {
                QIQI_TEST_CHECK(IsNative64BitClsidRegistered());
            }
            QIQI_TEST_CHECK(mock_profiles->last_profile_desc_ == kProfileDescZh ||
                            mock_profiles->last_profile_desc_ == kProfileDescEn);
            QIQI_TEST_CHECK(mock_profiles->display_name_res_id_ == 100);
            QIQI_TEST_CHECK(mock_cat_mgr->registered_categories_ == 7);

            // 2d. WOW64 coexistence path (override_wow64 = true while native 64-bit CLSID is present)
            ScopedRegKey native64_key;
            const wchar_t* kInproc64 =
                L"CLSID\\{8E7B9D31-5A42-4C81-9F11-202610290001}\\InProcServer32";
            if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, kInproc64, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                                  nullptr, native64_key.Put(), nullptr) == ERROR_SUCCESS) {
                native64_key.Reset();
                const int enable_calls = mock_profiles->enable_calls_;
                const int enable_default_calls = mock_profiles->enable_default_calls_;
                QIQI_TEST_CHECK(RegisterServerWithManagers(
                    hmod, mock_profiles.Get(), mock_cat_mgr.Get(), true) == S_OK);
                QIQI_TEST_CHECK(UnregisterServerWithManagers(
                    mock_profiles.Get(), mock_cat_mgr.Get(), true) == S_OK);
                QIQI_TEST_CHECK(mock_profiles->enable_calls_ == enable_calls);
                QIQI_TEST_CHECK(mock_profiles->enable_default_calls_ == enable_default_calls);
                QIQI_TEST_CHECK(mock_profiles->deactivate_calls_ == 0);
            }

            // 2e. Full UnregisterServerWithManagers
            const int enable_calls = mock_profiles->enable_calls_;
            const int enable_default_calls = mock_profiles->enable_default_calls_;
            QIQI_TEST_CHECK(SUCCEEDED(UnregisterServerWithManagers(
                mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            QIQI_TEST_CHECK(!mock_profiles->registered_);
            QIQI_TEST_CHECK(mock_cat_mgr->registered_categories_ == 0);
            QIQI_TEST_CHECK(mock_profiles->enable_calls_ == enable_calls);
            QIQI_TEST_CHECK(mock_profiles->enable_default_calls_ == enable_default_calls);
            QIQI_TEST_CHECK(mock_profiles->enabled_);
            QIQI_TEST_CHECK(mock_profiles->deactivate_calls_ == 1);
            QIQI_TEST_CHECK(mock_profiles->deactivate_flags_ == TF_IPPMF_FORSESSION);
            QIQI_TEST_CHECK(!IsNative64BitClsidRegistered());

            // Reinstall must override an old per-user disabled state.
            mock_profiles->enabled_ = false;
            QIQI_TEST_CHECK(SUCCEEDED(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            QIQI_TEST_CHECK(mock_profiles->enabled_);
            QIQI_TEST_CHECK(SUCCEEDED(UnregisterServerWithManagers(
                mock_profiles.Get(), mock_cat_mgr.Get(), false)));
        }
    }
}

void TestEnableCurrentUserProfile() {
    ComPtr<MockTfInputProcessorProfiles> profiles(new MockTfInputProcessorProfiles());
    QIQI_TEST_CHECK(EnableCurrentUserProfile(profiles.Get()) == S_OK);
    QIQI_TEST_CHECK(profiles->enabled_);
    QIQI_TEST_CHECK(!profiles->registered_);
    QIQI_TEST_CHECK(!profiles->profile_added_);
    QIQI_TEST_CHECK(profiles->enable_default_calls_ == 0);
    QIQI_TEST_CHECK(profiles->activate_calls_ == 0);
    QIQI_TEST_CHECK(EnableCurrentUserProfile(profiles.Get()) == S_OK);
    QIQI_TEST_CHECK(profiles->enable_calls_ == 2);
    profiles->fail_enable_ = true;
    QIQI_TEST_CHECK(EnableCurrentUserProfile(profiles.Get()) == E_ACCESSDENIED);
}

} // namespace

void RunTsfComTests() {
    TestLambdaEditSession();
    TestClassFactoryAndDllExports();
    TestEnableCurrentUserProfile();
}

} // namespace qiqi::test
