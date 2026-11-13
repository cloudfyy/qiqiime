// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"
#include "ime/comp/edit.h"
#include "ime/comp/finish.h"
#include "ime/comp/layout.h"
#include "ime/comp/presentation.h"
#include "ime/comp/start.h"
#include "ime/comp/target.h"
#include "ime/comp/update.h"
#include "ime/tsf/edit_session.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"

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
                const auto monitor = QueryMonitorForPoint(POINT{focus_rc.left, focus_rc.top}, gti.hwndFocus);
                out_rc = ComputeFocusedWindowFallbackCaretRect(focus_rc, monitor.dpi);
                return true;
            }
        }
    }
    POINT cursor_pt{};
    if (::GetCursorPos(&cursor_pt)) {
        const auto monitor = QueryMonitorForPoint(cursor_pt);
        out_rc = ComputeFallbackCaretRect(cursor_pt, monitor.dpi);
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
    return EnterCompositionLayoutNotification(
        pic, lcode, composition_lifecycle_, service_log_id_, current_lifecycle_activity_,
        [this]() noexcept { return candidate_ui_ != nullptr; },
        [this, pic, pView](const GUID& layout_act_id) {
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
                        const EditSessionTicket layout_ticket{self_holder->composition_lifecycle_.Epoch(), context.get()};
                        const AccessPolicy policy = self_holder->EvaluateContextAccessInSession(cookie, context, layout_ticket, true);
                        if (!policy.can_input || !self_holder->IsEditSessionCurrent(layout_ticket)) {
                            return;
                        }
                        self_holder->UpdateCandidateWindowPositionInSession(cookie, context, view);
                    });
            });
            RequestCompositionLayoutSession(
                gsl::not_null<ITfContext*>(pic), gsl::not_null<ITfEditSession*>(session.Get()),
                client_id_, ticket, service_log_id_, layout_act_id);
    });
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
    DisconnectActiveContextAccessSinks();
    context_access_.Reset();
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
    const bool was_same = (context && composition_lifecycle_.Context().Get() == context);
    if (!PrepareCompositionInputTarget(
            context, composition_lifecycle_, preparing_input_context_, text_layout_sink_,
            gsl::not_null<ITfTextLayoutSink*>(this), service_log_id_, current_lifecycle_activity_,
            [this] { ResetCompositionAndUI(); },
            [this](EditSessionTicket ticket) noexcept { return IsEditSessionCurrent(ticket); })) {
        return false;
    }
    if (!was_same && composition_lifecycle_.Context().Get() == context) {
        ConnectActiveContextAccessSinks(context);
        const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context};
        if (!IsEditSessionCurrent(ticket)) {
            return false;
        }
    }
    return true;
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
            const AccessPolicy policy = EvaluateContextAccessInSession(cookie, context, ticket, true);
            if (!policy.can_input || !IsEditSessionCurrent(ticket)) {
                return S_OK;
            }
            engine_.ApplyDeferredPhrasePromotion(result, policy.can_learn);
            if (!IsEditSessionCurrent(ticket)) {
                return S_OK;
            }
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
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const auto composition = composition_lifecycle_.Composition();
    const auto ui = candidate_ui_;
    UpdateCompositionLayoutPositionInSession(
        ec, context, composition_lifecycle_, ui.Get(), view, has_valid_caret_pos_,
        service_log_id_, current_lifecycle_activity_,
        [this](EditSessionTicket ticket, const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        },
        [](RECT& rect) noexcept { return QueryGuiThreadOrCursorFallbackRect(rect); },
        [this, &view, ticket, composition, ui](const RECT& rect) {
            HWND host_window = nullptr;
            if (view) {
                const HRESULT window_hr = view->GetWnd(&host_window);
                if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
                if (!log::CheckHResult(window_hr, "CandidatePosition::GetWnd", log::Level::debug)) {
                    host_window = nullptr;
                }
            }
            if (!host_window) host_window = ::GetFocus();
            if (!IsCompositionEditCurrent(ticket, composition.Get())) return;
            ui->MoveWindowTo(rect, host_window);
        });
}

void QiQiTextService::BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot) {
    EnterCompositionCandidatePresentation(
        candidate_ui_.Get(), thread_mgr_.Get(), composition_lifecycle_,
        [this, &snapshot](EditSessionTicket ticket) {
            PresentCompositionCandidates(
                *candidate_ui_.Get(), activate_flags_, snapshot, ticket.epoch, service_log_id_, current_lifecycle_activity_,
                [this]() noexcept { return thread_mgr_.Get(); },
                [this, ticket]() noexcept { return IsEditSessionCurrent(ticket); });
        });
}

void QiQiTextService::EndCandidateUI() {
    if (!candidate_ui_) {
        return;
    }
    EndCompositionCandidatePresentation(*candidate_ui_.Get(), thread_mgr_, service_log_id_, current_lifecycle_activity_);
}

} // namespace qiqi
