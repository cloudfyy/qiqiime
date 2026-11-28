// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"
#include "common/cfgjson.h"
#include "common/configclient.h"
#include "common/configwire.h"

namespace qiqi::test::common_cases {

void TestConfigTransport() {
    struct RouteCase {
        bool app_container;
        bool has_lpac_com;
        bool has_registry_read;
        config::ConfigReadRoute expected;
    };
    constexpr std::array route_cases{
        RouteCase{
            false,
            false,
            false,
            config::ConfigReadRoute::Broker},
        RouteCase{
            false,
            false,
            true,
            config::ConfigReadRoute::Broker},
        RouteCase{
            false,
            true,
            false,
            config::ConfigReadRoute::Broker},
        RouteCase{
            false,
            true,
            true,
            config::ConfigReadRoute::Broker},
        RouteCase{
            true,
            false,
            false,
            config::ConfigReadRoute::
                BuiltinDefaults},
        RouteCase{
            true,
            false,
            true,
            config::ConfigReadRoute::
                AppContainerRegistry},
        RouteCase{
            true,
            true,
            false,
            config::ConfigReadRoute::Broker},
        RouteCase{
            true,
            true,
            true,
            config::ConfigReadRoute::Broker},
    };
    for (const auto& route_case :
         route_cases) {
        QIQI_TEST_CHECK(
            config::SelectConfigReadRoute(
                route_case.app_container,
                route_case.has_lpac_com,
                route_case
                    .has_registry_read) ==
            route_case.expected);
    }
    config::ConfigReadRoute process_route =
        config::ConfigReadRoute::
            BuiltinDefaults;
    QIQI_TEST_CHECK(
        config::
            QueryProcessConfigReadRoute(
                process_route) == S_OK);
    if (!IsProcessAppContainer()) {
        QIQI_TEST_CHECK(
            process_route ==
            config::ConfigReadRoute::
                Broker);
    }
    for (const HRESULT result :
         {S_OK,
          S_FALSE,
          E_PENDING,
          HRESULT_FROM_WIN32(
              ERROR_ACCESS_DENIED)}) {
        QIQI_TEST_CHECK(
            config::DecodeAsyncResult(
                config::EncodeAsyncResult(
                    result)) ==
            result);
    }
    QiQiConfig value;
    ForEachConfigField([&](const auto& field) {
        QIQI_TEST_CHECK(
            field.Assign(value, field.maximum));
    });
    value.primary_dict_path =
        L"C:\\private\\primary.qdict";
    value.user_dict_path =
        L"C:\\private\\user.txt";
    value.candidate_font_family =
        L"Microsoft YaHei UI";

    config::Snapshot snapshot{
        .revision = 0x1020304050607080ull,
        .notification_result =
            HRESULT_FROM_WIN32(
                ERROR_RETRY),
        .value = value,
    };
    QIQI_TEST_CHECK(
        MakeBuiltinImeSkinCatalog(
            snapshot.skin_catalog) == S_OK);

    const auto equal = [](
                           const QiQiConfig& left,
                           const QiQiConfig& right) {
        ForEachConfigField(
            [&](const auto& field) {
                QIQI_TEST_CHECK(
                    field.Value(left) ==
                    field.Value(right));
            });
        QIQI_TEST_CHECK(
            left.candidate_font_family ==
            right.candidate_font_family);
        QIQI_TEST_CHECK(
            left.primary_dict_path ==
            right.primary_dict_path);
        QIQI_TEST_CHECK(
            left.user_dict_path ==
            right.user_dict_path);
    };
    const auto check_snapshot =
        [&](const config::Snapshot& decoded) {
            QIQI_TEST_CHECK(
                decoded.revision ==
                snapshot.revision);
            QIQI_TEST_CHECK(
                decoded.notification_result ==
                snapshot.notification_result);
            equal(decoded.value, snapshot.value);
            QIQI_TEST_CHECK(
                decoded.skin_catalog.id ==
                snapshot.skin_catalog.id);
            QIQI_TEST_CHECK(
                decoded.skin_catalog.skins.size() ==
                snapshot.skin_catalog.skins.size());
            for (size_t index = 0;
                 index <
                 snapshot.skin_catalog.skins.size();
                 ++index) {
                const auto& expected =
                    gsl::at(
                        snapshot.skin_catalog.skins,
                        index);
                const auto& actual =
                    gsl::at(
                        decoded.skin_catalog.skins,
                        index);
                QIQI_TEST_CHECK(
                    actual.id == expected.id &&
                    actual.display_order ==
                        expected.display_order &&
                    actual.name_zh ==
                        expected.name_zh &&
                    actual.name_en ==
                        expected.name_en &&
                    actual.palettes.light ==
                        expected.palettes.light &&
                    actual.palettes.dark ==
                        expected.palettes.dark);
            }
        };

    std::vector<uint8_t> bytes;
    QIQI_TEST_CHECK(
        config::EncodeSnapshot(
            snapshot, bytes) == S_OK);
    QIQI_TEST_CHECK(
        !bytes.empty() &&
        bytes.size() <=
            config::kMaxConfigTransportBytes);
    config::Snapshot decoded;
    QIQI_TEST_CHECK(
        config::DecodeSnapshot(
            bytes, decoded) == S_OK);
    check_snapshot(decoded);

    auto corrupted = bytes;
    QIQI_TEST_CHECK(corrupted.size() > 20);
    gsl::at(corrupted, 20) ^= 1;
    auto unchanged = decoded;
    QIQI_TEST_CHECK(
        FAILED(config::DecodeSnapshot(
            corrupted, unchanged)));
    check_snapshot(unchanged);

    QIQI_TEST_CHECK(
        FAILED(config::DecodeSnapshot(
            std::span(
                bytes).first(bytes.size() - 1),
            unchanged)));
    QIQI_TEST_CHECK(
        FAILED(config::DecodeSnapshot(
            {}, unchanged)));
    check_snapshot(unchanged);

    const auto oversized =
        std::vector<uint8_t>(
            config::kMaxConfigTransportBytes + 1);
    QIQI_TEST_CHECK(
        FAILED(config::DecodeSnapshot(
            oversized, unchanged)));

    config::SaveRequest save{
        .expected_revision = 77,
        .value = value,
    };
    std::vector<uint8_t> save_bytes;
    QIQI_TEST_CHECK(
        config::EncodeSaveRequest(
            save, save_bytes) == S_OK);
    config::SaveRequest decoded_save;
    QIQI_TEST_CHECK(
        config::DecodeSaveRequest(
            save_bytes, decoded_save) == S_OK);
    QIQI_TEST_CHECK(
        decoded_save.expected_revision ==
        save.expected_revision);
    equal(decoded_save.value, save.value);
    save.expected_revision = 0;
    const auto before_save_bytes = save_bytes;
    QIQI_TEST_CHECK(
        FAILED(config::EncodeSaveRequest(
            save, save_bytes)));
    QIQI_TEST_CHECK(
        save_bytes == before_save_bytes);

    config::SyncDirectoryRequest sync{
        .expected_revision = 11,
        .directory =
            L"C:\\Users\\user\\AppData\\Local"
            L"\\QiQiIME\\user_dict.txt.sync",
    };
    std::vector<uint8_t> sync_bytes;
    QIQI_TEST_CHECK(
        config::EncodeSyncDirectoryRequest(
            sync, sync_bytes) == S_OK);
    config::SyncDirectoryRequest decoded_sync;
    QIQI_TEST_CHECK(
        config::DecodeSyncDirectoryRequest(
            sync_bytes, decoded_sync) == S_OK);
    QIQI_TEST_CHECK(
        decoded_sync.expected_revision ==
        sync.expected_revision);
    QIQI_TEST_CHECK(
        decoded_sync.directory ==
        sync.directory);
    sync.directory = L"relative";
    QIQI_TEST_CHECK(
        FAILED(config::EncodeSyncDirectoryRequest(
            sync, sync_bytes)));

    ULONG rpc_size = 0;
    byte* rpc_bytes = nullptr;
    QIQI_TEST_CHECK(
        config::AllocateRpcBuffer(
            bytes,
            rpc_size,
            rpc_bytes) == S_OK);
    const ScopedCoTaskMem rpc_memory(
        rpc_bytes);
    std::vector<uint8_t> round_trip_bytes;
    QIQI_TEST_CHECK(
        config::ReadRpcBuffer(
            rpc_size,
            rpc_bytes,
            round_trip_bytes) == S_OK);
    QIQI_TEST_CHECK(
        round_trip_bytes == bytes);

    rpc_size = 1;
    rpc_bytes = reinterpret_cast<byte*>(
        1);
    QIQI_TEST_CHECK(
        FAILED(config::AllocateRpcBuffer(
            oversized,
            rpc_size,
            rpc_bytes)));
    QIQI_TEST_CHECK(rpc_size == 0);
    QIQI_TEST_CHECK(rpc_bytes == nullptr);

    QIQI_TEST_CHECK(
        config::ReadRpcBuffer(
            0,
            nullptr,
            round_trip_bytes) == S_OK);
    QIQI_TEST_CHECK(
        round_trip_bytes.empty());
    QIQI_TEST_CHECK(
        config::ReadRpcBuffer(
            1,
            nullptr,
            round_trip_bytes) == E_POINTER);
    QIQI_TEST_CHECK(
        FAILED(config::ReadRpcBuffer(
            gsl::narrow_cast<ULONG>(
                config::
                    kMaxConfigTransportBytes + 1),
            reinterpret_cast<const byte*>(
                1),
            round_trip_bytes)));
}

void TestConfigJson() {
    QiQiConfig baseline;
    baseline.primary_dict_path = L"local_primary.txt";
    baseline.user_dict_path = L"local_user.txt";
    baseline.ui_text_scale = UiTextScale::Percent200;
    baseline.space_enter_mode = WubiSpaceEnterMode::SpaceSelectEnterClear;
    baseline.full_code_practice = true;
    baseline.phrase_char_selection = true;
    baseline.enable_z_pinyin_lookup = true;
    baseline.caps_lock_toggle_mode = true;
    baseline.output_conversion = OutputConversion::SimplifiedToTraditional;
    const auto check_equal = [](const QiQiConfig& left, const QiQiConfig& right) {
        ForEachConfigField([&](const auto& field) { QIQI_TEST_CHECK(field.Value(left) == field.Value(right)); });
        QIQI_TEST_CHECK(left.primary_dict_path == right.primary_dict_path);
        QIQI_TEST_CHECK(left.user_dict_path == right.user_dict_path);
        QIQI_TEST_CHECK(left.candidate_font_family == right.candidate_font_family);
    };
    QiQiConfig source;
    ForEachConfigField([&](const auto& field) { QIQI_TEST_CHECK(field.Assign(source, field.maximum)); });
    source.primary_dict_path = L"C:\\private\\source.txt";
    source.user_dict_path = L"C:\\private\\words.txt";
    source.candidate_font_family = L"Consolas";
    ConfigJsonError error;
    std::string json;
    QIQI_TEST_CHECK(SerializeConfigJson(source, json, error) == S_OK);
    QIQI_TEST_CHECK(json.find("\"format\": \"qiqiime-config\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"version\": 1") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"enable_code_completion\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"single_character_mode\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"full_code_practice\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"phrase_char_selection\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"enable_z_pinyin_lookup\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"caps_lock_toggle_mode\": true") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"output_conversion\": \"traditional-to-simplified\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"space_enter_mode\": \"enter-select-space-clear\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"page_size\": 9") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"ui_theme\": \"high_contrast\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"ui_text_scale\": \"200%\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"candidate_font_family\": \"Consolas\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("\"candidate_font_weight\": \"black\"") != std::string::npos);
    QIQI_TEST_CHECK(json.find("path") == std::string::npos && json.find("private") == std::string::npos);
    QiQiConfig result;
    QIQI_TEST_CHECK(ParseConfigJson(json, baseline, result, error) == S_OK);
    source.primary_dict_path = baseline.primary_dict_path;
    source.user_dict_path = baseline.user_dict_path;
    check_equal(result, source);
    const auto document = [](std::string_view settings) {
        return std::string(R"({"format":"qiqiime-config","version":1,"settings":{)") +
            std::string(settings) + "}}";
    };
    for (const auto prefix : {std::string{}, std::string{"\xEF\xBB\xBF"}}) {
        QIQI_TEST_CHECK(ParseConfigJson(prefix + document(R"("page_size":7,"enable_code_completion":false)"),
            baseline, result, error) == S_OK);
        auto expected = baseline;
        expected.page_size = 7;
        expected.enable_code_completion = false;
        check_equal(result, expected);
        QIQI_TEST_CHECK(ParseConfigJson(document(""), baseline, result, error) == S_OK);
        check_equal(result, baseline);
    }
    ForEachConfigField([&](const auto& field) {
        for (uint64_t value = field.minimum; value <= field.maximum; ++value) {
            auto candidate = baseline;
            QIQI_TEST_CHECK(field.Assign(candidate, value));
            QIQI_TEST_CHECK(SerializeConfigJson(candidate, json, error) == S_OK);
            QIQI_TEST_CHECK(ParseConfigJson(json, baseline, result, error) == S_OK);
            check_equal(result, candidate);
        }
    });
    struct InvalidCase {
        std::string_view settings;
        ConfigJsonIssue issue;
        std::string_view field;
    };
    constexpr std::array cases{
        InvalidCase{R"("page_size":0)", ConfigJsonIssue::OutOfRange, "settings.page_size"},
        InvalidCase{R"("page_size":10)", ConfigJsonIssue::OutOfRange, "settings.page_size"},
        InvalidCase{R"("page_size":-1)", ConfigJsonIssue::OutOfRange, "settings.page_size"},
        InvalidCase{R"("page_size":1e999)", ConfigJsonIssue::OutOfRange, "settings.page_size"},
        InvalidCase{R"("page_size":5.0)", ConfigJsonIssue::InvalidType, "settings.page_size"},
        InvalidCase{R"("page_size":"5")", ConfigJsonIssue::InvalidType, "settings.page_size"},
        InvalidCase{R"("enable_code_completion":1)", ConfigJsonIssue::InvalidType, "settings.enable_code_completion"},
        InvalidCase{R"("single_character_mode":1)", ConfigJsonIssue::InvalidType, "settings.single_character_mode"},
        InvalidCase{R"("full_code_practice":1)", ConfigJsonIssue::InvalidType, "settings.full_code_practice"},
        InvalidCase{R"("phrase_char_selection":1)", ConfigJsonIssue::InvalidType, "settings.phrase_char_selection"},
        InvalidCase{R"("space_enter_mode":2)", ConfigJsonIssue::InvalidType, "settings.space_enter_mode"},
        InvalidCase{R"("space_enter_mode":"unknown")", ConfigJsonIssue::InvalidEnum, "settings.space_enter_mode"},
        InvalidCase{R"("output_conversion":"unknown")", ConfigJsonIssue::InvalidEnum, "settings.output_conversion"},
        InvalidCase{R"("output_conversion":"off")", ConfigJsonIssue::OutOfRange, "settings.output_conversion"},
        InvalidCase{R"("ui_theme":1)", ConfigJsonIssue::InvalidType, "settings.ui_theme"},
        InvalidCase{R"("ui_theme":"Dark")", ConfigJsonIssue::InvalidEnum, "settings.ui_theme"},
        InvalidCase{R"("ui_theme":null)", ConfigJsonIssue::InvalidType, "settings.ui_theme"},
        InvalidCase{R"("ime_skin_id":"unknown")", ConfigJsonIssue::InvalidEnum, "settings.ime_skin_id"},
        InvalidCase{R"("ime_skin_id":1)", ConfigJsonIssue::InvalidType, "settings.ime_skin_id"},
        InvalidCase{R"("candidate_font_family":1)", ConfigJsonIssue::InvalidType, "settings.candidate_font_family"},
        InvalidCase{R"("candidate_font_weight":"regular")", ConfigJsonIssue::InvalidEnum, "settings.candidate_font_weight"},
        InvalidCase{R"("page_size":7,"page_size":8)", ConfigJsonIssue::DuplicateField, "settings.page_size"},
        InvalidCase{R"("ui_theme":"dark","\u0075i_theme":"light")", ConfigJsonIssue::DuplicateField, "settings.ui_theme"},
        InvalidCase{R"("user_dict_path":"C:\\words.txt")", ConfigJsonIssue::UnknownField, "settings.user_dict_path"},
        InvalidCase{R"("PrimaryDict":0)", ConfigJsonIssue::UnknownField, "settings.PrimaryDict"},
        InvalidCase{R"("ui_theme":{})", ConfigJsonIssue::InvalidType, "settings.ui_theme"},
        InvalidCase{R"("page_size":[])", ConfigJsonIssue::InvalidType, "settings.page_size"},
        InvalidCase{R"("easy_reading":true,"ui_text_scale":"100%")", ConfigJsonIssue::ComfortScaleRequired, "settings.ui_text_scale"}
    };
    for (const auto& invalid : cases) {
        result = baseline;
        QIQI_TEST_CHECK(FAILED(ParseConfigJson(document(invalid.settings), baseline, result, error)));
        QIQI_TEST_CHECK(error.issue == invalid.issue && error.field == invalid.field);
        check_equal(result, baseline);
    }
    for (const auto invalid : {
        R"({"format":"other","version":1,"settings":{}})",
        R"({"format":"qiqiime-config","version":2,"settings":{}})",
        R"({"format":"qiqiime-config","version":1.0,"settings":{}})",
        R"({"format":"qiqiime-config","version":1,"settings":{},"extra":true})",
        R"({"format":"qiqiime-config","format":"qiqiime-config","version":1,"settings":{}})",
        R"({"format":"qiqiime-config","version":1})",
        R"({"format":"qiqiime-config","version":1,"settings":false})",
        R"({"format":"qiqiime-config","version":1,"settings":{}} trailing)",
        R"({"format":"qiqiime-config","version":1,"settings":{"page_size":5,}})",
        R"({"format":"qiqiime-config","version":1,"settings":{"page_size":1e999}})",
        "null", "[]", "{\"", "\xFF"}) {
        result = baseline;
        QIQI_TEST_CHECK(FAILED(ParseConfigJson(invalid, baseline, result, error)));
        check_equal(result, baseline);
    }
    auto no_dictionary = baseline;
    no_dictionary.primary_dict_path.clear();
    QIQI_TEST_CHECK(FAILED(ParseConfigJson(document(R"("primary_dict":"custom")"), no_dictionary, result, error)));
    QIQI_TEST_CHECK(error.issue == ConfigJsonIssue::CustomDictionaryRequired);
    auto oversized = document("");
    oversized.resize(kMaxConfigJsonBytes + 1, ' ');
    QIQI_TEST_CHECK(FAILED(ParseConfigJson(oversized, baseline, result, error)));
    QIQI_TEST_CHECK(error.issue == ConfigJsonIssue::TooLarge);
    oversized.resize(kMaxConfigJsonBytes);
    QIQI_TEST_CHECK(ParseConfigJson(oversized, baseline, result, error) == S_OK);
    baseline.page_size = 0;
    json = "unchanged";
    QIQI_TEST_CHECK(FAILED(SerializeConfigJson(baseline, json, error)));
    QIQI_TEST_CHECK(json == "unchanged" && error.field == "settings.page_size");
}

