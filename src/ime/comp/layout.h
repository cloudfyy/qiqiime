#pragma once
#include "common/logger.h"
#include "ime/tsf/edit_session.h"
#include <cstdint>
#include <gsl/gsl>
#include <wrl/client.h>
#include <utility>

namespace qiqi {

// Deferred layout owns its target and optional notification view, never a service or cookie.
class CompositionLayoutTask {
public:
    CompositionLayoutTask(
        Microsoft::WRL::ComPtr<ITfContext> context, Microsoft::WRL::ComPtr<ITfContextView> view,
        EditSessionTicket ticket, GUID activity) noexcept
        : context_(std::move(context)), view_(std::move(view)), ticket_(ticket), activity_(activity) {
        Expects(context_ != nullptr && ticket_.target == context_.Get());
    }
    ~CompositionLayoutTask() = default;
    CompositionLayoutTask(const CompositionLayoutTask&) = default;
    CompositionLayoutTask(CompositionLayoutTask&&) noexcept = default;
    CompositionLayoutTask& operator=(const CompositionLayoutTask&) = delete;
    CompositionLayoutTask& operator=(CompositionLayoutTask&&) = delete;

    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfContext>& Context() const noexcept { return context_; }
    [[nodiscard]] const Microsoft::WRL::ComPtr<ITfContextView>& View() const noexcept { return view_; }
    [[nodiscard]] EditSessionTicket Ticket() const noexcept { return ticket_; }
    [[nodiscard]] const GUID& Activity() const noexcept { return activity_; }

private:
    Microsoft::WRL::ComPtr<ITfContext> context_;
    Microsoft::WRL::ComPtr<ITfContextView> view_;
    EditSessionTicket ticket_;
    GUID activity_;
};

template <typename IsCurrent, typename UpdatePosition>
HRESULT ExecuteCompositionLayoutInSession(
    TfEditCookie ec, const CompositionLayoutTask& task, GUID& activity, uint64_t service_id,
    IsCurrent is_current, UpdatePosition update_position) {
    const auto ticket = task.Ticket();
    const auto& layout_act_id = task.Activity();
    const GUID prev_act = std::exchange(activity, layout_act_id);
    const auto restore_act = gsl::finally([&activity, prev_act]() noexcept {
        activity = prev_act;
    });
    if (!is_current(ticket)) {
        return S_OK;
    }
    QIQI_LOG_ACT_STOP(
        log::Level::debug,
        log::Keyword::UI,
        &layout_act_id,
        "CandidatePosition(service={}, stage=layout-callback, activation={}, context={})",
        service_id, ticket.epoch.activation, ticket.epoch.context);
    update_position(ec, gsl::not_null<ITfContext*>(task.Context().Get()), task.View());
    return S_OK;
}

inline void RequestCompositionLayoutSession(
    gsl::not_null<ITfContext*> context, gsl::not_null<ITfEditSession*> session, TfClientId client_id,
    EditSessionTicket ticket, uint64_t service_id, const GUID& layout_act_id) {
    HRESULT session_hr = S_OK;
    const HRESULT request_hr = context->RequestEditSession(
        client_id, session.get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &layout_act_id,
        "CandidatePosition(service={}, stage=layout-request, activation={}, context={}, request_hr=0x{:08x}, session_hr=0x{:08x})",
        service_id, ticket.epoch.activation, ticket.epoch.context,
        gsl::narrow_cast<unsigned int>(request_hr), gsl::narrow_cast<unsigned int>(session_hr));
    const log::LogContext layout_ctx{log::Keyword::UI, log::Opcode::Info, &layout_act_id, nullptr};
    std::ignore = log::CheckHResult(request_hr, "OnLayoutChange::RequestEditSession", log::Level::debug, layout_ctx);
    if (SUCCEEDED(request_hr)) {
        std::ignore = log::CheckHResult(session_hr, "OnLayoutChange::DoEditSession", log::Level::debug, layout_ctx);
    }
}

// Keep the range alive through the caller's UI work; the caller owns the composition and view.
struct CompositionLayoutQuery {
    Microsoft::WRL::ComPtr<ITfRange> range;
    RECT rect{};
    BOOL clipped = FALSE;
    bool got_position = false;
    bool current = false;
};

template <typename IsCurrent>
[[nodiscard]] CompositionLayoutQuery QueryCompositionLayoutInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfComposition*> composition,
    Microsoft::WRL::ComPtr<ITfContextView>& view, const bool& had_position,
    const EditSessionEpoch& attempted_epoch, uint64_t service_id, const GUID& activity, IsCurrent is_current) {
    const bool notification_view = view != nullptr;
    CompositionLayoutQuery query;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &activity,
        "CandidatePosition(service={}, stage=query, activation={}, context={}, had_position={}, notification_view={})",
        service_id, attempted_epoch.activation, attempted_epoch.context, had_position, notification_view);
    query.got_position =
        (view || log::CheckHResult(context->GetActiveView(&view), "CandidatePosition::GetActiveView", log::Level::debug)) && view &&
        is_current() &&
        log::CheckHResult(composition->GetRange(&query.range), "CandidatePosition::GetRange", log::Level::debug) && query.range &&
        is_current() &&
        log::CheckHResult(view->GetTextExt(ec, query.range.Get(), &query.rect, &query.clipped),
                         "CandidatePosition::GetTextExt", log::Level::trace);
    query.current = is_current();
    if (!query.current) return query;
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &activity,
        "CandidatePosition(service={}, stage=query-result, activation={}, context={}, success={}, rect=({}, {}, {}, {}), clipped={}, had_position={})",
        service_id, attempted_epoch.activation, attempted_epoch.context, query.got_position,
        query.rect.left, query.rect.top, query.rect.right, query.rect.bottom, query.clipped != FALSE, had_position);
    return query;
}

} // namespace qiqi
