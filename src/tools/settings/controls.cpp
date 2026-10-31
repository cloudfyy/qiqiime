#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/theme.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>

namespace qiqi::settings_detail {
namespace {

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct SettingsCheckboxBinding {
    int control_id;
    bool QiQiConfig::* config_field;
    const wchar_t* UiStrings::* label_field;
    int x_offset_px;
    int y_offset_px;
    int width_px;
};

constexpr std::array<SettingsCheckboxBinding, 13> kSettingsCheckboxes = {{
    {kIdChkExtraDict,     &QiQiConfig::enable_jidian_extra,          &UiStrings::dlg_chk_extra,           32,  78, 360},
    {kIdChkAutoCommit4,   &QiQiConfig::auto_commit_4code,            &UiStrings::dlg_chk_auto_commit4,    32, 202, 360},
    {kIdChkPushCommit5,   &QiQiConfig::push_commit_5th,              &UiStrings::dlg_chk_push_commit5,    32, 230, 360},
    {kIdChkQuickSelect,   &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,    32, 258, 360},
    {kIdChkCodeHint,      &QiQiConfig::show_code_hint,               &UiStrings::dlg_chk_code_hint,       32, 286, 360},
    {kIdChkDynamicFreq,   &QiQiConfig::dynamic_phrase_freq,          &UiStrings::dlg_chk_dynamic_freq,    32, 314, 360},
    {kIdChkShiftToggle,   &QiQiConfig::shift_toggle_mode,            &UiStrings::dlg_chk_shift_toggle,    32, 342, 360},
    {kIdChkStatusBar,     &QiQiConfig::show_status_bar,              &UiStrings::dlg_chk_status_bar,     430,  50, 360},
    {kIdChkBeepEmpty4,    &QiQiConfig::beep_on_empty_4code,          &UiStrings::dlg_chk_beep_empty4,    430, 202, 360},
    {kIdChkBeepDup4,      &QiQiConfig::beep_on_dup_4code,            &UiStrings::dlg_chk_beep_dup4,      430, 230, 360},
    {kIdChkClearEmpty4,   &QiQiConfig::clear_on_empty_4code,         &UiStrings::dlg_chk_clear_empty4,   430, 258, 360},
    {kIdChkKeySound,      &QiQiConfig::enable_key_sound,             &UiStrings::dlg_chk_key_sound,      430, 286, 360},
    {kIdChkLogKeystrokes, &QiQiConfig::log_keystrokes,               &UiStrings::dlg_chk_log_keystrokes, 430, 314, 360},
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
    if (ctx.config) {
        ctx.config->ui_language = lang;
    }
    const auto& str = GetUiStrings(lang);

    ::SetWindowTextW(hwnd, str.dlg_title);
    ::SetDlgItemTextW(hwnd, kIdRadioJidian, str.dlg_radio_jidian);
    ::SetDlgItemTextW(hwnd, kIdRadioHaifeng, str.dlg_radio_haifeng);
    ::SetDlgItemTextW(hwnd, kIdRadioBuiltin, str.dlg_radio_builtin);
    for (const auto& binding : kSettingsCheckboxes) {
        ::SetDlgItemTextW(hwnd, binding.control_id, str.*(binding.label_field));
    }
    ::SetDlgItemTextW(hwnd, kIdLblPrivacyNotice, str.dlg_lbl_privacy_notice);
    ::SetDlgItemTextW(hwnd, kIdLblPageSize, str.dlg_lbl_page_size);
    ::SetDlgItemTextW(hwnd, kIdLblUiLang, str.dlg_lbl_ui_lang);
    ::SetDlgItemTextW(hwnd, kIdBtnAbout, str.dlg_btn_about);
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::SetDlgItemTextW(hwnd, kIdBtnCancel, str.dlg_btn_cancel);

    const LRESULT cur_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_RESETCONTENT, 0, 0);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_SETCURSEL, cur_sel >= 0 ? cur_sel : 0, 0);

