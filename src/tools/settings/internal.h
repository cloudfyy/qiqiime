// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/settings/window.h"
#include "common/comobj.h"
#include "common/controls.h"
#include "common/guids.h"
#include <msctf.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <wrl/client.h>
#include <array>
#include <span>
#include <optional>

namespace qiqi {
class ToolCommandUi;
}

namespace qiqi::settings_detail {

struct DialogContext;

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
constexpr int kIdChkQuickSelect = 116;
constexpr int kIdChkBeepDup4 = 117;
constexpr int kIdChkCodeHint = 118;
constexpr int kIdChkDynamicFreq = 119;
constexpr int kIdChkUpDownPageTurn = 120;
constexpr int kIdLblPageSize = 122;
constexpr int kIdLblUiLang = 123;
constexpr int kIdLblUnderlineStyle = 124;
constexpr int kIdComboUnderlineStyle = 125;
constexpr int kIdChkBoldUnderline = 126;
constexpr int kIdBtnDictManager = 127;
constexpr int kIdLblEnglishSwitch = 128;
constexpr int kIdComboEnglishSwitch = 129;
constexpr int kIdRadioCustom = 131;
constexpr int kIdBtnBrowsePrimaryDict = 132;
constexpr int kIdLblCustomPrimaryPath = 133;
constexpr int kIdBtnRestoreDefaults = 134;
constexpr int kIdComboCandidateLayout = 135;
constexpr int kIdComboUiTheme = 136;
constexpr int kIdComboUiTextScale = 137;
constexpr int kIdChkEasyReading = 138;
constexpr int kIdLblUiTheme = 140;
constexpr int kIdLblUiTextScale = 141;
constexpr int kIdCandidatePreview = 142;
constexpr int kIdDisplayExplanation = 143;
constexpr int kIdSettingsTabs = 144;
constexpr int kIdPageScroll = 145;
constexpr int kIdChkCodeCompletion = 146;
constexpr int kIdBtnImportConfig = 147;
constexpr int kIdBtnExportConfig = 148;
constexpr int kIdLblImeSkin = 149;
constexpr int kIdComboImeSkin = 150;
constexpr int kIdBtnCandidateFont = 151;
constexpr int kIdSkinPickerList = 152;
constexpr int kIdAboutIcon = 153;
constexpr int kIdLblAboutTitle = 154;
constexpr int kIdLblAboutVersion = 155;
constexpr int kIdLblAboutLicense = 156;
constexpr int kIdBtnOpenGitHub = 157;
constexpr int kIdLblShortcutsTitle = 158;
constexpr int kIdLblShortcutsSubtitle = 159;
constexpr int kIdLblShortcutsBody = 160;
constexpr int kIdLblSpaceEnter = 161;
constexpr int kIdComboSpaceEnter = 162;
constexpr int kIdChkSingleCharacter = 163;
inline constexpr const wchar_t* kSkinPickerWndClass = L"QiQiImeSkinPicker";
constexpr UINT kSettingsEnsureFocusVisible = WM_APP + 93;
constexpr UINT kSettingsCyclePage = WM_APP + 94;
enum class SettingsPage {
    Input, Dictionary, Candidates, Display, Feedback, Shortcuts, About, Count
};
struct SettingsPageLabel {
    const wchar_t* zh;
    const wchar_t* en;
};
inline constexpr std::array<SettingsPageLabel, 7> kSettingsPageLabels{{
    {L"输入习惯", L"Input"},
    {L"词库", L"Dictionary"},
    {L"候选窗口", L"Candidates"},
    {L"显示", L"Display"},
    {L"声音与反馈", L"Feedback"},
    {L"快捷键", L"Shortcuts"},
    {L"关于", L"About"},
}};
inline constexpr std::array kSettingsInputControls{
    kIdChkAutoCommit4, kIdChkPushCommit5, kIdChkQuickSelect, kIdChkDynamicFreq,
    kIdChkShiftToggle, kIdLblEnglishSwitch, kIdComboEnglishSwitch, kIdLblSpaceEnter, kIdComboSpaceEnter};
inline constexpr std::array kSettingsDictionaryControls{
    kIdRadioJidian, kIdChkExtraDict, kIdRadioHaifeng, kIdRadioBuiltin,
    kIdRadioCustom, kIdBtnBrowsePrimaryDict, kIdBtnDictManager, kIdLblCustomPrimaryPath,
    kIdBtnImportConfig, kIdBtnExportConfig};
inline constexpr std::array kSettingsFooterControls{
    kIdBtnRestoreDefaults, IDOK, IDCANCEL};

constexpr bool IsSettingsFooterControl(int id) noexcept {
    for (const int footer_id : kSettingsFooterControls) {
        if (footer_id == id) return true;
    }
    return false;
}
inline constexpr std::array kSettingsCandidateControls{
    kIdLblPageSize, kIdComboPageSize, kIdComboCandidateLayout, kIdChkCodeCompletion, kIdChkSingleCharacter, kIdChkCodeHint,
    kIdChkUpDownPageTurn, kIdLblUnderlineStyle, kIdComboUnderlineStyle, kIdChkBoldUnderline,
    kIdLblImeSkin, kIdComboImeSkin, kIdBtnCandidateFont, kIdCandidatePreview};
inline constexpr std::array kSettingsDisplayControls{
    kIdLblUiTheme, kIdComboUiTheme,
    kIdChkEasyReading, kIdLblUiTextScale, kIdComboUiTextScale,
    kIdLblUiLang, kIdComboUiLang, kIdDisplayExplanation,
    kIdChkStatusBar};
inline constexpr std::array kSettingsFeedbackControls{
    kIdChkBeepEmpty4, kIdChkBeepDup4, kIdChkClearEmpty4, kIdChkKeySound};
inline constexpr std::array kSettingsShortcutsControls{
    kIdLblShortcutsTitle, kIdLblShortcutsSubtitle, kIdLblShortcutsBody};
inline constexpr std::array kSettingsAboutControls{
    kIdAboutIcon, kIdLblAboutTitle, kIdLblAboutVersion,
    kIdLblAboutLicense, kIdBtnOpenGitHub};

constexpr std::span<const int> SettingsPageControls(SettingsPage page) noexcept {
    switch (page) {
        case SettingsPage::Input: return kSettingsInputControls;
        case SettingsPage::Dictionary: return kSettingsDictionaryControls;
        case SettingsPage::Candidates: return kSettingsCandidateControls;
        case SettingsPage::Display: return kSettingsDisplayControls;
        case SettingsPage::Feedback: return kSettingsFeedbackControls;
        case SettingsPage::Shortcuts: return kSettingsShortcutsControls;
        case SettingsPage::About: return kSettingsAboutControls;
        default: return {};
    }
}
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr UINT SettingsLabelWrapFlags(int id) noexcept {
    return DT_WORDBREAK | DT_NOPREFIX | (id == kIdLblAboutVersion ? DT_EDITCONTROL : 0);
}

namespace settings_layout {
inline constexpr int kFontSize = 15;
inline constexpr int kBoldFontSize = 19;
inline constexpr SIZE kClientSize{820, 494};
inline constexpr RECT kDictionaryCard{18, 16, 404, 196};
inline constexpr RECT kInputCard{18, 206, 404, 434};
inline constexpr RECT kAppearanceCard{416, 16, 802, 202};
inline constexpr RECT kSoundCard{416, 206, 802, 434};
inline constexpr int kControlInset = 14;
inline constexpr int kLeftColumn = kDictionaryCard.left + kControlInset;
inline constexpr int kRightColumn = kAppearanceCard.left + kControlInset;
inline constexpr int kRightLabelLeft = kRightColumn + kFluentToggleTextInset;
inline constexpr int kOptionWidth = 360;
inline constexpr int kRadioHeight = 22;
inline constexpr int kCheckboxHeight = 24;
inline constexpr int kDictionaryFirstRow = kDictionaryCard.top + 30;
inline constexpr int kDictionaryRowStep = 24;
inline constexpr int kHaifengRow = kDictionaryFirstRow + 2 * kDictionaryRowStep;
inline constexpr int kBuiltinRow = kHaifengRow + kDictionaryRowStep;
inline constexpr int kCustomRow = kBuiltinRow + kDictionaryRowStep;
inline constexpr int kInputFirstRow = kInputCard.top + 26;
inline constexpr int kInputRowStep = 28;
inline constexpr int kSoundFirstRow = kSoundCard.top + 32;
inline constexpr int kSoundRowStep = 34;
inline constexpr int kAppearanceFirstRow = kAppearanceCard.top + 62;
inline constexpr int kAppearanceRowStep = 34;
inline constexpr int kLabelWidth = 156;
inline constexpr int kComboLeft = kRightColumn + kLabelWidth + 2;
inline constexpr int kComboWidth = 200;
inline constexpr int kSpaceEnterComboWidth = 260;
inline constexpr int kComboLabelInset = 4;
inline constexpr int kButtonGap = 10;
inline constexpr int kFooterTop = kInputCard.bottom + 14;
inline constexpr int kButtonHeight = 32;
inline constexpr int kAcceptButtonWidth = 98;
inline constexpr RECT kBrowseDictionary{
    kDictionaryCard.right - kControlInset - 84, kCustomRow - 2,
    kDictionaryCard.right - kControlInset, kCustomRow - 2 + 26};
inline constexpr int kCustomRadioWidth = kBrowseDictionary.left - kLeftColumn - 6;
inline constexpr RECT kDictionaryPath{
    kLeftColumn + kDictionaryRowStep, kCustomRow + 26,
    kDictionaryCard.right - kControlInset, kCustomRow + 26 + 20};
inline constexpr RECT kPageSizeLabel{
    kRightLabelLeft, kAppearanceFirstRow, kRightColumn + kLabelWidth, kAppearanceFirstRow + kCheckboxHeight};
inline constexpr RECT kPageSizeCombo{
    kComboLeft, kAppearanceFirstRow - kComboLabelInset, kComboLeft + 80,
    kAppearanceFirstRow - kComboLabelInset + 260};
inline constexpr RECT kCandidateLayoutCombo{
    kComboLeft + 90, kPageSizeCombo.top, kComboLeft + kComboWidth,
    kPageSizeCombo.top + 160};
inline constexpr RECT kLanguageLabel{
    kRightLabelLeft, kAppearanceFirstRow + kAppearanceRowStep,
    kRightColumn + kLabelWidth, kAppearanceFirstRow + kAppearanceRowStep + kCheckboxHeight};
inline constexpr RECT kLanguageCombo{
    kComboLeft, kLanguageLabel.top - kComboLabelInset, kComboLeft + kComboWidth,
    kLanguageLabel.top - kComboLabelInset + 200};
inline constexpr RECT kUnderlineLabel{
    kRightLabelLeft, kLanguageLabel.top + kAppearanceRowStep,
    kRightColumn + kLabelWidth, kLanguageLabel.top + kAppearanceRowStep + kCheckboxHeight};
inline constexpr RECT kUnderlineCombo{
    kComboLeft, kUnderlineLabel.top - kComboLabelInset, kComboLeft + kComboWidth,
    kUnderlineLabel.top - kComboLabelInset + 220};
inline constexpr int kBoldUnderlineRow = kUnderlineCombo.top + 32;
inline constexpr RECT kEnglishSwitchLabel{
    kRightLabelLeft, kSoundFirstRow + 4 * kSoundRowStep + kComboLabelInset,
    kRightColumn + kLabelWidth, kSoundFirstRow + 4 * kSoundRowStep + kComboLabelInset + kCheckboxHeight};
inline constexpr RECT kEnglishSwitchCombo{
    kComboLeft, kEnglishSwitchLabel.top - kComboLabelInset, kComboLeft + kComboWidth,
    kEnglishSwitchLabel.top - kComboLabelInset + 160};
inline constexpr RECT kDictionaryManagerButton{
    kDictionaryCard.left, kFooterTop,
    kDictionaryCard.left + 156, kFooterTop + kButtonHeight};
inline constexpr RECT kCancelButton{
    kClientSize.cx - kDictionaryCard.left - kAcceptButtonWidth, kFooterTop,
    kClientSize.cx - kDictionaryCard.left, kFooterTop + kButtonHeight};
inline constexpr RECT kOkButton{
    kCancelButton.left - kButtonGap - kAcceptButtonWidth, kFooterTop,
    kCancelButton.left - kButtonGap, kFooterTop + kButtonHeight};
inline constexpr RECT kRestoreDefaultsButton{
    kDictionaryCard.left, kFooterTop,
    kOkButton.left - kButtonGap, kFooterTop + kButtonHeight};
} // namespace settings_layout

DWORD OpenDictionaryManager(
    HWND owner, const std::filesystem::path& executable, UiLanguage language,
    gsl::not_null<decltype(&::ShellExecuteExW)> execute = &::ShellExecuteExW);
std::filesystem::path GetSettingsModuleDirectory(const DialogContext& context);

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
    HINSTANCE instance = nullptr;
    QiQiConfig* config = nullptr;
    QiQiConfig baseline_config{};
    bool transfer_in_progress = false;
    ToolCommandUi* active_font_menu = nullptr;
    std::wstring custom_primary_dict_path;
    PrimaryDict last_valid_primary_dict = PrimaryDict::Jidian;
    uint64_t config_registry_sequence = 0;
    bool config_refresh_inflight = false;
    bool config_refresh_pending = false;
    bool controls_initialized = false;
    bool save_on_accept = false;
    bool accepted = false;
    bool done = false;
    UiTheme preview_theme = UiTheme::System;
    UiTextScale preview_scale = UiTextScale::Percent100;
    bool preview_easy_reading = false;
    ThemePalette candidate_preview_palette{};
    SkinPalette candidate_skin_colors{};
    CandidateFontPreferences preview_candidate_font;
    ImeSkinCatalog skin_catalog;
    HRESULT skin_catalog_result = S_OK;
    ImeSkinId preview_skin_id = ImeSkinId::Default;
    std::optional<ImeSkinSnapshot> preview_skin_snapshot;
    std::array<ImeSkinId, 7> quick_skin_choices{};
    size_t quick_skin_count = 0;
    int scroll_offset = 0;
    int content_height = 0;
    int viewport_height = 0;
    bool laying_out = false;
    SettingsPage active_page = SettingsPage::Input;
    std::array<int, kSettingsPageLabels.size()> page_scroll_offsets{};
    RECT page_bounds{};
    ScopedGdiHandle<HFONT> font;
    ScopedGdiHandle<HFONT> bold_font;
    ScopedGdiHandle<HFONT> candidate_preview_font;
    ScopedIcon icon_small;
    ScopedIcon icon_big;
    FluentD2DThemeContext d2d;
    Microsoft::WRL::ComPtr<ITfThreadMgr> tsf_thread_mgr;
    Microsoft::WRL::ComPtr<SettingsCompartmentListener> compartment_listener;
    ScopedTfCompartment global_config_comp;
};

