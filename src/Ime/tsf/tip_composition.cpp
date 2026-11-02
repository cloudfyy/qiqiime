#include "Ime/tsf/tip_service.h"
#include "Ime/tsf/edit_session.h"
#include "common/logger.h"
#include <algorithm>

namespace qiqi {

namespace {

template <typename IsCurrent>
HRESULT ClearCompositionDisplayAttribute(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range,
    IsCurrent is_current, bool* clear_started = nullptr) {
    if (!is_current()) {
        QIQI_LOG_DEBUG("CompositionAttributeClear: invalidated operation skipped");
        return S_FALSE;
    }
    ComPtr<ITfProperty> property;
    const HRESULT property_hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
    if (!log::CheckHResult(property_hr, "CompositionCleanup::GetProperty(GUID_PROP_ATTRIBUTE)")) {
        return property_hr;
    }
    if (!property) {
        QIQI_LOG_WARN("CompositionCleanup::GetProperty returned no property (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(property_hr));
        return E_UNEXPECTED;
    }
    if (!is_current()) {
        QIQI_LOG_DEBUG("CompositionAttributeClear: invalidated operation skipped after property lookup");
        return S_FALSE;
    }
    if (clear_started) *clear_started = true;
    const HRESULT hr = property->Clear(ec, range.get());
    std::ignore = log::CheckHResult(hr, "CompositionCleanup::Clear(GUID_PROP_ATTRIBUTE)");
    return hr;
}

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

QiQiTextService::CompositionCleanup::~CompositionCleanup() noexcept {
    if (id == 0) return;
    QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=released, async_requested={}, started={}, finished={}, terminated={}, context_removed={})",
                   service_id, id, async_requested, started, finished, terminated, context_removed);
}

void QiQiTextService::CompositionCleanup::LogResult(std::string_view stage, HRESULT hr) const noexcept {
    if (FAILED(hr)) {
        QIQI_LOG_WARN("TsfCleanup(service={}, cleanup={}, stage={}, hr=0x{:08x})",
                       service_id, id, stage, gsl::narrow_cast<unsigned int>(hr));
    } else {
        QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage={}, hr=0x{:08x})",
                       service_id, id, stage, gsl::narrow_cast<unsigned int>(hr));
    }
}

// ITfCompositionSink: called when the host application forcefully terminates composition
// (e.g. user clicked elsewhere, switched Excel cell, or DOM re-rendered)
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) {
    for (const auto& pending : composition_cleanups_) {
        const auto cleanup = pending.lock();
        if (pComposition && cleanup && pComposition == cleanup->composition.Get()) {
            if (cleanup->id == 0) {
                cleanup->terminated = true;
                cleanup->finished = true;
                QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=composition-start-terminated)", service_log_id_);
                return S_OK;
            }
            QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=host-terminated, started={}, finished={}, duplicate={})",
                           cleanup->service_id, cleanup->id, cleanup->started, cleanup->finished, cleanup->terminated);
            if (cleanup->terminated) {
                return S_OK;
            }
            cleanup->terminated = true;
            if (cleanup->context_removed) return S_OK;
            if (!cleanup->attribute_clear_started && cleanup->range) {
                const HRESULT clear_hr = ClearCompositionDisplayAttribute(
                    ecWrite, gsl::not_null<ITfContext*>(cleanup->context.Get()),
                    gsl::not_null<ITfRange*>(cleanup->range.Get()),
                    [&cleanup]() noexcept { return !cleanup->context_removed; }, &cleanup->attribute_clear_started);
                cleanup->LogResult("host-attribute-clear", clear_hr);
            }
            if (!cleanup->started) cleanup->finished = true;
            return S_OK;
        }
    }
    if (pComposition && pComposition != composition_.Get()) {
        QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=host-termination-ignored, activation={}, context={})",
                       service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context);
        return S_OK;
    }
    QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=host-terminated-current, activation={}, context={}, has_composition={})",
                   service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context, composition_ != nullptr);
    if (pComposition && active_context_) {
        const auto epoch = edit_session_epoch_;
        const ComPtr<ITfContext> context_holder = active_context_;
        const ComPtr<ITfComposition> composition_holder = std::move(composition_);
        ComPtr<ITfRange> range;
        const HRESULT range_hr = composition_holder->GetRange(&range);
        if (log::CheckHResult(range_hr, "HostTermination::GetRange")) {
            if (range) {
                std::ignore = ClearCompositionDisplayAttribute(
                    ecWrite, gsl::not_null<ITfContext*>(context_holder.Get()), gsl::not_null<ITfRange*>(range.Get()),
                    [this, epoch, &context_holder]() noexcept {
                        return IsCompositionEditCurrent(epoch, context_holder.Get(), nullptr);
                    });
            } else {
                QIQI_LOG_WARN("HostTermination::GetRange returned no range (hr=0x{:08x})",
                               gsl::narrow_cast<unsigned int>(range_hr));
            }
        }
        if (!IsCompositionEditCurrent(epoch, context_holder.Get(), nullptr)) {
            return S_OK;
        }
    }
    ResetCompositionAndUI();
    return S_OK;
}

