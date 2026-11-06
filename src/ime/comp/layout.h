#pragma once
#include "common/logger.h"
#include "ime/tsf/edit_session.h"
#include <cstdint>
#include <gsl/gsl>
#include <wrl/client.h>

namespace qiqi {

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
