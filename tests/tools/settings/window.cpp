// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/binary_codec.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "ime/engine/binary_dict.h"
#include "tools/settings/internal.h"
#include "tools/settings/window.h"
#include "tools/dictionary_validation.h"

#include <windows.h>
#include <array>
#include <string>
#include <tuple>
#include <utility>

namespace qiqi::test {

namespace {

thread_local int dictionary_confirmation_button = IDCANCEL;
thread_local bool dictionary_confirmation_seen = false;
thread_local bool unexpected_dictionary_picker_seen = false;

void TestSettingsDictionaryDependencies() {
    using namespace settings_detail;
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = true;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    unexpected_dictionary_picker_seen = false;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 30, [](HWND, UINT, UINT_PTR, DWORD) noexcept {
        if (const HWND picker = FindThreadWindowByClass(L"#32770")) {
            unexpected_dictionary_picker_seen = true;
            ::SendMessageW(picker, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cleanup = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    const auto click = [&](int id) {
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    };
    const HWND browse = ::GetDlgItem(dialog.Get(), kIdBtnBrowsePrimaryDict);
    const HWND extra = ::GetDlgItem(dialog.Get(), kIdChkExtraDict);
    QIQI_TEST_CHECK(!::IsWindowEnabled(browse) && !::IsWindowEnabled(extra));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
    std::array<wchar_t, 128> title{};
    ::GetWindowTextW(dialog.Get(), title.data(), gsl::narrow_cast<int>(title.size()));
    QIQI_TEST_CHECK(std::wstring(title.data()) == GetUiStrings(UiLanguage::EnUs).dlg_title);
    QIQI_TEST_CHECK(config.enable_jidian_extra);
    click(kIdBtnBrowsePrimaryDict);
    QIQI_TEST_CHECK(!unexpected_dictionary_picker_seen);
    click(kIdRadioCustom);
    QIQI_TEST_CHECK(!unexpected_dictionary_picker_seen);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdRadioCustom) == BST_CHECKED);
    QIQI_TEST_CHECK(::IsWindowEnabled(browse) && !::IsWindowEnabled(extra));
    QiQiConfig edited;
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.primary_dict == PrimaryDict::Custom && edited.primary_dict_path.empty());
    WithDismissedMessage(GetUiStrings(UiLanguage::EnUs).dlg_invalid_primary_dict, [&] {
        click(IDOK);
    });
    const auto* context = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context && !context->done && !context->accepted);
    QIQI_TEST_CHECK(config.primary_dict == PrimaryDict::BuiltinOnly);
    for (const int id : {kIdRadioHaifeng, kIdRadioBuiltin, kIdRadioCustom}) {
        click(kIdRadioJidian);
        QIQI_TEST_CHECK(::IsWindowEnabled(extra));
        QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
        ::CheckDlgButton(dialog.Get(), kIdChkExtraDict, BST_CHECKED);
        click(id);
        QIQI_TEST_CHECK(::IsWindowEnabled(browse) == (id == kIdRadioCustom));
        QIQI_TEST_CHECK(!::IsWindowEnabled(extra));
        QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
        ReadSettingsFromWindowControls(dialog.Get(), edited);
        QIQI_TEST_CHECK(!edited.enable_jidian_extra);
    }
    click(kIdRadioJidian);
    QIQI_TEST_CHECK(::IsWindowEnabled(extra));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
}

int WINAPI FailDictionaryDialog(HWND, LPCWSTR, LPCWSTR, UINT) noexcept {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
}

template <typename Action>
void WithDictionaryConfirmation(int button, Action action) {
    dictionary_confirmation_button = button;
    dictionary_confirmation_seen = false;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND dialog = FindThreadWindowByClass(L"#32770");
        if (!dialog) {
            return;
        }
        ::KillTimer(nullptr, id);
        dictionary_confirmation_seen = true;
        const HWND button = ::GetDlgItem(dialog, dictionary_confirmation_button);
        QIQI_TEST_CHECK(button);
        ::SendMessageW(button, BM_CLICK, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cleanup = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    action();
    QIQI_TEST_CHECK(dictionary_confirmation_seen);
}

void TestDictionaryConfirmationText() {
    std::map<uint32_t, std::vector<std::wstring>> entries;
    entries[PackWubiCode(L"aaaa")] = {L"词"};
    std::vector<uint8_t> binary;
    WubiMappedDict::CompileEntriesToBinaryBuffer(entries, binary);
    PrimaryDictValidationResult validation{
        PrimaryDictValidationStatus::Valid, ERROR_SUCCESS,
        WubiMappedDict::LoadFromBinaryBuffer(std::move(binary)),
        {1, 10, {{2, DictionaryLineError::MissingWord}}}
    };
    const auto zh = dictionary_ui::ValidationDetails(validation, UiLanguage::ZhCn);
    const auto en = dictionary_ui::ValidationDetails(validation, UiLanguage::EnUs);
    QIQI_TEST_CHECK(zh.find(L"有效行：1。无效行：10。") != std::wstring::npos &&
        zh.find(L"第 2 行：缺少词语") != std::wstring::npos);
    QIQI_TEST_CHECK(en.find(L"Valid rows: 1. Invalid rows: 10.") != std::wstring::npos &&
        en.find(L"Line 2: Missing word") != std::wstring::npos);
    QIQI_TEST_CHECK(!dictionary_ui::ConfirmPartialDictionary(
        nullptr, UiLanguage::ZhCn, validation, &FailDictionaryDialog));
}

void TestSettingsDialogControls() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto restore_theme = gsl::finally([original_theme]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
    });
    QiQiConfig cfg{};
    cfg.primary_dict = PrimaryDict::Haifeng;
    cfg.enable_jidian_extra = false;
    cfg.auto_commit_4code = false;
    cfg.push_commit_5th = false;
    cfg.semicolon_quote_quick_select = false;
    cfg.show_code_hint = false;
    cfg.dynamic_phrase_freq = true;
    cfg.shift_toggle_mode = false;
    cfg.commit_on_english_switch = false;
    cfg.show_status_bar = true;
    cfg.beep_on_empty_4code = false;
    cfg.beep_on_dup_4code = true;
    cfg.clear_on_empty_4code = true;
    cfg.enable_key_sound = true;
    cfg.up_down_page_turn = true;
    cfg.page_size = 7;

    ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd.IsValid());

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), settings_detail::kIdBtnBrowsePrimaryDict)));

    QIQI_TEST_CHECK(!read_back.enable_jidian_extra);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.push_commit_5th);
    QIQI_TEST_CHECK(!read_back.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!read_back.show_code_hint);
    QIQI_TEST_CHECK(read_back.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!read_back.shift_toggle_mode);
    QIQI_TEST_CHECK(!read_back.commit_on_english_switch);
    QIQI_TEST_CHECK(read_back.show_status_bar);
    QIQI_TEST_CHECK(!read_back.beep_on_empty_4code);
    QIQI_TEST_CHECK(read_back.beep_on_dup_4code);
    QIQI_TEST_CHECK(read_back.clear_on_empty_4code);
    QIQI_TEST_CHECK(read_back.enable_key_sound);
    QIQI_TEST_CHECK(read_back.up_down_page_turn);
    QIQI_TEST_CHECK(read_back.page_size == 7);

    // Also test BuiltinOnly and Jidian radio states + UiLanguage combobox + Cancel / WM_CLOSE
    cfg.primary_dict = PrimaryDict::BuiltinOnly;
    cfg.ui_language = UiLanguage::EnUs;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::EnUs);

    cfg.primary_dict = PrimaryDict::Jidian;
    cfg.ui_language = UiLanguage::ZhCn;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    // Simulate clicking Haifeng radio (102) -> disables Extra checkbox; clicking Jidian radio (101) -> enables it
    ::CheckRadioButton(hwnd.Get(), 101, 103, 102);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ::CheckRadioButton(hwnd.Get(), 101, 103, 101);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn);

    cfg.ui_language = UiLanguage::Auto;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::Auto);

    // Test Custom primary dictionary selection and validation in Settings window
    {
        const auto temp_dir = std::filesystem::temp_directory_path() /
            (L"qiqi_settings_custom_dict_" + std::to_wstring(::GetCurrentProcessId()));
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
        std::filesystem::create_directories(temp_dir, ec);
        const auto cleanup_dir = gsl::finally([&temp_dir]() noexcept {
            WubiMappedDict::ClearProcessCache();
            std::error_code rm_ec;
            std::filesystem::remove_all(temp_dir, rm_ec);
        });
        const auto valid_dict = temp_dir / L"custom_valid.txt";
        const auto invalid_dict = temp_dir / L"custom_invalid.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(valid_dict, codec::AsByteSpan(std::string_view("aaaa 自定义设置词\n"))));
        QIQI_TEST_CHECK(WriteFileAtomically(invalid_dict, codec::AsByteSpan(std::string_view("# empty\n"))));

        cfg.primary_dict = PrimaryDict::Haifeng;
        cfg.ui_language = UiLanguage::ZhCn;
        WriteSettingsToWindowControls(hwnd.Get(), cfg);

        // Selecting an invalid file shows InvalidPrimaryDict message and retains Haifeng
        WithDismissedMessage(GetUiStrings(UiLanguage::ZhCn).dlg_invalid_primary_dict, [&]() {
            QIQI_TEST_CHECK(!SelectCustomPrimaryDictionaryInSettings(hwnd.Get(), invalid_dict));
        });
        ReadSettingsFromWindowControls(hwnd.Get(), read_back);
        QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
        QIQI_TEST_CHECK(read_back.primary_dict_path.empty());

        // Selecting a valid file switches to PrimaryDict::Custom, updates path, and disables ExtraDict checkbox
        QIQI_TEST_CHECK(SelectCustomPrimaryDictionaryInSettings(hwnd.Get(), valid_dict));
        QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), settings_detail::kIdBtnBrowsePrimaryDict)));
        std::array<wchar_t, 256> custom_title{};
        ::GetWindowTextW(hwnd.Get(), custom_title.data(), gsl::narrow_cast<int>(custom_title.size()));
        QIQI_TEST_CHECK(std::wstring(custom_title.data()) ==
                        std::wstring(GetUiStrings(UiLanguage::ZhCn).dlg_title) + L" *");
        ReadSettingsFromWindowControls(hwnd.Get(), read_back);
        QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Custom);
        QIQI_TEST_CHECK(read_back.primary_dict_path == valid_dict.wstring());
        QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
        const auto partial_dict = temp_dir / L"custom_partial.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(partial_dict,
            codec::AsByteSpan(std::string_view("bbbb 有效部分\n12345 67890\n"))));
        WithDictionaryConfirmation(IDCANCEL, [&] {
            QIQI_TEST_CHECK(!SelectCustomPrimaryDictionaryInSettings(hwnd.Get(), partial_dict));
        });
        ReadSettingsFromWindowControls(hwnd.Get(), read_back);
        QIQI_TEST_CHECK(read_back.primary_dict_path == valid_dict.wstring());
        WithDictionaryConfirmation(IDOK, [&] {
            QIQI_TEST_CHECK(SelectCustomPrimaryDictionaryInSettings(hwnd.Get(), partial_dict));
        });
        ReadSettingsFromWindowControls(hwnd.Get(), read_back);
        QIQI_TEST_CHECK(read_back.primary_dict_path == partial_dict.wstring());
        WithDictionaryConfirmation(IDCANCEL, [&] {
            ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        });
        const auto* context = reinterpret_cast<const settings_detail::DialogContext*>(
            ::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
        QIQI_TEST_CHECK(context && !context->accepted && !context->done);
        {
            const std::wstring subkey = L"Software\\QiQiIME_PartialDictSaveTest_" +
                std::to_wstring(::GetCurrentProcessId());
            const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
            SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
            const auto restore_registry = gsl::finally([&]() noexcept {
                SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
                const LSTATUS result = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
                QIQI_TEST_CHECK(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
            });
            QiQiConfig stored;
            stored.ui_language = UiLanguage::ZhCn;
            QIQI_TEST_CHECK(SaveConfig(stored));
            const auto sequence = QueryRegistryConfigSequence();
            const ScopedWindow save_window(CreateSettingsWindow(
                ::GetModuleHandleW(nullptr), nullptr, &stored, true));
            QiQiConfig edit = stored;
            edit.primary_dict = PrimaryDict::Custom;
            edit.primary_dict_path = partial_dict.wstring();
            WriteSettingsToWindowControls(save_window.Get(), edit);
            WithDictionaryConfirmation(IDCANCEL, [&] {
                ::SendMessageW(save_window.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            });
            QIQI_TEST_CHECK(stored.primary_dict == PrimaryDict::Jidian &&
                QueryRegistryConfigSequence() == sequence);
            WithDictionaryConfirmation(IDOK, [&] {
                ::SendMessageW(save_window.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            });
            QIQI_TEST_CHECK(stored.primary_dict == PrimaryDict::Custom &&
                stored.primary_dict_path == partial_dict.wstring());
            QIQI_TEST_CHECK(LoadConfig().primary_dict_path == partial_dict.wstring());
        }
        cfg.primary_dict = PrimaryDict::Jidian;
        cfg.primary_dict_path.clear();
        WriteSettingsToWindowControls(hwnd.Get(), cfg);
        QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), settings_detail::kIdBtnBrowsePrimaryDict)));
        QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), settings_detail::kIdBtnDictManager)));
    }

    // Test WM_CTLCOLORSTATIC brush/text styling on a static control (kIdLblPageSize = 122)
    {
        const HWND lbl = ::GetDlgItem(hwnd.Get(), 122);
        QIQI_TEST_CHECK(lbl != nullptr);
        const ScopedWindowDC lbl_dc(lbl);
        QIQI_TEST_CHECK(::SendMessageW(
                            hwnd.Get(),
                            WM_CTLCOLORSTATIC,
                            reinterpret_cast<WPARAM>(lbl_dc.Get()),
                            reinterpret_cast<LPARAM>(lbl)) != 0);
        const COLORREF tc = ::GetTextColor(lbl_dc.Get());
        QIQI_TEST_CHECK(tc == RGB(28, 32, 40) || tc == kDarkTextColor);
    }

    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    ::SendMessageW(hwnd.Get(), WM_CLOSE, 0, 0);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    std::array<wchar_t, 128> title_buf{};
    {
        // Verify English settings dialog title when created with UiLanguage::EnUs
        cfg.ui_language = UiLanguage::EnUs;
        const ScopedWindow hwnd_en(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_en.IsValid());
        ::GetWindowTextW(hwnd_en.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi Wubi IME - Settings");

        // Verify Chinese settings dialog title when created with UiLanguage::ZhCn
        cfg.ui_language = UiLanguage::ZhCn;
        const ScopedWindow hwnd_zh(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_zh.IsValid());
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi 五笔输入法 - 属性设置");

        // Exercise Direct2D Fluent UI card and toggle-switch painting in both Light and Dark themes
        for (const bool dark : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(hwnd_zh.Get(), WM_THEMECHANGED, 0, 0);
            ::ShowWindow(hwnd_zh.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_zh.Get(), nullptr, TRUE);
            ::UpdateWindow(hwnd_zh.Get());
        }
        SetThemeDarkOverrideForTest(original_theme);

        // Verify live UI language switching via kIdComboUiLang (111) CBN_SELCHANGE
        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 2, 0); // EnUs
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi Wubi IME - Settings *");

        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 1, 0); // ZhCn
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi 五笔输入法 - 属性设置");

        // Test clicking the "About..." button (id=115) and "Shortcuts..." button (id=130) inside SettingsDialog
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = FindThreadWindowByClass(kInfoWndClass);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_CLOSE, 0, 0);
            }
        });
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);

        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND help_dlg = FindThreadWindowByClass(kInfoWndClass);
            if (help_dlg) {
                ::SendMessageW(help_dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(130, BN_CLICKED), 0);

        const ScopedWindow hwnd_null_cfg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, nullptr));
        QIQI_TEST_CHECK(hwnd_null_cfg.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(hwnd_null_cfg.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = FindThreadWindowByClass(kInfoWndClass);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd_null_cfg.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);
    }

    hwnd.Reset();

    // Test modal ShowSettingsDialog with auto-closing timer (IDOK -> returns true, IDCANCEL -> returns false)
    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    // Verify single-instance Settings behavior:
    // 1) When no Settings window exists, ActivateExistingSettingsWindow returns false.
    // 2) When a Settings window is open (including minimized or with an owned popup),
    //    ActivateExistingSettingsWindow restores/activates it and ShowSettingsDialog returns false
    //    immediately without creating a second Settings window.
    // 3) When the single-instance mutex is already held, ShowSettingsDialog returns false without
    //    opening a duplicate dialog.
    QIQI_TEST_CHECK(!ActivateExistingSettingsWindow());
    {
        const ScopedWindow existing(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(existing.IsValid());
        ::ShowWindow(existing.Get(), SW_MINIMIZE);
        QIQI_TEST_CHECK(::IsIconic(existing.Get()) != FALSE);
        QIQI_TEST_CHECK(ActivateExistingSettingsWindow());
        QIQI_TEST_CHECK(::IsIconic(existing.Get()) == FALSE);

        const ScopedWindow owned_about(
            CreateAboutWindow(::GetModuleHandleW(nullptr), existing.Get(), UiLanguage::ZhCn));
        QIQI_TEST_CHECK(owned_about.IsValid());
        ::ShowWindow(owned_about.Get(), SW_SHOWNOACTIVATE);
        QIQI_TEST_CHECK(ActivateExistingSettingsWindow());
        QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
    }
    {
        const ScopedKernelHandle held_mutex(
            ::CreateMutexW(nullptr, TRUE, kSettingsSingleInstanceMutexName));
        QIQI_TEST_CHECK(held_mutex.IsValid());
        QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
    }

    {
        const ScopedKernelHandle conflicting_event(
            ::CreateEventW(nullptr, TRUE, FALSE, kSettingsSingleInstanceMutexName));
        QIQI_TEST_CHECK(conflicting_event.IsValid());
        QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
        QIQI_TEST_CHECK(!FindThreadWindowByClass(kSettingsWndClass));
    }

    // Verify About dialog creation, painting, title localization, child controls, icon, and modal ShowAboutDialog
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_app_name).find(L"奇奇五笔") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_app_name) == L"QiQi Wubi IME");
    QIQI_TEST_CHECK(std::wstring(kGitHubRepoUrl) == L"https://github.com/cloudfyy/qiqiime");
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"GPL-3.0-or-later") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"GPL-3.0-or-later") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"Apache License 2.0") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"BSD 3-Clause License") != std::wstring::npos);

    {
        const ScopedWindow about_zh(CreateAboutWindow(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
        QIQI_TEST_CHECK(about_zh.IsValid());
        const LRESULT small_icon = ::SendMessageW(about_zh.Get(), WM_GETICON, ICON_SMALL, 0);
        const LRESULT big_icon = ::SendMessageW(about_zh.Get(), WM_GETICON, ICON_BIG, 0);
        QIQI_TEST_CHECK(small_icon != 0 && big_icon != 0 && small_icon != big_icon);
        ::GetWindowTextW(about_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"关于 奇奇五笔");
        std::array<wchar_t, 64> btn_buf{};
        ::GetDlgItemTextW(about_zh.Get(), 201, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"访问 GitHub");
        ::GetDlgItemTextW(about_zh.Get(), IDOK, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"确定");
        ::ShowWindow(about_zh.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_zh.Get(), nullptr, TRUE);
        ::UpdateWindow(about_zh.Get());
        ::SendMessageW(about_zh.Get(), WM_CLOSE, 0, 0);

        const ScopedWindow about_en(CreateAboutWindow(::GetModuleHandleW(nullptr), about_zh.Get(), UiLanguage::EnUs));
        QIQI_TEST_CHECK(about_en.IsValid());
        ::GetWindowTextW(about_en.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"About QiQi Wubi IME");
        ::GetDlgItemTextW(about_en.Get(), 201, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"Visit GitHub");
        ::GetDlgItemTextW(about_en.Get(), IDOK, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"OK");
        ::ShowWindow(about_en.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_en.Get(), nullptr, TRUE);
        ::UpdateWindow(about_en.Get());
        ::SendMessageW(about_en.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    }

    ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kInfoWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));

    // Verify Help / Shortcuts dialog creation, painting, live language switch, and modal ShowHelpDialog
    {
        const ScopedWindow help_zh(CreateHelpWindow(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
        QIQI_TEST_CHECK(help_zh.IsValid());
        ::GetWindowTextW(help_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == kZhCnStrings.help_title);
        QIQI_TEST_CHECK(::GetDlgItem(help_zh.Get(), 201) == nullptr);
        std::array<wchar_t, 64> btn_buf{};
        ::GetDlgItemTextW(help_zh.Get(), IDOK, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"确定");
        ::ShowWindow(help_zh.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(help_zh.Get(), nullptr, TRUE);
        ::UpdateWindow(help_zh.Get());

        ::SendMessageW(help_zh.Get(), kSettingsLanguageChangedMessage, static_cast<WPARAM>(UiLanguage::EnUs), 0);
        ::GetWindowTextW(help_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == kEnUsStrings.help_title);

        const ScopedWindow help_en(CreateHelpWindow(::GetModuleHandleW(nullptr), help_zh.Get(), UiLanguage::EnUs));
        QIQI_TEST_CHECK(help_en.IsValid());
        ::GetWindowTextW(help_en.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == kEnUsStrings.help_title);
    }
    ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kInfoWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowHelpDialog(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
}

void TestSettingsTextAlignment() {
    using namespace settings_detail;
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    constexpr std::array aligned_rows{
        std::pair{kIdLblPageSize, kIdComboPageSize},
        std::pair{kIdLblUiLang, kIdComboUiLang},
        std::pair{kIdLblUnderlineStyle, kIdComboUnderlineStyle},
        std::pair{kIdLblEnglishSwitch, kIdComboEnglishSwitch}};
    for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 3840, 2160}, 96});
        QiQiConfig config;
        config.ui_language = language;
        const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
        QIQI_TEST_CHECK(dialog);
        const auto bounds = [&](int id) {
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), id), &rect));
            ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&rect), 2);
            return rect;
        };
        for (const UINT dpi : {96u, 120u, 144u, 192u}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 3840, 2160}, dpi});
            const RECT suggested{0, 0, ScalePxByDpi(820, dpi), ScalePxByDpi(494, dpi)};
            ::SendMessageW(dialog.Get(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                           reinterpret_cast<LPARAM>(&suggested));
            for (const auto& [label_id, toggle_id] : aligned_rows) {
                auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
                const SettingsPage page = label_id == kIdLblUiLang ? SettingsPage::Display :
                    label_id == kIdLblEnglishSwitch ? SettingsPage::Input : SettingsPage::Candidates;
                SelectSettingsPage(dialog.Get(), *context, page);
                RECT label{}, toggle{};
                QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), label_id), &label));
                QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), toggle_id), &toggle));
                if (label.right <= toggle.left) {
                    QIQI_TEST_CHECK(label.top == toggle.top);
                } else {
                    QIQI_TEST_CHECK(label.left == toggle.left);
                    QIQI_TEST_CHECK(label.bottom <= toggle.top);
                }
            }
            auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
            const int control_right = context->page_bounds.right - context->d2d.ScaleDipPx(16);
            SelectSettingsPage(dialog.Get(), *context, SettingsPage::Input);
            const RECT help = bounds(kIdBtnHelp);
            const RECT about = bounds(kIdBtnAbout);
            const RECT restore = bounds(kIdBtnRestoreDefaults);
            QIQI_TEST_CHECK(help.top == about.top && about.top == restore.top);
            QIQI_TEST_CHECK(about.right <= help.left && help.right <= restore.left);
            QIQI_TEST_CHECK(restore.top == bounds(IDOK).top && restore.top == bounds(IDCANCEL).top);
            QIQI_TEST_CHECK(restore.right < bounds(IDOK).left);
            QIQI_TEST_CHECK(help.top >= context->page_bounds.bottom);
            QIQI_TEST_CHECK(bounds(kIdComboEnglishSwitch).right == control_right);
            QIQI_TEST_CHECK(restore.right - restore.left < (control_right - context->page_bounds.left) / 2);
            SelectSettingsPage(dialog.Get(), *context, SettingsPage::Dictionary);
            const RECT browse = bounds(kIdBtnBrowsePrimaryDict);
            const RECT manager = bounds(kIdBtnDictManager);
            QIQI_TEST_CHECK(bounds(kIdChkExtraDict).left > bounds(kIdRadioJidian).left);
            QIQI_TEST_CHECK(bounds(kIdChkExtraDict).top >= bounds(kIdRadioJidian).bottom);
            QIQI_TEST_CHECK(browse.top == manager.top && browse.right <= manager.left);
            QIQI_TEST_CHECK(bounds(kIdRadioCustom).top == browse.top);
            QIQI_TEST_CHECK(bounds(kIdRadioCustom).right <= browse.left);
            QIQI_TEST_CHECK(manager.right == control_right);
            SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
            const RECT size = bounds(kIdComboPageSize);
            const RECT layout = bounds(kIdComboCandidateLayout);
            QIQI_TEST_CHECK(size.top == layout.top && size.right < layout.left);
            QIQI_TEST_CHECK(layout.right == control_right);
            QIQI_TEST_CHECK(bounds(kIdComboUnderlineStyle).right == control_right);
            SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
            QIQI_TEST_CHECK(bounds(kIdComboUiTheme).right == control_right);
            QIQI_TEST_CHECK(bounds(kIdComboUiTextScale).right == control_right);
            QIQI_TEST_CHECK(bounds(kIdComboUiLang).right == control_right);
            QIQI_TEST_CHECK(bounds(kIdCandidatePreview).bottom - bounds(kIdCandidatePreview).top < ScalePxByDpi(150, dpi));
        }
    }
}

