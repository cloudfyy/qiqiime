#include "common/test_support.h"
#include "common/etw.h"
#include "common/version.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/aggregate.h"
#include "support.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <limits>
#include <span>
#include <thread>

namespace {
using namespace qiqi;
using namespace qiqi::monitor;
using namespace qiqi::monitor::metric_tests;

struct Field {
    std::wstring name;
    USHORT type;
    std::vector<BYTE> bytes;
    PROPERTY_FLAGS flags = static_cast<PROPERTY_FLAGS>(0);
};
struct Fixture {
    std::wstring name = L"QiQiMetric";
    std::vector<Field> fields;
    ULONG information_error = ERROR_SUCCESS;
    ULONG property_error = ERROR_SUCCESS;
    ULONG property_size_error = ERROR_SUCCESS;
    ULONG declared_size = 0;
};
thread_local Fixture fixture;

template <typename T>
void Add(const wchar_t* name, USHORT type, const T& value) {
    const auto bytes = std::as_bytes(std::span(&value, 1));
    Field field{name, type, std::vector<BYTE>(bytes.size()), static_cast<PROPERTY_FLAGS>(0)};
    std::memcpy(field.bytes.data(), bytes.data(), bytes.size());
    fixture.fields.push_back(std::move(field));
}

std::vector<BYTE> Metadata() {
    constexpr size_t base = offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray);
    std::vector<BYTE> bytes((std::max)(sizeof(TRACE_EVENT_INFO), base + sizeof(EVENT_PROPERTY_INFO) * fixture.fields.size()));
    const auto text = [&](std::wstring_view value) {
        const ULONG offset = gsl::narrow_cast<ULONG>(bytes.size());
        bytes.resize(bytes.size() + (value.size() + 1) * sizeof(wchar_t));
        std::memcpy(std::span(bytes).subspan(offset).data(), value.data(), value.size() * sizeof(wchar_t));
        return offset;
    };
    const ULONG name = text(fixture.name);
    for (size_t index = 0; index < fixture.fields.size(); ++index) {
        const auto& field = gsl::at(fixture.fields, index);
        const ULONG offset = text(field.name);
        EVENT_PROPERTY_INFO property{};
        property.NameOffset = offset;
        property.Flags = field.flags;
        property.nonStructType.InType = field.type;
        property.count = 1;
        std::memcpy(std::span(bytes).subspan(base + index * sizeof(property)).data(), &property, sizeof(property));
    }
    const gsl::not_null<TRACE_EVENT_INFO*> info(reinterpret_cast<TRACE_EVENT_INFO*>(bytes.data()));
    info->EventNameOffset = name;
    info->PropertyCount = gsl::narrow_cast<ULONG>(fixture.fields.size());
    info->TopLevelPropertyCount = info->PropertyCount;
    return bytes;
}

ULONG WINAPI Information(PEVENT_RECORD, ULONG, PTDH_CONTEXT, PTRACE_EVENT_INFO info, PULONG size) {
    if (!size) return ERROR_INVALID_PARAMETER;
    if (fixture.information_error) return fixture.information_error;
    const auto bytes = Metadata();
    if (!info || *size < bytes.size()) {
        *size = fixture.declared_size ? fixture.declared_size : gsl::narrow_cast<ULONG>(bytes.size());
        return ERROR_INSUFFICIENT_BUFFER;
    }
    std::memcpy(info, bytes.data(), bytes.size());
    *size = gsl::narrow_cast<ULONG>(bytes.size());
    return ERROR_SUCCESS;
}

const Field* Find(const PROPERTY_DATA_DESCRIPTOR& descriptor) noexcept {
    const auto* name = reinterpret_cast<const wchar_t*>(descriptor.PropertyName);
    const auto match = std::find_if(fixture.fields.begin(), fixture.fields.end(),
        [&](const Field& field) noexcept { return field.name == name; });
    return match == fixture.fields.end() ? nullptr : &*match;
}

