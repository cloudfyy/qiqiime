// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/backup.h"
#include "ime/engine/engine.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::dict_manager {

enum class DictEntrySource : uint8_t {
    All = 0,
    UserDict = 1,
    Pinned = 2,
    Blocked = 3,
    PhraseFreq = 4,
    PrimaryDict = 5,
    ExtraDict = 6,
};

struct DictRecord {
    uint64_t id = 0;
    uint32_t packed_code = 0;
    std::wstring code;
    std::wstring word;
    DictEntrySource source = DictEntrySource::UserDict;
    uint32_t rank_in_code = 1;
};

struct DictFilter {
    DictEntrySource source = DictEntrySource::All;

    void SetQuery(std::wstring_view query);
    bool Matches(const DictRecord& record) const noexcept;

private:
    std::wstring query_;
    bool search_code_ = false;
};

class DictManagerModel final {
public:
    DictManagerModel() = default;

    // Initialize or reload engine dictionaries from the given module directory and paths.
    void InitializePaths(
        const std::filesystem::path& module_dir,
        const std::filesystem::path& user_dict_path = {},
        const std::filesystem::path& phrase_freq_path = {});

    void ReloadAll();
    // Missing optional files are valid; failures leave a fresh but potentially incomplete persisted view.
    HRESULT ReloadAllResult();
    bool PollExternalChanges();

    const std::filesystem::path& UserDictPath() const noexcept { return user_dict_path_; }
    const std::filesystem::path& PhraseFreqPath() const noexcept { return phrase_freq_path_; }
    PrimaryDict ActivePrimaryDict() const noexcept { return active_primary_dict_; }
    const std::filesystem::path& ActivePrimaryDictPath() const noexcept { return active_primary_dict_path_; }

    // Validate, persist, and switch to a user-selected custom Wubi 86 primary dictionary file,
    // or restore the bundled default primary dictionary (retaining previous state on failure).
    bool SelectCustomPrimaryDictionary(const std::filesystem::path& custom_path);
    HRESULT SelectCustomPrimaryDictionaryResult(const std::filesystem::path& custom_path);
    HRESULT SelectValidatedCustomPrimaryDictionary(
        const std::filesystem::path& custom_path, const PrimaryDictValidationResult& validation);
    bool RestoreDefaultPrimaryDictionary();
    HRESULT RestoreDefaultPrimaryDictionaryResult();

    const std::vector<DictRecord>& Records() const noexcept { return records_; }
    const DictRecord* FindById(uint64_t id) const noexcept;

    size_t UserEntryCount() const noexcept { return user_entry_count_; }
    size_t PinnedEntryCount() const noexcept { return pinned_entry_count_; }
    size_t BlockedEntryCount() const noexcept { return blocked_entry_count_; }
    size_t PhraseFreqCount() const noexcept { return phrase_freq_count_; }
    size_t SystemEntryCount() const noexcept { return system_entry_count_; }

    // Auto-compute Wubi 86 4-letter code for a character or phrase.
    std::wstring ComputeAutoCode(std::wstring_view word) const;

    // User dictionary, manual pinning, and blocklist operations (coordinated via change_sync)
    bool AddOrUpdateUserEntry(std::wstring_view code, std::wstring_view word);
    HRESULT AddOrUpdateUserEntryResult(std::wstring_view code, std::wstring_view word);
    bool ModifyUserEntry(std::wstring_view code, std::wstring_view old_word, std::wstring_view new_word);
    bool ReplaceUserEntry(
        std::wstring_view old_code, std::wstring_view old_word,
        std::wstring_view new_code, std::wstring_view new_word);
    HRESULT ReplaceUserEntryResult(
        std::wstring_view old_code, std::wstring_view old_word,
        std::wstring_view new_code, std::wstring_view new_word);
    bool RemoveUserEntry(std::wstring_view code, std::wstring_view word);
    bool PinUserEntry(std::wstring_view code, std::wstring_view word);
    bool UnpinUserEntry(std::wstring_view code, std::wstring_view word);
    bool BlockSystemEntry(std::wstring_view code, std::wstring_view word);
    bool UnblockSystemEntry(std::wstring_view code, std::wstring_view word);
    bool DeleteOrBlockEntry(std::wstring_view code, std::wstring_view word);

    // Dynamic phrase frequency operations (coordinated via change_sync)
    bool PromotePhraseFrequency(std::wstring_view code, std::wstring_view word);
    HRESULT PromotePhraseFrequencyResult(std::wstring_view code, std::wstring_view word);
    bool RemovePhraseFrequency(std::wstring_view code, std::wstring_view word);
    bool ClearAllPhraseFrequencies();

    // Batch import/export UTF-8 text dictionary (`code word1 word2 ...`)
    bool ImportUserEntriesFromFile(const std::filesystem::path& import_path, size_t& out_imported_count);
    bool ExportRecordsToFile(
        const std::filesystem::path& export_path,
        std::span<const DictRecord> records) const;

    // Complete portable ZIP backup of configuration, dictionary rules, phrase order, and a selected custom primary dictionary.
    HRESULT CreateBackupBundleResult(const std::filesystem::path& backup_path) const;
    bool CreateBackupBundle(const std::filesystem::path& backup_path) const;
    BackupRestoreResult RestoreBackupBundle(
        const std::filesystem::path& backup_path,
        BackupConflictMode mode);
    // Compatibility: true means full success, never partial application.
    bool RestoreBackupBundle(
        const std::filesystem::path& backup_path,
        BackupConflictMode mode,
        BackupRestoreSummary& out_summary);

private:
    void RebuildRecords();

    WubiEngine engine_;
    std::filesystem::path module_dir_;
    std::filesystem::path user_dict_path_;
    bool publish_user_dict_directory_ = false;
    std::filesystem::path phrase_freq_path_;
    std::shared_ptr<const WubiMappedDict> primary_dict_;
    std::shared_ptr<const WubiMappedDict> extra_dict_;
    PrimaryDict active_primary_dict_ = PrimaryDict::Jidian;
    std::filesystem::path active_primary_dict_path_;
    uint64_t config_registry_sequence_ = 0;
    std::vector<DictRecord> records_;
    size_t user_entry_count_ = 0;
    size_t pinned_entry_count_ = 0;
    size_t blocked_entry_count_ = 0;
    size_t phrase_freq_count_ = 0;
    size_t system_entry_count_ = 0;
};

std::wstring FormatRecordsForClipboard(std::span<const DictRecord> records);

} // namespace qiqi::dict_manager
