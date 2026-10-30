#pragma once
#include <windows.h>
#include <msctf.h>

#include "common/com_object.h"

namespace qiqi {

// Provides dashed/dotted underline styling for active TSF composition text.
class QiQiDisplayAttributeInfo final
    : public ComObjectBase<QiQiDisplayAttributeInfo, ITfDisplayAttributeInfo> {
public:
    QiQiDisplayAttributeInfo() noexcept;
    ~QiQiDisplayAttributeInfo() noexcept = default;

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) noexcept override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) noexcept override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP Reset() noexcept override;

private:
    static constexpr TF_DISPLAYATTRIBUTE DefaultAttribute() noexcept;

    TF_DISPLAYATTRIBUTE attr_{};
};

class QiQiEnumDisplayAttributeInfo final
    : public ComObjectBase<QiQiEnumDisplayAttributeInfo, IEnumTfDisplayAttributeInfo> {
public:
    explicit QiQiEnumDisplayAttributeInfo(ULONG index = 0) noexcept;
    ~QiQiEnumDisplayAttributeInfo() noexcept = default;

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) noexcept override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) noexcept override;
    STDMETHODIMP Reset() noexcept override;
    STDMETHODIMP Skip(ULONG ulCount) noexcept override;

private:
    ULONG index_ = 0;
};

} // namespace qiqi
