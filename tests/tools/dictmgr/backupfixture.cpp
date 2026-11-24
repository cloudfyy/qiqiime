// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backupfixture.h"

namespace qiqi::dict_manager::tests::backup {

void CheckArchiveFixtureWrite(const std::filesystem::path& path, HRESULT hr) {
    if (FAILED(hr)) {
        std::cerr << std::format("[FIXTURE-FAIL] stage=write-archive, file=\"{}\", hr=0x{:08X}\n",
            WideToUtf8(path.filename().native()), gsl::narrow_cast<uint32_t>(hr)) << std::flush;
    }
    QIQI_TEST_CHECK(SUCCEEDED(hr));
}

std::filesystem::path WriteArchiveFixture(
    std::filesystem::path path, std::string_view bytes) {
    CheckArchiveFixtureWrite(path, WriteFileAtomicallyResult(path, codec::AsByteSpan(bytes)));
    return path;
}

std::string BackupManifest(bool has_custom) {
    std::string manifest =
        "{\"format\":\"qiqiime-backup\",\"version\":1,\"files\":{"
        "\"config\":\"config.json\","
        "\"user_dictionary\":\"user-dictionary.txt\","
        "\"phrase_order\":\"phrase-order.txt\"";
    if (has_custom) {
        manifest +=
            ",\"custom_primary_dictionary\":\"custom-primary.txt\"";
    }
    manifest += "}}\n";
    return manifest;
}

std::string ConfigJson(std::string_view settings) {
    return "{\"format\":\"qiqiime-config\",\"version\":1,\"settings\":{" +
        std::string(settings) + "}}\n";
}

void WriteRestoreBundle(
    const std::filesystem::path& path,
    std::string_view config,
    std::string_view user_dictionary,
    std::string_view phrase_order,
    std::optional<std::string_view> custom_primary,
    std::string_view manifest) {
    const auto bytes = [](std::string_view value) {
        return std::vector<uint8_t>(value.begin(), value.end());
    };
    const std::string manifest_text = manifest.empty()
        ? BackupManifest(custom_primary.has_value())
        : std::string(manifest);
    std::vector<ZipArchiveEntry> entries{
        {std::string(kBackupManifestEntry), bytes(manifest_text)},
        {std::string(kBackupConfigEntry), bytes(config)},
        {std::string(kBackupUserDictionaryEntry), bytes(user_dictionary)},
        {std::string(kBackupPhraseOrderEntry), bytes(phrase_order)}};
    if (custom_primary) {
        entries.push_back({
            std::string(kBackupCustomPrimaryTextEntry),
            bytes(*custom_primary)});
    }
    CheckArchiveFixtureWrite(path, WriteZipArchiveFileResult(path, entries));
}

#pragma warning(suppress : 26455) // Fixture construction allocates and reports setup failures.
BackupFixture::BackupFixture()
    : directory(std::filesystem::current_path() /
        (L"qiqi_restore_results_" + std::to_wstring(::GetCurrentProcessId()))),
      subkey(L"Software\\QiQiIME_RestoreResults_" +
          std::to_wstring(::GetCurrentProcessId())),
      user(directory / L"user.txt"),
      frequency(directory / L"frequency.txt"),
      backup(directory / L"restore.zip"),
      full_config(ConfigJson(R"("page_size":7)")),
      full_user("abcd 新增词条\n"),
      full_phrase("abcd 新增词组\n") {
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
}

BackupFixture::~BackupFixture() noexcept {
    SetConfigRegistrySubKeyOverrideForTest(nullptr);
    ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    if (std::uncaught_exceptions() != 0) {
        std::fprintf(stderr, "[FIXTURE-RETAINED] pid=%lu, directory=\"%ls\"\n",
            ::GetCurrentProcessId(), directory.c_str());
        std::fflush(stderr);
        return;
    }
    try {
        const auto ec = qiqi::test::RemoveDirectoryWithSharingRetry(directory, 2000, ::Sleep);
        if (ec) {
            std::fprintf(stderr, "[FIXTURE-CLEANUP-FAIL] error=%d, directory=\"%ls\"\n",
                ec.value(), directory.c_str());
            std::fflush(stderr);
            QIQI_TEST_CHECK_CLEANUP(!ec);
        }
    } catch (...) {
        QIQI_TEST_CHECK_CLEANUP(
            false &&
            "Backup fixture cleanup threw");
    }
}

void BackupFixture::Reset() {
    ++reset_count_;
    const auto ec = qiqi::test::RemoveDirectoryWithSharingRetry(directory, 2000, ::Sleep);
    if (ec) {
        std::cerr << std::format(
            "[FIXTURE-FAIL] stage=reset-directory, reset={}, error={}, directory=\"{}\"\n",
            reset_count_, ec.value(), WideToUtf8(directory.native())) << std::flush;
    }
    QIQI_TEST_CHECK(!ec);
    QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
    QiQiConfig config;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.page_size = 3;
    QIQI_TEST_CHECK(SaveConfig(config));
    model.InitializePaths({}, user, frequency);
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"原有词条"));
    const HRESULT frequency_hr =
        model.PromotePhraseFrequencyResult(L"zzzz", L"原有词组");
    if (FAILED(frequency_hr)) {
        std::cerr << std::format(
            "[FIXTURE-FAIL] stage=initial-phrase-promotion, reset={}, pid={}, "
            "hr=0x{:08X}, directory=\"{}\"\n",
            reset_count_, ::GetCurrentProcessId(),
            gsl::narrow_cast<uint32_t>(frequency_hr),
            WideToUtf8(directory.native())) << std::flush;
    }
    QIQI_TEST_CHECK(frequency_hr == S_OK);
}

bool BackupFixture::HasWord(
    DictEntrySource source, std::wstring_view word) const {
    return std::ranges::any_of(model.Records(), [=](const DictRecord& record) {
        return record.source == source && record.word == word;
    });
}

ScopedFileHandle BackupFixture::LockFile(
    const std::filesystem::path& path) noexcept {
    return ScopedFileHandle(::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
        nullptr));
}

std::string BackupFixture::ReadFile(const std::filesystem::path& path) {
    std::string bytes;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, bytes));
    return bytes;
}

} // namespace qiqi::dict_manager::tests::backup
