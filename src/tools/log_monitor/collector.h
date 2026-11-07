#pragma once

#include "tools/log_monitor/model.h"
#include <evntrace.h>
#include <evntcons.h>
#include <gsl/gsl>
#include <atomic>
#include <thread>

namespace qiqi::monitor {

class Diagnostics final {
public:
    Diagnostics();
    ~Diagnostics() noexcept;
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;
    void Record(UCHAR level, std::wstring_view operation, ULONG error = ERROR_SUCCESS) noexcept;
    EventQueue queue{512, 1024 * 1024};
    std::atomic<uint64_t> allocation_errors{0};
};

// Decode only our provider's known fields. The callback never retains EVENT_RECORD pointers.
ULONG DecodeEvent(EVENT_RECORD& record, Event& event);

struct CaptureStartFailure {
    std::wstring_view operation;
    ULONG error = ERROR_SUCCESS;
    bool IsPermissionDenied() const noexcept {
        return error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD;
    }
};

struct CaptureStartApi {
    gsl::not_null<decltype(&::StartTraceW)> start_trace = &::StartTraceW;
    gsl::not_null<decltype(&::EnableTraceEx2)> enable_trace = &::EnableTraceEx2;
    gsl::not_null<decltype(&::OpenTraceW)> open_trace = &::OpenTraceW;
};

class Collector final {
public:
    Collector(EventQueue& queue, Diagnostics& diagnostics, CaptureStartApi api = {}) noexcept
        : queue_(queue), diagnostics_(diagnostics), api_(api) {}
    ~Collector() noexcept;
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;
    Collector(Collector&&) = delete;
    Collector& operator=(Collector&&) = delete;
    bool Start();
    bool Stop() noexcept;
    void PollLoss() noexcept;
    bool Running() const noexcept { return running_.load(); }
    std::wstring_view SessionName() const noexcept { return name_; }
    const std::optional<CaptureStartFailure>& LastStartFailure() const noexcept { return start_failure_; }
    std::atomic<uint64_t> decode_errors{0};
    std::atomic<uint64_t> events_lost{0};
    std::atomic<uint64_t> buffers_lost{0};
    std::atomic<uint64_t> allocation_errors{0};
private:
    static void WINAPI OnEvent(EVENT_RECORD* record) noexcept;
    void Consume() noexcept;
    bool FailStart(std::wstring_view operation, ULONG error) noexcept;
    EventQueue& queue_;
    Diagnostics& diagnostics_;
    CaptureStartApi api_;
    std::optional<CaptureStartFailure> start_failure_;
    TRACEHANDLE session_ = 0;
    TRACEHANDLE reader_ = INVALID_PROCESSTRACE_HANDLE;
    std::wstring name_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    uint64_t lost_baseline_ = 0;
    uint64_t buffers_baseline_ = 0;
};

} // namespace qiqi::monitor
