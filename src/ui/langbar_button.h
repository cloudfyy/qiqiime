#pragma once
#include <windows.h>
#include <ctfutb.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <functional>

#include "common/com_object.h"
#include "common/config.h"
#include "common/scoped_handle.h"

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
    ToggleUiLanguage = 10,
    OpenAbout = 11
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

struct LangBarMenuItem {
    LangBarMenuId id{};
    const wchar_t* text = nullptr;
    bool checked = false;
    bool enabled = true;

    constexpr bool IsSeparator() const noexcept { return text == nullptr; }
};

// Implements ITfLangBarItemButton + ITfSource registered with GUID_LBI_INPUTMODE
// to render the Windows taskbar tray "中/英" icon and right-click context menu.
class LangBarButton final
    : public ComObjectBase<LangBarButton, ITfLangBarItemButton, ITfSource> {
public:
    using ActionCallback = std::function<void(LangBarMenuId action_id)>;
    using MenuItems = std::array<LangBarMenuItem, 13>;

    explicit LangBarButton(ActionCallback cb = {}) noexcept;
    ~LangBarButton() noexcept = default;

    bool QueryAdditionalInterfaces(REFIID riid, void** ppvObj) noexcept {
        return MatchBaseInterfaces<ITfLangBarItemButton, ITfLangBarItem>(riid, ppvObj);
    }

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

    static MenuItems BuildSharedMenuItems(const LangBarState& state) noexcept;
    // The returned menu must be destroyed before out_bg_brush.
    static ScopedMenu BuildSharedPopupMenu(
        const LangBarState& state, HWND owner_hwnd, ScopedGdiHandle<HBRUSH>& out_bg_brush) noexcept;
    static void ShowSharedPopupMenu(
        const LangBarState& state, POINT screen_pt, const ActionCallback& callback, bool align_bottom = false);

    static constexpr bool KeepsMenuOpen(LangBarMenuId id) noexcept {
        switch (id) {
            case LangBarMenuId::ToggleChinese:
            case LangBarMenuId::ToggleFullWidth:
            case LangBarMenuId::TogglePunctuation:
            case LangBarMenuId::DictJidian:
            case LangBarMenuId::DictHaifeng:
            case LangBarMenuId::ToggleExtraDict:
            case LangBarMenuId::ToggleStatusBar:
            case LangBarMenuId::ToggleUiLanguage:
                return true;
            default:
                return false;
        }
    }

private:
    static ScopedIcon CreateTextIcon(wchar_t ch) noexcept;

    ComPtr<ITfLangBarItemSink> sink_;
    DWORD sink_cookie_ = 0;
    LangBarState state_{};
    ActionCallback action_callback_;
};

} // namespace qiqi
