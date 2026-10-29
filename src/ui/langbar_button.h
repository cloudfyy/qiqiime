#pragma once
#include <windows.h>
#include <ctfutb.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <atomic>
#include <functional>

#include "common/config.h"

namespace qiqi {

using Microsoft::WRL::ComPtr;

enum class LangBarMenuId : UINT {
    ToggleChinese = 1,
    ToggleFullWidth = 2,
    TogglePunctuation = 3,
    DictJidian = 4,
    DictHaifeng = 5,
    ToggleExtraDict = 6,
    ToggleStatusBar = 7,
    OpenSettings = 8,
    OpenConfigFile = 9,
    ToggleUiLanguage = 10
};

struct LangBarState {
    bool chinese_mode = true;
    bool full_width = false;
    bool chinese_punct = true;
    bool show_status_bar = false;
    PrimaryDict primary_dict = PrimaryDict::Jidian;
    bool enable_jidian_extra = true;
    UiLanguage ui_language = UiLanguage::Auto;
};

// Implements ITfLangBarItemButton + ITfSource registered with GUID_LBI_INPUTMODE
// to render the Windows taskbar tray "中/英" icon and right-click context menu.
class LangBarButton final
    : public ITfLangBarItemButton,
      public ITfSource {
public:
    using ActionCallback = std::function<void(LangBarMenuId action_id)>;

    explicit LangBarButton(ActionCallback cb = {}) noexcept;
    ~LangBarButton() noexcept;

    LangBarButton(const LangBarButton&) = delete;
    LangBarButton& operator=(const LangBarButton&) = delete;
    LangBarButton(LangBarButton&&) = delete;
    LangBarButton& operator=(LangBarButton&&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override;
    STDMETHODIMP_(ULONG) Release() noexcept override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) noexcept override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) noexcept override;
    STDMETHODIMP Show(BOOL fShow) noexcept override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) noexcept override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) noexcept override;
    STDMETHODIMP GetText(BSTR* pbstrText) noexcept override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    void UpdateState(const LangBarState& state);
    const LangBarState& State() const noexcept { return state_; }

private:
    static HICON CreateTextIcon(wchar_t ch) noexcept;

    std::atomic<ULONG> ref_count_{1};
    ComPtr<ITfLangBarItemSink> sink_;
    DWORD sink_cookie_ = 0;
    LangBarState state_{};
    ActionCallback action_callback_;
};

} // namespace qiqi
