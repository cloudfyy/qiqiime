#include "ime/tsf/composition_cleanup.h"
#include "ime/tsf/composition_attribute.h"
#include "ime/tsf/edit_session.h"
#include <algorithm>
#include <gsl/gsl>

namespace qiqi {

CompositionCleanup::~CompositionCleanup() noexcept {
    if (id == 0) return;
    const log::Opcode opcode = finished ? log::Opcode::Info : log::Opcode::Stop;
    QIQI_LOG_CTX(
        log::Level::debug,
        (log::LogContext{log::Keyword::Composition, opcode, &activity_id, nullptr}),
        "TsfCleanup(service={}, cleanup={}, stage=released, async_requested={}, started={}, finished={}, terminated={}, context_removed={})",
        service_id, id, async_requested, started, finished, terminated, context_removed);
}

void CompositionCleanup::LogResult(
    std::string_view stage, HRESULT hr, log::Opcode opcode) const noexcept {
    const log::LogContext ctx{log::Keyword::Composition, opcode, &activity_id, nullptr};
    if (FAILED(hr)) {
        QIQI_LOG_CTX(log::Level::warn, ctx, "TsfCleanup(service={}, cleanup={}, stage={}, hr=0x{:08x})",
                     service_id, id, stage, gsl::narrow_cast<unsigned int>(hr));
    } else {
        QIQI_LOG_CTX(log::Level::debug, ctx, "TsfCleanup(service={}, cleanup={}, stage={}, hr=0x{:08x})",
                     service_id, id, stage, gsl::narrow_cast<unsigned int>(hr));
    }
}

HRESULT ExecuteCompositionCleanup(std::shared_ptr<CompositionCleanup> cleanup, TfEditCookie ec) {
    Expects(cleanup != nullptr && cleanup->context != nullptr && cleanup->composition != nullptr);
    cleanup->started = true;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=callback-enter, async_requested={}, terminated={}, has_range={})",
        cleanup->service_id, cleanup->id, cleanup->async_requested, cleanup->terminated, cleanup->range != nullptr);
    const auto finish = gsl::finally([&cleanup]() noexcept {
        cleanup->finished = true;
        QIQI_LOG_ACT_STOP(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=callback-exit, terminated={})",
            cleanup->service_id, cleanup->id, cleanup->terminated);
    });
    if (cleanup->context_removed) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=skip-removed-context)",
            cleanup->service_id, cleanup->id);
        return S_OK;
    }
    if (cleanup->terminated) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=skip-host-terminated)",
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
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage={})",
            cleanup->service_id, cleanup->id,
            cleanup->context_removed ? "skip-end-after-context-removal" : "skip-end-after-host-termination");
        return S_OK;
    }
    const HRESULT hr = cleanup->composition->EndComposition(ec);
    cleanup->LogResult("end-composition", hr);
    std::ignore = log::CheckHResult(
        hr, "DeactivateComposition::EndComposition", log::Level::warn,
        log::LogContext{log::Keyword::Composition, log::Opcode::Info, &cleanup->activity_id, nullptr});
    return hr;
}

