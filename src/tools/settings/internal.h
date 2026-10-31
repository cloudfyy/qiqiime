#pragma once

#include "tools/settings/window.h"
#include "common/com_object.h"
#include "common/d2d_controls.h"
#include "common/guids.h"
#include <msctf.h>
#include <wrl/client.h>

namespace qiqi::settings_detail {

constexpr int kIdRadioJidian = 101;
constexpr int kIdRadioHaifeng = 102;
constexpr int kIdRadioBuiltin = 103;
constexpr int kIdChkExtraDict = 104;
constexpr int kIdChkAutoCommit4 = 105;
constexpr int kIdChkPushCommit5 = 106;
constexpr int kIdChkShiftToggle = 107;
constexpr int kIdChkStatusBar = 108;
constexpr int kIdComboPageSize = 109;
constexpr int kIdComboUiLang = 111;
constexpr int kIdChkBeepEmpty4 = 112;
constexpr int kIdChkKeySound = 113;
constexpr int kIdChkClearEmpty4 = 114;
constexpr int kIdBtnAbout = 115;
constexpr int kIdChkQuickSelect = 116;
constexpr int kIdChkBeepDup4 = 117;
constexpr int kIdChkCodeHint = 118;
constexpr int kIdChkDynamicFreq = 119;
constexpr int kIdChkLogKeystrokes = 120;
constexpr int kIdLblPrivacyNotice = 121;
constexpr int kIdLblPageSize = 122;
constexpr int kIdLblUiLang = 123;
constexpr int kIdLblUnderlineStyle = 124;
constexpr int kIdComboUnderlineStyle = 125;
constexpr int kIdChkBoldUnderline = 126;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

class SettingsCompartmentListener final
    : public ComObjectBase<SettingsCompartmentListener, ITfCompartmentEventSink> {
public:
    explicit SettingsCompartmentListener(HWND hwnd) noexcept : hwnd_(hwnd) {}

    STDMETHODIMP OnChange(REFGUID rguid) noexcept override {
        if (IsEqualGUID(rguid, kGuidCompartmentConfigVersion) && hwnd_ && ::IsWindow(hwnd_)) {
            ::PostMessageW(hwnd_, kSettingsExternalConfigChangedMessage, 0, 0);
        }
        return S_OK;
    }

private:
    HWND hwnd_ = nullptr;
};

struct DialogContext {
    QiQiConfig* config = nullptr;
    QiQiConfig baseline_config{};
    uint64_t config_registry_sequence = 0;
    bool save_on_accept = false;
    bool accepted = false;
    bool done = false;
    ScopedGdiHandle<HFONT> font;
    ScopedGdiHandle<HFONT> bold_font;
    ScopedIcon icon_small;
    ScopedIcon icon_big;
    FluentD2DThemeContext d2d;
    Microsoft::WRL::ComPtr<ITfThreadMgr> tsf_thread_mgr;
    Microsoft::WRL::ComPtr<SettingsCompartmentListener> compartment_listener;
    ScopedTfCompartment global_config_comp;
};

UiLanguage GetSelectedUiLanguageFromCombo(HWND hwnd, const DialogContext& ctx) noexcept;
void ApplyLiveSettingsLanguage(HWND hwnd, DialogContext& ctx) noexcept;
void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx) noexcept;
void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept;
void UpdateExtraDictCheckboxState(HWND hwnd) noexcept;
void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept;

} // namespace qiqi::settings_detail
