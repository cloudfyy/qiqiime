#pragma once
#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "common/com_object.h"
#include "common/guids.h"
#include "common/logger.h"

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

// Minimal mock ITfRange
class MockTfRange final : public ITfRange {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfRange)) {
            *ppvObj = static_cast<ITfRange*>(this);
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
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) noexcept override { return S_OK; }
    STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR* pch, LONG cch) override {
        ++set_text_calls_;
        if (pch && cch >= 0) {
            text_.assign(pch, gsl::narrow_cast<size_t>(cch));
        }
        if (set_text_callback_) set_text_callback_();
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) noexcept override { return S_OK; }
    STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP IsEmpty(TfEditCookie, BOOL* pfEmpty) noexcept override {
        if (pfEmpty) *pfEmpty = text_.empty() ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP Collapse(TfEditCookie, TfAnchor) override {
        if (collapse_callback_) collapse_callback_();
        return S_OK;
    }
    STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) noexcept override { return S_OK; }
    STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) noexcept override { return S_OK; }
    STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) noexcept override { return S_OK; }
    STDMETHODIMP GetGravity(TfGravity*, TfGravity*) noexcept override { return S_OK; }
    STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) noexcept override { return S_OK; }
    STDMETHODIMP Clone(ITfRange** ppClone) override {
        if (!ppClone) return E_INVALIDARG;
        ++clone_calls_;
        auto clone = MakeComObject<MockTfRange>();
        clone->text_ = text_;
        clone->collapse_callback_ = collapse_callback_;
        *ppClone = clone.Detach();
        if (clone_callback_) clone_callback_();
        return S_OK;
    }
    STDMETHODIMP GetContext(ITfContext**) noexcept override { return E_NOTIMPL; }

    std::wstring text_;
    unsigned int set_text_calls_ = 0;
    unsigned int clone_calls_ = 0;
    std::function<void()> set_text_callback_;
    std::function<void()> clone_callback_;
    std::function<void()> collapse_callback_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfComposition
class MockTfComposition final : public ITfComposition {
public:
    explicit MockTfComposition(ComPtr<MockTfRange> range, ITfCompositionSink* sink = nullptr) noexcept
        : range_(std::move(range)), sink_(sink) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfComposition)) {
            *ppvObj = static_cast<ITfComposition*>(this);
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
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetRange(ITfRange** ppRange) override {
        if (!ppRange || fail_get_range_) return E_FAIL;
        if (get_range_callback_) get_range_callback_();
        *ppRange = range_.Get();
        (*ppRange)->AddRef();
        return S_OK;
    }
    STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) noexcept override { return S_OK; }
    STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) noexcept override { return S_OK; }
    STDMETHODIMP EndComposition(TfEditCookie ec) override {
        ++end_composition_calls_;
        last_end_cookie_ = ec;
        if (end_callback_) end_callback_(ec);
        if (FAILED(end_result_)) return end_result_;
        ended_ = true;
        sink_.Reset();
        return S_OK;
    }
    HRESULT NotifyHostTermination(TfEditCookie ec) {
        ended_ = true;
        const auto sink = std::move(sink_);
        return sink ? sink->OnCompositionTerminated(ec, this) : S_OK;
    }

    ComPtr<MockTfRange> range_;
    bool ended_ = false;
    bool fail_get_range_ = false;
    unsigned int end_composition_calls_ = 0;
    TfEditCookie last_end_cookie_ = 0;
    HRESULT end_result_ = S_OK;
    std::function<void(TfEditCookie)> end_callback_;
    std::function<void()> get_range_callback_;
private:
    ComPtr<ITfCompositionSink> sink_;
    ULONG ref_ = 1;
};

// Minimal mock ITfContextView
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
            // COM Release destroys the mock when its reference count reaches zero.
