#include "common.h"
#include "common/logger.h"
#include "common/version.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qiqi::test {

namespace {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 2, 0});
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

    // 2. Test WideToUtf8 with empty and non-empty strings
    QIQI_TEST_CHECK(log::WideToUtf8(L"").empty());
    QIQI_TEST_CHECK(log::WideToUtf8(L"五笔ABC") == "五笔ABC");

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

} // namespace

void RunCommonTests() {
    TestSpdlogLogger();
    TestVersionMechanism();
}

} // namespace qiqi::test
