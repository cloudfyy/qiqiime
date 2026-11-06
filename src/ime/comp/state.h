#pragma once
#include "common/com_object.h"
#include "common/logger.h"
#include "ime/comp/cleanup.h"
#include "ime/tsf/edit_session.h"
#include <wrl/client.h>
#include <cstdint>
#include <utility>

namespace qiqi {

class CompositionContextResetScope;

// STA state only; activation/preparation entry gates, host scheduling, and UI policy stay in the service.
class CompositionLifecycleState {
public:
    explicit CompositionLifecycleState(uint64_t service_id) noexcept : cleanups_(service_id) {}
    ~CompositionLifecycleState() = default;
    CompositionLifecycleState(const CompositionLifecycleState&) = delete;
    CompositionLifecycleState& operator=(const CompositionLifecycleState&) = delete;
    CompositionLifecycleState(CompositionLifecycleState&&) = delete;
    CompositionLifecycleState& operator=(CompositionLifecycleState&&) = delete;

    [[nodiscard]] bool IsActive() const noexcept { return active_; }
    void SetActive(bool active) noexcept { active_ = active; }
    [[nodiscard]] const EditSessionEpoch& Epoch() const noexcept { return epoch_; }
    void InvalidateActivation() noexcept { ++epoch_.activation; }
    void InvalidateContext() noexcept { ++epoch_.context; }

    [[nodiscard]] bool IsResettingContext() const noexcept { return resetting_context_; }
    [[nodiscard]] bool IsCurrent(EditSessionTicket ticket) const noexcept {
        return active_ && !resetting_context_ && ticket.Matches(epoch_, context_.Get());
    }

    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfComposition>& Composition() const noexcept { return composition_; }
    void SetComposition(Microsoft::WRL::ComPtr<ITfComposition> composition) noexcept {
        composition_ = std::move(composition);
    }
    [[nodiscard]] Microsoft::WRL::ComPtr<ITfComposition> TakeComposition() noexcept {
        return std::move(composition_);
    }
    void ClearComposition() { composition_.Reset(); }

    [[nodiscard]] CompositionCleanupCoordinator& Cleanups() noexcept { return cleanups_; }
    [[nodiscard]] const CompositionCleanupCoordinator& Cleanups() const noexcept { return cleanups_; }
    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfContext>& Context() const noexcept { return context_; }
    void SetContext(Microsoft::WRL::ComPtr<ITfContext> context) noexcept { context_ = std::move(context); }
    void ClearContext() { context_.Reset(); }
    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfDocumentMgr>& Document() const noexcept { return document_; }
    void SetDocument(Microsoft::WRL::ComPtr<ITfDocumentMgr> document) noexcept { document_ = std::move(document); }
    void ClearDocument() { document_.Reset(); }

private:
    friend class CompositionContextResetScope;
    EditSessionEpoch epoch_;
    bool active_ = false;
    bool resetting_context_ = false;
    // Keep the original COM/bookkeeping destruction order: document, context, cleanups, composition.
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    CompositionCleanupCoordinator cleanups_;
    Microsoft::WRL::ComPtr<ITfContext> context_;
    Microsoft::WRL::ComPtr<ITfDocumentMgr> document_;
};

class CompositionContextResetScope {
public:
    explicit CompositionContextResetScope(CompositionLifecycleState& state) noexcept : state_(state) {
        // Even a nested reset invalidates callbacks, but only the outer scope owns teardown.
        state_.InvalidateContext();
        if (state_.resetting_context_) return;
        state_.resetting_context_ = true;
        entered_ = true;
    }
    ~CompositionContextResetScope() noexcept {
        if (entered_) state_.resetting_context_ = false;
    }
    CompositionContextResetScope(const CompositionContextResetScope&) = delete;
    CompositionContextResetScope& operator=(const CompositionContextResetScope&) = delete;
    CompositionContextResetScope(CompositionContextResetScope&&) = delete;
    CompositionContextResetScope& operator=(CompositionContextResetScope&&) = delete;

    [[nodiscard]] bool Entered() const noexcept { return entered_; }

private:
    CompositionLifecycleState& state_;
    bool entered_ = false;
};

template <typename ScheduleCleanup, typename EndUI>
void ResetCompositionContextAndUI(
    CompositionLifecycleState& state, bool try_host_cleanup, bool& has_valid_caret_pos,
    ScopedTfSinkConnection& layout_sink, uint64_t service_id, const GUID& activity,
    ScheduleCleanup schedule_cleanup, EndUI end_ui) {
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &activity,
        "TsfLifecycle(service={}, stage=context-reset, activation={}, context_before={}, context_after={}, has_context={}, has_composition={}, try_host_cleanup={})",
        service_id, state.Epoch().activation, state.Epoch().context,
        state.Epoch().context + 1, state.Context() != nullptr, state.Composition() != nullptr, try_host_cleanup);
    const CompositionContextResetScope reset_scope(state);
    if (!reset_scope.Entered()) return;
    if (try_host_cleanup) {
        schedule_cleanup();
    } else if (state.Composition()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &activity,
            "TsfLifecycle(service={}, stage=cleanup-skipped-removed-context)",
            service_id);
    }
    state.ClearContext();
    state.ClearDocument();
    has_valid_caret_pos = false;
    state.ClearComposition();
    layout_sink.Reset();
    end_ui();
}

} // namespace qiqi
