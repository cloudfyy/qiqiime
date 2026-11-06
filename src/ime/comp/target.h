#pragma once
#include "common/com_object.h"
#include "common/logger.h"
#include "ime/comp/state.h"
#include <cstdint>
#include <gsl/gsl>
#include <wrl/client.h>
#include <utility>

namespace qiqi {

// Borrowed slots must outlive this synchronous call.
template <typename Reset, typename IsCurrent>
[[nodiscard]] bool PrepareCompositionTarget(
    gsl::not_null<ITfContext*> context, CompositionLifecycleState& state,
    Microsoft::WRL::ComPtr<ITfContext>& preparing_context, ScopedTfSinkConnection& layout_sink,
    gsl::not_null<ITfTextLayoutSink*> sink, uint64_t service_id, const GUID& prepare_activity,
    Reset reset, IsCurrent is_current) {
    Microsoft::WRL::ComPtr<ITfContext> ctx_holder(context.get());
    preparing_context = ctx_holder;
    const auto finish_prepare = gsl::finally([&preparing_context]() noexcept { preparing_context.Reset(); });
    EditSessionTicket ticket{state.Epoch(), nullptr};
    ++ticket.epoch.context;
    reset();
    if (!is_current(ticket)) return false;
    Microsoft::WRL::ComPtr<ITfDocumentMgr> document;
    const HRESULT document_hr = context->GetDocumentMgr(&document);
    if (!log::CheckHResult(document_hr, "PrepareInputContext::GetDocumentMgr", log::Level::warn,
                           log::LogContext{log::Keyword::Lifecycle, log::Opcode::Info, &prepare_activity, nullptr}) ||
        !document) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &prepare_activity,
            "TsfLifecycle(service={}, stage=input-document-unavailable, hr=0x{:08x})",
            service_id, gsl::narrow_cast<unsigned int>(document_hr));
    }
    if (!is_current(ticket)) return false;
    state.SetContext(ctx_holder);
    state.SetDocument(std::move(document));
    ticket.target = context.get();
    const bool layout_advised = layout_sink.Advise(state.Context().Get(), IID_ITfTextLayoutSink, sink.get());
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &prepare_activity,
        "CandidatePosition(service={}, stage=layout-advise, activation={}, context={}, connected={})",
        service_id, ticket.epoch.activation, ticket.epoch.context, layout_advised);
    if (!is_current(ticket)) {
        // AdviseSink may invalidate the target before its cookie is published.
        layout_sink.Reset();
        return false;
    }
    return true;
}

template <typename Prepare>
[[nodiscard]] bool EnterCompositionTargetPreparation(
    ITfContext* context, const CompositionLifecycleState& state,
    const ITfContext* preparing_context, GUID& activity,
    uint64_t service_id, Prepare prepare) {
    if (!context || !state.IsActive() || state.IsResettingContext() || preparing_context) {
        QIQI_LOG_ACT_INFO(
            log::Level::trace,
            log::Keyword::Lifecycle,
            &activity,
            "TsfLifecycle(service={}, stage=input-target-unavailable)",
            service_id);
        return false;
    }
    if (state.Context().Get() == context) return true;
    const GUID prepare_act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(activity, prepare_act_id);
    const auto restore_act = gsl::finally([&activity, prev_act]() noexcept {
        activity = prev_act;
    });
    return prepare(gsl::not_null<ITfContext*>(context), prepare_act_id);
}

} // namespace qiqi
