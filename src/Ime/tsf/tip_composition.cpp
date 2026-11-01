#include "Ime/tsf/tip_service.h"
#include "Ime/tsf/edit_session.h"
#include "common/logger.h"

namespace qiqi {

namespace {

bool QueryGuiThreadOrCursorFallbackRect(RECT& out_rc) noexcept {
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti)) {
        if (gti.hwndCaret && (gti.rcCaret.right > gti.rcCaret.left || gti.rcCaret.bottom > gti.rcCaret.top)) {
            POINT top_left{gti.rcCaret.left, gti.rcCaret.top};
            POINT bottom_right{gti.rcCaret.right, gti.rcCaret.bottom};
            if (::ClientToScreen(gti.hwndCaret, &top_left) && ::ClientToScreen(gti.hwndCaret, &bottom_right)) {
                out_rc = RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
                return true;
            }
        }
        if (gti.hwndFocus) {
            RECT focus_rc{};
            if (::GetWindowRect(gti.hwndFocus, &focus_rc)) {
                out_rc = RECT{focus_rc.left + 20, focus_rc.top + 24, focus_rc.left + 22, focus_rc.top + 44};
                return true;
            }
        }
    }
    POINT cursor_pt{};
    if (::GetCursorPos(&cursor_pt)) {
        out_rc = RECT{cursor_pt.x, cursor_pt.y, cursor_pt.x + 2, cursor_pt.y + 20};
        return true;
    }
    return false;
}

} // namespace

// ITfCompositionSink: called when the host application forcefully terminates composition
// (e.g. user clicked elsewhere, switched Excel cell, or DOM re-rendered)
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) {
    QIQI_LOG_DEBUG("Host terminated active TSF composition");
    ResetCompositionAndUI();
    return S_OK;
}

// ITfTextLayoutSink: called when an async host (Chromium/VS Code/WPF/Word) updates its text layout
STDMETHODIMP QiQiTextService::OnLayoutChange(
    ITfContext* pic, TfLayoutCode lcode, ITfContextView* /*pView*/) {
    if (lcode != TF_LC_CHANGE || !pic || !composition_ || !candidate_ui_) {
        return S_OK;
    }
    ComPtr<ITfContext> ctx_holder(pic);
    ComPtr<QiQiTextService> self_holder(this);
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder](TfEditCookie ec) -> HRESULT {
        self_holder->UpdateCandidateWindowPositionInSession(
            ec, gsl::not_null<ITfContext*>(ctx_holder.Get()));
        return S_OK;
    });
    HRESULT session_hr = S_OK;
    std::ignore = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
    return S_OK;
}

void QiQiTextService::ResetCompositionAndUI() {
    shift_tap_candidate_ = false;
    shift_press_tick_ = 0;
    has_valid_caret_pos_ = false;
    text_layout_sink_.Reset();
    composition_.Reset();
    active_context_.Reset();
    engine_.Reset();
    EndCandidateUI();
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context) {
        return;
    }
    if (active_context_.Get() != context) {
        text_layout_sink_.Reset();
        active_context_ = context;
        ITfTextLayoutSink* sink = this;
        text_layout_sink_.Advise(active_context_.Get(), IID_ITfTextLayoutSink, sink);
    }

    ComPtr<ITfContext> ctx_holder(context);
    ComPtr<QiQiTextService> self_holder(this);

    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, result](TfEditCookie ec) -> HRESULT {
        const gsl::not_null<ITfContext*> non_null_ctx(ctx_holder.Get());
        if (result.action == KeyActionType::Cancel) {
            self_holder->CancelCompositionInSession(ec, non_null_ctx.get());
            self_holder->EndCandidateUI();
            return S_OK;
        }

        if (result.action == KeyActionType::Commit) {
            self_holder->CommitAndEndCompositionInSession(ec, non_null_ctx, result.committed_text);
            if (!result.composition_text.empty()) {
                // e.g. 5th key auto-committed previous 4-code word and started a new 1-code composition
                self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result);
            } else {
                self_holder->EndCandidateUI();
            }
            return S_OK;
        }

        if (result.action == KeyActionType::UpdateState) {
            self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result);
            return S_OK;
        }

        return S_OK;
    });

    HRESULT session_hr = S_OK;
    const HRESULT req_hr = context->RequestEditSession(
        client_id_,
        session.Get(),
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &session_hr);
    if (FAILED(req_hr) || FAILED(session_hr)) {
        QIQI_LOG_WARN("RequestEditSession failed (req_hr=0x{:08x}, session_hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(req_hr),
                      gsl::narrow_cast<unsigned int>(session_hr));
    }
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    StartOrUpdateCompositionInSession(ec, context, snapshot.composition_text);
    UpdateCandidateWindowPositionInSession(ec, context);
    BeginOrUpdateCandidateUI(snapshot);
}

