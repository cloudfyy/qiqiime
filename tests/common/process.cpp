// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/syncutil.h"

namespace qiqi::test::changesync {
struct AttributeListTraits {
    using HandleType = LPPROC_THREAD_ATTRIBUTE_LIST;
    static HandleType Invalid() noexcept { return nullptr; }
    static bool IsValid(const void* value) noexcept { return value != nullptr; }
    static void Close(HandleType value) noexcept { ::DeleteProcThreadAttributeList(value); }
};

Child::Child(const std::filesystem::path& executable, std::wstring arguments,
             SECURITY_CAPABILITIES* capabilities, HANDLE start) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    QIQI_TEST_CHECK(::CreatePipe(read_.Put(), write_.Put(), &attributes, 0));
    QIQI_TEST_CHECK(::SetHandleInformation(read_.Get(), HANDLE_FLAG_INHERIT, 0));
    SIZE_T size = 0;
    const DWORD count = capabilities ? 2 : 1;
    ::InitializeProcThreadAttributeList(nullptr, count, 0, &size);
    std::vector<uint8_t> storage(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    QIQI_TEST_CHECK(::InitializeProcThreadAttributeList(list, count, 0, &size));
    const ScopedHandle<AttributeListTraits> list_guard(list);
    std::array<HANDLE, 2> inherited{write_.Get(), start};
    QIQI_TEST_CHECK(::UpdateProcThreadAttribute(
        list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
        sizeof(HANDLE) * (start ? 2 : 1), nullptr, nullptr));
    if (capabilities) {
        QIQI_TEST_CHECK(::UpdateProcThreadAttribute(
            list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
            capabilities, sizeof(*capabilities), nullptr, nullptr));
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;
    auto command = L"\"" + executable.wstring() + L"\" " + arguments + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(write_.Get()));
    job_.Reset(::CreateJobObjectW(nullptr, nullptr));
    QIQI_TEST_CHECK(job_);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    QIQI_TEST_CHECK(::SetInformationJobObject(
        job_.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    PROCESS_INFORMATION process{};
    const BOOL created = ::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr, nullptr, &startup.StartupInfo, &process);
    if (!created) std::cerr << "CreateProcessW error=" << ::GetLastError() << "\n";
    QIQI_TEST_CHECK(created);
    process_.Reset(process.hProcess);
    pid_ = process.dwProcessId;
    const ScopedKernelHandle thread(process.hThread);
    bool assigned = false;
    const auto stop_unassigned = gsl::finally([&]() noexcept {
        if (!assigned && !::TerminateProcess(process_.Get(), ERROR_PROCESS_ABORTED)) {
            const DWORD error = ::GetLastError();
            std::fprintf(stderr, "Unassigned test child termination failed (pid=%lu, error=%lu)\n",
                pid_, error);
            std::fflush(stderr);
        }
    });
    QIQI_TEST_CHECK(::AssignProcessToJobObject(job_.Get(), process_.Get()));
    assigned = true;
    QIQI_TEST_CHECK(::ResumeThread(thread.Get()) != MAXDWORD);
    write_.Reset();
    std::fprintf(stderr, "[CHILD-START] parent_pid=%lu, pid=%lu, executable=\"%s\"\n",
        ::GetCurrentProcessId(), pid_, WideToUtf8(executable.native()).c_str());
    std::fflush(stderr);
}

Child::~Child() {
    job_.Reset();
    const DWORD wait = ::WaitForSingleObject(process_.Get(), kTimeout);
    if (wait != WAIT_OBJECT_0) {
        const DWORD error = wait == WAIT_FAILED ? ::GetLastError() : ERROR_TIMEOUT;
        std::fprintf(stderr, "Test child cleanup failed (pid=%lu, wait=%lu, error=%lu, unwinding=%d)\n",
            pid_, wait, error, std::uncaught_exceptions() != 0);
        std::fflush(stderr);
        std::abort();
    }
    std::fprintf(stderr, "[CHILD-CLEANUP-END] parent_pid=%lu, pid=%lu, unwinding=%d\n",
        ::GetCurrentProcessId(), pid_, std::uncaught_exceptions() != 0);
    std::fflush(stderr);
}

void Child::AwaitReady() const {
    std::fprintf(stderr, "[CHILD-READY-WAIT] parent_pid=%lu, pid=%lu\n", ::GetCurrentProcessId(), pid_);
    std::fflush(stderr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeout);
    DWORD available = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        QIQI_TEST_CHECK(::PeekNamedPipe(read_.Get(), nullptr, 0, nullptr, &available, nullptr));
        if (available) {
            char ready = 0;
            DWORD read = 0;
            QIQI_TEST_CHECK(::ReadFile(read_.Get(), &ready, 1, &read, nullptr) && read == 1 && ready == 'R');
            return;
        }
        QIQI_TEST_CHECK(::WaitForSingleObject(process_.Get(), 1) == WAIT_TIMEOUT);
    }
    QIQI_TEST_CHECK(false);
}

void Child::Join() const {
    const DWORD wait = ::WaitForSingleObject(process_.Get(), kTimeout);
    const DWORD error = wait == WAIT_FAILED ? ::GetLastError() :
        wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_SUCCESS;
    if (wait != WAIT_OBJECT_0) {
        std::cerr << std::format(
            "[CHILD-WAIT] pid={}, wait={}, error={}, budget_ms={}\n",
            pid_, wait, error, kTimeout);
    }
    QIQI_TEST_CHECK(wait == WAIT_OBJECT_0);
    DWORD code = MAXDWORD;
    QIQI_TEST_CHECK(::GetExitCodeProcess(process_.Get(), &code));
    std::fprintf(stderr, "[CHILD-END] parent_pid=%lu, pid=%lu, exit_code=0x%08lX\n",
        ::GetCurrentProcessId(), pid_, code);
    std::fflush(stderr);
    QIQI_TEST_CHECK(code == 0);
}

void Child::Terminate() const {
    QIQI_TEST_CHECK(::TerminateProcess(process_.Get(), 0));
    Join();
}

void Writer(const std::filesystem::path& directory, size_t count, HANDLE start, HANDLE ready) {
    const TestStage stage(__func__);
    Channel channel(directory, kSource);
    SendReady(ready);
    QIQI_TEST_CHECK(::WaitForSingleObject(start, kTimeout) == WAIT_OBJECT_0);
    const auto started = ::GetTickCount64();
    const auto deadline = started + kTimeout;
    size_t done = 0;
    size_t lock_conflicts = 0;
    size_t revision_conflicts = 0;
    HRESULT last_error = S_OK;
    while (done < count && ::GetTickCount64() < deadline) {
        Batch batch;
        HRESULT hr = channel.Read({}, batch);
        if (hr == S_OK) {
            Version committed;
            hr = channel.Publish(batch.version, Operation::Modify, kPayload, [&]() {
                std::string text;
                QIQI_TEST_CHECK(ReadFileToUtf8String(directory / L"counter", text));
                const std::string next = std::to_string(std::stoul(text) + 1);
                const std::vector<uint8_t> bytes(next.begin(), next.end());
                return WriteFileAtomicallyResult(directory / L"counter", bytes);
            }, committed);
        }
        last_error = hr;
        if (hr == S_OK) ++done;
        else {
            if (hr == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION)) ++lock_conflicts;
            else if (hr == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) ++revision_conflicts;
            else {
                std::cerr << std::format("[WRITER-ERROR] pid={}, completed={}, HRESULT=0x{:08X}\n",
                    ::GetCurrentProcessId(), done, gsl::narrow_cast<uint32_t>(hr));
            }
            QIQI_TEST_CHECK(hr == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) ||
                            hr == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH));
        }
        ::SwitchToThread();
    }
    std::cerr << std::format(
        "[WRITER-END] pid={}, completed={}/{}, elapsed_ms={}, lock_conflicts={}, revision_conflicts={}, last_hresult=0x{:08X}\n",
        ::GetCurrentProcessId(), done, count, ::GetTickCount64() - started,
        lock_conflicts, revision_conflicts, gsl::narrow_cast<uint32_t>(last_error));
    QIQI_TEST_CHECK(done == count);
}

