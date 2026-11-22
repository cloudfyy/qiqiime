// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/syncutil.h"

int wmain(int argc, wchar_t** argv) try {
    using namespace qiqi::test::changesync;
    const std::span<wchar_t*> arguments(argv, gsl::narrow_cast<size_t>(argc));
    if (arguments.size() == 3 && std::wstring_view(gsl::at(arguments, 1)) == L"--peer") {
        TestConcurrentWriters(gsl::at(arguments, 2));
        TestReaderInterferenceCharacterization(gsl::at(arguments, 2));
        TestAppContainerTransport(gsl::at(arguments, 2));
        std::cout << "[PASS] Mixed-architecture writers and AppContainer reader.\n";
        return 0;
    }
    if (arguments.size() == 6 && std::wstring_view(gsl::at(arguments, 1)) == L"--hold-reader") {
        const ScopedKernelHandle release(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 4), nullptr, 10)));
        const ScopedKernelHandle ready(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 5), nullptr, 10)));
        HoldReader(gsl::at(arguments, 2), gsl::at(arguments, 3), release.Get(), ready.Get());
        return 0;
    }
    if (arguments.size() == 5 && std::wstring_view(gsl::at(arguments, 1)) == L"--probe") {
        const ScopedKernelHandle ready(reinterpret_cast<HANDLE>(_wcstoui64(gsl::at(arguments, 4), nullptr, 10)));
        ProbeReader(gsl::at(arguments, 2), gsl::at(arguments, 3), ready.Get());
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
    TestFailureDiagnostics();
    TestDirectoryCleanupRetry();
    TestJournal();
    TestRetentionAndCorruption();
    TestConcurrentWriters();
    TestWriterExit();
    TestCommitFailure();
    TestReaderInterferenceCharacterization();
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
