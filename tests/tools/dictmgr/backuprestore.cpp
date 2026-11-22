// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backupfixture.h"

namespace qiqi::dict_manager::tests::backup {

void TestBackupConfig(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& directory = fixture.directory;
    fixture.Reset();
    for (const bool maximum : {false, true}) {
        const auto config_backup = directory / (maximum ? L"max-config.zip" : L"min-config.zip");
        QiQiConfig expected = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Assign(expected, maximum ? field.maximum : field.minimum));
        });
        expected.primary_dict = PrimaryDict::Jidian;
        expected.user_dict_path = L"not-in-backup.txt";
        QIQI_TEST_CHECK(SaveConfig(expected));
        CheckArchiveFixtureWrite(config_backup, model.CreateBackupBundleResult(config_backup));
        std::vector<ZipArchiveEntry> entries;
        QIQI_TEST_CHECK(SUCCEEDED(ReadZipArchiveFileResult(config_backup, entries)));
        QIQI_TEST_CHECK(entries.size() == 4);
        const auto config_entry = std::ranges::find_if(
            entries, [](const ZipArchiveEntry& entry) {
                return entry.name == kBackupConfigEntry;
            });
        QIQI_TEST_CHECK(config_entry != entries.end());
        const std::string config_bytes(
            config_entry->data.begin(), config_entry->data.end());
        QIQI_TEST_CHECK(!config_bytes.contains("user_dict_path") &&
            !config_bytes.contains("not-in-backup.txt") &&
            !config_bytes.contains("log_level"));
        ConfigJsonError config_error;
        QiQiConfig archived{};
        QIQI_TEST_CHECK(SUCCEEDED(ParseConfigJson(
            config_bytes, QiQiConfig{}, archived, config_error)));
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(archived) == field.Value(expected));
        });
        QIQI_TEST_CHECK(SaveConfig(QiQiConfig{}));
        QIQI_TEST_CHECK(model.RestoreBackupBundle(config_backup, BackupConflictMode::Merge).status ==
            BackupRestoreStatus::Success);
        const auto restored = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(restored) == field.Value(expected));
        });
        QIQI_TEST_CHECK(restored.user_dict_path.empty());
    }
}

