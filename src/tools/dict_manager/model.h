#pragma once

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
    PhraseFreq = 2,
    PrimaryDict = 3,
    ExtraDict = 4,
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
    std::wstring code_query;
    std::wstring word_query;

    bool Matches(const DictRecord& record) const;
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
    bool PollExternalChanges();

    const std::filesystem::path& UserDictPath() const noexcept { return user_dict_path_; }
    const std::filesystem::path& PhraseFreqPath() const noexcept { return phrase_freq_path_; }

    const std::vector<DictRecord>& Records() const noexcept { return records_; }
    const DictRecord* FindById(uint64_t id) const noexcept;

    size_t UserEntryCount() const noexcept { return user_entry_count_; }
    size_t PhraseFreqCount() const noexcept { return phrase_freq_count_; }
    size_t SystemEntryCount() const noexcept { return system_entry_count_; }

    // Auto-compute Wubi 86 4-letter code for a character or phrase.
    std::wstring ComputeAutoCode(std::wstring_view word) const;

    // User dictionary operations (coordinated via change_sync)
    bool AddOrUpdateUserEntry(std::wstring_view code, std::wstring_view word);
    bool ModifyUserEntry(std::wstring_view code, std::wstring_view old_word, std::wstring_view new_word);
    bool RemoveUserEntry(std::wstring_view code, std::wstring_view word);

    // Dynamic phrase frequency operations (coordinated via change_sync)
    bool PromotePhraseFrequency(std::wstring_view code, std::wstring_view word);
    bool RemovePhraseFrequency(std::wstring_view code, std::wstring_view word);
    bool ClearAllPhraseFrequencies();

    // Batch import/export UTF-8 text dictionary (`code word1 word2 ...`)
    bool ImportUserEntriesFromFile(const std::filesystem::path& import_path, size_t& out_imported_count);
    bool ExportRecordsToFile(
        const std::filesystem::path& export_path,
        std::span<const DictRecord> records) const;

private:
    void RebuildRecords();

    WubiEngine engine_;
    std::filesystem::path module_dir_;
    std::filesystem::path user_dict_path_;
    std::filesystem::path phrase_freq_path_;
    std::shared_ptr<const WubiMappedDict> primary_dict_;
    std::shared_ptr<const WubiMappedDict> extra_dict_;
    std::vector<DictRecord> records_;
    size_t user_entry_count_ = 0;
    size_t phrase_freq_count_ = 0;
    size_t system_entry_count_ = 0;
};

std::wstring FormatRecordsForClipboard(std::span<const DictRecord> records);

} // namespace qiqi::dict_manager