#pragma warning(suppress : 26401 26409)
            delete this;
        }
        return c;
    }
    STDMETHODIMP GetRangeFromPoint(TfEditCookie, const POINT*, DWORD, ITfRange**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TfEditCookie, ITfRange*, RECT* prc, BOOL* pfClipped) override {
        if (get_text_ext_callback_) get_text_ext_callback_();
        if (fail_get_text_ext_) return TS_E_NOLAYOUT;
        if (prc) *prc = text_ext_;
        if (pfClipped) *pfClipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(RECT* prc) noexcept override {
        if (prc) *prc = {0, 0, 1920, 1080};
        return S_OK;
    }
    STDMETHODIMP GetWnd(HWND* phwnd) noexcept override {
        if (phwnd) *phwnd = nullptr;
        return S_OK;
    }
    bool fail_get_text_ext_ = false;
    RECT text_ext_{100, 100, 140, 124};
    std::function<void()> get_text_ext_callback_;
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
    STDMETHODIMP SetValue(TfEditCookie, ITfRange*, const VARIANT*) override {
        set_called_ = true;
        ++set_calls_;
        if (set_callback_) set_callback_();
        return S_OK;
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
class MockTfCompartment final : public ITfCompartment, public ITfSource {
public:
    explicit MockTfCompartment(REFGUID guid = GUID_NULL) noexcept : guid_(guid) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompartment)) {
            *ppvObj = static_cast<ITfCompartment*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfSource)) {
            *ppvObj = static_cast<ITfSource*>(this);
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
            delete this;
        }
        return c;
    }
    STDMETHODIMP SetValue(TfClientId, const VARIANT* pvarValue) override {
        if (fail_set_value_) return E_FAIL;
        if (pvarValue && pvarValue->vt == VT_I4) {
            last_val_ = pvarValue->lVal;
            if (sink_) {
                sink_->OnChange(guid_);
            }
        }
        return S_OK;
    }
    STDMETHODIMP GetValue(VARIANT* pvarValue) override {
        if (!pvarValue) return E_INVALIDARG;
        if (get_value_result_ != S_OK) {
            ::VariantInit(pvarValue);
            return get_value_result_;
        }
        pvarValue->vt = VT_I4;
        pvarValue->lVal = last_val_;
        if (get_value_callback_) get_value_callback_();
        return S_OK;
    }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override {
        if (!punk || !pdwCookie) return E_INVALIDARG;
        if (!IsEqualIID(riid, IID_ITfCompartmentEventSink)) return CONNECT_E_CANNOTCONNECT;
        if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&sink_))) || !sink_) return E_NOINTERFACE;
        *pdwCookie = 101;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD) override {
        sink_.Reset();
        return S_OK;
    }

    GUID guid_{};
    LONG last_val_ = 0;
    HRESULT get_value_result_ = S_OK;
    bool fail_set_value_ = false;
    std::function<void()> get_value_callback_;
    ComPtr<ITfCompartmentEventSink> sink_;
