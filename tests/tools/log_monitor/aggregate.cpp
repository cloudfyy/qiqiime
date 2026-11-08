#include "common/test_support.h"
#include "support.h"
#include "tools/log_monitor/aggregate.h"
#include "tools/log_monitor/collector.h"
#include <algorithm>
#include <limits>
#include <thread>
#include <vector>

namespace qiqi::monitor::metric_tests {
namespace {
MetricRecord Sample(uint64_t operation, uint64_t tick = 0, metrics::Outcome outcome = metrics::Outcome::Success,
    uint64_t duration = 1) noexcept {
    auto record = Record();
    record.received_tick_ms = tick;
    auto& event = std::get<metrics::Event>(record.payload);
    event.operation_id = operation;
    event.outcome = outcome;
    event.duration_ns = duration;
    return record;
}
MetricDimension Key(const MetricRecord& record) noexcept {
    const auto& event = std::get<metrics::Event>(record.payload);
    return {record.pid, event.instance_id, event.service_id, event.metric, event.mode, event.source};
}
MetricRecord Begin(uint64_t operation, uint64_t tick = 0) noexcept {
    auto record = Sample(operation, tick);
    auto& event = std::get<metrics::Event>(record.payload);
    event.phase = metrics::Phase::Begin;
    event.outcome = metrics::Outcome::Unspecified;
    event.duration_ns.reset();
    return record;
}
MetricRecord End(uint64_t operation, uint64_t tick = 0, metrics::Outcome outcome = metrics::Outcome::Success) noexcept {
    auto record = Sample(operation, tick, outcome);
    std::get<metrics::Event>(record.payload).phase = metrics::Phase::End;
    return record;
}
void Start(MetricAggregator& aggregator) {
    QIQI_TEST_CHECK(aggregator.BeginCapture(1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(Record(true)) == ERROR_SUCCESS);
}

void TestHistogram() {
    std::vector<MetricHistogram> storage(6);
    const gsl::not_null<MetricHistogram*> histogram(&gsl::at(storage, 0));
    QIQI_TEST_CHECK(histogram->Quantile(50).state == QuantileState::NoSamples);
    QIQI_TEST_CHECK(!histogram->Maximum());
    QIQI_TEST_CHECK(histogram->Quantile(98).state == QuantileState::InvalidPercentile);
    QIQI_TEST_CHECK(MetricHistogram::UpperBound(0) == 0 && MetricHistogram::UpperBound(1) == 1);
    QIQI_TEST_CHECK(MetricHistogram::UpperBound(510) == kMetricHistogramLimitNs);
    QIQI_TEST_CHECK(!MetricHistogram::UpperBound(511) && !MetricHistogram::UpperBound(512));
    for (size_t bucket = 0; bucket < 511; ++bucket) {
        const auto bound = MetricHistogram::UpperBound(bucket);
        QIQI_TEST_CHECK(bound);
        if (bucket) QIQI_TEST_CHECK(*bound > *MetricHistogram::UpperBound(bucket - 1));
        QIQI_TEST_CHECK(histogram->Observe(*bound) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(gsl::at(histogram->Buckets(), bucket) == 1);
    }
    QIQI_TEST_CHECK(histogram->Observe(kMetricHistogramLimitNs + 1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(gsl::at(histogram->Buckets(), 511) == 1);
    QIQI_TEST_CHECK(histogram->Maximum() == kMetricHistogramLimitNs + 1);
    const gsl::not_null<MetricHistogram*> limits(&gsl::at(storage, 1));
    for (size_t count = 1; count <= 1000; ++count) {
        QIQI_TEST_CHECK(limits->Observe(10) == ERROR_SUCCESS);
        if (count == 99) QIQI_TEST_CHECK(limits->Quantile(95).state == QuantileState::InsufficientSamples);
        if (count == 100) QIQI_TEST_CHECK(limits->Quantile(95).state == QuantileState::Value);
        if (count == 999) QIQI_TEST_CHECK(limits->Quantile(99).state == QuantileState::InsufficientSamples);
    }
    QIQI_TEST_CHECK(limits->Quantile(99).state == QuantileState::Value && limits->Quantile(99).upper_ns == 10);
    std::vector<uint64_t> exact;
    const gsl::not_null<MetricHistogram*> distribution(&gsl::at(storage, 2));
    for (uint64_t index = 0; index < 1000; ++index) {
        const uint64_t value = index < 500 ? 0 : index < 950 ? 2'345'678 : (std::numeric_limits<uint64_t>::max)();
        exact.push_back(value);
        QIQI_TEST_CHECK(distribution->Observe(value) == ERROR_SUCCESS);
    }
    std::sort(exact.begin(), exact.end());
    for (const unsigned int percentile : {50u, 95u, 99u}) {
        const size_t rank = (exact.size() * percentile + 99) / 100;
        const uint64_t value = gsl::at(exact, rank - 1);
        const auto quantile = distribution->Quantile(percentile);
        if (value > kMetricHistogramLimitNs) QIQI_TEST_CHECK(quantile.state == QuantileState::Overflow && !quantile.upper_ns);
        else {
            QIQI_TEST_CHECK(quantile.state == QuantileState::Value && quantile.upper_ns && *quantile.upper_ns >= value);
            size_t bucket = 0;
            while (MetricHistogram::UpperBound(bucket) != quantile.upper_ns) ++bucket;
            if (bucket) QIQI_TEST_CHECK(*MetricHistogram::UpperBound(bucket - 1) < value);
        }
    }
    QIQI_TEST_CHECK(distribution->Maximum() == (std::numeric_limits<uint64_t>::max)());
    const gsl::not_null<MetricHistogram*> first(&gsl::at(storage, 3));
    const gsl::not_null<MetricHistogram*> second(&gsl::at(storage, 4));
    for (int count = 0; count < 100; ++count) {
        QIQI_TEST_CHECK(first->Observe(0) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(second->Observe(10'000'000) == ERROR_SUCCESS);
    }
    const auto upper = second->Quantile(95).upper_ns;
    QIQI_TEST_CHECK(first->Merge(*second) == ERROR_SUCCESS && first->Quantile(95).upper_ns == upper);
    const gsl::not_null<MetricHistogram*> thresholds(&gsl::at(storage, 5));
    QIQI_TEST_CHECK(thresholds->Observe(5'000'000, 5'000'000) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(thresholds->Observe(5'000'001, 5'000'000) == ERROR_SUCCESS && thresholds->OverThreshold() == 1);
}

void TestOutcomesAndWindow() {
    MetricAggregator aggregator;
    QIQI_TEST_CHECK(aggregator.Status().memory_bytes == 0);
    Start(aggregator);
    const auto key = Key(Sample(1));
    for (uint64_t operation = 1; operation <= 1000; ++operation) {
        QIQI_TEST_CHECK(aggregator.Consume(Sample(operation, 0, metrics::Outcome::Success, operation)) == ERROR_SUCCESS);
    }
    auto failed = Sample(1001, 1000, metrics::Outcome::Failure, 9'876'543);
    auto& failure = std::get<metrics::Event>(failed.payload);
    failure.error_domain = metrics::ErrorDomain::HResult;
    failure.error_code = gsl::narrow_cast<uint32_t>(E_ACCESSDENIED);
    QIQI_TEST_CHECK(aggregator.Consume(failed) == ERROR_SUCCESS);
    for (const auto outcome : {metrics::Outcome::Cancelled, metrics::Outcome::Stale, metrics::Outcome::Exception,
        metrics::Outcome::Unhandled, metrics::Outcome::Rejected, metrics::Outcome::Incomplete}) {
        QIQI_TEST_CHECK(aggregator.Consume(Sample(1002ULL + std::to_underlying(outcome), 2000, outcome)) == ERROR_SUCCESS);
    }
    auto summary = aggregator.Snapshot(key, MetricView::Cumulative);
    QIQI_TEST_CHECK(summary && summary->intents == 1007 && summary->observed_terminals == 1006 && summary->pending == 0);
    QIQI_TEST_CHECK(summary->success.Samples() == 1000 && summary->success.Maximum() == 1000);
    QIQI_TEST_CHECK(summary->success.Quantile(99).state == QuantileState::Value && summary->failure.Samples() == 1);
    QIQI_TEST_CHECK(summary->error_count == 1 && gsl::at(summary->errors, 0).code == gsl::narrow_cast<uint32_t>(E_ACCESSDENIED));
    QIQI_TEST_CHECK(summary->FailureRate() && *summary->FailureRate() == 1.0 / 1001.0);
    QIQI_TEST_CHECK(summary->Coverage() && *summary->Coverage() == 1006.0 / 1007.0);
    QIQI_TEST_CHECK(aggregator.Advance(59'999) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Snapshot(key, MetricView::Recent)->success.Samples() == 1000);
    QIQI_TEST_CHECK(aggregator.Advance(60'000) == ERROR_SUCCESS);
    auto recent = aggregator.Snapshot(key, MetricView::Recent);
    QIQI_TEST_CHECK(recent && recent->success.Samples() == 0 && recent->failure.Samples() == 1 && !recent->Coverage());
    QIQI_TEST_CHECK(aggregator.Advance(61'000) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Snapshot(key, MetricView::Recent)->failure.Samples() == 0);
    QIQI_TEST_CHECK(aggregator.Consume(Sample(2000, 0)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Snapshot(key, MetricView::Recent)->success.Samples() == 0);
    QIQI_TEST_CHECK(aggregator.Snapshot(key, MetricView::Cumulative)->success.Samples() == 1001);
    QIQI_TEST_CHECK(aggregator.Advance(60'999) == ERROR_INVALID_DATA && aggregator.Status().incomplete);
    QIQI_TEST_CHECK(aggregator.Stop(61'000) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Advance(999'000) == ERROR_INVALID_STATE);
    QIQI_TEST_CHECK(aggregator.Snapshot(key, MetricView::Cumulative)->success.Samples() == 1001);
    QIQI_TEST_CHECK(aggregator.BeginCapture(2) == ERROR_SUCCESS && !aggregator.Status().incomplete);
    QIQI_TEST_CHECK(!aggregator.Snapshot(key, MetricView::Cumulative));
    QIQI_TEST_CHECK(aggregator.Consume(Sample(1)) == ERROR_REVISION_MISMATCH);
    std::vector<MetricSummary> summaries(3);
    auto& merged = gsl::at(summaries, 0);
    auto& other = gsl::at(summaries, 1);
    merged.intents = 100;
    merged.observed_terminals = 100;
    gsl::at(merged.outcomes, std::to_underlying(metrics::Outcome::Success)) = 100;
    other.intents = 10;
    other.observed_terminals = 10;
    gsl::at(other.outcomes, std::to_underlying(metrics::Outcome::Failure)) = 10;
    QIQI_TEST_CHECK(merged.Merge(other) == ERROR_SUCCESS && merged.intents == 110 &&
        merged.FailureRate() && *merged.FailureRate() == 10.0 / 110.0);
    auto& overflow = gsl::at(summaries, 2);
    overflow.intents = (std::numeric_limits<uint64_t>::max)();
    QIQI_TEST_CHECK(merged.Merge(overflow) == ERROR_ARITHMETIC_OVERFLOW && merged.intents == 110);
}

void TestCorrelation() {
    MetricAggregator aggregator;
    Start(aggregator);
    const auto key = Key(Sample(1));
    QIQI_TEST_CHECK(aggregator.Consume(Begin(1)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(Begin(1)) == ERROR_ALREADY_EXISTS);
    QIQI_TEST_CHECK(aggregator.Consume(End(1, 1000, metrics::Outcome::Cancelled)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(End(1, 1001)) == ERROR_ALREADY_EXISTS);
    QIQI_TEST_CHECK(aggregator.Consume(Sample(2, 2000)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(Sample(2, 2001)) == ERROR_ALREADY_EXISTS);
    QIQI_TEST_CHECK(aggregator.Consume(End(3, 3000)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(Begin(3, 3001)) == ERROR_ALREADY_EXISTS);
    QIQI_TEST_CHECK(aggregator.Consume(Begin(4, 4000)) == ERROR_SUCCESS);
    auto wrong = End(4, 4001);
    std::get<metrics::Event>(wrong.payload).mode = metrics::Mode::Popup;
    QIQI_TEST_CHECK(aggregator.Consume(wrong) == ERROR_INVALID_DATA);
    QIQI_TEST_CHECK(aggregator.Consume(Sample(4, 4002)) == ERROR_INVALID_DATA);
    QIQI_TEST_CHECK(aggregator.Status().pending == 1);
    QIQI_TEST_CHECK(aggregator.Stop(5000) == ERROR_SUCCESS);
    const auto summary = aggregator.Snapshot(key, MetricView::Cumulative);
    QIQI_TEST_CHECK(summary && summary->intents == 4 && summary->pending == 0 && summary->observed_terminals == 2);
    QIQI_TEST_CHECK(summary->Count(metrics::Outcome::Cancelled) == 1 && summary->Count(metrics::Outcome::Incomplete) == 2);
    QIQI_TEST_CHECK(summary->success.Samples() == 1 && aggregator.Status().duplicate_events == 4 &&
        aggregator.Status().unmatched_ends == 1);

    MetricAggregator transitions;
    Start(transitions);
    QIQI_TEST_CHECK(transitions.Consume(Begin(1)) == ERROR_SUCCESS);
    auto next = Record(true);
    std::get<MetricInstance>(next.payload).capture_generation = 8;
    QIQI_TEST_CHECK(transitions.Consume(next) == ERROR_SUCCESS && transitions.Status().pending == 0);
    QIQI_TEST_CHECK(transitions.Consume(End(1)) == ERROR_NOT_READY);
    auto current = Sample(2);
    std::get<metrics::Event>(current.payload).capture_generation = 8;
    QIQI_TEST_CHECK(transitions.Consume(current) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(transitions.Consume(Begin(3)) == ERROR_NOT_READY);
    transitions.Clear();
    QIQI_TEST_CHECK(transitions.Status().dimensions == 0 && !transitions.Snapshot(key, MetricView::Cumulative));
    QIQI_TEST_CHECK(transitions.Consume(current) == ERROR_ALREADY_EXISTS);
    std::get<metrics::Event>(current.payload).operation_id = 4;
    QIQI_TEST_CHECK(transitions.Consume(current) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(transitions.Snapshot(key, MetricView::Cumulative)->intents == 1);
}

void TestLimitsAndBudget() {
    MetricAggregator pending;
    Start(pending);
    for (uint64_t operation = 1; operation <= 4096; ++operation) QIQI_TEST_CHECK(pending.Consume(Begin(operation)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(pending.Status().pending == 4096);
    QIQI_TEST_CHECK(pending.Consume(Begin(4097)) == ERROR_BUFFER_OVERFLOW && pending.Status().pending == 4096);
    QIQI_TEST_CHECK(pending.Status().pending_dropped == 1 && pending.Status().aggregation_dropped == 1);
    QIQI_TEST_CHECK(pending.Consume(End(1)) == ERROR_SUCCESS && pending.Consume(Begin(4097)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(pending.Stop(0) == ERROR_SUCCESS && pending.Status().pending == 0);
    QIQI_TEST_CHECK(pending.Snapshot(Key(Sample(1)), MetricView::Cumulative)->Count(metrics::Outcome::Incomplete) == 4096);

    MetricAggregator services;
    Start(services);
    for (uint64_t service = 1; service <= 17; ++service) {
        auto record = Sample(service);
        std::get<metrics::Event>(record.payload).service_id = service;
        const ULONG expected = service <= 16 ? ERROR_SUCCESS : ERROR_TOO_MANY_NAMES;
        QIQI_TEST_CHECK(services.Consume(record) == expected);
    }
    MetricAggregator dimensions;
    Start(dimensions);
    for (uint64_t index = 0; index < 17; ++index) {
        auto record = Sample(index + 1);
        auto& event = std::get<metrics::Event>(record.payload);
        event.metric = gsl::narrow_cast<metrics::Metric>(index / 5 + 1);
        event.mode = gsl::narrow_cast<metrics::Mode>(index % 5);
        const ULONG expected = index < 16 ? ERROR_SUCCESS : ERROR_TOO_MANY_NAMES;
        QIQI_TEST_CHECK(dimensions.Consume(record) == expected);
    }
    QIQI_TEST_CHECK(dimensions.Status().dimensions == 16);
    MetricAggregator instances;
    QIQI_TEST_CHECK(instances.BeginCapture(1) == ERROR_SUCCESS);
    for (DWORD pid = 1; pid <= 33; ++pid) {
        auto metadata = Record(true);
        metadata.pid = pid;
        const ULONG expected = pid <= 32 ? ERROR_SUCCESS : ERROR_TOO_MANY_NAMES;
        QIQI_TEST_CHECK(instances.Consume(metadata) == expected);
    }
    MetricAggregator probe;
    Start(probe);
    const size_t base = probe.Status().memory_bytes;
    QIQI_TEST_CHECK(base >= MetricAggregator::FixedBytes());
    MetricAggregator limited(base + 4096);
    Start(limited);
    const auto initial = limited.Status().memory_bytes;
    QIQI_TEST_CHECK(limited.Consume(Sample(1)) == ERROR_NOT_ENOUGH_MEMORY);
    QIQI_TEST_CHECK(limited.Status().aggregation_dropped == 1 && limited.Status().dimensions == 0 &&
        limited.Status().remembered_operations == 0 && limited.Status().memory_bytes == initial);
    QIQI_TEST_CHECK(limited.Status().memory_bytes <= limited.Status().memory_limit);
    MetricAggregator full;
    Start(full);
    ULONG result = ERROR_SUCCESS;
    for (uint64_t index = 0; index < 8192 && result == ERROR_SUCCESS; ++index) {
        const DWORD pid = gsl::narrow_cast<DWORD>(index / 256 + 1);
        if (index % 256 == 0) {
            auto metadata = Record(true);
            metadata.pid = pid;
            QIQI_TEST_CHECK(full.Consume(metadata) == ERROR_SUCCESS);
        }
        auto record = Sample(index + 1);
        record.pid = pid;
        auto& event = std::get<metrics::Event>(record.payload);
        event.service_id = index % 256 / 16 + 1;
        event.metric = gsl::narrow_cast<metrics::Metric>(index % 16 / 5 + 1);
        event.mode = gsl::narrow_cast<metrics::Mode>(index % 16 % 5);
        result = full.Consume(record);
        if (result == ERROR_SUCCESS) {
            for (uint64_t second = 1; second < 60 && result == ERROR_SUCCESS; ++second) {
                record.received_tick_ms = second * 1000;
                event.operation_id = 8192 + index * 60 + second;
                result = full.Consume(record);
            }
        }
        QIQI_TEST_CHECK(full.Status().memory_bytes <= kMetricAggregationBudget);
    }
    QIQI_TEST_CHECK(result == ERROR_NOT_ENOUGH_MEMORY && full.Status().aggregation_dropped != 0 && full.Status().incomplete);
}

void TestDiagnosticsAndDrain() {
    MetricAggregator errors;
    Start(errors);
    for (uint32_t code = 1; code <= 33; ++code) {
        auto record = Sample(code, 0, metrics::Outcome::Failure);
        auto& event = std::get<metrics::Event>(record.payload);
        event.error_domain = metrics::ErrorDomain::Win32;
        event.error_code = code;
        QIQI_TEST_CHECK(errors.Consume(record) == ERROR_SUCCESS);
    }
    const auto summary = errors.Snapshot(Key(Sample(1)), MetricView::Cumulative);
    QIQI_TEST_CHECK(summary && summary->failure.Samples() == 33 && summary->error_count == 32 && summary->other_errors == 1);
    QIQI_TEST_CHECK(errors.Status().error_codes_dropped == 1 && errors.Status().incomplete);
    MetricAggregator diagnostics;
    auto initial = Record(true);
    std::get<MetricInstance>(initial.payload).diagnostics.protocol = 100;
    QIQI_TEST_CHECK(diagnostics.BeginCapture(1) == ERROR_SUCCESS && diagnostics.Consume(initial) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(diagnostics.Status().provider_deltas.protocol == 0);
    auto update = initial;
    std::get<MetricInstance>(update.payload).diagnostics.protocol = 103;
    QIQI_TEST_CHECK(diagnostics.Consume(update) == ERROR_SUCCESS && diagnostics.Status().provider_deltas.protocol == 3);
    QIQI_TEST_CHECK(diagnostics.Consume(update) == ERROR_SUCCESS && diagnostics.Status().provider_deltas.protocol == 3);
    QIQI_TEST_CHECK(diagnostics.Consume(initial) == ERROR_INVALID_DATA);
    MetricAggregator overflow;
    Start(overflow);
    auto maximum = Record(true);
    std::get<MetricInstance>(maximum.payload).diagnostics.protocol = (std::numeric_limits<uint64_t>::max)();
    QIQI_TEST_CHECK(overflow.Consume(maximum) == ERROR_SUCCESS);
    auto another = Record(true);
    another.pid = 2;
    QIQI_TEST_CHECK(overflow.Consume(another) == ERROR_SUCCESS);
    std::get<MetricInstance>(another.payload).diagnostics.protocol = 1;
    QIQI_TEST_CHECK(overflow.Consume(another) == ERROR_ARITHMETIC_OVERFLOW);
    QIQI_TEST_CHECK(overflow.Status().provider_deltas.protocol == (std::numeric_limits<uint64_t>::max)());

    MetricQueue queue;
    MetricAggregator aggregator;
    Diagnostics monitor;
    QIQI_TEST_CHECK(queue.BeginCapture(1) == ERROR_SUCCESS && queue.Push(Record(true)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Begin(1)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Sample(2)) == ERROR_SUCCESS);
    DrainMetrics(queue, aggregator, monitor, 0);
    QIQI_TEST_CHECK(aggregator.Status().pending == 1 && aggregator.Snapshot(Key(Sample(1)), MetricView::Cumulative)->success.Samples() == 1);
    EventQueue logs;
    Event log;
    logs.Push(log);
    QIQI_TEST_CHECK(logs.Take().size() == 1);
    queue.RecordLoss(1, 2);
    queue.EndCapture();
    DrainMetrics(queue, aggregator, monitor, 1000);
    QIQI_TEST_CHECK(!aggregator.Status().active && aggregator.Status().pending == 0 && aggregator.Status().incomplete);
    QIQI_TEST_CHECK(aggregator.Status().queue.events_lost == 1 && aggregator.Status().queue.buffers_lost == 2);
    QIQI_TEST_CHECK(aggregator.Snapshot(Key(Sample(1)), MetricView::Cumulative)->Count(metrics::Outcome::Incomplete) == 1);
    const auto frozen = aggregator.Snapshot(Key(Sample(1)), MetricView::Recent)->success.Samples();
    DrainMetrics(queue, aggregator, monitor, 999'000);
    QIQI_TEST_CHECK(aggregator.Snapshot(Key(Sample(1)), MetricView::Recent)->success.Samples() == frozen);
    aggregator.Clear();
    QIQI_TEST_CHECK(aggregator.Status().incomplete && aggregator.Status().queue.events_lost == 1);
    QIQI_TEST_CHECK(aggregator.Status().statistics_start_tick_ms == 1000);

    MetricQueue concurrent(2000, 4 * 1024 * 1024);
    MetricAggregator streaming;
    QIQI_TEST_CHECK(concurrent.BeginCapture(1) == ERROR_SUCCESS && concurrent.Push(Record(true)) == ERROR_SUCCESS);
    std::jthread producer([&]() {
        for (uint64_t operation = 1; operation <= 1000; ++operation) {
            QIQI_TEST_CHECK(concurrent.Push(Sample(operation, ::GetTickCount64())) == ERROR_SUCCESS);
            std::this_thread::yield();
        }
        concurrent.EndCapture();
    });
    do {
        DrainMetrics(concurrent, streaming, monitor, ::GetTickCount64());
        std::this_thread::yield();
    } while (concurrent.Status().active || concurrent.Status().queued_records != 0);
    producer.join();
    DrainMetrics(concurrent, streaming, monitor, ::GetTickCount64());
    QIQI_TEST_CHECK(!streaming.Status().active && !streaming.Status().incomplete);
    QIQI_TEST_CHECK(streaming.Snapshot(Key(Sample(1)), MetricView::Cumulative)->success.Samples() == 1000);
}
} // namespace
void RunAggregationTests() {
    TestHistogram();
    TestOutcomesAndWindow();
    TestCorrelation();
    TestLimitsAndBudget();
    TestDiagnosticsAndDrain();
    std::cout << "[PASS] Metric histograms, receive windows, correlation and unified memory budget.\n";
}
} // namespace qiqi::monitor::metric_tests
