#include "tools/log_monitor/aggregate.h"
#include "tools/log_monitor/collector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory_resource>
#include <new>
#include <tuple>

namespace qiqi::monitor {
namespace {
constexpr uint64_t kMillisecondsPerSecond = 1000;

const auto& Bounds() noexcept {
    static const auto bounds = []() noexcept {
        std::array<uint64_t, kMetricBucketCount - 1> result{};
        const double range = std::log(gsl::narrow_cast<double>(kMetricHistogramLimitNs));
        for (size_t index = 1; index < result.size(); ++index) {
            const double fraction = gsl::narrow_cast<double>(index - 1) /
                gsl::narrow_cast<double>(result.size() - 2);
            gsl::at(result, index) = (std::max)(gsl::at(result, index - 1) + 1,
                gsl::narrow_cast<uint64_t>(std::ceil(std::exp(range * fraction))));
        }
        result.back() = kMetricHistogramLimitNs;
        return result;
    }();
    return bounds;
}


bool GuidLess(const GUID& left, const GUID& right) noexcept {
    return std::memcmp(&left, &right, sizeof(GUID)) < 0;
}
struct DimensionLess {
    bool operator()(const MetricDimension& left, const MetricDimension& right) const noexcept {
        if (left.pid != right.pid) return left.pid < right.pid;
        if (left.instance_id != right.instance_id) return GuidLess(left.instance_id, right.instance_id);
        return std::tie(left.service_id, left.metric, left.mode, left.source) <
            std::tie(right.service_id, right.metric, right.mode, right.source);
    }
};
struct OperationKey {
    uint64_t capture_generation;
    DWORD pid;
    GUID instance_id;
    uint64_t service_id;
    uint64_t operation_id;
};
struct OperationLess {
    bool operator()(const OperationKey& left, const OperationKey& right) const noexcept {
        if (left.capture_generation != right.capture_generation) return left.capture_generation < right.capture_generation;
        if (left.pid != right.pid) return left.pid < right.pid;
        if (left.instance_id != right.instance_id) return GuidLess(left.instance_id, right.instance_id);
        return std::tie(left.service_id, left.operation_id) < std::tie(right.service_id, right.operation_id);
    }
};

class Budget final : public std::pmr::memory_resource {
public:
    Budget(size_t limit, size_t fixed) noexcept : used(fixed), limit_(limit) {}
    size_t used;
    uint64_t rejections = 0;
private:
    void* do_allocate(size_t bytes, size_t alignment) override {
        if (bytes > limit_ - used) {
            ++rejections;
            throw std::bad_alloc();
        }
        void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        used += bytes;
        return result;
    }
    void do_deallocate(void* memory, size_t bytes, size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
        used -= bytes;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    size_t limit_;
};

bool AddError(MetricSummary& summary, metrics::ErrorDomain domain, uint32_t code, uint64_t count = 1) noexcept {
    if (domain == metrics::ErrorDomain::None) return true;
    for (auto& error : std::span(summary.errors).first(summary.error_count)) {
        if (error.domain == domain && error.code == code) { error.count += count; return true; }
    }
    if (summary.error_count == summary.errors.size()) { summary.other_errors += count; return false; }
    gsl::at(summary.errors, summary.error_count++) = {domain, code, count};
    return true;
}
bool Any(const metrics::DiagnosticCounts& counts) noexcept {
    return counts.protocol || counts.clock || counts.identity || counts.write || counts.interrupted;
}
bool Accumulate(metrics::DiagnosticCounts& target, const metrics::DiagnosticCounts& delta) noexcept {
    constexpr auto maximum = (std::numeric_limits<uint64_t>::max)();
    if (delta.protocol > maximum - target.protocol || delta.clock > maximum - target.clock ||
        delta.identity > maximum - target.identity || delta.write > maximum - target.write ||
        delta.interrupted > maximum - target.interrupted) return false;
    target.protocol += delta.protocol;
    target.clock += delta.clock;
    target.identity += delta.identity;
    target.write += delta.write;
    target.interrupted += delta.interrupted;
    return true;
}
} // namespace

ULONG MetricHistogram::Observe(uint64_t duration_ns, uint64_t threshold_ns) noexcept {
    if (samples_ == (std::numeric_limits<uint64_t>::max)()) return ERROR_ARITHMETIC_OVERFLOW;
    const auto& bounds = Bounds();
    const size_t bucket = gsl::narrow_cast<size_t>(std::lower_bound(bounds.begin(), bounds.end(), duration_ns) - bounds.begin());
    ++gsl::at(buckets_, bucket);
    ++samples_;
    maximum_ = (std::max)(maximum_, duration_ns);
    if (threshold_ns && duration_ns > threshold_ns) ++over_threshold_;
    return ERROR_SUCCESS;
}

ULONG MetricHistogram::Merge(const MetricHistogram& other) noexcept {
    if (other.samples_ > (std::numeric_limits<uint64_t>::max)() - samples_) return ERROR_ARITHMETIC_OVERFLOW;
    for (size_t index = 0; index < buckets_.size(); ++index) gsl::at(buckets_, index) += gsl::at(other.buckets_, index);
    samples_ += other.samples_;
    maximum_ = (std::max)(maximum_, other.maximum_);
    over_threshold_ += other.over_threshold_;
    return ERROR_SUCCESS;
}

std::optional<uint64_t> MetricHistogram::UpperBound(size_t bucket) noexcept {
    if (bucket >= Bounds().size()) return std::nullopt;
    return gsl::at(Bounds(), bucket);
}

MetricQuantile MetricHistogram::Quantile(unsigned int percentile) const noexcept {
    MetricQuantile result;
    result.samples = samples_;
    if (percentile != 50 && percentile != 95 && percentile != 99) { result.state = QuantileState::InvalidPercentile; return result; }
    if (samples_ == 0) return result;
    const uint64_t minimum = percentile == 95 ? kMetricP95MinimumSamples :
                            percentile == 99 ? kMetricP99MinimumSamples : 1;
    if (samples_ < minimum) { result.state = QuantileState::InsufficientSamples; return result; }
    const uint64_t rank = (samples_ / 100) * percentile + ((samples_ % 100) * percentile + 99) / 100;
    uint64_t seen = 0;
    for (size_t index = 0; index < buckets_.size(); ++index) {
        seen += gsl::at(buckets_, index);
        if (seen >= rank) {
            result.upper_ns = UpperBound(index);
            result.state = result.upper_ns ? QuantileState::Value : QuantileState::Overflow;
            return result;
        }
    }
    result.state = QuantileState::InvalidPercentile;
    return result;
}

uint64_t MetricSummary::Count(metrics::Outcome outcome) const noexcept {
    return gsl::at(outcomes, std::to_underlying(outcome));
}
std::optional<double> MetricSummary::FailureRate() const noexcept {
    const uint64_t success_count = Count(metrics::Outcome::Success);
    const uint64_t failure_count = Count(metrics::Outcome::Failure);
    if (failure_count > (std::numeric_limits<uint64_t>::max)() - success_count || success_count + failure_count == 0) return std::nullopt;
    return gsl::narrow_cast<double>(failure_count) / gsl::narrow_cast<double>(success_count + failure_count);
}
std::optional<double> MetricSummary::Coverage() const noexcept {
    if (!cumulative || intents == 0 || observed_terminals > intents) return std::nullopt;
    return gsl::narrow_cast<double>(observed_terminals) / gsl::narrow_cast<double>(intents);
}
ULONG MetricSummary::Merge(const MetricSummary& other) noexcept {
    constexpr auto maximum = (std::numeric_limits<uint64_t>::max)();
    if (other.intents > maximum - intents || other.observed_terminals > maximum - observed_terminals ||
        other.pending > maximum - pending || other.success.Samples() > maximum - success.Samples() ||
        other.failure.Samples() > maximum - failure.Samples() || other.other_errors > maximum - other_errors) return ERROR_ARITHMETIC_OVERFLOW;
    for (size_t index = 0; index < outcomes.size(); ++index) {
        if (gsl::at(other.outcomes, index) > maximum - gsl::at(outcomes, index)) return ERROR_ARITHMETIC_OVERFLOW;
    }
    std::ignore = success.Merge(other.success);
    std::ignore = failure.Merge(other.failure);
    intents += other.intents;
    observed_terminals += other.observed_terminals;
    pending += other.pending;
    for (size_t index = 0; index < outcomes.size(); ++index) gsl::at(outcomes, index) += gsl::at(other.outcomes, index);
    for (const auto& error : std::span(other.errors).first(other.error_count)) std::ignore = AddError(*this, error.domain, error.code, error.count);
    other_errors += other.other_errors;
    cumulative = cumulative && other.cumulative;
    return ERROR_SUCCESS;
}

struct MetricAggregator::Impl {
    struct Service { uint64_t id = 0; size_t dimensions = 0; };
    struct Instance {
        DWORD pid = 0;
        MetricInstance metadata;
        std::array<Service, kMetricMaxServicesPerInstance> services{};
        size_t service_count = 0;
    };
    struct Dimension {
        explicit Dimension(std::pmr::memory_resource* resource) : seconds(resource) {}
        MetricSummary cumulative;
        GUID latest_activity{};
        std::pmr::map<uint64_t, MetricSummary> seconds;
    };
    struct Operation {
        MetricDimension dimension;
        uint64_t parent;
        uint64_t source_generation;
        bool closed;
    };
    Impl(size_t limit, uint64_t generation)
        : budget(limit, sizeof(Impl) + sizeof(Bounds())), dimensions(&budget), operations(&budget) {
        status.capture_generation = generation;
        status.memory_limit = limit;
        status.active = true;
    }
    Budget budget;
    std::pmr::map<MetricDimension, Dimension, DimensionLess> dimensions;
    std::pmr::map<OperationKey, Operation, OperationLess> operations;
    std::array<Instance, kMetricMaxInstances> instances{};
    MetricAggregationStatus status;
    uint64_t tick_ms = 0;

