// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "test.h"
#include "common/backup.h"
#include "common/cfgjson.h"
#include "common/cleanup.h"
#include "common/zip.h"
#include "ime/engine/backup.h"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>

#undef QIQI_TEST_CHECK
#define QIQI_TEST_CHECK(expr) do { \
    if (!(expr)) { \
        ::qiqi::test::ReportTestCleanupFailure(__FILE__, __LINE__, #expr); \
        throw std::runtime_error(std::format("{}:{}: {}", __FILE__, __LINE__, #expr)); \
    } \
} while (false)

namespace qiqi::dict_manager::tests::backup {

std::string BackupManifest(bool has_custom = false);
std::string ConfigJson(std::string_view settings);
void CheckArchiveFixtureWrite(const std::filesystem::path& path, HRESULT hr);
std::filesystem::path WriteArchiveFixture(
    std::filesystem::path path, std::string_view bytes);
void WriteRestoreBundle(
    const std::filesystem::path& path,
    std::string_view config,
    std::string_view user_dictionary = {},
    std::string_view phrase_order = {},
    std::optional<std::string_view> custom_primary = std::nullopt,
    std::string_view manifest = {});
std::string DeflatedZipFixture(bool corrupt_crc = false);

class BackupFixture final {
public:
    BackupFixture();
    ~BackupFixture() noexcept;
    BackupFixture(const BackupFixture&) = delete;
    BackupFixture& operator=(const BackupFixture&) = delete;
    BackupFixture(BackupFixture&&) = delete;
    BackupFixture& operator=(BackupFixture&&) = delete;

    void Reset();
    bool HasWord(DictEntrySource source, std::wstring_view word) const;
    static ScopedFileHandle LockFile(
        const std::filesystem::path& path) noexcept;
    static std::string ReadFile(const std::filesystem::path& path);
    static constexpr bool SharingFailure(
        HRESULT error) noexcept {
        return error == E_ACCESSDENIED ||
            error ==
                HRESULT_FROM_WIN32(
                    ERROR_SHARING_VIOLATION);
    }

    const std::filesystem::path directory;
    const std::wstring subkey;
    const std::filesystem::path user;
    const std::filesystem::path frequency;
    const std::filesystem::path backup;
    DictManagerModel model;
    const std::string full_config;
    const std::string full_user;
    const std::string full_phrase;

private:
    size_t reset_count_ = 0;
};

void TestZipCompression();
void TestBackupConfig(BackupFixture& fixture);
void TestInvalidConfig(BackupFixture& fixture);
void TestCustomDictionary(BackupFixture& fixture);
void TestRestoreWithBom(BackupFixture& fixture);
void TestRestoreValidation(BackupFixture& fixture);
void TestRestoreFailures(BackupFixture& fixture);
void TestMergeReplace(BackupFixture& fixture);
void TestRestoreMessages();

} // namespace qiqi::dict_manager::tests::backup