void RequestCompositionCleanup(std::shared_ptr<CompositionCleanup> cleanup, TfClientId client_id) {
    Expects(cleanup != nullptr && cleanup->context != nullptr && cleanup->composition != nullptr &&
            client_id != TF_CLIENTID_NULL);
    const HRESULT range_hr = cleanup->composition->GetRange(&cleanup->range);
    cleanup->LogResult("get-range", range_hr);
    if (log::CheckHResult(range_hr, "DeactivateComposition::GetRange", log::Level::warn,
                          log::LogContext{log::Keyword::Composition, log::Opcode::Info, &cleanup->activity_id, nullptr}) &&
        !cleanup->range) {
        QIQI_LOG_ACT_INFO(
            log::Level::warn,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "DeactivateComposition::GetRange returned no range (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(range_hr));
    }
    if (cleanup->context_removed || cleanup->terminated) {
        cleanup->finished = true;
        QIQI_LOG_ACT_STOP(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=skip-request-invalidated, terminated={}, context_removed={})",
            cleanup->service_id, cleanup->id, cleanup->terminated, cleanup->context_removed);
        return;
    }
    const auto session = MakeComObject<LambdaEditSession>(
        [cleanup](TfEditCookie ec) -> HRESULT {
            return ExecuteCompositionCleanup(cleanup, ec);
        });
    HRESULT session_hr = E_FAIL;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=sync-request, flags=0x{:x})",
        cleanup->service_id, cleanup->id, TF_ES_SYNC | TF_ES_READWRITE);
    const HRESULT request_hr = cleanup->context->RequestEditSession(
        client_id, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=sync-result, request_hr=0x{:08x}, session_hr=0x{:08x}, started={}, terminated={})",
        cleanup->service_id, cleanup->id, gsl::narrow_cast<unsigned int>(request_hr),
        gsl::narrow_cast<unsigned int>(session_hr), cleanup->started, cleanup->terminated);
    if (SUCCEEDED(request_hr) && session_hr == TF_E_SYNCHRONOUS &&
        !cleanup->started && !cleanup->terminated && !cleanup->context_removed) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "DeactivateComposition: synchronous access refused; requesting asynchronous cleanup (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(session_hr));
        session_hr = E_FAIL;
        cleanup->async_requested = true;
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=async-request, flags=0x{:x})",
            cleanup->service_id, cleanup->id, TF_ES_ASYNC | TF_ES_READWRITE);
        const HRESULT async_hr = cleanup->context->RequestEditSession(
            client_id, session.Get(), TF_ES_ASYNC | TF_ES_READWRITE, &session_hr);
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=async-result, request_hr=0x{:08x}, session_hr=0x{:08x}, queued={})",
            cleanup->service_id, cleanup->id, gsl::narrow_cast<unsigned int>(async_hr),
            gsl::narrow_cast<unsigned int>(session_hr), SUCCEEDED(async_hr) && session_hr == TF_S_ASYNC);
        if (FAILED(async_hr) || FAILED(session_hr)) {
            cleanup->finished = true;
            cleanup->LogResult("async-rejected", FAILED(async_hr) ? async_hr : session_hr, log::Opcode::Stop);
        }
        const log::LogContext cleanup_ctx{log::Keyword::Composition, log::Opcode::Info, &cleanup->activity_id, nullptr};
        if (log::CheckHResult(async_hr, "DeactivateComposition::RequestEditSession(TF_ES_ASYNC | TF_ES_READWRITE)",
                              log::Level::warn, cleanup_ctx)) {
            std::ignore = log::CheckHResult(session_hr, "DeactivateComposition::AsyncEditSession",
                                            log::Level::warn, cleanup_ctx);
        }
        return;
    }
    if (FAILED(request_hr) || FAILED(session_hr)) {
        cleanup->finished = true;
        cleanup->LogResult("sync-failed-no-fallback", FAILED(request_hr) ? request_hr : session_hr, log::Opcode::Stop);
    }
    const log::LogContext cleanup_ctx{log::Keyword::Composition, log::Opcode::Info, &cleanup->activity_id, nullptr};
    if (log::CheckHResult(request_hr, "DeactivateComposition::RequestEditSession(TF_ES_SYNC | TF_ES_READWRITE)",
                          log::Level::warn, cleanup_ctx)) {
        std::ignore = log::CheckHResult(session_hr, "DeactivateComposition::DoEditSession",
                                        log::Level::warn, cleanup_ctx);
    }
}

bool HandleCompositionCleanupTermination(
    const CompositionCleanupRegistry& registry, TfEditCookie ec, const ITfComposition* composition,
    uint64_t service_id, const GUID& lifecycle_activity) {
    const auto cleanup = registry.FindComposition(composition);
    if (!cleanup) return false;
    if (cleanup->id == 0) {
        cleanup->terminated = true;
        cleanup->finished = true;
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &lifecycle_activity,
            "TsfLifecycle(service={}, stage=composition-start-terminated)",
            service_id);
        return true;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=host-terminated, started={}, finished={}, duplicate={})",
        cleanup->service_id, cleanup->id, cleanup->started, cleanup->finished, cleanup->terminated);
    if (cleanup->terminated) return true;
    cleanup->terminated = true;
    if (cleanup->context_removed) return true;
    if (!cleanup->attribute_clear_started && cleanup->range) {
        const HRESULT clear_hr = ClearCompositionDisplayAttribute(
            ec, gsl::not_null<ITfContext*>(cleanup->context.Get()), gsl::not_null<ITfRange*>(cleanup->range.Get()),
            [&cleanup]() noexcept { return !cleanup->context_removed; }, &cleanup->attribute_clear_started);
        cleanup->LogResult("host-attribute-clear", clear_hr);
    }
    if (!cleanup->started) cleanup->finished = true;
    return true;
}

void CompositionCleanupRegistry::Track(const std::shared_ptr<CompositionCleanup>& cleanup) {
    Expects(cleanup != nullptr);
    records_.push_back(cleanup);
}

