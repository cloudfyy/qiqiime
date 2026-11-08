#pragma once

#include "common/metric.h"
#include <array>
#include <deque>
#include <variant>
#include <vector>

namespace qiqi::monitor {

struct MetricInstance {
    uint16_t schema_version = metrics::kSchemaVersion;
    GUID instance_id{};
    uint64_t capture_generation = 0;
    std::array<char, 128> build_version{};
    std::array<char, 16> architecture{};
    metrics::DiagnosticCounts diagnostics;
};

struct MetricRecord {
    uint64_t capture_generation = 0; // Monitor capture, not the provider's generation.
    uint64_t time = 0;
    uint64_t received_tick_ms = 0;
    DWORD pid = 0;
    DWORD tid = 0;
    std::variant<metrics::Event, MetricInstance> payload;
};

bool ValidMetricInstance(const MetricInstance& instance) noexcept;
bool ValidMetricRecord(const MetricRecord& record) noexcept;

struct MetricQueueStatus {
    uint64_t capture_generation = 0;
    size_t queued_records = 0;
    size_t queued_bytes = 0;
    uint64_t queue_dropped = 0;
    uint64_t protocol_errors = 0;
    uint64_t generation_errors = 0;
    uint64_t source_limit_errors = 0;
    uint64_t allocation_errors = 0;
    uint64_t capture_errors = 0;
    uint64_t events_lost = 0;
    uint64_t buffers_lost = 0;
    bool active = false;
    bool incomplete = false;
};

class MetricQueue final {
public:
#pragma warning(suppress : 26455) // Invalid limits and bounded storage allocation reach the caller.
    explicit MetricQueue(size_t max_records = 2'000, size_t max_bytes = 4 * 1024 * 1024);
    MetricQueue(const MetricQueue&) = delete;
    MetricQueue& operator=(const MetricQueue&) = delete;
    MetricQueue(MetricQueue&&) = delete;
    MetricQueue& operator=(MetricQueue&&) = delete;
    ULONG BeginCapture(uint64_t generation) noexcept;
    void EndCapture() noexcept;
    ULONG Push(const MetricRecord& record);
    std::vector<MetricRecord> Take();
    MetricQueueStatus Status() const noexcept;
    void RecordDecodeError() noexcept;
    void RecordAllocationError() noexcept;
    void RecordCaptureError() noexcept;
    void RecordLoss(uint64_t events, uint64_t buffers) noexcept;
private:
    struct SourceState {
        DWORD pid = 0;
        GUID instance_id{};
        uint64_t generation = 0;
        bool published = false;
    };
    auto Lock() const noexcept {
        ::AcquireSRWLockExclusive(&mutex_);
        return gsl::finally([this]() noexcept { ::ReleaseSRWLockExclusive(&mutex_); });
    }
    mutable SRWLOCK mutex_ = SRWLOCK_INIT;
    std::deque<MetricRecord> records_;
    std::array<SourceState, 32> sources_{};
    size_t source_count_ = 0;
    size_t max_records_;
    size_t max_bytes_;
    MetricQueueStatus status_;
};

} // namespace qiqi::monitor