private:
    ULONG ref_ = 1;
};

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

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext)) {
            *ppvObj = static_cast<ITfContext*>(this);
        } else if (IsEqualIID(riid, IID_ITfInsertAtSelection) && !fail_qi_insert_) {
            *ppvObj = static_cast<ITfInsertAtSelection*>(this);
        } else if (IsEqualIID(riid, IID_ITfContextComposition) && !fail_qi_comp_) {
            *ppvObj = static_cast<ITfContextComposition*>(this);
        } else if (IsEqualIID(riid, IID_ITfSource)) {
            *ppvObj = static_cast<ITfSource*>(this);
        } else if (IsEqualIID(riid, IID_ITfCompartmentMgr)) {
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
        if (c == 0) delete this;
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
        return S_OK;
    }
    STDMETHODIMP InWriteSession(TfClientId, BOOL* pfWriteSession) noexcept override {
        if (pfWriteSession) *pfWriteSession = TRUE;
        return S_OK;
    }
    STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION*, ULONG*) noexcept override { return S_OK; }
    STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION*) override {
        ++set_selection_calls_;
        if (set_selection_callback_) set_selection_callback_();
        return S_OK;
    }
    STDMETHODIMP GetStart(TfEditCookie, ITfRange**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveView(ITfContextView** ppView) override {
        ++get_active_view_calls_;
        if (!ppView || fail_get_view_) return E_FAIL;
        *ppView = view_.Get();
        (*ppView)->AddRef();
        return S_OK;
    }
    STDMETHODIMP EnumViews(IEnumTfContextViews**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetStatus(TF_STATUS*) noexcept override { return S_OK; }
    STDMETHODIMP GetProperty(REFGUID, ITfProperty** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (FAILED(get_property_result_)) {
            *ppv = nullptr;
            return get_property_result_;
        }
        *ppv = prop_.Get();
        (*ppv)->AddRef();
        if (get_property_callback_) get_property_callback_();
        return S_OK;
    }
    STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumProperties(IEnumTfProperties**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document) override {
        if (!document) return E_INVALIDARG;
        *document = nullptr;
        const auto link = document_link_.lock();
        if (!link || !link->document) return TF_E_DISCONNECTED;
        *document = link->document;
        (*document)->AddRef();
        if (get_document_callback_) get_document_callback_();
        return S_OK;
    }
    STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) noexcept override { return E_NOTIMPL; }

    // ITfInsertAtSelection
    STDMETHODIMP InsertTextAtSelection(TfEditCookie, DWORD, const WCHAR* pchText, LONG cch, ITfRange** ppRange) override {
        if (fail_insert_at_sel_) return E_FAIL;
        if (pchText && cch > 0) {
            range_->text_.assign(pchText, gsl::narrow_cast<size_t>(cch));
        }
        if (ppRange) {
            *ppRange = range_.Get();
            (*ppRange)->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject*, ITfRange**) noexcept override { return E_NOTIMPL; }

    // ITfContextComposition
    STDMETHODIMP StartComposition(TfEditCookie, ITfRange*, ITfCompositionSink* sink, ITfComposition** ppComposition) override {
        ++start_composition_calls_;
        if (!ppComposition || fail_start_comp_) return E_FAIL;
        auto comp = MakeComObject<MockTfComposition>(range_, sink);
        comp->fail_get_range_ = fail_comp_get_range_;
        last_composition_ = comp;
        *ppComposition = comp.Detach();
        if (start_composition_callback_) start_composition_callback_();
        return S_OK;
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
        return CONNECT_E_CANNOTCONNECT;
    }
    STDMETHODIMP UnadviseSink(DWORD) override {
        ++layout_unadvise_calls_;
        layout_sink_.Reset();
        return S_OK;
    }

    // ITfCompartmentMgr
    STDMETHODIMP GetCompartment(REFGUID rguid, ITfCompartment** ppcomp) override {
        if (!ppcomp) return E_INVALIDARG;
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_DISABLED)) {
            if (!disabled_compartment_) {
                disabled_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_DISABLED);
            }
            *ppcomp = disabled_compartment_.Get();
            (*ppcomp)->AddRef();
            return S_OK;
        }
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_EMPTYCONTEXT)) {
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
    HRESULT async_edit_session_result_ = S_OK;
    unsigned int get_active_view_calls_ = 0;
    unsigned int start_composition_calls_ = 0;
    unsigned int set_selection_calls_ = 0;
    std::function<void()> set_selection_callback_;
    std::function<void()> get_property_callback_;
    std::function<void()> start_composition_callback_;
    ComPtr<MockTfProperty> prop_;
    ComPtr<ITfTextLayoutSink> layout_sink_;
    HRESULT layout_advise_result_ = S_OK;
    unsigned int layout_advise_calls_ = 0;
    unsigned int layout_unadvise_calls_ = 0;
    std::function<void()> get_document_callback_;
    std::function<void()> layout_advise_callback_;
    ComPtr<MockTfCompartment> disabled_compartment_;
    ComPtr<MockTfCompartment> empty_compartment_;
    bool fail_qi_insert_ = false;
    bool fail_insert_at_sel_ = false;
    bool fail_qi_comp_ = false;
    bool fail_start_comp_ = false;
    bool fail_comp_get_range_ = false;
    bool fail_get_view_ = false;
    HRESULT edit_session_request_result_ = S_OK;
    bool allow_sync_edit_sessions_ = true;
    bool read_only_ = false;
private:
    ULONG ref_ = 1;
    // The external queue holds contexts/sessions; a strong back-reference here would create a COM ownership cycle.
    std::weak_ptr<MockEditSessionQueue> edit_session_queue_;
    const bool defer_edit_sessions_ = false;
};

