#pragma once
#include "common/com_object.h"
#include "ime/comp/range.h"
#include <string_view>
#include <utility>

namespace qiqi {

// Keep the captured composition alive through host calls and return it for the caller's UI checks.
template <typename IsCurrent>
[[nodiscard]] Microsoft::WRL::ComPtr<ITfComposition> UpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context,
    const Microsoft::WRL::ComPtr<ITfComposition> composition, std::wstring_view text,
    const TfGuidAtom& attribute_atom, IsCurrent is_current) {
    Expects(composition != nullptr);
    Microsoft::WRL::ComPtr<ITfRange> range;
    const HRESULT range_hr = composition->GetRange(&range);
    const bool got_range = log::CheckHResult(range_hr, "StartOrUpdateComposition::GetRange");
    if (got_range && !range) {
        QIQI_LOG_WARN("StartOrUpdateComposition::GetRange returned no range (hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(range_hr));
    }
    if (!got_range || !range) return composition;
    const auto current = [&]() noexcept(noexcept(is_current(composition.Get()))) {
        return is_current(composition.Get());
    };
    if (!current()) return composition;
    WriteCompositionTextAndCaret(ec, context, range.Get(), text, current);
    if (!current()) return composition;

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
            if (!current()) return composition;
            const VARIANT value = MakeInt4Variant(gsl::narrow_cast<LONG>(attribute_atom));
            std::ignore = log::CheckHResult(
                property->SetValue(ec, range.Get(), &value), "StartOrUpdateComposition::SetValue(GUID_PROP_ATTRIBUTE)");
        }
    }
    return composition;
}

} // namespace qiqi
