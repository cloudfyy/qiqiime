#include "ime/tsf/tip_service.h"
#include "ime/comp/edit.h"
#include "ime/comp/finish.h"
#include "ime/comp/layout.h"
#include "ime/comp/presentation.h"
#include "ime/comp/start.h"
#include "ime/comp/target.h"
#include "ime/comp/update.h"
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
    if (composition_lifecycle_.Cleanups().HandleTermination(ecWrite, pComposition, current_lifecycle_activity_)) {
        return S_OK;
    }
    if (pComposition && pComposition != composition_lifecycle_.Composition().Get()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=host-termination-ignored, activation={}, context={})",
            service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context);
        return S_OK;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=host-terminated-current, activation={}, context={}, has_composition={})",
        service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context, composition_lifecycle_.Composition() != nullptr);
    if (pComposition && composition_lifecycle_.Context()) {
        const EditSessionTicket ticket{composition_lifecycle_.Epoch(), composition_lifecycle_.Context().Get()};
        const ComPtr<ITfContext> context_holder = composition_lifecycle_.Context();
        const ComPtr<ITfComposition> composition_holder = composition_lifecycle_.TakeComposition();
        const auto terminated = ClearTerminatedCompositionInSession(
            ecWrite, context_holder.Get(), composition_holder.Get(),
            [this, ticket]() noexcept {
                return IsCompositionEditCurrent(ticket, nullptr);
            });
        if (!terminated.may_reset) {
            return S_OK;
        }
    }
    ResetCompositionAndUI();
    return S_OK;
}

void QiQiTextService::ScheduleCompositionCleanup() {
    if (!composition_lifecycle_.Composition()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=cleanup-not-needed, activation={}, context={})",
            service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context);
        return;
    }
    composition_lifecycle_.Cleanups().Schedule(
        composition_lifecycle_.Context(), composition_lifecycle_.TakeComposition(), client_id_, composition_lifecycle_.Epoch(), current_lifecycle_activity_);
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
        service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context,
        gsl::narrow_cast<int>(lcode), composition_lifecycle_.IsActive(), pic && pic == composition_lifecycle_.Context().Get(),
        composition_lifecycle_.Composition() != nullptr, candidate_ui_ != nullptr);
    if (lcode != TF_LC_CHANGE || !pic || !composition_lifecycle_.IsActive() || pic != composition_lifecycle_.Context().Get() ||
        !composition_lifecycle_.Composition() || !candidate_ui_) {
        return S_OK;
    }
    ComPtr<ITfContext> ctx_holder(pic);
    const ComPtr<ITfContextView> view_holder(pView);
    ComPtr<QiQiTextService> self_holder(this);
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), ctx_holder.Get()};
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, task = CompositionLayoutTask(ctx_holder, view_holder, ticket, layout_act_id)](TfEditCookie ec) -> HRESULT {
        return ExecuteCompositionLayoutInSession(
            ec, task, self_holder->current_lifecycle_activity_, self_holder->service_log_id_,
            [&self_holder](EditSessionTicket expected) noexcept {
                return self_holder->IsEditSessionCurrent(expected);
            },
            [&self_holder](TfEditCookie cookie, gsl::not_null<ITfContext*> context, const ComPtr<ITfContextView>& view) {
                self_holder->UpdateCandidateWindowPositionInSession(cookie, context, view);
            });
    });
    RequestCompositionLayoutSession(
        gsl::not_null<ITfContext*>(pic), gsl::not_null<ITfEditSession*>(session.Get()),
        client_id_, ticket, service_log_id_, layout_act_id);
    return S_OK;
}

void QiQiTextService::ResetCompositionAndUI(bool try_host_cleanup) {
    if (!preparing_input_context_) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("ResetCompositionAndUI", 0, composition_lifecycle_.Context().Get(), false, "context-reset-cancelled");
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
        service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context,
        composition_lifecycle_.Epoch().context + 1, composition_lifecycle_.Context() != nullptr, composition_lifecycle_.Composition() != nullptr, try_host_cleanup);
    const CompositionContextResetScope reset_scope(composition_lifecycle_);
    if (!reset_scope.Entered()) return;
    if (try_host_cleanup) {
        ScheduleCompositionCleanup();
    } else if (composition_lifecycle_.Composition()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=cleanup-skipped-removed-context)",
            service_log_id_);
    }
    composition_lifecycle_.ClearContext();
    composition_lifecycle_.ClearDocument();
    has_valid_caret_pos_ = false;
    composition_lifecycle_.ClearComposition();
    text_layout_sink_.Reset();
    EndCandidateUI();
}

