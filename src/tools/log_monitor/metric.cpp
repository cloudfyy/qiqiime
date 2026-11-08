#include "tools/log_monitor/metric.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <span>
#include <string_view>
#include <type_traits>

namespace qiqi::monitor {
namespace {
template <size_t Size>
std::optional<std::string_view> Text(const std::array<char, Size>& value) noexcept {
    const auto end = std::find(value.begin(), value.end(), '\0');
    if (end == value.begin() || end == value.end()) return std::nullopt;
    if (!std::all_of(value.begin(), end, [](char c) { return c >= ' ' && c <= '~'; })) return std::nullopt;
    return std::string_view(value.begin(), end);
}
} // namespace

bool ValidMetricInstance(const MetricInstance& instance) noexcept {
    const auto arch = Text(instance.architecture);
    return instance.schema_version == metrics::kSchemaVersion && instance.instance_id != GUID_NULL &&
        instance.capture_generation != 0 && Text(instance.build_version).has_value() &&
        arch && (*arch == "x86" || *arch == "x64");
}

bool ValidMetricRecord(const MetricRecord& record) noexcept {
    if (record.capture_generation == 0 || record.pid == 0 || record.tid == 0) return false;
    return std::visit([](const auto& value) noexcept {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, metrics::Event>) {
            return metrics::Validate(value) == metrics::ValidationError::None;
        } else {
            return ValidMetricInstance(value);
        }
    }, record.payload);
}

#pragma warning(suppress : 26455) // Invalid limits and allocation failures propagate to monitor startup.
MetricQueue::MetricQueue(size_t max_records, size_t max_bytes)
    : max_records_(max_records), max_bytes_(max_bytes) {
    if (max_records == 0 || max_bytes < sizeof(MetricRecord)) {
        throw std::invalid_argument("Metric queue limits must be positive");
    }
}

ULONG MetricQueue::BeginCapture(uint64_t generation) noexcept {
    const auto lock = Lock();
    if (generation == 0 || generation <= status_.capture_generation) return ERROR_INVALID_PARAMETER;
    records_.clear();
    sources_ = {};
    source_count_ = 0;
    status_ = {};
    status_.capture_generation = generation;
    status_.active = true;
    return ERROR_SUCCESS;
}

void MetricQueue::EndCapture() noexcept {
    const auto lock = Lock();
    status_.active = false;
}

ULONG MetricQueue::Push(const MetricRecord& record) {
    const auto lock = Lock();
    if (!status_.active || record.capture_generation != status_.capture_generation) {
        ++status_.generation_errors;
        status_.incomplete = true;
        return ERROR_REVISION_MISMATCH;
    }
    if (!ValidMetricRecord(record)) {
        ++status_.protocol_errors;
        status_.incomplete = true;
        return ERROR_INVALID_DATA;
    }
    const auto id = std::visit([](const auto& value) noexcept { return value.instance_id; }, record.payload);
    const auto generation = std::visit([](const auto& value) noexcept { return value.capture_generation; }, record.payload);
    const auto sources = std::span(sources_).first(source_count_);
    auto source = std::find_if(sources.begin(), sources.end(), [&](const auto& value) noexcept {
        return value.pid == record.pid && value.instance_id == id;
    });
    const bool metadata = std::holds_alternative<MetricInstance>(record.payload);
    if (source == sources.end()) {
        if (!metadata) {
            ++status_.generation_errors;
            status_.incomplete = true;
            return ERROR_NOT_READY;
        }
        if (source_count_ == sources_.size()) {
            ++status_.source_limit_errors;
            status_.incomplete = true;
            return ERROR_TOO_MANY_NAMES;
        }
        source = std::span(sources_).begin() + source_count_;
        ++source_count_;
        *source = {record.pid, id, generation, false};
    } else if (generation < source->generation || (!metadata && generation != source->generation)) {
        ++status_.generation_errors;
        status_.incomplete = true;
        return ERROR_REVISION_MISMATCH;
    }
    if (metadata && generation > source->generation) {
        source->generation = generation;
        source->published = false;
    }
    if (!metadata && !source->published) {
        ++status_.generation_errors;
        status_.incomplete = true;
        return ERROR_NOT_READY;
    }
    if (records_.size() >= max_records_ || status_.queued_bytes > max_bytes_ - sizeof(MetricRecord)) {
        ++status_.queue_dropped;
        status_.incomplete = true;
        return ERROR_BUFFER_OVERFLOW;
    }
    try {
        records_.push_back(record);
    } catch (const std::bad_alloc&) {
        ++status_.allocation_errors;
        status_.incomplete = true;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (metadata) {
        source->published = true;
        const auto& counts = std::get<MetricInstance>(record.payload).diagnostics;
        // Snapshots are module-lifetime totals; without a terminal snapshot, zero deltas cannot prove completeness.
        if (counts.protocol || counts.clock || counts.identity || counts.write || counts.interrupted) {
            status_.incomplete = true;
        }
    }
    status_.queued_records = records_.size();
    status_.queued_bytes += sizeof(MetricRecord);
    return ERROR_SUCCESS;
}

std::vector<MetricRecord> MetricQueue::Take() {
    const auto lock = Lock();
    try {
        std::vector<MetricRecord> result(records_.begin(), records_.end());
        records_.clear();
        status_.queued_records = 0;
        status_.queued_bytes = 0;
        return result;
    } catch (const std::bad_alloc&) {
        ++status_.allocation_errors;
        status_.incomplete = true;
        throw;
    }
}

MetricQueueStatus MetricQueue::Status() const noexcept {
    const auto lock = Lock();
    return status_;
}

void MetricQueue::RecordDecodeError() noexcept {
    const auto lock = Lock();
    ++status_.protocol_errors;
    status_.incomplete = true;
}

void MetricQueue::RecordAllocationError() noexcept {
    const auto lock = Lock();
    ++status_.allocation_errors;
    status_.incomplete = true;
}

void MetricQueue::RecordCaptureError() noexcept {
    const auto lock = Lock();
    ++status_.capture_errors;
    status_.incomplete = true;
}

void MetricQueue::RecordLoss(uint64_t events, uint64_t buffers) noexcept {
    const auto lock = Lock();
    status_.events_lost = events;
    status_.buffers_lost = buffers;
    if (events || buffers) status_.incomplete = true;
}
} // namespace qiqi::monitor