void TestSettingsWindowMonitorChanges() {
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1920, 1080}, 96});
    QiQiConfig config{};
    config.page_size = 7;
    config.auto_commit_4code = true;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog.IsValid());

    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboPageSize, CB_SETCURSEL, 8, 0);
    ::CheckDlgButton(dialog.Get(), settings_detail::kIdChkAutoCommit4, BST_UNCHECKED);
    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboEnglishSwitch, CB_SETCURSEL, 1, 0);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-2560, -1440, 0, 0}, 192});
    const RECT suggested_rect{-2100, -1250, -400, -200};
    ::SendMessageW(
        dialog.Get(),
        WM_DPICHANGED,
        MAKEWPARAM(192, 192),
        reinterpret_cast<LPARAM>(&suggested_rect));

    const HWND page_size_combo = ::GetDlgItem(dialog.Get(), settings_detail::kIdComboPageSize);
    QIQI_TEST_CHECK(page_size_combo != nullptr);
    POINT combo_origin{};
    ::ClientToScreen(dialog.Get(), &combo_origin);
    RECT combo_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(page_size_combo, &combo_rect));
    RECT client{};
    ::GetClientRect(dialog.Get(), &client);
    QIQI_TEST_CHECK(combo_rect.left >= combo_origin.x &&
                    combo_rect.right <= combo_origin.x + client.right);

    const HFONT combo_font = reinterpret_cast<HFONT>(::SendMessageW(page_size_combo, WM_GETFONT, 0, 0));
    LOGFONTW font_info{};
    QIQI_TEST_CHECK(combo_font != nullptr);
    QIQI_TEST_CHECK(::GetObjectW(combo_font, sizeof(font_info), &font_info) == sizeof(font_info));
    QIQI_TEST_CHECK(font_info.lfHeight == -ScalePxByDpi(15, 192));

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(dialog.Get(), read_back);
    QIQI_TEST_CHECK(read_back.page_size == 9);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.commit_on_english_switch);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1900, -1200, 0, 0}, 192});
    ::SendMessageW(dialog.Get(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    RECT dialog_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(dialog.Get(), &dialog_rect));
    QIQI_TEST_CHECK(dialog_rect.left == -1900 && dialog_rect.top == -1200);
    QIQI_TEST_CHECK(dialog_rect.right <= 0 && dialog_rect.bottom <= 0);
}

