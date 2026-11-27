// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/configclient.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string_view>
#include <utility>
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
    if (!log::CheckHResult(ReadSettingsDraft(hwnd, current), "Settings::ReadDraftForTitle")) return;
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

void NormalizeSettingsBaseline(QiQiConfig& config) noexcept {
    if (config.primary_dict != PrimaryDict::Jidian) config.enable_jidian_extra = false;
    if (config.easy_reading) config.ui_text_scale = std::max(config.ui_text_scale, UiTextScale::Percent150);
}

void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx) {
    if (ctx.done || ctx.transfer_in_progress || !ctx.controls_initialized) return;
    config::Snapshot external;
    HRESULT catalog_result = S_OK;
    const bool registry_override = HasConfigRegistrySubKeyOverride();
    if (registry_override) {
        const uint64_t revision = QueryRegistryConfigSequence();
        if (revision == 0 || revision == ctx.baseline_revision) return;
        if (!LoadConfigFromRegistry(external.value, &external.revision)) {
            QIQI_LOG_WARN("Settings could not read the isolated configuration snapshot");
            return;
        }
    } else {
        const HRESULT refresh_result = config::CopyCachedSnapshot(external);
        if (refresh_result != S_OK) {
            ctx.skin_catalog_result =
                refresh_result ==
                        S_FALSE
                    ? E_PENDING
                    : refresh_result;
            QIQI_LOG_ERROR(
                "Settings broker refresh failed "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    refresh_result));
            return;
        }
    }
    if (external.revision == ctx.baseline_revision) return;

    try {
        QiQiConfig current_ui = ctx.baseline_config;
        if (!log::CheckHResult(ReadSettingsDraft(hwnd, current_ui), "Settings::ReadDraftForRefresh")) return;
        QiQiConfig merged_ui = external.value;
        MergeSettingsConfig(merged_ui, ctx.baseline_config, current_ui);
        QiQiConfig baseline = external.value;
        NormalizeSettingsBaseline(baseline);

        if (ctx.save_on_accept && registry_override) {
            catalog_result = LoadImeSkinCatalog(external.skin_catalog);
        }
        const HRESULT previous_catalog_result = ctx.skin_catalog_result;
        const bool apply_catalog = ctx.save_on_accept && SUCCEEDED(catalog_result);
        if (apply_catalog) std::swap(ctx.skin_catalog, external.skin_catalog);
        if (ctx.save_on_accept) ctx.skin_catalog_result = catalog_result;

        if (!log::CheckHResult(RenderSettingsDraft(hwnd, ctx, merged_ui), "Settings::ApplyExternalDraft")) {
            if (apply_catalog) std::swap(ctx.skin_catalog, external.skin_catalog);
            ctx.skin_catalog_result = previous_catalog_result;
            return;
        }

        if (ctx.config) *ctx.config = std::move(external.value);
        ctx.baseline_config = std::move(baseline);
        ctx.baseline_revision = external.revision;
        UpdateSettingsWindowTitle(hwnd, ctx);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings could not prepare the merged configuration (hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(E_OUTOFMEMORY));
    }
}

