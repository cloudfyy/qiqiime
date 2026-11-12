// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestDictionarySourceSelection() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_plan_" + std::to_wstring(::GetCurrentProcessId()));
    const std::wstring subkey = L"Software\\QiQiIME_DictPlan_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
        WubiMappedDict::ClearProcessCache();
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        QIQI_TEST_CHECK(!ec);
    });
    const auto module = directory / L"bin";
    const auto data = directory / L"data";
    QIQI_TEST_CHECK(std::filesystem::create_directories(module));
    QIQI_TEST_CHECK(std::filesystem::create_directories(data));
    const auto write = [](const std::filesystem::path& path, std::string_view text) {
        QIQI_TEST_CHECK(WriteFileAtomically(path, codec::AsByteSpan(text)));
    };
    write(data / L"wubi86_jidian.txt", "aaaa 计划极点\n");
    write(data / L"wubi86_jidian_extra.txt", "bbbb 计划扩展\n");
    write(data / L"wubi86_haifeng.txt", "cccc 计划海峰\n");
    write(module / L"relative-user.txt", "dddd 计划用户\n");
    for (const PrimaryDict primary : {PrimaryDict::BuiltinOnly, PrimaryDict::Jidian, PrimaryDict::Haifeng}) {
        for (const bool extra : {false, true}) {
            QiQiConfig config;
            config.primary_dict = primary;
            config.enable_jidian_extra = extra;
            config.user_dict_path = L"relative-user.txt";
            QIQI_TEST_CHECK(SaveConfig(config));
            DictManagerModel model;
            model.InitializePaths(module, {}, directory / L"missing-frequency.txt");
            QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK && model.UserEntryCount() == 1);
            std::vector<DictEntrySource> sources;
            for (const auto& record : model.Records()) {
                if (record.source == DictEntrySource::UserDict) {
                    QIQI_TEST_CHECK(record.word == L"计划用户");
                } else if (record.source == DictEntrySource::PrimaryDict) {
                    QIQI_TEST_CHECK(record.word == (primary == PrimaryDict::Jidian ? L"计划极点" : L"计划海峰"));
                    sources.push_back(record.source);
                } else if (record.source == DictEntrySource::ExtraDict) {
                    QIQI_TEST_CHECK(record.word == L"计划扩展");
                    sources.push_back(record.source);
                }
            }
            std::vector<DictEntrySource> expected;
            if (primary != PrimaryDict::BuiltinOnly) expected.push_back(DictEntrySource::PrimaryDict);
            if (primary == PrimaryDict::Jidian && extra) expected.push_back(DictEntrySource::ExtraDict);
            QIQI_TEST_CHECK(sources == expected);
            if (primary != PrimaryDict::BuiltinOnly) {
                const auto path = data / (primary == PrimaryDict::Jidian ? L"wubi86_jidian.txt" : L"wubi86_haifeng.txt");
                const auto cached = WubiMappedDict::LoadFromTextOrCache(path);
                QIQI_TEST_CHECK(cached && model.ReloadAllResult() == S_OK);
                QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(path) == cached);
            }
        }
    }
    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(std::filesystem::remove(data / L"wubi86_haifeng.txt"));
    QIQI_TEST_CHECK(std::filesystem::remove(data / L"wubi86_haifeng.qdict"));
    DictManagerModel missing;
    missing.InitializePaths(module, {}, directory / L"missing-frequency.txt");
    QIQI_TEST_CHECK(missing.ReloadAllResult() == S_OK && missing.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::none_of(missing.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict || record.source == DictEntrySource::ExtraDict;
    }));

    // Verify custom primary dictionary selection, validation failure retention, and default restoration
    const auto custom_valid = directory / L"custom_primary.txt";
    const auto custom_invalid = directory / L"custom_empty.txt";
    write(custom_valid, "gggg 自定义词管主词\n");
    write(custom_invalid, "# no valid entries\n");

    DictManagerModel custom_model;
    custom_model.InitializePaths(module, {}, directory / L"missing-frequency.txt");
    QIQI_TEST_CHECK(custom_model.SelectCustomPrimaryDictionary(custom_valid));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Custom);
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDictPath() == custom_valid);
    QIQI_TEST_CHECK(custom_model.UserEntryCount() == 1 && custom_model.SystemEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"自定义词管主词";
    }));

    const auto custom_partial = directory / L"custom_partial.txt";
    write(custom_partial, "hhhh 部分有效词\n12345 67890\n");
    const auto previous_sequence = QueryRegistryConfigSequence();
    QIQI_TEST_CHECK(!custom_model.SelectCustomPrimaryDictionary(custom_partial));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == previous_sequence &&
        custom_model.ActivePrimaryDictPath() == custom_valid);
    const auto confirmed = WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_partial);
    QIQI_TEST_CHECK(confirmed && confirmed.diagnostics.invalid_lines == 1);
    QIQI_TEST_CHECK(SUCCEEDED(custom_model.SelectValidatedCustomPrimaryDictionary(custom_partial, confirmed)));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDictPath() == custom_partial &&
        custom_model.SystemEntryCount() == 1 && custom_model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"部分有效词";
    }));
    QIQI_TEST_CHECK(custom_model.SelectCustomPrimaryDictionary(custom_valid));

    // Selecting an invalid file fails and preserves the currently active custom primary dictionary
    QIQI_TEST_CHECK(!custom_model.SelectCustomPrimaryDictionary(custom_invalid));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Custom);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"自定义词管主词";
    }));

    // Restoring default primary dictionary switches back to Jidian while keeping user words intact
    QIQI_TEST_CHECK(custom_model.RestoreDefaultPrimaryDictionary());
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(custom_model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"计划极点";
    }));
}

