#include "common/logger.h"
#include "common/etw_registration.h"
#include "common/etw_write.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"

#include <windows.h>

namespace {
ULONG WINAPI QiQiWriteEtwEvent(
    REGHANDLE provider, PCEVENT_DESCRIPTOR descriptor, LPCGUID activity, LPCGUID related,
    ULONG count, PEVENT_DATA_DESCRIPTOR data) noexcept {
    return qiqi::log::detail::WriteEtwEvent(
        provider, descriptor, activity, related, count, data, ::EventWriteTransfer, ::OutputDebugStringA);
}
} // namespace

// The SDK hook observes actual write errors without recursively writing another ETW event.
#define TLG_EVENT_WRITE_TRANSFER QiQiWriteEtwEvent
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
                const std::wstring_view full_path(path.data(), length);
                const size_t sep = full_path.find_last_of(L'\\');
                const std::wstring_view file_view =
                    (sep == std::wstring_view::npos) ? full_path : full_path.substr(sep + 1);
                const int bytes = ::WideCharToMultiByte(
                    CP_UTF8, 0, file_view.data(), gsl::narrow_cast<int>(file_view.size()), nullptr, 0, nullptr, nullptr);
                if (bytes > 0) {
                    std::string result(gsl::narrow_cast<size_t>(bytes), '\0');
                    if (::WideCharToMultiByte(
                            CP_UTF8, 0, file_view.data(), gsl::narrow_cast<int>(file_view.size()),
                            result.data(), bytes, nullptr, nullptr) > 0) {
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

constexpr UCHAR ToEtwLevel(Level level) noexcept {
    switch (level) {
        case Level::trace:
        case Level::debug:    return WINEVENT_LEVEL_VERBOSE;
        case Level::info:     return WINEVENT_LEVEL_INFO;
        case Level::warn:     return WINEVENT_LEVEL_WARNING;
        case Level::err:
        case Level::critical: return WINEVENT_LEVEL_ERROR;
        case Level::off:
        default:              return 0;
    }
}

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
bool IsEtwEnabledForLevel(Level level, Keyword keyword) noexcept {
    const UCHAR etw_level = ToEtwLevel(level);
    const Keyword norm_kw = NormalizeKeyword(keyword);
    return etw_level != 0 &&
           g_provider_registration.IsRegistered() &&
           (::TraceLoggingProviderEnabled(
                g_qiqi_etw_provider, etw_level, std::to_underlying(norm_kw)) != FALSE);
}

bool IsDebuggerEnabledForLevel(Level level, Level configured_level) noexcept {
    return level != Level::off &&
           configured_level != Level::off &&
           level >= configured_level &&
           (::IsDebuggerPresent() != FALSE);
}

template <Keyword Kw, Opcode Op>
void EmitEtwWithMetadata(
    Level level,
    std::string_view proc_name,
    std::string_view message,
    const GUID* activity_id,
    const GUID* related_activity_id) noexcept {
    constexpr ULONGLONG kKeywordMask = std::to_underlying(Kw);
    constexpr UCHAR kOpcodeVal = std::to_underlying(Op);
    const int msg_len = gsl::narrow_cast<int>(message.size());
    const int proc_len = gsl::narrow_cast<int>(proc_name.size());
    const GUID* const effective_activity = (activity_id != nullptr) ? activity_id : &GUID_NULL;
    const GUID* const effective_related =
        (related_activity_id != nullptr && *related_activity_id != GUID_NULL) ? related_activity_id : nullptr;

    switch (level) {
        case Level::trace:
        case Level::debug:
            TraceLoggingWriteActivity(
                g_qiqi_etw_provider,
                "QiQiLogVerbose",
                effective_activity,
                effective_related,
                TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
                TraceLoggingKeyword(kKeywordMask),
                TraceLoggingOpcode(kOpcodeVal),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::info:
            TraceLoggingWriteActivity(
                g_qiqi_etw_provider,
                "QiQiLogInfo",
                effective_activity,
                effective_related,
                TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                TraceLoggingKeyword(kKeywordMask),
                TraceLoggingOpcode(kOpcodeVal),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::warn:
            TraceLoggingWriteActivity(
                g_qiqi_etw_provider,
                "QiQiLogWarning",
                effective_activity,
                effective_related,
                TraceLoggingLevel(WINEVENT_LEVEL_WARNING),
                TraceLoggingKeyword(kKeywordMask),
                TraceLoggingOpcode(kOpcodeVal),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::err:
        case Level::critical:
            TraceLoggingWriteActivity(
                g_qiqi_etw_provider,
                "QiQiLogError",
                effective_activity,
                effective_related,
                TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
                TraceLoggingKeyword(kKeywordMask),
                TraceLoggingOpcode(kOpcodeVal),
                TraceLoggingCountedString(proc_name.data(), proc_len, "process"),
                TraceLoggingCountedString(message.data(), msg_len, "message"));
            break;
        case Level::off:
        default:
            break;
    }
}

template <Keyword Kw>
void EmitEtwForKeyword(
    Level level,
    Opcode opcode,
    std::string_view proc_name,
    std::string_view message,
    const GUID* activity_id,
    const GUID* related_activity_id) noexcept {
    switch (NormalizeOpcode(opcode)) {
        case Opcode::Start:
            EmitEtwWithMetadata<Kw, Opcode::Start>(level, proc_name, message, activity_id, related_activity_id);
            break;
        case Opcode::Stop:
            EmitEtwWithMetadata<Kw, Opcode::Stop>(level, proc_name, message, activity_id, related_activity_id);
            break;
        case Opcode::Info:
        default:
            EmitEtwWithMetadata<Kw, Opcode::Info>(level, proc_name, message, activity_id, related_activity_id);
            break;
    }
}

void EmitEtwTraceEvent(
    Level level,
    std::string_view proc_name,
    std::string_view message,
    const LogContext& context) noexcept {
    switch (NormalizeKeyword(context.keyword)) {
        case Keyword::Lifecycle:
            EmitEtwForKeyword<Keyword::Lifecycle>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::Input:
            EmitEtwForKeyword<Keyword::Input>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::Composition:
            EmitEtwForKeyword<Keyword::Composition>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::Dictionary:
            EmitEtwForKeyword<Keyword::Dictionary>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::Sync:
            EmitEtwForKeyword<Keyword::Sync>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::UI:
            EmitEtwForKeyword<Keyword::UI>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::Config:
            EmitEtwForKeyword<Keyword::Config>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
        case Keyword::General:
        default:
            EmitEtwForKeyword<Keyword::General>(
                level, context.opcode, proc_name, message, context.activity_id, context.related_activity_id);
            break;
    }
}
#pragma warning(pop)

} // namespace

GUID CreateActivityId() noexcept {
    const DWORD saved_error = ::GetLastError();
    GUID id = GUID_NULL;
    const ULONG status = ::EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_ID, &id);
    ::SetLastError(saved_error);
    if (status != ERROR_SUCCESS) {
        std::array<char, 128> message{};
        std::snprintf(message.data(), message.size(),
                      "QiQiIME: EventActivityIdControl(CREATE_ID) failed (error=%lu)\n", status);
        ::OutputDebugStringA(message.data());
        return GUID_NULL;
    }
    return id;
}

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

bool CheckHResult(int32_t hr, std::string_view operation, Level level, const LogContext& context) noexcept {
    if (hr >= 0) {
        return true;
    }

    const DWORD error = ::GetLastError();
    LogWithContext(level, context, "{} failed (hr=0x{:08x})", operation, gsl::narrow_cast<unsigned int>(hr));
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
    return (g_provider_registration.IsRegistered() &&
            (::TraceLoggingProviderEnabled(g_qiqi_etw_provider, 0, 0) != FALSE)) ||
           (::IsDebuggerPresent() != FALSE);
}

bool ShouldLog(Level level, Keyword keyword) noexcept {
    const Level configured = GetLevel();
    if (level == Level::off || configured == Level::off) {
        return false;
    }
    return IsEtwEnabledForLevel(level, keyword) || IsDebuggerEnabledForLevel(level, configured);
}

void WriteLogMessage(Level level, std::string_view message, const LogContext& context) noexcept {
    const Level configured = GetLevel();
    if (level == Level::off || configured == Level::off) {
        return;
    }
    const bool emit_etw = IsEtwEnabledForLevel(level, context.keyword);
    const bool emit_debugger = IsDebuggerEnabledForLevel(level, configured);
    if (!emit_etw && !emit_debugger) {
        return;
    }
    const std::string_view proc_name = GetProcessName();

    if (emit_etw) {
        EmitEtwTraceEvent(level, proc_name, message, context);
    }

    // 2. Output to debugger only when attached and within configured level (avoids kernel exception overhead in normal typing)
    if (emit_debugger) {
        try {
            const std::string line = std::format(
                "[{}] [{}] [{}:{}] {}\r\n",
                proc_name,
                LevelTag(level),
                KeywordTag(context.keyword),
                OpcodeTag(context.opcode),
                message);
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
