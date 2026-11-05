#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <string_view>
#include <utility>

#include "common/logger.h"

namespace qiqi {

// The caller validates entry and retains the target/guard owners for this synchronous operation.
template <typename IsCurrent>
void WriteCompositionTextAndCaret(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range,
    std::wstring_view text, IsCurrent is_current) {
    std::ignore = log::CheckHResult(range->SetText(ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size())),
                                   "SetRangeTextAndCollapseCaret::SetText");
    if (!is_current()) return;
    Microsoft::WRL::ComPtr<ITfRange> caret_range;
    const HRESULT clone_hr = range->Clone(&caret_range);
    if (!log::CheckHResult(clone_hr, "SetRangeTextAndCollapseCaret::Clone")) return;
    if (!caret_range) {
        QIQI_LOG_WARN("SetRangeTextAndCollapseCaret::Clone returned no range (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(clone_hr));
        return;
    }
    if (!is_current()) return;
    std::ignore = log::CheckHResult(caret_range->Collapse(ec, TF_ANCHOR_END),
                                   "SetRangeTextAndCollapseCaret::Collapse");
    if (!is_current()) return;
    TF_SELECTION sel{};
    sel.range = caret_range.Get();
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    std::ignore = log::CheckHResult(context->SetSelection(ec, 1, &sel),
                                   "SetRangeTextAndCollapseCaret::SetSelection");
}

} // namespace qiqi
