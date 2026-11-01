#include "common/logger.h"
#include "common/etw_registration.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <gsl/gsl>

#include <atomic>
#include <array>
#include <cstdio>
#include <cwchar>
#include <format>

// Windows ETW TraceLogging Provider for QiQiIME: {8E7B9D31-5A42-4C81-9F11-202610290099}
// Works inside UWP/AppContainer/Low-Integrity sandboxes without filesystem permissions, with low idle overhead.
#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_etw_provider,
    "QiQiIME.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99));
#pragma warning(pop)

namespace qiqi::log {

namespace {

std::atomic<Level> g_log_level{Level::info};

ULONG RegisterProvider() noexcept {
    const ULONG error = ::TraceLoggingRegister(g_qiqi_etw_provider);
    if (error != ERROR_SUCCESS) {
        std::array<char, 128> message{};
        std::snprintf(message.data(), message.size(),
                      "QiQiIME: TraceLoggingRegister failed (error=%lu)\n", error);
        ::OutputDebugStringA(message.data());
    }
    return error;
}

void UnregisterProvider() noexcept { ::TraceLoggingUnregister(g_qiqi_etw_provider); }

constinit detail::EtwRegistration g_provider_registration{RegisterProvider, UnregisterProvider};

std::string_view GetProcessName() noexcept {
    static const std::string kProcName = []() noexcept -> std::string {
        try {
            std::array<wchar_t, MAX_PATH> path{};
            const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), gsl::narrow_cast<DWORD>(path.size()));
            if (length > 0 && length < path.size()) {
                const wchar_t* separator = std::wcsrchr(path.data(), L'\\');
                const wchar_t* name = separator ? separator + 1 : path.data();
                const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                if (bytes > 0) {
                    std::string result(gsl::narrow_cast<size_t>(bytes), '\0');
                    if (::WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), bytes, nullptr, nullptr) > 0) {
                        result.pop_back();
                        return result;
                    }
                }
            }
        } catch (...) {
            ReportLoggingFailure();
        }
        return "qiqiime";
    }();
    return kProcName;
}

constexpr std::string_view LevelTag(Level level) noexcept {
    switch (level) {
        case Level::trace:    return "trace";
        case Level::debug:    return "debug";
        case Level::info:     return "info";
        case Level::warn:     return "warning";
        case Level::err:      return "error";
        case Level::critical: return "critical";
        case Level::off:
        default:              return "off";
    }
}

#pragma warning(push)
#pragma warning(disable : 26447 26812)
void EmitEtwTraceEvent(Level level, std::string_view proc_name, std::string_view message) noexcept {
    const int msg_len = gsl::narrow_cast<int>(message.size());
    const int proc_len = gsl::narrow_cast<int>(proc_name.size());
    switch (level) {
        case Level::trace:
        case Level::debug:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogVerbose",
                TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::info:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogInfo",
                TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::warn:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogWarning",
                TraceLoggingLevel(WINEVENT_LEVEL_WARNING),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::err:
        case Level::critical:
            TraceLoggingWrite(
                g_qiqi_etw_provider,
                "QiQiLogError",
                TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::off:
        default:
            break;
    }
}
#pragma warning(pop)

} // namespace

void InitLogger(Level level) noexcept {
    g_log_level = level;
    std::ignore = EnsureInitialized();
}

bool EnsureInitialized() noexcept {
    return g_provider_registration.EnsureRegistered() == ERROR_SUCCESS;
}

Level GetLevel() noexcept {
    return g_log_level.load();
}

void SetLevel(Level level) noexcept {
    g_log_level = level;
}

bool CheckHResult(int32_t hr, std::string_view operation, Level level) noexcept {
    if (hr >= 0) {
        return true;
    }

    const DWORD error = ::GetLastError();
    Log(level, "{} failed (hr=0x{:08x})", operation, gsl::narrow_cast<unsigned int>(hr));
    ::SetLastError(error);
    return false;
}

void ReportLoggingFailure() noexcept {
    ::OutputDebugStringA("QiQiIME: logging failed with a formatting, allocation or encoding error\n");
}

bool IsEtwProviderRegistered() noexcept {
    return g_provider_registration.IsRegistered();
}

bool IsLogSinkActive() noexcept {
    return (::TraceLoggingProviderEnabled(g_qiqi_etw_provider, 0, 0) != FALSE) ||
           (::IsDebuggerPresent() != FALSE);
}

void WriteLogMessage(Level level, std::string_view message) noexcept {
    if (level < GetLevel() || level == Level::off) {
        return;
    }
    const std::string_view proc_name = GetProcessName();

    if (g_provider_registration.IsRegistered()) {
        EmitEtwTraceEvent(level, proc_name, message);
    }

    // 2. Output to debugger only when attached (avoids kernel exception overhead in normal typing)
    if (::IsDebuggerPresent() != FALSE) {
        try {
            const std::string line = std::format("[{}] [{}] {}\r\n", proc_name, LevelTag(level), message);
            const int chars = ::MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
            if (chars > 0) {
                std::wstring wide_line(gsl::narrow_cast<size_t>(chars), L'\0');
                if (::MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide_line.data(), chars) > 0) {
                    ::OutputDebugStringW(wide_line.c_str());
                } else {
                    ReportLoggingFailure();
                }
            } else {
                ReportLoggingFailure();
            }
        } catch (...) {
            ReportLoggingFailure();
        }
    }
}

void Shutdown() noexcept {
    g_provider_registration.Shutdown();
}

} // namespace qiqi::log
