// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backupfixture.h"

namespace qiqi::dict_manager::tests::backup {

void TestInvalidConfig(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& directory = fixture.directory;
    const QiQiConfig before_invalid = LoadConfig();
    const uint64_t sequence_before_invalid = QueryRegistryConfigSequence();
    size_t invalid_config_index = 0;
    for (const auto* invalid_config : {
             R"({"format":"qiqiime-config","version":1,"settings":{"page_size":0}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"future_numeric_key":123}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"log_level":6}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"page_size":7,"page_size":8}})"}) {
        const auto config_backup = directory / std::format(L"invalid-config-{}.zip", ++invalid_config_index);
        WriteRestoreBundle(config_backup, invalid_config);
        const auto result =
            model.RestoreBackupBundle(config_backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed &&
            result.stage == BackupRestoreStage::Validate &&
            QueryRegistryConfigSequence() == sequence_before_invalid);
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(LoadConfig()) ==
                field.Value(before_invalid));
        });
    }
}

void TestRestoreValidation(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& directory = fixture.directory;
    const auto& subkey = fixture.subkey;
    const auto& user = fixture.user;
    const auto& frequency = fixture.frequency;
    const auto& backup = fixture.backup;
    const auto& full_config = fixture.full_config;
    const auto& full_user = fixture.full_user;
    const auto& full_phrase = fixture.full_phrase;
    fixture.Reset();
    const auto old_user = fixture.ReadFile(user);
    const auto old_frequency = fixture.ReadFile(frequency);
    const auto old_sequence = QueryRegistryConfigSequence();
    const auto missing = model.RestoreBackupBundle(directory / L"missing.zip", BackupConflictMode::Replace);
    QIQI_TEST_CHECK(missing.status == BackupRestoreStatus::Failed && missing.stage == BackupRestoreStage::Read);
    const std::array malformed{
        std::tuple{full_config, full_user + "not_a_code 损坏词组\n", full_phrase, std::string{}},
        std::tuple{ConfigJson(R"("page_size":0)"), full_user, full_phrase, std::string{}},
        std::tuple{full_config, full_user, full_phrase,
                   std::string(R"({"format":"qiqiime-backup","version":1,"files":{"config":"config.json","user_dictionary":"user-dictionary.txt","phrase_order":"phrase-order.txt","unexpected":"x"}})")},
        std::tuple{full_config, std::string("abcd ^\n"), full_phrase, std::string{}},
        std::tuple{full_config, full_user, std::string("abcd 单\n"), std::string{}},
        std::tuple{full_config, std::string("abcd ") + "\xFF\n", full_phrase, std::string{}},
        std::tuple{full_config, std::string("abcd \0\n", 7), full_phrase, std::string{}},
        std::tuple{full_config, std::string("abcd \x01\n", 7), full_phrase, std::string{}}};
    // Independent validation cases do not require replacing a just-read archive.
    size_t malformed_index = 0;
    for (const auto& [config, user_data, phrase_data, manifest] : malformed) {
        const auto malformed_backup = directory / std::format(L"malformed-{}.zip", ++malformed_index);
        WriteRestoreBundle(
            malformed_backup, config, user_data, phrase_data,
            std::nullopt, manifest);
        const auto result = model.RestoreBackupBundle(malformed_backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && FAILED(result.error));
        QIQI_TEST_CHECK(result.refresh_error == S_FALSE && !result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(fixture.ReadFile(user) == old_user && fixture.ReadFile(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3 && QueryRegistryConfigSequence() == old_sequence);
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::UserDict, L"原有词条"));
    }

    const auto legacy_backup = WriteArchiveFixture(directory / L"legacy.zip",
        "# QIQIIME_BACKUP_V1\n# checksum_fnv1a64=0000000000000000\n");
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            legacy_backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    const auto valid_backup = directory / L"valid.zip";
    WriteRestoreBundle(valid_backup, full_config, full_user, full_phrase);
    const auto good_archive = fixture.ReadFile(valid_backup);
    const auto corrupt_backup = WriteArchiveFixture(directory / L"bad-crc.zip", DeflatedZipFixture(true));
    const auto bad_crc =
        model.RestoreBackupBundle(corrupt_backup, BackupConflictMode::Replace);
    QIQI_TEST_CHECK(bad_crc.status == BackupRestoreStatus::Failed &&
        bad_crc.stage == BackupRestoreStage::Validate &&
        bad_crc.error == HRESULT_FROM_WIN32(ERROR_CRC));
    QIQI_TEST_CHECK(fixture.ReadFile(user) == old_user &&
                    fixture.ReadFile(frequency) == old_frequency);

    std::vector<ZipArchiveEntry> unexpected_entries;
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchive(codec::AsByteSpan(good_archive), unexpected_entries)));
    unexpected_entries.push_back({
        "unexpected.txt",
        std::vector<uint8_t>{'x'}});
    const auto unexpected_backup = directory / L"unexpected-entry.zip";
    CheckArchiveFixtureWrite(unexpected_backup, WriteZipArchiveFileResult(unexpected_backup, unexpected_entries));
    const auto unexpected =
        model.RestoreBackupBundle(unexpected_backup, BackupConflictMode::Replace);
    QIQI_TEST_CHECK(unexpected.status == BackupRestoreStatus::Failed &&
        unexpected.stage == BackupRestoreStage::Validate);
    QIQI_TEST_CHECK(fixture.ReadFile(user) == old_user && fixture.ReadFile(frequency) == old_frequency);
    QIQI_TEST_CHECK(LoadConfig().page_size == 3 && QueryRegistryConfigSequence() == old_sequence);

    std::string unsafe_archive = good_archive;
    size_t unsafe_name = unsafe_archive.find("config.json");
    QIQI_TEST_CHECK(unsafe_name != std::string::npos);
    while (unsafe_name != std::string::npos) {
        unsafe_archive.replace(unsafe_name, 11, "unsafe/xx!!");
        unsafe_name = unsafe_archive.find("config.json", unsafe_name + 11);
    }
    const auto unsafe_backup = WriteArchiveFixture(directory / L"unsafe-name.zip", unsafe_archive);
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            unsafe_backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    std::string gapped_archive = good_archive;
    const auto eocd = gapped_archive.rfind(
        std::string("\x50\x4b\x05\x06", 4));
    QIQI_TEST_CHECK(eocd != std::string::npos);
    auto read_u32 = [&gapped_archive](size_t offset) {
        uint32_t value = 0;
        for (size_t index = 0; index < 4; ++index) {
            value |= gsl::narrow_cast<uint32_t>(
                gsl::narrow_cast<uint8_t>(
                    gsl::at(gapped_archive, offset + index)))
                << (index * 8);
        }
        return value;
    };
    const uint32_t directory_offset = read_u32(eocd + 16);
    gapped_archive.insert(
        gsl::narrow_cast<size_t>(directory_offset), 1, '\0');
    const size_t shifted_eocd = eocd + 1;
    for (size_t index = 0; index < 4; ++index) {
        gsl::at(gapped_archive, shifted_eocd + 16 + index) =
            gsl::narrow_cast<char>(
                ((directory_offset + 1) >> (index * 8)) & 0xffu);
    }
    std::vector<ZipArchiveEntry> padded_entries;
    QIQI_TEST_CHECK(ReadZipArchive(
        codec::AsByteSpan(gapped_archive), padded_entries) == S_OK);
    QIQI_TEST_CHECK(padded_entries.size() == 4);

    constexpr char unsupported_method{99};
    std::string unsupported_archive = good_archive;
    const auto local_header = unsupported_archive.find(
        std::string("\x50\x4b\x03\x04", 4));
    QIQI_TEST_CHECK(local_header != std::string::npos);
    gsl::at(unsupported_archive, local_header + 8) = unsupported_method;
    const auto central_header = unsupported_archive.find(
        std::string("\x50\x4b\x01\x02", 4));
    QIQI_TEST_CHECK(central_header != std::string::npos);
    gsl::at(unsupported_archive, central_header + 10) = unsupported_method;
    const auto unsupported_backup = WriteArchiveFixture(directory / L"unsupported-method.zip", unsupported_archive);
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            unsupported_backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    std::vector<uint8_t> rejected_archive;
    QIQI_TEST_CHECK(FAILED(BuildZipArchive(
        std::array{ZipArchiveEntry{
            "unsafe/name.txt", std::vector<uint8_t>{'x'}}},
        rejected_archive)));
    QIQI_TEST_CHECK(FAILED(BuildZipArchive(
        std::array{
            ZipArchiveEntry{"duplicate.txt", std::vector<uint8_t>{'x'}},
            ZipArchiveEntry{"duplicate.txt", std::vector<uint8_t>{'y'}}},
        rejected_archive)));

    // Actual registry write denial, without changing the process user's real configuration.
    WriteRestoreBundle(backup, full_config, {}, {});
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
            READ_CONTROL | WRITE_DAC, key.Put()) == ERROR_SUCCESS);
        DWORD size = 0;
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, nullptr, &size) ==
            ERROR_INSUFFICIENT_BUFFER);
        std::vector<uint8_t> original(size);
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION,
            original.data(), &size) == ERROR_SUCCESS);
        const auto restore_acl = gsl::finally([&]() noexcept {
            ::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, original.data());
        });
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;KW;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
        const ScopedLocalAlloc acl(descriptor);
        QIQI_TEST_CHECK(::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, descriptor) == ERROR_SUCCESS);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) &&
            !result.summary.config_restored);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
        const auto partial_backup = directory / L"partial-config.zip";
        WriteRestoreBundle(partial_backup, full_config, full_user, full_phrase);
        const auto partial = model.RestoreBackupBundle(partial_backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(partial.status == BackupRestoreStatus::Partial && partial.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(partial.operations_succeeded == 2 && partial.operations_failed == 1);
        QIQI_TEST_CHECK(partial.summary.user_entries_applied == 1 && partial.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(!partial.summary.config_restored && LoadConfig().page_size == 3);
    }
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == old_sequence);
}

