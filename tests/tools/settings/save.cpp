#include "common/test_support.h"
#include "common/i18n.h"
#include "common/win32_ui_util.h"
#include "tools/settings/internal.h"

#include <array>
#include <string>

namespace qiqi::test {
namespace {

using namespace settings_detail;

DialogContext& GetContext(HWND hwnd) {
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    QIQI_TEST_CHECK(context != nullptr);
    return *context;
}

void TestSaveRetryAndCancel() {
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsSaveTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
        const LSTATUS result = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    });

    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const bool cancel : {false, true}) {
            QiQiConfig cfg;
            cfg.user_dict_path = L"preserved_words.txt";
            cfg.enable_z_temp_english = false;
            cfg.ui_language = language == UiLanguage::ZhCn ? UiLanguage::EnUs : UiLanguage::ZhCn;
            SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
            QIQI_TEST_CHECK(SaveConfig(cfg));
            const uint64_t before = QueryRegistryConfigSequence();
            const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg, true));
            QIQI_TEST_CHECK(hwnd);
            ::SendDlgItemMessageW(hwnd.Get(), kIdRadioHaifeng, BM_CLICK, 0, 0);
            ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_SETCURSEL, 8, 0);
            ::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_SETCURSEL, 1, 0);
            ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_SETCURSEL,
                                 language == UiLanguage::ZhCn ? 1 : 2, 0);
            ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(kIdComboUiLang, CBN_SELCHANGE), 0);
            ::CheckDlgButton(hwnd.Get(), kIdChkKeySound, BST_CHECKED);
            SetConfigRegistrySubKeyOverrideForTest(L"");
            WithDismissedMessage(GetUiStrings(language).dlg_save_failed, [&]() {
                ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            });
            QIQI_TEST_CHECK(::IsWindow(hwnd.Get()) && !GetContext(hwnd.Get()).done);
            QIQI_TEST_CHECK(!GetContext(hwnd.Get()).accepted);
            QIQI_TEST_CHECK(cfg.primary_dict == PrimaryDict::Jidian && cfg.page_size == 5);
            QIQI_TEST_CHECK(!cfg.enable_key_sound && cfg.ui_language != language);
            QiQiConfig edits = cfg;
            ReadSettingsFromWindowControls(hwnd.Get(), edits);
            QIQI_TEST_CHECK(edits.primary_dict == PrimaryDict::Haifeng && edits.page_size == 9);
            QIQI_TEST_CHECK(edits.enable_key_sound && edits.ui_language == language);
            QIQI_TEST_CHECK(!edits.commit_on_english_switch && cfg.commit_on_english_switch);
            SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
            QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before);

            ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(cancel ? IDCANCEL : IDOK, BN_CLICKED), 0);
            QIQI_TEST_CHECK(GetContext(hwnd.Get()).done);
            QIQI_TEST_CHECK(GetContext(hwnd.Get()).accepted == !cancel);
            QiQiConfig stored;
            QIQI_TEST_CHECK(LoadConfigFromRegistry(stored));
            QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before + (cancel ? 0 : 1));
            QIQI_TEST_CHECK(cfg.page_size == (cancel ? 5u : 9u));
            QIQI_TEST_CHECK(stored.page_size == cfg.page_size);
            QIQI_TEST_CHECK(stored.primary_dict == cfg.primary_dict);
            QIQI_TEST_CHECK(stored.ui_language == cfg.ui_language);
            QIQI_TEST_CHECK(stored.enable_key_sound == cfg.enable_key_sound);
            QIQI_TEST_CHECK(stored.commit_on_english_switch == cancel);
            QIQI_TEST_CHECK(stored.commit_on_english_switch == cfg.commit_on_english_switch);
            QIQI_TEST_CHECK(stored.user_dict_path == L"preserved_words.txt");
            QIQI_TEST_CHECK(!stored.enable_z_temp_english);
            if (!cancel) {
                WithDismissedMessage(GetUiStrings(language).dlg_notify_failed, [&]() {
                    ReportSettingsNotificationFailure(hwnd.Get(), language);
                });
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before + 1);
                QIQI_TEST_CHECK(LoadConfigFromRegistry(stored) && stored.page_size == 9);
            }
        }
    }

    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    {
        QiQiConfig custom_cfg;
        custom_cfg.primary_dict = PrimaryDict::Jidian;
        custom_cfg.ui_language = UiLanguage::ZhCn;
        QIQI_TEST_CHECK(SaveConfig(custom_cfg));
        const uint64_t seq_before = QueryRegistryConfigSequence();
        const ScopedWindow custom_hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &custom_cfg, true));
        QIQI_TEST_CHECK(custom_hwnd);
        QiQiConfig invalid_edit = custom_cfg;
        invalid_edit.primary_dict = PrimaryDict::Custom;
        invalid_edit.primary_dict_path = L"C:\\NonExistentDir\\missing_primary.txt";
        WriteSettingsToWindowControls(custom_hwnd.Get(), invalid_edit);
        WithDismissedMessage(GetUiStrings(UiLanguage::ZhCn).dlg_invalid_primary_dict, [&]() {
            ::SendMessageW(custom_hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        });
        QIQI_TEST_CHECK(!GetContext(custom_hwnd.Get()).done && !GetContext(custom_hwnd.Get()).accepted);
        QIQI_TEST_CHECK(custom_cfg.primary_dict == PrimaryDict::Jidian);
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == seq_before);
    }

    SetConfigRegistrySubKeyOverrideForTest(L"");
    QiQiConfig cfg;
    const ScopedWindow edit_only(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(edit_only);
    ::SendDlgItemMessageW(edit_only.Get(), kIdComboPageSize, CB_SETCURSEL, 6, 0);
    ::SendMessageW(edit_only.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    QIQI_TEST_CHECK(GetContext(edit_only.Get()).accepted && cfg.page_size == 7);
    QIQI_TEST_CHECK(!CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, nullptr, true));
}

