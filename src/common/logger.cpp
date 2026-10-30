#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <gsl/gsl>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <array>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace qiqi::log {

namespace {

// Static union wrapper whose destructor is a no-op so CRT process-exit under the Windows Loader Lock
// never destroys the mutex or flushes file handles on ExitProcess, without calling raw new/delete.
struct LoggerState {
    std::mutex mutex;
    std::shared_ptr<spdlog::logger> logger;
};

union NoDestroyLoggerState {
    LoggerState value;
    NoDestroyLoggerState() noexcept : value{} {}
    ~NoDestroyLoggerState() noexcept {}

    NoDestroyLoggerState(const NoDestroyLoggerState&) = delete;
    NoDestroyLoggerState& operator=(const NoDestroyLoggerState&) = delete;
    NoDestroyLoggerState(NoDestroyLoggerState&&) = delete;
    NoDestroyLoggerState& operator=(NoDestroyLoggerState&&) = delete;
};

LoggerState& GetState() noexcept {
    static NoDestroyLoggerState holder;
    return holder.value;
}

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
        // check_debugger_present = true avoids expensive OutputDebugStringW kernel exceptions
        // during normal typing when no debugger is attached to the host application.
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>(true));
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

    std::string proc_name = WideToUtf8(GetModuleFilePath(nullptr).filename().wstring());
    if (proc_name.empty()) {
        proc_name = "qiqiime";
    }

    auto logger = std::make_shared<spdlog::logger>(proc_name, sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [pid:%P] [tid:%t] [%l] %v");
    logger->flush_on(spdlog::level::warn);
    return logger;
}

} // namespace

void InitLogger(const std::filesystem::path& log_file_path, spdlog::level::level_enum level) {
    auto& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.logger = CreateLoggerInternal(log_file_path, level);
}

const std::shared_ptr<spdlog::logger>& GetLogger() {
    auto& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.logger) {
        state.logger = CreateLoggerInternal({}, spdlog::level::info);
    }
    return state.logger;
}

void SetLevel(spdlog::level::level_enum level) {
    GetLogger()->set_level(level);
}

void Flush() {
    GetLogger()->flush();
}

void Shutdown() noexcept {
    try {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.logger) {
            state.logger->flush();
            state.logger.reset();
        }
    } catch (...) {
    }
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
    // Open with least privilege: read-only access, read-only sharing, anonymous SQOS to prevent pipe impersonation,
    // and verify the target is a regular disk file.
    const ScopedFileHandle file(::CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
        nullptr));
    if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
        return false;
    }

    LARGE_INTEGER file_size{};
    constexpr LONGLONG kMaxAllowedReadBytes = 64LL * 1024LL * 1024LL; // 64 MB safety ceiling
    if (!::GetFileSizeEx(file.Get(), &file_size) ||
        file_size.QuadPart < 0 ||
        file_size.QuadPart > kMaxAllowedReadBytes) {
        return false;
    }

    out_content.clear();
    if (file_size.QuadPart == 0) {
        return true;
    }

    out_content.resize(gsl::narrow_cast<size_t>(file_size.QuadPart));
    DWORD total_read = 0;
    const DWORD to_read = gsl::narrow_cast<DWORD>(file_size.QuadPart);
    if (!::ReadFile(file.Get(), out_content.data(), to_read, &total_read, nullptr) ||
        total_read != to_read) {
        out_content.clear();
        return false;
    }
    return true;
}

bool WriteFileAtomically(const std::filesystem::path& target_path, std::span<const uint8_t> bytes) {
    try {
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            return false;
        }
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path(), ec);
        }

        const std::filesystem::path tmp_path =
            target_path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId());
        bool moved_ok = false;
        const auto cleanup_tmp = gsl::finally([&tmp_path, &moved_ok]() noexcept {
            if (!moved_ok) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
            }
        });

        {
            const ScopedFileHandle file(::CreateFileW(
                tmp_path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                nullptr));
            if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                return false;
            }

            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(bytes.size());
            if (to_write > 0 &&
                (!::WriteFile(file.Get(), bytes.data(), to_write, &written, nullptr) || written != to_write)) {
                return false;
            }
        }

        moved_ok = (::MoveFileExW(tmp_path.c_str(), target_path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE);
        return moved_ok;
    } catch (...) {
        return false;
    }
}

} // namespace qiqi::log
