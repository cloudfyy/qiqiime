// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "ime/comp/attribute.h"
#include "ime/comp/range.h"
#include "ime/comp/state.h"
#include <functional>
#include <string_view>
#include <utility>

namespace qiqi {

enum class CompositionFinishKind { Commit, Cancel };

// Retain the acquired range through the caller's original termination scope.
struct CompositionTerminationResult {
    Microsoft::WRL::ComPtr<ITfRange> range;
    bool may_reset = false;
};

template <typename IsCurrent>
[[nodiscard]] CompositionTerminationResult ClearTerminatedCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfComposition*> composition,
    IsCurrent is_current) {
    CompositionTerminationResult result;
    const HRESULT range_hr = composition->GetRange(&result.range);
    if (log::CheckHResult(range_hr, "HostTermination::GetRange")) {
        if (result.range) {
            std::ignore = ClearCompositionDisplayAttribute(ec, context, result.range.Get(), is_current);
        } else {
            QIQI_LOG_CAT_WARN(
                log::Keyword::Composition,
                "HostTermination::GetRange returned no range (hr=0x{:08x})",
                gsl::narrow_cast<unsigned int>(range_hr));
        }
    }
    result.may_reset = is_current();
    return result;
}

// The caller has already routed tracked and unrelated composition notifications.
template <typename IsCurrent, typename Reset>
HRESULT CompleteCurrentCompositionTermination(
    TfEditCookie ec, const ITfComposition* notification, CompositionLifecycleState& state,
    IsCurrent is_current, Reset reset) {
    if (notification && state.Context()) {
        const EditSessionTicket ticket{state.Epoch(), state.Context().Get()};
        const Microsoft::WRL::ComPtr<ITfContext> context_holder = state.Context();
        const Microsoft::WRL::ComPtr<ITfComposition> composition_holder = state.TakeComposition();
        const auto terminated = ClearTerminatedCompositionInSession(
            ec, context_holder.Get(), composition_holder.Get(),
            [&is_current, ticket]() noexcept(noexcept(is_current(ticket))) {
                return is_current(ticket);
            });
        if (!terminated.may_reset) {
            return S_OK;
        }
    }
    reset();
    return S_OK;
}

template <typename IsCurrent, typename Reset>
HRESULT HandleCompositionTermination(
    TfEditCookie ec, const ITfComposition* notification, CompositionLifecycleState& state,
    uint64_t service_id, const GUID& activity, IsCurrent is_current, Reset reset) {
    if (state.Cleanups().HandleTermination(ec, notification, activity)) {
        return S_OK;
    }
    if (notification && notification != state.Composition().Get()) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &activity,
            "TsfLifecycle(service={}, stage=host-termination-ignored, activation={}, context={})",
            service_id, state.Epoch().activation, state.Epoch().context);
        return S_OK;
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &activity,
        "TsfLifecycle(service={}, stage=host-terminated-current, activation={}, context={}, has_composition={})",
        service_id, state.Epoch().activation, state.Epoch().context, state.Composition() != nullptr);
    return CompleteCurrentCompositionTermination(
        ec, notification, state, std::ref(is_current), std::ref(reset));
}

// The caller retains the context/composition and detaches current state in before_end.
template <typename IsCurrent, typename BeforeEnd>
void FinishCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfComposition*> composition,
    CompositionFinishKind kind, std::wstring_view text, IsCurrent is_current, BeforeEnd before_end) {
    const bool commit = kind == CompositionFinishKind::Commit;
    const auto range_operation = commit ? "CommitComposition::GetRange" : "CancelComposition::GetRange";
    Microsoft::WRL::ComPtr<ITfRange> range;
    const HRESULT range_hr = composition->GetRange(&range);
    const bool got_range = log::CheckHResult(range_hr, range_operation);
    if (got_range && !range) {
        QIQI_LOG_WARN("{} returned no range (hr=0x{:08x})", range_operation,
                      gsl::narrow_cast<unsigned int>(range_hr));
    }
    if (got_range && range) {
        if (!is_current()) return;
        if (commit) {
            std::ignore = ClearCompositionDisplayAttribute(ec, context, range.Get(), is_current);
            if (!is_current()) return;
            WriteCompositionTextAndCaret(ec, context, range.Get(), text, is_current);
        } else {
            std::ignore = log::CheckHResult(range->SetText(ec, 0, L"", 0), "CancelComposition::SetText");
        }
    }
    if (!is_current()) return;
    before_end();
    std::ignore = log::CheckHResult(
        composition->EndComposition(ec), commit ? "CommitComposition::EndComposition" : "CancelComposition::EndComposition");
}

template <typename IsCurrent>
void InsertCommittedTextInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, std::wstring_view text, IsCurrent is_current) {
    if (text.empty()) return;
    Microsoft::WRL::ComPtr<ITfInsertAtSelection> insertion;
    const HRESULT insertion_hr = context->QueryInterface(IID_PPV_ARGS(&insertion));
    if (!log::CheckHResult(insertion_hr, "DirectCommit::QueryInterface(ITfInsertAtSelection)")) return;
    if (!insertion) {
        QIQI_LOG_WARN("DirectCommit::QueryInterface(ITfInsertAtSelection) returned no interface (hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(insertion_hr));
        return;
    }
    if (!is_current()) return;
    Microsoft::WRL::ComPtr<ITfRange> inserted_range;
    std::ignore = log::CheckHResult(insertion->InsertTextAtSelection(
        ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size()), &inserted_range), "DirectCommit::InsertTextAtSelection");
}

template <typename IsCurrent>
void FinishOrInsertCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, CompositionLifecycleState& state,
    bool& has_valid_caret_pos, CompositionFinishKind kind, std::wstring_view text, IsCurrent is_current) {
    const auto composition = state.Composition();
    const auto current = [&is_current, &composition]() noexcept(noexcept(is_current(composition.Get()))) {
        return is_current(composition.Get());
    };
    if (!composition) {
        if (kind == CompositionFinishKind::Cancel) {
            has_valid_caret_pos = false;
            return;
        }
        InsertCommittedTextInSession(ec, context, text, std::ref(current));
        return;
    }
    FinishCompositionInSession(
        ec, context, composition.Get(), kind, text, std::ref(current),
        [&state, &has_valid_caret_pos]() noexcept {
            state.ClearComposition();
            has_valid_caret_pos = false;
        });
}

} // namespace qiqi