    ULONG Reject(uint64_t& counter, ULONG error) noexcept { ++counter; status.incomplete = true; return error; }
    ULONG AllocationFailure(uint64_t before) noexcept {
        return Reject(budget.rejections != before ? status.aggregation_dropped : status.allocation_errors, ERROR_NOT_ENOUGH_MEMORY);
    }
    Instance* FindInstance(DWORD pid, const GUID& id) noexcept {
        for (auto& instance : std::span(instances).first(status.instances)) {
            if (instance.pid == pid && instance.metadata.instance_id == id) return &instance;
        }
        return nullptr;
    }
    bool InWindow(uint64_t second) const noexcept {
        const uint64_t current = tick_ms / kMillisecondsPerSecond;
        return second <= current && current - second < kMetricWindowSeconds;
    }
    MetricSummary* Window(Dimension& dimension, uint64_t second) {
        if (!InWindow(second)) return nullptr;
        return &dimension.seconds.try_emplace(second).first->second;
    }
    void Advance(uint64_t tick) noexcept {
        tick_ms = (std::max)(tick_ms, tick);
        for (auto& [key, dimension] : dimensions) {
            std::ignore = key;
            while (!dimension.seconds.empty() && !InWindow(dimension.seconds.begin()->first)) {
                dimension.seconds.erase(dimension.seconds.begin());
            }
        }
    }
    void Incomplete(Operation& operation, uint64_t second) noexcept {
#pragma warning(suppress : 26447) // Lookup allocates nothing and DimensionLess cannot throw.
        auto& dimension = dimensions.find(operation.dimension)->second;
        --dimension.cumulative.pending;
        ++gsl::at(dimension.cumulative.outcomes, std::to_underlying(metrics::Outcome::Incomplete));
        operation.closed = true;
        --status.pending;
        status.incomplete = true;
        const uint64_t before = budget.rejections;
        try {
            if (auto* window = Window(dimension, second)) ++gsl::at(window->outcomes, std::to_underlying(metrics::Outcome::Incomplete));
        } catch (const std::bad_alloc&) { std::ignore = AllocationFailure(before); }
    }
    void ClosePending(DWORD pid, const GUID& instance_id, uint64_t second, bool all) noexcept {
        for (auto& [key, operation] : operations) {
            if (!operation.closed && (all || (key.pid == pid && key.instance_id == instance_id))) Incomplete(operation, second);
        }
    }
    ULONG Metadata(const MetricRecord& record, const MetricInstance& metadata) noexcept {
        auto* instance = FindInstance(record.pid, metadata.instance_id);
        if (!instance) {
            if (status.instances == instances.size()) return Reject(status.aggregation_dropped, ERROR_TOO_MANY_NAMES);
            instance = &gsl::at(instances, status.instances++);
            instance->pid = record.pid;
            instance->metadata = metadata;
            if (Any(metadata.diagnostics)) status.incomplete = true;
            return ERROR_SUCCESS;
        }
        if (metadata.capture_generation < instance->metadata.capture_generation) return Reject(status.generation_errors, ERROR_REVISION_MISMATCH);
        if (metadata.build_version != instance->metadata.build_version || metadata.architecture != instance->metadata.architecture) {
            return Reject(status.protocol_errors, ERROR_INVALID_DATA);
        }
        const auto& previous = instance->metadata.diagnostics;
        const auto& next = metadata.diagnostics;
        if (next.protocol < previous.protocol || next.clock < previous.clock || next.identity < previous.identity ||
            next.write < previous.write || next.interrupted < previous.interrupted) return Reject(status.protocol_errors, ERROR_INVALID_DATA);
        const metrics::DiagnosticCounts delta{next.protocol - previous.protocol, next.clock - previous.clock,
            next.identity - previous.identity, next.write - previous.write, next.interrupted - previous.interrupted};
        if (!Accumulate(status.provider_deltas, delta)) return Reject(status.protocol_errors, ERROR_ARITHMETIC_OVERFLOW);
        if (Any(status.provider_deltas)) status.incomplete = true;
        if (metadata.capture_generation > instance->metadata.capture_generation) {
            ClosePending(record.pid, metadata.instance_id, record.received_tick_ms / kMillisecondsPerSecond, false);
        }
        instance->metadata = metadata;
        return ERROR_SUCCESS;
    }
    ULONG Sample(const MetricRecord& record, const metrics::Event& event) {
        auto* instance = FindInstance(record.pid, event.instance_id);
        if (!instance || instance->metadata.capture_generation != event.capture_generation) return Reject(status.generation_errors, ERROR_NOT_READY);
        const MetricDimension key{record.pid, event.instance_id, event.service_id, event.metric, event.mode, event.source};
        const OperationKey operation_key{record.capture_generation, record.pid, event.instance_id, event.service_id, event.operation_id};
        auto operation = operations.find(operation_key);
        if (operation != operations.end()) {
            if (operation->second.closed || event.phase == metrics::Phase::Begin) return Reject(status.duplicate_events, ERROR_ALREADY_EXISTS);
            if (event.phase != metrics::Phase::End || operation->second.dimension != key ||
                operation->second.parent != event.parent_operation_id || operation->second.source_generation != event.capture_generation) {
                return Reject(status.protocol_errors, ERROR_INVALID_DATA);
            }
        } else if (event.phase == metrics::Phase::Begin && status.pending == kMetricMaxPending) {
            ++status.pending_dropped;
            return Reject(status.aggregation_dropped, ERROR_BUFFER_OVERFLOW);
        }
        auto service = std::find_if(instance->services.begin(), instance->services.begin() + instance->service_count,
            [&](const Service& value) noexcept { return value.id == event.service_id; });
        const bool new_service = service == instance->services.begin() + instance->service_count;
        if (new_service && instance->service_count == instance->services.size()) return Reject(status.aggregation_dropped, ERROR_TOO_MANY_NAMES);
        auto dimension = dimensions.find(key);
        if (dimension == dimensions.end() && !new_service && service->dimensions == kMetricMaxDimensionsPerService) return Reject(status.aggregation_dropped, ERROR_TOO_MANY_NAMES);
        bool inserted_dimension = false;
        bool inserted_window = false;
        bool inserted_operation = false;
        bool committed = false;
        const uint64_t second = record.received_tick_ms / kMillisecondsPerSecond;
        const auto rollback = gsl::finally([&]() noexcept {
            if (!committed) {
                if (inserted_operation) operations.erase(operation);
                if (inserted_window) dimension->second.seconds.erase(second);
                if (inserted_dimension) dimensions.erase(dimension);
            }
        });
        if (dimension == dimensions.end()) {
            dimension = dimensions.try_emplace(key, &budget).first;
            inserted_dimension = true;
        }
        MetricSummary* window = nullptr;
        if (InWindow(second)) {
            const auto entry = dimension->second.seconds.try_emplace(second);
            window = &entry.first->second;
            inserted_window = entry.second;
        }
        const bool new_operation = operation == operations.end();
        if (new_operation) {
            operation = operations.emplace(operation_key, Operation{key, event.parent_operation_id, event.capture_generation, false}).first;
            inserted_operation = true;
        }
        if (new_service) {
            service = instance->services.begin() + instance->service_count++;
            *service = {event.service_id, 0};
        }
        if (inserted_dimension) { ++service->dimensions; ++status.dimensions; }
        auto& summary = dimension->second.cumulative;
        if (new_operation) { ++summary.intents; if (window) ++window->intents; }
        if (event.phase == metrics::Phase::Begin) {
            ++summary.pending;
            ++status.pending;
        } else {
            if (!new_operation) { --summary.pending; --status.pending; }
            const bool unmatched = new_operation && event.phase == metrics::Phase::End;
            const auto outcome = unmatched ? metrics::Outcome::Incomplete : event.outcome;
            const auto update = [&](MetricSummary& target) noexcept {
                ++gsl::at(target.outcomes, std::to_underlying(outcome));
                if (outcome != metrics::Outcome::Incomplete) ++target.observed_terminals;
                if (outcome == metrics::Outcome::Success) std::ignore = target.success.Observe(*event.duration_ns, MetricThresholdNs(event.metric));
                if (outcome == metrics::Outcome::Failure) std::ignore = target.failure.Observe(*event.duration_ns, MetricThresholdNs(event.metric));
            };
            update(summary);
            if (window) update(*window);
            if (!unmatched && event.error_domain != metrics::ErrorDomain::None) {
                const bool known = AddError(summary, event.error_domain, event.error_code);
                if (window) {
                    if (known) std::ignore = AddError(*window, event.error_domain, event.error_code);
                    else ++window->other_errors;
                }
                if (!known) { ++status.error_codes_dropped; status.incomplete = true; }
            }
            if (unmatched) { ++status.unmatched_ends; status.incomplete = true; }
            if (outcome == metrics::Outcome::Incomplete) status.incomplete = true;
            operation->second.closed = true;
        }
        status.remembered_operations = operations.size();
        dimension->second.latest_activity = event.activity_id;
        committed = true;
        return ERROR_SUCCESS;
    }
};

MetricAggregator::MetricAggregator(size_t budget_bytes) noexcept : budget_bytes_(budget_bytes) {}
MetricAggregator::~MetricAggregator() noexcept = default;
size_t MetricAggregator::FixedBytes() noexcept { return sizeof(Impl) + sizeof(Bounds()); }
ULONG MetricAggregator::BeginCapture(uint64_t generation) noexcept {
    if (generation == 0 || generation <= generation_) return ERROR_INVALID_PARAMETER;
    if (budget_bytes_ < FixedBytes() || budget_bytes_ > kMetricAggregationBudget) return ERROR_INVALID_PARAMETER;
    impl_.reset();
    try {
        impl_ = std::make_unique<Impl>(budget_bytes_, generation);
        generation_ = generation;
        return ERROR_SUCCESS;
    } catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
}
ULONG MetricAggregator::Consume(const MetricRecord& record) noexcept {
    if (!impl_) return ERROR_INVALID_STATE;
    if (!impl_->status.active || record.capture_generation != impl_->status.capture_generation) {
        return impl_->Reject(impl_->status.generation_errors, ERROR_REVISION_MISMATCH);
    }
    if (!ValidMetricRecord(record)) return impl_->Reject(impl_->status.protocol_errors, ERROR_INVALID_DATA);
    if (!impl_->status.statistics_start_tick_ms) {
        impl_->status.statistics_start_tick_ms = record.received_tick_ms;
    }
    impl_->Advance(record.received_tick_ms);
    if (const auto* metadata = std::get_if<MetricInstance>(&record.payload)) return impl_->Metadata(record, *metadata);
    const uint64_t before = impl_->budget.rejections;
    try { return impl_->Sample(record, std::get<metrics::Event>(record.payload)); }
    catch (const std::bad_alloc&) { return impl_->AllocationFailure(before); }
}
ULONG MetricAggregator::Advance(uint64_t tick_ms) noexcept {
    if (!impl_ || !impl_->status.active) return ERROR_INVALID_STATE;
    if (tick_ms < impl_->tick_ms) return impl_->Reject(impl_->status.protocol_errors, ERROR_INVALID_DATA);
    impl_->Advance(tick_ms);
    return ERROR_SUCCESS;
}
ULONG MetricAggregator::Stop(uint64_t tick_ms) noexcept {
    if (!impl_) return ERROR_INVALID_STATE;
    if (!impl_->status.active) return ERROR_SUCCESS;
    if (tick_ms < impl_->tick_ms) return impl_->Reject(impl_->status.protocol_errors, ERROR_INVALID_DATA);
    impl_->Advance(tick_ms);
    impl_->ClosePending(0, GUID_NULL, tick_ms / kMillisecondsPerSecond, true);
    impl_->status.active = false;
    return ERROR_SUCCESS;
}
void MetricAggregator::Clear() noexcept {
    if (!impl_) return;
    for (auto& [key, operation] : impl_->operations) { std::ignore = key; operation.closed = true; }
    impl_->dimensions.clear();
    for (auto& instance : std::span(impl_->instances).first(impl_->status.instances)) {
        instance.services = {};
        instance.service_count = 0;
    }
    impl_->status.dimensions = 0;
    impl_->status.pending = 0;
    impl_->status.statistics_start_tick_ms = impl_->tick_ms;
}
ULONG MetricAggregator::UpdateQueueStatus(const MetricQueueStatus& status) noexcept {
    if (!impl_) return ERROR_INVALID_STATE;
    if (status.capture_generation != impl_->status.capture_generation) return impl_->Reject(impl_->status.generation_errors, ERROR_REVISION_MISMATCH);
    impl_->status.queue = status;
    impl_->status.incomplete = impl_->status.incomplete || status.incomplete;
    return ERROR_SUCCESS;
}
MetricAggregationStatus MetricAggregator::Status() const noexcept {
    if (!impl_) {
        MetricAggregationStatus status;
        status.capture_generation = generation_;
        status.memory_limit = budget_bytes_;
        return status;
    }
    auto status = impl_->status;
    status.memory_bytes = impl_->budget.used;
    return status;
}
std::vector<MetricDimension> MetricAggregator::Dimensions() const {
    std::vector<MetricDimension> result;
    if (impl_) {
        result.reserve(impl_->dimensions.size());
        for (const auto& [key, dimension] : impl_->dimensions) {
            std::ignore = dimension;
            result.push_back(key);
        }
    }
    return result;
}
std::vector<MetricProcessInstance> MetricAggregator::Instances() const {
    std::vector<MetricProcessInstance> result;
    if (impl_) {
        result.reserve(impl_->status.instances);
        for (const auto& instance : std::span(impl_->instances).first(impl_->status.instances))
            result.push_back({instance.pid, instance.metadata});
    }
    return result;
}
GUID MetricAggregator::LatestActivity(const MetricDimension& key) const noexcept {
    if (!impl_) return GUID_NULL;
#pragma warning(suppress : 26447) // Ordered lookup allocates nothing and DimensionLess cannot throw.
    const auto found = impl_->dimensions.find(key);
    return found == impl_->dimensions.end() ? GUID_NULL : found->second.latest_activity;
}
ULONG MetricAggregator::Snapshot(const MetricDimension& key, MetricView view, MetricSummary& output) const noexcept {
    if (!impl_) return ERROR_INVALID_STATE;
#pragma warning(suppress : 26447) // Lookup allocates nothing and DimensionLess cannot throw.
    const auto dimension = impl_->dimensions.find(key);
    if (dimension == impl_->dimensions.end()) return ERROR_NOT_FOUND;
    if (view == MetricView::Cumulative) {
        output = dimension->second.cumulative;
        return ERROR_SUCCESS;
    }
    MetricSummary result;
    result.cumulative = false;
    for (const auto& [second, summary] : dimension->second.seconds) {
        std::ignore = second;
        const ULONG error = result.Merge(summary);
        if (error != ERROR_SUCCESS) return error;
    }
    result.pending = dimension->second.cumulative.pending;
    output = result;
    return ERROR_SUCCESS;
}
std::optional<MetricSummary> MetricAggregator::Snapshot(const MetricDimension& key, MetricView view) const noexcept {
    MetricSummary result;
    if (Snapshot(key, view, result) != ERROR_SUCCESS) return std::nullopt;
    return result;
}

void DrainMetrics(MetricQueue& queue, MetricAggregator& aggregator, Diagnostics& diagnostics, uint64_t tick_ms) {
    auto capture = queue.Status();
    if (capture.capture_generation == 0) return;
    if (capture.capture_generation != aggregator.Status().capture_generation) {
        const ULONG error = aggregator.BeginCapture(capture.capture_generation);
        if (error != ERROR_SUCCESS) { diagnostics.Record(2, L"Initialize metric aggregation", error); return; }
    }
    const auto before = aggregator.Status();
    uint64_t effective_tick = tick_ms;
    for (const auto& record : queue.Take()) {
        effective_tick = (std::max)(effective_tick, record.received_tick_ms);
        const ULONG error = aggregator.Consume(record);
        if (error != ERROR_SUCCESS) {
            const auto status = aggregator.Status();
            const uint64_t count = status.aggregation_dropped + status.protocol_errors + status.generation_errors +
                status.duplicate_events + status.allocation_errors;
            if (count == 1 || count % kDiagnosticReportStride == 0) diagnostics.Record(3, L"Metric aggregation (aggregated)", error);
        }
    }
    capture = queue.Status();
    const ULONG queue_error = aggregator.UpdateQueueStatus(capture);
    if (queue_error != ERROR_SUCCESS) diagnostics.Record(3, L"Metric queue capture changed during aggregation", queue_error);
    if (aggregator.Status().active) {
        // A final callback may enqueue after Take; freeze only after the stopped queue is empty.
        const ULONG error = capture.active || capture.queued_records != 0 ?
            aggregator.Advance(effective_tick) : aggregator.Stop(effective_tick);
        if (error != ERROR_SUCCESS) diagnostics.Record(3, L"Metric aggregation clock/stop", error);
    }
    const auto after = aggregator.Status();
    if (after.error_codes_dropped != before.error_codes_dropped) diagnostics.Record(3, L"Metric error-code dimensions overflow; counted as other", ERROR_BUFFER_OVERFLOW);
    if (after.unmatched_ends != before.unmatched_ends) diagnostics.Record(3, L"Metric end without observed begin; counted as incomplete");
}
} // namespace qiqi::monitor
