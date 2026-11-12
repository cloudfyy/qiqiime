// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "range.h"
#include "compartment.h"

namespace qiqi::test {

class MockTfContextView final : public ITfContextView {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContextView)) {
            *ppvObj = static_cast<ITfContextView*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) {
            if (release_observer_) release_observer_();
            // COM Release destroys the mock when its reference count reaches zero.
#pragma warning(suppress : 26401 26409)
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetRangeFromPoint(TfEditCookie, const POINT*, DWORD, ITfRange**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TfEditCookie ec, ITfRange* range, RECT* prc, BOOL* pfClipped) override {
        ++get_text_ext_calls_;
        if (get_text_ext_observer_) get_text_ext_observer_(ec, range, prc, pfClipped);
        if (get_text_ext_callback_) get_text_ext_callback_();
        if (fail_get_text_ext_) return TS_E_NOLAYOUT;
        if (prc) *prc = text_ext_;
        if (pfClipped) *pfClipped = text_clipped_;
        return text_ext_result_;
    }
    STDMETHODIMP GetScreenExt(RECT* prc) noexcept override {
        if (prc) *prc = {0, 0, 1920, 1080};
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND* phwnd) override {
        if (get_wnd_callback_) get_wnd_callback_();
        if (phwnd) *phwnd = hwnd_;
        return get_wnd_result_;
    }
    HWND hwnd_ = nullptr;
    HRESULT get_wnd_result_ = S_OK;
    std::function<void()> get_wnd_callback_;
    bool fail_get_text_ext_ = false;
    RECT text_ext_{100, 100, 140, 124};
    BOOL text_clipped_ = FALSE;
    HRESULT text_ext_result_ = S_OK;
    unsigned int get_text_ext_calls_ = 0;
    std::function<void(TfEditCookie, ITfRange*, RECT*, BOOL*)> get_text_ext_observer_;
    std::function<void()> get_text_ext_callback_;
    std::move_only_function<void() noexcept> release_observer_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfProperty
class MockTfProperty final : public ITfProperty {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfReadOnlyProperty) || IsEqualIID(riid, IID_ITfProperty)) {
            *ppvObj = static_cast<ITfProperty*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) {
            // COM Release destroys the mock when its reference count reaches zero.
#pragma warning(suppress : 26401 26409)
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetType(GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges**, ITfRange*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetValue(TfEditCookie, ITfRange*, VARIANT*) noexcept override { return S_OK; }
    STDMETHODIMP GetContext(ITfContext**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP FindRange(TfEditCookie, ITfRange*, ITfRange**, TfAnchor) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetValueStore(TfEditCookie, ITfRange*, ITfPropertyStore*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetValue(TfEditCookie ec, ITfRange* range, const VARIANT* value) override {
        set_called_ = true;
        ++set_calls_;
        if (set_observer_) set_observer_(ec, range, value);
        if (set_callback_) set_callback_();
        return set_result_;
    }
    STDMETHODIMP Clear(TfEditCookie ec, ITfRange*) override {
        clear_called_ = true;
        ++clear_calls_;
        last_clear_cookie_ = ec;
        if (clear_callback_) clear_callback_(ec);
        return clear_result_;
    }

    bool set_called_ = false;
    unsigned int set_calls_ = 0;
    HRESULT set_result_ = S_OK;
    std::function<void(TfEditCookie, ITfRange*, const VARIANT*)> set_observer_;
    std::function<void()> set_callback_;
    bool clear_called_ = false;
    unsigned int clear_calls_ = 0;
    TfEditCookie last_clear_cookie_ = 0;
    HRESULT clear_result_ = S_OK;
    std::function<void(TfEditCookie)> clear_callback_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfCompartment + ITfSource (supports ITfCompartmentEventSink::OnChange notifications)

class MockEditSessionQueue final {
public:
    struct Request {
        ComPtr<ITfContext> context;
        ComPtr<ITfEditSession> session;
        TfClientId client_id;
        DWORD flags;
    };

    MockEditSessionQueue() = default;
    ~MockEditSessionQueue() = default;
    MockEditSessionQueue(const MockEditSessionQueue&) = delete;
    MockEditSessionQueue& operator=(const MockEditSessionQueue&) = delete;
    MockEditSessionQueue(MockEditSessionQueue&&) = delete;
    MockEditSessionQueue& operator=(MockEditSessionQueue&&) = delete;

    HRESULT Enqueue(gsl::not_null<ITfContext*> context, gsl::not_null<ITfEditSession*> session,
                    TfClientId client_id, DWORD flags) {
        if (!IsOwnerThread()) return RPC_E_WRONG_THREAD;
        requests_.push_back({ComPtr<ITfContext>(context.get()), ComPtr<ITfEditSession>(session.get()), client_id, flags});
        return S_OK;
    }

    HRESULT RunNext(TfEditCookie cookie = 1) {
        if (!IsOwnerThread()) return RPC_E_WRONG_THREAD;
        if (requests_.empty()) {
            QIQI_LOG_ERROR("MockEditSessionQueue::RunNext: no pending edit session");
            return E_UNEXPECTED;
        }
        auto request = std::move(requests_.front());
        requests_.pop_front();
        return request.session->DoEditSession(cookie);
    }

    HRESULT ClearForCleanup() noexcept {
        if (!IsOwnerThread()) return RPC_E_WRONG_THREAD;
        requests_.clear();
        return S_OK;
    }

    size_t PendingCount() const noexcept { return requests_.size(); }
    const std::deque<Request>& PendingRequests() const noexcept { return requests_; }

private:
    bool IsOwnerThread() const noexcept {
        if (::GetCurrentThreadId() == owner_thread_) return true;
        QIQI_LOG_ERROR("MockEditSessionQueue: edit sessions must execute on the owning thread");
        return false;
    }

    const DWORD owner_thread_ = ::GetCurrentThreadId();
    std::deque<Request> requests_;
};

struct MockDocumentLink {
    ITfDocumentMgr* document = nullptr;
};

// Minimal mock ITfContext + ITfInsertAtSelection + ITfContextComposition + ITfSource + ITfCompartmentMgr
class MockTfContext final
    : public ITfContext,
      public ITfInsertAtSelection,
      public ITfContextComposition,
      public ITfSource,
      public ITfCompartmentMgr {
public:
    // These mocks allocate COM dependencies, so construction can throw std::bad_alloc.
#pragma warning(suppress : 26455)
    explicit MockTfContext(const std::shared_ptr<MockEditSessionQueue>& queue = {})
        : range_(MakeComObject<MockTfRange>()),
          view_(MakeComObject<MockTfContextView>()),
          prop_(MakeComObject<MockTfProperty>()),
          edit_session_queue_(queue),
          defer_edit_sessions_(queue != nullptr) {}

    void SetDeferredEditSessions(bool enabled) noexcept { defer_edit_sessions_ = enabled; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_ITfInsertAtSelection)) ++query_insert_calls_;
        if (IsEqualIID(riid, IID_ITfContextComposition)) ++query_composition_calls_;
        if (query_interface_observer_) query_interface_observer_(riid);
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext)) {
            *ppvObj = static_cast<ITfContext*>(this);
        } else if (IsEqualIID(riid, IID_ITfInsertAtSelection) && !fail_qi_insert_) {
            if (return_null_insert_) {
                *ppvObj = nullptr;
                return S_OK;
            }
            *ppvObj = static_cast<ITfInsertAtSelection*>(this);
        } else if (IsEqualIID(riid, IID_ITfContextComposition) && !fail_qi_comp_) {
            if (return_null_composition_context_) {
                *ppvObj = nullptr;
                return S_OK;
            }
            *ppvObj = static_cast<ITfContextComposition*>(this);
        } else if (IsEqualIID(riid, IID_ITfSource)) {
            *ppvObj = static_cast<ITfSource*>(this);
        } else if (IsEqualIID(riid, IID_ITfCompartmentMgr) && !fail_qi_compartment_mgr_) {
            if (return_null_compartment_mgr_) {
                *ppvObj = nullptr;
                return S_OK;
            }
            *ppvObj = static_cast<ITfCompartmentMgr*>(this);
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
            if (release_observer_) release_observer_();
            delete this;
        }
        return c;
    }

    // ITfContext
    STDMETHODIMP RequestEditSession(TfClientId client_id, ITfEditSession* pes, DWORD flags, HRESULT* phrSession) override {
        ++request_edit_session_calls_;
        last_edit_session_flags_ = flags;
        if (!pes || !phrSession) return E_INVALIDARG;
        constexpr DWORD timing_mask = TF_ES_SYNC | TF_ES_ASYNC | TF_ES_ASYNCDONTCARE;
        const DWORD timing = flags & timing_mask;
        const DWORD access = flags & TF_ES_READWRITE;
        if (timing == (TF_ES_SYNC | TF_ES_ASYNC) ||
            (access != TF_ES_READ && access != TF_ES_READWRITE) ||
            (flags & ~(timing_mask | TF_ES_READWRITE)) != 0) {
            *phrSession = E_INVALIDARG;
            QIQI_LOG_DEBUG("MockTfContext::RequestEditSession: invalid flags=0x{:x}", flags);
            return E_INVALIDARG;
        }
        if (FAILED(edit_session_request_result_)) {
            *phrSession = edit_session_request_result_;
            std::ignore = log::CheckHResult(edit_session_request_result_, "MockTfContext::RequestEditSession",
                                           log::Level::debug);
            return edit_session_request_result_;
        }
        if (read_only_ && access == TF_ES_READWRITE) {
            *phrSession = TS_E_READONLY;
            return S_OK;
        }
        if (timing == TF_ES_SYNC && !allow_sync_edit_sessions_) {
            *phrSession = TF_E_SYNCHRONOUS;
            return S_OK;
        }
        if (timing == TF_ES_ASYNC || (defer_edit_sessions_ && timing != TF_ES_SYNC)) {
            if (FAILED(async_edit_session_result_)) {
                *phrSession = async_edit_session_result_;
                std::ignore = log::CheckHResult(async_edit_session_result_, "MockTfContext::AsyncRequest",
                                               log::Level::debug);
                return async_edit_session_result_;
            }
            const auto queue = edit_session_queue_.lock();
            if (!queue) {
                *phrSession = E_UNEXPECTED;
                QIQI_LOG_ERROR("MockTfContext::RequestEditSession: asynchronous scheduler missing or expired");
                return E_UNEXPECTED;
            }
            const HRESULT result = queue->Enqueue(this, pes, client_id, flags);
            *phrSession = SUCCEEDED(result) ? TF_S_ASYNC : result;
            return result;
        }
        const bool previous_write_session = in_immediate_write_session_;
        in_immediate_write_session_ = access == TF_ES_READWRITE;
        const auto restore_write_session = gsl::finally([this, previous_write_session]() noexcept {
            in_immediate_write_session_ = previous_write_session;
        });
        *phrSession = pes->DoEditSession(1);
        return edit_session_request_result_;
    }
    STDMETHODIMP InWriteSession(TfClientId, BOOL* pfWriteSession) noexcept override {
        if (pfWriteSession) *pfWriteSession = TRUE;
        return S_OK;
    }
    STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION*, ULONG*) noexcept override { return S_OK; }
    STDMETHODIMP SetSelection(TfEditCookie ec, ULONG count, const TF_SELECTION* selection) override {
        ++set_selection_calls_;
        if (selection_observer_) selection_observer_(ec, count, selection);
        if (set_selection_callback_) set_selection_callback_();
        return set_selection_result_;
    }
    STDMETHODIMP GetStart(TfEditCookie, ITfRange** ppRange) override {
        if (!ppRange) return E_INVALIDARG;
        if (FAILED(get_start_result_)) {
            *ppRange = nullptr;
            return get_start_result_;
        }
        *ppRange = return_null_start_range_ ? nullptr : range_.Get();
        if (*ppRange) (*ppRange)->AddRef();
        return get_start_result_;
    }
    STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveView(ITfContextView** ppView) override {
        ++get_active_view_calls_;
        if (get_active_view_observer_) get_active_view_observer_(ppView);
        if (!ppView || fail_get_view_) return E_FAIL;
        *ppView = return_null_active_view_ ? nullptr : view_.Get();
        if (*ppView) (*ppView)->AddRef();
        return active_view_result_;
    }
    STDMETHODIMP EnumViews(IEnumTfContextViews**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetStatus(TF_STATUS* status) noexcept override {
        if (!status) return E_INVALIDARG;
        if (FAILED(get_status_result_)) return get_status_result_;
        *status = {};
        status->dwDynamicFlags = status_read_only_ ? TF_SD_READONLY : 0;
        return get_status_result_;
    }
    STDMETHODIMP GetProperty(REFGUID guid, ITfProperty** ppv) override {
        ++get_property_calls_;
        if (get_property_observer_) get_property_observer_(guid, ppv);
        if (!ppv) return E_INVALIDARG;
        if (return_null_property_) {
            *ppv = nullptr;
            return S_OK;
        }
        if (FAILED(get_property_result_)) {
            *ppv = nullptr;
            return get_property_result_;
        }
        *ppv = prop_.Get();
        (*ppv)->AddRef();
        if (get_property_callback_) get_property_callback_();
        return S_OK;
    }
    STDMETHODIMP GetAppProperty(REFGUID guid, ITfReadOnlyProperty** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (app_property_callback_) app_property_callback_(guid);
        if (FAILED(get_app_property_result_) || !app_property_) {
            return app_property_ ? get_app_property_result_ :
                   (get_app_property_result_ != S_OK ? get_app_property_result_ : E_NOTIMPL);
        }
        *ppv = app_property_.Get();
        (*ppv)->AddRef();
        return get_app_property_result_;
    }
    STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumProperties(IEnumTfProperties**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document) override {
        if (!document) return E_INVALIDARG;
        ++get_document_calls_;
        *document = nullptr;
        const auto link = document_link_.lock();
        if (!link || !link->document) return TF_E_DISCONNECTED;
        if (!return_null_document_) {
            *document = link->document;
            (*document)->AddRef();
        }
        if (get_document_callback_) get_document_callback_();
        return get_document_result_;
    }
    STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) noexcept override { return E_NOTIMPL; }

    // ITfInsertAtSelection
    STDMETHODIMP InsertTextAtSelection(TfEditCookie ec, DWORD flags, const WCHAR* pchText, LONG cch, ITfRange** ppRange) override {
        ++insert_at_selection_calls_;
        if (insert_observer_) insert_observer_(ec, flags, pchText, cch, ppRange);
        if (fail_insert_at_sel_) return E_FAIL;
        if (pchText && cch > 0) {
            range_->text_.assign(pchText, gsl::narrow_cast<size_t>(cch));
        }
        if (ppRange) {
            *ppRange = return_null_insert_range_ ? nullptr : range_.Get();
            if (*ppRange) (*ppRange)->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject*, ITfRange**) noexcept override { return E_NOTIMPL; }

    // ITfContextComposition
    STDMETHODIMP StartComposition(
        TfEditCookie ec, ITfRange* range, ITfCompositionSink* sink, ITfComposition** ppComposition) override {
        ++start_composition_calls_;
        if (start_composition_observer_) start_composition_observer_(ec, range, sink, ppComposition);
        if (!ppComposition || fail_start_comp_) return E_FAIL;
        *ppComposition = nullptr;
        if (!return_null_started_composition_) {
            auto comp = MakeComObject<MockTfComposition>(range_, sink);
            comp->fail_get_range_ = fail_comp_get_range_;
            last_composition_ = comp;
            *ppComposition = comp.Detach();
        }
        if (start_composition_callback_) start_composition_callback_();
        return start_composition_result_;
    }
    STDMETHODIMP EnumCompositions(IEnumITfCompositionView**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**) noexcept override { return E_NOTIMPL; }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override {
        if (!punk || !pdwCookie) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_ITfTextLayoutSink)) {
            ++layout_advise_calls_;
            if (FAILED(layout_advise_result_)) return layout_advise_result_;
            if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&layout_sink_))) || !layout_sink_) {
                return E_NOINTERFACE;
            }
            *pdwCookie = 202;
            if (layout_advise_callback_) layout_advise_callback_();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfStatusSink) && allow_status_sink_) {
            if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&status_sink_))) || !status_sink_) {
                return E_NOINTERFACE;
            }
            *pdwCookie = 203;
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfTextEditSink)) {
            if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&edit_sink_))) || !edit_sink_) {
                return E_NOINTERFACE;
            }
            *pdwCookie = 204;
            if (edit_advise_callback_) edit_advise_callback_();
            return S_OK;
        }
        return CONNECT_E_CANNOTCONNECT;
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        if (cookie == 204) {
            edit_sink_.Reset();
            return S_OK;
        }
        if (cookie == 203) {
            status_sink_.Reset();
            return S_OK;
        }
        ++layout_unadvise_calls_;
        layout_sink_.Reset();
        return S_OK;
    }

    // ITfCompartmentMgr
    STDMETHODIMP GetCompartment(REFGUID rguid, ITfCompartment** ppcomp) override {
        if (!ppcomp) return E_INVALIDARG;
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_DISABLED)) {
            if (FAILED(get_disabled_compartment_result_)) {
                *ppcomp = nullptr;
                return get_disabled_compartment_result_;
            }
            if (return_null_disabled_compartment_) {
                *ppcomp = nullptr;
                return S_OK;
            }
            if (!disabled_compartment_) {
                disabled_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_DISABLED);
            }
            *ppcomp = disabled_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_EMPTYCONTEXT)) {
            if (FAILED(get_empty_compartment_result_)) {
                *ppcomp = nullptr;
                return get_empty_compartment_result_;
            }
            if (return_null_empty_compartment_) {
                *ppcomp = nullptr;
                return S_OK;
            }
            if (!empty_compartment_) {
                empty_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_EMPTYCONTEXT);
            }
            *ppcomp = empty_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP ClearCompartment(TfClientId, REFGUID) noexcept override { return S_OK; }
    STDMETHODIMP EnumCompartments(IEnumGUID**) noexcept override { return E_NOTIMPL; }

    ComPtr<MockTfRange> range_;
    ComPtr<MockTfContextView> view_;
    ComPtr<MockTfComposition> last_composition_;
    unsigned int request_edit_session_calls_ = 0;
    DWORD last_edit_session_flags_ = 0;
    bool in_immediate_write_session_ = false;
    std::weak_ptr<MockDocumentLink> document_link_;
    HRESULT get_property_result_ = S_OK;
    unsigned int get_property_calls_ = 0;
    bool return_null_property_ = false;
    HRESULT async_edit_session_result_ = S_OK;
    unsigned int get_active_view_calls_ = 0;
    HRESULT active_view_result_ = S_OK;
    bool return_null_active_view_ = false;
    std::function<void(ITfContextView**)> get_active_view_observer_;
    unsigned int start_composition_calls_ = 0;
    std::move_only_function<void() noexcept> release_observer_;
    unsigned int set_selection_calls_ = 0;
    HRESULT set_selection_result_ = S_OK;
    std::function<void(TfEditCookie, ULONG, const TF_SELECTION*)> selection_observer_;
    std::function<void()> set_selection_callback_;
    std::function<void()> get_property_callback_;
    std::function<void(REFGUID, ITfProperty**)> get_property_observer_;
    std::function<void()> start_composition_callback_;
    std::function<void(TfEditCookie, ITfRange*, ITfCompositionSink*, ITfComposition**)> start_composition_observer_;
    HRESULT start_composition_result_ = S_OK;
    bool return_null_started_composition_ = false;
    ComPtr<MockTfProperty> prop_;
    ComPtr<ITfTextLayoutSink> layout_sink_;
    HRESULT layout_advise_result_ = S_OK;
    unsigned int layout_advise_calls_ = 0;
    unsigned int layout_unadvise_calls_ = 0;
    std::function<void()> get_document_callback_;
    HRESULT get_document_result_ = S_OK;
    bool return_null_document_ = false;
    unsigned int get_document_calls_ = 0;
    std::function<void()> layout_advise_callback_;
    bool fail_qi_compartment_mgr_ = false;
    bool return_null_compartment_mgr_ = false;
    HRESULT get_disabled_compartment_result_ = S_OK;
    bool return_null_disabled_compartment_ = false;
    HRESULT get_empty_compartment_result_ = S_OK;
    bool return_null_empty_compartment_ = false;
    ComPtr<MockTfCompartment> disabled_compartment_;
    ComPtr<MockTfCompartment> empty_compartment_;
    bool fail_qi_insert_ = false;
    bool fail_insert_at_sel_ = false;
    bool fail_qi_comp_ = false;
    bool return_null_insert_ = false;
    bool return_null_insert_range_ = false;
    bool return_null_composition_context_ = false;
    unsigned int query_insert_calls_ = 0;
    unsigned int insert_at_selection_calls_ = 0;
    unsigned int query_composition_calls_ = 0;
    std::function<void(REFIID)> query_interface_observer_;
    std::function<void(TfEditCookie, DWORD, const WCHAR*, LONG, ITfRange**)> insert_observer_;
    bool fail_start_comp_ = false;
    bool fail_comp_get_range_ = false;
    bool fail_get_view_ = false;
    HRESULT edit_session_request_result_ = S_OK;
    bool allow_sync_edit_sessions_ = true;
    bool read_only_ = false;
    bool status_read_only_ = false;
    HRESULT get_status_result_ = S_OK;
    HRESULT get_start_result_ = E_NOTIMPL;
    bool return_null_start_range_ = false;
    HRESULT get_app_property_result_ = E_NOTIMPL;
    ComPtr<ITfReadOnlyProperty> app_property_;
    std::function<void(REFGUID)> app_property_callback_;
    bool allow_status_sink_ = false;
    ComPtr<ITfStatusSink> status_sink_;
    ComPtr<ITfTextEditSink> edit_sink_;
    std::function<void()> edit_advise_callback_;
private:
    ULONG ref_ = 1;
    // The external queue holds contexts/sessions; a strong back-reference here would create a COM ownership cycle.
    std::weak_ptr<MockEditSessionQueue> edit_session_queue_;
    bool defer_edit_sessions_ = false;
};

// Minimal mock ITfThreadMgr + ITfSource + ITfSourceSingle + ITfKeystrokeMgr + ITfUIElementMgr + ITfCompartmentMgr + ITfLangBarItemMgr

} // namespace qiqi::test
