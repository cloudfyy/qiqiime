#include "common.h"
#include "common/config.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/sound.h"
#include "common/version.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qiqi::test {

namespace {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 5, 0});
    static_assert(version::SemanticVersion{1, 0, 0} > version::kCurrentVersion);
    QIQI_TEST_CHECK(!version::kVersion.empty());
    QIQI_TEST_CHECK(!version::kVersionW.empty());
    QIQI_TEST_CHECK(!version::kFullVersion.empty());
    QIQI_TEST_CHECK(!version::kFullVersionW.empty());
    QIQI_TEST_CHECK(!version::kGitCommit.empty());
    QIQI_TEST_CHECK(!version::kGitCommitW.empty());
    QIQI_TEST_CHECK(version::kArch == (sizeof(void*) == 8 ? "x64" : "x86"));
    QIQI_TEST_CHECK(version::kArchW == (sizeof(void*) == 8 ? L"x64" : L"x86"));
    QIQI_TEST_CHECK(version::kFullVersion.starts_with(version::kVersion));
}

void TestSpdlogLogger() {
    // 1. Test lazy initialization of GetLogger() before InitLogger() is ever called
    auto default_logger = log::GetLogger();
    QIQI_TEST_CHECK(default_logger != nullptr);
    QIQI_LOG_DEBUG("Lazy default logger test");

    // 2. Test WideToUtf8, Utf8ToWide, and GetModuleFilePath
    QIQI_TEST_CHECK(log::WideToUtf8(L"").empty());
    QIQI_TEST_CHECK(log::WideToUtf8(L"五笔ABC") == "五笔ABC");
    QIQI_TEST_CHECK(log::Utf8ToWide("").empty());
    QIQI_TEST_CHECK(log::Utf8ToWide("五笔ABC") == L"五笔ABC");
    QIQI_TEST_CHECK(!log::GetModuleFilePath(nullptr).empty());
    QIQI_TEST_CHECK(log::GetModuleFilePath(reinterpret_cast<void*>(0x12345678)).empty());

    // 3. Test custom file logger, SetLevel, and Flush
    const auto temp_log = std::filesystem::temp_directory_path() / L"qiqiime_unit_test.log";
    std::error_code ec;
    std::filesystem::remove(temp_log, ec);

    log::InitLogger(temp_log, spdlog::level::info);
    log::SetLevel(spdlog::level::trace);
    QIQI_LOG_TRACE("Trace message");
    QIQI_LOG_DEBUG("Debug message");
    QIQI_LOG_INFO("Unit test log entry: {}", log::WideToUtf8(L"五笔日志测试"));
    QIQI_LOG_WARN("Warn message");
    QIQI_LOG_ERROR("Error message");
    log::Flush();

    QIQI_TEST_CHECK(std::filesystem::exists(temp_log));
    {
        std::ifstream ifs(temp_log);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        QIQI_TEST_CHECK(content.find("Unit test log entry:") != std::string::npos);
        QIQI_TEST_CHECK(content.find("Trace message") != std::string::npos);
        QIQI_TEST_CHECK(content.find("[qiqi_engine_test.exe]") != std::string::npos);
    }

    // Re-initialize default logger to release temp_log handle
    log::InitLogger();
    std::filesystem::remove(temp_log, ec);

    // 4. Test logger with relative filename (no parent_path)
    const std::filesystem::path rel_log = L"qiqiime_rel_test.log";
    std::filesystem::remove(rel_log, ec);
    log::InitLogger(rel_log, spdlog::level::info);
    QIQI_LOG_INFO("Relative path log test");
    log::Flush();

    // 5. Test fallback when file sink fails to open (passing a directory path as file path)
    log::InitLogger(std::filesystem::temp_directory_path(), spdlog::level::info);
    QIQI_LOG_INFO("Fallback msvc sink only test");
    std::filesystem::remove(rel_log, ec);

    // 6. Test GetDefaultLogFilePath fallback when LOCALAPPDATA is unset
    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    _wputenv_s(L"LOCALAPPDATA", L"");
    log::InitLogger({}, spdlog::level::info);
    if (orig_localappdata) {
        _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
        free(orig_localappdata);
    }
    log::InitLogger();
}