bool QiQiTextService::IsEditSessionCurrent(EditSessionTicket ticket) const noexcept {
    if (composition_lifecycle_.IsCurrent(ticket)) {
        return true;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=stale-edit-skipped, activation={}/{}, context={}/{}, active={}, target_matches={})",
        service_log_id_, ticket.epoch.activation, composition_lifecycle_.Epoch().activation,
        ticket.epoch.context, composition_lifecycle_.Epoch().context, composition_lifecycle_.IsActive(), ticket.target == composition_lifecycle_.Context().Get());
    return false;
}

bool QiQiTextService::PrepareInputContext(ITfContext* context) {
    if (!context || !composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() || preparing_input_context_) {
        QIQI_LOG_ACT_INFO(
            log::Level::trace,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=input-target-unavailable)",
            service_log_id_);
        return false;
    }
    if (composition_lifecycle_.Context().Get() == context) return true;
    const GUID prepare_act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, prepare_act_id);
    const auto restore_act = gsl::finally([this, prev_act]() noexcept {
        current_lifecycle_activity_ = prev_act;
    });
    return PrepareCompositionTarget(
        gsl::not_null<ITfContext*>(context), composition_lifecycle_, preparing_input_context_,
        text_layout_sink_, gsl::not_null<ITfTextLayoutSink*>(this), service_log_id_, prepare_act_id,
        [this] { ResetCompositionAndUI(); },
        [this](EditSessionTicket ticket) noexcept { return IsEditSessionCurrent(ticket); });
}

bool QiQiTextService::IsCompositionEditCurrent(
    EditSessionTicket ticket, const ITfComposition* composition) const noexcept {
    if (!IsEditSessionCurrent(ticket)) return false;
    if (composition_lifecycle_.Composition().Get() == composition) return true;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=replaced-composition-edit-skipped, activation={}, context={})",
        service_log_id_, ticket.epoch.activation, ticket.epoch.context);
    return false;
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context || !composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() || preparing_input_context_ ||
        context != composition_lifecycle_.Context().Get()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=unprepared-edit-skipped)",
            service_log_id_);
        return;
    }
    ComPtr<ITfContext> ctx_holder(context);
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), ctx_holder.Get()};
    ComPtr<QiQiTextService> self_holder(this);
    const GUID edit_act_id = log::CreateActivityId();

    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, task = CompositionEditTask(ctx_holder, result, ticket, edit_act_id)](TfEditCookie ec) -> HRESULT {
            return self_holder->ApplyCompositionEditInSession(ec, task);
    });

    RequestCompositionEditSession(
        gsl::not_null<ITfContext*>(context), gsl::not_null<ITfEditSession*>(session.Get()),
        client_id_, composition_lifecycle_.Cleanups(), current_lifecycle_activity_, edit_act_id);
}

HRESULT QiQiTextService::ApplyCompositionEditInSession(TfEditCookie ec, const CompositionEditTask& task) {
    const auto& ctx_holder = task.Context();
    const auto& result = task.Snapshot();
    const auto ticket = task.Ticket();
    const auto& edit_act_id = task.Activity();
    const CompositionEditScope scope(task, current_lifecycle_activity_);
    const auto disposition = scope.Check(composition_lifecycle_.Cleanups(), [this](EditSessionTicket expected) noexcept {
        return IsEditSessionCurrent(expected);
    });
    if (disposition == CompositionEditDisposition::Discard) {
        return S_OK;
    }
    if (disposition == CompositionEditDisposition::Requeue) {
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
    return ApplyCompositionEditActionInSession(
        ec, non_null_ctx, result,
        [this, ticket]() noexcept { return IsCompositionEditCurrent(ticket, nullptr); },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> context) {
            CancelCompositionInSession(cookie, context);
        },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> context, const std::wstring& text) {
            CommitAndEndCompositionInSession(cookie, context, text);
        },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
            SyncCompositionUIInSession(cookie, context, snapshot);
        },
        [this] { EndCandidateUI(); });
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const auto composition = StartOrUpdateCompositionInSession(ec, context, snapshot.composition_text);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    UpdateCandidateWindowPositionInSession(ec, context);
    if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
    BeginOrUpdateCandidateUI(snapshot);
}

