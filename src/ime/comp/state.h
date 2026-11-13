// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

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
    // Callbacks and reset scopes borrow this state; its address must remain stable.
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
    // Declaration order is a lifetime contract; do not reorder these four members.
    // Reverse destruction releases document, context, cleanups, then composition.
    // COM Release may reenter; later-destroyed members must remain alive during earlier releases.
    // TestLifecycleStateReleaseOrder guards COM release order in tests/ime/comp/state.cpp.
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

inline bool CheckCompositionEditSessionCurrent(
    const CompositionLifecycleState& state, EditSessionTicket ticket,
    uint64_t service_id, const GUID& activity) noexcept {
    if (state.IsCurrent(ticket)) {
        return true;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &activity,
        "TsfLifecycle(service={}, stage=stale-edit-skipped, activation={}/{}, context={}/{}, active={}, target_matches={})",
        service_id, ticket.epoch.activation, state.Epoch().activation,
        ticket.epoch.context, state.Epoch().context, state.IsActive(), ticket.target == state.Context().Get());
    return false;
}

inline bool CheckCompositionEditCurrent(
    const CompositionLifecycleState& state, EditSessionTicket ticket, const ITfComposition* composition,
    uint64_t service_id, const GUID& activity) noexcept {
    if (!CheckCompositionEditSessionCurrent(state, ticket, service_id, activity)) return false;
    if (state.Composition().Get() == composition) return true;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Composition,
        &activity,
        "TsfLifecycle(service={}, stage=replaced-composition-edit-skipped, activation={}, context={})",
        service_id, ticket.epoch.activation, ticket.epoch.context);
    return false;
}

// Synchronous identity only; the caller retains the service and original context.
class ContextEditGuard {
public:
    ContextEditGuard(
        const CompositionLifecycleState& state, EditSessionTicket ticket,
        uint64_t service_id, const GUID& activity) noexcept
        : state_(state), ticket_(ticket), service_id_(service_id), activity_(activity) {}
    ContextEditGuard(const ContextEditGuard&) = delete;
    ContextEditGuard& operator=(const ContextEditGuard&) = delete;
    ContextEditGuard(ContextEditGuard&&) = delete;
    ContextEditGuard& operator=(ContextEditGuard&&) = delete;

    [[nodiscard]] bool Current() const noexcept {
        return CheckCompositionEditSessionCurrent(state_, ticket_, service_id_, activity_);
    }
    [[nodiscard]] bool CurrentComposition(const ITfComposition* expected) const noexcept {
        return CheckCompositionEditCurrent(state_, ticket_, expected, service_id_, activity_);
    }

private:
    const CompositionLifecycleState& state_;
    const EditSessionTicket ticket_;
    const uint64_t service_id_;
    const GUID& activity_;
};

// Retains one fixed composition, including an explicitly expected empty slot.
template <typename IsCurrent>
class CompositionEditGuard {
public:
    CompositionEditGuard(Microsoft::WRL::ComPtr<ITfComposition> composition, IsCurrent is_current)
        : composition_(std::move(composition)), is_current_(std::move(is_current)) {}
    CompositionEditGuard(const CompositionEditGuard&) = delete;
    CompositionEditGuard& operator=(const CompositionEditGuard&) = delete;
    CompositionEditGuard(CompositionEditGuard&&) = delete;
    CompositionEditGuard& operator=(CompositionEditGuard&&) = delete;

    [[nodiscard]] bool Current() const noexcept(noexcept(is_current_(composition_.Get()))) {
        return is_current_(composition_.Get());
    }
    [[nodiscard]] bool operator()() const noexcept(noexcept(Current())) { return Current(); }
    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfComposition>& Composition() const noexcept {
        return composition_;
    }

private:
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    IsCurrent is_current_;
};

inline void ScheduleCurrentCompositionCleanup(
    CompositionLifecycleState& state, const TfClientId& client_id, uint64_t service_id, const GUID& activity) {
    if (!state.Composition()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &activity,
            "TsfLifecycle(service={}, stage=cleanup-not-needed, activation={}, context={})",
            service_id, state.Epoch().activation, state.Epoch().context);
        return;
    }
    state.Cleanups().Schedule(
        state.Context(), state.TakeComposition(), client_id, state.Epoch(), activity);
}

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
