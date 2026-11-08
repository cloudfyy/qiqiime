#include "common/test_support.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"
#include "common/version.h"
#include "support.h"
#include "tools/log_monitor/export.h"
#include "tools/log_monitor/model.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>

namespace qiqi::monitor::metric_tests {
namespace {
constexpr DWORD kExportPid = 424242;
constexpr uint64_t kExportService = (std::numeric_limits<uint64_t>::max)();
constexpr GUID kSecondInstance{2, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};

MetricRecord ExportMetadata(DWORD pid = kExportPid, GUID id = kInstance) noexcept {
    auto record = Record(true);
    record.pid = pid;
    record.received_tick_ms = 1000;
    auto& metadata = std::get<MetricInstance>(record.payload);
    metadata.instance_id = id;
    constexpr std::string_view build = "test\"quoted\\version";
    std::copy(build.begin(), build.end(), metadata.build_version.begin());
    gsl::at(metadata.build_version, build.size()) = '\0';
    return record;
}
MetricRecord ExportSample(uint64_t operation, uint64_t tick, metrics::Outcome outcome = metrics::Outcome::Success,
    uint64_t duration = 0) noexcept {
    auto record = Record();
    record.pid = kExportPid;
    record.received_tick_ms = tick;
    auto& event = std::get<metrics::Event>(record.payload);
    event.service_id = kExportService;
    event.operation_id = operation;
    event.activity_id = kInstance;
    event.related_activity_id = kSecondInstance;
    event.outcome = outcome;
    event.duration_ns = duration;
    return record;
}
void SaveFixture(const std::filesystem::path& directory, const wchar_t* name, std::string_view bytes) {
    if (directory.empty()) return;
    const auto path = directory / name;
    QIQI_TEST_CHECK(!std::filesystem::exists(path));
    QIQI_TEST_CHECK(WriteExport(path.c_str(), bytes) == ERROR_SUCCESS);
}
}
void RunMetricExportTests(const std::filesystem::path& fixtures) {
    MetricAggregator aggregator;
    MetricExportSelection selection;
    std::string bytes;
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.starts_with("{\"format\":\"qiqiime.metrics\",\"format_version\":1,"));
    QIQI_TEST_CHECK(bytes.contains(std::format("\"version\":\"{}\"", version::kFullVersion)));
    QIQI_TEST_CHECK(bytes.contains("\"observed_start_receive_tick_ms\":null"));
    QIQI_TEST_CHECK(bytes.contains("\"observed_stop_receive_tick_ms\":null"));
    QIQI_TEST_CHECK(bytes.contains("\"snapshot_receive_tick_ms\":null"));
    QIQI_TEST_CHECK(bytes.ends_with("\"dimensions\":[]}\n"));
    SaveFixture(fixtures, L"empty.json", bytes);

