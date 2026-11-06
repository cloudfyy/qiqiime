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
    return HandleCompositionTermination(
        ecWrite, pComposition, composition_lifecycle_, service_log_id_, current_lifecycle_activity_,
        [this](EditSessionTicket ticket) noexcept { return IsCompositionEditCurrent(ticket, nullptr); },
        [this] { ResetCompositionAndUI(); });
}

void QiQiTextService::ScheduleCompositionCleanup() {
    ScheduleCurrentCompositionCleanup(composition_lifecycle_, client_id_, service_log_id_, current_lifecycle_activity_);
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
    ResetCompositionContextAndUI(
        composition_lifecycle_, try_host_cleanup, has_valid_caret_pos_, text_layout_sink_,
        service_log_id_, current_lifecycle_activity_,
        [this] { ScheduleCompositionCleanup(); },
        [this] { EndCandidateUI(); });
}

bool QiQiTextService::IsEditSessionCurrent(EditSessionTicket ticket) const noexcept {
    return CheckCompositionEditSessionCurrent(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
}

bool QiQiTextService::PrepareInputContext(ITfContext* context) {
    return EnterCompositionTargetPreparation(
        context, composition_lifecycle_, preparing_input_context_.Get(), current_lifecycle_activity_, service_log_id_,
        [this](gsl::not_null<ITfContext*> target, const GUID& prepare_act_id) {
            return PrepareCompositionTarget(
                target, composition_lifecycle_, preparing_input_context_,
                text_layout_sink_, gsl::not_null<ITfTextLayoutSink*>(this), service_log_id_, prepare_act_id,
                [this] { ResetCompositionAndUI(); },
                [this](EditSessionTicket ticket) noexcept { return IsEditSessionCurrent(ticket); });
        });
}

bool QiQiTextService::IsCompositionEditCurrent(
    EditSessionTicket ticket, const ITfComposition* composition) const noexcept {
    return CheckCompositionEditCurrent(
        composition_lifecycle_, ticket, composition, service_log_id_, current_lifecycle_activity_);
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    EnterCompositionEditRequest(
        context, composition_lifecycle_, preparing_input_context_.Get(), service_log_id_, current_lifecycle_activity_,
        [this, context, &result](const ComPtr<ITfContext>& ctx_holder, EditSessionTicket ticket) {
            ComPtr<QiQiTextService> self_holder(this);
            const GUID edit_act_id = log::CreateActivityId();

            const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
                [self_holder, task = CompositionEditTask(ctx_holder, result, ticket, edit_act_id)](TfEditCookie ec) -> HRESULT {
                    return self_holder->ApplyCompositionEditInSession(ec, task);
            });

            RequestCompositionEditSession(
                gsl::not_null<ITfContext*>(context), gsl::not_null<ITfEditSession*>(session.Get()),
                client_id_, composition_lifecycle_.Cleanups(), current_lifecycle_activity_, edit_act_id);
        });
}

HRESULT QiQiTextService::ApplyCompositionEditInSession(TfEditCookie ec, const CompositionEditTask& task) {
    return ExecuteCompositionEditInSession(
        ec, task, current_lifecycle_activity_, composition_lifecycle_.Cleanups(), service_log_id_,
        [this](EditSessionTicket expected) noexcept { return IsEditSessionCurrent(expected); },
        [this](ITfContext* context, const KeyProcessResult& result) { ApplyEngineResult(context, result); },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> context,
               const KeyProcessResult& result, EditSessionTicket ticket) {
            return ApplyCompositionEditActionInSession(
                cookie, context, result,
                [this, ticket]() noexcept { return IsCompositionEditCurrent(ticket, nullptr); },
                [this](TfEditCookie cancel_cookie, gsl::not_null<ITfContext*> target) {
                    CancelCompositionInSession(cancel_cookie, target);
                },
                [this](TfEditCookie commit_cookie, gsl::not_null<ITfContext*> target, const std::wstring& text) {
                    CommitAndEndCompositionInSession(commit_cookie, target, text);
                },
                [this](TfEditCookie sync_cookie, gsl::not_null<ITfContext*> target, const KeyProcessResult& snapshot) {
                    SyncCompositionUIInSession(sync_cookie, target, snapshot);
                },
                [this] { EndCandidateUI(); });
        });
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    SyncCompositionPresentationInSession(
        ec, context, snapshot,
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> target, const std::wstring& text) {
            return StartOrUpdateCompositionInSession(cookie, target, text);
        },
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> target) {
            UpdateCandidateWindowPositionInSession(cookie, target);
        },
        [this](const KeyProcessResult& result) { BeginOrUpdateCandidateUI(result); });
}

ComPtr<ITfComposition> QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const TfClientId client_id = client_id_;
    return EnsureAndUpdateCompositionInSession(
        ec, context, this, composition_lifecycle_, client_id, ticket.epoch, current_lifecycle_activity_,
        text, display_attribute_atom_,
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        },
        [this] { ResetCompositionAndUI(); });
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    FinishOrInsertCompositionInSession(
        ec, context, composition_lifecycle_, has_valid_caret_pos_, CompositionFinishKind::Commit, committed_text,
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        });
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    FinishOrInsertCompositionInSession(
        ec, context, composition_lifecycle_, has_valid_caret_pos_, CompositionFinishKind::Cancel, {},
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
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
    ApplyCompositionLayoutPosition(
        query, has_valid_caret_pos_, ticket.epoch, service_log_id_, current_lifecycle_activity_,
        [](RECT& rect) noexcept { return QueryGuiThreadOrCursorFallbackRect(rect); },
        [this](const RECT& rect) { candidate_ui_->MoveWindowTo(rect); });
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