void QiQiTextService::ScheduleCompositionCleanup() {
    if (!composition_) {
        QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=cleanup-not-needed, activation={}, context={})",
                       service_log_id_, edit_session_epoch_.activation, edit_session_epoch_.context);
        return;
    }
    ScheduleCompositionCleanup(active_context_, std::move(composition_), client_id_);
}

void QiQiTextService::ScheduleCompositionCleanup(
    ComPtr<ITfContext> context, ComPtr<ITfComposition> composition, TfClientId client_id) {
    const auto cleanup = std::make_shared<CompositionCleanup>();
    cleanup->service_id = service_log_id_;
    cleanup->id = ++next_cleanup_log_id_;
    QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=created, activation={}, context={}, client_id={})",
                   cleanup->service_id, cleanup->id, edit_session_epoch_.activation, edit_session_epoch_.context, client_id);
    cleanup->composition = std::move(composition);
    cleanup->context = std::move(context);
    if (!cleanup->context || client_id == TF_CLIENTID_NULL) {
        QIQI_LOG_WARN("TsfCleanup(service={}, cleanup={}, stage=unavailable, has_context={}, valid_client={})",
                       cleanup->service_id, cleanup->id, cleanup->context != nullptr, client_id != TF_CLIENTID_NULL);
        return;
    }
    std::ignore = HasPendingCompositionCleanup(nullptr);
    composition_cleanups_.push_back(cleanup);
    const HRESULT range_hr = cleanup->composition->GetRange(&cleanup->range);
    cleanup->LogResult("get-range", range_hr);
    if (log::CheckHResult(range_hr, "DeactivateComposition::GetRange") && !cleanup->range) {
        QIQI_LOG_WARN("DeactivateComposition::GetRange returned no range (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(range_hr));
    }
    if (cleanup->context_removed || cleanup->terminated) {
        QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=skip-request-invalidated, terminated={}, context_removed={})",
                       cleanup->service_id, cleanup->id, cleanup->terminated, cleanup->context_removed);
        return;
    }
    const auto session = MakeComObject<LambdaEditSession>(
        [cleanup](TfEditCookie ec) -> HRESULT {
            cleanup->started = true;
            QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=callback-enter, async_requested={}, terminated={}, has_range={})",
                           cleanup->service_id, cleanup->id, cleanup->async_requested, cleanup->terminated, cleanup->range != nullptr);
            const auto finish = gsl::finally([cleanup]() noexcept {
                cleanup->finished = true;
                QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=callback-exit, terminated={})",
                               cleanup->service_id, cleanup->id, cleanup->terminated);
            });
            if (cleanup->context_removed) {
                QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=skip-removed-context)",
                               cleanup->service_id, cleanup->id);
                return S_OK;
            }
            if (cleanup->terminated) {
                QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=skip-host-terminated)",
                               cleanup->service_id, cleanup->id);
                return S_OK;
            }
            if (cleanup->range) {
                const HRESULT clear_hr = ClearCompositionDisplayAttribute(
                    ec, gsl::not_null<ITfContext*>(cleanup->context.Get()),
                    gsl::not_null<ITfRange*>(cleanup->range.Get()),
                    [&cleanup]() noexcept { return !cleanup->terminated && !cleanup->context_removed; },
                    &cleanup->attribute_clear_started);
                cleanup->LogResult("attribute-clear", clear_hr);
            }
            if (cleanup->terminated || cleanup->context_removed) {
                QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage={})",
                               cleanup->service_id, cleanup->id,
                               cleanup->context_removed ? "skip-end-after-context-removal" : "skip-end-after-host-termination");
                return S_OK;
            }
            const HRESULT hr = cleanup->composition->EndComposition(ec);
            cleanup->LogResult("end-composition", hr);
            std::ignore = log::CheckHResult(hr, "DeactivateComposition::EndComposition");
            return hr;
        });
    HRESULT session_hr = E_FAIL;
    QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=sync-request, flags=0x{:x})",
                   cleanup->service_id, cleanup->id, TF_ES_SYNC | TF_ES_READWRITE);
    const HRESULT request_hr = cleanup->context->RequestEditSession(
        client_id, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
    QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=sync-result, request_hr=0x{:08x}, session_hr=0x{:08x}, started={}, terminated={})",
                   cleanup->service_id, cleanup->id, gsl::narrow_cast<unsigned int>(request_hr),
                   gsl::narrow_cast<unsigned int>(session_hr), cleanup->started, cleanup->terminated);
    if (SUCCEEDED(request_hr) && session_hr == TF_E_SYNCHRONOUS &&
        !cleanup->started && !cleanup->terminated && !cleanup->context_removed) {
        QIQI_LOG_DEBUG("DeactivateComposition: synchronous access refused; requesting asynchronous cleanup (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(session_hr));
        session_hr = E_FAIL;
        cleanup->async_requested = true;
        QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=async-request, flags=0x{:x})",
                       cleanup->service_id, cleanup->id, TF_ES_ASYNC | TF_ES_READWRITE);
        const HRESULT async_hr = cleanup->context->RequestEditSession(
            client_id, session.Get(), TF_ES_ASYNC | TF_ES_READWRITE, &session_hr);
        QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=async-result, request_hr=0x{:08x}, session_hr=0x{:08x}, queued={})",
                       cleanup->service_id, cleanup->id, gsl::narrow_cast<unsigned int>(async_hr),
                       gsl::narrow_cast<unsigned int>(session_hr), SUCCEEDED(async_hr) && session_hr == TF_S_ASYNC);
        if (FAILED(async_hr) || FAILED(session_hr)) {
            cleanup->LogResult("async-rejected", FAILED(async_hr) ? async_hr : session_hr);
        }
        if (log::CheckHResult(async_hr, "DeactivateComposition::RequestEditSession(TF_ES_ASYNC | TF_ES_READWRITE)")) {
            std::ignore = log::CheckHResult(session_hr, "DeactivateComposition::AsyncEditSession");
        }
        return;
    }
    if (FAILED(request_hr) || FAILED(session_hr)) {
        cleanup->LogResult("sync-failed-no-fallback", FAILED(request_hr) ? request_hr : session_hr);
    }
    if (log::CheckHResult(request_hr, "DeactivateComposition::RequestEditSession(TF_ES_SYNC | TF_ES_READWRITE)")) {
        std::ignore = log::CheckHResult(session_hr, "DeactivateComposition::DoEditSession");
    }
}

