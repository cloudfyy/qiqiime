// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/log_monitor/export.h"
#include "common/version.h"

#include <algorithm>
#include <format>
#include <new>

namespace qiqi::monitor {
namespace {
constexpr std::array kOutcomeNames{"unspecified", "success", "failure", "cancelled", "stale",
    "incomplete", "exception", "unhandled", "rejected"};
constexpr std::array kMetricNames{"focus_total", "focus_reload", "focus_position", "key_callback",
    "candidate_query", "candidate_update", "commit", "cleanup"};
constexpr std::array kModeNames{"unspecified", "popup", "ui_less", "focus_gained", "focus_lost"};
constexpr std::array kSourceNames{"unspecified", "thread_manager", "key_event"};
struct JsonError { ULONG code; };
void Check(ULONG error) {
    if (error != ERROR_SUCCESS) throw JsonError{error};
}

class JsonWriter final {
public:
    explicit JsonWriter(size_t limit) noexcept : limit_(limit) {}
    void Append(std::string_view text) {
        if (text.size() > limit_ - bytes.size()) throw JsonError{ERROR_FILE_TOO_LARGE};
        bytes.append(text);
    }
    template<class T> void Number(T value) { Append(std::format("{}", value)); }
    void Boolean(bool value) { Append(value ? "true" : "false"); }
    template<class T> void OptionalNumber(std::optional<T> value) {
        if (value) Number(*value);
        else Append("null");
    }
    void String(std::string_view value) {
        Append("\"");
        for (const char ch : value) {
            switch (ch) {
            case '"': Append("\\\""); break;
            case '\\': Append("\\\\"); break;
            default:
                if (gsl::narrow_cast<unsigned char>(ch) < ' ') {
                    Append(std::format("\\u{:04x}", gsl::narrow_cast<unsigned char>(ch)));
                } else {
                    Append(std::string_view(&ch, 1));
                }
            }
        }
        Append("\"");
    }
    template<size_t N> void Text(const std::array<char, N>& value) {
        const auto end = std::find(value.begin(), value.end(), '\0');
        Check(end == value.end() ? ERROR_INVALID_DATA : ERROR_SUCCESS);
        String(std::string_view(value.begin(), end));
    }
    std::string bytes;
private:
    size_t limit_;
};

std::string InstanceAlias(size_t index) { return std::format("instance-{}", index + 1); }
size_t InstanceIndex(std::span<const MetricProcessInstance> instances, DWORD pid, const GUID& id) {
    const auto found = std::find_if(instances.begin(), instances.end(), [&](const MetricProcessInstance& instance) noexcept {
        return instance.pid == pid && instance.metadata.instance_id == id;
    });
    Check(found == instances.end() ? ERROR_NOT_FOUND : ERROR_SUCCESS);
    return gsl::narrow_cast<size_t>(found - instances.begin());
}
void WriteDiagnostics(JsonWriter& json, const metrics::DiagnosticCounts& counts) {
    json.Append("{\"protocol\":"); json.Number(counts.protocol);
    json.Append(",\"clock\":"); json.Number(counts.clock);
    json.Append(",\"identity\":"); json.Number(counts.identity);
    json.Append(",\"write\":"); json.Number(counts.write);
    json.Append(",\"interrupted\":"); json.Number(counts.interrupted);
    json.Append("}");
}
void WriteIntegrity(JsonWriter& json, const MetricAggregationStatus& status) {
    const auto& queue = status.queue;
    json.Append("{\"incomplete\":"); json.Boolean(status.incomplete);
    json.Append(",\"scope\":\"capture\",\"no_detected_loss_is_not_a_completeness_guarantee\":true,\"queue\":{");
    json.Append("\"active\":"); json.Boolean(queue.active);
    json.Append(",\"incomplete\":"); json.Boolean(queue.incomplete);
    json.Append(",\"generation\":"); json.Number(queue.capture_generation);
    json.Append(",\"records\":"); json.Number(queue.queued_records);
    json.Append(",\"bytes\":"); json.Number(queue.queued_bytes);
    json.Append(",\"dropped\":"); json.Number(queue.queue_dropped);
    json.Append(",\"decode_or_protocol_errors\":"); json.Number(queue.protocol_errors);
    json.Append(",\"generation_errors\":"); json.Number(queue.generation_errors);
    json.Append(",\"source_limit_errors\":"); json.Number(queue.source_limit_errors);
    json.Append(",\"allocation_errors\":"); json.Number(queue.allocation_errors);
    json.Append(",\"capture_errors\":"); json.Number(queue.capture_errors);
    json.Append(",\"etw_events_lost\":"); json.Number(queue.events_lost);
    json.Append(",\"etw_buffers_lost\":"); json.Number(queue.buffers_lost);
    json.Append("},\"aggregation\":{");
    json.Append("\"instances\":"); json.Number(status.instances);
    json.Append(",\"dimensions\":"); json.Number(status.dimensions);
    json.Append(",\"pending\":"); json.Number(status.pending);
    json.Append(",\"remembered_operations\":"); json.Number(status.remembered_operations);
    json.Append(",\"memory_bytes\":"); json.Number(status.memory_bytes);
    json.Append(",\"memory_limit_bytes\":"); json.Number(status.memory_limit);
    json.Append(",\"dropped\":"); json.Number(status.aggregation_dropped);
    json.Append(",\"pending_dropped\":"); json.Number(status.pending_dropped);
    json.Append(",\"protocol_errors\":"); json.Number(status.protocol_errors);
    json.Append(",\"generation_errors\":"); json.Number(status.generation_errors);
    json.Append(",\"duplicate_events\":"); json.Number(status.duplicate_events);
    json.Append(",\"unmatched_ends\":"); json.Number(status.unmatched_ends);
    json.Append(",\"error_codes_dropped\":"); json.Number(status.error_codes_dropped);
    json.Append(",\"allocation_errors\":"); json.Number(status.allocation_errors);
    json.Append("},\"provider_deltas\":"); WriteDiagnostics(json, status.provider_deltas);
    json.Append("}");
}
void WriteHistogram(JsonWriter& json, const MetricHistogram& histogram, uint64_t threshold) {
    json.Append("{\"samples\":"); json.Number(histogram.Samples());
    json.Append(",\"buckets\":[");
    bool first = true;
    for (const uint64_t count : histogram.Buckets()) {
        if (!first) json.Append(",");
        first = false;
        json.Number(count);
    }
    json.Append("],\"maximum\":{\"value_ns\":"); json.OptionalNumber(histogram.Maximum());
    json.Append(",\"state\":"); json.String(histogram.Samples() == 0 ? "no_samples" : "value");
    json.Append("},\"over_threshold\":{\"count\":");
    json.OptionalNumber(threshold != 0 && histogram.Samples() != 0 ? std::optional(histogram.OverThreshold()) : std::nullopt);
    json.Append(",\"state\":");
    json.String(threshold == 0 ? "not_configured" : histogram.Samples() == 0 ? "no_samples" : "value");
    json.Append("},\"quantiles\":{");
    for (size_t i = 0; i < kMetricPercentiles.size(); ++i) {
        if (i != 0) json.Append(",");
        const auto percentile = gsl::at(kMetricPercentiles, i);
        const auto value = histogram.Quantile(percentile);
        Check(value.state == QuantileState::InvalidPercentile ? ERROR_INVALID_DATA : ERROR_SUCCESS);
        json.String(std::format("p{}", percentile));
        json.Append(":{\"value_ns\":"); json.OptionalNumber(value.upper_ns);
        json.Append(",\"state\":");
        switch (value.state) {
        case QuantileState::Value: json.String("value"); break;
        case QuantileState::Overflow: json.String("overflow"); break;
        case QuantileState::NoSamples: json.String("no_samples"); break;
        case QuantileState::InsufficientSamples: json.String("insufficient_samples"); break;
        case QuantileState::InvalidPercentile: break;
        }
        json.Append(",\"samples\":"); json.Number(value.samples);
        json.Append("}");
    }
    json.Append("}}");
}
void WriteSummary(JsonWriter& json, const MetricSummary& summary) {
    Check(summary.cumulative && summary.observed_terminals > summary.intents ? ERROR_INVALID_DATA : ERROR_SUCCESS);
    json.Append("{\"intents\":"); json.Number(summary.intents);
    json.Append(",\"observed_terminals\":"); json.Number(summary.observed_terminals);
    json.Append(",\"pending_current\":"); json.Number(summary.pending);
    json.Append(",\"outcomes\":{");
    for (size_t i = 0; i < kOutcomeNames.size(); ++i) {
        if (i != 0) json.Append(",");
        json.String(gsl::at(kOutcomeNames, i));
        json.Append(":"); json.Number(gsl::at(summary.outcomes, i));
    }
    json.Append("},\"coverage\":{\"value\":"); json.OptionalNumber(summary.Coverage());
    json.Append(",\"numerator\":"); json.OptionalNumber(summary.cumulative ? std::optional(summary.observed_terminals) : std::nullopt);
    json.Append(",\"denominator\":"); json.OptionalNumber(summary.cumulative ? std::optional(summary.intents) : std::nullopt);
    json.Append(",\"state\":"); json.String(!summary.cumulative ? "not_applicable" : summary.intents == 0 ? "no_denominator" : "value");
    json.Append("},\"failure_rate\":{\"value\":"); json.OptionalNumber(summary.FailureRate());
    json.Append(",\"numerator\":"); json.Number(summary.Count(metrics::Outcome::Failure));
    json.Append(",\"denominator\":");
    uint64_t denominator = 0;
    Check(summary.FailureDenominator(denominator));
    json.Number(denominator);
    json.Append(",\"state\":"); json.String(denominator == 0 ? "no_denominator" : "value");
    json.Append("},\"errors\":[");
    Check(summary.error_count > summary.errors.size() ? ERROR_INVALID_DATA : ERROR_SUCCESS);
    bool first = true;
    for (const auto& error : std::span(summary.errors).first(summary.error_count)) {
        if (!first) json.Append(",");
        first = false;
        json.Append("{\"domain\":"); json.String(error.domain == metrics::ErrorDomain::HResult ? "hresult" : "win32");
        json.Append(",\"code\":"); json.Number(error.code);
        json.Append(",\"count\":"); json.Number(error.count);
        json.Append("}");
    }
    json.Append("],\"other_errors\":"); json.Number(summary.other_errors);
    json.Append("}");
}
}

bool MetricExportSelection::Valid() const noexcept {
    return (window == MetricView::Cumulative || window == MetricView::Recent) &&
        (!instance || (instance->pid != 0 && instance->instance_id != GUID_NULL)) &&
        (!metric || (*metric >= metrics::Metric::FocusTotal && *metric <= metrics::Metric::Cleanup)) &&
        (!outcome || *outcome <= metrics::Outcome::Rejected) &&
        (!mode || *mode <= metrics::Mode::FocusLost);
}
bool MetricExportSelection::MatchesDimension(const MetricDimension& dimension) const noexcept {
    return (!instance || (dimension.pid == instance->pid && dimension.instance_id == instance->instance_id)) &&
        (!metric || dimension.metric == *metric) && (!mode || dimension.mode == *mode);
}
bool MetricExportSelection::MatchesSummary(const MetricSummary& summary) const noexcept {
    return !outcome || summary.Count(*outcome) != 0;
}
ULONG SelectMetricTiming(const MetricSummary& summary, std::optional<metrics::Outcome> outcome, MetricHistogram& timing) noexcept {
    if (outcome && *outcome > metrics::Outcome::Rejected) return ERROR_INVALID_PARAMETER;
    MetricHistogram selected;
    if (!outcome || *outcome == metrics::Outcome::Success) selected = summary.success;
    if (!outcome || *outcome == metrics::Outcome::Failure) {
        const ULONG error = selected.Merge(summary.failure);
        if (error != ERROR_SUCCESS) return error;
    }
    timing = selected;
    return ERROR_SUCCESS;
}
ULONG ExportMetrics(const MetricAggregator& aggregator, const MetricExportSelection& selection,
    std::string& bytes, size_t max_bytes) noexcept {
    if (!selection.Valid() || max_bytes == 0 || max_bytes > kMetricJsonMaxBytes) return ERROR_INVALID_PARAMETER;
    try {
        const auto status = aggregator.Status();
        const auto instances = aggregator.Instances();
        JsonWriter json(max_bytes);
        json.Append("{\"format\":\"qiqiime.metrics\",\"format_version\":"); json.Number(kMetricJsonFormatVersion);
        json.Append(",\"tool\":{\"version\":"); json.String(version::kFullVersion);
        json.Append(",\"architecture\":"); json.String(version::kArch);
        json.Append("},\"protocol_version\":"); json.Number(metrics::kSchemaVersion);
        json.Append(",\"privacy\":{\"instance_identity\":\"capture_scoped_alias\",\"contains_raw_logs\":false,"
            "\"contains_process_names\":false,\"contains_pid_or_guids\":false,\"contains_input_or_paths\":false}");
        json.Append(",\"capture\":{\"generation\":"); json.Number(status.capture_generation);
        json.Append(",\"active\":"); json.Boolean(status.active);
        json.Append(",\"observed_start_receive_tick_ms\":"); json.OptionalNumber(status.capture_start_tick_ms);
        json.Append(",\"observed_stop_receive_tick_ms\":"); json.OptionalNumber(status.capture_stop_tick_ms);
        json.Append(",\"snapshot_receive_tick_ms\":"); json.OptionalNumber(status.latest_tick_ms);
        json.Append(",\"statistics_start_receive_tick_ms\":"); json.OptionalNumber(status.statistics_start_tick_ms);
        json.Append("},\"window\":{\"kind\":"); json.String(selection.window == MetricView::Cumulative ? "cumulative" : "recent");
        json.Append(",\"clock\":\"receive_tick_ms\",\"duration_seconds\":");
        json.OptionalNumber(selection.window == MetricView::Recent ? std::optional(kMetricWindowSeconds) : std::nullopt);
        json.Append(",\"recent_bucket_rule\":\"current_second_and_previous_59\",\"pending_scope\":\"current\","
            "\"coverage_scope\":\"cumulative_only\",\"frozen\":"); json.Boolean(status.capture_generation != 0 && !status.active);
        json.Append("},\"filters\":{\"instance\":");
        if (selection.instance) json.String(InstanceAlias(InstanceIndex(instances, selection.instance->pid, selection.instance->instance_id)));
        else json.Append("null");
        json.Append(",\"metric\":");
        if (selection.metric) json.String(gsl::at(kMetricNames, std::to_underlying(*selection.metric) - 1));
        else json.Append("null");
        json.Append(",\"outcome\":");
        if (selection.outcome) json.String(gsl::at(kOutcomeNames, std::to_underlying(*selection.outcome)));
        else json.Append("null");
        json.Append(",\"mode\":");
        if (selection.mode) json.String(gsl::at(kModeNames, std::to_underlying(*selection.mode)));
        else json.Append("null");
        json.Append(",\"counts_scope\":\"full_matching_dimension\",\"timing_outcome\":");
        json.String(selection.outcome ? gsl::at(kOutcomeNames, std::to_underlying(*selection.outcome)) : "success_and_failure");
        json.Append("},\"histogram_definition\":{\"unit\":\"ns\",\"bucket_upper_bounds_ns\":[");
        for (size_t i = 0; i < kMetricBucketCount; ++i) {
            if (i != 0) json.Append(",");
            json.OptionalNumber(MetricHistogram::UpperBound(i));
        }
        json.Append("],\"last_bucket\":\"overflow\",\"zero_bucket\":0,\"quantile_method\":\"nearest_rank_bucket_upper_bound\","
            "\"quantile_minimum_samples\":{\"p50\":1,\"p95\":"); json.Number(kMetricP95MinimumSamples);
        json.Append(",\"p99\":"); json.Number(kMetricP99MinimumSamples);
        json.Append("}},\"thresholds_ns\":{");
        for (size_t i = 0; i < kMetricNames.size(); ++i) {
            if (i != 0) json.Append(",");
            json.String(gsl::at(kMetricNames, i)); json.Append(":");
            const auto threshold = MetricThresholdNs(gsl::narrow_cast<metrics::Metric>(i + 1));
            json.OptionalNumber(threshold != 0 ? std::optional(threshold) : std::nullopt);
        }
        json.Append("},\"instances\":[");
        for (size_t i = 0; i < instances.size(); ++i) {
            if (i != 0) json.Append(",");
            const auto& metadata = gsl::at(instances, i).metadata;
            json.Append("{\"alias\":"); json.String(InstanceAlias(i));
            json.Append(",\"build_version\":"); json.Text(metadata.build_version);
            json.Append(",\"architecture\":"); json.Text(metadata.architecture);
            json.Append(",\"schema_version\":"); json.Number(metadata.schema_version);
            json.Append(",\"provider_generation\":"); json.Number(metadata.capture_generation);
            json.Append(",\"provider_lifetime_diagnostics\":"); WriteDiagnostics(json, metadata.diagnostics);
            json.Append("}");
        }
        json.Append("],\"integrity\":"); WriteIntegrity(json, status);
        json.Append(",\"dimensions\":[");
        bool first = true;
        for (const auto& dimension : aggregator.Dimensions()) {
            if (!selection.MatchesDimension(dimension)) continue;
            MetricSummary summary;
            Check(aggregator.Snapshot(dimension, selection.window, summary));
            if (!selection.MatchesSummary(summary)) continue;
            MetricHistogram timing;
            Check(SelectMetricTiming(summary, selection.outcome, timing));
            if (!first) json.Append(",");
            first = false;
            json.Append("{\"instance\":"); json.String(InstanceAlias(InstanceIndex(instances, dimension.pid, dimension.instance_id)));
            json.Append(",\"service_id\":"); json.Number(dimension.service_id);
            json.Append(",\"metric\":"); json.String(gsl::at(kMetricNames, std::to_underlying(dimension.metric) - 1));
            json.Append(",\"mode\":"); json.String(gsl::at(kModeNames, std::to_underlying(dimension.mode)));
            json.Append(",\"source\":"); json.String(gsl::at(kSourceNames, std::to_underlying(dimension.source)));
            json.Append(",\"counts\":"); WriteSummary(json, summary);
            const auto threshold = MetricThresholdNs(dimension.metric);
            json.Append(",\"success_timing\":"); WriteHistogram(json, summary.success, threshold);
            json.Append(",\"failure_timing\":"); WriteHistogram(json, summary.failure, threshold);
            json.Append(",\"selected_timing\":"); WriteHistogram(json, timing, threshold);
            json.Append("}");
        }
        json.Append("]}\n");
        bytes.swap(json.bytes);
        return ERROR_SUCCESS;
    } catch (const JsonError& error) { return error.code; }
    catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
}
} // namespace qiqi::monitor
