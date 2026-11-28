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
namespace {

std::optional<UiLanguage> ReadSettingsLanguageSelection(HWND hwnd) noexcept {
    const LRESULT index = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (index >= 0 && std::cmp_less(index, kSettingsLanguageOptions.size())) {
        return gsl::at(kSettingsLanguageOptions, gsl::narrow_cast<size_t>(index)).value;
    }
    return std::nullopt;
}

size_t SettingsLanguageIndex(UiLanguage language) noexcept {
    const auto match = std::ranges::find(kSettingsLanguageOptions, language, &SettingsLanguageOption::value);
    return match != kSettingsLanguageOptions.end()
        ? gsl::narrow_cast<size_t>(match - kSettingsLanguageOptions.begin()) : 0;
}

void EnableDraftEditing(HWND hwnd, bool enabled) noexcept {
    const auto enable = [hwnd, enabled](int id) noexcept {
        if (const HWND child = ::GetDlgItem(hwnd, id)) ::EnableWindow(child, enabled);
    };
    for (const auto& radio : kSettingsDictRadios) enable(radio.control_id);
    for (const auto& checkbox : kSettingsCheckboxes) enable(checkbox.control_id);
    for (const int id : {kIdComboPageSize, kIdComboCandidateLayout, kIdComboUiLang, kIdComboUnderlineStyle,
                         kIdComboEnglishSwitch, kIdComboSpaceEnter, kIdComboOutputConversion, kIdComboUiTheme,
                         kIdComboUiTextScale,
                         kIdComboImeSkin, kIdChkEasyReading, kIdBtnBrowsePrimaryDict, kIdBtnCandidateFont,
                         kIdBtnRestoreDefaults, kIdBtnImportConfig}) {
        enable(id);
    }
}

HRESULT ApplySettingsControls(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept;

void RetryRetainedSettingsDraft(HWND hwnd, DialogContext& ctx) noexcept {
    if (ctx.draft_to_recover && log::CheckHResult(
            ApplySettingsControls(hwnd, ctx, *ctx.draft_to_recover), "Settings::RetryRetainedDraft")) {
        UpdateSettingsWindowTitle(hwnd, ctx);
    }
}

} // namespace

UiLanguage GetSelectedUiLanguageFromCombo(HWND hwnd, const DialogContext& ctx) noexcept {
    return ReadSettingsLanguageSelection(hwnd).value_or(ctx.config ? ctx.config->ui_language : UiLanguage::Auto);
}

void UpdateSettingsWindowTitle(HWND hwnd, const DialogContext& ctx) noexcept {
    if (!ctx.controls_initialized || ctx.applying_draft || ctx.draft_to_recover) {
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
    if (ctx.done || ctx.transfer_in_progress || ctx.applying_draft || !ctx.controls_initialized) return;
    config::Snapshot external;
    HRESULT catalog_result = S_OK;
    const bool registry_override = HasConfigRegistrySubKeyOverride();
    if (registry_override) {
        const uint64_t revision = QueryRegistryConfigSequence();
        if (revision == 0 || revision == ctx.baseline_revision) {
            RetryRetainedSettingsDraft(hwnd, ctx);
            return;
        }
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
            if (refresh_result == S_FALSE) RetryRetainedSettingsDraft(hwnd, ctx);
            return;
        }
    }
    if (external.revision == ctx.baseline_revision) {
        RetryRetainedSettingsDraft(hwnd, ctx);
        return;
    }

    try {
        QiQiConfig current_ui;
        if (!log::CheckHResult(CaptureSettingsDraft(hwnd, ctx, current_ui), "Settings::ReadDraftForRefresh")) return;
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

        if (!log::CheckHResult(ApplySettingsControls(hwnd, ctx, merged_ui), "Settings::ApplyExternalDraft")) {
            if (apply_catalog) std::swap(ctx.skin_catalog, external.skin_catalog);
            ctx.skin_catalog_result = previous_catalog_result;
            std::ignore = ReapplySettingsDraft(hwnd, ctx, std::move(current_ui));
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
    if (ctx.done || ctx.applying_draft || !ctx.controls_initialized) return;
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
        if (!is_jidian && ::IsDlgButtonChecked(hwnd, kIdChkExtraDict) == BST_CHECKED) {
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

HRESULT SelectSettingsComboItem(HWND hwnd, int control_id, WPARAM index) noexcept {
    const HWND combo = ::GetDlgItem(hwnd, control_id);
    const LRESULT selected = combo ? ::SendMessageW(combo, CB_SETCURSEL, index, 0) : CB_ERR;
    const HRESULT result = !combo ? HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE)
        : (selected >= 0 && std::cmp_equal(selected, index) ? S_OK : E_FAIL);
    if (FAILED(result)) {
        QIQI_LOG_WARN("Settings combo {} selection failed (hr=0x{:08X})",
            control_id, gsl::narrow_cast<unsigned long>(result));
    }
    return result;
}

namespace {

HRESULT SetSettingsCheck(HWND hwnd, int control_id, bool checked) noexcept {
    const HWND button = ::GetDlgItem(hwnd, control_id);
    if (!button) {
        constexpr HRESULT result = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
        QIQI_LOG_WARN("Settings button {} missing (hr=0x{:08X})", control_id, gsl::narrow_cast<unsigned long>(result));
        return result;
    }
    const int expected = checked ? BST_CHECKED : BST_UNCHECKED;
    ::SendMessageW(button, BM_SETCHECK, gsl::narrow_cast<WPARAM>(expected), 0);
    // BM_SETCHECK has no result; Fluent buttons can fail to store the check state.
    if (::SendMessageW(button, BM_GETCHECK, 0, 0) != expected) {
        QIQI_LOG_WARN("Settings button {} did not apply its check state", control_id);
        return E_FAIL;
    }
    return S_OK;
}

HRESULT ApplySettingsControls(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept {
    if (!::IsWindow(hwnd)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (ctx.applying_draft) return E_PENDING;
    ctx.applying_draft = true;
    const auto finish = gsl::finally([&ctx]() noexcept { ctx.applying_draft = false; });
    std::wstring custom_path;
    CandidateFontPreferences candidate_font;
    try {
        custom_path = config.primary_dict_path;
        candidate_font = GetCandidateFontPreferences(config);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    const size_t language_index = SettingsLanguageIndex(config.ui_language);
    const auto language = gsl::at(kSettingsLanguageOptions, language_index).value;
    const auto display = NormalizeDisplayPreferences(GetDisplayPreferences(config));
    // An earlier failed replacement may have left a list incomplete.
    if (const HRESULT result = PopulateSettingsComboItems(hwnd, language); FAILED(result)) return result;

    using ComboSelection = std::pair<int, WPARAM>;
    const int underline = std::clamp(std::to_underlying(config.underline_style),
        0, std::to_underlying(CompositionUnderlineStyle::None));
    for (const auto& [id, index] : std::array{
             ComboSelection{kIdComboPageSize, std::clamp<size_t>(config.page_size, 1, 9) - 1},
             ComboSelection{kIdComboCandidateLayout, config.candidate_layout == CandidateLayout::Vertical ? 1u : 0u},
             ComboSelection{kIdComboUiLang, language_index},
             ComboSelection{kIdComboUnderlineStyle, gsl::narrow_cast<WPARAM>(underline)},
             ComboSelection{kIdComboEnglishSwitch, config.commit_on_english_switch ? 0u : 1u},
             ComboSelection{kIdComboSpaceEnter, std::to_underlying(config.space_enter_mode)},
             ComboSelection{kIdComboOutputConversion, std::to_underlying(config.output_conversion)},
             ComboSelection{kIdComboUiTheme, gsl::narrow_cast<WPARAM>(std::to_underlying(display.theme))},
             ComboSelection{kIdComboUiTextScale, gsl::narrow_cast<WPARAM>(std::to_underlying(display.text_scale))}}) {
        if (const HRESULT result = SelectSettingsComboItem(hwnd, id, index); FAILED(result)) return result;
    }

    int checked_radio_id = kIdRadioJidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (config.primary_dict == radio.dict_value) {
            checked_radio_id = radio.control_id;
            break;
        }
    }
    for (const auto& radio : kSettingsDictRadios) {
        if (const HRESULT result = SetSettingsCheck(hwnd, radio.control_id, radio.control_id == checked_radio_id);
                FAILED(result)) return result;
    }

    for (const auto& binding : kSettingsCheckboxes) {
        const bool checked = (config.*(binding.config_field)) &&
            (binding.control_id != kIdChkExtraDict || checked_radio_id == kIdRadioJidian);
        if (const HRESULT result = SetSettingsCheck(hwnd, binding.control_id, checked); FAILED(result)) return result;
    }
    if (const HRESULT result = SetSettingsCheck(hwnd, kIdChkEasyReading, display.easy_reading); FAILED(result))
        return result;
    if (const HRESULT result = PopulateSettingsSkinCombo(hwnd, ctx, language, config.ime_skin_id); FAILED(result))
        return result;
    ::SetLastError(ERROR_SUCCESS);
    if (!::SetDlgItemTextW(hwnd, kIdLblCustomPrimaryPath,
            ResolveDictionaryPathDisplayText(custom_path, GetUiStrings(language).dlg_lbl_custom_dict_none))) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("Settings custom dictionary path label update failed (error={})", error);
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }

    ctx.custom_primary_dict_path = std::move(custom_path);
    ctx.preview_candidate_font = std::move(candidate_font);
    ctx.last_valid_primary_dict = config.primary_dict;
    if (ctx.draft_to_recover) EnableDraftEditing(hwnd, true);
    UpdateExtraDictCheckboxState(hwnd);
    RenderSettingsLanguageLabels(hwnd, language);
    ApplySettingsDisplayPreview(hwnd, ctx, language, display);
    ctx.draft_to_recover.reset();
    return S_OK;
}

} // namespace

HRESULT CaptureSettingsDraft(HWND hwnd, const DialogContext& ctx, QiQiConfig& draft) noexcept {
    try {
        if (ctx.draft_to_recover) {
            draft = *ctx.draft_to_recover;
            return S_OK;
        }
        draft = ctx.baseline_config;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    return ReadSettingsDraft(hwnd, draft);
}

HRESULT ReapplySettingsDraft(HWND hwnd, DialogContext& ctx, QiQiConfig draft) noexcept {
    const HRESULT result = ApplySettingsControls(hwnd, ctx, draft);
    if (!log::CheckHResult(result, "Settings::ReapplyDraft")) {
        ctx.draft_to_recover = std::move(draft);
        EnableDraftEditing(hwnd, false);
    } else {
        UpdateSettingsWindowTitle(hwnd, ctx);
    }
    return result;
}

HRESULT RenderSettingsDraft(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept {
    if (ctx.applying_draft) return E_PENDING;
    QiQiConfig previous;
    if (const HRESULT result = CaptureSettingsDraft(hwnd, ctx, previous); FAILED(result)) return result;
    const HRESULT result = ApplySettingsControls(hwnd, ctx, config);
    if (FAILED(result)) std::ignore = ReapplySettingsDraft(hwnd, ctx, std::move(previous));
    else UpdateSettingsWindowTitle(hwnd, ctx);
    return result;
}

void RefreshSettingsLanguage(HWND hwnd, DialogContext& ctx) noexcept {
    if (ctx.done || !ctx.controls_initialized || ctx.applying_draft || ctx.draft_to_recover) return;
    QiQiConfig draft;
    if (log::CheckHResult(CaptureSettingsDraft(hwnd, ctx, draft), "Settings::ReadDraftForLanguageRefresh")) {
        std::ignore = ReapplySettingsDraft(hwnd, ctx, std::move(draft));
    }
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    std::ignore = log::CheckHResult(
        WriteSettingsToWindowControlsResult(hwnd, config), "WriteSettingsToWindowControls");
}

HRESULT WriteSettingsToWindowControlsResult(HWND hwnd, const QiQiConfig& config) noexcept {
    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ctx) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    QiQiConfig baseline;
    try {
        baseline = config;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    NormalizeSettingsBaseline(baseline);
    const HRESULT result = ctx->controls_initialized ? RenderSettingsDraft(hwnd, *ctx, config)
                                                     : ApplySettingsControls(hwnd, *ctx, config);
    if (SUCCEEDED(result)) {
        ctx->baseline_config = std::move(baseline);
        UpdateSettingsWindowTitle(hwnd, *ctx);
    }
    return result;
}

HRESULT settings_detail::ReadSettingsDraft(HWND hwnd, QiQiConfig& config) noexcept {
    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (ctx && ctx->draft_to_recover) return E_PENDING;
    config.primary_dict = PrimaryDict::Jidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (::IsDlgButtonChecked(hwnd, radio.control_id) == BST_CHECKED) {
            config.primary_dict = radio.dict_value;
            break;
        }
    }
    if (ctx) {
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

    if (const auto language = ReadSettingsLanguageSelection(hwnd)) config.ui_language = *language;

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
    const HWND conversion = ::GetDlgItem(hwnd, kIdComboOutputConversion);
    const LRESULT mode = conversion ? ::SendMessageW(conversion, CB_GETCURSEL, 0, 0) : CB_ERR;
    if (mode < 0 || mode > std::to_underlying(OutputConversion::TraditionalToSimplified)) {
        QIQI_LOG_WARN("Settings output conversion selection unavailable (result={})", mode);
        return E_FAIL;
    }
    config.output_conversion = OutputConversion{gsl::narrow_cast<uint8_t>(mode)};
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