#pragma warning(suppress : 26461) // TDH's callback-compatible API requires a mutable descriptor pointer.
ULONG WINAPI PropertySize(PEVENT_RECORD, ULONG, PTDH_CONTEXT, ULONG, PPROPERTY_DATA_DESCRIPTOR descriptor, PULONG size) noexcept {
    if (!descriptor || !size) return ERROR_INVALID_PARAMETER;
    if (fixture.property_size_error) return fixture.property_size_error;
    const auto* field = Find(*descriptor);
    if (!field) return ERROR_NOT_FOUND;
    *size = gsl::narrow_cast<ULONG>(field->bytes.size());
    return ERROR_SUCCESS;
}

#pragma warning(suppress : 26461) // Keep the exact TDH function-pointer signature.
ULONG WINAPI Property(PEVENT_RECORD, ULONG, PTDH_CONTEXT, ULONG, PPROPERTY_DATA_DESCRIPTOR descriptor, ULONG size, PBYTE data) noexcept {
    if (!descriptor || !data) return ERROR_INVALID_PARAMETER;
    if (fixture.property_error) return fixture.property_error;
    const auto* field = Find(*descriptor);
    if (!field) return ERROR_NOT_FOUND;
    if (size != field->bytes.size()) return ERROR_INVALID_DATA;
    std::memcpy(data, field->bytes.data(), size);
    return ERROR_SUCCESS;
}

void MetricFixture(metrics::Event event = Metric()) {
    fixture = {};
    Add(L"schema_version", TDH_INTYPE_UINT16, event.schema_version);
    Add(L"instance_id", TDH_INTYPE_GUID, event.instance_id);
    Add(L"capture_generation", TDH_INTYPE_UINT64, event.capture_generation);
    Add(L"service_id", TDH_INTYPE_UINT64, event.service_id);
    Add(L"operation_id", TDH_INTYPE_UINT64, event.operation_id);
    Add(L"parent_operation_id", TDH_INTYPE_UINT64, event.parent_operation_id);
    Add(L"metric", TDH_INTYPE_UINT8, event.metric);
    Add(L"phase", TDH_INTYPE_UINT8, event.phase);
    Add(L"outcome", TDH_INTYPE_UINT8, event.outcome);
    Add(L"error_domain", TDH_INTYPE_UINT8, event.error_domain);
    Add(L"error_code", TDH_INTYPE_UINT32, event.error_code);
    Add(L"mode", TDH_INTYPE_UINT8, event.mode);
    Add(L"source", TDH_INTYPE_UINT8, event.source);
    if (event.duration_ns) Add(L"duration_ns", TDH_INTYPE_UINT64, *event.duration_ns);
}

void InstanceFixture() {
    fixture = {};
    fixture.name = L"QiQiMetricInstance";
    const auto instance = std::get<MetricInstance>(Record(true).payload);
    Add(L"schema_version", TDH_INTYPE_UINT16, instance.schema_version);
    Add(L"instance_id", TDH_INTYPE_GUID, instance.instance_id);
    Add(L"capture_generation", TDH_INTYPE_UINT64, instance.capture_generation);
    Add(L"protocol_errors", TDH_INTYPE_UINT64, instance.diagnostics.protocol);
    Add(L"clock_errors", TDH_INTYPE_UINT64, instance.diagnostics.clock);
    Add(L"identity_errors", TDH_INTYPE_UINT64, instance.diagnostics.identity);
    Add(L"write_errors", TDH_INTYPE_UINT64, instance.diagnostics.write);
    Add(L"interrupted_samples", TDH_INTYPE_UINT64, instance.diagnostics.interrupted);
    Add(L"build_version", TDH_INTYPE_ANSISTRING, "test");
    Add(L"architecture", TDH_INTYPE_ANSISTRING, "x64");
}