void TestExternalConfigSyncWhileSettingsOpen() {
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsExtSyncTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
        const LSTATUS result = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    });

    QiQiConfig cfg;
    cfg.primary_dict = PrimaryDict::Jidian;
    cfg.enable_jidian_extra = true;
    cfg.show_status_bar = false;
    cfg.page_size = 5;
    cfg.ui_language = UiLanguage::ZhCn;
    QIQI_TEST_CHECK(SaveConfig(cfg));

    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg, true));
    QIQI_TEST_CHECK(hwnd);

    // 1) Clean window: external change updates all controls and live UI language immediately
    QiQiConfig ext1 = cfg;
    ext1.primary_dict = PrimaryDict::Haifeng;
    ext1.show_status_bar = true;
    ext1.ui_language = UiLanguage::EnUs;
    ext1.underline_style = CompositionUnderlineStyle::Squiggle;
    ext1.bold_underline = true;
    ext1.commit_on_english_switch = false;
    QIQI_TEST_CHECK(SaveConfig(ext1));
    if (GetContext(hwnd.Get()).compartment_listener) {
        QIQI_TEST_CHECK(SUCCEEDED(
            GetContext(hwnd.Get()).compartment_listener->OnChange(kGuidCompartmentConfigVersion)));
    }
    ::SendMessageW(hwnd.Get(), kSettingsExternalConfigChangedMessage, 0, 0);

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(read_back.show_status_bar);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::EnUs);
    QIQI_TEST_CHECK(read_back.underline_style == CompositionUnderlineStyle::Squiggle);
    QIQI_TEST_CHECK(read_back.bold_underline);
    QIQI_TEST_CHECK(!read_back.commit_on_english_switch);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    std::array<wchar_t, 128> title_buf{};
    ::GetWindowTextW(hwnd.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
    QIQI_TEST_CHECK(std::wstring(title_buf.data()) == GetUiStrings(UiLanguage::EnUs).dlg_title);

    // 2) Dirty window: user edits page_size (to 8), underline_style (to Solid), and enable_key_sound (checked)
    //    in the dialog, while external quick-settings switches primary_dict back to Jidian and ui_language to ZhCn.
    //    Field-level merge preserves user's unsaved edits while adopting untouched external changes.
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_SETCURSEL, 7, 0); // page_size = 8
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboUnderlineStyle, CB_SETCURSEL,
                         static_cast<WPARAM>(CompositionUnderlineStyle::Solid), 0);
    ::CheckDlgButton(hwnd.Get(), kIdChkKeySound, BST_CHECKED);
    ::SendDlgItemMessageW(hwnd.Get(), kIdComboEnglishSwitch, CB_SETCURSEL, 0, 0);

    QiQiConfig ext2 = ext1;
    ext2.primary_dict = PrimaryDict::Jidian;
    ext2.ui_language = UiLanguage::ZhCn;
    QIQI_TEST_CHECK(SaveConfig(ext2));
    ::SendMessageW(hwnd.Get(), WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), 0);

    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(read_back.page_size == 8);
    QIQI_TEST_CHECK(read_back.underline_style == CompositionUnderlineStyle::Solid);
    QIQI_TEST_CHECK(read_back.enable_key_sound);
    QIQI_TEST_CHECK(read_back.commit_on_english_switch);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    ::GetWindowTextW(hwnd.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
    QIQI_TEST_CHECK(std::wstring(title_buf.data()) == GetUiStrings(UiLanguage::ZhCn).dlg_title);
    QIQI_TEST_CHECK(cfg.primary_dict == PrimaryDict::Jidian && cfg.page_size == 5 && !cfg.enable_key_sound);
}