UiLanguage GetSelectedUiLanguageFromCombo(HWND hwnd, const DialogContext& ctx) noexcept;
void UpdateSettingsWindowTitle(HWND hwnd, const DialogContext& ctx) noexcept;
const wchar_t* SettingsConfigFieldLabel(std::string_view key, UiLanguage language) noexcept;
void RenderSettingsLanguage(HWND hwnd, DialogContext& ctx) noexcept;
HRESULT RenderSettingsDraft(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept;
void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx);
void RequestSettingsConfigRefresh(HWND hwnd, DialogContext& ctx);
void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept;
void UpdateExtraDictCheckboxState(HWND hwnd) noexcept;
void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept;
void LayoutSettingsControls(HWND hwnd, DialogContext& ctx) noexcept;
using SettingsFontFamilyLookup = bool (*)(std::wstring_view) noexcept;
bool RefreshSettingsUiAppearance(
    HWND hwnd, DialogContext& ctx, UiLanguage language, DisplayPreferences preferences, UINT dpi,
    SettingsFontFamilyLookup is_font_installed = nullptr) noexcept;
void ApplySettingsDisplayPreview(HWND hwnd, DialogContext& ctx) noexcept;
void PopulateSettingsSkinCombo(HWND hwnd, DialogContext& ctx) noexcept;
void UpdateSettingsSkinPreview(HWND hwnd, DialogContext& ctx) noexcept;
void HandleSettingsSkinSelection(HWND hwnd, DialogContext& ctx) noexcept;
void ReportSkinConfigurationFailure(HWND hwnd, UiLanguage language, HRESULT result) noexcept;
using CandidateFontChooser = BOOL(WINAPI*)(CHOOSEFONTW*);
bool PromptSettingsCandidateFont(
    HWND hwnd, DialogContext& ctx, CandidateFontChooser chooser = &::ChooseFontW) noexcept;
