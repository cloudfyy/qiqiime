#include "common/logger.h"

#include <windows.h>
#include <gsl/gsl>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

namespace qiqi::log {

namespace {

std::mutex g_logger_mutex;
std::shared_ptr<spdlog::logger> g_logger;

std::filesystem::path GetDefaultLogFilePath() {
    const std::filesystem::path app_dir = GetAppDataDirectory();
    if (app_dir.empty()) {
        return L"qiqiime.log";
    }
    return app_dir / L"logs" / L"qiqiime.log";
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
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(gsl::narrow_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(gsl::narrow_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), result.data(), len);
    return result;
}

std::filesystem::path GetModuleFilePath(void* module_handle) noexcept {
    try {
        std::array<wchar_t, MAX_PATH> module_path{};
        if (::GetModuleFileNameW(
                static_cast<HMODULE>(module_handle),
                module_path.data(),
                gsl::narrow_cast<DWORD>(module_path.size())) == 0) {
            return {};
        }
        return std::filesystem::path(module_path.data());
    } catch (...) {
        return {};
    }
}

std::filesystem::path GetAppDataDirectory() {
    std::array<wchar_t, MAX_PATH> env_buf{};
    const DWORD env_len = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", env_buf.data(), gsl::narrow_cast<DWORD>(env_buf.size()));
    std::filesystem::path base_dir;
    if (env_len > 0 && env_len < env_buf.size()) {
        base_dir = env_buf.data();
    } else {
        std::array<wchar_t, MAX_PATH> temp_path{};
        if (::GetTempPathW(gsl::narrow_cast<DWORD>(temp_path.size()), temp_path.data()) > 0) {
            base_dir = temp_path.data();
        }
    }
    if (base_dir.empty()) {
        return {};
    }
    return base_dir / L"QiQiIME";
}

bool ReadFileToUtf8String(const std::filesystem::path& file_path, std::string& out_content) {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const auto file_size = file.tellg();
    out_content.clear();
    if (file_size > 0) {
        out_content.resize(gsl::narrow_cast<size_t>(file_size));
        file.seekg(0, std::ios::beg);
        file.read(out_content.data(), file_size);
    }
    return true;
}

} // namespace qiqi::log
