#include "common.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/version.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qiqi::test {

namespace {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 4, 0});
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
        "shift_toggle_mode = false\n"
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
    QIQI_TEST_CHECK(!cfg.default_chinese_mode);
    QIQI_TEST_CHECK(!cfg.shift_toggle_mode);
    QIQI_TEST_CHECK(cfg.log_level == spdlog::level::debug);

    // 2. Test alternative enum/boolean values
    const QiQiConfig cfg2 = ParseConfigFromUtf8(
        "[dictionary]\nprimary = builtin\nenable_jidian_extra = yes\n"
        "[log]\nlevel = trace\n");
    QIQI_TEST_CHECK(cfg2.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(cfg2.enable_jidian_extra);
    QIQI_TEST_CHECK(cfg2.log_level == spdlog::level::trace);

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
    QIQI_TEST_CHECK(loaded_default.default_chinese_mode);
    QIQI_TEST_CHECK(loaded_default.shift_toggle_mode);
    QIQI_TEST_CHECK(loaded_default.log_level == spdlog::level::info);

    std::filesystem::remove(temp_cfg_path, ec);

    // 4. Test LoadConfig discovery
    const QiQiConfig discovered = LoadConfig(nullptr);
    QIQI_TEST_CHECK(discovered.page_size >= 1 && discovered.page_size <= 9);
}

} // namespace

void RunCommonTests() {
    TestSpdlogLogger();
    TestVersionMechanism();
    TestIniConfiguration();
}

} // namespace qiqi::test
