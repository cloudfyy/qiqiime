// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/support.h"
#include "common/dlgutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/uiutil.h"
#include "tools/settings/internal.h"

#include <limits>
#include <string>

namespace qiqi::test {
namespace {

using namespace settings_detail;

struct CheckboxCase {
    int id;
    bool QiQiConfig::* field;
    const wchar_t* UiStrings::* label;
};

constexpr std::array<CheckboxCase, 16> kCheckboxCases = {{
    {kIdChkExtraDict, &QiQiConfig::enable_jidian_extra, &UiStrings::dlg_chk_extra},
    {kIdChkAutoCommit4, &QiQiConfig::auto_commit_4code, &UiStrings::dlg_chk_auto_commit4},
    {kIdChkPushCommit5, &QiQiConfig::push_commit_5th, &UiStrings::dlg_chk_push_commit5},
    {kIdChkQuickSelect, &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select},
    {kIdChkCodeHint, &QiQiConfig::show_code_hint, &UiStrings::dlg_chk_code_hint},
    {kIdChkCodeCompletion, &QiQiConfig::enable_code_completion, &UiStrings::dlg_chk_code_completion},
    {kIdChkSingleCharacter, &QiQiConfig::single_character_mode, &UiStrings::dlg_chk_single_character},
    {kIdChkDynamicFreq, &QiQiConfig::dynamic_phrase_freq, &UiStrings::dlg_chk_dynamic_freq},
    {kIdChkShiftToggle, &QiQiConfig::shift_toggle_mode, &UiStrings::dlg_chk_shift_toggle},
    {kIdChkStatusBar, &QiQiConfig::show_status_bar, &UiStrings::dlg_chk_status_bar},
    {kIdChkBoldUnderline, &QiQiConfig::bold_underline, &UiStrings::dlg_chk_bold_underline},
    {kIdChkBeepEmpty4, &QiQiConfig::beep_on_empty_4code, &UiStrings::dlg_chk_beep_empty4},
    {kIdChkBeepDup4, &QiQiConfig::beep_on_dup_4code, &UiStrings::dlg_chk_beep_dup4},
    {kIdChkClearEmpty4, &QiQiConfig::clear_on_empty_4code, &UiStrings::dlg_chk_clear_empty4},
    {kIdChkKeySound, &QiQiConfig::enable_key_sound, &UiStrings::dlg_chk_key_sound},
    {kIdChkUpDownPageTurn, &QiQiConfig::up_down_page_turn, &UiStrings::dlg_chk_up_down_page_turn},
}};

void TestSettingsBindingBoundaries() {
    QiQiConfig cfg;
    cfg.ui_language = UiLanguage::ZhCn;
    cfg.user_dict_path = L"unchanged";
    cfg.enable_z_temp_english = false;
    for (const auto& test : kCheckboxCases) {
        cfg.*(test.field) = false;
    }
    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETCOUNT, 0, 0) == 2);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETCURSEL, 0, 0) == 0);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboSpaceEnter, CB_GETCOUNT, 0, 0) == 3);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboSpaceEnter, CB_GETCURSEL, 0, 0) == 0);
    for (const auto mode : {WubiSpaceEnterMode::SpaceSelectEnterCode,
                            WubiSpaceEnterMode::SpaceSelectEnterClear,
                            WubiSpaceEnterMode::EnterSelectSpaceClear}) {
        cfg.space_enter_mode = mode;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QiQiConfig read_mode;
        ReadSettingsFromWindowControls(hwnd.Get(), read_mode);
        QIQI_TEST_CHECK(read_mode.space_enter_mode == mode);
    }
    for (const auto layout : {CandidateLayout::Horizontal, CandidateLayout::Vertical}) {
        cfg.candidate_layout = layout;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QiQiConfig read_layout;
        ReadSettingsFromWindowControls(hwnd.Get(), read_layout);
        QIQI_TEST_CHECK(read_layout.candidate_layout == layout);
    }
    WPARAM language_index = 0;
    for (const UiLanguage language : {UiLanguage::Auto, UiLanguage::ZhCn, UiLanguage::EnUs}) {
        cfg.ui_language = language;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_GETCURSEL, 0, 0) ==
                        gsl::narrow_cast<LRESULT>(language_index++));
        QiQiConfig actual;
        ReadSettingsFromWindowControls(hwnd.Get(), actual);
        QIQI_TEST_CHECK(actual.ui_language == language);
    }
    for (const auto& selected : kCheckboxCases) {
        cfg.*(selected.field) = true;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QiQiConfig actual = cfg;
        ReadSettingsFromWindowControls(hwnd.Get(), actual);
        for (const auto& test : kCheckboxCases) {
            QIQI_TEST_CHECK((actual.*(test.field)) == (test.id == selected.id));
            QIQI_TEST_CHECK((::IsDlgButtonChecked(hwnd.Get(), test.id) == BST_CHECKED) == (test.id == selected.id));
        }
        QIQI_TEST_CHECK(actual.user_dict_path == cfg.user_dict_path);
        QIQI_TEST_CHECK(!actual.enable_z_temp_english);
        cfg.*(selected.field) = false;
    }
    constexpr std::array<size_t, 5> sizes = {0, 1, 9, 10, std::numeric_limits<size_t>::max()};
    constexpr std::array<size_t, 5> expected = {1, 1, 9, 9, 9};
    for (size_t i = 0; i < sizes.size(); ++i) {
        cfg.page_size = gsl::at(sizes, i);
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QiQiConfig actual;
        ReadSettingsFromWindowControls(hwnd.Get(), actual);
        QIQI_TEST_CHECK(actual.page_size == gsl::at(expected, i));
    }

    ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboUnderlineStyle, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboSpaceEnter, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboCandidateLayout, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    QiQiConfig actual;
    actual.page_size = 7;
    actual.ui_language = UiLanguage::EnUs;
    actual.underline_style = CompositionUnderlineStyle::Squiggle;
    actual.commit_on_english_switch = false;
    actual.space_enter_mode = WubiSpaceEnterMode::SpaceSelectEnterClear;
    actual.candidate_layout = CandidateLayout::Vertical;
    ReadSettingsFromWindowControls(hwnd.Get(), actual);
    QIQI_TEST_CHECK(actual.page_size == 7 && actual.ui_language == UiLanguage::EnUs &&
                    actual.underline_style == CompositionUnderlineStyle::Squiggle);
    QIQI_TEST_CHECK(!actual.commit_on_english_switch);
    QIQI_TEST_CHECK(actual.space_enter_mode == WubiSpaceEnterMode::SpaceSelectEnterClear);
    QIQI_TEST_CHECK(actual.candidate_layout == CandidateLayout::Vertical);
    for (const bool commit : {true, false}) {
        cfg.commit_on_english_switch = commit;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETCURSEL, 0, 0) ==
                        (commit ? 0 : 1));
        QiQiConfig read_policy;
        ReadSettingsFromWindowControls(hwnd.Get(), read_policy);
        QIQI_TEST_CHECK(read_policy.commit_on_english_switch == commit);
    }

    for (const CompositionUnderlineStyle style : {
             CompositionUnderlineStyle::Dot,
             CompositionUnderlineStyle::Solid,
             CompositionUnderlineStyle::Dash,
             CompositionUnderlineStyle::Squiggle,
             CompositionUnderlineStyle::None}) {
        cfg.underline_style = style;
        cfg.bold_underline = (style == CompositionUnderlineStyle::Solid ||
                              style == CompositionUnderlineStyle::Squiggle);
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkBoldUnderline)) ==
                        (style != CompositionUnderlineStyle::None ? TRUE : FALSE));
        QiQiConfig read_style;
        ReadSettingsFromWindowControls(hwnd.Get(), read_style);
        QIQI_TEST_CHECK(read_style.underline_style == style &&
                        read_style.bold_underline == cfg.bold_underline);
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(kIdComboUnderlineStyle, CBN_SELCHANGE), 0);
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(kIdChkBoldUnderline, BN_CLICKED), 0);
        if (const HWND combo = ::GetDlgItem(hwnd.Get(), kIdComboUnderlineStyle)) {
            const ScopedWindowDC combo_dc(combo);
            DRAWITEMSTRUCT dis{};
            dis.CtlType = ODT_COMBOBOX;
            dis.CtlID = kIdComboUnderlineStyle;
            dis.hwndItem = combo;
            dis.hDC = combo_dc.Get();
            dis.rcItem = {0, 0, 200, 22};
            dis.itemID = static_cast<UINT>(style);
            dis.itemState = (style == CompositionUnderlineStyle::Squiggle) ? ODS_SELECTED : 0;
            ::SendMessageW(hwnd.Get(), WM_DRAWITEM, kIdComboUnderlineStyle, reinterpret_cast<LPARAM>(&dis));
            dis.itemState = ODS_COMBOBOXEDIT;
            ::SendMessageW(hwnd.Get(), WM_DRAWITEM, kIdComboUnderlineStyle, reinterpret_cast<LPARAM>(&dis));
        }
    }
}

