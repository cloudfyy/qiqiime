#include "tsf/tip_service.h"
#include "tsf/edit_session.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/version.h"

#include <array>
#include <filesystem>

namespace qiqi {

namespace {

bool IsKeyDown(int vk) noexcept {
    return (::GetKeyState(vk) & 0x8000) != 0;
}

} // namespace

QiQiTextService::QiQiTextService() {
    ++g_com_object_count;
    candidate_ui_.Attach(new CandidateUI(g_dll_instance));
    LoadExternalDictionaryNextToDll();
}

QiQiTextService::~QiQiTextService() noexcept {
    if (candidate_ui_) {
        candidate_ui_->DestroyPopupWindow();
        candidate_ui_.Reset();
    }
    --g_com_object_count;
}

void QiQiTextService::LoadExternalDictionaryNextToDll() {
    std::array<wchar_t, MAX_PATH> module_path{};
    if (::GetModuleFileNameW(g_dll_instance, module_path.data(), static_cast<DWORD>(module_path.size())) == 0) {
        QIQI_LOG_WARN("GetModuleFileNameW failed when locating external dictionary");
        return;
    }
    std::filesystem::path dict_path(module_path.data());
    dict_path.replace_filename(L"wubi86.txt");
    if (std::filesystem::exists(dict_path)) {
        engine_.LoadDictionaryFile(dict_path);
    } else {
        QIQI_LOG_DEBUG("External dictionary not found at '{}', using builtin dictionary only",
                       log::WideToUtf8(dict_path.wstring()));
    }
}

STDMETHODIMP QiQiTextService::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    } else {
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) QiQiTextService::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) QiQiTextService::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP QiQiTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP QiQiTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    if (!ptim) {
        return E_INVALIDARG;
    }
    thread_mgr_ = ptim;
    client_id_ = tid;
    activate_flags_ = dwFlags;

    QIQI_LOG_INFO("QiQiTextService::ActivateEx(version={}, arch={}, client_id={}, flags=0x{:x})",
                  version::kFullVersion, version::kArch, tid, dwFlags);

    if (!InitThreadMgrEventSink() || !InitKeyEventSink()) {
        QIQI_LOG_ERROR("QiQiTextService::ActivateEx failed to initialize TSF event sinks");
        Deactivate();
        return E_FAIL;
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::Deactivate() {
    QIQI_LOG_INFO("QiQiTextService::Deactivate(client_id={})", client_id_);
    EndCandidateUI();
    composition_.Reset();
    engine_.Reset();

    UninitKeyEventSink();
    UninitThreadMgrEventSink();

    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    activate_flags_ = 0;
    log::Flush();
    return S_OK;
}

bool QiQiTextService::InitThreadMgrEventSink() {
    ComPtr<ITfSource> source;
    if (FAILED(thread_mgr_.As(&source)) || !source) {
        return false;
    }
    return SUCCEEDED(source->AdviseSink(
        IID_ITfThreadMgrEventSink,
        static_cast<ITfThreadMgrEventSink*>(this),
        &thread_mgr_event_cookie_));
}

void QiQiTextService::UninitThreadMgrEventSink() {
    if (thread_mgr_event_cookie_ == TF_INVALID_COOKIE || !thread_mgr_) {
        return;
    }
    ComPtr<ITfSource> source;
    if (SUCCEEDED(thread_mgr_.As(&source)) && source) {
        source->UnadviseSink(thread_mgr_event_cookie_);
    }
    thread_mgr_event_cookie_ = TF_INVALID_COOKIE;
}

bool QiQiTextService::InitKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (FAILED(thread_mgr_.As(&key_mgr)) || !key_mgr) {
        return false;
    }
    if (FAILED(key_mgr->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE))) {
        return false;
    }

    // Register Ctrl+Space as a preserved key to toggle Chinese/English mode
    TF_PRESERVEDKEY pk{};
    pk.uVKey = VK_SPACE;
    pk.uModifiers = TF_MOD_CONTROL;
    key_mgr->PreserveKey(client_id_, kGuidPreservedKeyToggleMode, &pk, L"切换中英文", 5);
    return true;
}

void QiQiTextService::UninitKeyEventSink() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (SUCCEEDED(thread_mgr_.As(&key_mgr)) && key_mgr) {
        TF_PRESERVEDKEY pk{};
        pk.uVKey = VK_SPACE;
        pk.uModifiers = TF_MOD_CONTROL;
        key_mgr->UnpreserveKey(kGuidPreservedKeyToggleMode, &pk);
        key_mgr->UnadviseKeyEventSink(client_id_);
    }
}

