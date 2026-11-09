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

namespace settings_geometry = settings_detail::settings_layout;
constexpr std::array kSettingsLayoutRectCases{
    std::pair{settings_geometry::kDictionaryCard, RECT{18, 16, 404, 196}},
    std::pair{settings_geometry::kInputCard, RECT{18, 206, 404, 434}},
    std::pair{settings_geometry::kAppearanceCard, RECT{416, 16, 802, 202}},
    std::pair{settings_geometry::kSoundCard, RECT{416, 206, 802, 434}},
    std::pair{settings_geometry::kBrowseDictionary, RECT{306, 140, 390, 166}},
    std::pair{settings_geometry::kDictionaryPath, RECT{56, 168, 390, 188}},
    std::pair{settings_geometry::kPageSizeLabel, RECT{430, 78, 586, 102}},
    std::pair{settings_geometry::kPageSizeCombo, RECT{588, 74, 668, 334}},
    std::pair{settings_geometry::kLanguageLabel, RECT{430, 112, 586, 136}},
    std::pair{settings_geometry::kLanguageCombo, RECT{588, 108, 788, 308}},
    std::pair{settings_geometry::kUnderlineLabel, RECT{430, 146, 586, 170}},
    std::pair{settings_geometry::kUnderlineCombo, RECT{588, 142, 788, 362}},
    std::pair{settings_geometry::kEnglishSwitchLabel, RECT{430, 378, 586, 402}},
    std::pair{settings_geometry::kEnglishSwitchCombo, RECT{588, 374, 788, 534}},
    std::pair{settings_geometry::kAboutButton, RECT{18, 448, 122, 480}},
    std::pair{settings_geometry::kDictionaryManagerButton, RECT{132, 448, 288, 480}},
    std::pair{settings_geometry::kHelpButton, RECT{298, 448, 426, 480}},
    std::pair{settings_geometry::kRestoreDefaultsButton, RECT{436, 448, 586, 480}},
    std::pair{settings_geometry::kOkButton, RECT{596, 448, 694, 480}},
    std::pair{settings_geometry::kCancelButton, RECT{704, 448, 802, 480}}};
static_assert([]() constexpr noexcept {
    for (const auto& [actual, expected] : kSettingsLayoutRectCases) {
        if (actual.left != expected.left || actual.top != expected.top ||
            actual.right != expected.right || actual.bottom != expected.bottom) {
            return false;
        }
    }
    return true;
}());
static_assert(settings_geometry::kClientSize.cx == 820 && settings_geometry::kClientSize.cy == 494);
static_assert(settings_geometry::kLeftColumn == 32 && settings_geometry::kRightColumn == 430);
static_assert(settings_geometry::kRadioHeight == 22 && settings_geometry::kCheckboxHeight == 24);
static_assert(settings_geometry::kDictionaryFirstRow == 46 && settings_geometry::kHaifengRow == 94);
static_assert(settings_geometry::kBuiltinRow == 118 && settings_geometry::kCustomRow == 142);
static_assert(settings_geometry::kCustomRadioWidth == 268 && settings_geometry::kOptionWidth == 360);
static_assert(settings_geometry::kInputFirstRow == 232 && settings_geometry::kInputRowStep == 28);
static_assert(settings_geometry::kSoundFirstRow == 238 && settings_geometry::kSoundRowStep == 34);
static_assert(settings_geometry::kBoldUnderlineRow == 174);
static_assert(settings_geometry::kFontSize == 15 && settings_geometry::kBoldFontSize == 12);

thread_local int dictionary_confirmation_button = IDCANCEL;
thread_local bool dictionary_confirmation_seen = false;

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
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
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
    QIQI_TEST_CHECK(combo_rect.left - combo_origin.x == ScalePxByDpi(588, 192));

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

} // namespace

void RunSettingsDialogTests() {
    TestDictionaryConfirmationText();
    TestSettingsDialogControls();
    TestSettingsWindowMonitorChanges();
}

} // namespace qiqi::test
