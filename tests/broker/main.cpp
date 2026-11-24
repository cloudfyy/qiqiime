// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/configstore.h"
#include "common/support.h"

#include <gsl/gsl>
#include <atomic>
#include <filesystem>
#include <thread>

namespace trace = qiqi::trace;

namespace {

using qiqi::QiQiConfig;
using qiqi::ImeSkinId;

void CheckEqual(
    const QiQiConfig& expected,
    const QiQiConfig& actual) {
    qiqi::ForEachConfigField(
        [&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(expected) ==
                field.Value(actual));
        });
    QIQI_TEST_CHECK(
        expected.candidate_font_family ==
        actual.candidate_font_family);
    QIQI_TEST_CHECK(
        expected.primary_dict_path ==
        actual.primary_dict_path);
    QIQI_TEST_CHECK(
        expected.user_dict_path ==
        actual.user_dict_path);
}

} // namespace

int main() {
    const std::wstring subkey =
        L"Software\\QiQiIME_BrokerStore_" +
        std::to_wstring(
            ::GetCurrentProcessId());
    const auto cleanup =
        gsl::finally(
            [&]() noexcept {
                const LSTATUS status =
                    ::RegDeleteTreeW(
                        HKEY_CURRENT_USER,
                        subkey.c_str());
                QIQI_TEST_CHECK(
                    status == ERROR_SUCCESS ||
                    status ==
                        ERROR_FILE_NOT_FOUND);
            });

    qiqi::broker::ConfigStore store(
        subkey);
    qiqi::config::Snapshot first;
    QIQI_TEST_CHECK(
        store.Read(first) == S_OK);
    QIQI_TEST_CHECK(
        first.revision != 0);
    QIQI_TEST_CHECK(
        qiqi::ValidateImeSkinCatalog(
            first.skin_catalog) == S_OK);

    QiQiConfig changed =
        first.value;
    changed.page_size = 9;
    changed.ime_skin_id =
        ImeSkinId::PeachBlossom;
    changed.candidate_font_family =
        L"Microsoft YaHei UI";
    const trace::Context save_trace =
        trace::CreateContext();
    const GUID save_activity =
        trace::CreateContext()
            .caller_activity_id;
    QIQI_TEST_CHECK(
        trace::IsValid(save_trace));
    QIQI_TEST_CHECK(
        save_activity != GUID_NULL);
    qiqi::config::Snapshot saved;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            changed,
            save_trace,
            save_activity,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        first.revision);
    CheckEqual(saved.value, changed);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        save_trace.trace_id);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        save_activity);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);

    const auto unchanged = saved;
    const trace::Context conflict_trace =
        trace::CreateContext();
    const GUID conflict_activity =
        trace::CreateContext()
            .caller_activity_id;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            QiQiConfig{},
            conflict_trace,
            conflict_activity,
            saved) ==
        HRESULT_FROM_WIN32(
            ERROR_REVISION_MISMATCH));
    QIQI_TEST_CHECK(
        saved.revision ==
        unchanged.revision);
    CheckEqual(saved.value, unchanged.value);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        save_trace.trace_id);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        save_activity);

    std::atomic<int> successes{0};
    std::atomic<int> conflicts{0};
    const uint64_t concurrent_revision =
        saved.revision;
    QiQiConfig left = saved.value;
    QiQiConfig right = saved.value;
    left.page_size = 6;
    right.page_size = 7;
    const auto save_once =
        [&](QiQiConfig value,
            const trace::Context trace,
            const GUID activity) {
            qiqi::config::Snapshot output;
            const HRESULT result =
                store.Save(
                    concurrent_revision,
                    value,
                    trace,
                    activity,
                    output);
            if (result == S_OK) {
                ++successes;
            } else if (
                result ==
                HRESULT_FROM_WIN32(
                    ERROR_REVISION_MISMATCH)) {
                ++conflicts;
            } else {
                QIQI_TEST_CHECK(
                    result == S_OK);
            }
        };
    {
        std::jthread left_thread(
            save_once,
                left,
                trace::CreateContext(),
                trace::CreateContext()
                    .caller_activity_id);
        std::jthread right_thread(
            save_once,
                right,
                trace::CreateContext(),
                trace::CreateContext()
                    .caller_activity_id);
    }
    QIQI_TEST_CHECK(successes == 1);
    QIQI_TEST_CHECK(conflicts == 1);

    QIQI_TEST_CHECK(
        store.Read(saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.commit_trace_id !=
        GUID_NULL);
    QIQI_TEST_CHECK(
        saved.commit_activity_id !=
        GUID_NULL);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);
    auto commit_trace =
        saved.commit_trace_id;
    auto commit_activity =
        saved.commit_activity_id;
    const auto directory =
        std::filesystem::temp_directory_path() /
        L"QiQiIME_BrokerStore";
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);
    QIQI_TEST_CHECK(
        saved.commit_trace_id !=
        commit_trace);
    commit_trace =
        saved.commit_trace_id;
    commit_activity =
        saved.commit_activity_id;
    const uint64_t before_noop_publish =
        saved.revision;
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision ==
        before_noop_publish);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        commit_trace);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        commit_activity);
    std::filesystem::path published;
    QIQI_TEST_CHECK(
        qiqi::LoadUserDictSyncDirectory(
            published,
            subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(
        published == directory);

    const uint64_t before_reset =
        saved.revision;
    QIQI_TEST_CHECK(
        store.Reset(
            before_reset,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        before_reset);
    CheckEqual(saved.value, QiQiConfig{});

    qiqi::broker::ConfigStore restarted(
        subkey);
    qiqi::config::Snapshot after_restart;
    QIQI_TEST_CHECK(
        restarted.Read(after_restart) ==
        S_OK);
    QIQI_TEST_CHECK(
        after_restart.commit_trace_id ==
        saved.commit_trace_id);
    QIQI_TEST_CHECK(
        after_restart.commit_activity_id ==
        saved.commit_activity_id);
    QIQI_TEST_CHECK(
        after_restart.commit_revision ==
        saved.commit_revision);

    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            L"relative",
            saved) == E_INVALIDARG);
    std::cout
        << "[PASS] Config broker store "
           "tests succeeded.\n";
    return 0;
}
