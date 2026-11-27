// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string_view>
#include <type_traits>
#include "tools/settings/binding.h"

namespace qiqi::settings_detail {
namespace {

struct DisplayLabels {
    const wchar_t* theme;
    const wchar_t* scale;
    const wchar_t* easy;
    const wchar_t* explanation;
    const wchar_t* preview;
    std::array<const wchar_t*, 4> themes;
};
constexpr DisplayLabels kDisplayZh{
    L"主题", L"文字大小", L"关怀模式",
    L"系统高对比度已启用，将优先使用 Windows 配色。",
    L"候选窗预览\nwubi\n1 五笔\n2 输入\n3 你好\n上一页 / 下一页",
    {L"跟随系统", L"明亮", L"深色", L"高对比度"}};
constexpr DisplayLabels kDisplayEn{
    L"Theme", L"Text size", L"Comfort mode",
    L"Windows high contrast is active and overrides the selected theme.",
    L"Candidate preview\nwubi\n1 五笔\n2 输入\n3 你好\nPrevious / Next",
    {L"Follow Windows", L"Light", L"Dark", L"High contrast"}};

const DisplayLabels& DisplayText(HWND hwnd, const DialogContext& ctx) noexcept {
    return ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, ctx)) == UiLanguage::EnUs
        ? kDisplayEn : kDisplayZh;
}

} // namespace

const wchar_t* SettingsConfigFieldLabel(std::string_view key, UiLanguage language) noexcept {
    const auto& str = GetUiStrings(language);
    const wchar_t* label = nullptr;
    ForEachConfigField([&](const auto& field) noexcept {
        using T = std::remove_cvref_t<decltype(QiQiConfig{}.*(field.member))>;
        if constexpr (std::is_same_v<T, bool>) {
            if (key == field.backup_key) {
                for (const auto& binding : kSettingsCheckboxes) {
                    if (binding.config_field == field.member) label = str.*(binding.label_field);
                }
            }
        }
    });
    if (label) return label;
    if (key == "primary_dict")
        return LocalizedUiText(language, L"主词库", L"Primary dictionary");
    if (key == "page_size" || key == "candidate_layout") return str.dlg_lbl_page_size;
    if (key == "ui_language") return str.dlg_lbl_ui_lang;
    if (key == "underline_style") return str.dlg_lbl_underline_style;
    if (key == "ime_skin_id") return LocalizedUiText(language, L"皮肤", L"Skin");
    if (key == "commit_on_english_switch") return str.dlg_lbl_english_switch;
    if (key == "space_enter_mode") return str.dlg_lbl_space_enter;
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    if (key == "ui_theme") return english ? kDisplayEn.theme : kDisplayZh.theme;
    if (key == "ui_text_scale") return english ? kDisplayEn.scale : kDisplayZh.scale;
    if (key == "easy_reading") return english ? kDisplayEn.easy : kDisplayZh.easy;
    if (key.starts_with("candidate_font_"))
        return english ? L"Candidate font" : L"候选字体";
    if (key == "enable_z_temp_english") return english ? L"Temporary English input" : L"临时英文输入";
    if (key == "default_chinese_mode") return english ? L"Default Chinese mode" : L"默认中文模式";
    if (key == "default_full_width") return english ? L"Default full width" : L"默认全角模式";
    if (key == "default_chinese_punct") return english ? L"Default Chinese punctuation" : L"默认中文标点";
    return nullptr;
}

void UpdateSettingsPageLabels(HWND hwnd, const DialogContext& ctx) noexcept {
    const auto& text = DisplayText(hwnd, ctx);
    ::SetDlgItemTextW(hwnd, kIdLblUiTheme, text.theme);
    ::SetDlgItemTextW(hwnd, kIdLblUiTextScale, text.scale);
    ::SetDlgItemTextW(hwnd, kIdChkEasyReading, text.easy);
    ::SetDlgItemTextW(hwnd, kIdDisplayExplanation, text.explanation);
    ::SetDlgItemTextW(hwnd, kIdCandidatePreview, text.preview);
    const UiLanguage language =
        GetSelectedUiLanguageFromCombo(hwnd, ctx);
    const auto& str = GetUiStrings(language);
    const auto about = GetAboutContent(str);
    ::SetDlgItemTextW(hwnd, kIdLblAboutTitle, about.app_name);
    if (about.version.front() != L'\0') {
        ::SetDlgItemTextW(hwnd, kIdLblAboutVersion, about.version.data());
    }
    ::SetDlgItemTextW(hwnd, kIdLblAboutLicense, about.licenses);
    ::SetDlgItemTextW(hwnd, kIdBtnOpenGitHub, str.about_btn_github);
    ::SetDlgItemTextW(
        hwnd, kIdLblShortcutsTitle, str.help_header_title);
    ::SetDlgItemTextW(
        hwnd, kIdLblShortcutsSubtitle, str.help_header_subtitle);
    ::SetDlgItemTextW(
        hwnd, kIdLblShortcutsBody, str.help_shortcuts_info);
    UpdateSettingsTabLabels(hwnd, ctx);
}

