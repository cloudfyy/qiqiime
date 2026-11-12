// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/log_monitor/aggregate.h"
#include <string>

namespace qiqi::monitor {
inline constexpr uint16_t kMetricJsonFormatVersion = 1;
inline constexpr size_t kMetricJsonMaxBytes = 64 * 1024 * 1024;

struct MetricInstanceRef {
    DWORD pid = 0;
    GUID instance_id{};
};
struct MetricExportSelection {
    MetricView window = MetricView::Cumulative;
    std::optional<MetricInstanceRef> instance;
    std::optional<metrics::Metric> metric;
    std::optional<metrics::Outcome> outcome;
    std::optional<metrics::Mode> mode;
    bool Valid() const noexcept;
    bool MatchesDimension(const MetricDimension& dimension) const noexcept;
    bool MatchesSummary(const MetricSummary& summary) const noexcept;
};

ULONG SelectMetricTiming(const MetricSummary& summary, std::optional<metrics::Outcome> outcome,
    MetricHistogram& timing) noexcept;

// Own the serialized snapshot before opening modal dialogs. Failure leaves bytes unchanged.
ULONG ExportMetrics(const MetricAggregator& aggregator, const MetricExportSelection& selection,
    std::string& bytes, size_t max_bytes = kMetricJsonMaxBytes) noexcept;
} // namespace qiqi::monitor