void TestDecoder() {
    EVENT_RECORD record{};
    record.EventHeader.ProviderId = log::kProviderId;
    record.EventHeader.ProcessId = 11;
    record.EventHeader.ThreadId = 12;
    record.EventHeader.TimeStamp.QuadPart = 13;
    record.EventHeader.EventDescriptor.Keyword = std::to_underlying(log::Keyword::Metrics);
    record.EventHeader.ActivityId = kInstance;
    const MetricDecodeApi api{Information, PropertySize, Property};
    MetricRecord decoded;
    const auto expect = [&](ULONG error) {
        decoded.capture_generation = 99;
        QIQI_TEST_CHECK(DecodeMetricEvent(record, 1, decoded, api) == error);
        QIQI_TEST_CHECK(error == ERROR_SUCCESS || decoded.capture_generation == 99);
    };
    MetricFixture();
    expect(ERROR_SUCCESS);
    QIQI_TEST_CHECK(decoded.capture_generation == 1 && decoded.pid == 11 && decoded.tid == 12 && decoded.time == 13);
    QIQI_TEST_CHECK(std::get<metrics::Event>(decoded.payload).duration_ns == 0);
    QIQI_TEST_CHECK(std::get<metrics::Event>(decoded.payload).activity_id == kInstance);
    MetricFixture();
    const GUID related = {9, 8, 7, {6, 5, 4, 3, 2, 1, 0, 9}};
    EVENT_HEADER_EXTENDED_DATA_ITEM extension{};
    extension.ExtType = EVENT_HEADER_EXT_TYPE_RELATED_ACTIVITYID;
    extension.DataSize = sizeof(GUID);
    extension.DataPtr = reinterpret_cast<ULONGLONG>(&related);
    record.ExtendedData = &extension;
    record.ExtendedDataCount = 1;
    expect(ERROR_SUCCESS);
    QIQI_TEST_CHECK(std::get<metrics::Event>(decoded.payload).related_activity_id == related);
    extension.DataSize = sizeof(GUID) - 1;
    expect(ERROR_INVALID_DATA);
    record.ExtendedData = nullptr;
    expect(ERROR_INVALID_DATA);
    record.ExtendedDataCount = 0;
    auto maximum = Metric();
    maximum.duration_ns = (std::numeric_limits<uint64_t>::max)();
    MetricFixture(maximum);
    expect(ERROR_SUCCESS);
    QIQI_TEST_CHECK(std::get<metrics::Event>(decoded.payload).duration_ns == maximum.duration_ns);
    for (const auto phase : {metrics::Phase::Begin, metrics::Phase::End, metrics::Phase::Complete}) {
        auto event = Metric();
        event.phase = phase;
        if (phase == metrics::Phase::Begin) {
            event.duration_ns.reset();
            event.outcome = metrics::Outcome::Unspecified;
        }
        MetricFixture(event);
        expect(ERROR_SUCCESS);
    }
    for (const auto error : {ERROR_ACCESS_DENIED, ERROR_INVALID_DATA}) {
        MetricFixture();
        fixture.property_size_error = error;
        expect(error);
        fixture.property_size_error = 0;
        fixture.property_error = error;
        expect(error);
        fixture.property_error = 0;
        fixture.information_error = error;
        expect(error);
    }
    MetricFixture();
    fixture.declared_size = 65537;
    expect(ERROR_INVALID_DATA);
    fixture.declared_size = sizeof(TRACE_EVENT_INFO) - 1;
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    gsl::at(fixture.fields, 0).bytes = {2, 0};
    expect(ERROR_REVISION_MISMATCH);
    for (const size_t index : {6u, 7u, 8u, 9u, 11u, 12u}) {
        MetricFixture();
        gsl::at(fixture.fields, index).bytes = {255};
        expect(ERROR_INVALID_DATA);
    }
    MetricFixture();
    fixture.fields.pop_back();
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    gsl::at(fixture.fields, 13).type = TDH_INTYPE_DOUBLE;
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    gsl::at(fixture.fields, 13).bytes.resize(7);
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    gsl::at(fixture.fields, 0).flags = PropertyStruct;
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    gsl::at(fixture.fields, 1).name = L"schema_version";
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    Add(L"extra", TDH_INTYPE_UINT64, uint64_t{1});
    expect(ERROR_INVALID_DATA);
    auto event = Metric();
    event.error_domain = metrics::ErrorDomain::Win32;
    event.error_code = ERROR_ACCESS_DENIED;
    MetricFixture(event);
    expect(ERROR_INVALID_DATA);
    event.outcome = metrics::Outcome::Failure;
    MetricFixture(event);
    expect(ERROR_SUCCESS);
    QIQI_TEST_CHECK(std::get<metrics::Event>(decoded.payload).error_code == ERROR_ACCESS_DENIED);
    record.EventHeader.EventDescriptor.Keyword = std::to_underlying(log::Keyword::Logs);
    expect(ERROR_INVALID_DATA);
    record.EventHeader.EventDescriptor.Keyword = std::to_underlying(log::Keyword::Metrics);
    record.EventHeader.ProviderId = GUID_NULL;
    expect(ERROR_NOT_FOUND);
    record.EventHeader.ProviderId = log::kProviderId;
    InstanceFixture();
    expect(ERROR_SUCCESS);
    QIQI_TEST_CHECK(std::string_view(std::get<MetricInstance>(decoded.payload).architecture.data()) == "x64");
    gsl::at(fixture.fields, 9).bytes.back() = 'x';
    expect(ERROR_INVALID_DATA);
    InstanceFixture();
    gsl::at(fixture.fields, 9).bytes = {'a', 'r', 'm', 0};
    expect(ERROR_INVALID_DATA);
    InstanceFixture();
    gsl::at(fixture.fields, 8).bytes.assign(129, 'x');
    expect(ERROR_INVALID_DATA);
    InstanceFixture();
    gsl::at(fixture.fields, 8).bytes = {'t', 0, 'x', 0};
    expect(ERROR_INVALID_DATA);
    MetricFixture();
    Event log_event;
    QIQI_TEST_CHECK(DecodeEvent(record, log_event) == ERROR_NOT_FOUND);
}