// ITfThreadMgrEventSink
STDMETHODIMP QiQiTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* /*pdimPrevFocus*/) {
    if (!pdimFocus) {
        // Focus left our document: clean up composition UI
        engine_.Reset();
        composition_.Reset();
        EndCandidateUI();
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPushContext(ITfContext* /*pic*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPopContext(ITfContext* /*pic*/) noexcept {
    return S_OK;
}

// ITfKeyEventSink
STDMETHODIMP QiQiTextService::OnSetFocus(BOOL fForeground) {
    if (!fForeground) {
        engine_.Reset();
        composition_.Reset();
        EndCandidateUI();
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const uint32_t vk = static_cast<uint32_t>(wParam);
    if (vk == VK_SHIFT) {
        shift_tap_candidate_ = !IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_MENU);
        *pfEaten = FALSE;
        return S_OK;
    }
    shift_tap_candidate_ = false;

    *pfEaten = engine_.ShouldEatKey(vk, IsKeyDown(VK_SHIFT), IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU))
                   ? TRUE
                   : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const uint32_t vk = static_cast<uint32_t>(wParam);
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);

    if (!engine_.ShouldEatKey(vk, shift, ctrl, alt)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = TRUE;
    const KeyProcessResult result = engine_.ProcessKey(vk, shift, ctrl, alt);
    if (pic && result.action != KeyActionType::PassThrough) {
        ApplyEngineResult(pic, result);
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    *pfEaten = (wParam == VK_SHIFT && shift_tap_candidate_) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (wParam == VK_SHIFT && shift_tap_candidate_) {
        shift_tap_candidate_ = false;
        *pfEaten = TRUE;
        // If composing when Shift is tapped, cancel composition and switch mode
        if (engine_.IsComposing() && pic) {
            KeyProcessResult cancel_res{};
            cancel_res.action = KeyActionType::Cancel;
            ApplyEngineResult(pic, cancel_res);
        }
        engine_.ToggleChineseMode();
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) {
        *pfEaten = TRUE;
        if (engine_.IsComposing() && pic) {
            KeyProcessResult cancel_res{};
            cancel_res.action = KeyActionType::Cancel;
            ApplyEngineResult(pic, cancel_res);
        }
        engine_.ToggleChineseMode();
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

// ITfCompositionSink: called when the host application forcefully terminates composition
// (e.g. user clicked elsewhere, switched Excel cell, or DOM re-rendered)
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) {
    QIQI_LOG_DEBUG("Host terminated active TSF composition");
    composition_.Reset();
    engine_.Reset();
    EndCandidateUI();
    return S_OK;
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context) {
        return;
    }

    ComPtr<ITfContext> ctx_holder(context);
    ComPtr<QiQiTextService> self_holder(this);

    ComPtr<LambdaEditSession> session;
    session.Attach(new LambdaEditSession([self_holder, ctx_holder, result](TfEditCookie ec) -> HRESULT {
        if (result.action == KeyActionType::Cancel) {
            self_holder->CancelCompositionInSession(ec, ctx_holder.Get());
            self_holder->EndCandidateUI();
            return S_OK;
        }

        if (result.action == KeyActionType::Commit) {
            self_holder->CommitAndEndCompositionInSession(ec, ctx_holder.Get(), result.committed_text);
            if (!result.composition_text.empty()) {
                // e.g. 5th key auto-committed previous 4-code word and started a new 1-code composition
                self_holder->StartOrUpdateCompositionInSession(ec, ctx_holder.Get(), result.composition_text);
                self_holder->UpdateCandidateWindowPositionInSession(ec, ctx_holder.Get());
                self_holder->BeginOrUpdateCandidateUI();
            } else {
                self_holder->EndCandidateUI();
            }
            return S_OK;
        }

        if (result.action == KeyActionType::UpdateState) {
            self_holder->StartOrUpdateCompositionInSession(ec, ctx_holder.Get(), result.composition_text);
            self_holder->UpdateCandidateWindowPositionInSession(ec, ctx_holder.Get());
            self_holder->BeginOrUpdateCandidateUI();
            return S_OK;
        }

        return S_OK;
    }));

    HRESULT session_hr = S_OK;
    context->RequestEditSession(
        client_id_,
        session.Get(),
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &session_hr);
}

void QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, ITfContext* context, const std::wstring& text) {
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) || !insert_at_sel) {
            return;
        }
        ComPtr<ITfRange> insert_range;
        if (FAILED(insert_at_sel->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &insert_range)) || !insert_range) {
            return;
        }

        ComPtr<ITfContextComposition> ctx_comp;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&ctx_comp))) || !ctx_comp) {
            return;
        }
        if (FAILED(ctx_comp->StartComposition(
                ec, insert_range.Get(), static_cast<ITfCompositionSink*>(this), &composition_)) ||
            !composition_) {
            return;
        }
    }

    ComPtr<ITfRange> comp_range;
    if (FAILED(composition_->GetRange(&comp_range)) || !comp_range) {
        return;
    }
    comp_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size()));

    // Move caret to the end of the composition range
    ComPtr<ITfRange> caret_range;
    if (SUCCEEDED(comp_range->Clone(&caret_range)) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel{};
        sel.range = caret_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        context->SetSelection(ec, 1, &sel);
    }
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, ITfContext* context, const std::wstring& committed_text) {
    if (composition_) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
            comp_range->SetText(ec, 0, committed_text.c_str(), static_cast<LONG>(committed_text.size()));

            ComPtr<ITfRange> caret_range;
            if (SUCCEEDED(comp_range->Clone(&caret_range)) && caret_range) {
                caret_range->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION sel{};
                sel.range = caret_range.Get();
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                context->SetSelection(ec, 1, &sel);
            }
        }
        composition_->EndComposition(ec);
        composition_.Reset();
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) && insert_at_sel) {
            ComPtr<ITfRange> inserted_range;
            insert_at_sel->InsertTextAtSelection(
                ec, 0, committed_text.c_str(), static_cast<LONG>(committed_text.size()), &inserted_range);
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, ITfContext* /*context*/) {
    if (!composition_) {
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
        comp_range->SetText(ec, 0, L"", 0);
    }
    composition_->EndComposition(ec);
    composition_.Reset();
}

