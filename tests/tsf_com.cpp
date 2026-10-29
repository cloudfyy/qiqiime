#include "common.h"
#include "common/guids.h"
#include "tsf/class_factory.h"
#include "tsf/edit_session.h"
#include "tsf/register.h"

#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

void TestLambdaEditSession() {
    bool called = false;
    auto* session = new LambdaEditSession([&called](TfEditCookie ec) -> HRESULT {
        called = (ec == 99);
        return S_OK;
    });

    QIQI_TEST_CHECK(session->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* obj = nullptr;
    QIQI_TEST_CHECK(session->QueryInterface(IID_IClassFactory, &obj) == E_NOINTERFACE && obj == nullptr);

    ComPtr<ITfEditSession> edit_session;
    QIQI_TEST_CHECK(SUCCEEDED(session->QueryInterface(IID_ITfEditSession, &edit_session)) && edit_session);
    QIQI_TEST_CHECK(edit_session->DoEditSession(99) == S_OK);
    QIQI_TEST_CHECK(called);

    edit_session.Reset();
    QIQI_TEST_CHECK(session->Release() == 0);

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

    // Test DllRegisterServer and DllUnregisterServer using RegOverridePredefKey
    // so registry writes go to a temporary HKCU subkey without requiring Administrator privileges
    const wchar_t* kTempRegPath = L"Software\\QiQiIME_UnitTest_TempHKCR";
    HKEY temp_hkcr = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kTempRegPath, 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &temp_hkcr, nullptr) == ERROR_SUCCESS) {
        if (::RegOverridePredefKey(HKEY_CLASSES_ROOT, temp_hkcr) == ERROR_SUCCESS) {
            // 1. Standard register & unregister in current architecture view
            (void)DllRegisterServer();
            QIQI_TEST_CHECK(SUCCEEDED(DllUnregisterServer()));

            // 2. When running as 32-bit (x86) under WOW64, also verify coexistence when
            // the 64-bit CLSID is already registered in the 64-bit registry view (KEY_WOW64_64KEY)
            if constexpr (sizeof(void*) == 4) {
                if (IsRunningUnderWow64()) {
                    HKEY native64_key = nullptr;
                    const wchar_t* kInproc64 =
                        L"CLSID\\{8E7B9D31-5A42-4C81-9F11-202610290001}\\InProcServer32";
                    if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, kInproc64, 0, nullptr,
                                          REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                                          nullptr, &native64_key, nullptr) == ERROR_SUCCESS) {
                        ::RegCloseKey(native64_key);
                        // With native 64-bit CLSID present, 32-bit WOW64 RegisterServer/UnregisterServer
                        // should succeed immediately (S_OK) without touching 64-bit TSF profiles
                        QIQI_TEST_CHECK(DllRegisterServer() == S_OK);
                        QIQI_TEST_CHECK(DllUnregisterServer() == S_OK);
                    }
                }
            }

            ::RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
        }
        ::RegCloseKey(temp_hkcr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTempRegPath);
    }
}

} // namespace

void RunTsfComTests() {
    TestLambdaEditSession();
    TestClassFactoryAndDllExports();
}

} // namespace qiqi::test
