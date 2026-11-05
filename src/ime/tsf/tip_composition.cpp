#include "ime/tsf/tip_service.h"
#include "ime/tsf/composition_attribute.h"
#include "ime/tsf/edit_session.h"
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
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) {
    if (composition_cleanups_.HandleTermination(ecWrite, pComposition, current_lifecycle_activity_)) {
        return S_OK;
    }
    if (pComposition && pComposition != composition_.Get()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=host-termination-ignored, activation={}, context={})",
            service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context);
        return S_OK;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=host-terminated-current, activation={}, context={}, has_composition={})",
        service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context, composition_ != nullptr);
    if (pComposition && active_context_) {
        const EditSessionTicket ticket{edit_session_epoch_, active_context_.Get()};
        const ComPtr<ITfContext> context_holder = active_context_;
        const ComPtr<ITfComposition> composition_holder = std::move(composition_);
        ComPtr<ITfRange> range;
        const HRESULT range_hr = composition_holder->GetRange(&range);
        if (log::CheckHResult(range_hr, "HostTermination::GetRange")) {
            if (range) {
                std::ignore = ClearCompositionDisplayAttribute(
                    ecWrite, gsl::not_null<ITfContext*>(context_holder.Get()), gsl::not_null<ITfRange*>(range.Get()),
                    [this, ticket]() noexcept {
                        return IsCompositionEditCurrent(ticket, nullptr);
                    });
            } else {
                QIQI_LOG_CAT_WARN(
                    log::Keyword::Composition,
                    "HostTermination::GetRange returned no range (hr=0x{:08x})",
                    gsl::narrow_cast<unsigned int>(range_hr));
            }
        }
        if (!IsCompositionEditCurrent(ticket, nullptr)) {
            return S_OK;
        }
    }
    ResetCompositionAndUI();
    return S_OK;
}

void QiQiTextService::ScheduleCompositionCleanup() {
    if (!composition_) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=cleanup-not-needed, activation={}, context={})",
            service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context);
        return;
    }
    composition_cleanups_.Schedule(
        active_context_, std::move(composition_), client_id_, edit_session_epoch_, current_lifecycle_activity_);
}

// ITfTextLayoutSink: called when an async host (Chromium/VS Code/WPF/Word) updates its text layout
STDMETHODIMP QiQiTextService::OnLayoutChange(
    ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView) {
    const GUID layout_act_id = log::CreateActivityId();
    QIQI_LOG_ACT_START(
        log::Level::debug,
        log::Keyword::UI,
        &layout_act_id,
        &current_lifecycle_activity_,
        "CandidatePosition(service={}, stage=layout-notification, activation={}, context={}, code={}, active={}, target_matches={}, has_composition={}, has_ui={})",
        service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context,
        gsl::narrow_cast<int>(lcode), is_active_, pic && pic == active_context_.Get(),
        composition_ != nullptr, candidate_ui_ != nullptr);
    if (lcode != TF_LC_CHANGE || !pic || !is_active_ || pic != active_context_.Get() ||
        !composition_ || !candidate_ui_) {
        return S_OK;
    }
    ComPtr<ITfContext> ctx_holder(pic);
    const ComPtr<ITfContextView> view_holder(pView);
    ComPtr<QiQiTextService> self_holder(this);
    const EditSessionTicket ticket{edit_session_epoch_, ctx_holder.Get()};
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, view_holder, ticket, layout_act_id](TfEditCookie ec) -> HRESULT {
        const GUID prev_act = std::exchange(self_holder->current_lifecycle_activity_, layout_act_id);
        const auto restore_act = gsl::finally([&self_holder, prev_act]() noexcept {
            self_holder->current_lifecycle_activity_ = prev_act;
        });
        if (!self_holder->IsEditSessionCurrent(ticket)) {
            return S_OK;
        }
        QIQI_LOG_ACT_STOP(
            log::Level::debug,
            log::Keyword::UI,
            &layout_act_id,
            "CandidatePosition(service={}, stage=layout-callback, activation={}, context={})",
            self_holder->service_log_id_, ticket.epoch.activation, ticket.epoch.context);
        self_holder->UpdateCandidateWindowPositionInSession(
            ec, gsl::not_null<ITfContext*>(ctx_holder.Get()), view_holder);
        return S_OK;
    });
    HRESULT session_hr = S_OK;
    const HRESULT request_hr = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &layout_act_id,
        "CandidatePosition(service={}, stage=layout-request, activation={}, context={}, request_hr=0x{:08x}, session_hr=0x{:08x})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context,
        gsl::narrow_cast<unsigned int>(request_hr), gsl::narrow_cast<unsigned int>(session_hr));
    const log::LogContext layout_ctx{log::Keyword::UI, log::Opcode::Info, &layout_act_id, nullptr};
    std::ignore = log::CheckHResult(request_hr, "OnLayoutChange::RequestEditSession", log::Level::debug, layout_ctx);
    if (SUCCEEDED(request_hr)) {
        std::ignore = log::CheckHResult(session_hr, "OnLayoutChange::DoEditSession", log::Level::debug, layout_ctx);
    }
    return S_OK;
}