std::shared_ptr<CompositionCleanup> CompositionCleanupRegistry::FindComposition(
    const ITfComposition* composition) const noexcept {
    if (!composition) return {};
    for (const auto& pending : records_) {
        const auto cleanup = pending.lock();
        if (cleanup && cleanup->composition.Get() == composition) return cleanup;
    }
    return {};
}

std::shared_ptr<CompositionCleanup> CompositionCleanupRegistry::FindPending(const ITfContext* context) noexcept {
    std::erase_if(records_, [](const auto& pending) {
        const auto cleanup = pending.lock();
        return !cleanup || cleanup->finished;
    });
    for (const auto& pending : records_) {
        const auto cleanup = pending.lock();
        if (cleanup && cleanup->context.Get() == context) return cleanup;
    }
    return {};
}

void CompositionCleanupRegistry::MarkContextRemoved(
    const ITfContext* context, uint64_t service_id, const GUID& lifecycle_activity) const noexcept {
    for (const auto& pending : records_) {
        const auto cleanup = pending.lock();
        if (cleanup && cleanup->context.Get() == context) {
            cleanup->context_removed = true;
            cleanup->finished = true;
            if (cleanup->id == 0) {
                QIQI_LOG_ACT_INFO(
                    log::Level::debug,
                    log::Keyword::Lifecycle,
                    &lifecycle_activity,
                    "TsfLifecycle(service={}, stage=composition-start-removed)",
                    service_id);
                continue;
            }
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::Composition,
                &cleanup->activity_id,
                "TsfCleanup(service={}, cleanup={}, stage=context-removed, started={})",
                cleanup->service_id, cleanup->id, cleanup->started);
        }
    }
}

std::shared_ptr<CompositionCleanup> CompositionCleanupCoordinator::TrackStarting(gsl::not_null<ITfContext*> context) {
    const auto starting = std::make_shared<CompositionCleanup>();
    starting->context = context.get();
    registry_.Track(starting);
    return starting;
}

void CompositionCleanupCoordinator::Schedule(
    Microsoft::WRL::ComPtr<ITfContext> context, Microsoft::WRL::ComPtr<ITfComposition> composition,
    TfClientId client_id, const EditSessionEpoch& epoch, const GUID& lifecycle_activity) {
    const auto cleanup = std::make_shared<CompositionCleanup>();
    cleanup->service_id = service_id_;
    cleanup->id = ++next_cleanup_id_;
    cleanup->activity_id = log::CreateActivityId();
    cleanup->parent_activity_id = lifecycle_activity;
    QIQI_LOG_ACT_START(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        &cleanup->parent_activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=created, activation={}, context={}, client_id={})",
        cleanup->service_id, cleanup->id, epoch.activation, epoch.context, client_id);
    cleanup->composition = std::move(composition);
    cleanup->context = std::move(context);
    if (!cleanup->context || client_id == TF_CLIENTID_NULL) {
        cleanup->finished = true;
        QIQI_LOG_ACT_STOP(
            log::Level::warn,
            log::Keyword::Composition,
            &cleanup->activity_id,
            "TsfCleanup(service={}, cleanup={}, stage=unavailable, has_context={}, valid_client={})",
            cleanup->service_id, cleanup->id, cleanup->context != nullptr, client_id != TF_CLIENTID_NULL);
        return;
    }
    std::ignore = HasPending(nullptr, lifecycle_activity);
    registry_.Track(cleanup);
    RequestCompositionCleanup(cleanup, client_id);
}

bool CompositionCleanupCoordinator::HasPending(
    const ITfContext* context, const GUID& lifecycle_activity) noexcept {
    const auto cleanup = registry_.FindPending(context);
    if (!cleanup) return false;
    if (cleanup->id == 0) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &lifecycle_activity,
            "TsfLifecycle(service={}, stage=composition-start-write-deferred)",
            service_id_);
        return true;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &cleanup->activity_id,
        "TsfCleanup(service={}, cleanup={}, stage=write-forced-async)",
        cleanup->service_id, cleanup->id);
    return true;
}

void CompositionCleanupCoordinator::MarkContextRemoved(
    const ITfContext* context, const GUID& lifecycle_activity) const noexcept {
    registry_.MarkContextRemoved(context, service_id_, lifecycle_activity);
}

bool CompositionCleanupCoordinator::HandleTermination(
    TfEditCookie ec, const ITfComposition* composition, const GUID& lifecycle_activity) const {
    return HandleCompositionCleanupTermination(registry_, ec, composition, service_id_, lifecycle_activity);
}

} // namespace qiqi
