#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <string>
#include <utility>
#include "common/guids.h"

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

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
        if (!ppRange || fail_get_range_) return E_FAIL;
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
    bool fail_get_range_ = false;
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

// Minimal mock ITfProperty
class MockTfProperty final : public ITfProperty {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfReadOnlyProperty) || IsEqualIID(riid, IID_ITfProperty)) {
            *ppvObj = static_cast<ITfProperty*>(this);
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
    STDMETHODIMP GetType(GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges**, ITfRange*) override { return E_NOTIMPL; }
    STDMETHODIMP GetValue(TfEditCookie, ITfRange*, VARIANT*) override { return S_OK; }
    STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
    STDMETHODIMP FindRange(TfEditCookie, ITfRange*, ITfRange**, TfAnchor) override { return E_NOTIMPL; }
    STDMETHODIMP SetValueStore(TfEditCookie, ITfRange*, ITfPropertyStore*) override { return E_NOTIMPL; }
    STDMETHODIMP SetValue(TfEditCookie, ITfRange*, const VARIANT*) override {
        set_called_ = true;
        return S_OK;
    }
    STDMETHODIMP Clear(TfEditCookie, ITfRange*) override {
        clear_called_ = true;
        return S_OK;
    }

    bool set_called_ = false;
    bool clear_called_ = false;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfContext + ITfInsertAtSelection + ITfContextComposition
class MockTfContext final : public ITfContext, public ITfInsertAtSelection, public ITfContextComposition {
public:
    MockTfContext() : range_(new MockTfRange()), view_(new MockTfContextView()), prop_(new MockTfProperty()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext)) {
            *ppvObj = static_cast<ITfContext*>(this);
        } else if (IsEqualIID(riid, IID_ITfInsertAtSelection) && !fail_qi_insert_) {
            *ppvObj = static_cast<ITfInsertAtSelection*>(this);
        } else if (IsEqualIID(riid, IID_ITfContextComposition) && !fail_qi_comp_) {
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
        if (!ppView || fail_get_view_) return E_FAIL;
        *ppView = view_.Get();
        (*ppView)->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
    STDMETHODIMP GetStatus(TF_STATUS*) override { return S_OK; }
    STDMETHODIMP GetProperty(REFGUID, ITfProperty** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = prop_.Get();
        (*ppv)->AddRef();
        return S_OK;
    }
    STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }

    // ITfInsertAtSelection
    STDMETHODIMP InsertTextAtSelection(TfEditCookie, DWORD, const WCHAR* pchText, LONG cch, ITfRange** ppRange) override {
        if (fail_insert_at_sel_) return E_FAIL;
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
        if (!ppComposition || fail_start_comp_) return E_FAIL;
        auto* comp = new MockTfComposition(range_);
        comp->fail_get_range_ = fail_comp_get_range_;
        *ppComposition = comp;
        return S_OK;
    }
    STDMETHODIMP EnumCompositions(IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView**) override { return E_NOTIMPL; }
    STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**) override { return E_NOTIMPL; }

    ComPtr<MockTfRange> range_;
    ComPtr<MockTfContextView> view_;
    ComPtr<MockTfProperty> prop_;
    bool fail_qi_insert_ = false;
    bool fail_insert_at_sel_ = false;
    bool fail_qi_comp_ = false;
    bool fail_start_comp_ = false;
    bool fail_comp_get_range_ = false;
    bool fail_get_view_ = false;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfCompartment
class MockTfCompartment final : public ITfCompartment {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompartment)) {
            *ppvObj = static_cast<ITfCompartment*>(this);
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
    STDMETHODIMP SetValue(TfClientId, const VARIANT* pvarValue) override {
        if (pvarValue && pvarValue->vt == VT_I4) {
            last_val_ = pvarValue->lVal;
        }
        return S_OK;
    }
    STDMETHODIMP GetValue(VARIANT* pvarValue) override {
        if (!pvarValue) return E_INVALIDARG;
        pvarValue->vt = VT_I4;
        pvarValue->lVal = last_val_;
        return S_OK;
    }