void QiQiTextService::ResetCompositionAndUI(bool try_host_cleanup) {
    if (!preparing_input_context_) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("ResetCompositionAndUI", 0, active_context_.Get(), false, "context-reset-cancelled");
        }
        shift_tap_.Reset();
    }
    engine_.Reset();
    ResetContextAndUI(try_host_cleanup);
}

void QiQiTextService::ResetContextAndUI(bool try_host_cleanup) {
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=context-reset, activation={}, context_before={}, context_after={}, has_context={}, has_composition={}, try_host_cleanup={})",
        service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context,
        edit_session_epoch_.context + 1, active_context_ != nullptr, composition_ != nullptr, try_host_cleanup);
    // Invalidate before COM cleanup can deliver reentrant callbacks.
    ++edit_session_epoch_.context;
    if (resetting_context_) return;
    resetting_context_ = true;
    const auto finish_reset = gsl::finally([this]() noexcept { resetting_context_ = false; });
    if (try_host_cleanup) {
        ScheduleCompositionCleanup();
    } else if (composition_) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=cleanup-skipped-removed-context)",
            service_log_id_);
    }
    active_context_.Reset();
    active_document_.Reset();
    has_valid_caret_pos_ = false;
    composition_.Reset();
    text_layout_sink_.Reset();
    EndCandidateUI();
}

bool QiQiTextService::IsEditSessionCurrent(EditSessionTicket ticket) const noexcept {
    if (is_active_ && !resetting_context_ && ticket.Matches(edit_session_epoch_, active_context_.Get())) {
        return true;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=stale-edit-skipped, activation={}/{}, context={}/{}, active={}, target_matches={})",
        service_log_id_, ticket.epoch.activation, edit_session_epoch_.activation,
        ticket.epoch.context, edit_session_epoch_.context, is_active_, ticket.target == active_context_.Get());
    return false;
}

bool QiQiTextService::PrepareInputContext(ITfContext* context) {
    if (!context || !is_active_ || resetting_context_ || preparing_input_context_) {
        QIQI_LOG_ACT_INFO(
            log::Level::trace,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=input-target-unavailable)",
            service_log_id_);
        return false;
    }
    if (active_context_.Get() == context) return true;
    const GUID prepare_act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, prepare_act_id);
    const auto restore_act = gsl::finally([this, prev_act]() noexcept {
        current_lifecycle_activity_ = prev_act;
    });
    ComPtr<ITfContext> ctx_holder(context);
    preparing_input_context_ = ctx_holder;
    const auto finish_prepare = gsl::finally([this]() noexcept { preparing_input_context_.Reset(); });
    EditSessionTicket ticket{edit_session_epoch_, nullptr};
    ++ticket.epoch.context;
    ResetCompositionAndUI();
    if (!IsEditSessionCurrent(ticket)) return false;
    ComPtr<ITfDocumentMgr> document;
    const HRESULT document_hr = context->GetDocumentMgr(&document);
    if (!log::CheckHResult(document_hr, "PrepareInputContext::GetDocumentMgr", log::Level::warn,
                           log::LogContext{log::Keyword::Lifecycle, log::Opcode::Info, &prepare_act_id, nullptr}) ||
        !document) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &prepare_act_id,
            "TsfLifecycle(service={}, stage=input-document-unavailable, hr=0x{:08x})",
            service_log_id_, gsl::narrow_cast<unsigned int>(document_hr));
    }
    if (!IsEditSessionCurrent(ticket)) return false;
    active_context_ = ctx_holder;
    active_document_ = std::move(document);
    ticket.target = context;
    ITfTextLayoutSink* sink = this;
    const bool layout_advised = text_layout_sink_.Advise(active_context_.Get(), IID_ITfTextLayoutSink, sink);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &prepare_act_id,
        "CandidatePosition(service={}, stage=layout-advise, activation={}, context={}, connected={})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context, layout_advised);
    if (!IsEditSessionCurrent(ticket)) {
        // A notification during AdviseSink may have invalidated the target before the cookie was published.
        text_layout_sink_.Reset();
        return false;
    }
    return true;
}