void QiQiTextService::UpdateCandidateWindowPositionInSession(TfEditCookie ec, ITfContext* context) {
    if (!composition_ || !candidate_ui_) {
        return;
    }
    ComPtr<ITfContextView> view;
    if (FAILED(context->GetActiveView(&view)) || !view) {
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (FAILED(composition_->GetRange(&comp_range)) || !comp_range) {
        return;
    }
    RECT rc{};
    BOOL clipped = FALSE;
    if (SUCCEEDED(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped))) {
        candidate_ui_->MoveWindowTo(rc);
    }
}

void QiQiTextService::BeginOrUpdateCandidateUI() {
    if (!candidate_ui_ || !thread_mgr_) {
        return;
    }

    candidate_ui_->SetCandidates(
        engine_.RawCode(),
        engine_.AllCandidates(),
        engine_.PageSize(),
        engine_.CurrentPage(),
        engine_.SelectedInPage());

    ComPtr<ITfUIElementMgr> ui_mgr;
    if (FAILED(thread_mgr_.As(&ui_mgr)) || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        candidate_ui_->Show(TRUE);
        return;
    }

    if (candidate_ui_->UiElementId() == TF_INVALID_UIELEMENTID) {
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        if (SUCCEEDED(ui_mgr->BeginUIElement(candidate_ui_.Get(), &show_own_ui, &element_id))) {
            candidate_ui_->SetUiElementId(element_id);
            // If host game/app returned FALSE in show_own_ui (UILess mode), do not show popup window
            const bool force_uiless = (activate_flags_ & TF_TMAE_UIELEMENTENABLEDONLY) != 0 && !show_own_ui;
            candidate_ui_->Show((show_own_ui && !force_uiless) ? TRUE : FALSE);
            ui_mgr->UpdateUIElement(element_id);
        } else {
            candidate_ui_->Show(TRUE);
        }
    } else {
        ui_mgr->UpdateUIElement(candidate_ui_->UiElementId());
    }
}

void QiQiTextService::EndCandidateUI() {
    if (!candidate_ui_) {
        return;
    }
    if (candidate_ui_->UiElementId() != TF_INVALID_UIELEMENTID && thread_mgr_) {
        ComPtr<ITfUIElementMgr> ui_mgr;
        if (SUCCEEDED(thread_mgr_.As(&ui_mgr)) && ui_mgr) {
            ui_mgr->EndUIElement(candidate_ui_->UiElementId());
        }
        candidate_ui_->SetUiElementId(TF_INVALID_UIELEMENTID);
    }
    candidate_ui_->Show(FALSE);
}

} // namespace qiqi
