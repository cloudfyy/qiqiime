#include "common/test_support.h"
#include "mocks.h"
#include "common/guids.h"
#include "common/scoped_handle.h"
#include "common/logger.h"
#include "Ime/tsf/class_factory.h"
#include "Ime/tsf/edit_session.h"
#include "Ime/tsf/register.h"

#include <windows.h>
#include <gsl/gsl>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) noexcept;
extern "C" HRESULT STDAPICALLTYPE DllInstall(BOOL install, LPCWSTR command_line);

namespace qiqi::test {

void RunCategoryManagerTests();

using Microsoft::WRL::ComPtr;

namespace {

void TestLambdaEditSession() {
    ITfEditSession** null_out = nullptr;
    QIQI_TEST_CHECK(CreateComObjectToOut<LambdaEditSession>(null_out, LambdaEditSession::Callback{}) == E_INVALIDARG);
    bool called = false;
    auto session = MakeComObject<LambdaEditSession>([&called](TfEditCookie ec) -> HRESULT {
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
    QIQI_TEST_CHECK(session.Detach()->Release() == 0);
    QIQI_TEST_CHECK(DllCanUnloadNow() == S_OK);

    // Empty callback returns E_FAIL
    auto empty_session = MakeComObject<LambdaEditSession>(LambdaEditSession::Callback{});
    QIQI_TEST_CHECK(empty_session->DoEditSession(1) == E_FAIL);
    QIQI_TEST_CHECK(empty_session.Detach()->Release() == 0);
}

void TestDeferredEditSessions() {
    static_assert(TF_ES_ASYNCDONTCARE == 0);
    const long initial_objects = g_com_object_count.load();
    auto immediate = MakeComObject<MockTfContext>();
    int immediate_calls = 0;
    auto immediate_session = MakeComObject<LambdaEditSession>([&](TfEditCookie cookie) {
        QIQI_TEST_CHECK(cookie == 1);
        ++immediate_calls;
        return S_FALSE;
    });
    HRESULT session_result = E_FAIL;
    QIQI_TEST_CHECK(immediate->RequestEditSession(7, immediate_session.Get(),
                                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &session_result) == S_OK);
    QIQI_TEST_CHECK(session_result == S_FALSE && immediate_calls == 1);
    QIQI_TEST_CHECK(immediate->RequestEditSession(7, immediate_session.Get(),
                                                 TF_ES_ASYNC | TF_ES_READ, &session_result) == E_UNEXPECTED);
    QIQI_TEST_CHECK(session_result == E_UNEXPECTED && immediate_calls == 1);
    immediate_session.Reset();

    auto queue = std::make_shared<MockEditSessionQueue>();
    auto first = MakeComObject<MockTfContext>(queue);
    auto second = MakeComObject<MockTfContext>(queue);
    const DWORD owner_thread = ::GetCurrentThreadId();
    std::vector<int> order;
    for (int index = 0; index < 3; ++index) {
        auto context = index == 1 ? second : first;
        auto session = MakeComObject<LambdaEditSession>([&, context, index](TfEditCookie cookie) {
            QIQI_TEST_CHECK(::GetCurrentThreadId() == owner_thread);
            QIQI_TEST_CHECK(cookie == gsl::narrow_cast<TfEditCookie>(10 + index));
            QIQI_TEST_CHECK(context);
            order.push_back(index);
            return index == 1 ? E_FAIL : (index == 2 ? S_FALSE : S_OK);
        });
        const DWORD flags = index == 1 ? TF_ES_ASYNC | TF_ES_READ : TF_ES_ASYNCDONTCARE | TF_ES_READWRITE;
        QIQI_TEST_CHECK(context->RequestEditSession(gsl::narrow_cast<TfClientId>(7 + index), session.Get(),
                                                   flags, &session_result) == S_OK);
        QIQI_TEST_CHECK(session_result == TF_S_ASYNC);
    }
    QIQI_TEST_CHECK(order.empty() && queue->PendingCount() == 3);
    QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).context.Get() == first.Get());
    QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).context.Get() == second.Get());
    QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).client_id == 8);
    QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).flags == (TF_ES_ASYNC | TF_ES_READ));
    QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 2).flags == (TF_ES_ASYNCDONTCARE | TF_ES_READWRITE));
    auto sync_session = MakeComObject<LambdaEditSession>([&](TfEditCookie cookie) {
        QIQI_TEST_CHECK(cookie == 1);
        order.push_back(99);
        return S_OK;
    });
    QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), TF_ES_SYNC | TF_ES_READWRITE,
                                             &session_result) == S_OK);
    QIQI_TEST_CHECK(session_result == S_OK && order == std::vector<int>{99} && queue->PendingCount() == 3);
    first->allow_sync_edit_sessions_ = false;
    QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), TF_ES_SYNC | TF_ES_READWRITE,
                                             &session_result) == S_OK);
    QIQI_TEST_CHECK(session_result == TF_E_SYNCHRONOUS && queue->PendingCount() == 3);
    first->read_only_ = true;
    QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                                             &session_result) == S_OK);
    QIQI_TEST_CHECK(session_result == TS_E_READONLY && queue->PendingCount() == 3);
    {
        first->allow_sync_edit_sessions_ = true;
        bool read_called = false;
        auto read_session = MakeComObject<LambdaEditSession>([&](TfEditCookie) {
            read_called = true;
            return S_FALSE;
        });
        QIQI_TEST_CHECK(first->RequestEditSession(7, read_session.Get(), TF_ES_SYNC | TF_ES_READ,
                                                 &session_result) == S_OK);
        QIQI_TEST_CHECK(read_called && session_result == S_FALSE && queue->PendingCount() == 3);
    }
    first->read_only_ = false;
    for (const DWORD flags : std::array<DWORD, 3>{TF_ES_SYNC | TF_ES_ASYNC | TF_ES_READ,
                                                TF_ES_ASYNC | (TF_ES_READWRITE & ~TF_ES_READ),
                                                TF_ES_ASYNC | TF_ES_READ | 0x80000000u}) {
        QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), flags, &session_result) == E_INVALIDARG);
        QIQI_TEST_CHECK(session_result == E_INVALIDARG && queue->PendingCount() == 3);
    }
    QIQI_TEST_CHECK(first->RequestEditSession(7, nullptr, TF_ES_ASYNC | TF_ES_READ, &session_result) == E_INVALIDARG);
    QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), TF_ES_ASYNC | TF_ES_READ, nullptr) == E_INVALIDARG);
    first->edit_session_request_result_ = TF_E_DISCONNECTED;
    QIQI_TEST_CHECK(first->RequestEditSession(7, sync_session.Get(), TF_ES_ASYNC | TF_ES_READ,
                                             &session_result) == TF_E_DISCONNECTED);
    QIQI_TEST_CHECK(session_result == TF_E_DISCONNECTED && queue->PendingCount() == 3);
    QIQI_TEST_CHECK(queue->RunNext(10) == S_OK);
    QIQI_TEST_CHECK(queue->RunNext(11) == E_FAIL);
    QIQI_TEST_CHECK(queue->RunNext(12) == S_FALSE);
    QIQI_TEST_CHECK((order == std::vector<int>{99, 0, 1, 2}));
    QIQI_TEST_CHECK(queue->PendingCount() == 0 && queue->RunNext() == E_UNEXPECTED);
    sync_session.Reset();
    first->edit_session_request_result_ = S_OK;

    auto reentrant = MakeComObject<LambdaEditSession>([&, first](TfEditCookie) {
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        auto next = MakeComObject<LambdaEditSession>([&](TfEditCookie cookie) {
            QIQI_TEST_CHECK(cookie == 55);
            order.push_back(4);
            return S_OK;
        });
        HRESULT result = E_FAIL;
        QIQI_TEST_CHECK(first->RequestEditSession(7, next.Get(), TF_ES_ASYNC | TF_ES_READ, &result) == S_OK);
        QIQI_TEST_CHECK(result == TF_S_ASYNC);
        order.push_back(3);
        return S_OK;
    });
    QIQI_TEST_CHECK(first->RequestEditSession(7, reentrant.Get(), TF_ES_ASYNC | TF_ES_READ,
                                             &session_result) == S_OK);
    reentrant.Reset();
    QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
    QIQI_TEST_CHECK(queue->RunNext(55) == S_OK && queue->PendingCount() == 0);
    QIQI_TEST_CHECK((order == std::vector<int>{99, 0, 1, 2, 3, 4}));
    first.Reset();
    second.Reset();
    QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);

    auto token = std::make_shared<int>(1);
    const std::weak_ptr<int> token_lifetime(token);
    const std::weak_ptr<MockEditSessionQueue> queue_lifetime(queue);
    auto retained_context = MakeComObject<MockTfContext>(queue);
    bool called = false;
    auto retained_session = MakeComObject<LambdaEditSession>([retained_context, token, &called](TfEditCookie) {
        called = true;
        return S_OK;
    });
    QIQI_TEST_CHECK(retained_context->RequestEditSession(7, retained_session.Get(), TF_ES_ASYNC | TF_ES_READ,
                                                        &session_result) == S_OK);
    retained_session.Reset();
    token.reset();
    retained_context.Reset();
    QIQI_TEST_CHECK(!token_lifetime.expired() && queue->PendingCount() == 1);
    QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
    QIQI_TEST_CHECK(token_lifetime.expired() && !called && g_com_object_count.load() == initial_objects);

    auto surviving_context = MakeComObject<MockTfContext>(queue);
    auto pending = MakeComObject<LambdaEditSession>([surviving_context, &called](TfEditCookie) {
        called = true;
        return S_OK;
    });
    QIQI_TEST_CHECK(surviving_context->RequestEditSession(7, pending.Get(), TF_ES_ASYNC | TF_ES_READ,
                                                        &session_result) == S_OK);
    pending.Reset();
    queue.reset();
    QIQI_TEST_CHECK(queue_lifetime.expired() && !called);
    auto after_expiry = MakeComObject<LambdaEditSession>([](TfEditCookie) { return S_OK; });
    QIQI_TEST_CHECK(surviving_context->RequestEditSession(7, after_expiry.Get(),
                                                        TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_result) == E_UNEXPECTED);
    QIQI_TEST_CHECK(session_result == E_UNEXPECTED);
    after_expiry.Reset();
    QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
}

