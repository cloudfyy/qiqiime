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
#include "tools/settings/binding.h"

namespace qiqi::settings_detail {

UiLanguage GetSelectedUiLanguageFromCombo(HWND hwnd, const DialogContext& ctx) noexcept {
    const LRESULT lang_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (lang_sel == 1) {
        return UiLanguage::ZhCn;
    }
    if (lang_sel == 2) {
        return UiLanguage::EnUs;
    }
    if (lang_sel == 0) {
        return UiLanguage::Auto;
    }
    return ctx.config ? ctx.config->ui_language : UiLanguage::Auto;
}

void UpdateSettingsWindowTitle(HWND hwnd, const DialogContext& ctx) noexcept {
    if (!ctx.controls_initialized) {
        return;
    }
    QiQiConfig current;
    ForEachConfigField([&](const auto& field) noexcept {
        current.*(field.member) = ctx.baseline_config.*(field.member);
    });
    ReadSettingsFromWindowControls(hwnd, current);
    bool dirty = ctx.custom_primary_dict_path != ctx.baseline_config.primary_dict_path;
    ForEachConfigField([&](const auto& field) noexcept {
        if (field.HasSettingsChange(ctx.baseline_config, current)) {
            dirty = true;
        }
    });
    ForEachConfigStringField([&](const auto& field) noexcept {
        if (field.HasSettingsChange(ctx.baseline_config, current)) {
            dirty = true;
        }
    });
    const auto& str = GetUiStrings(current.ui_language);
    std::array<wchar_t, 256> title{};
    if (swprintf_s(title.data(), title.size(), L"%ls%ls", str.dlg_title, dirty ? L" *" : L"") < 0) {
        QIQI_LOG_ERROR("Settings title formatting failed");
        return;
    }
    if (!::SetWindowTextW(hwnd, title.data())) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("Settings title update failed (error={})", error);
    }
}

void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx) noexcept {
    if (ctx.transfer_in_progress) return;
    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == ctx.config_registry_sequence) {
        return;
    }

    QiQiConfig external_cfg{};
    uint64_t loaded_seq = 0;
    if (!LoadConfigFromRegistry(external_cfg, &loaded_seq)) {
        return;
    }
    if (ctx.save_on_accept) ctx.skin_catalog_result = LoadImeSkinCatalog(ctx.skin_catalog);

    QiQiConfig current_ui_cfg = ctx.baseline_config;
    ReadSettingsFromWindowControls(hwnd, current_ui_cfg);

    QiQiConfig merged_ui_cfg = external_cfg;
    MergeSettingsConfig(merged_ui_cfg, ctx.baseline_config, current_ui_cfg);
    ctx.config_registry_sequence = loaded_seq;
    if (ctx.config) {
        *ctx.config = external_cfg;
    }

    if (!log::CheckHResult(RenderSettingsDraft(hwnd, ctx, merged_ui_cfg), "SyncSettingsWindowFromExternalConfig")) return;
    ctx.baseline_config = external_cfg;
    ctx.baseline_config.enable_jidian_extra =
        external_cfg.primary_dict == PrimaryDict::Jidian && external_cfg.enable_jidian_extra;
    if (external_cfg.easy_reading) {
        ctx.baseline_config.ui_text_scale = std::max(external_cfg.ui_text_scale, UiTextScale::Percent150);
    }
    UpdateSettingsWindowTitle(hwnd, ctx);
}