void TestSettingsLiveLanguagePreservesControls() {
    QiQiConfig cfg;
    cfg.page_size = 9;
    cfg.ui_language = UiLanguage::ZhCn;
    cfg.commit_on_english_switch = false;
    cfg.candidate_layout = CandidateLayout::Vertical;
    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    LOGFONTW system_face{};
    QIQI_TEST_CHECK(TryGetSystemUiFont(system_face));
    const auto system_family = LogFontFaceNameView(system_face);
    constexpr std::wstring_view yahei = L"Microsoft YaHei UI";
    const auto chinese_family = CandidateFontFamilyAvailable(yahei) ? yahei : system_family;
    const auto check_font = [&](std::wstring_view expected, UiTextScale scale = UiTextScale::Percent100) {
        const UINT dpi = context->d2d.dpi;
        const auto combo_font = reinterpret_cast<HFONT>(
            ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(combo_font && combo_font == context->font.Get());
        LOGFONTW regular{}, bold{};
        QIQI_TEST_CHECK(::GetObjectW(combo_font, sizeof(regular), &regular) == sizeof(regular) &&
                        LogFontFaceNameView(regular) == expected &&
                        regular.lfHeight == -ScaleDisplayPx(settings_layout::kFontSize, dpi, scale));
        QIQI_TEST_CHECK(::GetObjectW(context->bold_font.Get(), sizeof(bold), &bold) == sizeof(bold) &&
                        LogFontFaceNameView(bold) == expected &&
                        bold.lfHeight == -ScaleDisplayPx(settings_layout::kBoldFontSize, dpi, scale));
        if (expected == yahei) {
            const ScopedWindowDC dc(hwnd.Get());
            const ScopedSelectObject selected(dc.Get(), combo_font);
            std::array<wchar_t, LF_FACESIZE> realized{};
            QIQI_TEST_CHECK(dc && selected &&
                ::GetTextFaceW(dc.Get(), gsl::narrow_cast<int>(realized.size()), realized.data()) > 0);
            QIQI_TEST_CHECK(_wcsicmp(realized.data(), yahei.data()) == 0 ||
                std::wstring_view(realized.data()) == L"微软雅黑 UI");
        }

        for (const auto format : {context->d2d.fmt_body.Get(), context->d2d.fmt_caption.Get(),
                context->d2d.ButtonTextFormat(false), context->d2d.ButtonTextFormat(true)}) {
            std::array<wchar_t, LF_FACESIZE> family{};
            QIQI_TEST_CHECK(format && SUCCEEDED(format->GetFontFamilyName(
                family.data(), gsl::narrow_cast<UINT32>(family.size()))));
            QIQI_TEST_CHECK(std::wstring_view(family.data()) == expected);
        }
        QIQI_TEST_CHECK(context->d2d.fmt_body->GetFontSize() == static_cast<float>(-regular.lfHeight));
        for (const bool primary : {false, true}) {
            auto* const format = context->d2d.ButtonTextFormat(primary);
            QIQI_TEST_CHECK(format && format->GetFontSize() == context->d2d.fmt_body->GetFontSize());
            QIQI_TEST_CHECK(format->GetFontWeight() ==
                            (primary ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL));
        }
        LOGFONTW candidate{};
        QIQI_TEST_CHECK(::GetObjectW(context->candidate_preview_font.Get(), sizeof(candidate), &candidate) ==
                        sizeof(candidate) && LogFontFaceNameView(candidate) == system_family);
    };
    const auto select = [&](int id, WPARAM selection) {
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), id, CB_SETCURSEL, selection, 0) != CB_ERR);
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), 0);
    };
    const auto check_language_choices = [&](UiLanguage language, LRESULT selection) {
        const HWND combo = ::GetDlgItem(hwnd.Get(), kIdComboUiLang);
        const auto& strings = GetUiStrings(language);
        const std::array expected{strings.dlg_lang_auto, strings.dlg_lang_zh, strings.dlg_lang_en};
        QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == gsl::narrow_cast<LRESULT>(expected.size()));
        QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == selection);
        for (size_t item = 0; item < expected.size(); ++item) {
            std::array<wchar_t, 64> text{};
            const LRESULT size = ::SendMessageW(combo, CB_GETLBTEXTLEN, item, 0);
            QIQI_TEST_CHECK(size > 0 && size < gsl::narrow_cast<LRESULT>(text.size()));
            QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETLBTEXT, item, reinterpret_cast<LPARAM>(text.data())) == size);
            QIQI_TEST_CHECK(std::wstring_view(text.data()) == gsl::at(expected, item));
        }
    };
    check_language_choices(UiLanguage::ZhCn, 1);
    check_font(chinese_family);
    ::SendDlgItemMessageW(hwnd.Get(), kIdRadioHaifeng, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    const UINT extra_checked = ::IsDlgButtonChecked(hwnd.Get(), kIdChkExtraDict);
    ::SendDlgItemMessageW(hwnd.Get(), kIdChkExtraDict, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), kIdChkExtraDict) == extra_checked);

    for (const WPARAM selection : {2u, 1u, 0u}) {
        select(kIdComboUiLang, selection);
        QiQiConfig actual;
        ReadSettingsFromWindowControls(hwnd.Get(), actual);
        QIQI_TEST_CHECK(actual.primary_dict == PrimaryDict::Haifeng && actual.page_size == 9);
        const auto expected_language = selection == 2 ? UiLanguage::EnUs :
            selection == 1 ? UiLanguage::ZhCn : UiLanguage::Auto;
        QIQI_TEST_CHECK(actual.ui_language == expected_language && cfg.ui_language == UiLanguage::ZhCn);
        check_font(ResolveUiLanguage(expected_language) == UiLanguage::ZhCn ? chinese_family : system_family);
        check_language_choices(expected_language, gsl::narrow_cast<LRESULT>(selection));
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_GETCOUNT, 0, 0) == 9);
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETCOUNT, 0, 0) == 2);
        QIQI_TEST_CHECK(!actual.commit_on_english_switch);
        QIQI_TEST_CHECK(actual.candidate_layout == CandidateLayout::Vertical);
        const auto& str = GetUiStrings(actual.ui_language);
        std::array<wchar_t, 128> switch_label{};
        ::GetDlgItemTextW(hwnd.Get(), kIdLblEnglishSwitch, switch_label.data(),
                         gsl::narrow_cast<int>(switch_label.size()));
        QIQI_TEST_CHECK(std::wstring(switch_label.data()) == str.dlg_lbl_english_switch);
        std::array<wchar_t, 128> space_enter_label{};
        ::GetDlgItemTextW(hwnd.Get(), kIdLblSpaceEnter, space_enter_label.data(),
                         gsl::narrow_cast<int>(space_enter_label.size()));
        QIQI_TEST_CHECK(std::wstring_view(space_enter_label.data()) == str.dlg_lbl_space_enter);
        for (size_t item = 0; item < str.dlg_space_enter_modes.size(); ++item) {
            std::array<wchar_t, 128> text{};
            const LRESULT length = ::SendDlgItemMessageW(hwnd.Get(), kIdComboSpaceEnter, CB_GETLBTEXTLEN, item, 0);
            QIQI_TEST_CHECK(length > 0 && length < gsl::narrow_cast<LRESULT>(text.size()));
            QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboSpaceEnter, CB_GETLBTEXT, item,
                                reinterpret_cast<LPARAM>(text.data())) == length);
            QIQI_TEST_CHECK(std::wstring_view(text.data()) == gsl::at(str.dlg_space_enter_modes, item));
        }
        std::array<wchar_t, 64> restore_label{};
        ::GetDlgItemTextW(hwnd.Get(), kIdBtnRestoreDefaults, restore_label.data(),
                         gsl::narrow_cast<int>(restore_label.size()));
        QIQI_TEST_CHECK(std::wstring(restore_label.data()) == str.dlg_btn_restore_defaults);
        for (const WPARAM item : {0u, 1u}) {
            std::array<wchar_t, 128> text{};
            QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETLBTEXTLEN, item, 0) <
                            gsl::narrow_cast<LRESULT>(text.size()));
            QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_GETLBTEXT, item,
                                                 reinterpret_cast<LPARAM>(text.data())) != CB_ERR);
            QIQI_TEST_CHECK(std::wstring(text.data()) ==
                            (item == 0 ? str.dlg_english_switch_commit : str.dlg_english_switch_clear));
            QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboCandidateLayout, CB_GETLBTEXTLEN, item, 0) <
                            gsl::narrow_cast<LRESULT>(text.size()));
            QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboCandidateLayout, CB_GETLBTEXT, item,
                                                 reinterpret_cast<LPARAM>(text.data())) != CB_ERR);
            QIQI_TEST_CHECK(std::wstring(text.data()) ==
                            (item == 0 ? str.dlg_candidate_horizontal : str.dlg_candidate_vertical));
        }
        for (const auto& test : kCheckboxCases) {
            const bool expected = test.id != kIdChkExtraDict && (cfg.*(test.field));
            QIQI_TEST_CHECK((actual.*(test.field)) == expected);
            std::array<wchar_t, 256> label{};
            ::GetDlgItemTextW(hwnd.Get(), test.id, label.data(), gsl::narrow_cast<int>(label.size()));
            QIQI_TEST_CHECK(std::wstring(label.data()) == str.*(test.label));
        }
    }
    select(kIdComboUiLang, 1);
    select(kIdComboUiTextScale, static_cast<WPARAM>(UiTextScale::Percent150));
    check_font(chinese_family, UiTextScale::Percent150);
    QIQI_TEST_CHECK(RefreshSettingsUiAppearance(
        hwnd.Get(), *context, UiLanguage::ZhCn, context->d2d.display_preferences, context->d2d.dpi,
        [](std::wstring_view) noexcept { return false; }));
    check_font(system_family, UiTextScale::Percent150);
    ::SendDlgItemMessageW(hwnd.Get(), kIdRadioJidian, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), kIdRadioHaifeng) == BST_UNCHECKED);
}

