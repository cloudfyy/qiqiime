#include "common/test_support.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"
#include "common/change_sync.h"

#include <sddl.h>
#include <userenv.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <new>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>

// Unlike abort, unwinding also removes the persistent test AppContainer profile.
#undef QIQI_TEST_CHECK
#define QIQI_TEST_CHECK(expr) do { \
    if (!(expr)) throw std::runtime_error(std::format("{}:{}: {}", __FILE__, __LINE__, #expr)); \
} while (false)

namespace {
using namespace qiqi;
using namespace qiqi::sync;

constexpr DWORD kTimeout = 15000;
constexpr Source kSource{DataType::Dictionary,
    {0x815eaa57, 0x5676, 0x41fb, {0xaf, 0x9b, 0x41, 0x89, 0x11, 0x68, 0xc3, 0x17}}};
constexpr std::array<uint8_t, 5> kPayload{'d', 'e', 'l', 't', 'a'};

std::wstring GuidText() {
    GUID id{};
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&id)));
    std::array<wchar_t, 40> text{};
    QIQI_TEST_CHECK(::StringFromGUID2(id, text.data(), gsl::narrow_cast<int>(text.size())) > 0);
    return text.data();
}

class Fixture final {
public:
    explicit Fixture(std::wstring_view purpose) : directory(std::filesystem::temp_directory_path() /
        (L"QiQiSync-" + std::wstring(purpose) + L"-" + GuidText())) {
        QIQI_TEST_CHECK(Channel::Create(directory, kSource) == S_OK);
    }

    ~Fixture() {
        try {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            if (error) {
                std::fprintf(stderr, "Test directory cleanup failed (error=%d)\n", error.value());
                std::abort();
            }
        } catch (const std::bad_alloc&) {
            std::fputs("Test directory cleanup ran out of memory.\n", stderr);
            std::abort();
        }
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;
    const std::filesystem::path directory;
};

struct AttributeListTraits {
    using HandleType = LPPROC_THREAD_ATTRIBUTE_LIST;
    static HandleType Invalid() noexcept { return nullptr; }
    static bool IsValid(const void* value) noexcept { return value != nullptr; }
    static void Close(HandleType value) noexcept { ::DeleteProcThreadAttributeList(value); }
};

class Child final {
public:
    Child(const std::filesystem::path& executable, std::wstring arguments,
          SECURITY_CAPABILITIES* capabilities = nullptr, HANDLE start = nullptr) {
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
        const ScopedKernelHandle thread(process.hThread);
        bool assigned = false;
        const auto stop_unassigned = gsl::finally([&]() noexcept {
            if (!assigned && !::TerminateProcess(process_.Get(), ERROR_PROCESS_ABORTED)) {
                const DWORD error = ::GetLastError();
                std::fprintf(stderr, "Unassigned test child termination failed (error=%lu)\n", error);
            }
        });
        QIQI_TEST_CHECK(::AssignProcessToJobObject(job_.Get(), process_.Get()));
        assigned = true;
        QIQI_TEST_CHECK(::ResumeThread(thread.Get()) != MAXDWORD);
        write_.Reset();
    }

