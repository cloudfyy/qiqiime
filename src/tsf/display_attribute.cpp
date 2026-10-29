#include "tsf/display_attribute.h"
#include "common/guids.h"
#include "common/scoped_handle.h"

#include <oleauto.h>
#include <wrl/client.h>
#include <new>

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
    : attr_(DefaultAttribute()) {
    ++g_com_object_count;
}

QiQiDisplayAttributeInfo::~QiQiDisplayAttributeInfo() noexcept {
    --g_com_object_count;
}

STDMETHODIMP QiQiDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
        *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) QiQiDisplayAttributeInfo::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) QiQiDisplayAttributeInfo::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

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
    return S_OK;
}

STDMETHODIMP QiQiDisplayAttributeInfo::Reset() noexcept {
    attr_ = DefaultAttribute();
    return S_OK;
}

// QiQiEnumDisplayAttributeInfo
QiQiEnumDisplayAttributeInfo::QiQiEnumDisplayAttributeInfo(ULONG index) noexcept
    : index_(index) {
    ++g_com_object_count;
}

QiQiEnumDisplayAttributeInfo::~QiQiEnumDisplayAttributeInfo() noexcept {
    --g_com_object_count;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
        *ppvObj = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) QiQiEnumDisplayAttributeInfo::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) QiQiEnumDisplayAttributeInfo::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) noexcept {
    if (!ppEnum) {
        return E_INVALIDARG;
    }
    *ppEnum = new (std::nothrow) QiQiEnumDisplayAttributeInfo(index_);
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
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
    ComPtr<ITfDisplayAttributeInfo> info;
    info.Attach(new (std::nothrow) QiQiDisplayAttributeInfo());
    if (!info) {
        if (pcFetched) *pcFetched = 0;
        return E_OUTOFMEMORY;
    }
    *rgInfo = info.Detach();
    ++index_;
    if (pcFetched) {
        *pcFetched = 1;
    }
    return (ulCount == 1) ? S_OK : S_FALSE;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Reset() noexcept {
    index_ = 0;
    return S_OK;
}

STDMETHODIMP QiQiEnumDisplayAttributeInfo::Skip(ULONG ulCount) noexcept {
    index_ += ulCount;
    return (index_ <= 1) ? S_OK : S_FALSE;
}

} // namespace qiqi
