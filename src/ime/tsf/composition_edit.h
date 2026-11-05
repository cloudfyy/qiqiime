#pragma once
#include "ime/engine/engine.h"
#include "ime/tsf/edit_session.h"
#include <gsl/gsl>
#include <wrl/client.h>
#include <utility>

namespace qiqi {

// Owns deferred input data, but neither a service nor an edit cookie.
class CompositionEditTask {
public:
    CompositionEditTask(
        Microsoft::WRL::ComPtr<ITfContext> context, const KeyProcessResult& snapshot,
        EditSessionTicket ticket, GUID activity)
        : context_(std::move(context)), snapshot_(snapshot), ticket_(ticket), activity_(activity) {
        Expects(context_ != nullptr && ticket_.target == context_.Get());
    }
    ~CompositionEditTask() = default;
    CompositionEditTask(const CompositionEditTask&) = default;
    CompositionEditTask(CompositionEditTask&&) noexcept = default;
    CompositionEditTask& operator=(const CompositionEditTask&) = delete;
    CompositionEditTask& operator=(CompositionEditTask&&) = delete;

    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfContext>& Context() const noexcept { return context_; }
    [[nodiscard]] const KeyProcessResult& Snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] EditSessionTicket Ticket() const noexcept { return ticket_; }
    [[nodiscard]] const GUID& Activity() const noexcept { return activity_; }

private:
    Microsoft::WRL::ComPtr<ITfContext> context_;
    KeyProcessResult snapshot_;
    EditSessionTicket ticket_;
    GUID activity_;
};

} // namespace qiqi