void TestConcurrentWriters(const std::filesystem::path& peer) {
    const TestStage stage(__func__);
    Fixture fixture(L"writers");
    Channel late(fixture.directory, kSource);
    const Version before = Current(late);
    constexpr size_t writes_per_process = 30;
    constexpr size_t total_writes = writes_per_process * 2;
    const std::string expected_counter = std::to_string(total_writes);
    constexpr std::array<uint8_t, 1> zero{'0'};
    QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"counter", zero));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const ScopedKernelHandle start(::CreateEventW(&attributes, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(start);
    const auto arguments = L"--writer \"" + fixture.directory.wstring() + L"\" " +
        std::to_wstring(writes_per_process) + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(start.Get()));
    Child first(GetModuleFilePath(nullptr), arguments, nullptr, start.Get());
    Child second(peer.empty() ? GetModuleFilePath(nullptr) : peer, arguments, nullptr, start.Get());
    first.AwaitReady();
    second.AwaitReady();
    QIQI_TEST_CHECK(::SetEvent(start.Get()));
    first.Join();
    second.Join();
    std::string counter;
    QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"counter", counter) && counter == expected_counter);
    Batch batch;
    QIQI_TEST_CHECK(late.Read({}, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::Startup && batch.version.sequence == total_writes);
    QIQI_TEST_CHECK(late.Read(before, batch) == S_OK && batch.snapshot == SnapshotReason::None &&
                    batch.version.baseline == before.baseline && batch.changes.size() == total_writes);
    uint64_t sequence = before.sequence;
    for (const auto& change : batch.changes) {
        QIQI_TEST_CHECK(change.sequence == ++sequence && change.operation == Operation::Modify);
        QIQI_TEST_CHECK(std::ranges::equal(change.payload, kPayload));
    }
    QIQI_TEST_CHECK(sequence == batch.version.sequence);
    Version applied;
    QIQI_TEST_CHECK(late.Synchronize(applied, InputBoundary::NewInput, [&]() {
        std::string snapshot;
        return ReadFileToUtf8String(fixture.directory / L"counter", snapshot) &&
            snapshot == expected_counter ? S_OK : E_FAIL;
    }, [](std::span<const Change>) { return E_FAIL; }) == S_OK);
    QIQI_TEST_CHECK(applied.baseline == before.baseline && applied.sequence == total_writes);
}