bool QiQiTextService::IsCompositionEditCurrent(
    EditSessionTicket ticket, const ITfComposition* composition) const noexcept {
    if (!IsEditSessionCurrent(ticket)) return false;
    if (composition_.Get() == composition) return true;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=replaced-composition-edit-skipped, activation={}, context={})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context);
    return false;
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context || !is_active_ || resetting_context_ || preparing_input_context_ ||
        context != active_context_.Get()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=unprepared-edit-skipped)",
            service_log_id_);
        return;
    }
    ComPtr<ITfContext> ctx_holder(context);
    const EditSessionTicket ticket{edit_session_epoch_, ctx_holder.Get()};
    ComPtr<QiQiTextService> self_holder(this);
    const GUID edit_act_id = log::CreateActivityId();

    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, task = CompositionEditTask(ctx_holder, result, ticket, edit_act_id)](TfEditCookie ec) -> HRESULT {
            return self_holder->ApplyCompositionEditInSession(ec, task);
    });

    HRESULT session_hr = S_OK;
    const HRESULT req_hr = context->RequestEditSession(
        client_id_,
        session.Get(),
        (composition_cleanups_.HasPending(context, current_lifecycle_activity_) ? TF_ES_ASYNC : TF_ES_ASYNCDONTCARE) |
            TF_ES_READWRITE,
        &session_hr);
    if (FAILED(req_hr) || FAILED(session_hr)) {
        QIQI_LOG_ACT_STOP(
            log::Level::warn,
            log::Keyword::Composition,
            &edit_act_id,
            "RequestEditSession failed (req_hr=0x{:08x}, session_hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(req_hr),
            gsl::narrow_cast<unsigned int>(session_hr));
    }
}

HRESULT QiQiTextService::ApplyCompositionEditInSession(TfEditCookie ec, const CompositionEditTask& task) {
    const auto& ctx_holder = task.Context();
    const auto& result = task.Snapshot();
    const auto ticket = task.Ticket();
    const auto& edit_act_id = task.Activity();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, edit_act_id);
    const auto restore_act = gsl::finally([this, prev_act]() noexcept {
        current_lifecycle_activity_ = prev_act;
    });
    if (!IsEditSessionCurrent(ticket)) {
        return S_OK;
    }
    if (composition_cleanups_.HasPending(ctx_holder.Get(), current_lifecycle_activity_)) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &edit_act_id,
            "TsfLifecycle(service={}, stage=write-requeued-behind-cleanup, activation={}, context={})",
            service_log_id_, ticket.epoch.activation, ticket.epoch.context);
        ApplyEngineResult(ctx_holder.Get(), result);
        return S_OK;
    }
    const gsl::not_null<ITfContext*> non_null_ctx(ctx_holder.Get());
    if (result.action == KeyActionType::Cancel) {
        CancelCompositionInSession(ec, non_null_ctx.get());
        if (IsCompositionEditCurrent(ticket, nullptr)) EndCandidateUI();
        return S_OK;
    }

    if (result.action == KeyActionType::Commit) {
        CommitAndEndCompositionInSession(ec, non_null_ctx, result.committed_text);
        if (!IsCompositionEditCurrent(ticket, nullptr)) return S_OK;
        if (!result.composition_text.empty()) {
            // e.g. 5th key auto-committed previous 4-code word and started a new 1-code composition
            SyncCompositionUIInSession(ec, non_null_ctx, result);
        } else {
            EndCandidateUI();
        }
        return S_OK;
    }

    if (result.action == KeyActionType::UpdateState) {
        SyncCompositionUIInSession(ec, non_null_ctx, result);
        return S_OK;
    }

    return S_OK;
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    const EditSessionTicket ticket{edit_session_epoch_, context.get()};
    const auto composition = StartOrUpdateCompositionInSession(ec, context, snapshot.composition_text);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    UpdateCandidateWindowPositionInSession(ec, context);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    BeginOrUpdateCandidateUI(snapshot);
}

