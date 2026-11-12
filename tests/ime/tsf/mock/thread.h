// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "document.h"

namespace qiqi::test {

class MockTfThreadMgr final
    : public ITfThreadMgr,
      public ITfSource,
      public ITfSourceSingle,
      public ITfKeystrokeMgr,
      public ITfUIElementMgr,
      public ITfCompartmentMgr,
      public ITfLangBarItemMgr {
public:
    enum class ActivationStage { ThreadSink, KeySink, FunctionProvider, LangBar, GlobalCompartments };
    // This mock allocates its compartment dependency, so construction can throw std::bad_alloc.
#pragma warning(suppress : 26455)
    MockTfThreadMgr() : compartment_(MakeComObject<MockTfCompartment>()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
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
            if (return_null_ui_manager_) {
                *ppvObj = nullptr;
                return S_OK;
            }
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
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) {
            delete this;
        }
        return c;
    }

    // ITfThreadMgr
    STDMETHODIMP Activate(TfClientId* ptid) noexcept override { if (ptid) *ptid = 1; return S_OK; }
    STDMETHODIMP Deactivate() noexcept override { return S_OK; }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetFocus(ITfDocumentMgr** document) override {
        if (!document) return E_INVALIDARG;
        *document = focused_document_.Get();
        if (*document) (*document)->AddRef();
        return focused_document_ ? S_OK : E_NOTIMPL;
    }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) noexcept override { return S_OK; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL* pfThreadFocus) noexcept override { if (pfThreadFocus) *pfThreadFocus = TRUE; return S_OK; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** ppCompMgr) override {
        if (!ppCompMgr) return E_INVALIDARG;
        if (activation_callback_) activation_callback_(ActivationStage::GlobalCompartments);
        *ppCompMgr = this;
        AddRef();
        return S_OK;
    }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID, IUnknown*, DWORD* pdwCookie) override {
        ++advise_thread_sink_calls_;
        if (activation_callback_) activation_callback_(ActivationStage::ThreadSink);
        if (fail_advise_) return E_FAIL;
        if (pdwCookie) *pdwCookie = 42;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD) noexcept override {
        ++unadvise_thread_sink_calls_;
        return S_OK;
    }

    // ITfSourceSingle
    STDMETHODIMP AdviseSingleSink(TfClientId, REFIID, IUnknown* punk) override {
        if (activation_callback_) activation_callback_(ActivationStage::FunctionProvider);
        if (FAILED(single_sink_result_)) return single_sink_result_;
        single_sink_ = punk;
        return S_OK;
    }
    STDMETHODIMP UnadviseSingleSink(TfClientId, REFIID) override {
        ++unadvise_single_calls_;
        single_sink_.Reset();
        return unadvise_single_result_;
    }

    // ITfKeystrokeMgr
    STDMETHODIMP AdviseKeyEventSink(TfClientId tid, ITfKeyEventSink* sink, BOOL) override {
        ++advise_key_sink_calls_;
        if (activation_callback_) activation_callback_(ActivationStage::KeySink);
        if (fail_key_sink_) return E_FAIL;
        key_sink_client_id_ = tid;
        key_event_sink_ = sink;
        return S_OK;
    }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId tid) override {
        ++unadvise_key_sink_calls_;
        last_unadvised_key_sink_client_id_ = tid;
        key_event_sink_.Reset();
        return unadvise_key_sink_result_;
    }
    STDMETHODIMP GetForeground(CLSID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP PreserveKey(
        TfClientId tid, REFGUID rguid, const TF_PRESERVEDKEY* prekey, const WCHAR* pchDesc, ULONG cchDesc) override {
        if (!prekey || (!pchDesc && cchDesc != 0)) return E_INVALIDARG;
        ++preserve_key_calls_;
        if (preserve_key_callback_) {
            return preserve_key_callback_(tid, rguid, *prekey);
        }
        preserved_keys_.push_back({tid, rguid, *prekey});
        return S_OK;
    }
    STDMETHODIMP UnpreserveKey(REFGUID rguid, const TF_PRESERVEDKEY* pprekey) override {
        if (!pprekey) return E_INVALIDARG;
        ++unpreserve_key_calls_;
        unpreserved_keys_.push_back({TF_CLIENTID_NULL, rguid, *pprekey});
        if (unpreserve_key_callback_) {
            return unpreserve_key_callback_(rguid, *pprekey);
        }
        const auto it = std::find_if(preserved_keys_.begin(), preserved_keys_.end(), [&](const auto& entry) {
            return IsEqualGUID(entry.guid, rguid) && entry.key.uVKey == pprekey->uVKey &&
                   entry.key.uModifiers == pprekey->uModifiers;
        });
        if (it != preserved_keys_.end()) {
            preserved_keys_.erase(it);
        }
        return unpreserve_key_result_;
    }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) noexcept override { return S_OK; }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) noexcept override { return E_NOTIMPL; }

    // ITfUIElementMgr
    STDMETHODIMP BeginUIElement(ITfUIElement* elem, BOOL* pbShow, DWORD* pdwUIElementId) override {
        ++begin_ui_calls_;
        if (fail_begin_ui_) return E_FAIL;
        last_ui_element_ = elem;
        if (pbShow) *pbShow = show_own_ui_;
        const DWORD allocated_id = next_ui_element_id_++;
        const DWORD id = return_invalid_ui_id_ ? TF_INVALID_UIELEMENTID : allocated_id;
        if (pdwUIElementId) *pdwUIElementId = id;
        if (begin_ui_callback_) begin_ui_callback_(id);
        return begin_ui_result_;
    }
    STDMETHODIMP UpdateUIElement(DWORD id) override {
        ++update_ui_calls_;
        updated_ui_ids_.push_back(id);
        return update_ui_result_;
    }
    STDMETHODIMP EndUIElement(DWORD id) override {
        ++end_ui_calls_;
        ended_ui_ids_.push_back(id);
        if (end_ui_callback_) end_ui_callback_(id);
        return end_ui_result_;
    }
    STDMETHODIMP GetUIElement(DWORD, ITfUIElement**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumUIElements(IEnumTfUIElements**) noexcept override { return E_NOTIMPL; }

    // ITfCompartmentMgr
    STDMETHODIMP GetCompartment(REFGUID rguid, ITfCompartment** ppcomp) override {
        if (!ppcomp) return E_INVALIDARG;
        if (IsEqualGUID(rguid, kGuidCompartmentConfigVersion)) {
            if (!global_config_compartment_) {
                global_config_compartment_ = MakeComObject<MockTfCompartment>(kGuidCompartmentConfigVersion);
            }
            *ppcomp = global_config_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        if (IsEqualGUID(rguid, kGuidCompartmentStatusBarPos)) {
            if (!global_pos_compartment_) {
                global_pos_compartment_ = MakeComObject<MockTfCompartment>(kGuidCompartmentStatusBarPos);
            }
            *ppcomp = global_pos_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
            if (!openclose_compartment_) {
                openclose_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
            }
            *ppcomp = openclose_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
            if (!convmode_compartment_) {
                convmode_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
            }
            *ppcomp = convmode_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        *ppcomp = compartment_.Get();
        (*ppcomp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP ClearCompartment(TfClientId, REFGUID) noexcept override { return S_OK; }
    STDMETHODIMP EnumCompartments(IEnumGUID**) noexcept override { return E_NOTIMPL; }

    // ITfLangBarItemMgr
    STDMETHODIMP EnumItems(IEnumTfLangBarItems**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetItem(REFGUID, ITfLangBarItem**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP AdviseItemSink(ITfLangBarItemSink*, DWORD*, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnadviseItemSink(DWORD) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetItemFloatingRect(DWORD, REFGUID, RECT*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetItemsStatus(ULONG, const GUID*, DWORD*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetItemNum(ULONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetItems(ULONG, ITfLangBarItem**, TF_LANGBARITEMINFO*, DWORD*, ULONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP AdviseItemsSink(ULONG, ITfLangBarItemSink**, const GUID*, DWORD*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnadviseItemsSink(ULONG, DWORD*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP AddItem(ITfLangBarItem* punk) override {
        if (activation_callback_) activation_callback_(ActivationStage::LangBar);
        if (FAILED(add_item_result_)) return add_item_result_;
        langbar_item_ = punk;
        return S_OK;
    }
    STDMETHODIMP RemoveItem(ITfLangBarItem*) override {
        ++remove_item_calls_;
        langbar_item_.Reset();
        return remove_item_result_;
    }

    struct PreservedKeyRecord {
        TfClientId client_id = TF_CLIENTID_NULL;
        GUID guid{};
        TF_PRESERVEDKEY key{};
    };

    ComPtr<MockTfCompartment> compartment_;
    ComPtr<MockTfCompartment> openclose_compartment_;
    ComPtr<ITfDocumentMgr> focused_document_;
    ComPtr<MockTfCompartment> convmode_compartment_;
    ComPtr<MockTfCompartment> global_config_compartment_;
    ComPtr<MockTfCompartment> global_pos_compartment_;
    ComPtr<IUnknown> single_sink_;
    ComPtr<ITfKeyEventSink> key_event_sink_;
    TfClientId key_sink_client_id_ = TF_CLIENTID_NULL;
    TfClientId last_unadvised_key_sink_client_id_ = TF_CLIENTID_NULL;
    unsigned int advise_key_sink_calls_ = 0;
    unsigned int advise_thread_sink_calls_ = 0;
    unsigned int unadvise_thread_sink_calls_ = 0;
    std::function<void(ActivationStage)> activation_callback_;
    unsigned int unadvise_key_sink_calls_ = 0;
    unsigned int preserve_key_calls_ = 0;
    unsigned int unpreserve_key_calls_ = 0;
    HRESULT unadvise_key_sink_result_ = S_OK;
    HRESULT unpreserve_key_result_ = S_OK;
    std::vector<PreservedKeyRecord> preserved_keys_;
    std::vector<PreservedKeyRecord> unpreserved_keys_;
    std::function<HRESULT(TfClientId, REFGUID, const TF_PRESERVEDKEY&)> preserve_key_callback_;
    std::function<HRESULT(REFGUID, const TF_PRESERVEDKEY&)> unpreserve_key_callback_;
    ComPtr<ITfUIElement> last_ui_element_;
    unsigned int begin_ui_calls_ = 0;
    unsigned int update_ui_calls_ = 0;
    unsigned int end_ui_calls_ = 0;
    std::vector<DWORD> updated_ui_ids_;
    DWORD next_ui_element_id_ = 7;
    std::vector<DWORD> ended_ui_ids_;
    std::function<void(DWORD)> begin_ui_callback_;
    std::function<void(DWORD)> end_ui_callback_;
    HRESULT end_ui_result_ = S_OK;
    HRESULT begin_ui_result_ = S_OK;
    HRESULT update_ui_result_ = S_OK;
    bool return_invalid_ui_id_ = false;
    bool return_null_ui_manager_ = false;
    ComPtr<ITfLangBarItem> langbar_item_;
    bool fail_query_source_ = false;
    bool fail_query_key_mgr_ = false;
    bool fail_query_ui_mgr_ = false;
    bool fail_advise_ = false;
    bool fail_key_sink_ = false;
    bool fail_begin_ui_ = false;
    HRESULT single_sink_result_ = S_OK;
    HRESULT unadvise_single_result_ = S_OK;
    HRESULT add_item_result_ = S_OK;
    HRESULT remove_item_result_ = S_OK;
    unsigned int unadvise_single_calls_ = 0;
    unsigned int remove_item_calls_ = 0;
    BOOL show_own_ui_ = TRUE;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfInputProcessorProfiles for non-admin unit testing of RegisterServer

} // namespace qiqi::test