bool QiQiTextService::HasPendingCompositionCleanup(const ITfContext* context) {
    std::erase_if(composition_cleanups_, [](const auto& pending) {
        const auto cleanup = pending.lock();
        return !cleanup || cleanup->finished;
    });
    for (const auto& pending : composition_cleanups_) {
        const auto cleanup = pending.lock();
        if (cleanup && cleanup->context.Get() == context) {
            if (cleanup->id == 0) {
                QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=composition-start-write-deferred)", service_log_id_);
                return true;
            }
            QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=write-forced-async)",
                           cleanup->service_id, cleanup->id);
            return true;
        }
    }
    return false;
}

void QiQiTextService::MarkCompositionContextRemoved(const ITfContext* context) noexcept {
    for (const auto& pending : composition_cleanups_) {
        const auto cleanup = pending.lock();
        if (cleanup && cleanup->context.Get() == context) {
            cleanup->context_removed = true;
            cleanup->finished = true;
            if (cleanup->id == 0) {
                QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=composition-start-removed)", service_log_id_);
                continue;
            }
            QIQI_LOG_DEBUG("TsfCleanup(service={}, cleanup={}, stage=context-removed, started={})",
                           cleanup->service_id, cleanup->id, cleanup->started);
        }
    }
}

// ITfTextLayoutSink: called when an async host (Chromium/VS Code/WPF/Word) updates its text layout
STDMETHODIMP QiQiTextService::OnLayoutChange(
    ITfContext* pic, TfLayoutCode lcode, ITfContextView* /*pView*/) {
    if (lcode != TF_LC_CHANGE || !pic || !is_active_ || pic != active_context_.Get() ||
        !composition_ || !candidate_ui_) {
        return S_OK;
    }
    ComPtr<ITfContext> ctx_holder(pic);
    ComPtr<QiQiTextService> self_holder(this);
    const auto epoch = edit_session_epoch_;
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, epoch](TfEditCookie ec) -> HRESULT {
        if (!self_holder->IsEditSessionCurrent(epoch, ctx_holder.Get())) {
            return S_OK;
        }
        self_holder->UpdateCandidateWindowPositionInSession(
            ec, gsl::not_null<ITfContext*>(ctx_holder.Get()));
        return S_OK;
    });
    HRESULT session_hr = S_OK;
    const HRESULT request_hr = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
    std::ignore = log::CheckHResult(request_hr, "OnLayoutChange::RequestEditSession", log::Level::debug);
    if (SUCCEEDED(request_hr)) {
        std::ignore = log::CheckHResult(session_hr, "OnLayoutChange::DoEditSession", log::Level::debug);
    }
    return S_OK;
}

