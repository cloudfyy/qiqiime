#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>

namespace qiqi::settings_detail {
namespace {

namespace layout = settings_layout;

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
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

constexpr std::array<SettingsCheckboxBinding, 14> kSettingsCheckboxes = {{
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

} // namespace

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

void ApplyLiveSettingsLanguage(HWND hwnd, DialogContext& ctx, bool update_bound_config) noexcept {
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    if (update_bound_config && ctx.config && !ctx.save_on_accept) {
        ctx.config->ui_language = lang;
    }
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
    ::SetDlgItemTextW(hwnd, kIdBtnBrowsePrimaryDict, str.dlg_btn_browse_dict);
    ::SetDlgItemTextW(
        hwnd,
        kIdLblCustomPrimaryPath,
        ResolveDictionaryPathDisplayText(ctx.custom_primary_dict_path, str.dlg_lbl_custom_dict_none));
    ::SetDlgItemTextW(hwnd, kIdBtnAbout, str.dlg_btn_about);
    ::SetDlgItemTextW(hwnd, kIdBtnDictManager, str.dlg_btn_dict_mgr);
    ::SetDlgItemTextW(hwnd, kIdBtnHelp, str.dlg_btn_help);
    ::SetDlgItemTextW(hwnd, kIdBtnRestoreDefaults, str.dlg_btn_restore_defaults);
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::SetDlgItemTextW(hwnd, kIdBtnCancel, str.dlg_btn_cancel);

    const auto replace_combo_items = [hwnd](int combo_id, std::span<const wchar_t* const> items) noexcept {
        std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, combo_id), items),
            "Settings::ReplaceComboBoxItems");
    };
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    replace_combo_items(kIdComboUiLang, lang_items);

    const std::array<const wchar_t*, 5> underline_items = {
        str.dlg_underline_dot,
        str.dlg_underline_solid,
        str.dlg_underline_dash,
        str.dlg_underline_squiggle,
        str.dlg_underline_none,
    };
    replace_combo_items(kIdComboUnderlineStyle, underline_items);
    const std::array<const wchar_t*, 2> english_switch_items = {
        str.dlg_english_switch_commit, str.dlg_english_switch_clear
    };
    replace_combo_items(kIdComboEnglishSwitch, english_switch_items);

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
                                   static_cast<WPARAM>(change->language), 0);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&update));
    UpdateSettingsWindowTitle(hwnd, ctx);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx) noexcept {
    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == ctx.config_registry_sequence) {
        return;
    }

    QiQiConfig external_cfg{};
    uint64_t loaded_seq = 0;
    if (!LoadConfigFromRegistry(external_cfg, &loaded_seq)) {
        return;
    }

    QiQiConfig current_ui_cfg = ctx.baseline_config;
    ReadSettingsFromWindowControls(hwnd, current_ui_cfg);

    QiQiConfig merged_ui_cfg = external_cfg;
    MergeSettingsConfig(merged_ui_cfg, ctx.baseline_config, current_ui_cfg);
    const bool ui_lang_follows_external =
        (current_ui_cfg.ui_language == ctx.baseline_config.ui_language);

    ctx.config_registry_sequence = loaded_seq;
    if (ctx.config) {
        *ctx.config = external_cfg;
    }

    WriteSettingsToWindowControls(hwnd, merged_ui_cfg);
    ctx.baseline_config = external_cfg;
    if (ui_lang_follows_external) {
        ApplyLiveSettingsLanguage(hwnd, ctx);
    } else {
        UpdateSettingsWindowTitle(hwnd, ctx);
        ::InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept {
    if (!ctx.d2d.d2d_target) {
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScaleF(px); };
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    const bool is_en = (ResolveUiLanguage(lang) == UiLanguage::EnUs);

    const ScopedD2DDcDraw draw_guard(ctx.d2d.d2d_target.Get(), hdc, client_rc);
    if (!draw_guard) {
        return;
    }
    ctx.d2d.d2d_target->Clear(ColorRefToD2DColor(is_dark ? kDarkWindowBg : kLightCaptionBg));

    const auto card_rect = [&S](const RECT& rect) noexcept {
        return D2D1::RectF(S(rect.left), S(rect.top), S(rect.right), S(rect.bottom));
    };
    const std::array<FluentCardSpec, 4> cards = {{
        {card_rect(layout::kDictionaryCard), L"词库方案", L"Dictionary"},
        {card_rect(layout::kInputCard), L"编码与上屏习惯", L"Input & Commit Behavior"},
        {card_rect(layout::kAppearanceCard), L"界面与外观", L"Appearance & Display"},
        {card_rect(layout::kSoundCard), L"提示音与反馈", L"Sound & Feedback"},
    }};
    DrawFluentCardGroup(ctx.d2d, cards, is_en, is_dark);
}

void UpdateExtraDictCheckboxState(HWND hwnd) noexcept {
    const bool is_jidian = (::IsDlgButtonChecked(hwnd, kIdRadioJidian) == BST_CHECKED);
    if (const HWND chk = ::GetDlgItem(hwnd, kIdChkExtraDict)) {
        ::EnableWindow(chk, is_jidian ? TRUE : FALSE);
    }
    const LRESULT underline_sel = ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_GETCURSEL, 0, 0);
    const bool has_underline =
        underline_sel != static_cast<LRESULT>(CompositionUnderlineStyle::None);
    if (const HWND bold_chk = ::GetDlgItem(hwnd, kIdChkBoldUnderline)) {
        ::EnableWindow(bold_chk, has_underline ? TRUE : FALSE);
    }
}

