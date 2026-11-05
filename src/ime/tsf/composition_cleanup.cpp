#include "ime/tsf/composition_cleanup.h"
#include "ime/tsf/composition_attribute.h"
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

} // namespace qiqi
