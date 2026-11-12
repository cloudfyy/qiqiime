// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/change_sync.h"
#include "common/binary_codec.h"
#include "common/file_util.h"
#include "common/logger.h"

#include <sddl.h>
#include <aclapi.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace qiqi::sync {
namespace {
constexpr size_t kHeaderBytes = 76;

HRESULT Report(HRESULT hr, const char* operation, const GUID* activity_id = nullptr) noexcept {
    if (FAILED(hr)) {
        const log::Opcode opcode = (activity_id != nullptr) ? log::Opcode::Stop : log::Opcode::Info;
        const log::LogContext ctx{log::Keyword::Sync, opcode, activity_id, nullptr};
        if (hr == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) ||
            hr == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            QIQI_LOG_CTX(log::Level::debug, ctx, "ChangeSync: {} requires retry (hr=0x{:08x})", operation,
                         gsl::narrow_cast<unsigned int>(hr));
        } else {
            QIQI_LOG_CTX(log::Level::err, ctx, "ChangeSync: {} failed (hr=0x{:08x})", operation,
                         gsl::narrow_cast<unsigned int>(hr));
        }
    }
    return hr;
}

template <typename Action>
HRESULT Checked(const char* operation, Action action, const GUID* activity_id = nullptr) noexcept {
    try {
        return Report(action(), operation, activity_id);
    } catch (const std::bad_alloc&) {
        return Report(E_OUTOFMEMORY, operation, activity_id);
    } catch (...) {
        return Report(E_UNEXPECTED, operation, activity_id);
    }
}

HRESULT DesktopPublisher() noexcept {
    ScopedKernelHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put()))
        return HRESULT_FROM_WIN32(::GetLastError());
    DWORD container = 0;
    DWORD length = 0;
    if (!::GetTokenInformation(token.Get(), TokenIsAppContainer, &container, sizeof(container), &length))
        return HRESULT_FROM_WIN32(::GetLastError());
    return container ? E_ACCESSDENIED : S_OK;
}

bool Valid(Source source) noexcept {
    return (source.type == DataType::Dictionary || source.type == DataType::PhraseFrequency) &&
        source.id != GUID_NULL;
}

constexpr bool Valid(Operation operation) noexcept {
    return operation >= Operation::Add && operation <= Operation::Reorder;
}

struct Journal {
    Source source;
    Version version;
    GUID recovery{};
    bool pending = false;
    std::vector<Change> changes;
};

std::vector<uint8_t> Encode(const Journal& journal) {
    std::vector<uint8_t> bytes;
    codec::AppendLe32(bytes, codec::kSyncJournalBinaryMagic);
    codec::AppendLe32(bytes, codec::kSyncJournalBinaryVer);
    codec::AppendLe32(bytes, std::to_underlying(journal.source.type));
    codec::AppendGuid(bytes, journal.source.id);
    codec::AppendGuid(bytes, journal.version.baseline);
    codec::AppendLe64(bytes, journal.version.sequence);
    codec::AppendGuid(bytes, journal.recovery);
    codec::AppendLe32(bytes, journal.pending ? 1u : 0u);
    codec::AppendLe32(bytes, gsl::narrow_cast<uint32_t>(journal.changes.size()));
    for (const auto& change : journal.changes) {
        codec::AppendLe64(bytes, change.sequence);
        codec::AppendLe32(bytes, std::to_underlying(change.operation));
        codec::AppendLe32(bytes, gsl::narrow_cast<uint32_t>(change.payload.size()));
        bytes.insert(bytes.end(), change.payload.begin(), change.payload.end());
    }
    codec::AppendLe64(bytes, codec::ComputeFnv1a64(bytes));
    return bytes;
}

