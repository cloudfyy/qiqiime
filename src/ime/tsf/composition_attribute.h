#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <utility>

#include "common/logger.h"

namespace qiqi {

template <typename IsCurrent>
HRESULT ClearCompositionDisplayAttribute(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range,
    IsCurrent is_current, bool* clear_started = nullptr) {
    if (!is_current()) {
        QIQI_LOG_DEBUG("CompositionAttributeClear: invalidated operation skipped");
        return S_FALSE;
    }
    Microsoft::WRL::ComPtr<ITfProperty> property;
    const HRESULT property_hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
    if (!log::CheckHResult(property_hr, "CompositionCleanup::GetProperty(GUID_PROP_ATTRIBUTE)")) {
        return property_hr;
    }
    if (!property) {
        QIQI_LOG_WARN("CompositionCleanup::GetProperty returned no property (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(property_hr));
        return E_UNEXPECTED;
    }
    if (!is_current()) {
        QIQI_LOG_DEBUG("CompositionAttributeClear: invalidated operation skipped after property lookup");
        return S_FALSE;
    }
    if (clear_started) *clear_started = true;
    const HRESULT hr = property->Clear(ec, range.get());
    std::ignore = log::CheckHResult(hr, "CompositionCleanup::Clear(GUID_PROP_ATTRIBUTE)");
    return hr;
}

} // namespace qiqi
