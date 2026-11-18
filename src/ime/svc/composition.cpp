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
#include "ime/tsf/editsession.h"
#include "common/logger.h"
#include "common/dpi.h"
#include <new>

namespace qiqi {

namespace {

bool QueryGuiThreadOrCursorFallbackRect(RECT& out_rc) noexcept {
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (QueryLayoutGuiThreadInfo(gti)) {
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

bool NeedsPostEditLayoutRefresh(const CandidateUI& ui) noexcept {
    const HWND popup = ui.WindowHandle();
    if (!popup || !::IsWindowVisible(popup)) return false;
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (!QueryLayoutGuiThreadInfo(gui)) {
        QIQI_LOG_CAT_DEBUG(log::Keyword::UI, "CandidatePosition::GetGUIThreadInfo failed (error={})",
                           ::GetLastError());
        return false;
    }
    const HWND focus_root = gui.hwndFocus ? ::GetAncestor(gui.hwndFocus, GA_ROOT) : nullptr;
    const bool owner_matches_focus = focus_root && focus_root == ::GetWindow(popup, GW_OWNER);
    QIQI_LOG_CAT_DEBUG(log::Keyword::UI,
        "CandidatePosition(stage=initial-layout-eligibility, has_native_caret={}, owner_matches_focus={})",
        gui.hwndCaret != nullptr, owner_matches_focus);
    return !gui.hwndCaret && owner_matches_focus;
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

void QiQiTextService::ScheduleCompositionCleanup(CompositionCleanupMode mode) {
    ScheduleCurrentCompositionCleanup(composition_lifecycle_, client_id_, service_log_id_,
                                      current_lifecycle_activity_, mode);
}

// ITfTextLayoutSink: called when an async host (Chromium/VS Code/WPF/Word) updates its text layout
STDMETHODIMP QiQiTextService::OnLayoutChange(
    ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView) {
    return EnterCompositionLayoutNotification(
        pic, lcode, composition_lifecycle_, service_log_id_, current_lifecycle_activity_,
        [this]() noexcept { return candidate_ui_ != nullptr; },
        [this, pic, pView](const GUID& layout_act_id) {
            RequestCandidateWindowLayout(gsl::not_null<ITfContext*>(pic), ComPtr<ITfContextView>(pView), layout_act_id);
    });
}

void QiQiTextService::RequestCandidateWindowLayout(
    gsl::not_null<ITfContext*> context, ComPtr<ITfContextView> view, const GUID& activity,
    ComPtr<ITfComposition> expected_composition) {
    const ComPtr<ITfContext> target(context.get());
    const ComPtr<QiQiTextService> self(this);
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const bool defer = expected_composition != nullptr;
    const auto session = MakeComObject<LambdaEditSession>(
        [self, task = CompositionLayoutTask(target, std::move(view), ticket, activity),
         expected_composition = std::move(expected_composition)](TfEditCookie ec) -> HRESULT {
            const auto is_current = [&](EditSessionTicket expected) noexcept {
                return expected_composition ?
                    self->IsCompositionEditCurrent(expected, expected_composition.Get()) :
                    self->IsEditSessionCurrent(expected);
            };
            return ExecuteCompositionLayoutInSession(
                ec, task, self->current_lifecycle_activity_, self->service_log_id_, is_current,
                [&self, &is_current, &task, &expected_composition](
                    TfEditCookie cookie, gsl::not_null<ITfContext*> target_context,
                    const ComPtr<ITfContextView>& target_view) {
                    const AccessPolicy policy = self->EvaluateContextAccessInSession(
                        cookie, target_context, task.Ticket(), true);
                    if (!policy.can_input || !is_current(task.Ticket())) return;
                    if (!self->TraceCandidateInputGeometryInSession(
                            cookie, target_context,
                            expected_composition ? "post-edit-read" : "layout-notification-read",
                            target_view.Get())) return;
                    self->UpdateCandidateWindowPositionInSession(cookie, target_context, target_view);
                });
        });
    RequestCompositionLayoutSession(
        context, gsl::not_null<ITfEditSession*>(session.Get()), client_id_, ticket, service_log_id_, activity, defer);
}

void QiQiTextService::RefreshCandidateLayoutOnKeyRelease(ITfContext* context, uint32_t key) noexcept {
    if (!initial_layout_key_release_key_ || key != initial_layout_key_release_key_ || !context ||
        context != composition_lifecycle_.Context().Get() || GetInputBlockReason() != InputBlockReason::None) return;
    const ComPtr<QiQiTextService> self(this);
    const auto composition = composition_lifecycle_.Composition();
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context};
    initial_layout_key_release_key_ = 0;
    if (!composition || !candidate_ui_ || !has_valid_caret_pos_ ||
        !NeedsPostEditLayoutRefresh(*candidate_ui_.Get()) ||
        !IsCompositionEditCurrent(ticket, composition.Get())) return;
    try {
        const GUID activity = log::CreateActivityId();
        QIQI_LOG_ACT_START(log::Level::debug, log::Keyword::UI, &activity, &current_lifecycle_activity_,
            "CandidatePosition(service={}, stage=initial-key-release-recheck, activation={}, context={})",
            service_log_id_, ticket.epoch.activation, ticket.epoch.context);
        RequestCandidateWindowLayout(gsl::not_null<ITfContext*>(context), {}, activity, composition);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_CAT_ERROR(log::Keyword::UI,
            "CandidatePosition::KeyReleaseRequest allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
    }
}

bool QiQiTextService::TraceCandidateInputGeometryInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, std::string_view stage,
    ITfContextView* notification_view) {
    if (!log::ShouldLog(log::Level::debug, log::Keyword::UI)) return true;
    const ComPtr<ITfContext> target(context.get());
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const CompositionEditGuard guard(composition_lifecycle_.Composition(),
        [this, ticket](const ITfComposition* composition) noexcept {
            return IsCompositionEditCurrent(ticket, composition);
        });
    if (!guard.Current()) return false;
    TF_SELECTION selection{};
    ULONG fetched = 0;
    const HRESULT selection_hr = context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    ComPtr<ITfRange> selection_range;
    selection_range.Attach(selection.range);
    QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
        "CandidateGeometry(service={}, stage={}, source=selection, hr=0x{:08x}, fetched={}, has_range={})",
        service_log_id_, stage, gsl::narrow_cast<unsigned int>(selection_hr), fetched, selection_range != nullptr);
    if (!guard.Current()) return false;
    if (FAILED(selection_hr) || fetched != 1 || !selection_range) return true;
    const auto trace_window = [&](std::string_view source, HWND window) {
        RECT rect{};
        const BOOL got_rect = window && ::GetWindowRect(window, &rect);
        const DWORD error = window && !got_rect ? ::GetLastError() : ERROR_SUCCESS;
        const UINT dpi = got_rect ? ::GetDpiForWindow(window) : 0;
        QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
            "CandidateGeometry(service={}, stage={}, source={}, has_window={}, queried={}, error={}, dpi={}, rect=({}, {}, {}, {}))",
            service_log_id_, stage, source, window != nullptr, got_rect != FALSE, error,
            dpi, rect.left, rect.top, rect.right, rect.bottom);
    };
    const auto trace_rect = [&](std::string_view source, HRESULT hr, const RECT& rect, BOOL clipped = FALSE) {
        QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
            "CandidateGeometry(service={}, stage={}, source={}, hr=0x{:08x}, rect=({}, {}, {}, {}), clipped={})",
            service_log_id_, stage, source, gsl::narrow_cast<unsigned int>(hr),
            rect.left, rect.top, rect.right, rect.bottom, clipped != FALSE);
    };
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    const BOOL got_gui = QueryLayoutGuiThreadInfo(gui);
    const DWORD gui_error = got_gui ? ERROR_SUCCESS : ::GetLastError();
    QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
        "CandidateGeometry(service={}, stage={}, source=gui-thread, queried={}, error={}, has_native_caret={})",
        service_log_id_, stage, got_gui != FALSE, gui_error, gui.hwndCaret != nullptr);
    if (got_gui) {
        trace_window("focus-window", gui.hwndFocus);
        trace_window("active-window", gui.hwndActive);
    }
    if (!guard.Current()) return false;
    ComPtr<ITfContextView> view;
    const HRESULT view_hr = context->GetActiveView(&view);
    QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
        "CandidateGeometry(service={}, stage={}, source=active-view, hr=0x{:08x}, has_view={}, "
        "has_notification_view={}, same_as_notification={})",
        service_log_id_, stage, gsl::narrow_cast<unsigned int>(view_hr), view != nullptr,
        notification_view != nullptr, notification_view && notification_view == view.Get());
    if (!guard.Current()) return false;
    const auto trace_view = [&](gsl::not_null<ITfContextView*> diagnostic_view, bool notified) {
        RECT screen{};
        const HRESULT screen_hr = diagnostic_view->GetScreenExt(&screen);
        trace_rect(notified ? "notification-view-screen" : "view-screen", screen_hr, screen);
        if (!guard.Current()) return false;
        HWND window = nullptr;
        const HRESULT window_hr = diagnostic_view->GetWnd(&window);
        QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
            "CandidateGeometry(service={}, stage={}, source={}, hr=0x{:08x}, has_window={})",
            service_log_id_, stage, notified ? "notification-view-window" : "view-window",
            gsl::narrow_cast<unsigned int>(window_hr), window != nullptr);
        if (!guard.Current()) return false;
        if (SUCCEEDED(window_hr)) {
            trace_window(notified ? "notification-view-window-bounds" : "view-window-bounds", window);
        }
        RECT rect{};
        BOOL clipped = FALSE;
        const HRESULT extent_hr = diagnostic_view->GetTextExt(ec, selection_range.Get(), &rect, &clipped);
        trace_rect(notified ? "notification-selection-extent" : "selection-extent", extent_hr, rect, clipped);
        return guard.Current();
    };
    if (SUCCEEDED(view_hr) && view && !trace_view(gsl::not_null<ITfContextView*>(view.Get()), false)) return false;
    if (notification_view && notification_view != view.Get() &&
        !trace_view(gsl::not_null<ITfContextView*>(notification_view), true)) return false;
    return guard.Current();
}