void TestSettingsDirtyTitle() {
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const bool persist : {false, true}) {
            QiQiConfig cfg;
            cfg.ui_language = language;
            const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg, persist));
            QIQI_TEST_CHECK(window);
            const auto check_title = [&](UiLanguage selected, bool dirty) {
                std::array<wchar_t, 256> title{};
                QIQI_TEST_CHECK(::GetWindowTextW(
                    window.Get(), title.data(), gsl::narrow_cast<int>(title.size())) > 0);
                QIQI_TEST_CHECK(std::wstring(title.data()) ==
                    std::wstring(GetUiStrings(selected).dlg_title) + (dirty ? L" *" : L""));
            };
            const auto select_combo = [&](int id, WPARAM value) {
                ::SendDlgItemMessageW(window.Get(), id, CB_SETCURSEL, value, 0);
                ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), 0);
            };
            check_title(language, false);
            for (const auto& checkbox : kCheckboxCases) {
                ::SendDlgItemMessageW(window.Get(), checkbox.id, BM_CLICK, 0, 0);
                check_title(language, true);
                ::SendDlgItemMessageW(window.Get(), checkbox.id, BM_CLICK, 0, 0);
                check_title(language, false);
            }
            for (const int radio : {kIdRadioHaifeng, kIdRadioBuiltin}) {
                ::SendDlgItemMessageW(window.Get(), radio, BM_CLICK, 0, 0);
                check_title(language, true);
                ::SendDlgItemMessageW(window.Get(), kIdRadioJidian, BM_CLICK, 0, 0);
                check_title(language, true);
                QIQI_TEST_CHECK(::IsDlgButtonChecked(window.Get(), kIdChkExtraDict) == BST_UNCHECKED);
                ::SendDlgItemMessageW(window.Get(), kIdChkExtraDict, BM_CLICK, 0, 0);
                check_title(language, false);
            }
            for (const int combo : {kIdComboPageSize, kIdComboCandidateLayout,
                                   kIdComboUnderlineStyle, kIdComboEnglishSwitch, kIdComboSpaceEnter}) {
                const auto initial = ::SendDlgItemMessageW(window.Get(), combo, CB_GETCURSEL, 0, 0);
                select_combo(combo, initial == 0 ? 1u : 0u);
                check_title(language, true);
                select_combo(combo, gsl::narrow_cast<WPARAM>(initial));
                check_title(language, false);
            }
            const auto other_language = language == UiLanguage::ZhCn ? UiLanguage::EnUs : UiLanguage::ZhCn;
            select_combo(kIdComboUiLang, other_language == UiLanguage::ZhCn ? 1u : 2u);
            check_title(other_language, true);
            select_combo(kIdComboUiLang, language == UiLanguage::ZhCn ? 1u : 2u);
            check_title(language, false);

            ::SendDlgItemMessageW(window.Get(), kIdChkKeySound, BM_CLICK, 0, 0);
            ::SendDlgItemMessageW(window.Get(), kIdChkAutoCommit4, BM_CLICK, 0, 0);
            ::SendDlgItemMessageW(window.Get(), kIdChkKeySound, BM_CLICK, 0, 0);
            check_title(language, true);
            for (const UINT event : {WM_THEMECHANGED, WM_SETTINGCHANGE}) {
                ::SendMessageW(window.Get(), event, 0, 0);
                check_title(language, true);
            }
            const RECT suggested{100, 100, 1400, 900};
            ::SendMessageW(window.Get(), WM_DPICHANGED, MAKEWPARAM(144, 144),
                           reinterpret_cast<LPARAM>(&suggested));
            check_title(language, true);
            ::SendDlgItemMessageW(window.Get(), kIdChkAutoCommit4, BM_CLICK, 0, 0);
            check_title(language, false);
            ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
            check_title(UiLanguage::Auto, true);
            WriteSettingsToWindowControls(window.Get(), QiQiConfig{});
            ::SendDlgItemMessageW(window.Get(), kIdChkKeySound, BM_CLICK, 0, 0);
            check_title(UiLanguage::Auto, true);
            ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
            check_title(UiLanguage::Auto, false);
            QiQiConfig with_path;
            with_path.primary_dict_path = L"previously_selected_primary.txt";
            WriteSettingsToWindowControls(window.Get(), with_path);
            check_title(UiLanguage::Auto, false);
            ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
            check_title(UiLanguage::Auto, true);
        }
    }
}