void TestCustomDictionary(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& directory = fixture.directory;
    const auto& backup = fixture.backup;
    fixture.Reset();
    QIQI_TEST_CHECK(model.RemoveUserEntry(L"zzzz", L"原有词条"));
    QIQI_TEST_CHECK(model.ClearAllPhraseFrequencies());
    const auto custom_source = directory / L"custom-source.txt";
    QIQI_TEST_CHECK(WriteFileAtomically(
        custom_source,
        codec::AsByteSpan(std::string_view("abcd 自定义主词库\n"))));
    QIQI_TEST_CHECK(model.SelectCustomPrimaryDictionary(custom_source));
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    std::vector<ZipArchiveEntry> custom_archive;
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(backup, custom_archive)));
    QIQI_TEST_CHECK(custom_archive.size() == 5);
    const auto archived_custom = std::ranges::find_if(
        custom_archive, [](const ZipArchiveEntry& entry) {
                return entry.name == kBackupCustomPrimaryTextEntry;
        });
    QIQI_TEST_CHECK(archived_custom != custom_archive.end());
    for (const auto& entry : custom_archive) {
        const std::string_view bytes(
                reinterpret_cast<const char*>(entry.data.data()),
                entry.data.size());
        QIQI_TEST_CHECK(!bytes.contains(WideToUtf8(custom_source.wstring())));
    }

    QiQiConfig builtin_config = LoadConfig();
    builtin_config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    std::error_code remove_custom_error;
    const bool custom_removed = std::filesystem::remove(
        custom_source, remove_custom_error);
    if (!custom_removed || remove_custom_error) {
        std::cerr << std::format(
            "[CUSTOM-DICT-REMOVE-FAIL] removed={}, error={}\n",
            custom_removed, remove_custom_error.value());
    }
    QIQI_TEST_CHECK(custom_removed && !remove_custom_error);
    const auto custom_result =
        model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
    QIQI_TEST_CHECK(custom_result.status == BackupRestoreStatus::Success &&
        custom_result.stage == BackupRestoreStage::Complete);
    QIQI_TEST_CHECK(custom_result.summary.custom_primary_dict_restored);
    QIQI_TEST_CHECK(model.ActivePrimaryDict() == PrimaryDict::Custom);
    const auto restored_custom_path =
        directory / L"qiqiime-restored-primary.txt";
    QIQI_TEST_CHECK(model.ActivePrimaryDictPath() ==
                        restored_custom_path);
    QIQI_TEST_CHECK(std::filesystem::exists(restored_custom_path));
    QIQI_TEST_CHECK(std::ranges::any_of(
        model.Records(), [](const DictRecord& record) {
                return record.source == DictEntrySource::PrimaryDict &&
                    record.word == L"自定义主词库";
        }));
    QIQI_TEST_CHECK(LoadConfig().primary_dict_path ==
                        restored_custom_path.wstring());

    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    {
        QIQI_TEST_CHECK(WriteFileAtomically(
                restored_custom_path,
                codec::AsByteSpan(std::string_view("abcd 旧自定义词库\n"))));
        const auto locked = fixture.LockFile(restored_custom_path);
        QIQI_TEST_CHECK(locked);
        const auto denied =
                model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(
                denied.status == BackupRestoreStatus::Failed &&
                denied.stage == BackupRestoreStage::CustomDictionary &&
                fixture.SharingFailure(denied.error));
        QIQI_TEST_CHECK(!denied.summary.custom_primary_dict_restored &&
                !denied.effects_possible);
        QIQI_TEST_CHECK(
                LoadConfig().primary_dict == PrimaryDict::BuiltinOnly);
    }

    QIQI_TEST_CHECK(WriteFileAtomically(
        custom_source,
        codec::AsByteSpan(std::string_view("abcd 二进制主词库\n"))));
    const auto custom_binary = directory / L"custom-source.qdict";
    QIQI_TEST_CHECK(WubiMappedDict::CompileTextFileToQdict(
        custom_source, custom_binary));
    QIQI_TEST_CHECK(model.SelectCustomPrimaryDictionary(custom_binary));
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    custom_archive.clear();
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(backup, custom_archive)));
    QIQI_TEST_CHECK(std::ranges::any_of(
        custom_archive, [](const ZipArchiveEntry& entry) {
                return entry.name ==
                    kBackupCustomPrimaryBinaryEntry;
        }));
    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    QIQI_TEST_CHECK(std::filesystem::remove(custom_binary));
    const auto binary_result =
        model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
    QIQI_TEST_CHECK(binary_result.status == BackupRestoreStatus::Success &&
        binary_result.summary.custom_primary_dict_restored);
    QIQI_TEST_CHECK(model.ActivePrimaryDict() == PrimaryDict::Custom);
    const auto restored_custom_binary =
        directory / L"qiqiime-restored-primary.qdict";
    QIQI_TEST_CHECK(model.ActivePrimaryDictPath() ==
                        restored_custom_binary);
    QIQI_TEST_CHECK(std::ranges::any_of(
        model.Records(), [](const DictRecord& record) {
                return record.source == DictEntrySource::PrimaryDict &&
                    record.word == L"二进制主词库";
        }));

    fixture.Reset();
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    {
        const auto locked_backup = fixture.LockFile(backup);
        QIQI_TEST_CHECK(locked_backup);
        const HRESULT create_result =
                model.CreateBackupBundleResult(backup);
        QIQI_TEST_CHECK(fixture.SharingFailure(create_result));
    }
}

void TestRestoreWithBom(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& backup = fixture.backup;
    const auto& full_config = fixture.full_config;
    const auto& full_user = fixture.full_user;
    const auto& full_phrase = fixture.full_phrase;

    // Malformed later input must not apply earlier configuration or dictionary entries.
    fixture.Reset();
    WriteRestoreBundle(
        backup, full_config,
        std::string("\xEF\xBB\xBF") + full_user,
        full_phrase);
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            backup, BackupConflictMode::Replace).status ==
        BackupRestoreStatus::Success);
}

void TestMergeReplace(BackupFixture& fixture) {
    auto& model = fixture.model;
    const auto& backup = fixture.backup;
    const auto& full_config = fixture.full_config;
    const auto& full_user = fixture.full_user;
    const auto& full_phrase = fixture.full_phrase;

    for (const auto mode : {BackupConflictMode::Merge, BackupConflictMode::Replace}) {
        fixture.Reset();
        WriteRestoreBundle(
            backup, full_config,
            full_user + "abcd ^置顶词条\nabcd !屏蔽词条\n",
            full_phrase);
        const auto result = model.RestoreBackupBundle(backup, mode);
        if (result.status != BackupRestoreStatus::Success || result.stage != BackupRestoreStage::Complete) {
            std::wcerr << L"Backup restore fixture mode="
                       << (mode == BackupConflictMode::Merge ? L"merge" : L"replace")
                       << L": " << FormatBackupRestoreMessage(result, UiLanguage::EnUs) << std::endl;
        }
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.stage == BackupRestoreStage::Complete);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.error == S_OK && result.operations_failed == 0);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 3 && result.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::UserDict, L"原有词条") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::PhraseFreq, L"原有词组") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::Pinned, L"置顶词条"));
        QIQI_TEST_CHECK(fixture.HasWord(DictEntrySource::Blocked, L"屏蔽词条"));
    }
}

} // namespace qiqi::dict_manager::tests::backup