void TestRestoreFailures(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& directory = fixture.directory;
    const auto& user = fixture.user;
    const auto& frequency = fixture.frequency;
    const auto& backup = fixture.backup;
    const auto& full_config = fixture.full_config;
    const auto& full_user = fixture.full_user;
    const auto& full_phrase = fixture.full_phrase;

    // Replace must stop when the first deletion fails, preserving the original files and config.
    fixture.Reset();
    const auto old_user = fixture.ReadFile(user);
    const auto old_frequency = fixture.ReadFile(frequency);
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = fixture.LockFile(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(fixture.ReadFile(user) == old_user && fixture.ReadFile(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    // Later preparation failures report already committed destructive work, never full success.
    {
        auto cache = user;
        cache.replace_extension(L".qdict");
        QIQI_TEST_CHECK(WriteFileAtomically(cache, codec::AsByteSpan(std::string_view("cache"))));
        const auto locked = fixture.LockFile(cache);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(fixture.ReadFile(frequency) == old_frequency && LoadConfig().page_size == 3);
    }
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto channel = GetSyncChannelDirectoryForFile(user);
        const auto journal = channel / L"journal.bin";
        const auto locked = fixture.LockFile(journal);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded > 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(std::filesystem::exists(channel / L"writer.lock"));
        QIQI_TEST_CHECK(!fixture.HasWord(DictEntrySource::UserDict, L"原有词条"));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
    }
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto isolated_user = directory / L"isolated-user.txt";
        const auto channel = GetSyncChannelDirectoryForFile(isolated_user);
        QIQI_TEST_CHECK(WriteFileAtomically(isolated_user, codec::AsByteSpan(old_user)));
        QIQI_TEST_CHECK(WriteFileAtomically(channel, codec::AsByteSpan(std::string_view("not a directory"))));
        CompleteBackup isolated({}, isolated_user, frequency);
        const auto result = isolated.RestoreFile(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && result.operations_succeeded == 0);
        QIQI_TEST_CHECK(fixture.ReadFile(isolated_user) == old_user && fixture.ReadFile(frequency) == old_frequency);
        QIQI_TEST_CHECK(fixture.ReadFile(channel) == "not a directory" && LoadConfig().page_size == 3);
    }
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = fixture.LockFile(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(fixture.SharingFailure(result.error) && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && !result.summary.config_restored);
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Best-effort application keeps honest per-item counts and skips config on partial failure.
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = fixture.LockFile(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary && fixture.SharingFailure(result.error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 1 && result.summary.phrase_freq_failed == 0);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(!fixture.HasWord(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::PhraseFreq, L"新增词组"));
        BackupRestoreSummary summary;
        QIQI_TEST_CHECK(!model.RestoreBackupBundle(backup, BackupConflictMode::Merge, summary));
        QIQI_TEST_CHECK(summary.user_entries_failed == 1 && summary.phrase_freq_applied == 1);
    }
    fixture.Reset();
    WriteRestoreBundle(backup, ConfigJson(""), "abcd 新增词条\n");
    {
        const auto locked = fixture.LockFile(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.user_entries_failed == 1);
    }
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = fixture.LockFile(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(fixture.SharingFailure(result.error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 1 && result.summary.user_entries_failed == 0);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(!fixture.HasWord(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    fixture.Reset();
    WriteRestoreBundle(backup, ConfigJson(""), {}, "abcd 新增词组\n");
    {
        const auto locked = fixture.LockFile(GetSyncChannelDirectoryForFile(frequency) / L"writer.lock");
        QIQI_TEST_CHECK(locked);
        const auto count = model.PhraseFreqCount();
        QIQI_TEST_CHECK(model.PromotePhraseFrequencyResult(L" ABCD ", L" 新增词组 ") ==
            HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(model.PhraseFreqCount() == count);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"0x80070020"));
    }

    for (const std::string_view user_data :
         {"abcd 新增词条\n", "abcd ^置顶词条\n", "abcd !屏蔽词条\n"}) {
        fixture.Reset();
        WriteRestoreBundle(backup, ConfigJson(""), user_data);
        const auto locked = fixture.LockFile(GetSyncChannelDirectoryForFile(user) / L"writer.lock");
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"0x80070020"));
    }

    // Missing optional sources remain a valid, empty view and need not be created by Merge.
    {
        DictManagerModel optional;
        const auto missing_user = directory / L"optional_user.txt";
        const auto missing_frequency = directory / L"optional_frequency.txt";
        optional.InitializePaths({}, missing_user, missing_frequency);
        QIQI_TEST_CHECK(optional.ReloadAllResult() == S_OK);
        const auto optional_backup = directory / L"optional-sources.zip";
        WriteRestoreBundle(optional_backup, ConfigJson(""));
        const auto result = optional.RestoreBackupBundle(optional_backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 0);
        QIQI_TEST_CHECK(optional.UserEntryCount() == 0 && optional.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(!std::filesystem::exists(missing_user) && !std::filesystem::exists(missing_frequency));
    }

    // A final refresh failure cannot certify a successful restore or a complete displayed view.
    fixture.Reset();
    WriteRestoreBundle(backup, full_config);
    {
        const ScopedFileHandle locked(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        if (result.stage != BackupRestoreStage::Refresh) {
            std::cerr << std::format(
                "[FINAL-REFRESH-DIAG] status={}, stage={}, error=0x{:08X}, "
                "refresh=0x{:08X}, succeeded={}, failed={}\n",
                std::to_underlying(result.status),
                std::to_underlying(result.stage),
                gsl::narrow_cast<uint32_t>(result.error),
                gsl::narrow_cast<uint32_t>(result.refresh_error),
                result.operations_succeeded,
                result.operations_failed);
        }
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(
            fixture.SharingFailure(result.error) &&
            result.refresh_error == result.error);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Replace must stop after an unsuccessful intermediate reload, before applying staged items.
    fixture.Reset();
    {
        const auto module = directory / L"module";
        QIQI_TEST_CHECK(std::filesystem::create_directories(module));
        auto config = LoadConfig();
        config.primary_dict = PrimaryDict::Jidian;
        config.enable_jidian_extra = false;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths(module, user, frequency); // The optional system file is not present yet.
        const auto dictionary = module / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(dictionary, codec::AsByteSpan(std::string_view("abcd 系统词组\n"))));
        const ScopedFileHandle locked(::CreateFileW(dictionary.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        WriteRestoreBundle(backup, full_config, full_user, full_phrase);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(
            result.error == E_FAIL &&
            result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.operations_failed == 1 && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(!fixture.HasWord(DictEntrySource::UserDict, L"新增词条"));
    }

    // Preserve the apply failure independently of refresh failure; discard speculative promotions.
    fixture.Reset();
    WriteRestoreBundle(backup, ConfigJson(""), {}, "abcd 新增词组\n");
    {
        const ScopedFileHandle unreadable_user(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        const auto unwritable_frequency = fixture.LockFile(frequency);
        QIQI_TEST_CHECK(unreadable_user && unwritable_frequency);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency && fixture.SharingFailure(result.error));
        QIQI_TEST_CHECK(
            fixture.SharingFailure(result.refresh_error) &&
            result.operations_failed == 2);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!fixture.HasWord(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::PhraseFreq, L"原有词组"));
        const auto message = FormatBackupRestoreMessage(result, UiLanguage::EnUs);
        QIQI_TEST_CHECK(message.contains(L"phrase ordering") && message.contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Clear can fail after its source write when the required synchronization snapshot is locked.
    fixture.Reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto snapshot = GetSyncChannelDirectoryForFile(frequency) / L"snapshot.qfreq";
        const auto old_snapshot = fixture.ReadFile(snapshot);
        const auto locked = fixture.LockFile(snapshot);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Prepare && fixture.SharingFailure(result.error));
        QIQI_TEST_CHECK(result.refresh_error == S_OK && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(fixture.ReadFile(frequency) != old_frequency && model.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(fixture.ReadFile(snapshot) == old_snapshot);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }
}

void TestRestoreMessages() {
    // The UI's pure formatter cannot label a partial/failed result as successful.
    BackupRestoreResult message_result;
    message_result.operations_failed = 1;
    for (const auto stage : {BackupRestoreStage::Read, BackupRestoreStage::Validate,
             BackupRestoreStage::Prepare, BackupRestoreStage::UserDictionary,
             BackupRestoreStage::PhraseFrequency, BackupRestoreStage::CustomDictionary,
             BackupRestoreStage::Config, BackupRestoreStage::Refresh}) {
        message_result.stage = stage;
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            message_result.status = BackupRestoreStatus::Failed;
            const auto failed = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(failed.contains(language == UiLanguage::EnUs ? L"restore failed" : L"恢复备份失败"));
            message_result.status = BackupRestoreStatus::Partial;
            const auto partial = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(partial.contains(language == UiLanguage::EnUs ? L"restore incomplete" : L"恢复未完成"));
            QIQI_TEST_CHECK(!partial.contains(language == UiLanguage::EnUs ? L"Backup restored" : L"已恢复备份"));
            message_result.refresh_error = E_FAIL;
            const auto incomplete_view = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(incomplete_view.contains(language == UiLanguage::EnUs ?
                L"view may be incomplete" : L"当前显示可能不完整"));
            message_result.refresh_error = S_FALSE;
        }
    }
    message_result.status = BackupRestoreStatus::Success;
    message_result.refresh_error = E_FAIL;
    QIQI_TEST_CHECK(!FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    message_result.refresh_error = S_OK;
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::ZhCn).contains(L"已恢复备份"));
}

} // namespace qiqi::dict_manager::tests::backup
