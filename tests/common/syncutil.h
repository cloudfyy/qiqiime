// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/support.h"
#include "common/cleanup.h"
#include "common/fileutil.h"
#include "common/RAII.h"
#include "common/changesync.h"
#include "ime/engine/engine.h"

#include <sddl.h>
#include <userenv.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <new>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Unlike abort, unwinding also removes the persistent test AppContainer profile.
#undef QIQI_TEST_CHECK
#define QIQI_TEST_CHECK(expr) do { \
    if (!(expr)) { \
        ::qiqi::test::ReportTestCleanupFailure(__FILE__, __LINE__, #expr); \
        throw std::runtime_error(std::format("{}:{}: {}", __FILE__, __LINE__, #expr)); \
    } \
} while (false)

namespace qiqi::test::changesync {
using namespace qiqi;
using namespace qiqi::sync;
using qiqi::test::RemoveDirectoryWithSharingRetry;

inline constexpr DWORD kTimeout = 15000;
inline constexpr DWORD kCleanupRetryBudget = 2000;
inline constexpr Source kSource{DataType::Dictionary,
    {0x815eaa57, 0x5676, 0x41fb, {0xaf, 0x9b, 0x41, 0x89, 0x11, 0x68, 0xc3, 0x17}}};
inline constexpr std::array<uint8_t, 5> kPayload{'d', 'e', 'l', 't', 'a'};

class TestStage final {
public:
    explicit TestStage(const char* name) noexcept : name_(name), started_(::GetTickCount64()) {
        Write("CASE-BEGIN");
    }
    ~TestStage() noexcept { Write("CASE-END"); }
    TestStage(const TestStage&) = delete;
    TestStage& operator=(const TestStage&) = delete;
    TestStage(TestStage&&) = delete;
    TestStage& operator=(TestStage&&) = delete;

private:
    void Write(const char* event) const noexcept {
        const ScopedLastErrorRestore preserve_error;
        std::fprintf(stderr, "[%s] %s, pid=%lu, elapsed_ms=%llu, unwinding=%d\n",
            event, name_, ::GetCurrentProcessId(), ::GetTickCount64() - started_,
            std::uncaught_exceptions() != 0);
        std::fflush(stderr);
    }
    const char* const name_;
    const ULONGLONG started_;
};

inline void ReportDirectoryCleanupFailure(const std::filesystem::path& directory,
                                          const std::error_code& error, FILE* output = stderr) {
    std::fprintf(output,
        "[CLEANUP-FAIL] pid=%lu, directory=\"%s\", category=%s, error=%d, unwinding=%d\n",
        ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str(), error.category().name(), error.value(),
        std::uncaught_exceptions() != 0);
    std::fflush(output);

    // Probe delete access without deleting or following reparse points; this is not the original error.
    const auto probe = [output](const std::filesystem::path& path) {
        const ScopedFileHandle handle(::CreateFileW(path.c_str(), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        const DWORD probe_error = handle ? ERROR_SUCCESS : ::GetLastError();
        std::fprintf(output, "[CLEANUP-PROBE] pid=%lu, path=\"%s\", delete_open_error=%lu\n",
            ::GetCurrentProcessId(), WideToUtf8(path.native()).c_str(), probe_error);
        std::fflush(output);
    };
    probe(directory);
    std::error_code enumeration_error;
    std::filesystem::recursive_directory_iterator entry(directory, enumeration_error);
    const std::filesystem::recursive_directory_iterator end;
    while (!enumeration_error && entry != end) {
        probe(entry->path());
        entry.increment(enumeration_error);
    }
    if (enumeration_error) {
        std::fprintf(output,
            "[CLEANUP-ENUMERATION-FAIL] pid=%lu, directory=\"%s\", category=%s, error=%d\n",
            ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str(), enumeration_error.category().name(),
            enumeration_error.value());
        std::fflush(output);
    }
}

inline std::wstring GuidText() {
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
        std::fprintf(stderr, "[FIXTURE-BEGIN] pid=%lu, directory=\"%s\"\n",
            ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str());
        std::fflush(stderr);
        QIQI_TEST_CHECK(Channel::Create(directory, kSource) == S_OK);
    }

    ~Fixture() {
        try {
            std::fprintf(stderr, "[CLEANUP-BEGIN] pid=%lu, directory=\"%s\", unwinding=%d\n",
                ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str(), std::uncaught_exceptions() != 0);
            std::fflush(stderr);
            const auto error = RemoveDirectoryWithSharingRetry(directory, kCleanupRetryBudget, ::Sleep);
            if (error) {
                ReportDirectoryCleanupFailure(directory, error);
                std::abort();
            }
            std::fprintf(stderr, "[CLEANUP-END] pid=%lu, directory=\"%s\"\n",
                ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str());
            std::fflush(stderr);
        } catch (const std::bad_alloc&) {
            std::fprintf(stderr, "Test directory cleanup ran out of memory (pid=%lu, directory=\"%ls\").\n",
                ::GetCurrentProcessId(), directory.c_str());
            std::fflush(stderr);
            std::abort();
        }
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;
    const std::filesystem::path directory;
};

class Child final {
public:
    Child(const std::filesystem::path& executable, std::wstring arguments,
          SECURITY_CAPABILITIES* capabilities = nullptr, HANDLE start = nullptr, bool restricted_packages = false);

    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&&) = delete;
    Child& operator=(Child&&) = delete;

    ~Child();
    void AwaitReady() const;
    void Join() const;
    void Terminate() const;

private:
    ScopedKernelHandle read_;
    ScopedKernelHandle write_;
    ScopedKernelHandle job_;
    ScopedKernelHandle process_;
    DWORD pid_ = 0;
};

inline void SendReady(HANDLE pipe) {
    constexpr char ready = 'R';
    DWORD written = 0;
    QIQI_TEST_CHECK(::WriteFile(pipe, &ready, 1, &written, nullptr) && written == 1);
}

inline Version Current(const Channel& channel) {
    Batch batch;
    QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
    return batch.version;
}

void TestFailureDiagnostics();
void TestDirectoryCleanupRetry();
void TestJournal();
void TestRetentionAndCorruption();
void TestConcurrentWriters(const std::filesystem::path& peer = {});
void TestWriterExit();
void TestCommitFailure();
void TestReaderInterferenceCharacterization(const std::filesystem::path& peer = {});
void TestAppContainerTransport(const std::filesystem::path& peer = {});

void HoldReader(const std::filesystem::path& directory, std::wstring_view mode,
                HANDLE release, HANDLE ready);
void ProbeReader(const std::filesystem::path& directory, const wchar_t* registry_subkey, HANDLE ready);
void ProbeDictionaryOrder(const std::filesystem::path& directory, const wchar_t* registry_subkey,
                          bool registry_read);
void Writer(const std::filesystem::path& directory, size_t count, HANDLE start, HANDLE ready);
void CrashWriter(const std::filesystem::path& directory, HANDLE ready);
} // namespace qiqi::test::changesync