ComPtr<ITfComposition> QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const TfClientId client_id = client_id_;
    if (!composition_lifecycle_.Composition()) {
        const auto attempt = BeginCompositionInSession(
            ec, context, this, composition_lifecycle_.Cleanups(), client_id, ticket.epoch,
            composition_lifecycle_.Epoch(), current_lifecycle_activity_,
            [this, ticket]() noexcept {
                return IsCompositionEditCurrent(ticket, nullptr);
            });
        if (attempt.disposition != CompositionStartDisposition::Publish) {
            if (attempt.disposition == CompositionStartDisposition::Terminated && IsCompositionEditCurrent(ticket, nullptr)) {
                ResetCompositionAndUI();
            }
            return {};
        }
        composition_lifecycle_.SetComposition(std::move(attempt.starting->composition));
    }

    return UpdateCompositionInSession(
        ec, context, composition_lifecycle_.Composition(), text, display_attribute_atom_,
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        });
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const auto composition = composition_lifecycle_.Composition();
    const auto is_current = [this, ticket, &composition]() noexcept {
        return IsCompositionEditCurrent(ticket, composition.Get());
    };
    if (composition) {
        FinishCompositionInSession(
            ec, context, composition.Get(), CompositionFinishKind::Commit, committed_text, is_current,
            [this]() noexcept {
                composition_lifecycle_.ClearComposition();
                has_valid_caret_pos_ = false;
            });
    } else {
        InsertCommittedTextInSession(ec, context, committed_text, is_current);
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const auto composition = composition_lifecycle_.Composition();
    if (!composition) {
        has_valid_caret_pos_ = false;
        return;
    }
    FinishCompositionInSession(
        ec, context, composition.Get(), CompositionFinishKind::Cancel, {},
        [this, ticket, &composition]() noexcept {
            return IsCompositionEditCurrent(ticket, composition.Get());
        },
        [this]() noexcept {
            composition_lifecycle_.ClearComposition();
            has_valid_caret_pos_ = false;
        });
}

void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, ComPtr<ITfContextView> view) {
    if (!composition_lifecycle_.Composition() || !candidate_ui_) {
        return;
    }
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const auto composition = composition_lifecycle_.Composition();
    const auto query = QueryCompositionLayoutInSession(
        ec, context, gsl::not_null<ITfComposition*>(composition.Get()), view, has_valid_caret_pos_,
        ticket.epoch, service_log_id_, current_lifecycle_activity_,
        [this, ticket, &composition]() noexcept { return IsCompositionEditCurrent(ticket, composition.Get()); });
    if (!query.current) return;
    RECT rc = query.rect;
    if (query.got_position) {
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
    if (!candidate_ui_ || !thread_mgr_ || !composition_lifecycle_.Context() || !composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext()) {
        return;
    }
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), composition_lifecycle_.Context().Get()};
    const auto context = composition_lifecycle_.Context();

    PresentCompositionCandidates(
        *candidate_ui_.Get(), activate_flags_, snapshot, ticket.epoch, service_log_id_, current_lifecycle_activity_,
        [this]() noexcept { return thread_mgr_.Get(); },
        [this, ticket]() noexcept { return IsEditSessionCurrent(ticket); });
}

void QiQiTextService::EndCandidateUI() {
    if (!candidate_ui_) {
        return;
    }
    EndCompositionCandidatePresentation(*candidate_ui_.Get(), thread_mgr_, service_log_id_, current_lifecycle_activity_);
}

} // namespace qiqi
