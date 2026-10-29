#include "common/guids.h"
#include "common/logger.h"
#include "tsf/class_factory.h"
#include "tsf/edit_session.h"
#include "tsf/register.h"
#include "tsf/tip_service.h"
#include "ui/candidate_ui.h"

#include <windows.h>
#include <msctf.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <cstdlib>
#include <iostream>
#include <string>

#define QIQI_TEST_CHECK(expr)                                                                   \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";     \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

using Microsoft::WRL::ComPtr;
using namespace qiqi;

extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);

namespace {

// Minimal mock ITfRange
class MockTfRange final : public ITfRange {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfRange)) {
            *ppvObj = static_cast<ITfRange*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) override { return S_OK; }
    STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR* pch, LONG cch) override {
        if (pch && cch >= 0) {
            text_.assign(pch, static_cast<size_t>(cch));
        }
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
    STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return S_OK; }
    STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return S_OK; }
    STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return S_OK; }
    STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return S_OK; }
    STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return S_OK; }
    STDMETHODIMP IsEmpty(TfEditCookie, BOOL* pfEmpty) override {
        if (pfEmpty) *pfEmpty = text_.empty() ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP Collapse(TfEditCookie, TfAnchor) override { return S_OK; }
    STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return S_OK; }
    STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return S_OK; }
    STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return S_OK; }
    STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return S_OK; }
    STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return S_OK; }
    STDMETHODIMP GetGravity(TfGravity*, TfGravity*) override { return S_OK; }
    STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) override { return S_OK; }
    STDMETHODIMP Clone(ITfRange** ppClone) override {
        if (!ppClone) return E_INVALIDARG;
        auto* clone = new MockTfRange();
        clone->text_ = text_;
        *ppClone = clone;
        return S_OK;
    }
    STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }

    std::wstring text_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfComposition
class MockTfComposition final : public ITfComposition {
public:
    explicit MockTfComposition(ComPtr<MockTfRange> range) : range_(std::move(range)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfComposition)) {
            *ppvObj = static_cast<ITfComposition*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP GetRange(ITfRange** ppRange) override {
        if (!ppRange) return E_INVALIDARG;
        *ppRange = range_.Get();
        (*ppRange)->AddRef();
        return S_OK;
    }
    STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) override { return S_OK; }
    STDMETHODIMP EndComposition(TfEditCookie) override {
        ended_ = true;
        return S_OK;
    }

    ComPtr<MockTfRange> range_;
    bool ended_ = false;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfContextView
class MockTfContextView final : public ITfContextView {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContextView)) {
            *ppvObj = static_cast<ITfContextView*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP GetRangeFromPoint(TfEditCookie, const POINT*, DWORD, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TfEditCookie, ITfRange*, RECT* prc, BOOL* pfClipped) override {
        if (prc) *prc = {100, 100, 140, 124};
        if (pfClipped) *pfClipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(RECT* prc) override {
        if (prc) *prc = {0, 0, 1920, 1080};
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND* phwnd) override {
        if (phwnd) *phwnd = nullptr;
        return S_OK;
    }
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfContext + ITfInsertAtSelection + ITfContextComposition
class MockTfContext final : public ITfContext, public ITfInsertAtSelection, public ITfContextComposition {
public:
    MockTfContext() : range_(new MockTfRange()), view_(new MockTfContextView()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext)) {
            *ppvObj = static_cast<ITfContext*>(this);
        } else if (IsEqualIID(riid, IID_ITfInsertAtSelection)) {
            *ppvObj = static_cast<ITfInsertAtSelection*>(this);
        } else if (IsEqualIID(riid, IID_ITfContextComposition)) {
            *ppvObj = static_cast<ITfContextComposition*>(this);
        } else {
            *ppvObj = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }

    // ITfContext
    STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* pes, DWORD, HRESULT* phrSession) override {
        if (!pes || !phrSession) return E_INVALIDARG;
        *phrSession = pes->DoEditSession(1);
        return S_OK;
    }
    STDMETHODIMP InWriteSession(TfClientId, BOOL* pfWriteSession) override {
        if (pfWriteSession) *pfWriteSession = TRUE;
        return S_OK;
    }
    STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION*, ULONG*) override { return S_OK; }
    STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION*) override { return S_OK; }
    STDMETHODIMP GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveView(ITfContextView** ppView) override {
        if (!ppView) return E_INVALIDARG;
        *ppView = view_.Get();
        (*ppView)->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
    STDMETHODIMP GetStatus(TF_STATUS*) override { return S_OK; }
    STDMETHODIMP GetProperty(REFGUID, ITfProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }

    // ITfInsertAtSelection
    STDMETHODIMP InsertTextAtSelection(TfEditCookie, DWORD, const WCHAR* pchText, LONG cch, ITfRange** ppRange) override {
        if (pchText && cch > 0) {
            range_->text_.assign(pchText, static_cast<size_t>(cch));
        }
        if (ppRange) {
            *ppRange = range_.Get();
            (*ppRange)->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject*, ITfRange**) override { return E_NOTIMPL; }

    // ITfContextComposition
    STDMETHODIMP StartComposition(TfEditCookie, ITfRange*, ITfCompositionSink*, ITfComposition** ppComposition) override {
        if (!ppComposition) return E_INVALIDARG;
        auto* comp = new MockTfComposition(range_);
        *ppComposition = comp;
        return S_OK;
    }
    STDMETHODIMP EnumCompositions(IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**) override { return E_NOTIMPL; }

    ComPtr<MockTfRange> range_;
    ComPtr<MockTfContextView> view_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfThreadMgr + ITfSource + ITfKeystrokeMgr + ITfUIElementMgr
class MockTfThreadMgr final
    : public ITfThreadMgr,
      public ITfSource,
      public ITfKeystrokeMgr,
      public ITfUIElementMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgr)) {
            *ppvObj = static_cast<ITfThreadMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfSource)) {
            *ppvObj = static_cast<ITfSource*>(this);
        } else if (IsEqualIID(riid, IID_ITfKeystrokeMgr)) {
            *ppvObj = static_cast<ITfKeystrokeMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfUIElementMgr)) {
            *ppvObj = static_cast<ITfUIElementMgr*>(this);
        } else {
            *ppvObj = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }

    // ITfThreadMgr
    STDMETHODIMP Activate(TfClientId* ptid) override { if (ptid) *ptid = 1; return S_OK; }
    STDMETHODIMP Deactivate() override { return S_OK; }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
    STDMETHODIMP GetFocus(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL* pfThreadFocus) override { if (pfThreadFocus) *pfThreadFocus = TRUE; return S_OK; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID, IUnknown*, DWORD* pdwCookie) override {
        if (fail_advise_) return E_FAIL;
        if (pdwCookie) *pdwCookie = 42;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD) override { return S_OK; }

    // ITfKeystrokeMgr
    STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink*, BOOL) override {
        return fail_key_sink_ ? E_FAIL : S_OK;
    }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override { return S_OK; }
    STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override { return S_OK; }
    STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return S_OK; }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return S_OK; }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }

    // ITfUIElementMgr
    STDMETHODIMP BeginUIElement(ITfUIElement*, BOOL* pbShow, DWORD* pdwUIElementId) override {
        if (pbShow) *pbShow = show_own_ui_;
        if (pdwUIElementId) *pdwUIElementId = 7;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) override { return S_OK; }
    STDMETHODIMP EndUIElement(DWORD) override { return S_OK; }
    STDMETHODIMP GetUIElement(DWORD, ITfUIElement**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumUIElements(IEnumTfUIElements**) override { return E_NOTIMPL; }

    bool fail_advise_ = false;
    bool fail_key_sink_ = false;
    BOOL show_own_ui_ = TRUE;
private:
    ULONG ref_ = 1;
};

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

