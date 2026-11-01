#include "common/test_support.h"
#include "common/i18n.h"
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
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), kIdChkExtraDict)));
    ::GetWindowTextW(hwnd.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
    QIQI_TEST_CHECK(std::wstring(title_buf.data()) == GetUiStrings(UiLanguage::ZhCn).dlg_title);
    QIQI_TEST_CHECK(cfg.primary_dict == PrimaryDict::Jidian && cfg.page_size == 5 && !cfg.enable_key_sound);
}

} // namespace

void RunSettingsSaveTests() {
    TestSaveRetryAndCancel();
    TestExternalConfigSyncWhileSettingsOpen();
}

} // namespace qiqi::test