void QiQiTextService::SetRangeTextAndCollapseCaret(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text) {
    const EditSessionTicket ticket{edit_session_epoch_, context.get()};
    const auto composition = composition_;
    std::ignore = log::CheckHResult(range->SetText(ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size())),
                                   "SetRangeTextAndCollapseCaret::SetText");
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    ComPtr<ITfRange> caret_range;
    if (log::CheckHResult(range->Clone(&caret_range), "SetRangeTextAndCollapseCaret::Clone") && caret_range) {
        if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
        std::ignore = log::CheckHResult(caret_range->Collapse(ec, TF_ANCHOR_END),
                                       "SetRangeTextAndCollapseCaret::Collapse");
        if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
        TF_SELECTION sel{};
        sel.range = caret_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        std::ignore = log::CheckHResult(context->SetSelection(ec, 1, &sel),
                                       "SetRangeTextAndCollapseCaret::SetSelection");
    }
}

ComPtr<ITfComposition> QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    const EditSessionTicket ticket{edit_session_epoch_, context.get()};
    const TfClientId client_id = client_id_;
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (!log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel)),
                              "StartOrUpdateComposition::QueryInterface(ITfInsertAtSelection)") || !insert_at_sel) {
            return {};
        }
        if (!IsCompositionEditCurrent(ticket, nullptr)) return {};
        ComPtr<ITfRange> insert_range;
        if (!log::CheckHResult(insert_at_sel->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &insert_range),
                "StartOrUpdateComposition::InsertTextAtSelection(TF_IAS_QUERYONLY)") || !insert_range) {
            return {};
        }
        if (!IsCompositionEditCurrent(ticket, nullptr)) return {};

        ComPtr<ITfContextComposition> ctx_comp;
        if (!log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&ctx_comp)),
                              "StartOrUpdateComposition::QueryInterface(ITfContextComposition)") || !ctx_comp) {
            return {};
        }
        if (!IsCompositionEditCurrent(ticket, nullptr)) return {};
        // Track the unpublished result so removal/termination during StartComposition is remembered.
        const auto starting = composition_cleanups_.TrackStarting(context);
        const HRESULT start_hr = ctx_comp->StartComposition(ec, insert_range.Get(), this, &starting->composition);
        const bool start_ok = log::CheckHResult(start_hr, "StartOrUpdateComposition::StartComposition");
        starting->finished = true;
        if (!IsCompositionEditCurrent(ticket, nullptr) ||
            starting->context_removed || starting->terminated) {
            QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=stale-composition-start, activation={}, context={}, "
                           "context_removed={}, terminated={})",
                           service_log_id_, ticket.epoch.activation, ticket.epoch.context,
                           starting->context_removed, starting->terminated);
            if (starting->composition && !starting->context_removed && !starting->terminated) {
                composition_cleanups_.Schedule(
                    starting->context, std::move(starting->composition), client_id,
                    edit_session_epoch_, current_lifecycle_activity_);
            }
            if (starting->terminated && IsCompositionEditCurrent(ticket, nullptr)) {
                ResetCompositionAndUI();
            }
            return {};
        }
        if (!start_ok || !starting->composition) {
            return {};
        }
        composition_ = std::move(starting->composition);
    }

    const auto composition = composition_;
    ComPtr<ITfRange> comp_range;
    if (!log::CheckHResult(composition->GetRange(&comp_range), "StartOrUpdateComposition::GetRange") || !comp_range) {
        return composition;
    }
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return composition;
    SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), text);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return composition;

    // Apply dotted underline display attribute to the composition range
    if (display_attribute_atom_ != TF_INVALID_GUIDATOM) {
        ComPtr<ITfProperty> disp_prop;
        if (log::CheckHResult(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop),
                             "StartOrUpdateComposition::GetProperty(GUID_PROP_ATTRIBUTE)") && disp_prop) {
            if (!IsCompositionEditCurrent(ticket, composition.Get())) return composition;
            const VARIANT var = MakeInt4Variant(gsl::narrow_cast<LONG>(display_attribute_atom_));
            std::ignore = log::CheckHResult(disp_prop->SetValue(ec, comp_range.Get(), &var),
                                           "StartOrUpdateComposition::SetValue(GUID_PROP_ATTRIBUTE)");
        }
    }
    return composition;
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    const EditSessionTicket ticket{edit_session_epoch_, context.get()};
    const auto composition = composition_;
    if (composition) {
        ComPtr<ITfRange> comp_range;
        if (log::CheckHResult(composition->GetRange(&comp_range), "CommitComposition::GetRange") && comp_range) {
            if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
            std::ignore = ClearCompositionDisplayAttribute(
                ec, context, gsl::not_null<ITfRange*>(comp_range.Get()),
                [this, ticket, &composition]() noexcept {
                    return IsCompositionEditCurrent(ticket, composition.Get());
                });
            if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
            SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), committed_text);
        }
        if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
        composition_.Reset();
        has_valid_caret_pos_ = false;
        std::ignore = log::CheckHResult(composition->EndComposition(ec), "CommitComposition::EndComposition");
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel)),
                             "DirectCommit::QueryInterface(ITfInsertAtSelection)") && insert_at_sel) {
            if (!IsCompositionEditCurrent(ticket, nullptr)) return;
            ComPtr<ITfRange> inserted_range;
            std::ignore = log::CheckHResult(insert_at_sel->InsertTextAtSelection(
                    ec, 0, committed_text.c_str(), gsl::narrow_cast<LONG>(committed_text.size()), &inserted_range),
                    "DirectCommit::InsertTextAtSelection");
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, const ITfContext* context) {
    const EditSessionTicket ticket{edit_session_epoch_, context};
    const auto composition = composition_;
    if (!composition) {
        has_valid_caret_pos_ = false;
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (log::CheckHResult(composition->GetRange(&comp_range), "CancelComposition::GetRange") && comp_range) {
        if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
        std::ignore = log::CheckHResult(comp_range->SetText(ec, 0, L"", 0), "CancelComposition::SetText");
    }
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    composition_.Reset();
    has_valid_caret_pos_ = false;
    std::ignore = log::CheckHResult(composition->EndComposition(ec), "CancelComposition::EndComposition");
}

void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, ComPtr<ITfContextView> view) {
    if (!composition_ || !candidate_ui_) {
        return;
    }
    const EditSessionTicket ticket{edit_session_epoch_, context.get()};
    const auto composition = composition_;
    const bool notification_view = view != nullptr;
    ComPtr<ITfRange> comp_range;
    RECT rc{};
    BOOL clipped = FALSE;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &current_lifecycle_activity_,
        "CandidatePosition(service={}, stage=query, activation={}, context={}, had_position={}, notification_view={})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context, has_valid_caret_pos_, notification_view);
    const bool got_position =
        (view || log::CheckHResult(context->GetActiveView(&view), "CandidatePosition::GetActiveView", log::Level::debug)) && view &&
        IsCompositionEditCurrent(ticket, composition.Get()) &&
        log::CheckHResult(composition->GetRange(&comp_range), "CandidatePosition::GetRange", log::Level::debug) && comp_range &&
        IsCompositionEditCurrent(ticket, composition.Get()) &&
        log::CheckHResult(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped),
                         "CandidatePosition::GetTextExt", log::Level::trace);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &current_lifecycle_activity_,
        "CandidatePosition(service={}, stage=query-result, activation={}, context={}, success={}, rect=({}, {}, {}, {}), clipped={}, had_position={})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context, got_position,
        rc.left, rc.top, rc.right, rc.bottom, clipped != FALSE, has_valid_caret_pos_);
    if (got_position) {
        has_valid_caret_pos_ = true;
        candidate_ui_->MoveWindowTo(rc);
        return;
    }
    if (!has_valid_caret_pos_ && QueryGuiThreadOrCursorFallbackRect(rc)) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &current_lifecycle_activity_,
            "UpdateCandidateWindowPositionInSession: GetTextExt pending layout; using GUI/cursor fallback");
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &current_lifecycle_activity_,
            "CandidatePosition(service={}, stage=fallback, activation={}, context={}, rect=({}, {}, {}, {}))",
            service_log_id_, ticket.epoch.activation, ticket.epoch.context, rc.left, rc.top, rc.right, rc.bottom);
        candidate_ui_->MoveWindowTo(rc);
    } else {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &current_lifecycle_activity_,
            "CandidatePosition(service={}, stage=position-unchanged, activation={}, context={}, had_position={})",
            service_log_id_, ticket.epoch.activation, ticket.epoch.context, has_valid_caret_pos_);
    }
}