bool ShowSettingsCandidateFontMenu(
    HWND hwnd, DialogContext& ctx, CandidateFontChooser chooser = &::ChooseFontW,
    gsl::not_null<decltype(&::TrackPopupMenuEx)> track_menu = &::TrackPopupMenuEx) noexcept;
void EnsureSettingsControlVisible(HWND hwnd, HWND child, DialogContext& ctx) noexcept;
void UpdateSettingsPageLabels(HWND hwnd, const DialogContext& ctx) noexcept;
const wchar_t* SettingsCandidateFontDisplayName(const std::wstring& family, UiLanguage language) noexcept;
void UpdateCandidateFontLabel(HWND hwnd, const DialogContext& ctx) noexcept;
void DrawSettingsCandidatePreview(const DRAWITEMSTRUCT& item, const DialogContext& ctx) noexcept;
int MeasureSettingsCandidatePreviewHeight(HWND preview, int width, const DialogContext& ctx) noexcept;
void UpdateSettingsTabLabels(HWND hwnd, const DialogContext& ctx) noexcept;
void SelectSettingsPage(HWND hwnd, DialogContext& ctx, SettingsPage page) noexcept;
LRESULT CALLBACK SettingsTabSubclass(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) noexcept;
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

} // namespace qiqi::settings_detail

namespace qiqi {
void HandleSettingsCommand(HWND hwnd, WPARAM wparam, settings_detail::DialogContext& ctx);
}
