#pragma once

#include <windows.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::monitor {

inline constexpr GUID kImeProvider =
    {0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99}};
inline constexpr GUID kMonitorProvider =
    {0x6ab897b4, 0xd148, 0x4df9, {0x8e, 0xb7, 0x47, 0x90, 0xa2, 0x74, 0x95, 0x1e}};

struct Event {
    uint64_t id = 0;
    uint64_t time = 0; // FILETIME units, UTC.
    DWORD pid = 0;
    DWORD tid = 0;
    UCHAR level = 0;
    std::wstring process;
    std::wstring name;
    std::wstring message;
    size_t Bytes() const noexcept;
};

struct Filter {
    std::wstring process;
    std::optional<DWORD> pid;
    UCHAR level = 0; // Zero means all; otherwise exact ETW level.
    std::wstring text;
    bool Matches(const Event& event) const noexcept;
};

class EventBuffer final {
public:
#pragma warning(suppress : 26455) // Allocating bounded storage may throw; callers report startup failures.
    explicit EventBuffer(size_t max_events = 10'000, size_t max_bytes = 16 * 1024 * 1024);
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
    explicit EventQueue(size_t max_events = 2'000, size_t max_bytes = 4 * 1024 * 1024)
        : buffer_(max_events, max_bytes) {}
    void Push(Event event);
    std::vector<Event> Take();
    uint64_t Evicted() const;
private:
    mutable std::mutex mutex_;
    EventBuffer buffer_;
};

std::optional<DWORD> ParsePid(const std::wstring& text) noexcept;
std::wstring LevelName(UCHAR level);
std::wstring FormatTime(uint64_t time);
std::string ExportEvents(const std::vector<Event>& events, std::wstring_view source);
// Explicit export only; reports Win32 errors. On failure a target can be partial; the snapshot is retained.
ULONG WriteExport(const wchar_t* path, std::string_view bytes) noexcept;

} // namespace qiqi::monitor
