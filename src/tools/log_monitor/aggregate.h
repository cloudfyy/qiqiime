#pragma once

#include "tools/log_monitor/metric.h"
#include <memory>
#include <span>

namespace qiqi::monitor {

inline constexpr size_t kMetricBucketCount = 512;
inline constexpr uint64_t kMetricHistogramLimitNs = 60'000'000'000;
inline constexpr size_t kMetricAggregationBudget = 64 * 1024 * 1024;
inline constexpr size_t kMetricMaxServicesPerInstance = 16;
inline constexpr size_t kMetricMaxDimensionsPerService = 16;
inline constexpr size_t kMetricMaxPending = 4096;
inline constexpr size_t kMetricMaxErrorCodes = 32;
inline constexpr uint64_t kMetricWindowSeconds = 60;
inline constexpr uint64_t kMetricP95MinimumSamples = 100;
inline constexpr uint64_t kMetricP99MinimumSamples = 1000;
inline constexpr uint64_t kKeyCallbackThresholdNs = 5'000'000;
inline constexpr uint64_t kCandidateUpdateThresholdNs = 5'000'000;
inline constexpr uint64_t kCandidateQueryThresholdNs = 2'000'000;
inline constexpr uint64_t kCommitThresholdNs = 20'000'000;
constexpr uint64_t MetricThresholdNs(metrics::Metric metric) noexcept {
    switch (metric) {
    case metrics::Metric::KeyCallback: return kKeyCallbackThresholdNs;
    case metrics::Metric::CandidateUpdate: return kCandidateUpdateThresholdNs;
    case metrics::Metric::CandidateQuery: return kCandidateQueryThresholdNs;
    case metrics::Metric::Commit: return kCommitThresholdNs;
    default: return 0;
    }
}

enum class QuantileState { Value, Overflow, NoSamples, InsufficientSamples, InvalidPercentile };
struct MetricQuantile {
    QuantileState state = QuantileState::NoSamples;
    std::optional<uint64_t> upper_ns;
    uint64_t samples = 0;
};

class MetricHistogram final {
public:
    ULONG Observe(uint64_t duration_ns, uint64_t threshold_ns = 0) noexcept;
    ULONG Merge(const MetricHistogram& other) noexcept;
    MetricQuantile Quantile(unsigned int percentile) const noexcept;
    static std::optional<uint64_t> UpperBound(size_t bucket) noexcept;
    std::span<const uint64_t, kMetricBucketCount> Buckets() const noexcept { return buckets_; }
    uint64_t Samples() const noexcept { return samples_; }
    std::optional<uint64_t> Maximum() const noexcept { return samples_ ? std::optional(maximum_) : std::nullopt; }
    uint64_t OverThreshold() const noexcept { return over_threshold_; }
private:
    std::array<uint64_t, kMetricBucketCount> buckets_{};
    uint64_t samples_ = 0;
    uint64_t maximum_ = 0;
    uint64_t over_threshold_ = 0;
};

struct MetricErrorCount {
    metrics::ErrorDomain domain = metrics::ErrorDomain::None;
    uint32_t code = 0;
    uint64_t count = 0;
};
struct MetricSummary {
    uint64_t intents = 0;
    uint64_t observed_terminals = 0;
    uint64_t pending = 0;
    std::array<uint64_t, 9> outcomes{};
    MetricHistogram success;
    MetricHistogram failure;
    std::array<MetricErrorCount, kMetricMaxErrorCodes> errors{};
    size_t error_count = 0;
    uint64_t other_errors = 0;
    bool cumulative = true;
    uint64_t Count(metrics::Outcome outcome) const noexcept;
    std::optional<double> FailureRate() const noexcept;
    std::optional<double> Coverage() const noexcept;
    ULONG Merge(const MetricSummary& other) noexcept;
};

struct MetricDimension {
    DWORD pid = 0;
    GUID instance_id{};
    uint64_t service_id = 0;
    metrics::Metric metric = metrics::Metric::FocusTotal;
    metrics::Mode mode = metrics::Mode::Unspecified;
    metrics::Source source = metrics::Source::Unspecified;
    bool operator==(const MetricDimension&) const noexcept = default;
};

enum class MetricView { Cumulative, Recent };
struct MetricProcessInstance {
    DWORD pid = 0;
    MetricInstance metadata;
};
struct MetricAggregationStatus {
    uint64_t capture_generation = 0;
    std::optional<uint64_t> statistics_start_tick_ms;
    size_t instances = 0;
    size_t dimensions = 0;
    size_t pending = 0;
    size_t remembered_operations = 0;
    size_t memory_bytes = 0;
    size_t memory_limit = kMetricAggregationBudget;
    uint64_t aggregation_dropped = 0;
    uint64_t pending_dropped = 0;
    uint64_t protocol_errors = 0;
    uint64_t generation_errors = 0;
    uint64_t duplicate_events = 0;
    uint64_t unmatched_ends = 0;
    uint64_t error_codes_dropped = 0;
    uint64_t allocation_errors = 0;
    metrics::DiagnosticCounts provider_deltas;
    MetricQueueStatus queue;
    bool active = false;
    bool incomplete = false;
};

// Single consumer owns aggregation; no references to IME/COM objects or EVENT_RECORD storage.
class MetricAggregator final {
public:
    explicit MetricAggregator(size_t budget_bytes = kMetricAggregationBudget) noexcept;
    ~MetricAggregator() noexcept;
    MetricAggregator(const MetricAggregator&) = delete;
    MetricAggregator& operator=(const MetricAggregator&) = delete;
    MetricAggregator(MetricAggregator&&) = delete;
    MetricAggregator& operator=(MetricAggregator&&) = delete;
    static size_t FixedBytes() noexcept;
    ULONG BeginCapture(uint64_t generation) noexcept;
    ULONG Consume(const MetricRecord& record) noexcept;
    ULONG Advance(uint64_t tick_ms) noexcept;
    ULONG Stop(uint64_t tick_ms) noexcept;
    void Clear() noexcept;
    ULONG UpdateQueueStatus(const MetricQueueStatus& status) noexcept;
    MetricAggregationStatus Status() const noexcept;
    std::vector<MetricDimension> Dimensions() const;
    std::vector<MetricProcessInstance> Instances() const;
    GUID LatestActivity(const MetricDimension& dimension) const noexcept;
    ULONG Snapshot(const MetricDimension& dimension, MetricView view, MetricSummary& result) const noexcept;
    std::optional<MetricSummary> Snapshot(const MetricDimension& dimension, MetricView view) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    size_t budget_bytes_;
    uint64_t generation_ = 0;
};

class Diagnostics;
void DrainMetrics(MetricQueue& queue, MetricAggregator& aggregator, Diagnostics& diagnostics, uint64_t tick_ms);

} // namespace qiqi::monitor
