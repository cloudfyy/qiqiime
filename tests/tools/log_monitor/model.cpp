// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

static_assert(kEventBufferDefaultRecords == 10'000 && kEventBufferDefaultBytes == 16 * 1024 * 1024);
static_assert(kEventQueueDefaultRecords == 2'000 && kEventQueueDefaultBytes == 4 * 1024 * 1024);
static_assert(kMetricQueueDefaultRecords == 2'000 && kMetricQueueDefaultBytes == 4 * 1024 * 1024);
static_assert(kDiagnosticMaxRecords == 512 && kDiagnosticMaxBytes == 1024 * 1024);
static_assert(kDiagnosticReportStride == 1024);
static_assert(kMonitorRefreshTimerId == 1 && kMonitorRefreshIntervalMs == 100);
static_assert(kMonitorLossPollIntervalMs == 1000 && kMonitorLossPollTicks == 10);
static_assert(kMonitorMinimumDetailHeight == 60 && kMonitorDetailsFooterSpace == 50);
static_assert(kMonitorPreviewChars == 1024);

void TestBuffersAndFilters() {
    EventBuffer buffer(2, 4096);
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"first")));
    const uint64_t first = buffer.Events().front().id;
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"second")));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"third")));
    QIQI_TEST_CHECK(buffer.Events().size() == 2 && buffer.Evicted() == 1 && !buffer.Find(first));
    QIQI_TEST_CHECK(buffer.Find(first + 1)->message == L"second");
    const uint64_t last = buffer.Events().back().id;
    buffer.Clear();
    QIQI_TEST_CHECK(buffer.Bytes() == 0 && !buffer.Find(last));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent()));
    QIQI_TEST_CHECK(buffer.Events().front().id > last);
    QIQI_TEST_CHECK(!buffer.Push(MakeEvent(std::wstring(5000, L'x'))));
    QIQI_TEST_CHECK(buffer.Events().size() == 1 && buffer.Evicted() == 2);

    EventBuffer bytes(100, 1024);
    for (int i = 0; i < 100; ++i) bytes.Push(MakeEvent(std::wstring(150, L'x')));
    QIQI_TEST_CHECK(bytes.Bytes() <= 1024 && bytes.Events().size() < 100 && bytes.Evicted() > 0);
    auto exact_event = MakeEvent(std::wstring(150, L'x'));
    const size_t exact_bytes = exact_event.Bytes();
    EventBuffer exact(100, exact_bytes);
    QIQI_TEST_CHECK(exact.Push(std::move(exact_event)) && exact.Bytes() == exact_bytes);
    QIQI_TEST_CHECK(exact.Push(MakeEvent(std::wstring(150, L'x'))));
    QIQI_TEST_CHECK(exact.Events().size() == 1 && exact.Evicted() == 1 && exact.Bytes() == exact_bytes);
    EventBuffer too_small(100, exact_bytes - 1);
    QIQI_TEST_CHECK(!too_small.Push(MakeEvent(std::wstring(150, L'x'))) && too_small.Events().empty());
    EventBuffer capacities(10, 1024);
    auto reserved = MakeEvent();
    reserved.message.reserve(4096);
    QIQI_TEST_CHECK(!capacities.Push(std::move(reserved)));
    QIQI_TEST_CHECK(capacities.Events().empty());

    EventBuffer processes(3, 4096);
    auto first_process = MakeEvent();
    first_process.pid = 10;
    QIQI_TEST_CHECK(processes.Push(first_process));
    QIQI_TEST_CHECK(processes.Push(first_process));
    auto second_process = first_process;
    second_process.pid = 20;
    second_process.process = L"code.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    auto choices = CollectProcessChoices(processes, L"CODE");
    QIQI_TEST_CHECK(choices.names.size() == 1 && choices.pids == std::vector<DWORD>({10, 20}));
    second_process.pid = 30;
    second_process.process = L"notepad.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").pids == std::vector<DWORD>({10, 20, 30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"notepad").pids == std::vector<DWORD>({30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"missing").pids.empty());
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"code").pids == std::vector<DWORD>({20}));
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names == std::vector<std::wstring>({L"notepad.exe"}));
    processes.Clear();
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names.empty() &&
                    CollectProcessChoices(processes, L"").pids.empty());
    auto unnamed = MakeEvent();
    unnamed.process.clear();
    unnamed.pid = 0;
    QIQI_TEST_CHECK(processes.Push(unnamed));
    auto maximum_pid = MakeEvent();
    maximum_pid.pid = MAXDWORD;
    QIQI_TEST_CHECK(processes.Push(maximum_pid));
    choices = CollectProcessChoices(processes, L"");
    QIQI_TEST_CHECK(choices.names == std::vector<std::wstring>({L"Code.exe"}));
    QIQI_TEST_CHECK(choices.pids == std::vector<DWORD>({0, MAXDWORD}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"code.exe").pids == std::vector<DWORD>({MAXDWORD}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L" ").pids.empty());

    Filter filter;
    auto event = MakeEvent(L"Case sensitive? no.\tline\nnext");
    const GUID parent_act{0x12345678, 0x9abc, 0xdef0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    const GUID child_act{0x87654321, 0x9abc, 0xdef0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    event.keyword = std::to_underlying(log::Keyword::Composition);
    event.opcode = std::to_underlying(log::Opcode::Start);
    event.activity_id = child_act;
    event.related_activity_id = parent_act;
    QIQI_TEST_CHECK(FormatShortGuid(GUID_NULL).empty() && FormatGuid(GUID_NULL).empty());
    QIQI_TEST_CHECK(FormatShortGuid(parent_act) == L"12345678");
    QIQI_TEST_CHECK(FormatGuid(parent_act) == L"{12345678-9abc-def0-1122-334455667788}");
    QIQI_TEST_CHECK(LevelName(1) == L"Critical" && LevelName(3) == L"Warning" &&
                    LevelName(5) == L"Verbose" && LevelName(99) == L"Level 99");
    bool bad_limits = false;
    try { EventBuffer invalid_buf(0, 1024); } catch (const std::invalid_argument&) { bad_limits = true; }
    QIQI_TEST_CHECK(bad_limits);
    bool bad_time = false;
    try { std::ignore = FormatTime(std::numeric_limits<uint64_t>::max()); } catch (const std::runtime_error&) { bad_time = true; }
    QIQI_TEST_CHECK(bad_time);
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.process = L"CODE";
    filter.pid = 42;
    filter.level = 4;
    filter.keyword = std::to_underlying(log::Keyword::Composition);
    filter.activity_id = parent_act; // Matches via related_activity_id!
    filter.text = L"NEXT";
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.activity_id = child_act; // Matches via activity_id!
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.activity_id = GUID_NULL;
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.activity_id.reset();
    filter.keyword = std::to_underlying(log::Keyword::Sync);
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.keyword = 0;
    filter.pid = 43;
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.pid.reset();
    filter.level = 2;
    QIQI_TEST_CHECK(!filter.Matches(event));
    QIQI_TEST_CHECK(!ParsePid(L"") && !ParsePid(L"-1") && !ParsePid(L"4294967296") && !ParsePid(L"1x"));
    QIQI_TEST_CHECK(ParsePid(L"0") == 0 && ParsePid(L"4294967295") == MAXDWORD);
    const auto exported = ExportEvents({event}, L"test");
    QIQI_TEST_CHECK(exported.contains(version::kFullVersion) && exported.contains(version::kArch));
    QIQI_TEST_CHECK(exported.contains("\\tline\\nnext") &&
                    exported.contains("PID\tTID\tLevel\tCategory\tOpcode\tActivityId\tRelatedActivityId") &&
                    exported.contains("Composition\tStart\t{87654321-9abc-def0-1122-334455667788}\t{12345678-9abc-def0-1122-334455667788}"));
    const auto entry_details = FormatEventEntry(event, L"");
    QIQI_TEST_CHECK(entry_details.contains(L"Composition:Start") &&
                    entry_details.contains(L"Activity={87654321-9abc-def0-1122-334455667788}") &&
                    entry_details.contains(L"Related={12345678-9abc-def0-1122-334455667788}"));
    QIQI_TEST_CHECK(FormatTime(event.time).ends_with(L"Z"));
    auto second_event = MakeEvent(std::wstring(L"multi\0nul", 9));
    second_event.level = 2;
    second_event.pid = 99;
    const auto formatted_clip = FormatEventsForClipboard(std::array{event, second_event});
    QIQI_TEST_CHECK(formatted_clip.contains(
        L"PID=42\tTID=84\tInfo\tComposition\tStart\t{87654321-9abc-def0-1122-334455667788}\tQiQiLogInfo\tCase sensitive? no.\tline\nnext\r\n"));
    QIQI_TEST_CHECK(formatted_clip.contains(
        L"PID=99\tTID=84\tError\tGeneral\tInfo\t\tQiQiLogInfo\tmulti\x2400nul"));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, std::wstring_view(L"clip\0test", 9)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == L"clip\x2400test");
    std::vector<Event> full_buffer;
    full_buffer.reserve(10'000);
    for (int i = 0; i < 10'000; ++i) {
        full_buffer.push_back(MakeEvent(std::format(L"bulk-record-{} {}", i, std::wstring(256, L'x'))));
    }
    const auto full_copy = FormatEventsForClipboard(full_buffer);
    QIQI_TEST_CHECK(full_copy.size() > 1'000'000);
    QIQI_TEST_CHECK(std::count(full_copy.begin(), full_copy.end(), L'\n') == 9'999);
    QIQI_TEST_CHECK(full_copy.contains(L"bulk-record-0 ") && full_copy.contains(L"bulk-record-9999 "));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, full_copy) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == full_copy);
    event.message = std::wstring(1, L'\xD800');
    bool invalid = false;
    try { ExportEvents({event}, L"test"); } catch (const std::system_error&) { invalid = true; }
    QIQI_TEST_CHECK(invalid);
    EventQueue queue(5, 4096);
    std::thread producer([&] { for (int i = 0; i < 100; ++i) queue.Push(MakeEvent()); });
    producer.join();
    QIQI_TEST_CHECK(queue.Evicted() == 95 && queue.Take().size() == 5 && queue.Take().empty());
}

void TestExport() {
    const auto path = std::filesystem::temp_directory_path() /
        (L"qiqi-monitor-export-" + std::to_wstring(::GetCurrentProcessId()) + L".log");
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove(path, error);
    });
    const auto bytes = ExportEvents({MakeEvent(std::wstring(L"中文\0\r\tline\nend\\", 14))}, L"test");
    QIQI_TEST_CHECK(bytes.contains("\\0\\r\\tline\\nend\\\\"));
    QIQI_TEST_CHECK(WriteExport(path.c_str(), bytes) == ERROR_SUCCESS);
    std::string content;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    {
        const ScopedFileHandle lock(::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        QIQI_TEST_CHECK(lock && WriteExport(path.c_str(), "replacement") == ERROR_SHARING_VIOLATION);
    }
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    const auto missing = path / L"invalid.log";
    QIQI_TEST_CHECK(WriteExport(missing.c_str(), bytes) != ERROR_SUCCESS);
}

} // namespace qiqi::monitor::tests
