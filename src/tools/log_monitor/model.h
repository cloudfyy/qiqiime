// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/logger.h"
#include <windows.h>
#include <cguid.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::monitor {

inline constexpr size_t kEventBufferDefaultRecords = 10'000;
inline constexpr size_t kEventBufferDefaultBytes = 16 * 1024 * 1024;
inline constexpr size_t kEventQueueDefaultRecords = 2'000;
inline constexpr size_t kEventQueueDefaultBytes = 4 * 1024 * 1024;

inline constexpr GUID kImeProvider = log::kProviderId;
inline constexpr GUID kMonitorProvider =
    {0x6ab897b4, 0xd148, 0x4df9, {0x8e, 0xb7, 0x47, 0x90, 0xa2, 0x74, 0x95, 0x1e}};

struct Event {
    uint64_t id = 0;
    uint64_t time = 0; // FILETIME units, UTC.
    DWORD pid = 0;
    DWORD tid = 0;
    UCHAR level = 0;
    UCHAR opcode = 0;
    uint64_t keyword = 1;
    GUID activity_id{};
    GUID related_activity_id{};
    std::wstring process;
    std::wstring name;
    std::wstring message;
    size_t Bytes() const noexcept;
};

struct Filter {
    std::wstring process;
    std::optional<DWORD> pid;
    UCHAR level = 0; // Zero means all; otherwise exact ETW level.
    uint64_t keyword = 0; // Zero means all; otherwise bitmask match.
    std::optional<GUID> activity_id; // Matches activity_id or related_activity_id.
    std::wstring text;
    bool Matches(const Event& event) const noexcept;
};

class EventBuffer final {
public:
#pragma warning(suppress : 26455) // Allocating bounded storage may throw; callers report startup failures.
    explicit EventBuffer(size_t max_events = kEventBufferDefaultRecords, size_t max_bytes = kEventBufferDefaultBytes);
    bool Push(Event event);
    void Clear() noexcept;
    const Event* Find(uint64_t id) const noexcept;
    const std::deque<Event>& Events() const noexcept { return events_; }
    size_t Bytes() const noexcept { return bytes_; }
    uint64_t Evicted() const noexcept { return evicted_; }
private:
    std::deque<Event> events_;
    size_t max_events_;
    size_t max_bytes_;
    size_t bytes_ = 0;
    uint64_t next_id_ = 1;
    uint64_t evicted_ = 0;
};

class EventQueue final {
public:
#pragma warning(suppress : 26455) // std::deque allocation failures must propagate, not terminate.
    explicit EventQueue(size_t max_events = kEventQueueDefaultRecords, size_t max_bytes = kEventQueueDefaultBytes)
        : buffer_(max_events, max_bytes) {}
    void Push(Event event);
    std::vector<Event> Take();
    uint64_t Evicted() const;
private:
    mutable std::mutex mutex_;
    EventBuffer buffer_;
};

struct ProcessChoices {
    std::vector<std::wstring> names;
    std::vector<DWORD> pids;
};

ProcessChoices CollectProcessChoices(const EventBuffer& buffer, std::wstring_view process);
std::optional<DWORD> ParsePid(const std::wstring& text) noexcept;
std::wstring LevelName(UCHAR level);
std::wstring FormatGuid(const GUID& guid);
std::wstring FormatShortGuid(const GUID& guid);
std::wstring FormatTime(uint64_t time);
std::wstring FormatEventEntry(const Event& event, std::wstring_view level_text = {});
std::wstring FormatEventsForClipboard(std::span<const Event> events);
std::string ExportEvents(const std::vector<Event>& events, std::wstring_view source);
// Explicit export only; reports Win32 errors. On failure a target can be partial; the snapshot is retained.
ULONG WriteExport(const wchar_t* path, std::string_view bytes) noexcept;

} // namespace qiqi::monitor
