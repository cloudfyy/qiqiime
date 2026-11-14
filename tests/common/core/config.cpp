// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

namespace qiqi::test::common_cases {

void TestConfigFieldProjections() {
    constexpr std::array<std::pair<std::string_view, std::wstring_view>, 27> expected_keys{{
        {"primary_dict", L"PrimaryDict"}, {"enable_jidian_extra", L"EnableJidianExtra"},
        {"page_size", L"PageSize"}, {"candidate_layout", L"CandidateLayout"},
        {"auto_commit_4code", L"AutoCommit4Code"},
        {"push_commit_5th", L"PushCommit5th"}, {"semicolon_quote_quick_select", L"SemicolonQuoteQuickSelect"},
        {"show_code_hint", L"ShowCodeHint"}, {"enable_z_temp_english", L"EnableZTempEnglish"},
        {"dynamic_phrase_freq", L"DynamicPhraseFreq"}, {"default_chinese_mode", L"DefaultChineseMode"},
        {"default_full_width", L"DefaultFullWidth"}, {"default_chinese_punct", L"DefaultChinesePunct"},
        {"shift_toggle_mode", L"ShiftToggleMode"}, {"commit_on_english_switch", L"CommitOnEnglishSwitch"},
        {"show_status_bar", L"ShowStatusBar"}, {"beep_on_empty_4code", L"BeepOnEmpty4Code"},
        {"beep_on_dup_4code", L"BeepOnDup4Code"}, {"clear_on_empty_4code", L"ClearOnEmpty4Code"},
        {"enable_key_sound", L"EnableKeySound"}, {"up_down_page_turn", L"UpDownPageTurn"},
        {"ui_language", L"UiLanguage"}, {"underline_style", L"UnderlineStyle"},
        {"bold_underline", L"BoldUnderline"}, {"ui_theme", L"UiTheme"},
        {"ui_text_scale", L"UiTextScale"}, {"easy_reading", L"EasyReading"}
    }};
    constexpr std::array<std::string_view, 23> editable_keys{
        "primary_dict", "enable_jidian_extra", "page_size", "candidate_layout", "auto_commit_4code",
        "push_commit_5th", "semicolon_quote_quick_select", "show_code_hint", "dynamic_phrase_freq",
        "shift_toggle_mode", "commit_on_english_switch", "show_status_bar", "beep_on_empty_4code",
        "beep_on_dup_4code", "clear_on_empty_4code", "enable_key_sound", "up_down_page_turn",
        "ui_language", "underline_style", "bold_underline", "ui_theme", "ui_text_scale", "easy_reading"
    };
    const std::wstring subkey = L"Software\\QiQiIME_ConfigFields_" + std::to_wstring(::GetCurrentProcessId());
    const auto cleanup = gsl::finally([&]() noexcept {
        const LSTATUS error = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND);
    });
    const QiQiConfig defaults;
    size_t index = 0;
    ForEachConfigField([&](const auto& field) {
        const auto& expected = gsl::at(expected_keys, index++);
        QIQI_TEST_CHECK(field.backup_key == expected.first && field.registry_key == expected.second);
        QIQI_TEST_CHECK(std::ranges::count(editable_keys, field.backup_key) == (field.settings_editable ? 1 : 0));
        for (const auto& other : expected_keys) {
            QIQI_TEST_CHECK((field.backup_key == other.first) == (field.registry_key == other.second));
        }
        for (uint64_t value = field.minimum; value <= field.maximum; ++value) {
            QiQiConfig config;
            QIQI_TEST_CHECK(field.Assign(config, value));
            QIQI_TEST_CHECK(SaveConfigToRegistry(config, subkey.c_str()));
            QiQiConfig loaded;
            QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
            QIQI_TEST_CHECK(field.Value(loaded) == value);
        }
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_SET_VALUE, key.Put()) == ERROR_SUCCESS);
        const DWORD invalid = gsl::narrow_cast<DWORD>(field.maximum + 1);
        QIQI_TEST_CHECK(::RegSetValueExW(key.Get(), field.registry_key, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&invalid), sizeof(invalid)) == ERROR_SUCCESS);
        QiQiConfig loaded;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
        using Value = std::remove_cvref_t<decltype(defaults.*(field.member))>;
        if constexpr (std::is_same_v<Value, bool>) {
            QIQI_TEST_CHECK(field.Value(loaded) == 1);
        } else {
            QIQI_TEST_CHECK(field.Value(loaded) == field.Value(defaults));
            QiQiConfig unchanged;
            QIQI_TEST_CHECK(!field.Assign(unchanged, invalid));
            QIQI_TEST_CHECK(field.Value(unchanged) == field.Value(defaults));
        }
        QIQI_TEST_CHECK(::RegDeleteValueW(key.Get(), field.registry_key) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
        QIQI_TEST_CHECK(field.Value(loaded) == field.Value(defaults));

        for (const bool local_changed : {false, true}) {
            for (const bool external_changed : {false, true}) {
                QiQiConfig baseline;
                QIQI_TEST_CHECK(field.Assign(baseline, field.minimum));
                QiQiConfig local = baseline;
                QiQiConfig external = baseline;
                if (local_changed) QIQI_TEST_CHECK(field.Assign(local, field.maximum));
                if (external_changed) QIQI_TEST_CHECK(field.Assign(external, field.maximum));
                QIQI_TEST_CHECK(field.HasSettingsChange(baseline, local) ==
                                (field.settings_editable && local_changed));
                local.user_dict_path = L"local.txt";
                external.user_dict_path = L"external.txt";
                auto merged = external;
                MergeSettingsConfig(merged, baseline, local);
                QIQI_TEST_CHECK(field.Value(merged) ==
                    (field.settings_editable && local_changed ? field.Value(local) : field.Value(external)));
                QIQI_TEST_CHECK(merged.user_dict_path == external.user_dict_path);
                ForEachConfigField([&](const auto& other) {
                    if (other.backup_key != field.backup_key) {
                        QIQI_TEST_CHECK(other.Value(merged) == other.Value(external));
                    }
                });
            }
        }
    });
    QIQI_TEST_CHECK(index == expected_keys.size());
    for (const size_t page_size : {size_t{0}, (std::numeric_limits<size_t>::max)()}) {
        QiQiConfig config;
        config.page_size = page_size;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config, subkey.c_str()));
        QiQiConfig loaded;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
        QIQI_TEST_CHECK(loaded.page_size == (page_size == 0 ? 1u : 9u));
    }
}

