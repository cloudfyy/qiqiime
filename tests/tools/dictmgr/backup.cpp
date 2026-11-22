// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backupfixture.h"
#include "common/etw.h"

namespace qiqi::dict_manager::tests {

namespace {
void PrintRestoreFixtureTrace(std::span<const qiqi::test::LogEvent> events) {
    for (const auto& event : events) {
        std::cerr << "[RESTORE-TRACE] " << event.message << '\n';
    }
    std::cerr << std::flush;
}

void RunBackupRestoreResults() {
    backup::TestZipCompression();
    backup::BackupFixture fixture;
    backup::TestBackupConfig(fixture);
    backup::TestInvalidConfig(fixture);
    backup::TestCustomDictionary(fixture);
    backup::TestRestoreWithBom(fixture);
    backup::TestRestoreValidation(fixture);
    backup::TestRestoreFailures(fixture);
    backup::TestMergeReplace(fixture);
    backup::TestRestoreMessages();
}
} // namespace

void TestBackupRestoreResults() {
    std::exception_ptr failure;
    const auto events = qiqi::test::CaptureLogEvents([&](TRACEHANDLE) {
        try {
            RunBackupRestoreResults();
        } catch (const std::exception&) {
            failure = std::current_exception();
        }
    });
    if (failure) {
        PrintRestoreFixtureTrace(std::span(events).last(std::min<size_t>(80, events.size())));
        std::rethrow_exception(failure);
    }
}

} // namespace qiqi::dict_manager::tests