    QIQI_TEST_CHECK(aggregator.BeginCapture(1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(ExportMetadata()) == ERROR_SUCCESS);
    for (uint64_t i = 0; i < 1000; ++i)
        QIQI_TEST_CHECK(aggregator.Consume(ExportSample(i + 1, 1000, metrics::Outcome::Success, i)) == ERROR_SUCCESS);
    auto failure = ExportSample(1001, 60'000, metrics::Outcome::Failure, kExportService);
    auto& failed_event = std::get<metrics::Event>(failure.payload);
    failed_event.error_domain = metrics::ErrorDomain::HResult;
    failed_event.error_code = gsl::narrow_cast<uint32_t>(E_ACCESSDENIED);
    QIQI_TEST_CHECK(aggregator.Consume(failure) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(ExportSample(1002, 60'000, metrics::Outcome::Cancelled)) == ERROR_SUCCESS);
    auto pending = ExportSample(1003, 60'000);
    auto& pending_event = std::get<metrics::Event>(pending.payload);
    pending_event.phase = metrics::Phase::Begin;
    pending_event.outcome = metrics::Outcome::Unspecified;
    pending_event.duration_ns.reset();
    QIQI_TEST_CHECK(aggregator.Consume(pending) == ERROR_SUCCESS);
    auto reload = ExportSample(1010, 60'000, metrics::Outcome::Success, 2);
    auto& reload_event = std::get<metrics::Event>(reload.payload);
    reload_event.metric = metrics::Metric::FocusReload;
    reload_event.mode = metrics::Mode::FocusGained;
    QIQI_TEST_CHECK(aggregator.Consume(reload) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Consume(ExportMetadata(kExportPid + 1, kSecondInstance)) == ERROR_SUCCESS);
    auto second = ExportSample(1, 60'000, metrics::Outcome::Success, 5);
    second.pid = kExportPid + 1;
    std::get<metrics::Event>(second.payload).instance_id = kSecondInstance;
    QIQI_TEST_CHECK(aggregator.Consume(second) == ERROR_SUCCESS);
    MetricQueueStatus queue;
    queue.capture_generation = 1;
    queue.active = true;
    queue.incomplete = true;
    queue.protocol_errors = 1;
    queue.allocation_errors = 2;
    queue.generation_errors = 3;
    queue.source_limit_errors = 4;
    queue.capture_errors = 5;
    queue.events_lost = 6;
    queue.buffers_lost = 7;
    queue.queue_dropped = 8;
    QIQI_TEST_CHECK(aggregator.UpdateQueueStatus(queue) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"intents\":1003,\"observed_terminals\":1002,\"pending_current\":1"));
    QIQI_TEST_CHECK(bytes.contains("\"success\":1000,\"failure\":1,\"cancelled\":1"));
    QIQI_TEST_CHECK(bytes.contains("\"maximum\":{\"value_ns\":18446744073709551615,\"state\":\"value\"}"));
    QIQI_TEST_CHECK(bytes.contains("\"service_id\":18446744073709551615"));
    QIQI_TEST_CHECK(bytes.contains("\"code\":2147942405,\"count\":1"));
    QIQI_TEST_CHECK(bytes.contains("\"build_version\":\"test\\\"quoted\\\\version\""));
    QIQI_TEST_CHECK(bytes.contains("\"etw_events_lost\":6,\"etw_buffers_lost\":7"));
    QIQI_TEST_CHECK(bytes.contains("\"decode_or_protocol_errors\":1"));
    QIQI_TEST_CHECK(!bytes.contains("\"pid\"") && !bytes.contains("\"instance_id\"") && !bytes.contains("\"activity_id\""));
    QIQI_TEST_CHECK(!bytes.contains("424242") && !bytes.contains("00000001-0002-0003"));
    SaveFixture(fixtures, L"cumulative.json", bytes);

    const auto stable = bytes;
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes, stable.size()) == ERROR_SUCCESS && bytes == stable);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes, stable.size() - 1) == ERROR_FILE_TOO_LARGE && bytes == stable);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes, 1) == ERROR_FILE_TOO_LARGE && bytes == stable);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes, 0) == ERROR_INVALID_PARAMETER && bytes == stable);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes, kMetricJsonMaxBytes + 1) == ERROR_INVALID_PARAMETER);
    selection.metric = gsl::narrow_cast<metrics::Metric>(0);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_INVALID_PARAMETER && bytes == stable);
    selection.metric.reset();
    selection.instance = MetricInstanceRef{1, kInstance};
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_NOT_FOUND && bytes == stable);
    selection.instance = MetricInstanceRef{kExportPid + 1, kSecondInstance};
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"filters\":{\"instance\":\"instance-2\""));
    QIQI_TEST_CHECK(bytes.contains("\"dimensions\":[{\"instance\":\"instance-2\""));
    SaveFixture(fixtures, L"instance-filter.json", bytes);
    selection.instance = MetricInstanceRef{kExportPid, kInstance};
    selection.metric = metrics::Metric::FocusTotal;
    selection.mode = metrics::Mode::Unspecified;
    selection.outcome = metrics::Outcome::Failure;
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"selected_timing\":{\"samples\":1"));
    QIQI_TEST_CHECK(bytes.contains("\"denominator\":1001"));
    QIQI_TEST_CHECK(bytes.contains("\"p50\":{\"value_ns\":null,\"state\":\"overflow\",\"samples\":1}"));
    QIQI_TEST_CHECK(bytes.contains("\"p95\":{\"value_ns\":null,\"state\":\"insufficient_samples\",\"samples\":1}"));
    SaveFixture(fixtures, L"failure-filter.json", bytes);
    selection.outcome = metrics::Outcome::Cancelled;
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"selected_timing\":{\"samples\":0"));
    QIQI_TEST_CHECK(bytes.contains("\"maximum\":{\"value_ns\":null,\"state\":\"no_samples\"}"));
    SaveFixture(fixtures, L"cancelled-filter.json", bytes);
    selection.outcome.reset();
    selection.window = MetricView::Recent;
    QIQI_TEST_CHECK(aggregator.Advance(61'000) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"coverage\":{\"value\":null,\"numerator\":null,\"denominator\":null,\"state\":\"not_applicable\"}"));
    QIQI_TEST_CHECK(bytes.contains("\"intents\":3,\"observed_terminals\":2,\"pending_current\":1"));
    SaveFixture(fixtures, L"recent.json", bytes);
    queue.active = false;
    QIQI_TEST_CHECK(aggregator.UpdateQueueStatus(queue) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(aggregator.Stop(62'000) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"observed_start_receive_tick_ms\":1000,\"observed_stop_receive_tick_ms\":62000"));
    QIQI_TEST_CHECK(bytes.contains("\"snapshot_receive_tick_ms\":62000"));
    QIQI_TEST_CHECK(bytes.contains("\"pending_current\":0"));
    QIQI_TEST_CHECK(bytes.contains("\"incomplete\":1"));
    SaveFixture(fixtures, L"stopped.json", bytes);
    const auto frozen = bytes;
    QIQI_TEST_CHECK(aggregator.Advance(120'000) == ERROR_INVALID_STATE);
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS && bytes == frozen);
    aggregator.Clear();
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.ends_with("\"dimensions\":[]}\n"));
    QIQI_TEST_CHECK(bytes.contains("\"statistics_start_receive_tick_ms\":62000"));
    QIQI_TEST_CHECK(bytes.contains("\"observed_start_receive_tick_ms\":1000,\"observed_stop_receive_tick_ms\":62000"));
    QIQI_TEST_CHECK(bytes.contains("\"etw_events_lost\":6,\"etw_buffers_lost\":7"));
    SaveFixture(fixtures, L"cleared.json", bytes);
    QIQI_TEST_CHECK(aggregator.BeginCapture(2) == ERROR_SUCCESS);
    selection.instance.reset();
    QIQI_TEST_CHECK(ExportMetrics(aggregator, selection, bytes) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes.contains("\"observed_start_receive_tick_ms\":null"));
    QIQI_TEST_CHECK(bytes.contains("\"etw_events_lost\":0,\"etw_buffers_lost\":0"));
    SaveFixture(fixtures, L"new-capture.json", bytes);

    const auto path = std::filesystem::temp_directory_path() /
        std::format(L"qiqi-metrics-export-{}-{}.json", ::GetCurrentProcessId(), ::GetTickCount64());
    QIQI_TEST_CHECK(!std::filesystem::exists(path));
    QIQI_TEST_CHECK(WriteExport(path.c_str(), frozen) == ERROR_SUCCESS);
    std::string saved;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, saved) && saved == frozen);
    {
        const ScopedFileHandle lock(::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(lock);
        QIQI_TEST_CHECK(WriteExport(path.c_str(), bytes) == ERROR_SHARING_VIOLATION);
    }
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, saved) && saved == frozen);
    QIQI_TEST_CHECK(std::filesystem::remove(path));
    std::cout << "[PASS] Versioned Metrics JSON, aliases, filters, exact integers, null states and checked I/O.\n";
}
} // namespace qiqi::monitor::metric_tests