void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept {
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScalePx(px); };
    const HFONT f = ctx.font.Get();
    const auto& str = GetUiStrings(ctx.config ? ctx.config->ui_language : UiLanguage::Auto);
    const auto create_control = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                                    const RECT& rect, int id, DWORD_PTR group = 0) noexcept {
        return CreateFluentChildControl(
            hwnd, instance, f, &ctx.d2d, cls, text, style,
            S(rect.left), S(rect.top), S(rect.right - rect.left), S(rect.bottom - rect.top), id, group);
    };

    constexpr DWORD_PTR radio_group = MAKELONG(kIdRadioJidian, kIdRadioCustom);
    for (const auto& radio : kSettingsDictRadios) {
        create_control(L"BUTTON", str.*(radio.label_field), BS_AUTORADIOBUTTON | radio.extra_style,
            RECT{layout::kLeftColumn, radio.y_offset_px, layout::kLeftColumn + radio.width_px,
                 radio.y_offset_px + layout::kRadioHeight}, radio.control_id, radio_group);
    }
    create_control(L"BUTTON", str.dlg_btn_browse_dict, BS_OWNERDRAW | WS_TABSTOP,
                   layout::kBrowseDictionary, kIdBtnBrowsePrimaryDict);
    create_control(L"STATIC",
        ResolveDictionaryPathDisplayText(ctx.custom_primary_dict_path, str.dlg_lbl_custom_dict_none),
        SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, layout::kDictionaryPath, kIdLblCustomPrimaryPath);

    for (const auto& binding : kSettingsCheckboxes) {
        create_control(L"BUTTON", str.*(binding.label_field), BS_AUTOCHECKBOX,
            RECT{binding.x_offset_px, binding.y_offset_px, binding.x_offset_px + binding.width_px,
                 binding.y_offset_px + layout::kCheckboxHeight}, binding.control_id);
    }

    constexpr DWORD combo_style = CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP;
    create_control(L"STATIC", str.dlg_lbl_page_size, 0, layout::kPageSizeLabel, kIdLblPageSize);
    create_control(L"COMBOBOX", L"", combo_style, layout::kPageSizeCombo, kIdComboPageSize);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETMINVISIBLE, kPageSizeLabels.size(), 0);
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboPageSize), kPageSizeLabels),
        "Settings::ReplaceComboBoxItems(page size)");

    create_control(L"STATIC", str.dlg_lbl_ui_lang, 0, layout::kLanguageLabel, kIdLblUiLang);
    create_control(L"COMBOBOX", L"", combo_style, layout::kLanguageCombo, kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboUiLang), lang_items),
        "Settings::ReplaceComboBoxItems(language)");

    create_control(L"STATIC", str.dlg_lbl_underline_style, 0, layout::kUnderlineLabel, kIdLblUnderlineStyle);
    const HWND underline_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kUnderlineCombo, kIdComboUnderlineStyle);
    EnableFluentUnderlineComboPreview(underline_combo, kIdChkBoldUnderline);
    const std::array<const wchar_t*, 5> underline_items = {
        str.dlg_underline_dot,
        str.dlg_underline_solid,
        str.dlg_underline_dash,
        str.dlg_underline_squiggle,
        str.dlg_underline_none,
    };
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(underline_combo, underline_items),
        "Settings::ReplaceComboBoxItems(underline)");

    create_control(L"STATIC", str.dlg_lbl_english_switch, 0, layout::kEnglishSwitchLabel, kIdLblEnglishSwitch);
    const HWND english_switch_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kEnglishSwitchCombo, kIdComboEnglishSwitch);
    const std::array<const wchar_t*, 2> english_switch_items = {
        str.dlg_english_switch_commit, str.dlg_english_switch_clear
    };
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(english_switch_combo, english_switch_items),
        "Settings::ReplaceComboBoxItems(English switch)");

    constexpr DWORD button_style = BS_OWNERDRAW | WS_TABSTOP;
    create_control(L"BUTTON", str.dlg_btn_about, button_style, layout::kAboutButton, kIdBtnAbout);
    create_control(L"BUTTON", str.dlg_btn_dict_mgr, button_style, layout::kDictionaryManagerButton, kIdBtnDictManager);
    create_control(L"BUTTON", str.dlg_btn_help, button_style, layout::kHelpButton, kIdBtnHelp);
    create_control(L"BUTTON", str.dlg_btn_restore_defaults, button_style,
                   layout::kRestoreDefaultsButton, kIdBtnRestoreDefaults);
    create_control(L"BUTTON", str.dlg_btn_ok, button_style, layout::kOkButton, kIdBtnOk);
    create_control(L"BUTTON", str.dlg_btn_cancel, button_style, layout::kCancelButton, kIdBtnCancel);
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        try {
            ctx->baseline_config = config;
            ctx->custom_primary_dict_path = config.primary_dict_path;
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("WriteSettingsToWindowControls: failed to copy configuration strings");
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
    UpdateExtraDictCheckboxState(hwnd);
    if (const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        UpdateSettingsWindowTitle(hwnd, *ctx);
    }
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
}

} // namespace qiqi
