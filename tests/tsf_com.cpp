#include "common.h"
#include "common/guids.h"
#include "tsf/class_factory.h"
#include "tsf/edit_session.h"
#include "tsf/register.h"

#include <windows.h>
#include <msctf.h>
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
}

} // namespace

void RunTsfComTests() {
    TestLambdaEditSession();
    TestClassFactoryAndDllExports();
}

} // namespace qiqi::test