CompositionCleanupMode QiQiTextService::UnfinishedInputCleanupMode() const noexcept {
    return config_.commit_on_english_switch
        ? CompositionCleanupMode::PreserveText : CompositionCleanupMode::DiscardText;
}

void QiQiTextService::ResetCompositionForInputMethodSwitch() {
    QIQI_LOG_CAT_DEBUG(log::Keyword::Composition,
        "Unfinished input on input-method switch: preserve_text={}", config_.commit_on_english_switch);
    ResetCompositionAndUI(true, UnfinishedInputCleanupMode());
}

void QiQiTextService::ResetCompositionAndUI(bool try_host_cleanup, CompositionCleanupMode mode) {
    if (!preparing_input_context_) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("ResetCompositionAndUI", 0, composition_lifecycle_.Context().Get(), false, "context-reset-cancelled");
        }
        shift_tap_.Reset();
    }
    engine_.Reset();
    ResetContextAndUI(try_host_cleanup, mode);
}

void QiQiTextService::ResetContextAndUI(bool try_host_cleanup, CompositionCleanupMode mode) {
    initial_layout_key_release_key_ = 0;
    DisconnectActiveContextAccessSinks();
    context_access_.Reset();
    ResetCompositionContextAndUI(
        composition_lifecycle_, try_host_cleanup, has_valid_caret_pos_, text_layout_sink_,
        service_log_id_, current_lifecycle_activity_,
        [this, mode] { ScheduleCompositionCleanup(mode); },
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
            [this] { ResetCompositionAndUI(true, UnfinishedInputCleanupMode()); },
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

// qiqi-reentrancy: context
HRESULT QiQiTextService::ApplyCompositionEditInSession(TfEditCookie ec, const CompositionEditTask& task) {
    return ExecuteCompositionEditInSession(
        ec, task, current_lifecycle_activity_, composition_lifecycle_.Cleanups(), service_log_id_,
        [this](EditSessionTicket expected) noexcept { return IsEditSessionCurrent(expected); },
        [this](ITfContext* context, const KeyProcessResult& result) { ApplyEngineResult(context, result); },
        [this](TfEditCookie cookie, gsl::not_null<ITfContext*> context,
               const KeyProcessResult& result, EditSessionTicket ticket) {
            return ApplyAdmittedCompositionEditInSession(cookie, context, result, ticket);
        });
}

// qiqi-reentrancy: context
HRESULT QiQiTextService::ApplyAdmittedCompositionEditInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context,
    const KeyProcessResult& result, EditSessionTicket ticket) {
    const ContextEditGuard guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    const AccessPolicy policy = EvaluateContextAccessInSession(ec, context, ticket, true);
    if (!policy.can_input || !guard.Current()) {
        return S_OK;
    }
    engine_.ApplyDeferredPhrasePromotion(result, policy.can_learn);
    if (!guard.Current()) {
        return S_OK;
    }
    return ApplyCompositionEditActionInSession(
        ec, context, result,
        [&guard]() noexcept { return guard.CurrentComposition(nullptr); },
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
}

// qiqi-reentrancy: composition
void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const ContextEditGuard guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    const bool first_update = !composition_lifecycle_.Composition();
    SyncCompositionPresentationInSession(
        ec, context, snapshot,
        [this, &guard, first_update](
            TfEditCookie cookie, gsl::not_null<ITfContext*> target, const std::wstring& text) {
            if (first_update && !TraceCandidateInputGeometryInSession(cookie, target, "before-first-edit")) {
                return ComPtr<ITfComposition>{};
            }
            auto composition = StartOrUpdateCompositionInSession(cookie, target, text);
            if (first_update && composition && guard.CurrentComposition(composition.Get())) {
                std::ignore = TraceCandidateInputGeometryInSession(cookie, target, "after-first-edit");
            }
            return composition;
        },
        [&guard](const ITfComposition* composition) noexcept {
            return guard.CurrentComposition(composition);
        },
        [this, first_update](TfEditCookie cookie, gsl::not_null<ITfContext*> target) {
            if (!first_update && !TraceCandidateInputGeometryInSession(cookie, target, "composition-update-read")) return;
            UpdateCandidateWindowPositionInSession(cookie, target);
        },
        [this, &guard, context, first_update](const KeyProcessResult& result) {
            const auto composition = composition_lifecycle_.Composition();
            BeginOrUpdateCandidateUI(result);
            if (!first_update || !composition || !guard.CurrentComposition(composition.Get()) ||
                !has_valid_caret_pos_ || !candidate_ui_ || !NeedsPostEditLayoutRefresh(*candidate_ui_.Get())) return;
            const wchar_t first_key = result.composition_text.empty() ? L'\0' : result.composition_text.front();
            initial_layout_key_release_key_ = first_key >= L'a' && first_key <= L'z'
                ? gsl::narrow_cast<uint32_t>(first_key - L'a' + L'A') : 0;
            // Some windowless text stores report the previous layout until this write session ends.
            const GUID activity = log::CreateActivityId();
            QIQI_LOG_ACT_START(log::Level::debug, log::Keyword::UI, &activity, &current_lifecycle_activity_,
                "CandidatePosition(service={}, stage=initial-layout-recheck, activation={}, context={})",
                service_log_id_, composition_lifecycle_.Epoch().activation, composition_lifecycle_.Epoch().context);
            RequestCandidateWindowLayout(context, {}, activity, composition);
        });
}

ComPtr<ITfComposition> QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const ContextEditGuard guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    const TfClientId client_id = client_id_;
    return EnsureAndUpdateCompositionInSession(
        ec, context, this, composition_lifecycle_, client_id, ticket.epoch, current_lifecycle_activity_,
        text, display_attribute_atom_,
        [&guard](const ITfComposition* composition) noexcept {
            return guard.CurrentComposition(composition);
        },
        [this] { ResetCompositionAndUI(); });
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const ContextEditGuard guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    FinishOrInsertCompositionInSession(
        ec, context, composition_lifecycle_, has_valid_caret_pos_, CompositionFinishKind::Commit, committed_text,
        [&guard](const ITfComposition* composition) noexcept {
            return guard.CurrentComposition(composition);
        });
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const ContextEditGuard guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    FinishOrInsertCompositionInSession(
        ec, context, composition_lifecycle_, has_valid_caret_pos_, CompositionFinishKind::Cancel, {},
        [&guard](const ITfComposition* composition) noexcept {
            return guard.CurrentComposition(composition);
        });
}

