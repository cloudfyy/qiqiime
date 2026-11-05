#include "ime/tsf/composition_cleanup.h"
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