void CrashWriter(const std::filesystem::path& directory, HANDLE ready) {
    const TestStage stage(__func__);
    Channel channel(directory, kSource);
    Version committed;
    QIQI_TEST_CHECK(channel.Publish(Current(channel), Operation::Add, kPayload, [&]() -> HRESULT {
        QIQI_TEST_CHECK(WriteFileAtomically(directory / L"published", kPayload));
        SendReady(ready);
        ::WaitForSingleObject(::GetCurrentProcess(), INFINITE);
        return E_UNEXPECTED;
    }, committed) == S_OK);
}

void TestWriterExit() {
    const TestStage stage(__func__);
    Fixture fixture(L"exit");
    Channel channel(fixture.directory, kSource);
    const Version before = Current(channel);
    Child child(GetModuleFilePath(nullptr), L"--crash \"" + fixture.directory.wstring() + L"\"");
    child.AwaitReady();
    Batch batch;
    QIQI_TEST_CHECK(channel.Read(before, batch) == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION));
    child.Terminate();
    QIQI_TEST_CHECK(channel.Read(before, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::InterruptedWrite && batch.changes.empty());
    Version applied = before;
    QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput, [&]() {
        std::string source;
        QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"published", source) && source == "delta");
        return S_OK;
    }, [](std::span<const Change>) { return E_FAIL; }) == S_OK);
    QIQI_TEST_CHECK(applied.sequence == 1 && applied.baseline != before.baseline);
    Version committed;
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Reorder, {}, []() { return S_OK; }, committed) == S_OK);
    QIQI_TEST_CHECK(committed.sequence == 2 && committed.baseline == applied.baseline);
    QIQI_TEST_CHECK(channel.Read(applied, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::None && batch.changes.size() == 1);
}
} // namespace qiqi::test::changesync