void TestSettingsLabelViewportClipping() {
    using namespace settings_detail;
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.easy_reading = true;
    config.ui_text_scale = UiTextScale::Percent200;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
    const HWND label = ::GetDlgItem(dialog.Get(), kIdLblUiTheme);
    RECT client{};
    QIQI_TEST_CHECK(::GetClientRect(label, &client));
    const ScopedWindowDC screen(dialog.Get());
    const ScopedMemoryDC painted(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(
        ::CreateCompatibleBitmap(screen.Get(), client.right, client.bottom));
    const ScopedGdiHandle<HBRUSH> untouched(::CreateSolidBrush(RGB(255, 0, 255)));
    QIQI_TEST_CHECK(screen && painted && bitmap && untouched);
    const ScopedSelectObject selected_bitmap(painted.Get(), bitmap.Get());
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_STATIC;
    item.CtlID = kIdLblUiTheme;
    item.hwndItem = label;
    item.hDC = painted.Get();
    item.rcItem = client;
    for (const bool fully_clipped : {true, false}) {
        const int hidden_height = fully_clipped ? client.bottom : client.bottom / 2;
        QIQI_TEST_CHECK(::SetWindowPos(label, nullptr,
            context->page_bounds.left, context->page_bounds.top - hidden_height, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW));
        ::FillRect(painted.Get(), &client, untouched.Get());
        ::SendMessageW(dialog.Get(), WM_DRAWITEM, kIdLblUiTheme, reinterpret_cast<LPARAM>(&item));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), client.right - 2, 1) == RGB(255, 0, 255));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), client.right - 2, client.bottom - 2) ==
            (fully_clipped ? RGB(255, 0, 255) : context->d2d.appearance.palette.surface_bg));
    }
}