void QiQiTextService::ResetCompositionAndUI(bool try_host_cleanup) {
    if (!preparing_input_context_) {
        shift_tap_candidate_ = false;
        shift_press_tick_ = 0;
    }
    engine_.Reset();
    ResetContextAndUI(try_host_cleanup);
}

void QiQiTextService::ResetContextAndUI(bool try_host_cleanup) {
    QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=context-reset, activation={}, context_before={}, context_after={}, has_context={}, has_composition={}, try_host_cleanup={})",
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
        QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=cleanup-skipped-removed-context)", service_log_id_);
    }
    active_context_.Reset();
    active_document_.Reset();
    has_valid_caret_pos_ = false;
    composition_.Reset();
    text_layout_sink_.Reset();
    EndCandidateUI();
}

bool QiQiTextService::IsEditSessionCurrent(EditSessionEpoch epoch, const ITfContext* context) const noexcept {
    if (is_active_ && !resetting_context_ && epoch.activation == edit_session_epoch_.activation &&
        epoch.context == edit_session_epoch_.context && context == active_context_.Get()) {
        return true;
    }
    QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=stale-edit-skipped, activation={}/{}, context={}/{}, active={}, target_matches={})",
                   service_log_id_, epoch.activation, edit_session_epoch_.activation,
                   epoch.context, edit_session_epoch_.context, is_active_, context == active_context_.Get());
    return false;
}