// Minimal mock ITfThreadMgr + ITfSource + ITfSourceSingle + ITfKeystrokeMgr + ITfUIElementMgr + ITfCompartmentMgr + ITfLangBarItemMgr
class MockTfDocumentMgr final : public ComObjectBase<MockTfDocumentMgr, ITfDocumentMgr> {
public:
    // The weak parent link is allocated and can fail with std::bad_alloc.
#pragma warning(suppress : 26455)
    MockTfDocumentMgr() : link_(std::make_shared<MockDocumentLink>()) { link_->document = this; }

    ComPtr<MockTfContext> CreateTestContext(const std::shared_ptr<MockEditSessionQueue>& queue = {}) {
        auto context = MakeComObject<MockTfContext>(queue);
        context->document_link_ = link_;
        return context;
    }

    STDMETHODIMP CreateContext(TfClientId, DWORD, IUnknown*, ITfContext** context, TfEditCookie* cookie) override {
        if (!context || !cookie) return E_INVALIDARG;
        *context = CreateTestContext().Detach();
        *cookie = 1;
        return S_OK;
    }
    STDMETHODIMP Push(ITfContext* context) override {
        if (!context) return E_INVALIDARG;
        if (popping_) return E_UNEXPECTED;
        if (stack_.size() == 2) return TF_E_STACKFULL;
        ComPtr<ITfDocumentMgr> owner;
        const HRESULT hr = context->GetDocumentMgr(&owner);
        if (FAILED(hr)) return hr;
        if (owner.Get() != this) return E_INVALIDARG;
        stack_.emplace_back(context);
        if (events && stack_.size() == 1) {
            const HRESULT init_hr = events->OnInitDocumentMgr(this);
            if (FAILED(init_hr)) return init_hr;
        }
        return events ? events->OnPushContext(context) : S_OK;
    }
    STDMETHODIMP Pop(DWORD flags) override {
        if (flags != 0 && flags != TF_POPF_ALL) return E_INVALIDARG;
        if (popping_) return E_UNEXPECTED;
        if (stack_.empty() || (flags == 0 && stack_.size() == 1)) return E_FAIL;
        popping_ = true;
        const auto finish = gsl::finally([this]() noexcept { popping_ = false; });
        const size_t count = flags == TF_POPF_ALL ? stack_.size() : 1;
        for (size_t i = 0; i < count; ++i) {
            const auto context = std::move(stack_.back());
            stack_.pop_back();
            if (events) {
                const HRESULT hr = events->OnPopContext(context.Get());
                if (FAILED(hr)) return hr;
            }
        }
        return events && stack_.empty() ? events->OnUninitDocumentMgr(this) : S_OK;
    }
    STDMETHODIMP GetTop(ITfContext** context) override {
        if (!context) return E_INVALIDARG;
        *context = nullptr;
        if (FAILED(get_top_result)) return get_top_result;
        if (get_top_callback) get_top_callback();
        if (!stack_.empty()) {
            *context = stack_.back().Get();
            (*context)->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP GetBase(ITfContext** context) override {
        if (!context) return E_INVALIDARG;
        *context = nullptr;
        if (FAILED(get_base_result)) return get_base_result;
        if (!stack_.empty()) {
            *context = stack_.front().Get();
            (*context)->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP EnumContexts(IEnumTfContexts** contexts) noexcept override {
        if (contexts) *contexts = nullptr;
        return E_NOTIMPL;
    }

    // Non-owning observer: tests keep the sink alive and disconnect it before teardown.
    ITfThreadMgrEventSink* events = nullptr;
    HRESULT get_top_result = S_OK;
    HRESULT get_base_result = S_OK;
    std::function<void()> get_top_callback;

private:
    const std::shared_ptr<MockDocumentLink> link_;
    std::vector<ComPtr<ITfContext>> stack_;
    bool popping_ = false;
};

class MockTfDocumentMgrAlias final : public ComObjectBase<MockTfDocumentMgrAlias, ITfDocumentMgr> {
public:
    explicit MockTfDocumentMgrAlias(gsl::not_null<ITfDocumentMgr*> document) noexcept : document_(document.get()) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) noexcept override {
        if (IsEqualIID(riid, IID_IUnknown)) {
            if (!object) return E_INVALIDARG;
            *object = nullptr;
            if (FAILED(identity_result)) return identity_result;
#pragma warning(suppress : 26447) // COM identity queries report failure through HRESULT.
            return document_->QueryInterface(riid, object);
        }
        return ComObjectBase<MockTfDocumentMgrAlias, ITfDocumentMgr>::QueryInterface(riid, object);
    }
    STDMETHODIMP CreateContext(TfClientId client, DWORD flags, IUnknown* owner,
                              ITfContext** context, TfEditCookie* cookie) override {
        return document_->CreateContext(client, flags, owner, context, cookie);
    }
    STDMETHODIMP Push(ITfContext* context) override { return document_->Push(context); }
    STDMETHODIMP Pop(DWORD flags) override { return document_->Pop(flags); }
    STDMETHODIMP GetTop(ITfContext** context) override { return document_->GetTop(context); }
    STDMETHODIMP GetBase(ITfContext** context) override { return document_->GetBase(context); }
    STDMETHODIMP EnumContexts(IEnumTfContexts** contexts) override { return document_->EnumContexts(contexts); }

    HRESULT identity_result = S_OK;
private:
    const ComPtr<ITfDocumentMgr> document_;
};

class MockTfThreadMgr final
    : public ITfThreadMgr,
      public ITfSource,
      public ITfSourceSingle,
      public ITfKeystrokeMgr,
      public ITfUIElementMgr,
      public ITfCompartmentMgr,
      public ITfLangBarItemMgr {
public:
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
    STDMETHODIMP GetFocus(ITfDocumentMgr**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) noexcept override { return S_OK; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL* pfThreadFocus) noexcept override { if (pfThreadFocus) *pfThreadFocus = TRUE; return S_OK; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** ppCompMgr) noexcept override {
        if (!ppCompMgr) return E_INVALIDARG;
        *ppCompMgr = this;
        AddRef();
        return S_OK;
    }

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID, IUnknown*, DWORD* pdwCookie) noexcept override {
        if (fail_advise_) return E_FAIL;
        if (pdwCookie) *pdwCookie = 42;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD) noexcept override { return S_OK; }

    // ITfSourceSingle
    STDMETHODIMP AdviseSingleSink(TfClientId, REFIID, IUnknown* punk) noexcept override {
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
    STDMETHODIMP AdviseKeyEventSink(TfClientId tid, ITfKeyEventSink* sink, BOOL) noexcept override {
        ++advise_key_sink_calls_;
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
        const DWORD id = next_ui_element_id_++;
        if (pdwUIElementId) *pdwUIElementId = id;
        if (begin_ui_callback_) begin_ui_callback_(id);
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) noexcept override { ++update_ui_calls_; return S_OK; }
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
    STDMETHODIMP AddItem(ITfLangBarItem* punk) noexcept override {
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
    ComPtr<MockTfCompartment> convmode_compartment_;
    ComPtr<MockTfCompartment> global_config_compartment_;
    ComPtr<MockTfCompartment> global_pos_compartment_;
    ComPtr<IUnknown> single_sink_;
    ComPtr<ITfKeyEventSink> key_event_sink_;
    TfClientId key_sink_client_id_ = TF_CLIENTID_NULL;
    TfClientId last_unadvised_key_sink_client_id_ = TF_CLIENTID_NULL;
    unsigned int advise_key_sink_calls_ = 0;
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
    DWORD next_ui_element_id_ = 7;
    std::vector<DWORD> ended_ui_ids_;
    std::function<void(DWORD)> begin_ui_callback_;
    std::function<void(DWORD)> end_ui_callback_;
    HRESULT end_ui_result_ = S_OK;
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
class MockTfInputProcessorProfiles final
    : public ITfInputProcessorProfilesEx,
      public ITfInputProcessorProfileMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfInputProcessorProfiles) ||
            IsEqualIID(riid, IID_ITfInputProcessorProfilesEx)) {
            *ppvObj = static_cast<ITfInputProcessorProfilesEx*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfInputProcessorProfileMgr) && !fail_qi_profile_mgr_) {
            *ppvObj = static_cast<ITfInputProcessorProfileMgr*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP Register(REFCLSID) noexcept override {
        if (fail_register_) return E_FAIL;
        registered_ = true;
        return S_OK;
    }
    STDMETHODIMP Unregister(REFCLSID) noexcept override {
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
    STDMETHODIMP RemoveLanguageProfile(REFCLSID, LANGID, REFGUID) noexcept override { return S_OK; }
    STDMETHODIMP EnumInputProcessorInfo(IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetDefaultLanguageProfile(LANGID, REFGUID, CLSID*, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetDefaultLanguageProfile(LANGID, REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ActivateLanguageProfile(REFCLSID, LANGID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveLanguageProfile(REFCLSID, LANGID*, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageProfileDescription(REFCLSID, LANGID, REFGUID, BSTR*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetCurrentLanguage(LANGID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ChangeCurrentLanguage(LANGID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageList(LANGID**, ULONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumLanguageProfiles(LANGID, IEnumTfLanguageProfiles**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfile(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) noexcept override {
        ++enable_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_) return E_ACCESSDENIED;
        enabled_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP IsEnabledLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfileByDefault(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) noexcept override {
        ++enable_default_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_default_) return E_ACCESSDENIED;
        enabled_by_default_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP SubstituteKeyboardLayout(REFCLSID, LANGID, REFGUID, HKL) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetLanguageProfileDisplayName(REFCLSID, LANGID, REFGUID, const WCHAR*, ULONG, ULONG res_id) noexcept override {
        display_name_res_id_ = res_id;
        return S_OK;
    }

    // ITfInputProcessorProfileMgr
    STDMETHODIMP ActivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD) noexcept override {
        ++activate_calls_;
        return S_OK;
    }
    STDMETHODIMP DeactivateProfile(DWORD profile_type, LANGID langid, REFCLSID clsid, REFGUID profile, HKL, DWORD flags) noexcept override {
        ++deactivate_calls_;
        if (profile_type != TF_PROFILETYPE_INPUTPROCESSOR || !IsEqualCLSID(clsid, kClsidQiQiTextService) ||
            langid != kChineseSimplifiedLangId || !IsEqualGUID(profile, kGuidProfileWubi86)) {
            return E_INVALIDARG;
        }
        if (fail_deactivate_) return E_FAIL;
        deactivate_flags_ = flags;
        return S_OK;
    }
    STDMETHODIMP RegisterProfile(REFCLSID, LANGID, REFGUID, const WCHAR*, ULONG, const WCHAR*, ULONG, ULONG, HKL, DWORD, BOOL, DWORD) noexcept override {
        if (fail_register_profile_) return E_FAIL;
        profile_mgr_registered_ = true;
        return S_OK;
    }
    STDMETHODIMP UnregisterProfile(REFCLSID, LANGID, REFGUID, DWORD) noexcept override { return S_OK; }
    STDMETHODIMP GetProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, TF_INPUTPROCESSORPROFILE*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveProfile(REFGUID, TF_INPUTPROCESSORPROFILE*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumProfiles(LANGID, IEnumTfInputProcessorProfiles**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ReleaseInputProcessor(REFCLSID, DWORD) noexcept override { return E_NOTIMPL; }

    bool fail_register_ = false;
    bool fail_add_profile_ = false;
    bool fail_register_profile_ = false;
    bool fail_enable_ = false;
    bool fail_enable_default_ = false;
    bool fail_qi_profile_mgr_ = false;
    bool fail_deactivate_ = false;
    bool enabled_ = false;
    bool enabled_by_default_ = false;
    int enable_calls_ = 0;
    int enable_default_calls_ = 0;
    ULONG display_name_res_id_ = 0;
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

// Minimal mock ITfCategoryMgr for registration and display attribute tests.
class MockTfCategoryMgr final : public ITfCategoryMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCategoryMgr)) {
            *ppvObj = static_cast<ITfCategoryMgr*>(this);
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
            if (on_final_release_) on_final_release_(this, release_context_);
            // COM Release destroys the mock when its reference count reaches zero.
#pragma warning(suppress : 26401 26409)
            delete this;
        }
        return c;
    }
    bool HasCategory(REFGUID category) const noexcept {
        return std::any_of(registered_categories_.begin(), registered_categories_.end(),
                           [&](const GUID& existing) { return IsEqualGUID(existing, category); });
    }
    STDMETHODIMP RegisterCategory(REFCLSID, REFGUID category, REFGUID) override {
        register_category_calls_.push_back(category);
        if (!HasCategory(category)) registered_categories_.push_back(category);
        return S_OK;
    }
    STDMETHODIMP UnregisterCategory(REFCLSID, REFGUID category, REFGUID) override {
        unregister_category_calls_.push_back(category);
        if (FAILED(unregister_category_result_)) return unregister_category_result_;
        std::erase_if(registered_categories_, [&](const GUID& existing) { return IsEqualGUID(existing, category); });
        return S_OK;
    }
    STDMETHODIMP EnumCategoriesInItem(REFGUID, IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumItemsInCategory(REFGUID, IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP FindClosestCategory(REFGUID, GUID* category, const GUID** list, ULONG count) noexcept override {
        if (!category || (count != 0 && !list)) return E_INVALIDARG;
        *category = GUID_NULL;
        if (FAILED(find_category_result_)) return find_category_result_;
        if (count == 0 && !registered_categories_.empty()) {
            *category = registered_categories_.front();
            return S_OK;
        }
        for (const auto* candidate : std::span<const GUID* const>(list, count)) {
            if (!candidate) return E_INVALIDARG;
            if (HasCategory(*candidate)) {
                *category = *candidate;
                return S_OK;
            }
        }
        return missing_category_result_;
    }
    STDMETHODIMP RegisterGUIDDescription(REFCLSID, REFGUID, const WCHAR*, ULONG) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDescription(REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDescription(REFGUID, BSTR*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUIDDWORD(REFCLSID, REFGUID, DWORD) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDWORD(REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDWORD(REFGUID, DWORD*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUID(REFGUID guid, TfGuidAtom* atom) noexcept override {
        if (!atom) return E_INVALIDARG;
        ++register_guid_calls_;
        registered_guid_ = guid;
        *atom = registered_atom_;
        return register_guid_result_;
    }
    STDMETHODIMP GetGUID(TfGuidAtom, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualTfGuidAtom(TfGuidAtom, REFGUID, BOOL*) noexcept override { return E_NOTIMPL; }

    std::vector<GUID> registered_categories_;
    std::vector<GUID> register_category_calls_;
    std::vector<GUID> unregister_category_calls_;
    HRESULT find_category_result_ = S_OK;
    HRESULT missing_category_result_ = S_FALSE;
    HRESULT unregister_category_result_ = S_OK;
    size_t register_guid_calls_ = 0;
    GUID registered_guid_{};
    TfGuidAtom registered_atom_ = 73;
    HRESULT register_guid_result_ = E_NOTIMPL;
    void (*on_final_release_)(MockTfCategoryMgr*, void*) noexcept = nullptr;
    void* release_context_ = nullptr;
private:
    ULONG ref_ = 1;
};

} // namespace qiqi::test
