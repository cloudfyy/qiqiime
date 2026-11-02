#include "common/test_support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "tools/settings/util.h"
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

constexpr std::array<CheckboxCase, 14> kCheckboxCases = {{
    {kIdChkExtraDict, &QiQiConfig::enable_jidian_extra, &UiStrings::dlg_chk_extra},
    {kIdChkAutoCommit4, &QiQiConfig::auto_commit_4code, &UiStrings::dlg_chk_auto_commit4},
    {kIdChkPushCommit5, &QiQiConfig::push_commit_5th, &UiStrings::dlg_chk_push_commit5},
    {kIdChkQuickSelect, &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select},
    {kIdChkCodeHint, &QiQiConfig::show_code_hint, &UiStrings::dlg_chk_code_hint},
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
    QiQiConfig actual;
    actual.page_size = 7;
    actual.ui_language = UiLanguage::EnUs;
    actual.underline_style = CompositionUnderlineStyle::Squiggle;
    ReadSettingsFromWindowControls(hwnd.Get(), actual);
    QIQI_TEST_CHECK(actual.page_size == 7 && actual.ui_language == UiLanguage::EnUs &&
                    actual.underline_style == CompositionUnderlineStyle::Squiggle);

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
    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd);
    ::SendDlgItemMessageW(hwnd.Get(), kIdRadioHaifeng, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    const UINT extra_checked = ::IsDlgButtonChecked(hwnd.Get(), kIdChkExtraDict);
    ::SendDlgItemMessageW(hwnd.Get(), kIdChkExtraDict, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), kIdChkExtraDict) == extra_checked);

    for (const WPARAM selection : {2u, 1u}) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_SETCURSEL, selection, 0);
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(kIdComboUiLang, CBN_SELCHANGE), 0);
        QiQiConfig actual;
        ReadSettingsFromWindowControls(hwnd.Get(), actual);
        QIQI_TEST_CHECK(actual.primary_dict == PrimaryDict::Haifeng && actual.page_size == 9);
        QIQI_TEST_CHECK(actual.ui_language == (selection == 2 ? UiLanguage::EnUs : UiLanguage::ZhCn));
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_GETCOUNT, 0, 0) == 3);
        QIQI_TEST_CHECK(::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_GETCOUNT, 0, 0) == 9);
        const auto& str = GetUiStrings(actual.ui_language);
        for (const auto& test : kCheckboxCases) {
            QIQI_TEST_CHECK((actual.*(test.field)) == (cfg.*(test.field)));
            std::array<wchar_t, 256> label{};
            ::GetDlgItemTextW(hwnd.Get(), test.id, label.data(), gsl::narrow_cast<int>(label.size()));
            QIQI_TEST_CHECK(std::wstring(label.data()) == str.*(test.label));
        }
    }
    ::SendDlgItemMessageW(hwnd.Get(), kIdRadioJidian, BM_CLICK, 0, 0);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), kIdRadioHaifeng) == BST_UNCHECKED);
}

void CheckQuitMessage(int exit_code) {
    MSG msg{};
    QIQI_TEST_CHECK(::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    QIQI_TEST_CHECK(msg.wParam == static_cast<WPARAM>(exit_code));
}

void TestModalLoopTermination() {
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
            } else if (const HWND about_window = FindThreadWindowByClass(kAboutWndClass)) {
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
    TestModalLoopTermination();
}

} // namespace qiqi::test
