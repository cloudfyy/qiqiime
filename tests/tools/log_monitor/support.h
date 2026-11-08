#pragma once
#include "tools/log_monitor/metric.h"
#include <cstring>
#include <filesystem>

namespace qiqi::monitor::metric_tests {
inline constexpr GUID kInstance{1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};

inline metrics::Event Metric() noexcept {
    metrics::Event event;
    event.instance_id = kInstance;
    event.capture_generation = 7;
    event.service_id = 1;
    event.operation_id = 2;
    event.duration_ns = 0;
    event.outcome = metrics::Outcome::Success;
    return event;
}

inline MetricRecord Record(bool metadata = false) noexcept {
    MetricRecord record;
    record.capture_generation = 1;
    record.pid = 1;
    record.tid = 2;
    if (metadata) {
        MetricInstance instance;
        instance.instance_id = kInstance;
        instance.capture_generation = 7;
        std::memcpy(instance.build_version.data(), "test", 5);
        std::memcpy(instance.architecture.data(), "x64", 4);
        record.payload = instance;
    } else record.payload = Metric();
    return record;
}
void RunAggregationTests();
void RunMetricExportTests(const std::filesystem::path& fixtures);
} // namespace qiqi::monitor::metric_tests
