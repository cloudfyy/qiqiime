#pragma once
#include "ime/comp/cleanup.h"
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

enum class CompositionEditDisposition { Discard, Requeue, Execute };

inline void RequestCompositionEditSession(
    gsl::not_null<ITfContext*> context, gsl::not_null<ITfEditSession*> session, TfClientId client_id,
    CompositionCleanupCoordinator& cleanups, const GUID& lifecycle_activity, const GUID& edit_act_id) {
    HRESULT session_hr = S_OK;
    const HRESULT req_hr = context->RequestEditSession(
        client_id,
        session.get(),
        (cleanups.HasPending(context.get(), lifecycle_activity) ? TF_ES_ASYNC : TF_ES_ASYNCDONTCARE) |
            TF_ES_READWRITE,
        &session_hr);
    if (FAILED(req_hr) || FAILED(session_hr)) {
        QIQI_LOG_ACT_STOP(
            log::Level::warn,
            log::Keyword::Composition,
            &edit_act_id,
            "RequestEditSession failed (req_hr=0x{:08x}, session_hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(req_hr),
            gsl::narrow_cast<unsigned int>(session_hr));
    }
}

// Synchronous scope: borrows the task and activity slot, never the service or edit cookie.
class CompositionEditScope {
public:
    CompositionEditScope(const CompositionEditTask& task, GUID& activity) noexcept
        : task_(task), activity_(activity), previous_activity_(std::exchange(activity, task.Activity())) {}
    ~CompositionEditScope() noexcept { activity_ = previous_activity_; }
    CompositionEditScope(const CompositionEditScope&) = delete;
    CompositionEditScope& operator=(const CompositionEditScope&) = delete;
    CompositionEditScope(CompositionEditScope&&) = delete;
    CompositionEditScope& operator=(CompositionEditScope&&) = delete;

    template <typename IsCurrent>
    [[nodiscard]] CompositionEditDisposition Check(
        CompositionCleanupCoordinator& cleanups, IsCurrent is_current) const {
        if (!is_current(task_.Ticket())) return CompositionEditDisposition::Discard;
        return cleanups.HasPending(task_.Context().Get(), activity_)
            ? CompositionEditDisposition::Requeue : CompositionEditDisposition::Execute;
    }

private:
    const CompositionEditTask& task_;
    GUID& activity_;
    const GUID previous_activity_;
};

} // namespace qiqi