    LONG last_val_ = 0;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfThreadMgr + ITfSource + ITfSourceSingle + ITfKeystrokeMgr + ITfUIElementMgr + ITfCompartmentMgr + ITfLangBarItemMgr
class MockTfThreadMgr final
    : public ITfThreadMgr,
      public ITfSource,
      public ITfSourceSingle,
      public ITfKeystrokeMgr,
      public ITfUIElementMgr,
      public ITfCompartmentMgr,
      public ITfLangBarItemMgr {
public:
    MockTfThreadMgr() : compartment_(new MockTfCompartment()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgr)) {
            *ppvObj = static_cast<ITfThreadMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfSource) && !fail_query_source_) {
            *ppvObj = static_cast<ITfSource*>(this);
        } else if (IsEqualIID(riid, IID_ITfSourceSingle)) {
            *ppvObj = static_cast<ITfSourceSingle*>(this);
        } else if (IsEqualIID(riid, IID_ITfKeystrokeMgr) && !fail_query_key_mgr_) {
            *ppvObj = static_cast<ITfKeystrokeMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfUIElementMgr) && !fail_query_ui_mgr_) {
            *ppvObj = static_cast<ITfUIElementMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfCompartmentMgr)) {
            *ppvObj = static_cast<ITfCompartmentMgr*>(this);
        } else if (IsEqualIID(riid, IID_ITfLangBarItemMgr)) {
            *ppvObj = static_cast<ITfLangBarItemMgr*>(this);
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

    // ITfSourceSingle
    STDMETHODIMP AdviseSingleSink(TfClientId, REFIID, IUnknown* punk) override {
        single_sink_ = punk;
        return S_OK;
    }
    STDMETHODIMP UnadviseSingleSink(TfClientId, REFIID) override {
        single_sink_.Reset();
        return S_OK;
    }

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
    STDMETHODIMP BeginUIElement(ITfUIElement* elem, BOOL* pbShow, DWORD* pdwUIElementId) override {
        if (fail_begin_ui_) return E_FAIL;
        last_ui_element_ = elem;
        if (pbShow) *pbShow = show_own_ui_;
        if (pdwUIElementId) *pdwUIElementId = 7;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) override { return S_OK; }
    STDMETHODIMP EndUIElement(DWORD) override { return S_OK; }
    STDMETHODIMP GetUIElement(DWORD, ITfUIElement**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumUIElements(IEnumTfUIElements**) override { return E_NOTIMPL; }

    // ITfCompartmentMgr
    STDMETHODIMP GetCompartment(REFGUID, ITfCompartment** ppcomp) override {
        if (!ppcomp) return E_INVALIDARG;
        *ppcomp = compartment_.Get();
        (*ppcomp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP ClearCompartment(TfClientId, REFGUID) override { return S_OK; }
    STDMETHODIMP EnumCompartments(IEnumGUID**) override { return E_NOTIMPL; }

    // ITfLangBarItemMgr
    STDMETHODIMP EnumItems(IEnumTfLangBarItems**) override { return E_NOTIMPL; }
    STDMETHODIMP GetItem(REFGUID, ITfLangBarItem**) override { return E_NOTIMPL; }
    STDMETHODIMP AdviseItemSink(ITfLangBarItemSink*, DWORD*, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP UnadviseItemSink(DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP GetItemFloatingRect(DWORD, REFGUID, RECT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetItemsStatus(ULONG, const GUID*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP GetItemNum(ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP GetItems(ULONG, ITfLangBarItem**, TF_LANGBARITEMINFO*, DWORD*, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP AdviseItemsSink(ULONG, ITfLangBarItemSink**, const GUID*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP UnadviseItemsSink(ULONG, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP AddItem(ITfLangBarItem* punk) override {
        langbar_item_ = punk;
        return S_OK;
    }
    STDMETHODIMP RemoveItem(ITfLangBarItem*) override {
        langbar_item_.Reset();
        return S_OK;
    }

    ComPtr<MockTfCompartment> compartment_;
    ComPtr<IUnknown> single_sink_;
    ComPtr<ITfUIElement> last_ui_element_;
    ComPtr<ITfLangBarItem> langbar_item_;
    bool fail_query_source_ = false;
    bool fail_query_key_mgr_ = false;
    bool fail_query_ui_mgr_ = false;
    bool fail_advise_ = false;
    bool fail_key_sink_ = false;
    bool fail_begin_ui_ = false;
    BOOL show_own_ui_ = TRUE;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfInputProcessorProfiles for non-admin unit testing of RegisterServer
class MockTfInputProcessorProfiles final
    : public ITfInputProcessorProfiles,
      public ITfInputProcessorProfileMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfInputProcessorProfiles)) {
            *ppvObj = static_cast<ITfInputProcessorProfiles*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfInputProcessorProfileMgr)) {
            *ppvObj = static_cast<ITfInputProcessorProfileMgr*>(this);
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
    STDMETHODIMP Register(REFCLSID) override {
        if (fail_register_) return E_FAIL;
        registered_ = true;
        return S_OK;
    }
    STDMETHODIMP Unregister(REFCLSID) override {
        registered_ = false;
        return S_OK;
    }
    STDMETHODIMP AddLanguageProfile(REFCLSID, LANGID, REFGUID, const WCHAR* desc, ULONG cch, const WCHAR*, ULONG, ULONG) override {
        if (fail_add_profile_) return E_FAIL;
        profile_added_ = true;
        if (desc && cch > 0) {
            last_profile_desc_.assign(desc, cch);
        }
        return S_OK;
    }
    STDMETHODIMP RemoveLanguageProfile(REFCLSID, LANGID, REFGUID) override { return S_OK; }
    STDMETHODIMP EnumInputProcessorInfo(IEnumGUID**) override { return E_NOTIMPL; }
    STDMETHODIMP GetDefaultLanguageProfile(LANGID, REFGUID, CLSID*, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP SetDefaultLanguageProfile(LANGID, REFCLSID, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP ActivateLanguageProfile(REFCLSID, LANGID, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveLanguageProfile(REFCLSID, LANGID*, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageProfileDescription(REFCLSID, LANGID, REFGUID, BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP GetCurrentLanguage(LANGID*) override { return E_NOTIMPL; }
    STDMETHODIMP ChangeCurrentLanguage(LANGID) override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageList(LANGID**, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP EnumLanguageProfiles(LANGID, IEnumTfLanguageProfiles**) override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfile(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) override {
        ++enable_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_) return E_ACCESSDENIED;
        enabled_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP IsEnabledLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfileByDefault(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) override {
        ++enable_default_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_default_) return E_ACCESSDENIED;
        enabled_by_default_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP SubstituteKeyboardLayout(REFCLSID, LANGID, REFGUID, HKL) override { return E_NOTIMPL; }

    // ITfInputProcessorProfileMgr
    STDMETHODIMP ActivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD) override {
        ++activate_calls_;
        return S_OK;
    }
    STDMETHODIMP DeactivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD flags) override {
        ++deactivate_calls_;
        deactivate_flags_ = flags;
        return S_OK;
    }
    STDMETHODIMP RegisterProfile(REFCLSID, LANGID, REFGUID, const WCHAR*, ULONG, const WCHAR*, ULONG, ULONG, HKL, DWORD, BOOL, DWORD) override {
        if (fail_register_profile_) return E_FAIL;
        profile_mgr_registered_ = true;
        return S_OK;
    }
    STDMETHODIMP UnregisterProfile(REFCLSID, LANGID, REFGUID, DWORD) override { return S_OK; }
    STDMETHODIMP GetProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, TF_INPUTPROCESSORPROFILE*) override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveProfile(REFGUID, TF_INPUTPROCESSORPROFILE*) override { return E_NOTIMPL; }
    STDMETHODIMP EnumProfiles(LANGID, IEnumTfInputProcessorProfiles**) override { return E_NOTIMPL; }
    STDMETHODIMP ReleaseInputProcessor(REFCLSID, DWORD) override { return E_NOTIMPL; }

    bool fail_register_ = false;
    bool fail_add_profile_ = false;
    bool fail_register_profile_ = false;
    bool fail_enable_ = false;
    bool fail_enable_default_ = false;
    bool enabled_ = false;
    bool enabled_by_default_ = false;
    int enable_calls_ = 0;
    int enable_default_calls_ = 0;
    int activate_calls_ = 0;
    int deactivate_calls_ = 0;
    DWORD deactivate_flags_ = 0;
    bool registered_ = false;
    bool profile_added_ = false;
    bool profile_mgr_registered_ = false;
    std::wstring last_profile_desc_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfCategoryMgr for non-admin unit testing of RegisterServer
class MockTfCategoryMgr final : public ITfCategoryMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCategoryMgr)) {
            *ppvObj = static_cast<ITfCategoryMgr*>(this);
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
    STDMETHODIMP RegisterCategory(REFCLSID, REFGUID, REFGUID) override {
        ++registered_categories_;
        return S_OK;
    }
    STDMETHODIMP UnregisterCategory(REFCLSID, REFGUID, REFGUID) override {
        if (registered_categories_ > 0) --registered_categories_;
        return S_OK;
    }
    STDMETHODIMP EnumCategoriesInItem(REFGUID, IEnumGUID**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumItemsInCategory(REFGUID, IEnumGUID**) override { return E_NOTIMPL; }
    STDMETHODIMP FindClosestCategory(REFGUID, GUID*, const GUID**, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUIDDescription(REFCLSID, REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDescription(REFCLSID, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUIDDWORD(REFCLSID, REFGUID, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDWORD(REFCLSID, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDWORD(REFGUID, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUID(REFGUID, TfGuidAtom*) override { return E_NOTIMPL; }
    STDMETHODIMP GetGUID(TfGuidAtom, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualTfGuidAtom(TfGuidAtom, REFGUID, BOOL*) override { return E_NOTIMPL; }

    size_t registered_categories_ = 0;
private:
    ULONG ref_ = 1;
};

} // namespace qiqi::test