void TestRestoreDefaults() {
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsDefaultsTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    const QiQiConfig defaults{};
    const auto check_restored = [&defaults](const QiQiConfig& actual, const QiQiConfig& original) {
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(actual) ==
                field.Value(field.settings_editable ? defaults : original));
        });
        QIQI_TEST_CHECK(actual.primary_dict_path.empty());
        QIQI_TEST_CHECK(actual.user_dict_path == original.user_dict_path);
    };
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const bool persist : {false, true}) {
            for (const bool accept : {false, true}) {
                QiQiConfig cfg;
                ForEachConfigField([&](const auto& field) {
                    QIQI_TEST_CHECK(field.Assign(cfg,
                        field.Value(defaults) == field.minimum ? field.maximum : field.minimum));
                });
                cfg.ui_language = language;
                cfg.primary_dict_path = L"preserved_custom_dictionary.txt";
                cfg.user_dict_path = L"preserved_user_dictionary.txt";
                const QiQiConfig original = cfg;
                QIQI_TEST_CHECK(SaveConfig(cfg));
                const auto before = QueryRegistryConfigSequence();
                const ScopedWindow window(CreateSettingsWindow(
                    ::GetModuleHandleW(nullptr), nullptr, &cfg, persist));
                QIQI_TEST_CHECK(window);
                std::array<wchar_t, 64> label{};
                ::GetDlgItemTextW(window.Get(), kIdBtnRestoreDefaults, label.data(),
                                 gsl::narrow_cast<int>(label.size()));
                QIQI_TEST_CHECK(std::wstring(label.data()) == GetUiStrings(language).dlg_btn_restore_defaults);
                {
                    const HWND button = ::GetDlgItem(window.Get(), kIdBtnRestoreDefaults);
                    QIQI_TEST_CHECK((::GetWindowLongPtrW(button, GWL_STYLE) & WS_TABSTOP) != 0);
                    const ScopedWindowDC dc(button);
                    const HFONT font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
                    const ScopedSelectObject select_font(dc.Get(), font);
                    SIZE text{};
                    const std::wstring caption(label.data());
                    QIQI_TEST_CHECK(::GetTextExtentPoint32W(
                        dc.Get(), caption.c_str(), gsl::narrow_cast<int>(caption.size()), &text));
                    RECT bounds{};
                    QIQI_TEST_CHECK(::GetClientRect(button, &bounds));
                    QIQI_TEST_CHECK(text.cx < bounds.right && text.cy < bounds.bottom);
                }
                ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
                ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
                const RECT suggested{100, 100, 1400, 900};
                ::SendMessageW(window.Get(), WM_DPICHANGED, MAKEWPARAM(144, 144),
                               reinterpret_cast<LPARAM>(&suggested));
                QIQI_TEST_CHECK(!GetContext(window.Get()).done);
                ForEachConfigField([&](const auto& field) {
                    QIQI_TEST_CHECK(field.Value(cfg) == field.Value(original));
                    QIQI_TEST_CHECK(field.Value(GetContext(window.Get()).baseline_config) == field.Value(original));
                });
                QIQI_TEST_CHECK(cfg.primary_dict_path == original.primary_dict_path);
                QIQI_TEST_CHECK(GetContext(window.Get()).baseline_config.primary_dict_path == original.primary_dict_path);
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before);
                QiQiConfig edited = cfg;
                ReadSettingsFromWindowControls(window.Get(), edited);
                check_restored(edited, original);
                QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(window.Get(), kIdChkExtraDict)));
                QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(window.Get(), kIdChkBoldUnderline)));
                ::GetDlgItemTextW(window.Get(), kIdBtnRestoreDefaults, label.data(),
                                 gsl::narrow_cast<int>(label.size()));
                QIQI_TEST_CHECK(std::wstring(label.data()) ==
                                GetUiStrings(UiLanguage::Auto).dlg_btn_restore_defaults);

                if (persist && accept) {
                    SetConfigRegistrySubKeyOverrideForTest(L"");
                    WithDismissedMessage(GetUiStrings(UiLanguage::Auto).dlg_save_failed, [&] {
                        ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                    });
                    QIQI_TEST_CHECK(!GetContext(window.Get()).done && !GetContext(window.Get()).accepted);
                    ReadSettingsFromWindowControls(window.Get(), edited);
                    check_restored(edited, original);
                    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
                    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before);
                }
                ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(accept ? IDOK : IDCANCEL, BN_CLICKED), 0);
                QIQI_TEST_CHECK(GetContext(window.Get()).done && GetContext(window.Get()).accepted == accept);
                QiQiConfig stored;
                QIQI_TEST_CHECK(LoadConfigFromRegistry(stored));
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before + (persist && accept ? 1 : 0));
                if (accept) {
                    check_restored(cfg, original);
                }
                ForEachConfigField([&](const auto& field) {
                    if (!accept) QIQI_TEST_CHECK(field.Value(cfg) == field.Value(original));
                    QIQI_TEST_CHECK(field.Value(stored) == field.Value(persist && accept ? cfg : original));
                });
                QIQI_TEST_CHECK(stored.user_dict_path == original.user_dict_path);
                QIQI_TEST_CHECK(stored.primary_dict_path ==
                                (persist && accept ? std::wstring{} : original.primary_dict_path));
            }
        }
    }

    QiQiConfig cfg;
    cfg.page_size = 9;
    cfg.primary_dict = PrimaryDict::Haifeng;
    QIQI_TEST_CHECK(SaveConfig(cfg));
    const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg, true));
    QIQI_TEST_CHECK(window);
    ::SendDlgItemMessageW(window.Get(), kIdBtnRestoreDefaults, BM_CLICK, 0, 0);
    QiQiConfig external = cfg;
    external.page_size = 8;
    external.primary_dict = PrimaryDict::BuiltinOnly;
    external.user_dict_path = L"externally_updated_user_dictionary.txt";
    QIQI_TEST_CHECK(SaveConfig(external));
    ::SendMessageW(window.Get(), kSettingsExternalConfigChangedMessage, 0, 0);
    QiQiConfig edited = cfg;
    ReadSettingsFromWindowControls(window.Get(), edited);
    QIQI_TEST_CHECK(edited.page_size == defaults.page_size && edited.primary_dict == defaults.primary_dict);
    QIQI_TEST_CHECK(edited.user_dict_path == external.user_dict_path);
    ::SendDlgItemMessageW(window.Get(), kIdComboPageSize, CB_SETCURSEL, 6, 0);
    ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    QIQI_TEST_CHECK(cfg.page_size == 7 && cfg.primary_dict == defaults.primary_dict);
    QiQiConfig stored;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(stored) && stored.page_size == 7);
    QIQI_TEST_CHECK(stored.user_dict_path == external.user_dict_path);
}

