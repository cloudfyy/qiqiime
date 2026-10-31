#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>

namespace qiqi::settings_detail {
namespace {

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct SettingsRadioBinding {
    int control_id;
    PrimaryDict dict_value;
    const wchar_t* UiStrings::* label_field;
    int y_offset_px;
    DWORD extra_style;
};

constexpr std::array<SettingsRadioBinding, 3> kSettingsDictRadios = {{
    {kIdRadioJidian,  PrimaryDict::Jidian,      &UiStrings::dlg_radio_jidian,   50, WS_GROUP},
    {kIdRadioHaifeng, PrimaryDict::Haifeng,     &UiStrings::dlg_radio_haifeng, 106, 0},
    {kIdRadioBuiltin, PrimaryDict::BuiltinOnly, &UiStrings::dlg_radio_builtin, 134, 0},
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
    {kIdChkExtraDict,      &QiQiConfig::enable_jidian_extra,          &UiStrings::dlg_chk_extra,             32,  78, 360},
    {kIdChkAutoCommit4,    &QiQiConfig::auto_commit_4code,            &UiStrings::dlg_chk_auto_commit4,      32, 232, 360},
    {kIdChkPushCommit5,    &QiQiConfig::push_commit_5th,              &UiStrings::dlg_chk_push_commit5,      32, 260, 360},
    {kIdChkQuickSelect,    &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,      32, 288, 360},
    {kIdChkCodeHint,       &QiQiConfig::show_code_hint,               &UiStrings::dlg_chk_code_hint,         32, 316, 360},
    {kIdChkDynamicFreq,    &QiQiConfig::dynamic_phrase_freq,          &UiStrings::dlg_chk_dynamic_freq,      32, 344, 360},
    {kIdChkShiftToggle,    &QiQiConfig::shift_toggle_mode,            &UiStrings::dlg_chk_shift_toggle,      32, 372, 360},
    {kIdChkUpDownPageTurn, &QiQiConfig::up_down_page_turn,            &UiStrings::dlg_chk_up_down_page_turn, 32, 400, 360},
    {kIdChkStatusBar,      &QiQiConfig::show_status_bar,              &UiStrings::dlg_chk_status_bar,       430,  46, 360},
    {kIdChkBoldUnderline,  &QiQiConfig::bold_underline,               &UiStrings::dlg_chk_bold_underline,   430, 174, 360},
    {kIdChkBeepEmpty4,     &QiQiConfig::beep_on_empty_4code,          &UiStrings::dlg_chk_beep_empty4,      430, 238, 360},
    {kIdChkBeepDup4,       &QiQiConfig::beep_on_dup_4code,            &UiStrings::dlg_chk_beep_dup4,        430, 272, 360},
    {kIdChkClearEmpty4,    &QiQiConfig::clear_on_empty_4code,         &UiStrings::dlg_chk_clear_empty4,     430, 306, 360},
    {kIdChkKeySound,       &QiQiConfig::enable_key_sound,             &UiStrings::dlg_chk_key_sound,        430, 340, 360},
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

void ApplyLiveSettingsLanguage(HWND hwnd, DialogContext& ctx) noexcept {
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    if (ctx.config && !ctx.save_on_accept) {
        ctx.config->ui_language = lang;
    }
    const auto& str = GetUiStrings(lang);

    ::SetWindowTextW(hwnd, str.dlg_title);
    for (const auto& radio : kSettingsDictRadios) {
        ::SetDlgItemTextW(hwnd, radio.control_id, str.*(radio.label_field));
    }
    for (const auto& binding : kSettingsCheckboxes) {
        ::SetDlgItemTextW(hwnd, binding.control_id, str.*(binding.label_field));
    }
    ::SetDlgItemTextW(hwnd, kIdLblPageSize, str.dlg_lbl_page_size);
    ::SetDlgItemTextW(hwnd, kIdLblUiLang, str.dlg_lbl_ui_lang);
    ::SetDlgItemTextW(hwnd, kIdLblUnderlineStyle, str.dlg_lbl_underline_style);
    ::SetDlgItemTextW(hwnd, kIdBtnAbout, str.dlg_btn_about);
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::SetDlgItemTextW(hwnd, kIdBtnCancel, str.dlg_btn_cancel);

    const auto replace_combo_items = [hwnd](int combo_id, std::span<const wchar_t* const> items) noexcept {
        const LRESULT cur_sel = ::SendDlgItemMessageW(hwnd, combo_id, CB_GETCURSEL, 0, 0);
        ::SendDlgItemMessageW(hwnd, combo_id, CB_RESETCONTENT, 0, 0);
        for (const wchar_t* item : items) {
            ::SendDlgItemMessageW(hwnd, combo_id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        }
        ::SendDlgItemMessageW(hwnd, combo_id, CB_SETCURSEL, cur_sel >= 0 ? cur_sel : 0, 0);
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
                if (std::wcscmp(name.data(), kAboutWndClass) == 0 ||
                    std::wcscmp(name.data(), kSettingsMessageWndClass) == 0) {
                    ::SendMessageW(owned, kSettingsLanguageChangedMessage,
                                   static_cast<WPARAM>(change->language), 0);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&update));
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
    if (current_ui_cfg.primary_dict != ctx.baseline_config.primary_dict) {
        merged_ui_cfg.primary_dict = current_ui_cfg.primary_dict;
    }
    for (const auto& binding : kSettingsCheckboxes) {
        if (current_ui_cfg.*(binding.config_field) != ctx.baseline_config.*(binding.config_field)) {
            merged_ui_cfg.*(binding.config_field) = current_ui_cfg.*(binding.config_field);
        }
    }
    if (current_ui_cfg.page_size != ctx.baseline_config.page_size) {
        merged_ui_cfg.page_size = current_ui_cfg.page_size;
    }
    if (current_ui_cfg.underline_style != ctx.baseline_config.underline_style) {
        merged_ui_cfg.underline_style = current_ui_cfg.underline_style;
    }
    const bool ui_lang_follows_external =
        (current_ui_cfg.ui_language == ctx.baseline_config.ui_language);
    if (!ui_lang_follows_external) {
        merged_ui_cfg.ui_language = current_ui_cfg.ui_language;
    }

    ctx.config_registry_sequence = loaded_seq;
    if (ctx.config) {
        *ctx.config = external_cfg;
    }

    WriteSettingsToWindowControls(hwnd, merged_ui_cfg);
    ctx.baseline_config = external_cfg;
    if (ui_lang_follows_external) {
        ApplyLiveSettingsLanguage(hwnd, ctx);
    } else {
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
    ctx.d2d.d2d_target->Clear(is_dark ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));

    const std::array<FluentCardSpec, 4> cards = {{
        {D2D1::RectF(S(18),  S(16),  S(404), S(196)), L"词库方案",           L"Dictionary"},
        {D2D1::RectF(S(18),  S(206), S(404), S(434)), L"编码与上屏习惯",     L"Input & Commit Behavior"},
        {D2D1::RectF(S(416), S(16),  S(802), S(202)), L"界面与外观",         L"Appearance & Display"},
        {D2D1::RectF(S(416), S(206), S(802), S(434)), L"提示音与反馈",       L"Sound & Feedback"},
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

    constexpr DWORD_PTR radio_group = MAKELONG(kIdRadioJidian, kIdRadioBuiltin);
    for (const auto& radio : kSettingsDictRadios) {
        CreateFluentChildControl(
            hwnd,
            instance,
            f,
            &ctx.d2d,
            L"BUTTON",
            str.*(radio.label_field),
            BS_AUTORADIOBUTTON | radio.extra_style,
            S(32),
            S(radio.y_offset_px),
            S(360),
            S(24),
            radio.control_id,
            radio_group);
    }

    for (const auto& binding : kSettingsCheckboxes) {
        CreateFluentChildControl(
            hwnd,
            instance,
            f,
            &ctx.d2d,
            L"BUTTON",
            str.*(binding.label_field),
            BS_AUTOCHECKBOX,
            S(binding.x_offset_px),
            S(binding.y_offset_px),
            S(binding.width_px),
            S(24),
            binding.control_id);
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"STATIC", str.dlg_lbl_page_size, 0, S(430), S(78), S(156), S(24), kIdLblPageSize);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, S(588), S(74), S(80), S(260), kIdComboPageSize);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETMINVISIBLE, 9, 0);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"STATIC", str.dlg_lbl_ui_lang, 0, S(430), S(112), S(156), S(24), kIdLblUiLang);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, S(588), S(108), S(200), S(200), kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"STATIC", str.dlg_lbl_underline_style, 0, S(430), S(146), S(156), S(24), kIdLblUnderlineStyle);
    const HWND underline_combo = CreateFluentChildControl(
        hwnd, instance, f, &ctx.d2d, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
        S(588), S(142), S(200), S(220), kIdComboUnderlineStyle);
    EnableFluentUnderlineComboPreview(underline_combo, kIdChkBoldUnderline);
    const std::array<const wchar_t*, 5> underline_items = {
        str.dlg_underline_dot,
        str.dlg_underline_solid,
        str.dlg_underline_dash,
        str.dlg_underline_squiggle,
        str.dlg_underline_none,
    };
    for (const wchar_t* item : underline_items) {
        ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_about, BS_OWNERDRAW | WS_TABSTOP, S(18), S(448), S(104), S(32), kIdBtnAbout);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_ok, BS_OWNERDRAW | WS_TABSTOP, S(596), S(448), S(98), S(32), kIdBtnOk);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_cancel, BS_OWNERDRAW | WS_TABSTOP, S(704), S(448), S(98), S(32), kIdBtnCancel);
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        ctx->baseline_config = config;
    }
    int checked_radio_id = kIdRadioJidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (config.primary_dict == radio.dict_value) {
            checked_radio_id = radio.control_id;
            break;
        }
    }
    ::CheckRadioButton(hwnd, kIdRadioJidian, kIdRadioBuiltin, checked_radio_id);

    for (const auto& binding : kSettingsCheckboxes) {
        ::CheckDlgButton(
            hwnd,
            binding.control_id,
            (config.*(binding.config_field)) ? BST_CHECKED : BST_UNCHECKED);
    }

    const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);

    const int lang_idx = (config.ui_language == UiLanguage::ZhCn)
        ? 1
        : (config.ui_language == UiLanguage::EnUs ? 2 : 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_SETCURSEL, static_cast<WPARAM>(lang_idx), 0);

    const int underline_idx = std::clamp(
        static_cast<int>(config.underline_style),
        0,
        static_cast<int>(CompositionUnderlineStyle::None));
    ::SendDlgItemMessageW(
        hwnd, kIdComboUnderlineStyle, CB_SETCURSEL, static_cast<WPARAM>(underline_idx), 0);
    UpdateExtraDictCheckboxState(hwnd);
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    config.primary_dict = PrimaryDict::Jidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (::IsDlgButtonChecked(hwnd, radio.control_id) == BST_CHECKED) {
            config.primary_dict = radio.dict_value;
            break;
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
}

} // namespace qiqi
