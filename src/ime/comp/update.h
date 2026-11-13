// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/com_object.h"
#include "ime/comp/range.h"
#include "ime/comp/state.h"
#include <functional>
#include <string_view>
#include <utility>

namespace qiqi {

// Keep the captured composition alive through host calls and return it for the caller's UI checks.
// qiqi-reentrancy: composition
template <typename IsCurrent>
[[nodiscard]] Microsoft::WRL::ComPtr<ITfComposition> UpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context,
    Microsoft::WRL::ComPtr<ITfComposition> composition, std::wstring_view text,
    const TfGuidAtom& attribute_atom, IsCurrent is_current) {
    Expects(composition != nullptr);
    const CompositionEditGuard guard(std::move(composition), std::ref(is_current));
    Microsoft::WRL::ComPtr<ITfRange> range;
    const HRESULT range_hr = guard.Composition()->GetRange(&range);
    const bool got_range = log::CheckHResult(range_hr, "StartOrUpdateComposition::GetRange");
    if (got_range && !range) {
        QIQI_LOG_WARN("StartOrUpdateComposition::GetRange returned no range (hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(range_hr));
    }
    if (!got_range || !range) return guard.Composition();
    if (!guard.Current()) return guard.Composition();
    WriteCompositionTextAndCaret(ec, context, range.Get(), text, std::ref(guard));
    if (!guard.Current()) return guard.Composition();

    // Read the live atom after text/caret edits and again after the property call.
    if (attribute_atom != TF_INVALID_GUIDATOM) {
        Microsoft::WRL::ComPtr<ITfProperty> property;
        const HRESULT property_hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
        const bool got_property = log::CheckHResult(
            property_hr, "StartOrUpdateComposition::GetProperty(GUID_PROP_ATTRIBUTE)");
        if (got_property && !property) {
            QIQI_LOG_WARN("StartOrUpdateComposition::GetProperty returned no property (hr=0x{:08x})",
                          gsl::narrow_cast<unsigned int>(property_hr));
        }
        if (got_property && property) {
            if (!guard.Current()) return guard.Composition();
            const VARIANT value = MakeInt4Variant(gsl::narrow_cast<LONG>(attribute_atom));
            std::ignore = log::CheckHResult(
                property->SetValue(ec, range.Get(), &value), "StartOrUpdateComposition::SetValue(GUID_PROP_ATTRIBUTE)");
        }
    }
    return guard.Composition();
}

} // namespace qiqi
