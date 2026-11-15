// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/test_support.h"
#include "common/RAII.h"
#include "tools/logmonitor/metric.h"
#include <cstring>
#include <filesystem>
#include <optional>

namespace qiqi::monitor::tests {
inline void FlushToolWindowLayout(HWND window) noexcept {
    MSG message{};
    while (::PeekMessageW(&message, window, WM_SIZE, WM_SIZE, PM_REMOVE))
        ::DispatchMessageW(&message);
}

inline std::optional<RECT> ClippedControlBounds(HWND child) {
    RECT bounds{};
    QIQI_TEST_CHECK(::GetWindowRect(child, &bounds));
    const ScopedGdiHandle<HRGN> region(::CreateRectRgn(0, 0, 0, 0));
    QIQI_TEST_CHECK(region && ::GetWindowRgn(child, region.Get()) != ERROR);
    RECT clipped{};
    QIQI_TEST_CHECK(::GetRgnBox(region.Get(), &clipped) != ERROR);
    ::OffsetRect(&clipped, bounds.left, bounds.top);
    if (!::IntersectRect(&bounds, &bounds, &clipped)) return std::nullopt;
    return bounds;
}
}

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