void UpdateCandidateFontLabel(HWND hwnd, const DialogContext& ctx) noexcept {
    const auto language = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    const auto* default_label = LocalizedUiText(language, L"字体", L"Font");
    const auto& font = ctx.preview_candidate_font;
    std::array<wchar_t, 128> label{};
    if (font.family.empty() && font.point_size == 0 &&
        font.weight == CandidateFontWeight::Auto && !font.italic) {
        wcscpy_s(label.data(), label.size(), default_label);
    } else {
        const auto* family_label = SettingsCandidateFontDisplayName(font.family, language);
        const int result = font.point_size != 0
            ? swprintf_s(label.data(), label.size(),
                LocalizedUiText(language, L"%ls %u 磅", L"%ls %u pt"),
                font.family.empty() ? LocalizedUiText(language, L"自动", L"Auto") : family_label,
                gsl::narrow_cast<unsigned int>(font.point_size))
            : swprintf_s(label.data(), label.size(), L"%ls",
                font.family.empty() ? default_label : family_label);
        if (result < 0) {
            QIQI_LOG_WARN("Candidate font button label exceeded its buffer");
            wcscpy_s(label.data(), label.size(), default_label);
        }
    }
    if (!font.family.empty() && !CandidateFontFamilyAvailable(font.family)) {
        const wchar_t* suffix = LocalizedUiText(
            language, L"（未安装，使用默认字体）", L" (missing; using default)");
        const auto remaining = label.size() - std::wcslen(label.data()) - 1;
        if (std::wcslen(suffix) <= remaining) {
            wcscat_s(label.data(), label.size(), suffix);
        }
    }
    ::SetDlgItemTextW(hwnd, kIdBtnCandidateFont, label.data());
}

void UpdateSettingsTabLabels(HWND hwnd, const DialogContext& ctx) noexcept {
    const HWND tabs = ::GetDlgItem(hwnd, kIdSettingsTabs);
    if (!tabs) return;
    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, ctx)) == UiLanguage::EnUs;
    const LRESULT count = ::SendMessageW(tabs, TCM_GETITEMCOUNT, 0, 0);
    for (size_t index = 0; index < kSettingsPageLabels.size(); ++index) {
        const auto& label = gsl::at(kSettingsPageLabels, index);
        std::array<wchar_t, 64> text{};
        wcscpy_s(text.data(), text.size(), english ? label.en : label.zh);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        const bool insert = gsl::narrow_cast<LRESULT>(index) >= count;
        const LRESULT result = ::SendMessageW(tabs, insert ? TCM_INSERTITEMW : TCM_SETITEMW,
            index, reinterpret_cast<LPARAM>(&item));
        if ((insert && result == -1) || (!insert && !result)) {
            QIQI_LOG_WARN("Settings tab {} label update failed", index);
        }
    }
    ::SendMessageW(tabs, TCM_SETCURSEL, gsl::narrow_cast<WPARAM>(std::to_underlying(ctx.active_page)), 0);
}