void UpdateExtraDictCheckboxState(HWND hwnd) noexcept {
    const bool is_jidian = (::IsDlgButtonChecked(hwnd, kIdRadioJidian) == BST_CHECKED);
    if (const HWND chk = ::GetDlgItem(hwnd, kIdChkExtraDict)) {
        if (!is_jidian) {
            ::CheckDlgButton(hwnd, kIdChkExtraDict, BST_UNCHECKED);
        }
        ::EnableWindow(chk, is_jidian ? TRUE : FALSE);
    }
    if (const HWND browse = ::GetDlgItem(hwnd, kIdBtnBrowsePrimaryDict)) {
        ::EnableWindow(browse, ::IsDlgButtonChecked(hwnd, kIdRadioCustom) == BST_CHECKED);
    }
    const LRESULT underline_sel = ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_GETCURSEL, 0, 0);
    const bool has_underline =
        underline_sel != static_cast<LRESULT>(CompositionUnderlineStyle::None);
    if (const HWND bold_chk = ::GetDlgItem(hwnd, kIdChkBoldUnderline)) {
        ::EnableWindow(bold_chk, has_underline ? TRUE : FALSE);
    }
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    std::ignore = log::CheckHResult(
        WriteSettingsToWindowControlsResult(hwnd, config), "WriteSettingsToWindowControls");
}

static HRESULT PopulateSettingsControls(HWND hwnd, const QiQiConfig& config, bool load_baseline) noexcept {
    if (!::IsWindow(hwnd)) {
        QIQI_LOG_WARN("WriteSettingsToWindowControls requires a valid window");
        return E_INVALIDARG;
    }
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        try {
            std::wstring custom_path = config.primary_dict_path;
            if (load_baseline) {
                QiQiConfig baseline = config;
                baseline.enable_jidian_extra =
                    config.primary_dict == PrimaryDict::Jidian && config.enable_jidian_extra;
                if (config.easy_reading) {
                    baseline.ui_text_scale = std::max(config.ui_text_scale, UiTextScale::Percent150);
                }
                ctx->baseline_config = std::move(baseline);
            }
            ctx->custom_primary_dict_path = std::move(custom_path);
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("WriteSettingsToWindowControls: failed to copy configuration strings");
            return E_OUTOFMEMORY;
        }
        ctx->last_valid_primary_dict = config.primary_dict;
    }
    int checked_radio_id = kIdRadioJidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (config.primary_dict == radio.dict_value) {
            checked_radio_id = radio.control_id;
            break;
        }
    }
    for (const auto& radio : kSettingsDictRadios) {
        ::CheckDlgButton(
            hwnd,
            radio.control_id,
            (radio.control_id == checked_radio_id) ? BST_CHECKED : BST_UNCHECKED);
    }
    const auto& str = GetUiStrings(config.ui_language);
    ::SetDlgItemTextW(
        hwnd,
        kIdLblCustomPrimaryPath,
        ResolveDictionaryPathDisplayText(config.primary_dict_path, str.dlg_lbl_custom_dict_none));

    for (const auto& binding : kSettingsCheckboxes) {
        ::CheckDlgButton(
            hwnd,
            binding.control_id,
            (config.*(binding.config_field)) ? BST_CHECKED : BST_UNCHECKED);
    }

    const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(idx), 0);
    ::SendDlgItemMessageW(hwnd, kIdComboCandidateLayout, CB_SETCURSEL,
        config.candidate_layout == CandidateLayout::Vertical ? 1 : 0, 0);

    const int lang_idx = (config.ui_language == UiLanguage::ZhCn)
        ? 1
        : (config.ui_language == UiLanguage::EnUs ? 2 : 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(lang_idx), 0);

    const int underline_idx = std::clamp(
        static_cast<int>(config.underline_style),
        0,
        static_cast<int>(CompositionUnderlineStyle::None));
    ::SendDlgItemMessageW(
        hwnd, kIdComboUnderlineStyle, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(underline_idx), 0);
    ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_SETCURSEL,
        config.commit_on_english_switch ? 0 : 1, 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_SETCURSEL, static_cast<WPARAM>(config.ui_theme), 0);
    if (auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        context->preview_skin_id = config.ime_skin_id;
        try {
            context->preview_candidate_font = GetCandidateFontPreferences(config);
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("WriteSettingsToWindowControls: failed to copy candidate font family");
            return E_OUTOFMEMORY;
        }
        PopulateSettingsSkinCombo(hwnd, *context);
        UpdateCandidateFontLabel(hwnd, *context);
    }
    ::CheckDlgButton(hwnd, kIdChkEasyReading, config.easy_reading ? BST_CHECKED : BST_UNCHECKED);
    const UiTextScale scale = config.easy_reading
        ? std::max(config.ui_text_scale, UiTextScale::Percent150) : config.ui_text_scale;
    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL, static_cast<WPARAM>(scale), 0);
    UpdateExtraDictCheckboxState(hwnd);
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        ApplySettingsDisplayPreview(hwnd, *ctx);
        UpdateSettingsWindowTitle(hwnd, *ctx);
    }
    return S_OK;
}

