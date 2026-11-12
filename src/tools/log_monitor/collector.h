// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/log_monitor/model.h"
#include "tools/log_monitor/metric.h"
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <gsl/gsl>
#include <atomic>
#include <thread>

namespace qiqi::monitor {

inline constexpr size_t kDiagnosticMaxRecords = 512;
inline constexpr size_t kDiagnosticMaxBytes = 1024 * 1024;
inline constexpr uint64_t kDiagnosticReportStride = 1024;

class Diagnostics final {
public:
    Diagnostics();
    ~Diagnostics() noexcept;
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;
    void Record(UCHAR level, std::wstring_view operation, ULONG error = ERROR_SUCCESS) noexcept;
    EventQueue queue{kDiagnosticMaxRecords, kDiagnosticMaxBytes};
    std::atomic<uint64_t> allocation_errors{0};
};

// Decode only our provider's known fields. The callback never retains EVENT_RECORD pointers.
ULONG DecodeEvent(EVENT_RECORD& record, Event& event);

struct MetricDecodeApi {
    gsl::not_null<decltype(&::TdhGetEventInformation)> information = &::TdhGetEventInformation;
    gsl::not_null<decltype(&::TdhGetPropertySize)> property_size = &::TdhGetPropertySize;
    gsl::not_null<decltype(&::TdhGetProperty)> property = &::TdhGetProperty;
};
// Failed decoding leaves the destination unchanged; returned errors are actual TDH errors or protocol errors.
ULONG DecodeMetricEvent(EVENT_RECORD& record, uint64_t capture_generation, MetricRecord& event,
    MetricDecodeApi api = {});

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
#pragma warning(suppress : 26455) // The independent bounded queue may allocate during monitor startup.
    Collector(EventQueue& queue, Diagnostics& diagnostics, CaptureStartApi api = {})
        : queue_(queue), diagnostics_(diagnostics), api_(api) {}
    ~Collector() noexcept;
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;
    Collector(Collector&&) = delete;
    Collector& operator=(Collector&&) = delete;
    bool Start(bool collect_metrics = false);
    bool Stop() noexcept;
    void PollLoss() noexcept;
    bool Running() const noexcept { return running_.load(); }
    bool CollectingMetrics() const noexcept { return Running() && collect_metrics_; }
    std::wstring_view SessionName() const noexcept { return name_; }
    const std::optional<CaptureStartFailure>& LastStartFailure() const noexcept { return start_failure_; }
    std::atomic<uint64_t> decode_errors{0};
    std::atomic<uint64_t> events_lost{0};
    std::atomic<uint64_t> buffers_lost{0};
    std::atomic<uint64_t> allocation_errors{0};
    MetricQueue metric_queue;
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
    uint64_t metric_generation_ = 0;
    bool collect_metrics_ = false;
};

} // namespace qiqi::monitor