    void AwaitReady() const {
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

    void Join() const {
        QIQI_TEST_CHECK(::WaitForSingleObject(process_.Get(), kTimeout) == WAIT_OBJECT_0);
        DWORD code = MAXDWORD;
        QIQI_TEST_CHECK(::GetExitCodeProcess(process_.Get(), &code));
        if (code) std::cerr << "Child exit code=" << code << "\n";
        QIQI_TEST_CHECK(code == 0);
    }

    void Terminate() const {
        QIQI_TEST_CHECK(::TerminateProcess(process_.Get(), 0));
        Join();
    }

private:
    ScopedKernelHandle read_;
    ScopedKernelHandle write_;
    ScopedKernelHandle job_;
    ScopedKernelHandle process_;
};

struct ChangeHandleTraits : FileHandleTraits {
    static void Close(HANDLE value) noexcept { ::FindCloseChangeNotification(value); }
};

void SendReady(HANDLE pipe) {
    constexpr char ready = 'R';
    DWORD written = 0;
    QIQI_TEST_CHECK(::WriteFile(pipe, &ready, 1, &written, nullptr) && written == 1);
}

void ProbeReader(const std::filesystem::path& directory, HANDLE ready) {
    ScopedKernelHandle token;
    QIQI_TEST_CHECK(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put()));
    DWORD container = 0;
    DWORD size = 0;
    QIQI_TEST_CHECK(::GetTokenInformation(
        token.Get(), TokenIsAppContainer, &container, sizeof(container), &size) && container);
    const auto path = directory / L"lock";
    const ScopedFileHandle reader(::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(reader);
    OVERLAPPED position{};
    QIQI_TEST_CHECK(::LockFileEx(reader.Get(), LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &position));
    QIQI_TEST_CHECK(::UnlockFileEx(reader.Get(), 0, 1, 0, &position));
    const ScopedFileHandle writer(::CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(!writer && ::GetLastError() == ERROR_ACCESS_DENIED);
    const ScopedHandle<ChangeHandleTraits> notification(::FindFirstChangeNotificationW(
        directory.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE));
    QIQI_TEST_CHECK(notification);
    Channel channel(directory, kSource);
    QIQI_TEST_CHECK(channel.Watch() == S_OK);
    Batch initial;
    QIQI_TEST_CHECK(channel.Read({}, initial) == S_OK);
    Version applied = initial.version;
    Version denied;
    bool called = false;
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, kPayload,
        [&]() { called = true; return S_OK; }, denied) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(!called && denied == Version{});
    QIQI_TEST_CHECK(Channel::Create(directory / L"forbidden", kSource) == E_ACCESSDENIED);
    SendReady(ready);
    const auto deadline = ::GetTickCount64() + kTimeout;
    for (;;) {
        const auto now = ::GetTickCount64();
        if (now >= deadline) break;
        QIQI_TEST_CHECK(::WaitForSingleObject(notification.Get(),
            gsl::narrow_cast<DWORD>(deadline - now)) == WAIT_OBJECT_0);
        QIQI_TEST_CHECK(::FindNextChangeNotification(notification.Get()));
        if (::GetFileAttributesW((directory / L"published").c_str()) != INVALID_FILE_ATTRIBUTES) {
            HRESULT hr = E_PENDING;
            bool received = false;
            while (::GetTickCount64() < deadline) {
                hr = channel.Synchronize(applied, InputBoundary::NewInput,
                    []() { QIQI_TEST_CHECK(false); return E_FAIL; },
                    [&](std::span<const Change> changes) {
                        std::string contents;
                        QIQI_TEST_CHECK(ReadFileToUtf8String(directory / L"published", contents) && contents == "delta");
                        QIQI_TEST_CHECK(changes.size() == 1);
                        QIQI_TEST_CHECK(changes.front().operation == Operation::Add);
                        QIQI_TEST_CHECK(std::ranges::equal(changes.front().payload, kPayload));
                        received = true;
                        return S_OK;
                    });
                if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION)) break;
                ::SwitchToThread();
            }
            QIQI_TEST_CHECK(hr == S_OK && received && applied.sequence == 1);
            QIQI_TEST_CHECK(channel.Rearm() == S_OK);
            return;
        }
    }
    QIQI_TEST_CHECK(false);
}

void TestAppContainerTransport(const std::filesystem::path& peer = {}) {
    Fixture fixture(L"appcontainer");
    PSECURITY_DESCRIPTOR raw = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;FA;;;OW)", SDDL_REVISION_1, &raw, nullptr));
    const ScopedLocalAlloc private_acl(raw);
    QIQI_TEST_CHECK(::SetFileSecurityW(fixture.directory.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
    QIQI_TEST_CHECK(Channel::Create(fixture.directory, kSource) == S_OK);
    const auto lock_path = fixture.directory / L"lock";
    const ScopedFileHandle lock(::CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(lock);
    const auto executable = fixture.directory / L"reader.exe";
    std::filesystem::copy_file(peer.empty() ? GetModuleFilePath(nullptr) : peer, executable);
    const auto name = L"QiQiSyncTest." + GuidText();
    PSID sid = nullptr;
    const HRESULT profile = ::CreateAppContainerProfile(name.c_str(), name.c_str(), L"QiQi sync test",
                                                        nullptr, 0, &sid);
    if (FAILED(profile)) std::cerr << "CreateAppContainerProfile hr=" << std::hex << profile << "\n";
    QIQI_TEST_CHECK(SUCCEEDED(profile));
    const auto cleanup = gsl::finally([&name, sid]() noexcept {
        ::FreeSid(sid);
        const HRESULT hr = ::DeleteAppContainerProfile(name.c_str());
        if (FAILED(hr)) {
            std::fprintf(stderr, "Test AppContainer profile cleanup failed (hr=0x%08lx)\n",
                         gsl::narrow_cast<unsigned long>(hr));
            std::abort();
        }
    });
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;
    Child child(executable, L"--probe \"" + fixture.directory.wstring() + L"\"", &capabilities);
    child.AwaitReady();
    Channel channel(fixture.directory, kSource);
    Batch batch;
    QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
    Version committed;
    QIQI_TEST_CHECK(channel.Publish(batch.version, Operation::Add, kPayload, [&]() {
        return WriteFileAtomicallyResult(fixture.directory / L"published", kPayload);
    }, committed) == S_OK);
    child.Join();
}

Version Current(const Channel& channel) {
    Batch batch;
    QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
    return batch.version;
}

void TestJournal() {
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

void Writer(const std::filesystem::path& directory, size_t count, HANDLE start, HANDLE ready) {
    Channel channel(directory, kSource);
    SendReady(ready);
    QIQI_TEST_CHECK(::WaitForSingleObject(start, kTimeout) == WAIT_OBJECT_0);
    const auto deadline = ::GetTickCount64() + kTimeout;
    size_t done = 0;
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
        if (hr == S_OK) ++done;
        else QIQI_TEST_CHECK(hr == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) ||
                             hr == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH));
        ::SwitchToThread();
    }
    QIQI_TEST_CHECK(done == count);
}