void QiQiTextService::SetRangeTextAndCollapseCaret(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text) {
    range->SetText(ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size()));

    ComPtr<ITfRange> caret_range;
    if (SUCCEEDED(range->Clone(&caret_range)) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel{};
        sel.range = caret_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        context->SetSelection(ec, 1, &sel);
    }
}

void QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) || !insert_at_sel) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: QueryInterface(ITfInsertAtSelection) failed");
            return;
        }
        ComPtr<ITfRange> insert_range;
        if (FAILED(insert_at_sel->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &insert_range)) || !insert_range) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: InsertTextAtSelection(TF_IAS_QUERYONLY) failed");
            return;
        }

        ComPtr<ITfContextComposition> ctx_comp;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&ctx_comp))) || !ctx_comp) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: QueryInterface(ITfContextComposition) failed");
            return;
        }
        if (FAILED(ctx_comp->StartComposition(
                ec, insert_range.Get(), static_cast<ITfCompositionSink*>(this), &composition_)) ||
            !composition_) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: StartComposition failed");
            return;
        }
    }

    ComPtr<ITfRange> comp_range;
    if (FAILED(composition_->GetRange(&comp_range)) || !comp_range) {
        QIQI_LOG_WARN("StartOrUpdateCompositionInSession: composition_->GetRange failed");
        return;
    }
    SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), text);

    // Apply dotted underline display attribute to the composition range
    if (display_attribute_atom_ != TF_INVALID_GUIDATOM) {
        ComPtr<ITfProperty> disp_prop;
        if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop)) && disp_prop) {
            const VARIANT var = MakeInt4Variant(gsl::narrow_cast<LONG>(display_attribute_atom_));
            disp_prop->SetValue(ec, comp_range.Get(), &var);
        }
    }
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    if (composition_) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
            ComPtr<ITfProperty> disp_prop;
            if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop)) && disp_prop) {
                disp_prop->Clear(ec, comp_range.Get());
            }
            SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), committed_text);
        }
        composition_->EndComposition(ec);
        composition_.Reset();
        has_valid_caret_pos_ = false;
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) && insert_at_sel) {
            ComPtr<ITfRange> inserted_range;
            if (FAILED(insert_at_sel->InsertTextAtSelection(
                    ec, 0, committed_text.c_str(), gsl::narrow_cast<LONG>(committed_text.size()), &inserted_range))) {
                QIQI_LOG_WARN("CommitAndEndCompositionInSession: InsertTextAtSelection failed");
            }
        } else {
            QIQI_LOG_WARN("CommitAndEndCompositionInSession: QueryInterface(ITfInsertAtSelection) failed");
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, ITfContext* /*context*/) {
    has_valid_caret_pos_ = false;
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

void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    if (!composition_ || !candidate_ui_) {
        return;
    }
    ComPtr<ITfContextView> view;
    ComPtr<ITfRange> comp_range;
    RECT rc{};
    BOOL clipped = FALSE;
    if (SUCCEEDED(context->GetActiveView(&view)) && view &&
        SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range &&
        SUCCEEDED(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped))) {
        has_valid_caret_pos_ = true;
        candidate_ui_->MoveWindowTo(rc);
        return;
    }
    if (!has_valid_caret_pos_ && QueryGuiThreadOrCursorFallbackRect(rc)) {
        QIQI_LOG_DEBUG("UpdateCandidateWindowPositionInSession: GetTextExt pending layout; using GUI/cursor fallback");
        candidate_ui_->MoveWindowTo(rc);
    }
}

void QiQiTextService::BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot) {
    if (!candidate_ui_ || !thread_mgr_) {
        return;
    }

    candidate_ui_->SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index);

    ComPtr<ITfUIElementMgr> ui_mgr;
    if (FAILED(thread_mgr_.As(&ui_mgr)) || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; showing popup directly");
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
            const bool show_popup = (show_own_ui && !force_uiless);
            QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={})",
                           element_id, show_own_ui != FALSE, show_popup);
            candidate_ui_->Show(show_popup ? TRUE : FALSE);
            ui_mgr->UpdateUIElement(element_id);
        } else {
            QIQI_LOG_WARN("BeginOrUpdateCandidateUI: BeginUIElement failed; falling back to popup window");
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