bool QiQiTextService::PrepareInputContext(ITfContext* context) {
    if (!context || !is_active_ || resetting_context_ || preparing_input_context_) {
        QIQI_LOG_TRACE("TsfLifecycle(service={}, stage=input-target-unavailable)", service_log_id_);
        return false;
    }
    if (active_context_.Get() == context) return true;
    ComPtr<ITfContext> ctx_holder(context);
    preparing_input_context_ = ctx_holder;
    const auto finish_prepare = gsl::finally([this]() noexcept { preparing_input_context_.Reset(); });
    auto epoch = edit_session_epoch_;
    ++epoch.context;
    ResetCompositionAndUI();
    if (!IsEditSessionCurrent(epoch, nullptr)) return false;
    ComPtr<ITfDocumentMgr> document;
    const HRESULT document_hr = context->GetDocumentMgr(&document);
    if (!log::CheckHResult(document_hr, "PrepareInputContext::GetDocumentMgr") || !document) {
        QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=input-document-unavailable, hr=0x{:08x})",
                       service_log_id_, gsl::narrow_cast<unsigned int>(document_hr));
    }
    if (!IsEditSessionCurrent(epoch, nullptr)) return false;
    active_context_ = ctx_holder;
    active_document_ = std::move(document);
    ITfTextLayoutSink* sink = this;
    text_layout_sink_.Advise(active_context_.Get(), IID_ITfTextLayoutSink, sink);
    if (!IsEditSessionCurrent(epoch, context)) {
        // A notification during AdviseSink may have invalidated the target before the cookie was published.
        text_layout_sink_.Reset();
        return false;
    }
    return true;
}

bool QiQiTextService::IsCompositionEditCurrent(
    EditSessionEpoch epoch, const ITfContext* context, const ITfComposition* composition) const noexcept {
    if (!IsEditSessionCurrent(epoch, context)) return false;
    if (composition_.Get() == composition) return true;
    QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=replaced-composition-edit-skipped, activation={}, context={})",
                   service_log_id_, epoch.activation, epoch.context);
    return false;
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context || !is_active_ || resetting_context_ || preparing_input_context_ ||
        context != active_context_.Get()) {
        QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=unprepared-edit-skipped)", service_log_id_);
        return;
    }
    ComPtr<ITfContext> ctx_holder(context);
    const auto epoch = edit_session_epoch_;
    ComPtr<QiQiTextService> self_holder(this);

    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, result, epoch](TfEditCookie ec) -> HRESULT {
        if (!self_holder->IsEditSessionCurrent(epoch, ctx_holder.Get())) {
            return S_OK;
        }
        if (self_holder->HasPendingCompositionCleanup(ctx_holder.Get())) {
            QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=write-requeued-behind-cleanup, activation={}, context={})",
                           self_holder->service_log_id_, epoch.activation, epoch.context);
            self_holder->ApplyEngineResult(ctx_holder.Get(), result);
            return S_OK;
        }
        const gsl::not_null<ITfContext*> non_null_ctx(ctx_holder.Get());
        if (result.action == KeyActionType::Cancel) {
            self_holder->CancelCompositionInSession(ec, non_null_ctx.get());
            if (self_holder->IsCompositionEditCurrent(epoch, ctx_holder.Get(), nullptr)) self_holder->EndCandidateUI();
            return S_OK;
        }

        if (result.action == KeyActionType::Commit) {
            self_holder->CommitAndEndCompositionInSession(ec, non_null_ctx, result.committed_text);
            if (!self_holder->IsCompositionEditCurrent(epoch, ctx_holder.Get(), nullptr)) return S_OK;
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
        (HasPendingCompositionCleanup(context) ? TF_ES_ASYNC : TF_ES_ASYNCDONTCARE) | TF_ES_READWRITE,
        &session_hr);
    if (FAILED(req_hr) || FAILED(session_hr)) {
        QIQI_LOG_WARN("RequestEditSession failed (req_hr=0x{:08x}, session_hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(req_hr),
                      gsl::narrow_cast<unsigned int>(session_hr));
    }
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    const auto epoch = edit_session_epoch_;
    const auto composition = StartOrUpdateCompositionInSession(ec, context, snapshot.composition_text);
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
    UpdateCandidateWindowPositionInSession(ec, context);
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
    BeginOrUpdateCandidateUI(snapshot);
}