void TestSettingsFieldMergeMatrix() {
    const std::wstring subkey = L"Software\\QiQiIME_SettingsMatrix_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    size_t cases = 0;
    ForEachConfigField([&](const auto& field) {
        if (!field.settings_editable) return;
        for (const bool dirty : {false, true}) {
            QiQiConfig baseline;
            QIQI_TEST_CHECK(field.Assign(baseline, field.minimum));
            QiQiConfig config = baseline;
            QIQI_TEST_CHECK(SaveConfig(config));
            const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true));
            QIQI_TEST_CHECK(window);
            QiQiConfig local = baseline;
            if (dirty) QIQI_TEST_CHECK(field.Assign(local, field.maximum));
            WriteSettingsToWindowControls(window.Get(), local);
            GetContext(window.Get()).baseline_config = baseline;
            QiQiConfig external = baseline;
            ForEachConfigField([&](const auto& other) {
                QIQI_TEST_CHECK(other.Assign(external,
                    other.Value(baseline) == other.maximum ? other.minimum : other.maximum));
            });
            external.user_dict_path = L"external-matrix.txt";
            QIQI_TEST_CHECK(SaveConfig(external));
            ::SendMessageW(window.Get(), kSettingsExternalConfigChangedMessage, 0, 0);
            QiQiConfig actual = config;
            ReadSettingsFromWindowControls(window.Get(), actual);
            ForEachConfigField([&](const auto& other) {
                QIQI_TEST_CHECK(other.Value(actual) ==
                    (dirty && other.backup_key == field.backup_key ? field.Value(local) : other.Value(external)));
            });
            QIQI_TEST_CHECK(config.user_dict_path == external.user_dict_path);
            ++cases;
        }
    });
    QIQI_TEST_CHECK(cases == 38);
}

} // namespace

void RunSettingsSaveTests() {
    TestSaveRetryAndCancel();
    TestExternalConfigSyncWhileSettingsOpen();
    TestRestoreDefaults();
    TestSettingsFieldMergeMatrix();
}

} // namespace qiqi::test
