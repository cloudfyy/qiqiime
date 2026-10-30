#include "common/logger.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <gsl/gsl>

#include <atomic>
#include <format>

// Windows ETW TraceLogging Provider for QiQiIME: {8E7B9D31-5A42-4C81-9F11-202610290099}
// Works inside UWP/AppContainer/Low-Integrity sandboxes without filesystem permissions and has zero cost when not enabled.
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

// RAII guard for the Windows ETW TraceLogging provider registration.
class ScopedEtwProvider final {
public:
    ScopedEtwProvider() noexcept { EnsureRegistered(); }
    ~ScopedEtwProvider() noexcept { Unregister(); }

    ScopedEtwProvider(const ScopedEtwProvider&) = delete;
    ScopedEtwProvider& operator=(const ScopedEtwProvider&) = delete;
    ScopedEtwProvider(ScopedEtwProvider&&) = delete;
    ScopedEtwProvider& operator=(ScopedEtwProvider&&) = delete;

    void EnsureRegistered() noexcept {
        if (!registered_.exchange(true)) {
            if (FAILED(::TraceLoggingRegister(g_qiqi_etw_provider))) {
                registered_ = false;
            }
        }
    }

    void Unregister() noexcept {
        if (registered_.exchange(false)) {
            ::TraceLoggingUnregister(g_qiqi_etw_provider);
        }
    }

    bool IsRegistered() const noexcept { return registered_.load(); }

private:
    std::atomic<bool> registered_{false};
};

ScopedEtwProvider& GetEtwProvider() noexcept {
    static ScopedEtwProvider provider;
    return provider;
}

std::string_view GetProcessName() noexcept {
    static const std::string kProcName = []() noexcept -> std::string {
        try {
            std::string name = WideToUtf8(GetModuleFilePath(nullptr).filename().wstring());
            if (!name.empty()) {
                return name;
            }
        } catch (...) {
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
    GetEtwProvider().EnsureRegistered();
}

Level GetLevel() noexcept {
    return g_log_level.load();
}

void SetLevel(Level level) noexcept {
    g_log_level = level;
}

bool IsEtwProviderRegistered() noexcept {
    auto& provider = GetEtwProvider();
    provider.EnsureRegistered();
    return provider.IsRegistered();
}

bool IsLogSinkActive() noexcept {
    return (::TraceLoggingProviderEnabled(g_qiqi_etw_provider, 0, 0) != FALSE) ||
           (::IsDebuggerPresent() != FALSE);
}

void WriteLogMessage(Level level, std::string_view message) noexcept {
    if (level < GetLevel() || level == Level::off) {
        return;
    }
    auto& provider = GetEtwProvider();
    provider.EnsureRegistered();
    const std::string_view proc_name = GetProcessName();

    // 1. Emit to Windows ETW TraceLogging (100% lock-free kernel write, zero heap allocation)
    EmitEtwTraceEvent(level, proc_name, message);

    // 2. Output to debugger only when attached (avoids kernel exception overhead in normal typing)
    if (::IsDebuggerPresent() != FALSE) {
        try {
            const std::string line = std::format("[{}] [{}] {}\r\n", proc_name, LevelTag(level), message);
            const std::wstring wide_line = Utf8ToWide(line);
            ::OutputDebugStringW(wide_line.c_str());
        } catch (...) {
        }
    }
}

void Shutdown() noexcept {
    GetEtwProvider().Unregister();
}

} // namespace qiqi::log
