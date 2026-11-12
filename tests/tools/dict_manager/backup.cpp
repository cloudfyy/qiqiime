// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void WriteRestoreBundle(const std::filesystem::path& path, std::string_view body) {
    const auto bytes = std::format("# QIQIIME_BACKUP_V1\n# checksum_fnv1a64={:016X}\n{}",
        codec::ComputeFnv1a64(codec::AsByteSpan(body)), body);
    QIQI_TEST_CHECK(WriteFileAtomically(path, codec::AsByteSpan(bytes)));
}

void TestBackupRestoreResults() {
    const auto directory = std::filesystem::current_path() /
        (L"qiqi_restore_results_" + std::to_wstring(::GetCurrentProcessId()));
    const std::wstring subkey = L"Software\\QiQiIME_RestoreResults_" + std::to_wstring(::GetCurrentProcessId());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    });
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto user = directory / L"user.txt";
    const auto frequency = directory / L"frequency.txt";
    const auto backup = directory / L"restore.qbak";
    DictManagerModel model;
    const auto reset = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        QIQI_TEST_CHECK(!ec);
        QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
        QiQiConfig config;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.page_size = 3;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths({}, user, frequency);
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"原有词条"));
        QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"zzzz", L"原有词组"));
    };
    const auto has_word = [&](DictEntrySource source, std::wstring_view word) {
        return std::ranges::any_of(model.Records(), [=](const DictRecord& record) {
            return record.source == source && record.word == word;
        });
    };
    const auto lock_file = [](const std::filesystem::path& path) {
        return ScopedFileHandle(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    };
    const auto read_file = [](const std::filesystem::path& path) {
        std::string bytes;
        QIQI_TEST_CHECK(ReadFileToUtf8String(path, bytes));
        return bytes;
    };
    const std::string full_body =
        "[config]\npage_size=7\n[user_dict]\nabcd 新增词条\n[phrase_freq]\nabcd 新增词组\n";

    reset();
    for (const bool maximum : {false, true}) {
        QiQiConfig expected = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Assign(expected, maximum ? field.maximum : field.minimum));
        });
        expected.user_dict_path = L"not-in-backup.txt";
        QIQI_TEST_CHECK(SaveConfig(expected));
        QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
        const auto bytes = read_file(backup);
        QIQI_TEST_CHECK(!bytes.contains("user_dict_path") && !bytes.contains("not-in-backup.txt"));
        size_t position = bytes.find("[config]\n");
        ForEachConfigField([&](const auto& field) {
            const auto line = std::format("{}={}\n", field.backup_key, field.Value(expected));
            const size_t found = bytes.find(line, position);
            QIQI_TEST_CHECK(found != std::string::npos && bytes.find(line, found + line.size()) == std::string::npos);
            position = found + line.size();
        });
        QIQI_TEST_CHECK(SaveConfig(QiQiConfig{}));
        QIQI_TEST_CHECK(model.RestoreBackupBundle(backup, BackupConflictMode::Merge).status == BackupRestoreStatus::Success);
        const auto restored = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(restored) == field.Value(expected));
        });
        QIQI_TEST_CHECK(restored.user_dict_path.empty());
    }
    ForEachConfigField([&](const auto& field) {
        for (uint64_t value = field.minimum; value <= field.maximum + 1; ++value) {
            const QiQiConfig before = LoadConfig();
            const uint64_t sequence = QueryRegistryConfigSequence();
            WriteRestoreBundle(backup, std::format("[config]\n{}={}\n", field.backup_key, value));
            QiQiConfig expected = before;
            const bool valid = field.Assign(expected, value);
            const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
            QIQI_TEST_CHECK(result.status == (valid ? BackupRestoreStatus::Success : BackupRestoreStatus::Failed));
            if (!valid) {
                QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && QueryRegistryConfigSequence() == sequence);
            }
            const auto restored = LoadConfig();
            ForEachConfigField([&](const auto& other) {
                QIQI_TEST_CHECK(other.Value(restored) == other.Value(expected));
            });
        }
    });
    WriteRestoreBundle(backup, "[config]\nfuture_numeric_key=123\n");
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup, BackupConflictMode::Merge).status == BackupRestoreStatus::Success);

    // Checksummed malformed later input must not apply earlier configuration or dictionary lines.
    reset();
    const auto old_user = read_file(user);
    const auto old_frequency = read_file(frequency);
    const auto old_sequence = QueryRegistryConfigSequence();
    const auto missing = model.RestoreBackupBundle(directory / L"missing.qbak", BackupConflictMode::Replace);
    QIQI_TEST_CHECK(missing.status == BackupRestoreStatus::Failed && missing.stage == BackupRestoreStage::Read);
    for (const auto& malformed : std::array<std::string, 6>{
             full_body + "not_a_code 损坏词组\n",
             full_body + "[config]\npage_size=0\n",
             full_body + "[unexpected]\n",
             full_body + "[user_dict]\nabcd ^\n",
             full_body + "abcd 单\n",
             full_body + "abcd \xFF\n"}) {
        WriteRestoreBundle(backup, malformed);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && FAILED(result.error));
        QIQI_TEST_CHECK(result.refresh_error == S_FALSE && !result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3 && QueryRegistryConfigSequence() == old_sequence);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条"));
    }

    // Actual registry write denial, without changing the process user's real configuration.
    WriteRestoreBundle(backup, "[config]\npage_size=7\n");
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
        QIQI_TEST_CHECK(result.error == E_FAIL && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
        WriteRestoreBundle(backup, full_body);
        const auto partial = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(partial.status == BackupRestoreStatus::Partial && partial.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(partial.operations_succeeded == 2 && partial.operations_failed == 1);
        QIQI_TEST_CHECK(partial.summary.user_entries_applied == 1 && partial.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(!partial.summary.config_restored && LoadConfig().page_size == 3);
    }
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == old_sequence);

    // Replace must stop when the first deletion fails, preserving the original files and config.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    // Later preparation failures report already committed destructive work, never full success.
    {
        auto cache = user;
        cache.replace_extension(L".qdict");
        QIQI_TEST_CHECK(WriteFileAtomically(cache, codec::AsByteSpan(std::string_view("cache"))));
        const auto locked = lock_file(cache);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) == old_frequency && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto channel = GetSyncChannelDirectoryForFile(user);
        const auto journal = channel / L"journal.bin";
        // Make the refresh failure independent of channel-directory enumeration order:
        // the locked journal cannot be removed, and neither source nor snapshot can reload it.
        QIQI_TEST_CHECK(std::filesystem::remove(channel / L"snapshot.txt"));
        const auto locked = lock_file(journal);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded > 0 && result.operations_failed == 2);
        QIQI_TEST_CHECK(FAILED(result.refresh_error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && !result.summary.config_restored);
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Best-effort application keeps honest per-item counts and skips config on partial failure.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 1 && result.summary.phrase_freq_failed == 0);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        BackupRestoreSummary summary;
        QIQI_TEST_CHECK(!model.RestoreBackupBundle(backup, BackupConflictMode::Merge, summary));
        QIQI_TEST_CHECK(summary.user_entries_failed == 1 && summary.phrase_freq_applied == 1);
    }
    reset();
    WriteRestoreBundle(backup, "[user_dict]\nabcd 新增词条\n");
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.user_entries_failed == 1);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 1 && result.summary.user_entries_failed == 0);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Missing optional sources remain a valid, empty view and need not be created by Merge.
    {
        DictManagerModel optional;
        const auto missing_user = directory / L"optional_user.txt";
        const auto missing_frequency = directory / L"optional_frequency.txt";
        optional.InitializePaths({}, missing_user, missing_frequency);
        QIQI_TEST_CHECK(optional.ReloadAllResult() == S_OK);
        WriteRestoreBundle(backup, "[user_dict]\n[phrase_freq]\n");
        const auto result = optional.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 0);
        QIQI_TEST_CHECK(optional.UserEntryCount() == 0 && optional.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(!std::filesystem::exists(missing_user) && !std::filesystem::exists(missing_frequency));
    }

    // A final refresh failure cannot certify a successful restore or a complete displayed view.
    reset();
    WriteRestoreBundle(backup, "[config]\npage_size=7\n");
    {
        const ScopedFileHandle locked(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Replace must stop after an unsuccessful intermediate reload, before applying staged items.
    reset();
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
        WriteRestoreBundle(backup, full_body);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.operations_failed == 1 && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
    }

    // Preserve the apply failure independently of refresh failure; discard speculative promotions.
    reset();
    WriteRestoreBundle(backup, "[phrase_freq]\nabcd 新增词组\n");
    {
        const ScopedFileHandle unreadable_user(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        const auto unwritable_frequency = lock_file(frequency);
        QIQI_TEST_CHECK(unreadable_user && unwritable_frequency);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.refresh_error == E_FAIL && result.operations_failed == 2);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
        const auto message = FormatBackupRestoreMessage(result, UiLanguage::EnUs);
        QIQI_TEST_CHECK(message.contains(L"phrase ordering") && message.contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Clear can fail after its source write when the required synchronization snapshot is locked.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto snapshot = GetSyncChannelDirectoryForFile(frequency) / L"snapshot.qfreq";
        const auto old_snapshot = read_file(snapshot);
        const auto locked = lock_file(snapshot);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Prepare && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.refresh_error == S_OK && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) != old_frequency && model.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(read_file(snapshot) == old_snapshot);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    for (const auto mode : {BackupConflictMode::Merge, BackupConflictMode::Replace}) {
        reset();
        WriteRestoreBundle(backup, full_body +
            "[user_dict]\nabcd ^置顶词条 !屏蔽词条\n");
        const auto result = model.RestoreBackupBundle(backup, mode);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.stage == BackupRestoreStage::Complete);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.error == S_OK && result.operations_failed == 0);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 3 && result.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Pinned, L"置顶词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Blocked, L"屏蔽词条"));
    }

    // The UI's pure formatter cannot label a partial/failed result as successful.
    BackupRestoreResult message_result;
    message_result.operations_failed = 1;
    for (const auto stage : {BackupRestoreStage::Read, BackupRestoreStage::Validate,
             BackupRestoreStage::Prepare, BackupRestoreStage::UserDictionary,
             BackupRestoreStage::PhraseFrequency, BackupRestoreStage::Config, BackupRestoreStage::Refresh}) {
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

} // namespace qiqi::dict_manager::tests