void TestCandidateUI() {
    auto* ui = new CandidateUI(::GetModuleHandleW(nullptr));

    // QueryInterface tests
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* dummy = nullptr;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IClassFactory, &dummy) == E_NOINTERFACE && dummy == nullptr);

    ComPtr<ITfCandidateListUIElement> cand_elem;
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfCandidateListUIElement, &cand_elem)) && cand_elem);

    // Metadata & invalid arg checks
    QIQI_TEST_CHECK(ui->GetDescription(nullptr) == E_INVALIDARG);
    BSTR desc = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDescription(&desc)) && desc != nullptr);
    ::SysFreeString(desc);

    QIQI_TEST_CHECK(ui->GetGUID(nullptr) == E_INVALIDARG);
    GUID guid{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetGUID(&guid)) && IsEqualGUID(guid, kGuidCandidateUIElement));

    QIQI_TEST_CHECK(ui->GetUpdatedFlags(nullptr) == E_INVALIDARG);
    DWORD flags = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetUpdatedFlags(&flags)) && flags != 0);

    QIQI_TEST_CHECK(ui->GetDocumentMgr(nullptr) == E_INVALIDARG);
    ITfDocumentMgr* doc_mgr = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDocumentMgr(&doc_mgr)) && doc_mgr == nullptr);

    QIQI_TEST_CHECK(ui->IsShown(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCount(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetSelection(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetString(0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetPageIndex(nullptr, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCurrentPage(nullptr) == E_INVALIDARG);

    // Empty candidate state
    UINT count = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 0);
    UINT sel = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 0);
    UINT page_cnt = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(nullptr, 0, &page_cnt)) && page_cnt == 0);
    BSTR str = nullptr;
    QIQI_TEST_CHECK(ui->GetString(0, &str) == E_INVALIDARG);

    // Show empty window and trigger WM_PAINT
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(TRUE)));
    BOOL shown = FALSE;
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == TRUE);
    RECT normal_rc{100, 100, 150, 125};
    ui->MoveWindowTo(normal_rc);
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Multi-page candidate state (7 candidates, page_size 5, page 1, selected 1 -> index 6)
    std::vector<std::wstring> cands = {L"一", L"二", L"三", L"四", L"五", L"六", L"七"};
    ui->SetCandidates(L"abcd", cands, 5, 1, 1);

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 7);
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 6);
    UINT cur_page = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCurrentPage(&cur_page)) && cur_page == 1);
    QIQI_TEST_CHECK(SUCCEEDED(ui->SetPageIndex(nullptr, 0)));

    UINT page_indices[4]{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(page_indices, 4, &page_cnt)) && page_cnt == 2);
    QIQI_TEST_CHECK(page_indices[0] == 0 && page_indices[1] == 5);

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetString(6, &str)) && str != nullptr);
    QIQI_TEST_CHECK(std::wstring(str) == L"七");
    ::SysFreeString(str);
    QIQI_TEST_CHECK(ui->GetString(7, &str) == E_INVALIDARG);

    // Test off-screen work area clamping & WM_PAINT dispatch
    RECT far_rc{99999, 99999, 100050, 100025};
    ui->MoveWindowTo(far_rc);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui->SetUiElementId(42);
    QIQI_TEST_CHECK(ui->UiElementId() == 42);

    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == FALSE);
    ui->DestroyPopupWindow();

    cand_elem.Reset();
    QIQI_TEST_CHECK(ui->Release() == 0);
}

void TestClassFactoryAndDllExports() {
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

void TestQiQiTextServiceLifecycleAndKeyFlow() {
    auto* service = new QiQiTextService();

    // QueryInterface coverage for all supported interfaces
    QIQI_TEST_CHECK(service->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(service->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE && out == nullptr);

    ComPtr<ITfTextInputProcessor> tip_base;
    ComPtr<ITfTextInputProcessorEx> tip_ex;
    ComPtr<ITfThreadMgrEventSink> tm_sink;
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_base))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_ex))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))));

    // Activate failure paths
    QIQI_TEST_CHECK(service->ActivateEx(nullptr, 1, 0) == E_INVALIDARG);
    ComPtr<MockTfThreadMgr> mock_tm(new MockTfThreadMgr());
    mock_tm->fail_advise_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_advise_ = false;
    mock_tm->fail_key_sink_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_key_sink_ = false;

    // Activate success
    QIQI_TEST_CHECK(SUCCEEDED(service->Activate(mock_tm.Get(), 1)));

    // ThreadMgrEventSink notifications
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnInitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnUninitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPushContext(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPopContext(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnSetFocus(nullptr, nullptr)));

    // KeyEventSink error checks
    QIQI_TEST_CHECK(key_sink->OnTestKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnTestKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnPreservedKey(nullptr, kGuidPreservedKeyToggleMode, nullptr) == E_INVALIDARG);

    ComPtr<MockTfContext> mock_ctx(new MockTfContext());
    BOOL eaten = FALSE;

    // Type 'A' then 'B' -> starts composition (BeginUIElement) then updates composition (UpdateUIElement)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");

    // Backspace back to 'a', then Space commits "工"
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_BACK, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SPACE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工");

    // Direct Chinese punctuation commit when not composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"。");

    // Start composing 'W', then cancel via Escape
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_.empty());

    // 5th key push auto-commit flow (triggers Commit with non-empty composition_text)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");

    // Test Shift tap mode toggle while composing (cancels composition and switches to English)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);

    // In English mode, 'A' is not eaten
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Toggle back to Chinese mode via PreservedKey (Ctrl+Space)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidProfileWubi86, &eaten)) && eaten == FALSE);

    // Start composing again, then test PreservedKey while composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);

    // Non-shift KeyUp
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Host composition termination & focus loss
    QIQI_TEST_CHECK(SUCCEEDED(comp_sink->OnCompositionTerminated(1, nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnSetFocus(FALSE)));

    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    comp_sink.Reset();
    key_sink.Reset();
    tm_sink.Reset();
    tip_ex.Reset();
    tip_base.Reset();
    QIQI_TEST_CHECK(service->Release() == 0);
}

} // namespace

int main() {
    TestLambdaEditSession();
    TestCandidateUI();
    TestClassFactoryAndDllExports();
    TestQiQiTextServiceLifecycleAndKeyFlow();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