void TestSettingsDisplayPreview() {
    using namespace settings_detail;
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.page_size = 8;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    const auto* context = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    const HWND tabs = ::GetDlgItem(dialog.Get(), kIdSettingsTabs);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Display), 0);
    RECT tab_bounds{};
    QIQI_TEST_CHECK(::GetClientRect(tabs, &tab_bounds));
    const ScopedWindowDC tab_dc(tabs);
    const ScopedMemoryDC painted(::CreateCompatibleDC(tab_dc.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(tab_dc.Get(), tab_bounds.right, tab_bounds.bottom));
    QIQI_TEST_CHECK(painted && bitmap);
    const ScopedSelectObject selected_bitmap(painted.Get(), bitmap.Get());
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTheme, CB_GETCOUNT, 0, 0) == 4);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTextScale, CB_GETCOUNT, 0, 0) == 4);
    const auto select = [&](int id, int value) {
        ::SendDlgItemMessageW(dialog.Get(), id, CB_SETCURSEL, static_cast<WPARAM>(value), 0);
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), 0);
    };
    QiQiConfig edited;
    for (const UiTheme theme : {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
        select(kIdComboUiTheme, static_cast<int>(theme));
        ReadSettingsFromWindowControls(dialog.Get(), edited);
        QIQI_TEST_CHECK(edited.ui_theme == theme);
        QIQI_TEST_CHECK(context->d2d.display_preferences.theme == theme);
        QIQI_TEST_CHECK(config.ui_theme == UiTheme::System);
        QIQI_TEST_CHECK(context->active_page == SettingsPage::Display);
        ::SendMessageW(tabs, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(painted.Get()), PRF_CLIENT);
        RECT unselected{}, selected{};
        QIQI_TEST_CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&unselected)));
        QIQI_TEST_CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, static_cast<WPARAM>(SettingsPage::Display),
                                     reinterpret_cast<LPARAM>(&selected)));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), unselected.left + 4, unselected.top + 4) ==
                        context->d2d.appearance.palette.window_bg);
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), selected.left + 4, selected.top + 4) ==
                        context->d2d.appearance.palette.selection_bg);
    }
    const HWND comfort = ::GetDlgItem(dialog.Get(), kIdChkEasyReading);
    std::array<wchar_t, 128> comfort_label{};
    ::GetWindowTextW(comfort, comfort_label.data(), gsl::narrow_cast<int>(comfort_label.size()));
    QIQI_TEST_CHECK(std::wstring(comfort_label.data()) == L"Comfort mode");
    ::SendMessageW(comfort, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    ::SendMessageW(comfort, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkEasyReading) == BST_UNCHECKED);
    RECT comfort_client{};
    QIQI_TEST_CHECK(::GetClientRect(comfort, &comfort_client));
    const RECT switch_bounds = FluentToggleBounds(comfort_client, context->d2d);
    const LPARAM switch_point = MAKELPARAM(
        (switch_bounds.left + switch_bounds.right) / 2, (switch_bounds.top + switch_bounds.bottom) / 2);
    ::SendMessageW(comfort, WM_LBUTTONDOWN, MK_LBUTTON, switch_point);
    ::SendMessageW(comfort, WM_LBUTTONUP, 0, switch_point);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkEasyReading) == BST_CHECKED);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdChkEasyReading, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent150);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent100));
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent150);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTextScale, CB_SETCURSEL,
        static_cast<WPARAM>(UiTextScale::Percent125), 0) == CB_ERR);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::CheckDlgButton(dialog.Get(), kIdChkEasyReading, BST_UNCHECKED);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdChkEasyReading, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(!edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent200);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent100));
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(!edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent100);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::SendDlgItemMessageW(dialog.Get(), kIdComboPageSize, CB_SETCURSEL, 5, 0);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Candidates), 0);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Display), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.page_size == 6 && edited.ui_text_scale == UiTextScale::Percent200);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdBtnRestoreDefaults, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    const QiQiConfig defaults;
    QIQI_TEST_CHECK(GetDisplayPreferences(edited) == GetDisplayPreferences(defaults));
    QIQI_TEST_CHECK(context->d2d.display_preferences == GetDisplayPreferences(defaults));
    QIQI_TEST_CHECK(edited.page_size == defaults.page_size && edited.ui_language == defaults.ui_language);
    QIQI_TEST_CHECK(edited.primary_dict == defaults.primary_dict);
    select(kIdComboUiTheme, static_cast<int>(UiTheme::Dark));
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    QIQI_TEST_CHECK(GetDisplayPreferences(config) == DisplayPreferences{});
    QIQI_TEST_CHECK(config.page_size == 8);

    const ScopedWindow accepted(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(accepted);
    ::SendDlgItemMessageW(accepted.Get(), kIdComboUiTheme, CB_SETCURSEL,
                         static_cast<WPARAM>(UiTheme::Dark), 0);
    ::SendDlgItemMessageW(accepted.Get(), kIdComboUiTextScale, CB_SETCURSEL,
                         static_cast<WPARAM>(UiTextScale::Percent125), 0);
    ::CheckDlgButton(accepted.Get(), kIdChkEasyReading, BST_CHECKED);
    QIQI_TEST_CHECK(GetDisplayPreferences(config) == DisplayPreferences{});
    ::SendMessageW(accepted.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    QIQI_TEST_CHECK(config.ui_theme == UiTheme::Dark &&
                    config.ui_text_scale == UiTextScale::Percent125 && config.easy_reading);
}

void TestSettingsAccessibleLayout() {
    using namespace settings_detail;
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    for (const UINT dpi : {96u, 144u, 192u}) {
        for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 800, 600}, dpi});
            QiQiConfig config;
            config.ui_language = language;
            config.ui_text_scale = UiTextScale::Percent200;
            config.easy_reading = true;
            const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
            QIQI_TEST_CHECK(dialog);
            auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
            QIQI_TEST_CHECK(context && context->content_height > context->viewport_height);
            RECT client{};
            ::GetClientRect(dialog.Get(), &client);
            const auto bounds = [&](int id) {
                RECT rect{};
                QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), id), &rect));
                ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&rect), 2);
                return rect;
            };
            const RECT ok = bounds(IDOK);
            const RECT cancel = bounds(IDCANCEL);
            QIQI_TEST_CHECK(ok.left >= 0 && cancel.right <= client.right);
            QIQI_TEST_CHECK(ok.top >= context->page_bounds.bottom && ok.bottom <= client.bottom);
            QIQI_TEST_CHECK(cancel.top >= context->page_bounds.bottom && cancel.bottom <= client.bottom);
            QIQI_TEST_CHECK(ok.right <= cancel.left);
            QIQI_TEST_CHECK(ok.bottom - ok.top >= ScalePxByDpi(44, dpi));
            const int before_footer_focus = context->scroll_offset;
            ::SendDlgItemMessageW(dialog.Get(), kIdBtnRestoreDefaults, WM_SETFOCUS, 0, 0);
            QIQI_TEST_CHECK(context->scroll_offset == before_footer_focus);
            QIQI_TEST_CHECK(bounds(kIdBtnRestoreDefaults).top >= context->page_bounds.bottom);
            ::SendDlgItemMessageW(dialog.Get(), kIdComboEnglishSwitch, WM_SETFOCUS, 0, 0);
            const RECT focused = bounds(kIdComboEnglishSwitch);
            QIQI_TEST_CHECK(context->scroll_offset > 0);
            QIQI_TEST_CHECK(focused.top < context->page_bounds.bottom && focused.bottom > context->page_bounds.top);
            if (focused.bottom - focused.top <= context->viewport_height - ScalePxByDpi(8, dpi)) {
                QIQI_TEST_CHECK(focused.top >= context->page_bounds.top && focused.bottom <= context->page_bounds.bottom);
            }
            QIQI_TEST_CHECK(bounds(IDOK).top == ok.top);
            const int input_scroll = context->scroll_offset;
            ::SendDlgItemMessageW(dialog.Get(), kIdSettingsTabs, TCM_SETCURSEL,
                                 static_cast<WPARAM>(SettingsPage::Display), 0);
            QIQI_TEST_CHECK(context->active_page == SettingsPage::Display);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdChkAutoCommit4), GWL_STYLE) & WS_VISIBLE) == 0);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdChkEasyReading), GWL_STYLE) & WS_VISIBLE) != 0);
            QIQI_TEST_CHECK(bounds(kIdChkEasyReading).bottom - bounds(kIdChkEasyReading).top >= ScalePxByDpi(44, dpi));
            QIQI_TEST_CHECK(bounds(kIdComboUiTheme).bottom <= bounds(kIdChkEasyReading).top);
            QIQI_TEST_CHECK(bounds(kIdChkEasyReading).bottom <= bounds(kIdLblUiTextScale).top);
            QIQI_TEST_CHECK(bounds(kIdComboUiTextScale).bottom <= bounds(kIdLblUiLang).top);
            QIQI_TEST_CHECK(bounds(kIdSettingsTabs).bottom <= context->page_bounds.top);
            ::SendMessageW(dialog.Get(), kSettingsEnsureFocusVisible,
                           reinterpret_cast<WPARAM>(::GetDlgItem(dialog.Get(), kIdChkStatusBar)), 0);
            QIQI_TEST_CHECK(bounds(kIdChkStatusBar).bottom <= context->page_bounds.bottom);
            QIQI_TEST_CHECK(bounds(IDOK).top == ok.top);
            ::SendDlgItemMessageW(dialog.Get(), kIdSettingsTabs, TCM_SETCURSEL,
                                 static_cast<WPARAM>(SettingsPage::Input), 0);
            QIQI_TEST_CHECK(context->scroll_offset == input_scroll);
            for (int page = 0; page < static_cast<int>(SettingsPage::Count); ++page) {
                SelectSettingsPage(dialog.Get(), *context, static_cast<SettingsPage>(page));
                for (const int id : SettingsPageControls(static_cast<SettingsPage>(page))) {
                    if (id == kIdDisplayExplanation) continue;
                    QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), id), GWL_STYLE) & WS_VISIBLE) != 0);
                }
                for (const int id : kSettingsFooterControls) {
                    QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), id), GWL_STYLE) & WS_VISIBLE) != 0);
                    QIQI_TEST_CHECK(bounds(id).top >= context->page_bounds.bottom);
                    QIQI_TEST_CHECK(bounds(id).bottom <= client.bottom);
                }
            }
        }
    }
}

} // namespace

void RunSettingsDialogTests() {
    TestDictionaryConfirmationText();
    TestSettingsDictionaryDependencies();
    TestSettingsDialogControls();
    TestSettingsTextAlignment();
    TestSettingsWindowMonitorChanges();
    TestSettingsLabelViewportClipping();
    TestSettingsDisplayPreview();
    TestSettingsAccessibleLayout();
}

} // namespace qiqi::test
