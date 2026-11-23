// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/configstore.h"
#include "common/support.h"

#include <gsl/gsl>
#include <atomic>
#include <filesystem>
#include <thread>

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
    qiqi::config::Snapshot saved;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            changed,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        first.revision);
    CheckEqual(saved.value, changed);

    const auto unchanged = saved;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            QiQiConfig{},
            saved) ==
        HRESULT_FROM_WIN32(
            ERROR_REVISION_MISMATCH));
    QIQI_TEST_CHECK(
        saved.revision ==
        unchanged.revision);
    CheckEqual(saved.value, unchanged.value);

    std::atomic<int> successes{0};
    std::atomic<int> conflicts{0};
    const uint64_t concurrent_revision =
        saved.revision;
    QiQiConfig left = saved.value;
    QiQiConfig right = saved.value;
    left.page_size = 6;
    right.page_size = 7;
    const auto save_once =
        [&](QiQiConfig value) {
            qiqi::config::Snapshot output;
            const HRESULT result =
                store.Save(
                    concurrent_revision,
                    value,
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
            left);
        std::jthread right_thread(
            save_once,
            right);
    }
    QIQI_TEST_CHECK(successes == 1);
    QIQI_TEST_CHECK(conflicts == 1);

    QIQI_TEST_CHECK(
        store.Read(saved) == S_OK);
    const auto directory =
        std::filesystem::temp_directory_path() /
        L"QiQiIME_BrokerStore";
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            saved) == S_OK);
    const uint64_t before_noop_publish =
        saved.revision;
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision ==
        before_noop_publish);
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
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        before_reset);
    CheckEqual(saved.value, QiQiConfig{});

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
