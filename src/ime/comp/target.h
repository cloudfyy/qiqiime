// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/com_object.h"
#include "common/logger.h"
#include "ime/comp/state.h"
#include <cstdint>
#include <functional>
#include <gsl/gsl>
#include <string_view>
#include <wrl/client.h>
#include <utility>

namespace qiqi {

[[nodiscard]] inline bool MatchesCompositionDocument(
    ITfDocumentMgr* document, const CompositionLifecycleState& state) {
    if (!document || !state.Document()) return false;
    if (document == state.Document().Get()) return true;
    Microsoft::WRL::ComPtr<IUnknown> current_identity;
    Microsoft::WRL::ComPtr<IUnknown> incoming_identity;
    if (!log::CheckHResult(state.Document().As(&current_identity), "IsActiveDocument::CurrentIdentity") ||
        !log::CheckHResult(document->QueryInterface(IID_PPV_ARGS(&incoming_identity)),
                          "IsActiveDocument::IncomingIdentity")) {
        return false;
    }
    return current_identity == incoming_identity;
}

template <typename Reset, typename IsCurrent>
[[nodiscard]] bool ReconcileCompositionDocumentTarget(
    ITfDocumentMgr* document, std::string_view source, CompositionLifecycleState& state,
    uint64_t service_id, const GUID& activity, Reset reset, IsCurrent is_current) {
    if (!state.Context() || !document) return true;
    const auto context = state.Context();
    const EditSessionTicket ticket{state.Epoch(), context.Get()};
    Microsoft::WRL::ComPtr<ITfContext> top;
    Microsoft::WRL::ComPtr<ITfContext> base;
    const HRESULT top_hr = document->GetTop(&top);
    const HRESULT base_hr = document->GetBase(&base);
    const bool top_ok = log::CheckHResult(top_hr, "ReconcileDocumentTarget::GetTop");
    const bool base_ok = log::CheckHResult(base_hr, "ReconcileDocumentTarget::GetBase");
    if (!is_current(ticket)) return false;
    const bool retained = top_ok && base_ok && (top == context || base == context);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &activity,
        "TsfLifecycle(service={}, stage=document-target, source={}, retained={}, covered_by_modal={})",
        service_id,
        source,
        retained,
        retained && top != context);
    if (!retained) {
        EditSessionTicket expected{state.Epoch(), nullptr};
        ++expected.epoch.context;
        reset();
        return is_current(expected);
    } else {
        state.SetDocument(document);
    }
    return true;
}

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

template <typename Reset, typename IsCurrent>
[[nodiscard]] bool PrepareCompositionInputTarget(
    ITfContext* context, CompositionLifecycleState& state,
    Microsoft::WRL::ComPtr<ITfContext>& preparing_context, ScopedTfSinkConnection& layout_sink,
    gsl::not_null<ITfTextLayoutSink*> sink, uint64_t service_id, GUID& activity,
    Reset reset, IsCurrent is_current) {
    return EnterCompositionTargetPreparation(
        context, state, preparing_context.Get(), activity, service_id,
        [&](gsl::not_null<ITfContext*> target, const GUID& prepare_act_id) {
            return PrepareCompositionTarget(
                target, state, preparing_context, layout_sink, sink, service_id, prepare_act_id,
                std::ref(reset), std::ref(is_current));
        });
}

} // namespace qiqi