HRESULT PopulateSettingsComboItems(HWND hwnd, UiLanguage lang) noexcept {
    const auto& str = GetUiStrings(lang);
    std::array<const wchar_t*, kSettingsLanguageOptions.size()> languages{};
    std::ranges::transform(kSettingsLanguageOptions, languages.begin(),
        [&str](const auto& option) noexcept { return str.*(option.label_field); });
    constexpr std::array page_sizes{L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"};
    constexpr std::array text_scales{L"100%", L"125%", L"150%", L"200%"};
    const std::array underline{
        str.dlg_underline_dot, str.dlg_underline_solid, str.dlg_underline_dash,
        str.dlg_underline_squiggle, str.dlg_underline_none};
    const std::array english_switch{str.dlg_english_switch_commit, str.dlg_english_switch_clear};
    const std::array candidate_layout{str.dlg_candidate_horizontal, str.dlg_candidate_vertical};
    const auto& display = ResolveUiLanguage(lang) == UiLanguage::EnUs ? kDisplayEn : kDisplayZh;
    using ComboItems = std::pair<int, std::span<const wchar_t* const>>;
    for (const auto& combo : std::array{
             ComboItems{kIdComboUiLang, languages}, ComboItems{kIdComboPageSize, page_sizes},
             ComboItems{kIdComboCandidateLayout, candidate_layout}, ComboItems{kIdComboUnderlineStyle, underline},
             ComboItems{kIdComboEnglishSwitch, english_switch}, ComboItems{kIdComboSpaceEnter, str.dlg_space_enter_modes},
             ComboItems{kIdComboUiTheme, display.themes}, ComboItems{kIdComboUiTextScale, text_scales}}) {
        const HRESULT result = ReplaceComboBoxItems(::GetDlgItem(hwnd, combo.first), combo.second);
        if (FAILED(result)) {
            QIQI_LOG_WARN("Settings combo {} item replacement failed (hr=0x{:08X})", combo.first,
                gsl::narrow_cast<unsigned long>(result));
            return result;
        }
    }
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETMINVISIBLE, page_sizes.size(), 0);
    return S_OK;
}

void RenderSettingsLanguageLabels(HWND hwnd, UiLanguage lang) noexcept {
    const auto& str = GetUiStrings(lang);

    for (const auto& radio : kSettingsDictRadios) {
        ::SetDlgItemTextW(hwnd, radio.control_id, str.*(radio.label_field));
    }
    for (const auto& binding : kSettingsCheckboxes) {
        ::SetDlgItemTextW(hwnd, binding.control_id, str.*(binding.label_field));
    }
    ::SetDlgItemTextW(hwnd, kIdLblPageSize, str.dlg_lbl_page_size);
    ::SetDlgItemTextW(hwnd, kIdLblUiLang, str.dlg_lbl_ui_lang);
    ::SetDlgItemTextW(hwnd, kIdLblUnderlineStyle, str.dlg_lbl_underline_style);
    ::SetDlgItemTextW(hwnd, kIdLblEnglishSwitch, str.dlg_lbl_english_switch);
    ::SetDlgItemTextW(hwnd, kIdLblSpaceEnter, str.dlg_lbl_space_enter);
    ::SetDlgItemTextW(hwnd, kIdBtnBrowsePrimaryDict, str.dlg_btn_browse_dict);
    ::SetDlgItemTextW(hwnd, kIdBtnDictManager, str.dlg_btn_dict_mgr);
    ::SetDlgItemTextW(hwnd, kIdBtnRestoreDefaults, str.dlg_btn_restore_defaults);
    ::SetDlgItemTextW(hwnd, kIdBtnImportConfig, str.dlg_btn_restore_backup);
    ::SetDlgItemTextW(hwnd, kIdBtnExportConfig, str.dlg_btn_create_backup);
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::SetDlgItemTextW(hwnd, kIdBtnCancel, str.dlg_btn_cancel);

    struct LanguageUpdate {
        HWND owner;
        UiLanguage language;
    } update{hwnd, lang};
    ::EnumThreadWindows(
        ::GetCurrentThreadId(),
        [](HWND owned, LPARAM param) noexcept -> BOOL {
            const auto* change = reinterpret_cast<const LanguageUpdate*>(param);
            if (::GetWindow(owned, GW_OWNER) == change->owner) {
                std::array<wchar_t, 128> name{};
                ::GetClassNameW(owned, name.data(), gsl::narrow_cast<int>(name.size()));
                if (std::wcscmp(name.data(), kInfoWndClass) == 0 ||
                    std::wcscmp(name.data(), kSettingsMessageWndClass) == 0) {
                    ::SendMessageW(owned, kSettingsLanguageChangedMessage,
                                   gsl::narrow_cast<WPARAM>(std::to_underlying(change->language)), 0);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&update));
}

} // namespace qiqi::settings_detail