// qiqi-reentrancy: composition
void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, ComPtr<ITfContextView> view) {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const ContextEditGuard context_guard(composition_lifecycle_, ticket, service_log_id_, current_lifecycle_activity_);
    const CompositionEditGuard guard(
        composition_lifecycle_.Composition(),
        [&context_guard](const ITfComposition* composition) noexcept {
            return context_guard.CurrentComposition(composition);
        });
    const auto ui = candidate_ui_;
    UpdateCompositionLayoutPositionInSession(
        ec, context, composition_lifecycle_, ui.Get(), view, has_valid_caret_pos_,
        service_log_id_, current_lifecycle_activity_,
        [&context_guard](EditSessionTicket, const ITfComposition* composition) noexcept {
            return context_guard.CurrentComposition(composition);
        },
        [](RECT& rect) noexcept { return QueryGuiThreadOrCursorFallbackRect(rect); },
        [&view, &guard, ui](const RECT& rect) {
            HWND host_window = nullptr;
            if (view) {
                const HRESULT window_hr = view->GetWnd(&host_window);
                if (!guard.Current()) return;
                if (!log::CheckHResult(window_hr, "CandidatePosition::GetWnd", log::Level::debug)) {
                    host_window = nullptr;
                }
            }
            if (!host_window) host_window = ::GetFocus();
            if (!guard.Current()) return;
            ui->MoveWindowTo(rect, host_window);
        });
}

// qiqi-reentrancy: context
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
    initial_layout_key_release_key_ = 0;
    if (!candidate_ui_) {
        return;
    }
    EndCompositionCandidatePresentation(*candidate_ui_.Get(), thread_mgr_, service_log_id_, current_lifecycle_activity_);
}

} // namespace qiqi
