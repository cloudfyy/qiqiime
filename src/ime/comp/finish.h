#pragma once
#include "ime/comp/attribute.h"
#include "ime/comp/range.h"
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

} // namespace qiqi