void TestConcurrentWriters(const std::filesystem::path& peer = {}) {
    Fixture fixture(L"writers");
    constexpr std::array<uint8_t, 1> zero{'0'};
    QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"counter", zero));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const ScopedKernelHandle start(::CreateEventW(&attributes, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(start);
    const auto arguments = L"--writer \"" + fixture.directory.wstring() + L"\" 30 " +
        std::to_wstring(reinterpret_cast<uintptr_t>(start.Get()));
    Child first(GetModuleFilePath(nullptr), arguments, nullptr, start.Get());
    Child second(peer.empty() ? GetModuleFilePath(nullptr) : peer, arguments, nullptr, start.Get());
    first.AwaitReady();
    second.AwaitReady();
    QIQI_TEST_CHECK(::SetEvent(start.Get()));
    first.Join();
    second.Join();
    std::string counter;
    QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"counter", counter) && counter == "60");
    Channel late(fixture.directory, kSource);
    Batch batch;
    QIQI_TEST_CHECK(late.Read({}, batch) == S_OK &&
                    batch.snapshot == SnapshotReason::Startup && batch.version.sequence == 60);
    Version applied;
    QIQI_TEST_CHECK(late.Synchronize(applied, InputBoundary::NewInput, [&]() {
        std::string snapshot;
        return ReadFileToUtf8String(fixture.directory / L"counter", snapshot) && snapshot == "60" ? S_OK : E_FAIL;
    }, [](std::span<const Change>) { return E_FAIL; }) == S_OK);
    QIQI_TEST_CHECK(applied.sequence == 60);
}

void CrashWriter(const std::filesystem::path& directory, HANDLE ready) {
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

void TestCommitFailure() {
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
} // namespace

int wmain(int argc, wchar_t** argv) try {
    const std::span<wchar_t*> arguments(argv, gsl::narrow_cast<size_t>(argc));
    if (arguments.size() == 3 && std::wstring_view(gsl::at(arguments, 1)) == L"--peer") {
        TestConcurrentWriters(gsl::at(arguments, 2));
        TestAppContainerTransport(gsl::at(arguments, 2));
        std::cout << "[PASS] Mixed-architecture writers and AppContainer reader.\n";
        return 0;
    }
    if (arguments.size() == 4 && std::wstring_view(gsl::at(arguments, 1)) == L"--probe") {
        const ScopedKernelHandle ready(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 3), nullptr, 10)));
        ProbeReader(gsl::at(arguments, 2), ready.Get());
        return 0;
    }
    if (arguments.size() == 6 && std::wstring_view(gsl::at(arguments, 1)) == L"--writer") {
        const ScopedKernelHandle start(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 4), nullptr, 10)));
        const ScopedKernelHandle ready(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 5), nullptr, 10)));
        Writer(gsl::at(arguments, 2), gsl::narrow_cast<size_t>(_wcstoui64(gsl::at(arguments, 3), nullptr, 10)),
               start.Get(), ready.Get());
        return 0;
    }
    if (arguments.size() == 4 && std::wstring_view(gsl::at(arguments, 1)) == L"--crash") {
        const ScopedKernelHandle ready(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 3), nullptr, 10)));
        CrashWriter(gsl::at(arguments, 2), ready.Get());
        return 0;
    }
    TestJournal();
    TestRetentionAndCorruption();
    TestConcurrentWriters();
    TestWriterExit();
    TestCommitFailure();
    TestAppContainerTransport();
    std::cout << "[PASS] Cross-process transport and real AppContainer read-only access.\n";
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "[FAIL] %s\n", error.what());
    return 1;
} catch (...) {
    std::fputs("[FAIL] Unexpected synchronization-test exception.\n", stderr);
    return 1;
}