void TestModelAndCrossProcessSync() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_mgr_test_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.qfreq";
    const auto import_file = temp_dir / L"import.txt";
    const auto export_file = temp_dir / L"export.txt";

    DictManagerModel model;
    const auto module_dir = GetModuleFilePath(nullptr).parent_path();
    model.InitializePaths(module_dir, user_dict, freq_file);
    QIQI_TEST_CHECK(model.UserEntryCount() == 0);
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);

    // Auto-compute Wubi 86 phrase and single-char codes
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中华人民共和国") == L"kwwl");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中国") == L"khlg");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"工") == L"aaaa");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"   ").empty());

    // Add, modify, and remove user entries while an active WubiEngine instance syncs incrementally
    WubiEngine peer_engine;
    peer_engine.SetUserDictPath(user_dict);
    peer_engine.SetPhraseFreqPath(freq_file);
    peer_engine.SetDynamicPhraseFreq(true);

    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词一"));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 2);

    QIQI_TEST_CHECK(model.ModifyUserEntry(L"abcd", L"管理词一", L"管理词改"));
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    const auto after_mod = peer_engine.LookupExact(L"abcd");
    QIQI_TEST_CHECK(after_mod.size() == 2 && gsl::at(after_mod, 0) == L"管理词改");

    QIQI_TEST_CHECK(model.RemoveUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 1);

    QIQI_TEST_CHECK(model.DeleteOrBlockEntry(L"abcd", L"管理词改")); // deletes user entry
    QIQI_TEST_CHECK(model.DeleteOrBlockEntry(L"aaaa", L"工"));     // blocks system entry
    QIQI_TEST_CHECK(model.UnblockSystemEntry(L"aaaa", L"工"));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词改"));

    // Invalid input validation branches
    QIQI_TEST_CHECK(!model.AddOrUpdateUserEntry(L"", L"词") && !model.AddOrUpdateUserEntry(L"abcd", L""));
    QIQI_TEST_CHECK(!model.ModifyUserEntry(L"", L"旧", L"新") && !model.ModifyUserEntry(L"abcd", L"", L"新"));
    QIQI_TEST_CHECK(!model.PinUserEntry(L"", L"词") && !model.UnpinUserEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.BlockSystemEntry(L"", L"词") && !model.UnblockSystemEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.DeleteOrBlockEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.ExportRecordsToFile({}, model.Records()));
    QIQI_TEST_CHECK(!model.CreateBackupBundle({}));
    size_t bad_imported = 0;
    QIQI_TEST_CHECK(!model.ImportUserEntriesFromFile(temp_dir / L"missing.txt", bad_imported));

    // Phrase frequency promotion, removal, and clearing synced to peer WubiEngine
    QIQI_TEST_CHECK(!model.PromotePhraseFrequency(L"abcd", L"单"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"管理词改"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).front() == L"高频词组");

    QIQI_TEST_CHECK(model.RemovePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).size() == 1);

    QIQI_TEST_CHECK(model.ClearAllPhraseFrequencies());
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().empty());

    // PollExternalChanges when peer_engine adds an entry and promotes a phrase
    QIQI_TEST_CHECK(!model.PollExternalChanges());
    QIQI_TEST_CHECK(peer_engine.AddUserEntry(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(peer_engine.PromoteUserPhraseFrequency(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(model.PollExternalChanges());
    QIQI_TEST_CHECK(model.UserEntryCount() == 2 && model.PhraseFreqCount() == 1);

    // Import and export
    {
        std::ofstream ofs(import_file, std::ios::binary | std::ios::trunc);
        ofs << "qwer 导入词一 导入词二\n";
    }
    size_t imported = 0;
    QIQI_TEST_CHECK(model.ImportUserEntriesFromFile(import_file, imported) && imported == 2);
    QIQI_TEST_CHECK(model.ExportRecordsToFile(export_file, model.Records()));
    std::string exported_utf8;
    QIQI_TEST_CHECK(ReadFileToUtf8String(export_file, exported_utf8) && !exported_utf8.empty());

    // Filter matching and clipboard formatting
    DictFilter filter{};
    filter.source = DictEntrySource::UserDict;
    for (const auto* query : {L" QW ", L"导入"}) {
        filter.SetQuery(query);
        size_t matched = 0;
        for (const auto& rec : model.Records()) {
            if (filter.Matches(rec)) ++matched;
        }
        QIQI_TEST_CHECK(matched == 2);
    }
    QIQI_TEST_CHECK(!FormatRecordsForClipboard(model.Records()).empty());

    // Backup & Restore (.qbak bundle with FNV-1a checksum validation, Merge vs Replace conflict modes)
    const std::wstring test_reg_subkey =
        L"Software\\QiQiIME_DictMgrBackupTest_" + std::to_wstring(::GetCurrentProcessId());
    SetConfigRegistrySubKeyOverrideForTest(test_reg_subkey.c_str());
    const auto restore_reg = gsl::finally([&test_reg_subkey]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, test_reg_subkey.c_str());
    });

    QiQiConfig custom_cfg = LoadConfig();
    custom_cfg.page_size = 7;
    custom_cfg.dynamic_phrase_freq = true;
    custom_cfg.commit_on_english_switch = false;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));

    QIQI_TEST_CHECK(model.PinUserEntry(L"abcd", L"备份置顶词"));
    QIQI_TEST_CHECK(model.BlockSystemEntry(L"aaaa", L"工"));
    const auto backup_file = temp_dir / L"test_backup.qbak";
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup_file));

    // Corrupting the backup file payload must be rejected by checksum validation
    std::string raw_backup;
    QIQI_TEST_CHECK(ReadFileToUtf8String(backup_file, raw_backup));
    QIQI_TEST_CHECK(raw_backup.contains("commit_on_english_switch=0\n"));
    const auto corrupt_file = temp_dir / L"corrupt.qbak";
    {
        std::string corrupted = raw_backup + "\nabcd 被篡改词\n";
        QIQI_TEST_CHECK(WriteFileAtomically(corrupt_file, codec::AsByteSpan(corrupted)));
    }
    BackupRestoreSummary summary{};
    QIQI_TEST_CHECK(!model.RestoreBackupBundle(corrupt_file, BackupConflictMode::Replace, summary));

    // Mutate current state and test Replace mode (overwrites config, user_dict, and phrase_freq)
    custom_cfg.page_size = 3;
    custom_cfg.commit_on_english_switch = true;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"临时未备份词"));
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Replace, summary));
    QIQI_TEST_CHECK(summary.config_restored && summary.user_entries_applied >= 5 && summary.phrase_freq_applied == 1);
    QIQI_TEST_CHECK(LoadConfig().page_size == 7);
    QIQI_TEST_CHECK(!LoadConfig().commit_on_english_switch);
    QIQI_TEST_CHECK(model.PinnedEntryCount() == 1 && model.BlockedEntryCount() == 1);

    // Test Merge mode (preserves newly added entry while re-applying backup entries)
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"mnop", L"合并保留词"));
    const size_t before_merge_count = model.UserEntryCount();
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Merge, summary));
    QIQI_TEST_CHECK(model.UserEntryCount() == before_merge_count);

    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"lmno", L"待修改词"));
    const size_t before_replace = model.UserEntryCount();
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"待修改词", L"lmno", L"待修改词"));
    model.ReloadAll();
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"待修改词", L"lmno", L"同码新词"));
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"lmno").front() == L"同码新词");
    const auto snapshots = peer_engine.UserDictSnapshotSyncCount();
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"同码新词", L" PQRS ", L"  新码新词  "));
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.UserDictSnapshotSyncCount() > snapshots);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"lmno").empty());
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"pqrs").front() == L"新码新词");
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"新码新词", L"a1", L"无效"));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"新码新词", L"abcd", L" "));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"已删除原词", L"abcd", L"不能复活"));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"已删除原词", L"pqrs", L"不能复活"));
    {
        const ScopedFileHandle locked(::CreateFileW(user_dict.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        QIQI_TEST_CHECK(model.ReplaceUserEntryResult(L"pqrs", L"新码新词", L"lmno", L"写入失败") ==
            HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
    }
    {
        const DWORD attributes = ::GetFileAttributesW(user_dict.c_str());
        QIQI_TEST_CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        const auto restore_attributes = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes));
        });
        QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes | FILE_ATTRIBUTE_READONLY));
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntryResult(L"abcd", L"无权新增") == E_ACCESSDENIED);
        QIQI_TEST_CHECK(model.ReplaceUserEntryResult(L"pqrs", L"新码新词", L"lmno", L"无权修改") == E_ACCESSDENIED);
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntryResult(L"a1", L"无效") == E_INVALIDARG);
    }
    model.ReloadAll();
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(std::ranges::any_of(model.Records(), [](const DictRecord& rec) {
        return rec.source == DictEntrySource::UserDict && rec.code == L"pqrs" && rec.word == L"新码新词";
    }));
}

} // namespace qiqi::dict_manager::tests