void TestIniConfiguration() {
    // 1. Test ParseConfigFromUtf8 with BOM, comments, inline comments, and all sections
    constexpr std::string_view kIniContent =
        "\xEF\xBB\xBF# Header comment\n"
        "; Semicolon comment\n"
        "[dictionary]\n"
        "primary = haifeng # inline comment\n"
        "enable_jidian_extra = false\n"
        "user_dict = custom_words.txt\n"
        "\n"
        "[engine]\n"
        "page_size = 7\n"
        "auto_commit_4code = 0\n"
        "push_commit_5th = no\n"
        "default_chinese_mode = off\n"
        "default_full_width = true\n"
        "default_chinese_punct = false\n"
        "shift_toggle_mode = false\n"
        "semicolon_quote_quick_select = false\n"
        "show_code_hint = false\n"
        "enable_z_temp_english = false\n"
        "dynamic_phrase_freq = true\n"
        "clear_on_empty_4code = true\n"
        "\n"
        "[log]\n"
        "level = debug\n";

    const QiQiConfig cfg = ParseConfigFromUtf8(kIniContent);
    QIQI_TEST_CHECK(cfg.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!cfg.enable_jidian_extra);
    QIQI_TEST_CHECK(cfg.user_dict_path == L"custom_words.txt");
    QIQI_TEST_CHECK(cfg.page_size == 7);
    QIQI_TEST_CHECK(!cfg.auto_commit_4code);
    QIQI_TEST_CHECK(!cfg.push_commit_5th);
    QIQI_TEST_CHECK(!cfg.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!cfg.show_code_hint);
    QIQI_TEST_CHECK(!cfg.enable_z_temp_english);
    QIQI_TEST_CHECK(cfg.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!cfg.default_chinese_mode);
    QIQI_TEST_CHECK(cfg.default_full_width);
    QIQI_TEST_CHECK(!cfg.default_chinese_punct);
    QIQI_TEST_CHECK(!cfg.shift_toggle_mode);
    QIQI_TEST_CHECK(cfg.clear_on_empty_4code);
    QIQI_TEST_CHECK(cfg.log_level == spdlog::level::debug);

    // Test SaveConfigFile & SerializeConfigToUtf8 round-trip
    const auto roundtrip_path = std::filesystem::temp_directory_path() / L"qiqiime_roundtrip.ini";
    std::error_code rt_ec;
    QIQI_TEST_CHECK(SaveConfigFile(roundtrip_path, cfg));
    QiQiConfig roundtrip_cfg{};
    QIQI_TEST_CHECK(LoadConfigFile(roundtrip_path, roundtrip_cfg));
    QIQI_TEST_CHECK(roundtrip_cfg.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(roundtrip_cfg.default_full_width);
    QIQI_TEST_CHECK(!roundtrip_cfg.default_chinese_punct);
    QIQI_TEST_CHECK(!roundtrip_cfg.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(roundtrip_cfg.clear_on_empty_4code);
    std::filesystem::remove(roundtrip_path, rt_ec);

    // 2. Test alternative enum/boolean values, uppercase keys, malformed lines, and all log levels
    const QiQiConfig cfg2 = ParseConfigFromUtf8(
        "[DICTIONARY]\n"
        "PRIMARY = builtin\n"
        "ENABLE_JIDIAN_EXTRA = yes\n"
        "malformed_line_without_equals\n"
        "[ENGINE]\n"
        "AUTO_COMMIT_4CODE = on\n"
        "PUSH_COMMIT_5TH = invalid_bool\n"
        "BEEP_ON_EMPTY_4CODE = false\n"
        "BEEP_ON_DUP_4CODE = true\n"
        "CLEAR_ON_EMPTY_4CODE = true\n"
        "ENABLE_KEY_SOUND = true\n"
        "UI_LANGUAGE = en-US\n"
        "[LOG]\n"
        "LEVEL = trace\n");
    QIQI_TEST_CHECK(cfg2.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(cfg2.enable_jidian_extra);
    QIQI_TEST_CHECK(cfg2.auto_commit_4code);
    QIQI_TEST_CHECK(cfg2.push_commit_5th);
    QIQI_TEST_CHECK(!cfg2.beep_on_empty_4code);
    QIQI_TEST_CHECK(cfg2.beep_on_dup_4code);
    QIQI_TEST_CHECK(cfg2.clear_on_empty_4code);
    QIQI_TEST_CHECK(cfg2.enable_key_sound);
    QIQI_TEST_CHECK(cfg2.ui_language == UiLanguage::EnUs);
    QIQI_TEST_CHECK(cfg2.log_level == spdlog::level::trace);

    const QiQiConfig cfg_zh = ParseConfigFromUtf8("[engine]\nlanguage = zh-CN\n");
    QIQI_TEST_CHECK(cfg_zh.ui_language == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(!SerializeConfigToUtf8(cfg_zh).empty());
    const QiQiConfig cfg_auto = ParseConfigFromUtf8("[engine]\nui_language = unknown_lang\n");
    QIQI_TEST_CHECK(cfg_auto.ui_language == UiLanguage::Auto);

    for (const auto& [lvl_str, lvl_enum] : {
             std::pair{"info", spdlog::level::info},
             std::pair{"warn", spdlog::level::warn},
             std::pair{"error", spdlog::level::err},
             std::pair{"critical", spdlog::level::critical},
             std::pair{"off", spdlog::level::off},
             std::pair{"unknown_lvl", spdlog::level::info}}) {
        QiQiConfig lcfg = ParseConfigFromUtf8(std::string("[log]\nlevel = ") + lvl_str + "\n[dictionary]\nprimary = unknown_dict\n");
        QIQI_TEST_CHECK(lcfg.log_level == lvl_enum);
        QIQI_TEST_CHECK(lcfg.primary_dict == PrimaryDict::Jidian);
        const std::string serialized = SerializeConfigToUtf8(lcfg);
        QIQI_TEST_CHECK(!serialized.empty());
    }
    QIQI_TEST_CHECK(!SerializeConfigToUtf8(cfg2).empty());

    // SaveConfigFile failure when target is a directory
    QIQI_TEST_CHECK(!SaveConfigFile(std::filesystem::temp_directory_path(), cfg));

    // 3. Test EnsureDefaultConfigFile and LoadConfigFile
    const auto temp_cfg_path = std::filesystem::temp_directory_path() / L"qiqiime_test_config.ini";
    std::error_code ec;
    std::filesystem::remove(temp_cfg_path, ec);

    QIQI_TEST_CHECK(!LoadConfigFile("non_existent_config_99999.ini", const_cast<QiQiConfig&>(cfg)));
    QIQI_TEST_CHECK(EnsureDefaultConfigFile(temp_cfg_path));
    // Calling EnsureDefaultConfigFile again when file exists returns true without overwriting
    QIQI_TEST_CHECK(EnsureDefaultConfigFile(temp_cfg_path));

    QiQiConfig loaded_default{};
    QIQI_TEST_CHECK(LoadConfigFile(temp_cfg_path, loaded_default));
    QIQI_TEST_CHECK(loaded_default.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(loaded_default.enable_jidian_extra);
    QIQI_TEST_CHECK(loaded_default.page_size == 5);
    QIQI_TEST_CHECK(loaded_default.auto_commit_4code);
    QIQI_TEST_CHECK(loaded_default.push_commit_5th);
    QIQI_TEST_CHECK(!loaded_default.clear_on_empty_4code);
    QIQI_TEST_CHECK(loaded_default.default_chinese_mode);
    QIQI_TEST_CHECK(loaded_default.shift_toggle_mode);
    QIQI_TEST_CHECK(loaded_default.log_level == spdlog::level::info);

    std::filesystem::remove(temp_cfg_path, ec);

    // 4. Test LoadConfig discovery when user config does not exist yet (using temporary LOCALAPPDATA)
    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    const auto temp_appdata = std::filesystem::temp_directory_path() / L"QiQiIME_TestAppData";
    std::filesystem::remove_all(temp_appdata, ec);
    _wputenv_s(L"LOCALAPPDATA", temp_appdata.wstring().c_str());

    const QiQiConfig discovered = LoadConfig(nullptr);
    QIQI_TEST_CHECK(discovered.page_size >= 1 && discovered.page_size <= 9);
    QIQI_TEST_CHECK(std::filesystem::exists(GetDefaultUserConfigPath()));

    if (orig_localappdata) {
        _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
        free(orig_localappdata);
    }
    std::filesystem::remove_all(temp_appdata, ec);
}

void TestScopedHandleTemplate() {
    ScopedKernelHandle h1(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(h1.IsValid());
    QIQI_TEST_CHECK(static_cast<bool>(h1));

    // Move construction
    ScopedKernelHandle h2(std::move(h1));
    QIQI_TEST_CHECK(!h1.IsValid());
    QIQI_TEST_CHECK(h2.IsValid());

    // Move assignment
    ScopedKernelHandle h3;
    h3 = std::move(h2);
    QIQI_TEST_CHECK(!h2.IsValid());
    QIQI_TEST_CHECK(h3.IsValid());

    h3.Reset();
    QIQI_TEST_CHECK(!h3.IsValid());
}

void TestI18nLocalization() {
    // 1. Direct language resolution
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::ZhCn) == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::EnUs) == UiLanguage::EnUs);
    const UiLanguage auto_resolved = ResolveUiLanguage(UiLanguage::Auto);
    QIQI_TEST_CHECK(auto_resolved == UiLanguage::ZhCn || auto_resolved == UiLanguage::EnUs);

    // 2. Bilingual string tables verification
    const UiStrings& zh = GetUiStrings(UiLanguage::ZhCn);
    const UiStrings& en = GetUiStrings(UiLanguage::EnUs);
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_header_prefix) == L"五笔 [");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_header_prefix) == L"Wubi [");
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_empty) == L"(无候选)");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_empty) == L"(No candidates)");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_logo) == L"五笔");
    QIQI_TEST_CHECK(std::wstring_view(en.status_logo) == L"Wubi");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_mode_cn) == L"中");
    QIQI_TEST_CHECK(std::wstring_view(en.status_mode_cn) == L"CN");
    QIQI_TEST_CHECK(std::wstring_view(zh.dlg_btn_ok) == L"确定");
    QIQI_TEST_CHECK(std::wstring_view(en.dlg_btn_ok) == L"OK");

    // 3. Config INI aliases & round-trip serialization for all UiLanguage values
    for (const auto& [alias, expected] : {
             std::pair{"auto", UiLanguage::Auto},
             std::pair{"system", UiLanguage::Auto},
             std::pair{"default", UiLanguage::Auto},
             std::pair{"zh-CN", UiLanguage::ZhCn},
             std::pair{"zh_cn", UiLanguage::ZhCn},
             std::pair{"zh", UiLanguage::ZhCn},
             std::pair{"cn", UiLanguage::ZhCn},
             std::pair{"chinese", UiLanguage::ZhCn},
             std::pair{"en-US", UiLanguage::EnUs},
             std::pair{"en_us", UiLanguage::EnUs},
             std::pair{"en", UiLanguage::EnUs},
             std::pair{"english", UiLanguage::EnUs}}) {
        const QiQiConfig parsed = ParseConfigFromUtf8(std::string("[engine]\nui_language = ") + alias + "\n");
        QIQI_TEST_CHECK(parsed.ui_language == expected);
        const std::string serialized = SerializeConfigToUtf8(parsed);
        const QiQiConfig roundtrip = ParseConfigFromUtf8(serialized);
        QIQI_TEST_CHECK(roundtrip.ui_language == expected);
    }

    // 4. Test sound synthesizer and PlayImeSound (both live WAV synthesis and muted test mode)
    ResetLastPlayedSoundForTest();
    PlayImeSound(SoundEffectKind::None);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::None);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    PlayImeSound(SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    PlayImeSound(SoundEffectKind::DupCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);

    SetMuteAudioOutputForTest(true);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    SetMuteAudioOutputForTest(false);
}

} // namespace

void RunCommonTests() {
    TestSpdlogLogger();
    TestVersionMechanism();
    TestIniConfiguration();
    TestScopedHandleTemplate();
    TestI18nLocalization();
}

} // namespace qiqi::test