void TestDictionaryLoadPlans() {
    for (const PrimaryDict primary : {PrimaryDict::BuiltinOnly, PrimaryDict::Jidian, PrimaryDict::Haifeng, PrimaryDict::Custom}) {
        for (const bool extra : {false, true}) {
            const auto plan = GetBundledDictionaryLoadPlan(primary, extra);
            const size_t expected_count =
                (primary == PrimaryDict::BuiltinOnly || primary == PrimaryDict::Custom) ? 0u :
                (primary == PrimaryDict::Jidian && extra ? 2u : 1u);
            QIQI_TEST_CHECK(plan.size() == expected_count);
            if (!plan.empty()) {
                QIQI_TEST_CHECK(plan.front().role == DictionarySourceRole::Primary);
                QIQI_TEST_CHECK(plan.front().file_name ==
                    (primary == PrimaryDict::Jidian ? L"wubi86_jidian.txt" : L"wubi86_haifeng.txt"));
            }
            if (plan.size() == 2) {
                QIQI_TEST_CHECK(plan.back().role == DictionarySourceRole::Extra);
                QIQI_TEST_CHECK(plan.back().file_name == L"wubi86_jidian_extra.txt");
            }
        }
    }
    QIQI_TEST_CHECK(PrimaryDictToString(PrimaryDict::Custom) == "custom");
    QIQI_TEST_CHECK(GetBundledDictionaryLoadPlan(static_cast<PrimaryDict>(99), true).empty());

    // Test PrimaryDictPath registry persistence and MergeSettingsConfig primary_dict_path handling
    const std::wstring subkey = L"Software\\QiQiIME_PrimaryDictPathTest_" + std::to_wstring(::GetCurrentProcessId());
    const auto cleanup = gsl::finally([&]() noexcept {
        std::ignore = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    });
    QiQiConfig cfg;
    cfg.primary_dict = PrimaryDict::Custom;
    cfg.primary_dict_path = L"C:\\CustomDicts\\my_wubi86.txt";
    QIQI_TEST_CHECK(SaveConfigToRegistry(cfg, subkey.c_str()));
    QiQiConfig loaded;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
    QIQI_TEST_CHECK(loaded.primary_dict == PrimaryDict::Custom);
    QIQI_TEST_CHECK(loaded.primary_dict_path == L"C:\\CustomDicts\\my_wubi86.txt");

    QiQiConfig baseline;
    baseline.primary_dict_path = L"baseline.txt";
    QiQiConfig local = baseline;
    QiQiConfig external = baseline;
    external.primary_dict_path = L"external.txt";
    auto merged_untouched = external;
    MergeSettingsConfig(merged_untouched, baseline, local);
    QIQI_TEST_CHECK(merged_untouched.primary_dict_path == L"external.txt");
    local.primary_dict_path = L"local_custom.txt";
    auto merged_edited = external;
    MergeSettingsConfig(merged_edited, baseline, local);
    QIQI_TEST_CHECK(merged_edited.primary_dict_path == L"local_custom.txt");
    QIQI_TEST_CHECK(std::wcscmp(ResolveDictionaryPathDisplayText(L"", L"none"), L"none") == 0);
    QIQI_TEST_CHECK(std::wcscmp(ResolveDictionaryPathDisplayText(local.primary_dict_path, L"none"), L"local_custom.txt") == 0);
}