void QiQiTextService::BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot) {
    if (!candidate_ui_ || !thread_mgr_ || !active_context_ || !is_active_ || resetting_context_) {
        return;
    }
    const EditSessionTicket ticket{edit_session_epoch_, active_context_.Get()};
    const auto context = active_context_;

    candidate_ui_->SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index);
    if (!IsEditSessionCurrent(ticket)) return;

    ComPtr<ITfUIElementMgr> ui_mgr;
    const HRESULT manager_hr = thread_mgr_.As(&ui_mgr);
    const bool manager_ok = log::CheckHResult(
        manager_hr, "BeginOrUpdateCandidateUI::QueryInterface(ITfUIElementMgr)", log::Level::debug);
    if (!IsEditSessionCurrent(ticket)) return;
    if (!manager_ok || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &current_lifecycle_activity_,
            "BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; showing popup directly");
        candidate_ui_->Show(TRUE);
        return;
    }

    if (candidate_ui_->UiElementId() == TF_INVALID_UIELEMENTID) {
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        const HRESULT begin_hr = ui_mgr->BeginUIElement(candidate_ui_.Get(), &show_own_ui, &element_id);
        const bool begin_ok = log::CheckHResult(begin_hr, "BeginOrUpdateCandidateUI::BeginUIElement");
        if (!IsEditSessionCurrent(ticket)) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &current_lifecycle_activity_,
                "TsfLifecycle(service={}, stage=ui-begin-stale, activation={}, context={}, element_id={}, begin_ok={})",
                service_log_id_, ticket.epoch.activation, ticket.epoch.context, element_id, begin_ok);
            if (begin_ok && element_id != TF_INVALID_UIELEMENTID) {
                std::ignore = log::CheckHResult(ui_mgr->EndUIElement(element_id), "StaleCandidateUI::EndUIElement");
            }
            return;
        }
        if (begin_ok) {
            candidate_ui_->SetUiElementId(element_id);
            // If host game/app returned FALSE in show_own_ui (UILess mode), do not show popup window
            const bool force_uiless = (activate_flags_ & TF_TMAE_UIELEMENTENABLEDONLY) != 0 && !show_own_ui;
            const bool show_popup = (show_own_ui && !force_uiless);
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &current_lifecycle_activity_,
                "BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={})",
                element_id, show_own_ui != FALSE, show_popup);
            candidate_ui_->Show(show_popup ? TRUE : FALSE);
            if (!IsEditSessionCurrent(ticket)) return;
            std::ignore = log::CheckHResult(ui_mgr->UpdateUIElement(element_id), "ITfUIElementMgr::UpdateUIElement");
        } else {
            QIQI_LOG_ACT_INFO(
                log::Level::warn,
                log::Keyword::UI,
                &current_lifecycle_activity_,
                "BeginOrUpdateCandidateUI: BeginUIElement failed; falling back to popup window");
            candidate_ui_->Show(TRUE);
        }
    } else {
        std::ignore = log::CheckHResult(ui_mgr->UpdateUIElement(candidate_ui_->UiElementId()),
                                       "ITfUIElementMgr::UpdateUIElement");
    }
}

void QiQiTextService::EndCandidateUI() {
    if (!candidate_ui_) {
        return;
    }
    const DWORD element_id = candidate_ui_->UiElementId();
    const auto manager = thread_mgr_;
    candidate_ui_->SetUiElementId(TF_INVALID_UIELEMENTID);
    candidate_ui_->Show(FALSE);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=ui-end, element_id={}, has_manager={})",
        service_log_id_, element_id, manager != nullptr);
    if (element_id != TF_INVALID_UIELEMENTID && manager) {
        ComPtr<ITfUIElementMgr> ui_mgr;
        if (log::CheckHResult(manager.As(&ui_mgr), "EndCandidateUI::QueryInterface(ITfUIElementMgr)") && ui_mgr) {
            std::ignore = log::CheckHResult(ui_mgr->EndUIElement(element_id),
                                           "ITfUIElementMgr::EndUIElement");
        }
    }
}

} // namespace qiqi
