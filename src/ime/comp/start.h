#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <gsl/gsl>

#include "common/logger.h"

namespace qiqi {

// Keep all preparation references alive through StartComposition and result publication.
struct CompositionStartPreparation {
    Microsoft::WRL::ComPtr<ITfInsertAtSelection> insertion;
    Microsoft::WRL::ComPtr<ITfRange> range;
    Microsoft::WRL::ComPtr<ITfContextComposition> composition_context;
    bool ready = false;
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

} // namespace qiqi