void TestIniConfiguration() {
    // 1. Test ResolveBundledDataFile fallback across <dll_dir>/<name>, <dll_dir>/../data/<name>, and <dll_dir>/../<name>
    {
        const auto base_dir = std::filesystem::temp_directory_path() / L"QiQiIME_BundledLayoutTest";
        const auto x64_dir = base_dir / L"x64";
        const auto shared_data_dir = base_dir / L"data";
        std::error_code layout_ec;
        std::filesystem::remove_all(base_dir, layout_ec);
        std::filesystem::create_directories(x64_dir, layout_ec);
        std::filesystem::create_directories(shared_data_dir, layout_ec);

        QIQI_TEST_CHECK(ResolveBundledDataFile({}, L"wubi86_jidian.txt") == std::filesystem::path(L"wubi86_jidian.txt"));
        const std::array<uint8_t, 4> dummy_bytes{'a', ' ', 'x', '\n'};
        // When file exists in ../data/, ResolveBundledDataFile(x64_dir, ...) resolves to ../data/
        const auto shared_dict = shared_data_dir / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(shared_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == shared_dict);

        // When file exists in ../, ResolveBundledDataFile(x64_dir, ...) resolves to ../
        const auto parent_dict = base_dir / L"wubi86_haifeng.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(parent_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_haifeng.txt") == parent_dict);

        // When file exists directly in dll_dir, direct path takes precedence
        const auto local_dict = x64_dir / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(local_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == local_dict);

        std::filesystem::remove_all(base_dir, layout_ec);
    }

    // 2. Test pure HKCU registry configuration & user_dict binary mirroring (using isolated test subkey)
    constexpr const wchar_t* kTestRegSubKey = L"Software\\QiQiIME_UnitTest";
    ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
    SetConfigRegistrySubKeyOverrideForTest(kTestRegSubKey);
    const auto restore_reg = gsl::finally([]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
    });

    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == kTestRegSubKey);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == 0);
    QiQiConfig reg_out{};
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out));
    QIQI_TEST_CHECK(!SaveConfigToRegistry(reg_out, L""));
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out, nullptr, L""));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(L"") == 0);

    // First LoadConfig initializes default values in HKCU\<subkey>
    const QiQiConfig discovered = LoadConfig(nullptr);
    QIQI_TEST_CHECK(discovered.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(discovered.enable_jidian_extra);
    QIQI_TEST_CHECK(discovered.page_size == 5);
    QIQI_TEST_CHECK(discovered.commit_on_english_switch);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() >= 1);

    // Save custom config with all fields modified and verify round-trip via LoadConfigFromRegistry & LoadConfig
    QiQiConfig custom_reg_cfg{};
    custom_reg_cfg.primary_dict = PrimaryDict::Haifeng;
    custom_reg_cfg.enable_jidian_extra = false;
    custom_reg_cfg.user_dict_path = L"custom_words.txt";
    custom_reg_cfg.page_size = 8;
    custom_reg_cfg.auto_commit_4code = false;
    custom_reg_cfg.push_commit_5th = false;
    custom_reg_cfg.semicolon_quote_quick_select = false;
    custom_reg_cfg.show_code_hint = false;
    custom_reg_cfg.enable_z_temp_english = false;
    custom_reg_cfg.dynamic_phrase_freq = true;
    custom_reg_cfg.default_chinese_mode = false;
    custom_reg_cfg.default_full_width = true;
    custom_reg_cfg.default_chinese_punct = false;
    custom_reg_cfg.shift_toggle_mode = false;
    custom_reg_cfg.commit_on_english_switch = false;
    custom_reg_cfg.show_status_bar = true;
    custom_reg_cfg.beep_on_empty_4code = false;
    custom_reg_cfg.beep_on_dup_4code = true;
    custom_reg_cfg.clear_on_empty_4code = true;
    custom_reg_cfg.enable_key_sound = true;
    custom_reg_cfg.up_down_page_turn = true;
    custom_reg_cfg.ui_language = UiLanguage::EnUs;
    custom_reg_cfg.underline_style = CompositionUnderlineStyle::Squiggle;
    custom_reg_cfg.bold_underline = true;

    QIQI_TEST_CHECK(SaveConfig(custom_reg_cfg));
    uint64_t seq_after_save = 0;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(reg_out, &seq_after_save));
    QIQI_TEST_CHECK(seq_after_save >= 2);
    QIQI_TEST_CHECK(reg_out.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!reg_out.enable_jidian_extra);
    QIQI_TEST_CHECK(reg_out.user_dict_path == L"custom_words.txt");
    QIQI_TEST_CHECK(reg_out.page_size == 8);
    QIQI_TEST_CHECK(!reg_out.auto_commit_4code);
    QIQI_TEST_CHECK(!reg_out.push_commit_5th);
    QIQI_TEST_CHECK(!reg_out.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!reg_out.show_code_hint);
    QIQI_TEST_CHECK(!reg_out.enable_z_temp_english);
    QIQI_TEST_CHECK(reg_out.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!reg_out.default_chinese_mode);
    QIQI_TEST_CHECK(reg_out.default_full_width);
    QIQI_TEST_CHECK(!reg_out.default_chinese_punct);
    QIQI_TEST_CHECK(!reg_out.shift_toggle_mode);
    QIQI_TEST_CHECK(!reg_out.commit_on_english_switch);
    QIQI_TEST_CHECK(reg_out.show_status_bar);
    QIQI_TEST_CHECK(!reg_out.beep_on_empty_4code);
    QIQI_TEST_CHECK(reg_out.beep_on_dup_4code);
    QIQI_TEST_CHECK(reg_out.clear_on_empty_4code);
    QIQI_TEST_CHECK(reg_out.enable_key_sound);
    QIQI_TEST_CHECK(reg_out.up_down_page_turn);
    QIQI_TEST_CHECK(reg_out.ui_language == UiLanguage::EnUs);
    QIQI_TEST_CHECK(reg_out.underline_style == CompositionUnderlineStyle::Squiggle);
    QIQI_TEST_CHECK(reg_out.bold_underline);

    const QiQiConfig reloaded = LoadConfig(nullptr);
    QIQI_TEST_CHECK(reloaded.page_size == 8 && reloaded.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!reloaded.commit_on_english_switch);
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
            HKEY_CURRENT_USER, kTestRegSubKey, 0, KEY_SET_VALUE, key.Put()) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::RegDeleteValueW(key.Get(), L"CommitOnEnglishSwitch") == ERROR_SUCCESS);
    }
    QIQI_TEST_CHECK(LoadConfigFromRegistry(reg_out) && reg_out.commit_on_english_switch);

    std::filesystem::path directory;
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(L"relative.sync") == E_INVALIDARG);
    QIQI_TEST_CHECK(FAILED(SaveUserDictSyncDirectory({}, L"")));
    QIQI_TEST_CHECK(FAILED(LoadUserDictSyncDirectory(directory, L"")));
    const auto shared_directory = GetSyncChannelDirectoryForFile(GetDefaultUserDictPath());
    const uint64_t before_path = QueryRegistryConfigSequence();
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(shared_directory) == S_OK);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() > before_path);
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory == shared_directory);
    const uint64_t unchanged = QueryRegistryConfigSequence();
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(shared_directory) == S_OK);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == unchanged);
    QIQI_TEST_CHECK(SaveConfig(reg_out));
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory == shared_directory);
    const std::filesystem::path maximum_path(L"C:\\" + std::wstring(2044, L'a'));
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(maximum_path) == S_OK);
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory == maximum_path);
    const auto maximum_sequence = QueryRegistryConfigSequence();
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(std::filesystem::path(maximum_path.wstring() + L"a")) == E_INVALIDARG);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == maximum_sequence);
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory == maximum_path);
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory({}) == S_OK);
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory.empty());
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
            HKEY_CURRENT_USER, kTestRegSubKey, 0, KEY_SET_VALUE, key.Put()) == ERROR_SUCCESS);
        const std::wstring relative = L"relative.sync";
        QIQI_TEST_CHECK(::RegSetValueExW(key.Get(), L"UserDictSyncDirectory", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(relative.c_str()),
            gsl::narrow_cast<DWORD>((relative.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        QIQI_TEST_CHECK(directory.empty());
        QIQI_TEST_CHECK(::RegSetValueExW(key.Get(), L"UserDictSyncDirectory", 0, REG_BINARY,
            reinterpret_cast<const BYTE*>(relative.c_str()),
            gsl::narrow_cast<DWORD>((relative.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        const uint64_t invalid_sequence = QueryRegistryConfigSequence();
        QIQI_TEST_CHECK(SaveUserDictSyncDirectory(shared_directory) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == invalid_sequence);
    }
}

} // namespace qiqi::test::common_cases
