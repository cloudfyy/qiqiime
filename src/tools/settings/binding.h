// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/settings/internal.h"
#include "common/i18n.h"

namespace qiqi::settings_detail {
namespace layout = settings_layout;

struct SettingsLanguageOption {
    UiLanguage value;
    const wchar_t* UiStrings::* label_field;
};

constexpr std::array kSettingsLanguageOptions{
    SettingsLanguageOption{UiLanguage::Auto, &UiStrings::dlg_lang_auto},
    SettingsLanguageOption{UiLanguage::ZhCn, &UiStrings::dlg_lang_zh},
    SettingsLanguageOption{UiLanguage::EnUs, &UiStrings::dlg_lang_en},
};

struct SettingsRadioBinding {
    int control_id;
    PrimaryDict dict_value;
    const wchar_t* UiStrings::* label_field;
    int y_offset_px;
    int width_px;
    DWORD extra_style;
};

constexpr std::array<SettingsRadioBinding, 4> kSettingsDictRadios = {{
    {kIdRadioJidian, PrimaryDict::Jidian, &UiStrings::dlg_radio_jidian,
     layout::kDictionaryFirstRow, layout::kOptionWidth, WS_GROUP},
    {kIdRadioHaifeng, PrimaryDict::Haifeng, &UiStrings::dlg_radio_haifeng,
     layout::kHaifengRow, layout::kOptionWidth, 0},
    {kIdRadioBuiltin, PrimaryDict::BuiltinOnly, &UiStrings::dlg_radio_builtin,
     layout::kBuiltinRow, layout::kOptionWidth, 0},
    {kIdRadioCustom, PrimaryDict::Custom, &UiStrings::dlg_radio_custom,
     layout::kCustomRow, layout::kCustomRadioWidth, 0},
}};

struct SettingsCheckboxBinding {
    int control_id;
    bool QiQiConfig::* config_field;
    const wchar_t* UiStrings::* label_field;
    int x_offset_px;
    int y_offset_px;
    int width_px;
};

constexpr std::array<SettingsCheckboxBinding, 17> kSettingsCheckboxes = {{
    {kIdChkExtraDict, &QiQiConfig::enable_jidian_extra, &UiStrings::dlg_chk_extra,
     layout::kLeftColumn, layout::kDictionaryFirstRow + layout::kDictionaryRowStep, layout::kOptionWidth},
    {kIdChkAutoCommit4, &QiQiConfig::auto_commit_4code, &UiStrings::dlg_chk_auto_commit4,
     layout::kLeftColumn, layout::kInputFirstRow, layout::kOptionWidth},
    {kIdChkPushCommit5, &QiQiConfig::push_commit_5th, &UiStrings::dlg_chk_push_commit5,
     layout::kLeftColumn, layout::kInputFirstRow + layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkQuickSelect, &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,
     layout::kLeftColumn, layout::kInputFirstRow + 2 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkCodeHint, &QiQiConfig::show_code_hint, &UiStrings::dlg_chk_code_hint,
     layout::kLeftColumn, layout::kInputFirstRow + 3 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkCodeCompletion, &QiQiConfig::enable_code_completion, &UiStrings::dlg_chk_code_completion,
     layout::kLeftColumn, layout::kInputFirstRow + 7 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkSingleCharacter, &QiQiConfig::single_character_mode, &UiStrings::dlg_chk_single_character,
     layout::kLeftColumn, layout::kInputFirstRow + 8 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkFullCodePractice, &QiQiConfig::full_code_practice, &UiStrings::dlg_chk_full_code_practice,
     layout::kLeftColumn, layout::kInputFirstRow + 9 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkDynamicFreq, &QiQiConfig::dynamic_phrase_freq, &UiStrings::dlg_chk_dynamic_freq,
     layout::kLeftColumn, layout::kInputFirstRow + 4 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkShiftToggle, &QiQiConfig::shift_toggle_mode, &UiStrings::dlg_chk_shift_toggle,
     layout::kLeftColumn, layout::kInputFirstRow + 5 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkUpDownPageTurn, &QiQiConfig::up_down_page_turn, &UiStrings::dlg_chk_up_down_page_turn,
     layout::kLeftColumn, layout::kInputFirstRow + 6 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkStatusBar, &QiQiConfig::show_status_bar, &UiStrings::dlg_chk_status_bar,
     layout::kRightColumn, layout::kDictionaryFirstRow, layout::kOptionWidth},
    {kIdChkBoldUnderline, &QiQiConfig::bold_underline, &UiStrings::dlg_chk_bold_underline,
     layout::kRightColumn, layout::kBoldUnderlineRow, layout::kOptionWidth},
    {kIdChkBeepEmpty4, &QiQiConfig::beep_on_empty_4code, &UiStrings::dlg_chk_beep_empty4,
     layout::kRightColumn, layout::kSoundFirstRow, layout::kOptionWidth},
    {kIdChkBeepDup4, &QiQiConfig::beep_on_dup_4code, &UiStrings::dlg_chk_beep_dup4,
     layout::kRightColumn, layout::kSoundFirstRow + layout::kSoundRowStep, layout::kOptionWidth},
    {kIdChkClearEmpty4, &QiQiConfig::clear_on_empty_4code, &UiStrings::dlg_chk_clear_empty4,
     layout::kRightColumn, layout::kSoundFirstRow + 2 * layout::kSoundRowStep, layout::kOptionWidth},
    {kIdChkKeySound, &QiQiConfig::enable_key_sound, &UiStrings::dlg_chk_key_sound,
     layout::kRightColumn, layout::kSoundFirstRow + 3 * layout::kSoundRowStep, layout::kOptionWidth},
}};

} // namespace qiqi::settings_detail
