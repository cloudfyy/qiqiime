#include "Ime/tsf/display_attribute.h"
#include "common/com_object.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <oleauto.h>
#include <wrl/client.h>

namespace qiqi {

using Microsoft::WRL::ComPtr;

constexpr TF_DISPLAYATTRIBUTE QiQiDisplayAttributeInfo::DefaultAttribute() noexcept {
    TF_DISPLAYATTRIBUTE da{};
    da.crText.type = TF_CT_NONE;
    da.crBk.type = TF_CT_NONE;
    da.lsStyle = TF_LS_DOT;
    da.fBoldLine = FALSE;
    da.crLine.type = TF_CT_COLORREF;
    da.crLine.cr = RGB(35, 105, 200);
    da.bAttr = TF_ATTR_INPUT;
    return da;
}

QiQiDisplayAttributeInfo::QiQiDisplayAttributeInfo() noexcept
    : attr_(DefaultAttribute()) {}

STDMETHODIMP QiQiDisplayAttributeInfo::GetGUID(GUID* pguid) noexcept {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = kGuidDisplayAttributeInput;
    return S_OK;
}

STDMETHODIMP QiQiDisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) noexcept {
    return CopyToBstr(L"QiQi Wubi Composition Underline", pbstrDesc);
}

STDMETHODIMP QiQiDisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) noexcept {
    if (!pda) {
        return E_INVALIDARG;
    }
    *pda = attr_;
    return S_OK;
}

STDMETHODIMP QiQiDisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) noexcept {
    if (!pda) {
        return E_INVALIDARG;
    }
    attr_ = *pda;
    QIQI_LOG_DEBUG("QiQiDisplayAttributeInfo::SetAttributeInfo updated (lsStyle={}, fBoldLine={})",
                   static_cast<unsigned int>(attr_.lsStyle), attr_.fBoldLine != FALSE);
    return S_OK;
}

STDMETHODIMP QiQiDisplayAttributeInfo::Reset() noexcept {
    attr_ = DefaultAttribute();
    QIQI_LOG_DEBUG("QiQiDisplayAttributeInfo::Reset restored default underline attribute");
    return S_OK;
}

// QiQiEnumDisplayAttributeInfo
QiQiEnumDisplayAttributeInfo::QiQiEnumDisplayAttributeInfo(ULONG index) noexcept
    : index_(index) {}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) noexcept {
    const HRESULT hr = CreateComObjectToOut<QiQiEnumDisplayAttributeInfo>(ppEnum, index_);
    if (FAILED(hr) && ppEnum != nullptr) {
        QIQI_LOG_ERROR("QiQiEnumDisplayAttributeInfo::Clone failed with E_OUTOFMEMORY");
    }
    return hr;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Next(
    ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) noexcept {
    if (!rgInfo) {
        return E_INVALIDARG;
    }
    *rgInfo = nullptr;
    if (ulCount == 0) {
        if (pcFetched) *pcFetched = 0;
        return S_OK;
    }
    if (index_ >= 1) {
        if (pcFetched) *pcFetched = 0;
        return S_FALSE;
    }
    if (FAILED(CreateComObjectToOut<QiQiDisplayAttributeInfo>(rgInfo))) {
        QIQI_LOG_ERROR("QiQiEnumDisplayAttributeInfo::Next failed to allocate QiQiDisplayAttributeInfo");
        if (pcFetched) *pcFetched = 0;
        return E_OUTOFMEMORY;
    }
    ++index_;
    QIQI_LOG_TRACE("QiQiEnumDisplayAttributeInfo::Next fetched display attribute (ulCount={})", ulCount);
    if (pcFetched) {
        *pcFetched = 1;
    }
    return (ulCount == 1) ? S_OK : S_FALSE;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Reset() noexcept {
    index_ = 0;
    QIQI_LOG_TRACE("QiQiEnumDisplayAttributeInfo::Reset");
    return S_OK;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Skip(ULONG ulCount) noexcept {
    index_ += ulCount;
    QIQI_LOG_TRACE("QiQiEnumDisplayAttributeInfo::Skip(ulCount={}, new_index={})", ulCount, index_);
    return (index_ <= 1) ? S_OK : S_FALSE;
}

} // namespace qiqi
