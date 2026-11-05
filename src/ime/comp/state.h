#pragma once
#include "ime/comp/cleanup.h"
#include "ime/tsf/edit_session.h"
#include <wrl/client.h>
#include <cstdint>
#include <utility>

namespace qiqi {

// STA state only; the service retains transition guards, host call ordering, and UI policy.
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

    [[nodiscard]] bool IsCurrent(EditSessionTicket ticket, bool resetting) const noexcept {
        return active_ && !resetting && ticket.Matches(epoch_, context_.Get());
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
    EditSessionEpoch epoch_;
    bool active_ = false;
    // Keep the original COM/bookkeeping destruction order: document, context, cleanups, composition.
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    CompositionCleanupCoordinator cleanups_;
    Microsoft::WRL::ComPtr<ITfContext> context_;
    Microsoft::WRL::ComPtr<ITfDocumentMgr> document_;
};

} // namespace qiqi