void TestConfigFieldProjections() {
    constexpr std::array<std::pair<std::string_view, std::wstring_view>, 39> expected_keys{{
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
        {"ui_text_scale", L"UiTextScale"}, {"easy_reading", L"EasyReading"},
        {"enable_code_completion", L"EnableCodeCompletion"}, {"ime_skin_id", L"ImeSkinId"},
        {"candidate_font_size", L"CandidateFontSize"},
        {"candidate_font_weight", L"CandidateFontWeight"},
        {"candidate_font_italic", L"CandidateFontItalic"}, {"space_enter_mode", L"SpaceEnterMode"},
        {"single_character_mode", L"SingleCharacterMode"}, {"full_code_practice", L"FullCodePractice"},
        {"phrase_char_selection", L"PhraseCharSelection"}, {"enable_z_pinyin_lookup", L"EnableZPinyinLookup"},
        {"caps_lock_toggle_mode", L"CapsLockToggleMode"}, {"output_conversion", L"OutputConversion"}
    }};
    constexpr std::array<std::string_view, 35> editable_keys{
        "primary_dict", "enable_jidian_extra", "page_size", "candidate_layout", "auto_commit_4code",
        "push_commit_5th", "semicolon_quote_quick_select", "show_code_hint", "dynamic_phrase_freq",
        "shift_toggle_mode", "commit_on_english_switch", "show_status_bar", "beep_on_empty_4code",
        "beep_on_dup_4code", "clear_on_empty_4code", "enable_key_sound", "up_down_page_turn",
        "ui_language", "underline_style", "bold_underline", "ui_theme", "ui_text_scale", "easy_reading",
        "enable_code_completion", "ime_skin_id", "candidate_font_size",
        "candidate_font_weight", "candidate_font_italic", "space_enter_mode", "single_character_mode",
        "full_code_practice", "phrase_char_selection", "enable_z_pinyin_lookup", "caps_lock_toggle_mode",
        "output_conversion"
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
        } else if constexpr (std::is_same_v<Value, ImeSkinId>) {
            QIQI_TEST_CHECK(loaded.*(field.member) == ImeSkinId::Invalid);
            QiQiConfig unchanged;
            QIQI_TEST_CHECK(!field.Assign(unchanged, invalid));
            QIQI_TEST_CHECK(field.Value(unchanged) == field.Value(defaults));
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
    ForEachConfigStringField([&](const auto& field) {
        QIQI_TEST_CHECK(field.backup_key == "candidate_font_family" &&
            field.registry_key == std::wstring_view(L"CandidateFontFamily") &&
            field.settings_editable);
        QiQiConfig configured;
        configured.candidate_font_family = L"Microsoft YaHei";
        QIQI_TEST_CHECK(SaveConfigToRegistry(configured, subkey.c_str()));
        QiQiConfig loaded;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded, nullptr, subkey.c_str()));
        QIQI_TEST_CHECK(loaded.*(field.member) == configured.*(field.member));
        auto merged = configured;
        const QiQiConfig baseline;
        MergeSettingsConfig(merged, baseline, configured);
        QIQI_TEST_CHECK(merged.*(field.member) == configured.*(field.member));
    });
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
    QIQI_TEST_CHECK(SaveConfigToRegistryResult(cfg, subkey.c_str()) == S_OK);
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
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, kTestRegSubKey, 0, KEY_READ, key.Put()) ==
            ERROR_FILE_NOT_FOUND);
        QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, kTestRegSubKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, key.Put(), nullptr) == ERROR_SUCCESS);
        const auto read_security = [&]() {
            DWORD size = 0;
            QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, nullptr, &size) ==
                ERROR_INSUFFICIENT_BUFFER);
            std::vector<BYTE> security(size);
            QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, security.data(), &size) ==
                ERROR_SUCCESS);
            security.resize(size);
            return security;
        };
        const auto security = read_security();
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == 0);
        QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out));
        QIQI_TEST_CHECK(read_security() == security);
    }
    QIQI_TEST_CHECK(!SaveConfigToRegistry(reg_out, L""));
    QIQI_TEST_CHECK(SaveConfigToRegistryResult(reg_out, L"") == E_INVALIDARG);
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