void TestClassFactoryAndDllExports() {
    if constexpr (sizeof(void*) == 8) {
        QIQI_TEST_CHECK(!IsRunningUnderWow64());
    }
    HMODULE hmod = ::GetModuleHandleW(nullptr);
    log::Shutdown();
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    QIQI_TEST_CHECK(DllMain(hmod, DLL_PROCESS_ATTACH, nullptr) == TRUE);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    QIQI_TEST_CHECK(DllMain(hmod, DLL_PROCESS_DETACH, nullptr) == TRUE);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
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
    QIQI_TEST_CHECK(DllInstall(FALSE, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(DllInstall(FALSE, L"machine") == E_INVALIDARG);

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
            ComPtr<MockTfInputProcessorProfiles> mock_profiles = MakeComObject<MockTfInputProcessorProfiles>();
            ComPtr<MockTfCategoryMgr> mock_cat_mgr = MakeComObject<MockTfCategoryMgr>();

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

            // Verify qiqiime-settings: URI protocol was registered in HKCR
            {
                ScopedRegKey proto_cmd;
                QIQI_TEST_CHECK(::RegOpenKeyExW(
                                    HKEY_CLASSES_ROOT,
                                    L"qiqiime-settings\\shell\\open\\command",
                                    0,
                                    KEY_READ,
                                    proto_cmd.Put()) == ERROR_SUCCESS);
            }

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
            {
                ScopedRegKey proto_cmd;
                QIQI_TEST_CHECK(::RegOpenKeyExW(
                                    HKEY_CLASSES_ROOT,
                                    L"qiqiime-settings\\shell\\open\\command",
                                    0,
                                    KEY_READ,
                                    proto_cmd.Put()) != ERROR_SUCCESS);
            }

            // Reinstall must override an old per-user disabled state, and unregistration continues if session deactivation fails.
            mock_profiles->enabled_ = false;
            QIQI_TEST_CHECK(SUCCEEDED(RegisterServerWithManagers(
                hmod, mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            QIQI_TEST_CHECK(mock_profiles->enabled_);
            mock_profiles->fail_deactivate_ = true;
            QIQI_TEST_CHECK(SUCCEEDED(UnregisterServerWithManagers(
                mock_profiles.Get(), mock_cat_mgr.Get(), false)));
            mock_profiles->fail_deactivate_ = false;
            QIQI_TEST_CHECK(!mock_profiles->registered_);
        }
    }
}

void TestEnableCurrentUserProfile() {
    ComPtr<MockTfInputProcessorProfiles> profiles = MakeComObject<MockTfInputProcessorProfiles>();
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

    QIQI_TEST_CHECK(DeactivateCurrentUserProfile(profiles.Get()) == S_OK);
    QIQI_TEST_CHECK(profiles->deactivate_calls_ == 1);
    QIQI_TEST_CHECK(profiles->deactivate_flags_ == TF_IPPMF_FORSESSION);
    profiles->fail_deactivate_ = true;
    QIQI_TEST_CHECK(DeactivateCurrentUserProfile(profiles.Get()) == E_FAIL);
    profiles->fail_deactivate_ = false;
    profiles->fail_qi_profile_mgr_ = true;
    QIQI_TEST_CHECK(DeactivateCurrentUserProfile(profiles.Get()) == E_NOINTERFACE);
}

} // namespace

void RunTsfComTests() {
    RunCategoryManagerTests();
    TestLambdaEditSession();
    TestDeferredEditSessions();
    TestClassFactoryAndDllExports();
    TestEnableCurrentUserProfile();
}

} // namespace qiqi::test