HRESULT Decode(std::span<const uint8_t> bytes, Source source, Journal& journal) {
    if (bytes.size() < kHeaderBytes + 8 || bytes.size() > Channel::kMaxJournalBytes)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    codec::BinarySpanReader checksum(bytes.last(8));
    if (codec::ComputeFnv1a64(bytes.first(bytes.size() - 8)) != checksum.ReadLe64())
        return HRESULT_FROM_WIN32(ERROR_CRC);
    codec::BinarySpanReader input(bytes.first(bytes.size() - 8));
    if (input.ReadLe32() != codec::kSyncJournalBinaryMagic ||
        input.ReadLe32() != codec::kSyncJournalBinaryVer)
        return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    journal.source.type = static_cast<DataType>(input.ReadLe32());
    journal.source.id = input.ReadGuid();
    journal.version.baseline = input.ReadGuid();
    journal.version.sequence = input.ReadLe64();
    journal.recovery = input.ReadGuid();
    const uint64_t flags = input.ReadLe32();
    const uint64_t count = input.ReadLe32();
    if (journal.source != source || journal.version.baseline == GUID_NULL || flags > 1 ||
        count > Channel::kMaxRecords || count > journal.version.sequence)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    journal.pending = flags != 0;
    if (journal.pending && journal.recovery == GUID_NULL)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (uint64_t i = 0; i < count; ++i) {
        Change change;
        change.sequence = input.ReadLe64();
        change.operation = static_cast<Operation>(input.ReadLe32());
        const uint64_t length = input.ReadLe32();
        if (length > Channel::kMaxPayloadBytes || !Valid(change.operation) ||
            change.sequence != journal.version.sequence - count + i + 1)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        change.payload = input.ReadBytes(gsl::narrow_cast<size_t>(length));
        journal.changes.push_back(std::move(change));
    }
    return input.Valid() && input.Empty() ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

class FileLock final {
public:
    HRESULT Acquire(const std::filesystem::path& directory, bool exclusive) {
        handle_.Reset(::CreateFileW((directory / L"writer.lock").c_str(),
            exclusive ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr));
        if (!handle_) return HRESULT_FROM_WIN32(::GetLastError());
        OVERLAPPED position{};
        if (!::LockFileEx(handle_.Get(), LOCKFILE_FAIL_IMMEDIATELY |
            (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &position))
            return HRESULT_FROM_WIN32(::GetLastError());
        return S_OK;
    }
private:
    // Closing this non-inheritable handle also releases the byte-range lock after process exit.
    ScopedFileHandle handle_;
};

HRESULT Load(const std::filesystem::path& directory, Source source, Journal& journal) {
    const ScopedFileHandle file(::CreateFileW((directory / L"journal.bin").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr));
    if (!file) return HRESULT_FROM_WIN32(::GetLastError());
    if (::GetFileType(file.Get()) != FILE_TYPE_DISK) return E_INVALIDARG;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file.Get(), &size)) return HRESULT_FROM_WIN32(::GetLastError());
    if (size.QuadPart < 0 || size.QuadPart > Channel::kMaxJournalBytes)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    std::vector<uint8_t> bytes(gsl::narrow_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!::ReadFile(file.Get(), bytes.data(), gsl::narrow_cast<DWORD>(bytes.size()), &read, nullptr))
        return HRESULT_FROM_WIN32(::GetLastError());
    if (read != bytes.size()) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Sync,
            "ChangeSync: short journal read (expected={}, read={}, error={})",
            bytes.size(), read, ERROR_SUCCESS);
        return E_FAIL;
    }
    return Decode(bytes, source, journal);
}

HRESULT Save(const std::filesystem::path& directory, const Journal& journal) {
    return WriteFileAtomicallyResult(directory / L"journal.bin", Encode(journal));
}

void Recover(Journal& journal) noexcept {
    journal.version.baseline = journal.recovery;
    journal.pending = false;
    journal.changes.clear();
}

Batch Since(const Journal& journal, Version applied, const GUID* activity_id = nullptr) {
    Batch batch;
    batch.version = journal.version;
    if (journal.pending) batch.version.baseline = journal.recovery;
    if (applied == batch.version) return batch;
    if (journal.pending) batch.snapshot = SnapshotReason::InterruptedWrite;
    else if (applied.baseline == GUID_NULL) batch.snapshot = SnapshotReason::Startup;
    else if (applied.baseline != journal.version.baseline) batch.snapshot = SnapshotReason::BaselineMismatch;
    else if (applied.sequence > journal.version.sequence ||
        applied.sequence < journal.version.sequence - journal.changes.size())
        batch.snapshot = SnapshotReason::VersionGap;
    else {
        for (const auto& change : journal.changes)
            if (change.sequence > applied.sequence) batch.changes.push_back(change);
    }
    if (batch.snapshot != SnapshotReason::None)
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Sync,
            activity_id,
            "ChangeSync: full snapshot required (reason={}, version={})",
            std::to_underlying(batch.snapshot), batch.version.sequence);
    return batch;
}
} // namespace

