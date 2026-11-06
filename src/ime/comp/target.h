#pragma once
#include "common/com_object.h"
#include "common/logger.h"
#include "ime/comp/state.h"
#include <cstdint>
#include <gsl/gsl>
#include <wrl/client.h>
#include <utility>

namespace qiqi {

// Entry gates and activity restoration remain in the service; borrowed slots must outlive this call.
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

} // namespace qiqi
