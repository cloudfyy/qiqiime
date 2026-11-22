// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/syncutil.h"

namespace qiqi::test::changesync {
void TestFailureDiagnostics() {
    const TestStage stage(__func__);
    std::ostringstream assertion_output;
    bool logged_before_cleanup = false;
    bool caught_assertion = false;
    {
        auto* const previous = std::cerr.rdbuf(assertion_output.rdbuf());
        const auto restore = gsl::finally([&]() noexcept { std::cerr.rdbuf(previous); });
        try {
            const auto cleanup = gsl::finally([&]() noexcept {
                logged_before_cleanup = std::uncaught_exceptions() != 0 &&
                    assertion_output.view().find("[FAIL]") != std::string_view::npos;
            });
            QIQI_TEST_CHECK(false && "diagnostic assertion probe");
        } catch (const std::runtime_error& error) {
            caught_assertion = true;
            QIQI_TEST_CHECK(std::string_view(error.what()).find(
                "diagnostic assertion probe") != std::string_view::npos);
        }
    }
    QIQI_TEST_CHECK(caught_assertion && logged_before_cleanup);
    QIQI_TEST_CHECK(assertion_output.view().find(
        "false && \"diagnostic assertion probe\"") != std::string_view::npos);

    const Fixture fixture(L"cleanup-diagnostics");
    const auto locked_path = fixture.directory / L"held-open-\u6d4b\u8bd5";
    ScopedFileHandle locked(::CreateFileW(locked_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(locked);
    std::error_code error;
    std::filesystem::remove_all(fixture.directory, error);
    QIQI_TEST_CHECK(error == std::error_code(ERROR_SHARING_VIOLATION, std::system_category()));
    FILE* raw_output = nullptr;
    QIQI_TEST_CHECK(::tmpfile_s(&raw_output) == 0);
    const ScopedHandle<NullHandleTraits<FILE*, &std::fclose>> output(raw_output);
    QIQI_TEST_CHECK(output);
    ReportDirectoryCleanupFailure(fixture.directory, error, output.Get());
    QIQI_TEST_CHECK(std::fseek(output.Get(), 0, SEEK_SET) == 0);
    std::string diagnostic;
    std::array<char, 1024> buffer{};
    for (;;) {
        const size_t count = std::fread(buffer.data(), 1, buffer.size(), output.Get());
        diagnostic.append(buffer.data(), count);
        if (count < buffer.size()) {
            QIQI_TEST_CHECK(std::ferror(output.Get()) == 0 && std::feof(output.Get()) != 0);
            break;
        }
    }
    QIQI_TEST_CHECK(diagnostic.find("[CLEANUP-FAIL]") != std::string::npos);
    QIQI_TEST_CHECK(diagnostic.find("error=32") != std::string::npos);
    const auto expected_path = WideToUtf8(locked_path.native());
    QIQI_TEST_CHECK(!expected_path.empty());
    const auto path_position = diagnostic.find(expected_path);
    QIQI_TEST_CHECK(path_position != std::string::npos);
    const auto line_end = diagnostic.find('\n', path_position);
    QIQI_TEST_CHECK(diagnostic.substr(path_position, line_end - path_position).find(
        "delete_open_error=32") != std::string::npos);
    QIQI_TEST_CHECK(std::filesystem::exists(locked_path));
    locked.Reset();
}

void TestDirectoryCleanupRetry() {
    const TestStage stage(__func__);
    const Fixture fixture(L"cleanup-retry");
    const auto locked_path = fixture.directory / L"held-open";
    ScopedFileHandle locked(::CreateFileW(locked_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(locked);

    constexpr DWORD budget = 100;
    const auto started = ::GetTickCount64();
    const auto blocked = RemoveDirectoryWithSharingRetry(fixture.directory, budget, ::Sleep);
    QIQI_TEST_CHECK(blocked == std::error_code(ERROR_SHARING_VIOLATION, std::system_category()));
    QIQI_TEST_CHECK(::GetTickCount64() - started >= budget);
    QIQI_TEST_CHECK(std::filesystem::exists(locked_path));

    {
        DWORD size = 0;
        const BOOL queried = ::GetFileSecurityW(
            fixture.directory.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
        const DWORD query_error = queried ? ERROR_SUCCESS : ::GetLastError();
        QIQI_TEST_CHECK(!queried && query_error == ERROR_INSUFFICIENT_BUFFER && size != 0);
        std::vector<uint8_t> original_security(size);
        QIQI_TEST_CHECK(::GetFileSecurityW(fixture.directory.c_str(), DACL_SECURITY_INFORMATION,
            original_security.data(), size, &size));
        PSECURITY_DESCRIPTOR raw = nullptr;
        QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;FR;;;WD)(A;;FA;;;OW)", SDDL_REVISION_1, &raw, nullptr));
        const ScopedLocalAlloc denied_security(raw);
        const auto restore = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK_CLEANUP(::SetFileSecurityW(fixture.directory.c_str(),
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, original_security.data()));
        });
        QIQI_TEST_CHECK(::SetFileSecurityW(fixture.directory.c_str(),
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
        bool unexpected_pause = false;
        const auto denied = RemoveDirectoryWithSharingRetry(fixture.directory, budget, [&](DWORD delay) {
            unexpected_pause = true;
            ::Sleep(delay);
        });
        QIQI_TEST_CHECK(denied == std::error_code(ERROR_ACCESS_DENIED, std::system_category()));
        QIQI_TEST_CHECK(!unexpected_pause);
    }

    bool released_after_conflict = false;
    const auto recovered = RemoveDirectoryWithSharingRetry(
        fixture.directory, kCleanupRetryBudget, [&](DWORD delay) {
            locked.Reset();
            released_after_conflict = true;
            ::Sleep(delay);
        });
    QIQI_TEST_CHECK(released_after_conflict && !recovered);
    QIQI_TEST_CHECK(!std::filesystem::exists(fixture.directory));
}

void TestJournal() {
    const TestStage stage(__func__);
    Fixture fixture(L"journal");
    Channel channel(fixture.directory, kSource);
    QIQI_TEST_CHECK(Channel::Create(fixture.directory, kSource) == S_OK);
    Version applied;
    size_t snapshots = 0;
    size_t deltas = 0;
    const auto snapshot = [&]() { ++snapshots; return S_OK; };
    const auto delta = [&](std::span<const Change> changes) {
        deltas += changes.size();
        return S_OK;
    };
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::Composing, snapshot, delta) == S_FALSE);
    QIQI_TEST_CHECK(applied == Version{} && !snapshots);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput,
        []() { return E_ACCESSDENIED; }, delta) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(applied == Version{});
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot, delta) == S_OK);
    QIQI_TEST_CHECK(snapshots == 1 && applied.sequence == 0);
    const Version start = applied;
    Version committed;
    for (auto operation : {Operation::Add, Operation::Modify, Operation::Remove, Operation::Reorder}) {
        QIQI_TEST_CHECK(channel.Publish(Current(channel), operation, kPayload, []() { return S_OK; },
                                       committed) == S_OK);
    }
    QIQI_TEST_CHECK(committed.sequence == 4);
    Batch batch;
    QIQI_TEST_CHECK(channel.Read(start, batch) == S_OK && batch.snapshot == SnapshotReason::None);
    QIQI_TEST_CHECK(batch.changes.size() == 4 && batch.changes.back().operation == Operation::Reorder);
    for (const auto& change : batch.changes) QIQI_TEST_CHECK(std::ranges::equal(change.payload, kPayload));
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::Composing, snapshot, delta) == S_FALSE);
    QIQI_TEST_CHECK(applied == start && !deltas);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot,
        [](std::span<const Change>) { return E_FAIL; }) == E_FAIL);
    QIQI_TEST_CHECK(applied == start);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot,
        [](std::span<const Change>) { return S_FALSE; }) == S_FALSE);
    QIQI_TEST_CHECK(applied == start);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot, delta) == S_OK);
    QIQI_TEST_CHECK(applied == committed && deltas == 4 && snapshots == 1);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot, delta) == S_OK);
    QIQI_TEST_CHECK(deltas == 4);
    bool called = false;
    QIQI_TEST_CHECK(channel.Publish(start, Operation::Add, {}, [&]() {
        called = true; return S_OK;
    }, committed) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH));
    QIQI_TEST_CHECK(!called);
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, {}, []() {
        return E_ACCESSDENIED;
    }, committed) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(channel.Read(applied, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::BaselineMismatch);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot, delta) == S_OK);
    QIQI_TEST_CHECK(snapshots == 2 && applied.sequence == 5);
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, {}, []() -> HRESULT {
        throw std::bad_alloc();
    }, committed) == E_OUTOFMEMORY);
    QIQI_TEST_CHECK(channel.Read(applied, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::BaselineMismatch);
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, snapshot, delta) == S_OK);
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, {}, []() -> HRESULT {
        throw std::runtime_error("unexpected callback failure");
    }, committed) == E_UNEXPECTED);
    QIQI_TEST_CHECK(channel.Read(applied, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::BaselineMismatch);
    QIQI_TEST_CHECK(channel.Watch() == S_OK);
    QIQI_TEST_CHECK(channel.NotificationHandle() != INVALID_HANDLE_VALUE);
    QIQI_TEST_CHECK(channel.Rearm() == S_OK);
    std::vector<uint8_t> too_large(Channel::kMaxPayloadBytes + 1);
    const Version before = Current(channel);
    QIQI_TEST_CHECK(channel.Publish(before, Operation::Add, too_large, []() { return S_OK; },
                                   committed) == E_INVALIDARG);
    QIQI_TEST_CHECK(Current(channel) == before);
    Channel wrong_source(fixture.directory, {DataType::PhraseFrequency, kSource.id});
    QIQI_TEST_CHECK(wrong_source.Read({}, batch) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    const Source frequency_source{DataType::PhraseFrequency, kSource.id};
    QIQI_TEST_CHECK(Channel::Create(fixture.directory / L"frequency", frequency_source) == S_OK);
    Channel frequency(fixture.directory / L"frequency", frequency_source);
    const Version frequency_start = Current(frequency);
    Version frequency_next;
    QIQI_TEST_CHECK(frequency.Publish(frequency_start, Operation::Reorder, kPayload,
        []() { return S_OK; }, frequency_next) == S_OK);
    QIQI_TEST_CHECK(frequency_next.sequence == 1 && Current(channel) == before);
    QIQI_TEST_CHECK(Channel::Create(fixture.directory / L"invalid", {}) == E_INVALIDARG);
    QIQI_TEST_CHECK(!std::filesystem::exists(fixture.directory / L"invalid"));
    const auto regular_file = fixture.directory / L"not_a_directory.txt";
    QIQI_TEST_CHECK(WriteFileAtomically(regular_file, kPayload));
    QIQI_TEST_CHECK(Channel::Create(regular_file, kSource) == E_INVALIDARG);
    Channel invalid(L"relative", kSource);
    QIQI_TEST_CHECK(invalid.Watch() == E_INVALIDARG);
    QIQI_TEST_CHECK(invalid.Read({}, batch) == E_INVALIDARG);
    QIQI_TEST_CHECK(invalid.Rearm() == E_UNEXPECTED);
}

void TestRetentionAndCorruption() {
    const TestStage stage(__func__);
    Fixture fixture(L"retention");
    Channel channel(fixture.directory, kSource);
    const Version start = Current(channel);
    Version committed = start;
    for (size_t i = 0; i <= Channel::kMaxRecords; ++i) {
        Version next;
        QIQI_TEST_CHECK(channel.Publish(committed, Operation::Add, {}, []() { return S_OK; }, next) == S_OK);
        committed = next;
    }
    Batch batch;
    QIQI_TEST_CHECK(channel.Read(start, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::VersionGap && batch.changes.empty());
    Version retained = start;
    retained.sequence = 1;
    QIQI_TEST_CHECK(channel.Read(retained, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::None && batch.changes.size() == Channel::kMaxRecords);
    const std::vector<uint8_t> large(Channel::kMaxPayloadBytes, 0xa5);
    const Version before_large = committed;
    for (size_t i = 0; i < 33; ++i) {
        Version next;
        QIQI_TEST_CHECK(channel.Publish(committed, Operation::Modify, large, []() { return S_OK; }, next) == S_OK);
        committed = next;
    }
    QIQI_TEST_CHECK(std::filesystem::file_size(fixture.directory / L"journal.bin") <= Channel::kMaxJournalBytes);
    QIQI_TEST_CHECK(channel.Read(before_large, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::VersionGap);
    std::string binary;
    QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"journal.bin", binary));
    const std::vector<uint8_t> valid_bytes(binary.begin(), binary.end());
    std::vector<uint8_t> damaged = valid_bytes;
    damaged.back() ^= 1;
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", damaged) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_CRC));

    const auto with_valid_hash = [](std::vector<uint8_t> body) {
        body.resize(body.size() - 8);
        uint64_t hash = 14695981039346656037ull;
        for (const uint8_t b : body) hash = (hash ^ b) * 1099511628211ull;
        for (size_t i = 0; i < 8; ++i) {
            body.push_back(gsl::narrow_cast<uint8_t>(hash & 0xff));
            hash >>= 8;
        }
        return body;
    };
    // 1. Wrong magic -> ERROR_UNKNOWN_REVISION
    auto wrong_magic = valid_bytes;
    gsl::at(wrong_magic, 0) = 0;
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", with_valid_hash(wrong_magic)) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION));

    // 2. Pending flag with zero recovery GUID -> ERROR_INVALID_DATA
    auto invalid_pending = valid_bytes;
    invalid_pending.resize(76 + 8);
    gsl::at(invalid_pending, 68) = 1; // flags = 1 (pending)
    for (size_t i = 72; i < 76; ++i) gsl::at(invalid_pending, i) = 0; // count = 0
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", with_valid_hash(invalid_pending)) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    // 3. Invalid operation in record -> ERROR_INVALID_DATA
    auto invalid_op = valid_bytes;
    gsl::at(invalid_op, 76 + 8) = 99;
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", with_valid_hash(invalid_op)) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    // 4. Truncated journal (< header + hash) -> ERROR_INVALID_DATA
    constexpr std::array<uint8_t, 10> truncated{};
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", truncated) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    // 5. Oversized journal (> kMaxJournalBytes) -> ERROR_INVALID_DATA
    const std::vector<uint8_t> oversized(Channel::kMaxJournalBytes + 1, 0);
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory / L"journal.bin", oversized) == S_OK);
    QIQI_TEST_CHECK(channel.Read(committed, batch) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    QIQI_TEST_CHECK(WriteFileAtomicallyResult(fixture.directory, {}) == E_INVALIDARG);
}

void TestCommitFailure() {
    const TestStage stage(__func__);
    Fixture fixture(L"commit-failure");
    Channel channel(fixture.directory, kSource);
    const Version before = Current(channel);
    Version committed;
    ScopedFileHandle prevent_replace;
    const HRESULT hr = channel.Publish(before, Operation::Modify, kPayload, [&]() {
        const HRESULT write = WriteFileAtomicallyResult(fixture.directory / L"published", kPayload);
        if (FAILED(write)) return write;
        prevent_replace.Reset(::CreateFileW((fixture.directory / L"journal.bin").c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(prevent_replace);
        return S_OK;
    }, committed);
    QIQI_TEST_CHECK(hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
    QIQI_TEST_CHECK(committed == Version{});
    QIQI_TEST_CHECK(!std::filesystem::exists((fixture.directory / L"journal.bin").wstring() +
        L".tmp." + std::to_wstring(::GetCurrentProcessId())));
    prevent_replace.Reset();
    Batch batch;
    QIQI_TEST_CHECK(channel.Read(before, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::InterruptedWrite && batch.changes.empty());
    const Version applied = batch.version;
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, {}, []() { return S_FALSE; }, committed) == S_FALSE);
    QIQI_TEST_CHECK(committed == Version{} && Current(channel).baseline != applied.baseline);
}
} // namespace qiqi::test::changesync
