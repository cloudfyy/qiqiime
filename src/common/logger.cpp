#include "common/logger.h"

#include <windows.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <cstdlib>
#include <mutex>
#include <vector>

namespace qiqi::log {

namespace {

std::mutex g_logger_mutex;
std::shared_ptr<spdlog::logger> g_logger;

std::filesystem::path GetDefaultLogFilePath() {
    wchar_t* local_app_data = nullptr;
    size_t len = 0;
    std::filesystem::path base_dir;
    if (_wdupenv_s(&local_app_data, &len, L"LOCALAPPDATA") == 0 && local_app_data) {
        base_dir = local_app_data;
        free(local_app_data);
    } else {
        wchar_t temp_path[MAX_PATH]{};
        if (::GetTempPathW(MAX_PATH, temp_path) > 0) {
            base_dir = temp_path;
        }
    }

    if (base_dir.empty()) {
        return L"qiqiime.log";
    }
    return base_dir / L"QiQiIME" / L"logs" / L"qiqiime.log";
}

std::shared_ptr<spdlog::logger> CreateLoggerInternal(
    const std::filesystem::path& custom_path,
    spdlog::level::level_enum level) {
    std::vector<spdlog::sink_ptr> sinks;

    try {
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    } catch (...) {
    }

    try {
        const std::filesystem::path log_path =
            custom_path.empty() ? GetDefaultLogFilePath() : custom_path;
        if (log_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(log_path.parent_path(), ec);
        }
        constexpr size_t kMaxFileSize = 5 * 1024 * 1024; // 5 MB
        constexpr size_t kMaxFiles = 3;
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path.wstring(), kMaxFileSize, kMaxFiles));
    } catch (...) {
        // Ignore file sink failure (e.g. restricted sandbox/AppContainer process)
    }

    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>("qiqiime", sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [pid:%P] [tid:%t] [%l] %v");
    logger->flush_on(spdlog::level::warn);
    return logger;
}

} // namespace

void InitLogger(const std::filesystem::path& log_file_path, spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_logger = CreateLoggerInternal(log_file_path, level);
}

const std::shared_ptr<spdlog::logger>& GetLogger() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (!g_logger) {
        g_logger = CreateLoggerInternal({}, spdlog::level::info);
    }
    return g_logger;
}

void SetLevel(spdlog::level::level_enum level) {
    GetLogger()->set_level(level);
}

void Flush() {
    GetLogger()->flush();
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

} // namespace qiqi::log