HRESULT WriteSettingsToWindowControlsResult(HWND hwnd, const QiQiConfig& config) noexcept {
    return PopulateSettingsControls(hwnd, config, true);
}

HRESULT settings_detail::RenderSettingsDraft(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept {
    const HRESULT hr = PopulateSettingsControls(hwnd, config, false);
    if (SUCCEEDED(hr)) RenderSettingsLanguage(hwnd, ctx);
    return hr;
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    config.primary_dict = PrimaryDict::Jidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (::IsDlgButtonChecked(hwnd, radio.control_id) == BST_CHECKED) {
            config.primary_dict = radio.dict_value;
            break;
        }
    }
    if (const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        config.ime_skin_id = ctx->preview_skin_id;
        try {
            config.candidate_font_family = ctx->preview_candidate_font.family;
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("ReadSettingsFromWindowControls: candidate font family copy failed");
        }
        config.candidate_font_size = ctx->preview_candidate_font.point_size;
        config.candidate_font_weight = ctx->preview_candidate_font.weight;
        config.candidate_font_italic = ctx->preview_candidate_font.italic;
        try {
            config.primary_dict_path = ctx->custom_primary_dict_path;
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("ReadSettingsFromWindowControls: custom_primary_dict_path copy failed");
        }
    }

    for (const auto& binding : kSettingsCheckboxes) {
        config.*(binding.config_field) = (::IsDlgButtonChecked(hwnd, binding.control_id) == BST_CHECKED);
    }
    const LRESULT sel = ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 9) {
        config.page_size = gsl::narrow_cast<size_t>(sel + 1);
    }
    const LRESULT layout_sel = ::SendDlgItemMessageW(hwnd, kIdComboCandidateLayout, CB_GETCURSEL, 0, 0);
    if (layout_sel == 0 || layout_sel == 1) {
        config.candidate_layout = static_cast<CandidateLayout>(layout_sel);
    }

    const LRESULT lang_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (lang_sel == 1) {
        config.ui_language = UiLanguage::ZhCn;
    } else if (lang_sel == 2) {
        config.ui_language = UiLanguage::EnUs;
    } else if (lang_sel == 0) {
        config.ui_language = UiLanguage::Auto;
    }

    const LRESULT underline_sel = ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_GETCURSEL, 0, 0);
    if (underline_sel >= 0 &&
        underline_sel <= static_cast<LRESULT>(CompositionUnderlineStyle::None)) {
        config.underline_style = static_cast<CompositionUnderlineStyle>(underline_sel);
    }
    const LRESULT english_switch_sel = ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_GETCURSEL, 0, 0);
    if (english_switch_sel == 0 || english_switch_sel == 1) {
        config.commit_on_english_switch = (english_switch_sel == 0);
    }
    const LRESULT theme = ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_GETCURSEL, 0, 0);
    const LRESULT scale = ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_GETCURSEL, 0, 0);
    if (theme >= 0 && theme < 4) config.ui_theme = static_cast<UiTheme>(theme);
    if (scale >= 0 && scale < 4) config.ui_text_scale = static_cast<UiTextScale>(scale);
    config.easy_reading = ::IsDlgButtonChecked(hwnd, kIdChkEasyReading) == BST_CHECKED;
    if (config.easy_reading && config.ui_text_scale < UiTextScale::Percent150) {
        config.ui_text_scale = UiTextScale::Percent150;
    }
}

} // namespace qiqi
