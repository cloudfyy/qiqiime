#pragma once
#include <windows.h>
#include <msctf.h>

#include "common/com_object.h"
#include "common/config.h"

namespace qiqi {

// Provides dashed/dotted underline styling for active TSF composition text.
class QiQiDisplayAttributeInfo final
    : public ComObjectBase<QiQiDisplayAttributeInfo, ITfDisplayAttributeInfo> {
public:
    explicit QiQiDisplayAttributeInfo(
        CompositionUnderlineStyle style = CompositionUnderlineStyle::Dot,
        bool bold_line = false) noexcept;
    ~QiQiDisplayAttributeInfo() noexcept = default;

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) noexcept override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) noexcept override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP Reset() noexcept override;

    static constexpr TF_DISPLAYATTRIBUTE BuildAttribute(
        CompositionUnderlineStyle style = CompositionUnderlineStyle::Dot,
        bool bold_line = false) noexcept {
        TF_DISPLAYATTRIBUTE da{};
        da.crText.type = TF_CT_NONE;
        da.crBk.type = TF_CT_NONE;
        switch (style) {
            case CompositionUnderlineStyle::Solid:
                da.lsStyle = TF_LS_SOLID;
                break;
            case CompositionUnderlineStyle::Dash:
                da.lsStyle = TF_LS_DASH;
                break;
            case CompositionUnderlineStyle::Squiggle:
                da.lsStyle = TF_LS_SQUIGGLE;
                break;
            case CompositionUnderlineStyle::None:
                da.lsStyle = TF_LS_NONE;
                break;
            case CompositionUnderlineStyle::Dot:
            default:
                da.lsStyle = TF_LS_DOT;
                break;
        }
        da.fBoldLine = bold_line ? TRUE : FALSE;
        da.crLine.type = TF_CT_NONE;
        da.bAttr = TF_ATTR_INPUT;
        return da;
    }

private:
    TF_DISPLAYATTRIBUTE attr_{};
};

class QiQiEnumDisplayAttributeInfo final
    : public ComObjectBase<QiQiEnumDisplayAttributeInfo, IEnumTfDisplayAttributeInfo> {
public:
    explicit QiQiEnumDisplayAttributeInfo(
        ULONG index = 0,
        CompositionUnderlineStyle style = CompositionUnderlineStyle::Dot,
        bool bold_line = false) noexcept;
    ~QiQiEnumDisplayAttributeInfo() noexcept = default;

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) noexcept override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) noexcept override;
    STDMETHODIMP Reset() noexcept override;
    STDMETHODIMP Skip(ULONG ulCount) noexcept override;

private:
    ULONG index_ = 0;
    CompositionUnderlineStyle style_ = CompositionUnderlineStyle::Dot;
    bool bold_line_ = false;
};

} // namespace qiqi