    ::InvalidateRect(hwnd, nullptr, TRUE);
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
        {D2D1::RectF(S(18),  S(16),  S(404), S(166)), L"词库方案",           L"Dictionary"},
        {D2D1::RectF(S(18),  S(176), S(404), S(384)), L"编码与上屏习惯",     L"Input & Commit Behavior"},
        {D2D1::RectF(S(416), S(16),  S(802), S(166)), L"界面与外观",         L"Appearance & Display"},
        {D2D1::RectF(S(416), S(176), S(802), S(384)), L"提示音与诊断",       L"Sound & Diagnostics"},
    }};
    DrawFluentCardGroup(ctx.d2d, cards, is_en, is_dark);
}

void UpdateExtraDictCheckboxState(HWND hwnd) noexcept {
    const bool is_jidian = (::IsDlgButtonChecked(hwnd, kIdRadioJidian) == BST_CHECKED);
    if (const HWND chk = ::GetDlgItem(hwnd, kIdChkExtraDict)) {
        ::EnableWindow(chk, is_jidian ? TRUE : FALSE);
    }
}

void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept {
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScalePx(px); };
    const HFONT f = ctx.font.Get();
    const HFONT bold_f = ctx.bold_font.Get();
    const auto& str = GetUiStrings(ctx.config ? ctx.config->ui_language : UiLanguage::Auto);

    constexpr DWORD_PTR radio_group = MAKELONG(kIdRadioJidian, kIdRadioBuiltin);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_radio_jidian, BS_AUTORADIOBUTTON | WS_GROUP, S(32), S(50), S(360), S(24), kIdRadioJidian, radio_group);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_radio_haifeng, BS_AUTORADIOBUTTON, S(32), S(106), S(360), S(24), kIdRadioHaifeng, radio_group);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_radio_builtin, BS_AUTORADIOBUTTON, S(32), S(134), S(360), S(24), kIdRadioBuiltin, radio_group);

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

    CreateFluentChildControl(hwnd, instance, bold_f, &ctx.d2d, L"STATIC", str.dlg_lbl_privacy_notice, SS_NOPREFIX, S(448), S(340), S(340), S(36), kIdLblPrivacyNotice);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"STATIC", str.dlg_lbl_page_size, 0, S(430), S(84), S(156), S(24), kIdLblPageSize);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, S(588), S(80), S(80), S(260), kIdComboPageSize);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETMINVISIBLE, 9, 0);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"STATIC", str.dlg_lbl_ui_lang, 0, S(430), S(120), S(156), S(24), kIdLblUiLang);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, S(588), S(116), S(200), S(200), kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_about, BS_OWNERDRAW | WS_TABSTOP, S(18), S(398), S(104), S(32), kIdBtnAbout);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_ok, BS_OWNERDRAW | WS_TABSTOP, S(596), S(398), S(98), S(32), kIdBtnOk);
    CreateFluentChildControl(hwnd, instance, f, &ctx.d2d, L"BUTTON", str.dlg_btn_cancel, BS_OWNERDRAW | WS_TABSTOP, S(704), S(398), S(98), S(32), kIdBtnCancel);
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    ::CheckRadioButton(
        hwnd,
        kIdRadioJidian,
        kIdRadioBuiltin,
        config.primary_dict == PrimaryDict::Haifeng
            ? kIdRadioHaifeng
            : (config.primary_dict == PrimaryDict::BuiltinOnly ? kIdRadioBuiltin : kIdRadioJidian));

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
    UpdateExtraDictCheckboxState(hwnd);
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    if (::IsDlgButtonChecked(hwnd, kIdRadioHaifeng) == BST_CHECKED) {
        config.primary_dict = PrimaryDict::Haifeng;
    } else if (::IsDlgButtonChecked(hwnd, kIdRadioBuiltin) == BST_CHECKED) {
        config.primary_dict = PrimaryDict::BuiltinOnly;
    } else {
        config.primary_dict = PrimaryDict::Jidian;
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
}

} // namespace qiqi