void QiQiTextService::SetRangeTextAndCollapseCaret(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text) {
    const auto epoch = edit_session_epoch_;
    const auto composition = composition_;
    std::ignore = log::CheckHResult(range->SetText(ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size())),
                                   "SetRangeTextAndCollapseCaret::SetText");
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
    ComPtr<ITfRange> caret_range;
    if (log::CheckHResult(range->Clone(&caret_range), "SetRangeTextAndCollapseCaret::Clone") && caret_range) {
        if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
        std::ignore = log::CheckHResult(caret_range->Collapse(ec, TF_ANCHOR_END),
                                       "SetRangeTextAndCollapseCaret::Collapse");
        if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
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
    const auto epoch = edit_session_epoch_;
    const TfClientId client_id = client_id_;
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (!log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel)),
                              "StartOrUpdateComposition::QueryInterface(ITfInsertAtSelection)") || !insert_at_sel) {
            return {};
        }
        if (!IsCompositionEditCurrent(epoch, context.get(), nullptr)) return {};
        ComPtr<ITfRange> insert_range;
        if (!log::CheckHResult(insert_at_sel->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &insert_range),
                "StartOrUpdateComposition::InsertTextAtSelection(TF_IAS_QUERYONLY)") || !insert_range) {
            return {};
        }
        if (!IsCompositionEditCurrent(epoch, context.get(), nullptr)) return {};

        ComPtr<ITfContextComposition> ctx_comp;
        if (!log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&ctx_comp)),
                              "StartOrUpdateComposition::QueryInterface(ITfContextComposition)") || !ctx_comp) {
            return {};
        }
        if (!IsCompositionEditCurrent(epoch, context.get(), nullptr)) return {};
        // Track the unpublished result so removal/termination during StartComposition is remembered.
        const auto starting = std::make_shared<CompositionCleanup>();
        starting->context = context.get();
        composition_cleanups_.push_back(starting);
        const HRESULT start_hr = ctx_comp->StartComposition(ec, insert_range.Get(), this, &starting->composition);
        const bool start_ok = log::CheckHResult(start_hr, "StartOrUpdateComposition::StartComposition");
        starting->finished = true;
        if (!IsCompositionEditCurrent(epoch, context.get(), nullptr) ||
            starting->context_removed || starting->terminated) {
            QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=stale-composition-start, activation={}, context={}, "
                           "context_removed={}, terminated={})",
                           service_log_id_, epoch.activation, epoch.context, starting->context_removed, starting->terminated);
            if (starting->composition && !starting->context_removed && !starting->terminated) {
                ScheduleCompositionCleanup(starting->context, std::move(starting->composition), client_id);
            }
            if (starting->terminated && IsCompositionEditCurrent(epoch, context.get(), nullptr)) {
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
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return composition;
    SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), text);
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return composition;

    // Apply dotted underline display attribute to the composition range
    if (display_attribute_atom_ != TF_INVALID_GUIDATOM) {
        ComPtr<ITfProperty> disp_prop;
        if (log::CheckHResult(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop),
                             "StartOrUpdateComposition::GetProperty(GUID_PROP_ATTRIBUTE)") && disp_prop) {
            if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return composition;
            const VARIANT var = MakeInt4Variant(gsl::narrow_cast<LONG>(display_attribute_atom_));
            std::ignore = log::CheckHResult(disp_prop->SetValue(ec, comp_range.Get(), &var),
                                           "StartOrUpdateComposition::SetValue(GUID_PROP_ATTRIBUTE)");
        }
    }
    return composition;
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    const auto epoch = edit_session_epoch_;
    const auto composition = composition_;
    if (composition) {
        ComPtr<ITfRange> comp_range;
        if (log::CheckHResult(composition->GetRange(&comp_range), "CommitComposition::GetRange") && comp_range) {
            if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
            std::ignore = ClearCompositionDisplayAttribute(
                ec, context, gsl::not_null<ITfRange*>(comp_range.Get()),
                [this, epoch, &context, &composition]() noexcept {
                    return IsCompositionEditCurrent(epoch, context.get(), composition.Get());
                });
            if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
            SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), committed_text);
        }
        if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
        composition_.Reset();
        has_valid_caret_pos_ = false;
        std::ignore = log::CheckHResult(composition->EndComposition(ec), "CommitComposition::EndComposition");
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel)),
                             "DirectCommit::QueryInterface(ITfInsertAtSelection)") && insert_at_sel) {
            if (!IsCompositionEditCurrent(epoch, context.get(), nullptr)) return;
            ComPtr<ITfRange> inserted_range;
            std::ignore = log::CheckHResult(insert_at_sel->InsertTextAtSelection(
                    ec, 0, committed_text.c_str(), gsl::narrow_cast<LONG>(committed_text.size()), &inserted_range),
                    "DirectCommit::InsertTextAtSelection");
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, const ITfContext* context) {
    const auto epoch = edit_session_epoch_;
    const auto composition = composition_;
    if (!composition) {
        has_valid_caret_pos_ = false;
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (log::CheckHResult(composition->GetRange(&comp_range), "CancelComposition::GetRange") && comp_range) {
        if (!IsCompositionEditCurrent(epoch, context, composition.Get())) return;
        std::ignore = log::CheckHResult(comp_range->SetText(ec, 0, L"", 0), "CancelComposition::SetText");
    }
    if (!IsCompositionEditCurrent(epoch, context, composition.Get())) return;
    composition_.Reset();
    has_valid_caret_pos_ = false;
    std::ignore = log::CheckHResult(composition->EndComposition(ec), "CancelComposition::EndComposition");
}

void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    if (!composition_ || !candidate_ui_) {
        return;
    }
    const auto epoch = edit_session_epoch_;
    const auto composition = composition_;
    ComPtr<ITfContextView> view;
    ComPtr<ITfRange> comp_range;
    RECT rc{};
    BOOL clipped = FALSE;
    const bool got_position =
        log::CheckHResult(context->GetActiveView(&view), "CandidatePosition::GetActiveView", log::Level::debug) && view &&
        IsCompositionEditCurrent(epoch, context.get(), composition.Get()) &&
        log::CheckHResult(composition->GetRange(&comp_range), "CandidatePosition::GetRange", log::Level::debug) && comp_range &&
        IsCompositionEditCurrent(epoch, context.get(), composition.Get()) &&
        log::CheckHResult(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped),
                         "CandidatePosition::GetTextExt", log::Level::trace);
    if (!IsCompositionEditCurrent(epoch, context.get(), composition.Get())) return;
    if (got_position) {
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
    if (!candidate_ui_ || !thread_mgr_ || !active_context_ || !is_active_ || resetting_context_) {
        return;
    }
    const auto epoch = edit_session_epoch_;
    const auto context = active_context_;

    candidate_ui_->SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index);
    if (!IsEditSessionCurrent(epoch, context.Get())) return;

    ComPtr<ITfUIElementMgr> ui_mgr;
    const HRESULT manager_hr = thread_mgr_.As(&ui_mgr);
    const bool manager_ok = log::CheckHResult(
        manager_hr, "BeginOrUpdateCandidateUI::QueryInterface(ITfUIElementMgr)", log::Level::debug);
    if (!IsEditSessionCurrent(epoch, context.Get())) return;
    if (!manager_ok || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; showing popup directly");
        candidate_ui_->Show(TRUE);
        return;
    }

    if (candidate_ui_->UiElementId() == TF_INVALID_UIELEMENTID) {
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        const HRESULT begin_hr = ui_mgr->BeginUIElement(candidate_ui_.Get(), &show_own_ui, &element_id);
        const bool begin_ok = log::CheckHResult(begin_hr, "BeginOrUpdateCandidateUI::BeginUIElement");
        if (!IsEditSessionCurrent(epoch, context.Get())) {
            QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=ui-begin-stale, activation={}, context={}, "
                           "element_id={}, begin_ok={})",
                           service_log_id_, epoch.activation, epoch.context, element_id, begin_ok);
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
            QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={})",
                           element_id, show_own_ui != FALSE, show_popup);
            candidate_ui_->Show(show_popup ? TRUE : FALSE);
            if (!IsEditSessionCurrent(epoch, context.Get())) return;
            std::ignore = log::CheckHResult(ui_mgr->UpdateUIElement(element_id), "ITfUIElementMgr::UpdateUIElement");
        } else {
            QIQI_LOG_WARN("BeginOrUpdateCandidateUI: BeginUIElement failed; falling back to popup window");
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
    QIQI_LOG_DEBUG("TsfLifecycle(service={}, stage=ui-end, element_id={}, has_manager={})",
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
