#pragma once
#include <windows.h>
#include <msctf.h>
#include <atomic>

namespace qiqi {

// Provides dashed/dotted underline styling for active TSF composition text.
class QiQiDisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    QiQiDisplayAttributeInfo() noexcept;
    ~QiQiDisplayAttributeInfo() noexcept = default;

    QiQiDisplayAttributeInfo(const QiQiDisplayAttributeInfo&) = delete;
    QiQiDisplayAttributeInfo& operator=(const QiQiDisplayAttributeInfo&) = delete;
    QiQiDisplayAttributeInfo(QiQiDisplayAttributeInfo&&) = delete;
    QiQiDisplayAttributeInfo& operator=(QiQiDisplayAttributeInfo&&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override;
    STDMETHODIMP_(ULONG) Release() noexcept override;

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) noexcept override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) noexcept override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) noexcept override;
    STDMETHODIMP Reset() noexcept override;

private:
    static TF_DISPLAYATTRIBUTE DefaultAttribute() noexcept;

    std::atomic<ULONG> ref_count_{1};
    TF_DISPLAYATTRIBUTE attr_{};
};

class QiQiEnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    explicit QiQiEnumDisplayAttributeInfo(ULONG index = 0) noexcept : index_(index) {}
    ~QiQiEnumDisplayAttributeInfo() noexcept = default;

    QiQiEnumDisplayAttributeInfo(const QiQiEnumDisplayAttributeInfo&) = delete;
    QiQiEnumDisplayAttributeInfo& operator=(const QiQiEnumDisplayAttributeInfo&) = delete;
    QiQiEnumDisplayAttributeInfo(QiQiEnumDisplayAttributeInfo&&) = delete;
    QiQiEnumDisplayAttributeInfo& operator=(QiQiEnumDisplayAttributeInfo&&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override;
    STDMETHODIMP_(ULONG) Release() noexcept override;

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) noexcept override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) noexcept override;
    STDMETHODIMP Reset() noexcept override;
    STDMETHODIMP Skip(ULONG ulCount) noexcept override;

private:
    std::atomic<ULONG> ref_count_{1};
    ULONG index_ = 0;
};

} // namespace qiqi