HRESULT Channel::Create(const std::filesystem::path& directory, Source source) noexcept {
    return Checked("Create", [&]() -> HRESULT {
        if (!directory.is_absolute() || !Valid(source)) return E_INVALIDARG;
        HRESULT hr = DesktopPublisher();
        if (FAILED(hr)) return hr;
        PSECURITY_DESCRIPTOR raw = nullptr;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)(A;OICI;FRFX;;;AC)(A;OICI;FRFX;;;S-1-15-2-2)",
            SDDL_REVISION_1, &raw, nullptr))
            return HRESULT_FROM_WIN32(::GetLastError());
        const ScopedLocalAlloc descriptor(raw);
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), raw, FALSE};
        if (!::CreateDirectoryW(directory.c_str(), &attributes)) {
            const DWORD error = ::GetLastError();
            if (error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
            const DWORD flags = ::GetFileAttributesW(directory.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(::GetLastError());
            if (!(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
                return E_INVALIDARG;
        }
        BOOL present = FALSE;
        BOOL defaulted = FALSE;
        PACL dacl = nullptr;
        if (!::GetSecurityDescriptorDacl(raw, &present, &dacl, &defaulted))
            return HRESULT_FROM_WIN32(::GetLastError());
        if (!present || !dacl) return E_UNEXPECTED;
        auto native_directory = directory.wstring();
        const DWORD acl_error = ::SetNamedSecurityInfoW(native_directory.data(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
        if (acl_error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(acl_error);
        const ScopedFileHandle file(::CreateFileW((directory / L"writer.lock").c_str(),
            GENERIC_READ | GENERIC_WRITE | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr));
        if (!file) return HRESULT_FROM_WIN32(::GetLastError());
        const DWORD lock_acl_error = ::SetSecurityInfo(file.Get(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
        if (lock_acl_error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lock_acl_error);
        FileLock lock;
        hr = lock.Acquire(directory, true);
        if (FAILED(hr)) return hr;
        Journal journal;
        hr = Load(directory, source, journal);
        if (hr == S_OK) {
            auto native_journal = (directory / L"journal.bin").wstring();
            return HRESULT_FROM_WIN32(::SetNamedSecurityInfoW(native_journal.data(), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr, nullptr, dacl, nullptr));
        }
        if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return hr;
        journal.source = source;
        hr = ::CoCreateGuid(&journal.version.baseline);
        return FAILED(hr) ? hr : Save(directory, journal);
    });
}

HRESULT Channel::Watch() noexcept {
    return Checked("Watch", [&]() -> HRESULT {
        if (!directory_.is_absolute() || !Valid(source_)) return E_INVALIDARG;
        const HANDLE handle = ::FindFirstChangeNotificationW(directory_.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        const DWORD error = handle == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
        notification_.Reset(handle);
        return handle != INVALID_HANDLE_VALUE ? S_OK : HRESULT_FROM_WIN32(error);
    });
}

HRESULT Channel::Rearm() noexcept {
    return Report(notification_ && ::FindNextChangeNotification(notification_.Get()) ?
        S_OK : (notification_ ? HRESULT_FROM_WIN32(::GetLastError()) : E_UNEXPECTED), "Rearm");
}

HRESULT Channel::Read(Version applied, Batch& batch) const noexcept {
    return Checked("Read", [&]() -> HRESULT {
        if (!directory_.is_absolute() || !Valid(source_)) return E_INVALIDARG;
        FileLock lock;
        HRESULT hr = lock.Acquire(directory_, false);
        if (FAILED(hr)) return hr;
        Journal journal;
        hr = Load(directory_, source_, journal);
        if (SUCCEEDED(hr)) batch = Since(journal, applied);
        return hr;
    });
}

HRESULT Channel::Publish(Version expected, Operation operation, std::span<const uint8_t> payload,
                       const std::function<HRESULT()>& write_source, Version& committed) const noexcept {
    const GUID act_id = log::CreateActivityId();
    return Checked("Publish", [&]() -> HRESULT {
        if (!directory_.is_absolute() || !Valid(source_) || !Valid(operation) ||
            payload.size() > kMaxPayloadBytes || !write_source) return E_INVALIDARG;
        HRESULT hr = DesktopPublisher();
        if (FAILED(hr)) return hr;
        FileLock lock;
        hr = lock.Acquire(directory_, true);
        if (FAILED(hr)) return hr;
        Journal journal;
        hr = Load(directory_, source_, journal);
        if (FAILED(hr)) return hr;
        if (journal.pending) {
            Recover(journal);
            hr = Save(directory_, journal);
            if (FAILED(hr)) return hr;
        }
        if (journal.version != expected) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        if (journal.version.sequence == std::numeric_limits<uint64_t>::max())
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        QIQI_LOG_ACT_START(
            log::Level::debug,
            log::Keyword::Sync,
            &act_id,
            nullptr,
            "ChangeSync: publish begin (type={}, op={}, expected_seq={})",
            std::to_underlying(source_.type),
            std::to_underlying(operation),
            expected.sequence);
        Journal next = journal;
        ++next.version.sequence;
        next.changes.push_back(Change{next.version.sequence, operation, {payload.begin(), payload.end()}});
        size_t bytes = kHeaderBytes + 8;
        for (const auto& change : next.changes) bytes += 16 + change.payload.size();
        while (next.changes.size() > kMaxRecords || bytes > kMaxJournalBytes) {
            bytes -= 16 + next.changes.front().payload.size();
            next.changes.erase(next.changes.begin());
        }
        journal.pending = true;
        journal.version.sequence = next.version.sequence;
        journal.changes.clear();
        hr = ::CoCreateGuid(&journal.recovery);
        if (FAILED(hr)) return hr;
        hr = Save(directory_, journal);
        if (FAILED(hr)) return hr;
        hr = Checked("WriteSource", write_source, &act_id);
        if (hr != S_OK) {
            Recover(journal);
            Report(Save(directory_, journal), "InvalidateAfterFailedWrite", &act_id);
            return hr;
        }
        hr = Save(directory_, next);
        if (SUCCEEDED(hr)) {
            committed = next.version;
            QIQI_LOG_ACT_STOP(
                log::Level::debug,
                log::Keyword::Sync,
                &act_id,
                "ChangeSync: publish committed (type={}, op={}, committed_seq={})",
                std::to_underlying(source_.type),
                std::to_underlying(operation),
                committed.sequence);
        }
        return hr;
    }, &act_id);
}

HRESULT Channel::Synchronize(Version& applied, InputBoundary boundary,
    const std::function<HRESULT()>& load_snapshot,
    const std::function<HRESULT(std::span<const Change>)>& apply_changes) const noexcept {
    const GUID act_id = log::CreateActivityId();
    return Checked("Synchronize", [&]() -> HRESULT {
        if (!directory_.is_absolute() || !Valid(source_) || !load_snapshot || !apply_changes ||
            (boundary != InputBoundary::Composing && boundary != InputBoundary::NewInput)) return E_INVALIDARG;
        if (boundary == InputBoundary::Composing) return S_FALSE;
        FileLock lock;
        HRESULT hr = lock.Acquire(directory_, false);
        if (FAILED(hr)) return hr;
        Journal journal;
        hr = Load(directory_, source_, journal);
        if (FAILED(hr)) return hr;
        const auto batch = Since(journal, applied, &act_id);
        if (batch.snapshot != SnapshotReason::None) {
            QIQI_LOG_ACT_START(
                log::Level::debug,
                log::Keyword::Sync,
                &act_id,
                nullptr,
                "ChangeSync: synchronize snapshot begin (reason={}, target_seq={})",
                std::to_underlying(batch.snapshot),
                batch.version.sequence);
            hr = load_snapshot();
        } else if (!batch.changes.empty()) {
            QIQI_LOG_ACT_START(
                log::Level::debug,
                log::Keyword::Sync,
                &act_id,
                nullptr,
                "ChangeSync: synchronize delta begin (changes={}, target_seq={})",
                batch.changes.size(),
                batch.version.sequence);
            hr = apply_changes(batch.changes);
        }
        if (hr == S_OK && (batch.snapshot != SnapshotReason::None || !batch.changes.empty())) {
            applied = batch.version;
            QIQI_LOG_ACT_STOP(
                log::Level::debug,
                log::Keyword::Sync,
                &act_id,
                "ChangeSync: synchronize complete (applied_seq={})",
                applied.sequence);
        } else if (hr == S_OK) {
            applied = batch.version;
        }
        return hr;
    }, &act_id);
}
} // namespace qiqi::sync
