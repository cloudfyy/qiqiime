// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <functional>
#include <string>
#include <tuple>
#include <utility>

#include "common/logger.h"
#include "ime/comp/cleanup.h"
#include "ime/comp/state.h"
#include "ime/comp/update.h"

namespace qiqi {

// Keep all preparation references alive through StartComposition and result publication.
struct CompositionStartPreparation {
    Microsoft::WRL::ComPtr<ITfInsertAtSelection> insertion;
    Microsoft::WRL::ComPtr<ITfRange> range;
    Microsoft::WRL::ComPtr<ITfContextComposition> composition_context;
    bool ready = false;
};

// Field order preserves record, then preparation-resource release after publication.
struct CompositionStartAttempt {
    CompositionStartPreparation preparation;
    std::shared_ptr<CompositionCleanup> starting;
    CompositionStartDisposition disposition = CompositionStartDisposition::Discard;
};

template <typename IsCurrent>
[[nodiscard]] CompositionStartPreparation PrepareCompositionStart(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, IsCurrent is_current) {
    CompositionStartPreparation prepared;
    const HRESULT insertion_hr = context->QueryInterface(IID_PPV_ARGS(&prepared.insertion));
    if (!log::CheckHResult(insertion_hr, "StartOrUpdateComposition::QueryInterface(ITfInsertAtSelection)")) {
        return prepared;
    }
    if (!prepared.insertion) {
        QIQI_LOG_WARN("StartOrUpdateComposition::QueryInterface(ITfInsertAtSelection) returned no interface (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(insertion_hr));
        return prepared;
    }
    if (!is_current()) return prepared;
    const HRESULT range_hr = prepared.insertion->InsertTextAtSelection(
        ec, TF_IAS_QUERYONLY, nullptr, 0, &prepared.range);
    if (!log::CheckHResult(range_hr, "StartOrUpdateComposition::InsertTextAtSelection(TF_IAS_QUERYONLY)")) {
        return prepared;
    }
    if (!prepared.range) {
        QIQI_LOG_WARN("StartOrUpdateComposition::InsertTextAtSelection(TF_IAS_QUERYONLY) returned no range (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(range_hr));
        return prepared;
    }
    if (!is_current()) return prepared;
    const HRESULT composition_hr = context->QueryInterface(IID_PPV_ARGS(&prepared.composition_context));
    if (!log::CheckHResult(composition_hr, "StartOrUpdateComposition::QueryInterface(ITfContextComposition)")) {
        return prepared;
    }
    if (!prepared.composition_context) {
        QIQI_LOG_WARN("StartOrUpdateComposition::QueryInterface(ITfContextComposition) returned no interface (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(composition_hr));
        return prepared;
    }
    if (!is_current()) return prepared;
    prepared.ready = true;
    return prepared;
}

// Epoch/activity references stay live across host reentrancy; none are retained in the result.
template <typename IsCurrent>
[[nodiscard]] CompositionStartAttempt BeginCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfCompositionSink*> sink,
    CompositionCleanupCoordinator& cleanups, TfClientId client_id, const EditSessionEpoch& attempted_epoch,
    const EditSessionEpoch& current_epoch, const GUID& lifecycle_activity, IsCurrent is_current) {
    CompositionStartAttempt attempt{.preparation = PrepareCompositionStart(ec, context, std::ref(is_current))};
    if (!attempt.preparation.ready) return attempt;
    attempt.starting = cleanups.TrackStarting(context);
    const HRESULT start_hr = attempt.preparation.composition_context->StartComposition(
        ec, attempt.preparation.range.Get(), sink.get(), &attempt.starting->composition);
    std::ignore = log::CheckHResult(start_hr, "StartOrUpdateComposition::StartComposition");
    attempt.starting->finished = true;
    attempt.disposition = cleanups.ResolveStarting(
        attempt.starting, start_hr, is_current(), client_id, attempted_epoch, current_epoch, lifecycle_activity);
    return attempt;
}

template <typename IsCurrent, typename Reset>
[[nodiscard]] Microsoft::WRL::ComPtr<ITfComposition> EnsureAndUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfCompositionSink*> sink,
    CompositionLifecycleState& state, TfClientId client_id, const EditSessionEpoch& attempted_epoch,
    const GUID& lifecycle_activity, const std::wstring& text, const TfGuidAtom& attribute_atom,
    IsCurrent is_current, Reset reset) {
    if (!state.Composition()) {
        const auto attempt = BeginCompositionInSession(
            ec, context, sink, state.Cleanups(), client_id, attempted_epoch,
            state.Epoch(), lifecycle_activity,
            [&is_current]() noexcept(noexcept(is_current(nullptr))) {
                return is_current(nullptr);
            });
        if (attempt.disposition != CompositionStartDisposition::Publish) {
            if (attempt.disposition == CompositionStartDisposition::Terminated && is_current(nullptr)) {
                reset();
            }
            return {};
        }
        state.SetComposition(std::move(attempt.starting->composition));
    }

    return UpdateCompositionInSession(
        ec, context, state.Composition(), text, attribute_atom, std::ref(is_current));
}

} // namespace qiqi