void RequestSettingsConfigRefresh(
    HWND hwnd,
    DialogContext& ctx) {
    if (ctx.done || !ctx.controls_initialized) return;
    if (HasConfigRegistrySubKeyOverride()) {
        SyncSettingsWindowFromExternalConfig(
            hwnd,
            ctx);
        return;
    }
    if (ctx.transfer_in_progress) {
        ctx.config_refresh_pending = true;
        return;
    }
    if (ctx.config_refresh_inflight) {
        ctx.config_refresh_pending = true;
        return;
    }
    ctx.config_refresh_pending = false;
    ctx.config_refresh_inflight = true;
    const HRESULT submit_result =
        config::ReadSnapshotAsync(
        [hwnd](
                HRESULT result,
                const config::Snapshot&) {
                if (::IsWindow(hwnd)) {
                    ::PostMessageW(
                        hwnd,
                        kSettingsConfigLoadedMessage,
                        config::EncodeAsyncResult(
                            result),
                        0);
                }
            });
    if (FAILED(submit_result)) {
        ctx.config_refresh_inflight =
            false;
        if (ctx.save_on_accept) {
            ctx.skin_catalog_result =
                submit_result;
        }
        QIQI_LOG_ERROR(
            "Settings broker refresh "
            "could not be queued "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                submit_result));
    }
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
        underline_sel != std::to_underlying(CompositionUnderlineStyle::None);
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
    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    QiQiConfig baseline;
    std::wstring custom_path;
    CandidateFontPreferences candidate_font;
    if (ctx) {
        try {
            custom_path = config.primary_dict_path;
            candidate_font = GetCandidateFontPreferences(config);
            if (load_baseline) {
                baseline = config;
                NormalizeSettingsBaseline(baseline);
            }
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("WriteSettingsToWindowControls: failed to copy configuration strings");
            return E_OUTOFMEMORY;
        }
    }

    // Check the required selection before changing any other controls or publishing the draft.
    const HWND space_enter = ::GetDlgItem(hwnd, kIdComboSpaceEnter);
    const auto mode = std::to_underlying(config.space_enter_mode);
    const LRESULT count = space_enter ? ::SendMessageW(space_enter, CB_GETCOUNT, 0, 0) : CB_ERR;
    const LRESULT previous = count > mode ? ::SendMessageW(space_enter, CB_GETCURSEL, 0, 0) : CB_ERR;
    if (count <= mode || ::SendMessageW(space_enter, CB_SETCURSEL, mode, 0) == CB_ERR) {
        if (count > mode && ::SendMessageW(space_enter, CB_GETCURSEL, 0, 0) != previous) {
            ::SendMessageW(space_enter, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(previous), 0);
            if (::SendMessageW(space_enter, CB_GETCURSEL, 0, 0) != previous) {
                QIQI_LOG_WARN("Settings could not restore the prior Space/Enter selection");
            }
        }
        const HRESULT hr = space_enter ? E_FAIL : HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
        std::ignore = log::CheckHResult(hr, "Settings::PopulateSettingsControls(Space/Enter)");
        return hr;
    }

    if (ctx) {
        if (load_baseline) ctx->baseline_config = std::move(baseline);
        ctx->custom_primary_dict_path = std::move(custom_path);
        ctx->preview_candidate_font = std::move(candidate_font);
        ctx->preview_skin_id = config.ime_skin_id;
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
        std::to_underlying(config.underline_style),
        0,
        std::to_underlying(CompositionUnderlineStyle::None));
    ::SendDlgItemMessageW(
        hwnd, kIdComboUnderlineStyle, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(underline_idx), 0);
    ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_SETCURSEL,
        config.commit_on_english_switch ? 0 : 1, 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_SETCURSEL,
        gsl::narrow_cast<WPARAM>(std::to_underlying(config.ui_theme)), 0);
    if (ctx) {
        PopulateSettingsSkinCombo(hwnd, *ctx);
        UpdateCandidateFontLabel(hwnd, *ctx);
    }
    ::CheckDlgButton(hwnd, kIdChkEasyReading, config.easy_reading ? BST_CHECKED : BST_UNCHECKED);
    const UiTextScale scale = config.easy_reading
        ? std::max(config.ui_text_scale, UiTextScale::Percent150) : config.ui_text_scale;
    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL,
        gsl::narrow_cast<WPARAM>(std::to_underlying(scale)), 0);
    UpdateExtraDictCheckboxState(hwnd);
    if (ctx) {
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

HRESULT settings_detail::ReadSettingsDraft(HWND hwnd, QiQiConfig& config) noexcept {
    config.primary_dict = PrimaryDict::Jidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (::IsDlgButtonChecked(hwnd, radio.control_id) == BST_CHECKED) {
            config.primary_dict = radio.dict_value;
            break;
        }
    }
    if (const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        try {
            auto font_family = ctx->preview_candidate_font.family;
            auto primary_dict_path = ctx->custom_primary_dict_path;
            config.candidate_font_family = std::move(font_family);
            config.primary_dict_path = std::move(primary_dict_path);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
        config.ime_skin_id = ctx->preview_skin_id;
        config.candidate_font_size = ctx->preview_candidate_font.point_size;
        config.candidate_font_weight = ctx->preview_candidate_font.weight;
        config.candidate_font_italic = ctx->preview_candidate_font.italic;
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
        config.candidate_layout = CandidateLayout{gsl::narrow_cast<int>(layout_sel)};
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
        underline_sel <= std::to_underlying(CompositionUnderlineStyle::None)) {
        config.underline_style = CompositionUnderlineStyle{gsl::narrow_cast<int>(underline_sel)};
    }
    const LRESULT english_switch_sel = ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_GETCURSEL, 0, 0);
    if (english_switch_sel == 0 || english_switch_sel == 1) {
        config.commit_on_english_switch = (english_switch_sel == 0);
    }
    const HWND space_enter = ::GetDlgItem(hwnd, kIdComboSpaceEnter);
    const LRESULT space_enter_sel = space_enter ? ::SendMessageW(space_enter, CB_GETCURSEL, 0, 0) : CB_ERR;
    if (space_enter_sel >= 0 && space_enter_sel <= std::to_underlying(WubiSpaceEnterMode::EnterSelectSpaceClear)) {
        config.space_enter_mode = WubiSpaceEnterMode{gsl::narrow_cast<uint8_t>(space_enter_sel)};
    } else {
        QIQI_LOG_WARN("ReadSettingsFromWindowControls: Space/Enter selection unavailable (result={})",
            space_enter_sel);
    }
    const LRESULT theme = ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_GETCURSEL, 0, 0);
    const LRESULT scale = ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_GETCURSEL, 0, 0);
    if (theme >= 0 && theme <= std::to_underlying(UiTheme::HighContrast)) {
        config.ui_theme = UiTheme{gsl::narrow_cast<int>(theme)};
    }
    if (scale >= 0 && scale <= std::to_underlying(UiTextScale::Percent200)) {
        config.ui_text_scale = UiTextScale{gsl::narrow_cast<int>(scale)};
    }
    config.easy_reading = ::IsDlgButtonChecked(hwnd, kIdChkEasyReading) == BST_CHECKED;
    if (config.easy_reading && config.ui_text_scale < UiTextScale::Percent150) {
        config.ui_text_scale = UiTextScale::Percent150;
    }
    return S_OK;
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    std::ignore = log::CheckHResult(ReadSettingsDraft(hwnd, config), "ReadSettingsFromWindowControls");
}

} // namespace qiqi