void TestQueue() {
    MetricQueue queue(3, 3 * sizeof(MetricRecord));
    QIQI_TEST_CHECK(queue.BeginCapture(0) == ERROR_INVALID_PARAMETER);
    QIQI_TEST_CHECK(queue.BeginCapture(1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Record()) == ERROR_NOT_READY);
    QIQI_TEST_CHECK(queue.Push(Record(true)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Record()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Record()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Record()) == ERROR_BUFFER_OVERFLOW);
    auto status = queue.Status();
    QIQI_TEST_CHECK(status.queued_records == 3 && status.queued_bytes == 3 * sizeof(MetricRecord));
    QIQI_TEST_CHECK(status.queue_dropped == 1 && status.incomplete);
    const auto records = queue.Take();
    QIQI_TEST_CHECK(records.size() == 3 && std::holds_alternative<MetricInstance>(gsl::at(records, 0).payload));
    QIQI_TEST_CHECK(queue.Status().queued_bytes == 0 && queue.Status().incomplete);
    auto stale = Record();
    --std::get<metrics::Event>(stale.payload).capture_generation;
    QIQI_TEST_CHECK(queue.Push(stale) == ERROR_REVISION_MISMATCH);
    auto next = Record(true);
    ++std::get<MetricInstance>(next.payload).capture_generation;
    QIQI_TEST_CHECK(queue.Push(next) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Push(Record()) == ERROR_REVISION_MISMATCH);
    queue.EndCapture();
    QIQI_TEST_CHECK(queue.Push(next) == ERROR_REVISION_MISMATCH);
    QIQI_TEST_CHECK(queue.BeginCapture(1) == ERROR_INVALID_PARAMETER);
    QIQI_TEST_CHECK(queue.BeginCapture(2) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(queue.Status().generation_errors == 0 && !queue.Status().incomplete);
    QIQI_TEST_CHECK(queue.Push(Record(true)) == ERROR_REVISION_MISMATCH);
    auto current = Record(true);
    current.capture_generation = 2;
    QIQI_TEST_CHECK(queue.Push(current) == ERROR_SUCCESS);
    EventQueue logs;
    Event log;
    log.message = L"independent log";
    logs.Push(log);
    QIQI_TEST_CHECK(logs.Take().size() == 1 && queue.Status().queued_records == 1);
    queue.RecordLoss(1, 2);
    queue.RecordDecodeError();
    queue.RecordAllocationError();
    queue.RecordCaptureError();
    status = queue.Status();
    QIQI_TEST_CHECK(status.events_lost == 1 && status.buffers_lost == 2 &&
        status.protocol_errors == 1 && status.allocation_errors == 1 && status.capture_errors == 1 && status.incomplete);

    MetricQueue bytes_queue(10, sizeof(MetricRecord));
    QIQI_TEST_CHECK(bytes_queue.BeginCapture(1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes_queue.Push(Record(true)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes_queue.Push(Record()) == ERROR_BUFFER_OVERFLOW);
    QIQI_TEST_CHECK(bytes_queue.Take().size() == 1);
    QIQI_TEST_CHECK(bytes_queue.Push(Record()) == ERROR_SUCCESS);
    auto newer = Record(true);
    ++std::get<MetricInstance>(newer.payload).capture_generation;
    QIQI_TEST_CHECK(bytes_queue.Push(newer) == ERROR_BUFFER_OVERFLOW);
    QIQI_TEST_CHECK(bytes_queue.Take().size() == 1);
    auto newer_metric = Record();
    ++std::get<metrics::Event>(newer_metric.payload).capture_generation;
    QIQI_TEST_CHECK(bytes_queue.Push(newer_metric) == ERROR_NOT_READY);
    QIQI_TEST_CHECK(bytes_queue.Push(Record()) == ERROR_REVISION_MISMATCH);
    QIQI_TEST_CHECK(bytes_queue.Push(newer) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(bytes_queue.Take().size() == 1);
    QIQI_TEST_CHECK(bytes_queue.Push(newer_metric) == ERROR_SUCCESS);

    MetricQueue sources;
    QIQI_TEST_CHECK(sources.BeginCapture(1) == ERROR_SUCCESS);
    for (DWORD pid = 1; pid <= 32; ++pid) {
        auto metadata = Record(true);
        metadata.pid = pid;
        QIQI_TEST_CHECK(sources.Push(metadata) == ERROR_SUCCESS);
    }
    auto metadata = Record(true);
    metadata.pid = 33;
    QIQI_TEST_CHECK(sources.Push(metadata) == ERROR_TOO_MANY_NAMES);
    QIQI_TEST_CHECK(sources.Status().source_limit_errors == 1 && sources.Status().incomplete);
    auto reloaded = Record();
    ++std::get<metrics::Event>(reloaded.payload).instance_id.Data1;
    QIQI_TEST_CHECK(sources.Push(reloaded) == ERROR_NOT_READY);
    MetricQueue reloading;
    QIQI_TEST_CHECK(reloading.BeginCapture(1) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(reloading.Push(Record(true)) == ERROR_SUCCESS);
    auto second_instance = Record(true);
    ++std::get<MetricInstance>(second_instance.payload).instance_id.Data1;
    QIQI_TEST_CHECK(reloading.Push(second_instance) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(reloading.Push(reloaded) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(reloading.Push(Record()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(!reloading.Status().incomplete);

    MetricQueue concurrent(1000, 1000 * sizeof(MetricRecord));
    QIQI_TEST_CHECK(concurrent.BeginCapture(1) == ERROR_SUCCESS && concurrent.Push(Record(true)) == ERROR_SUCCESS);
    std::array<std::jthread, 4> threads;
    for (auto& thread : threads) {
        thread = std::jthread([&]() {
            for (int index = 0; index < 100; ++index) QIQI_TEST_CHECK(concurrent.Push(Record()) == ERROR_SUCCESS);
        });
    }
    for (auto& thread : threads) thread.join();
    QIQI_TEST_CHECK(concurrent.Take().size() == 401 && !concurrent.Status().incomplete);
}

thread_local MetricQueue* native_queue = nullptr;
ULONG Observe(EVENT_RECORD& record) {
    MetricRecord decoded;
    const ULONG error = DecodeMetricEvent(record, 1, decoded);
    if (error != ERROR_SUCCESS) return error;
    Event log_event;
    if (DecodeEvent(record, log_event) != ERROR_NOT_FOUND) return ERROR_INVALID_DATA;
    return native_queue->Push(decoded);
}

int NativeTest() {
    MetricQueue queue;
    QIQI_TEST_CHECK(queue.BeginCapture(1) == ERROR_SUCCESS);
    native_queue = &queue;
    const auto reset = gsl::finally([]() noexcept { native_queue = nullptr; });
    const int result = qiqi::test::RunMetricsEtwTest(Observe);
    if (result != 0) return result;
    const auto records = queue.Take();
    size_t metadata = 0;
    size_t samples = 0;
    MetricAggregator aggregator;
    QIQI_TEST_CHECK(aggregator.BeginCapture(1) == ERROR_SUCCESS);
    for (const auto& record : records) {
        QIQI_TEST_CHECK(aggregator.Consume(record) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(record.pid == ::GetCurrentProcessId() && record.capture_generation == 1);
        if (const auto* instance = std::get_if<MetricInstance>(&record.payload)) {
            ++metadata;
            QIQI_TEST_CHECK(std::string_view(instance->build_version.data()) == version::kFullVersion);
            QIQI_TEST_CHECK(std::string_view(instance->architecture.data()) == version::kArch);
        } else ++samples;
    }
    QIQI_TEST_CHECK(samples == 6 && metadata == 4);
    QIQI_TEST_CHECK(queue.Status().protocol_errors == 0 && queue.Status().generation_errors == 0);
    const auto& first = std::get<metrics::Event>(gsl::at(records, 1).payload);
    const MetricDimension key{::GetCurrentProcessId(), first.instance_id, first.service_id, first.metric, first.mode, first.source};
    const auto summary = aggregator.Snapshot(key, MetricView::Cumulative);
    QIQI_TEST_CHECK(summary && summary->intents == 5 && summary->pending == 0);
    QIQI_TEST_CHECK(summary->Count(metrics::Outcome::Success) == 2 && summary->Count(metrics::Outcome::Failure) == 1 &&
        summary->Count(metrics::Outcome::Cancelled) == 1 && summary->Count(metrics::Outcome::Incomplete) == 1);
    QIQI_TEST_CHECK(summary->success.Samples() == 2 && summary->failure.Samples() == 1);
    QIQI_TEST_CHECK(aggregator.Status().provider_deltas.protocol == 1 && aggregator.Status().provider_deltas.interrupted == 2);
    std::cout << "[PASS] Production metric decoder, independent queue and aggregation over real ETW.\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    const std::span args(argv, gsl::narrow_cast<size_t>(argc));
    if (argc == 2 && std::string_view(gsl::at(args, 1)) == "--etw") return NativeTest();
    if (argc == 3 && std::string_view(gsl::at(args, 1)) == "--json-fixtures") {
        RunMetricExportTests(std::filesystem::path(gsl::at(args, 2)));
        return 0;
    }
    if (argc != 1) {
        std::cerr << "Usage: qiqi_monitor_metrics_test [--etw | --json-fixtures existing-directory]\n";
        return 2;
    }
    TestDecoder();
    TestQueue();
    RunAggregationTests();
    RunMetricExportTests({});
    std::cout << "[PASS] Strict metric decoding and bounded queue.\n";
    return 0;
}