void TestModalLoopTermination() {
    QIQI_TEST_CHECK(::PostThreadMessageW(::GetCurrentThreadId(), WM_APP, 0, 0));
    ::PostQuitMessage(29);
    CheckQuitMessage(29);
    constexpr bool done = true;
    QIQI_TEST_CHECK(!dialog_detail::RunModalDialogLoop(nullptr, done));
    const ScopedWindow hwnd(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(hwnd && dialog_detail::RunModalDialogLoop(hwnd.Get(), done));

    for (const bool about : {false, true}) {
        QiQiConfig cfg;
        ::PostQuitMessage(73);
        const bool shown = about
            ? ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr)
            : ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg);
        QIQI_TEST_CHECK(!shown);
        CheckQuitMessage(73);
    }
    for (const bool about : {false, true}) {
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            if (const HWND window = FindThreadWindowByClass(kSettingsWndClass)) {
                ::DestroyWindow(window);
            } else if (const HWND about_window = FindThreadWindowByClass(kInfoWndClass)) {
                ::DestroyWindow(about_window);
            }
            ::PostQuitMessage(91);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
        QiQiConfig cfg;
        const bool shown = about
            ? ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr)
            : ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg);
        QIQI_TEST_CHECK(!shown);
        CheckQuitMessage(91);
    }
}

} // namespace

void RunSettingsDialogEdgeTests() {
    TestSettingsBindingBoundaries();
    TestSettingsLiveLanguagePreservesControls();
    TestSettingsDirtyTitle();
    TestModalLoopTermination();
}

} // namespace qiqi::test
