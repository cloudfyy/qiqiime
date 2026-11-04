#include "tools/dict_manager/model.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <algorithm>
#include <cwctype>
#include <format>
#include <map>

namespace qiqi::dict_manager {

namespace {

std::wstring NormalizeAsciiLower(std::wstring_view input) {
    std::wstring out;
    out.reserve(input.size());
    for (const wchar_t ch : input) {
        if (ch >= L'A' && ch <= L'Z') {
            out.push_back(gsl::narrow_cast<wchar_t>(ch - L'A' + L'a'));
        } else if (ch != L' ' && ch != L'\t') {
            out.push_back(ch);
        }
    }
    return out;
}

std::wstring TrimWhitespace(std::wstring_view input) {
    size_t first = 0;
    while (first < input.size() && (gsl::at(input, first) == L' ' || gsl::at(input, first) == L'\t' ||
                                    gsl::at(input, first) == L'\r' || gsl::at(input, first) == L'\n')) {
        ++first;
    }
    size_t last = input.size();
    while (last > first && (gsl::at(input, last - 1) == L' ' || gsl::at(input, last - 1) == L'\t' ||
                            gsl::at(input, last - 1) == L'\r' || gsl::at(input, last - 1) == L'\n')) {
        --last;
    }
    return std::wstring(input.substr(first, last - first));
}

} // namespace

bool DictFilter::Matches(const DictRecord& record) const {
    if (source != DictEntrySource::All && record.source != source) {
        return false;
    }
    if (!code_query.empty()) {
        const std::wstring norm_code = NormalizeAsciiLower(code_query);
        if (!norm_code.empty() && record.code.find(norm_code) == std::wstring::npos) {
            return false;
        }
    }
    if (!word_query.empty()) {
        const std::wstring trimmed = TrimWhitespace(word_query);
        if (!trimmed.empty() &&
            record.word.find(trimmed) == std::wstring::npos &&
            record.code.find(NormalizeAsciiLower(trimmed)) == std::wstring::npos) {
            return false;
        }
    }
    return true;
}

void DictManagerModel::InitializePaths(
    const std::filesystem::path& module_dir,
    const std::filesystem::path& user_dict_path,
    const std::filesystem::path& phrase_freq_path) {
    module_dir_ = module_dir;
    const QiQiConfig cfg = LoadConfig();
    if (!user_dict_path.empty()) {
        user_dict_path_ = user_dict_path;
    } else if (!cfg.user_dict_path.empty()) {
        std::filesystem::path configured(cfg.user_dict_path);
        if (configured.is_relative() && !module_dir_.empty()) {
            configured = ResolveBundledDataFile(module_dir_, cfg.user_dict_path);
        }
        user_dict_path_ = std::move(configured);
    } else {
        user_dict_path_ = GetDefaultUserDictPath();
    }
    phrase_freq_path_ = !phrase_freq_path.empty() ? phrase_freq_path : GetDefaultPhraseFreqPath();
    ReloadAll();
}

void DictManagerModel::ReloadAll() {
    const GUID activity_id = log::CreateActivityId();
    QIQI_LOG_ACT_START(log::Level::debug, log::Keyword::Dictionary, &activity_id, nullptr, "DictManagerModel::ReloadAll(begin)");
    engine_ = WubiEngine{};
    primary_dict_.reset();
    extra_dict_.reset();

    const QiQiConfig cfg = LoadConfig();
    engine_.ApplyConfig(cfg);
    engine_.SetDynamicPhraseFreq(true);

    if (!module_dir_.empty()) {
        if (cfg.primary_dict == PrimaryDict::Jidian) {
            const auto jidian_path = ResolveBundledDataFile(module_dir_, L"wubi86_jidian.txt");
            if (std::filesystem::exists(jidian_path)) {
                primary_dict_ = WubiMappedDict::LoadFromTextOrCache(jidian_path);
                if (primary_dict_) {
                    std::ignore = engine_.LoadDictionaryFile(jidian_path);
                }
            }
            if (cfg.enable_jidian_extra) {
                const auto extra_path = ResolveBundledDataFile(module_dir_, L"wubi86_jidian_extra.txt");
                if (std::filesystem::exists(extra_path)) {
                    extra_dict_ = WubiMappedDict::LoadFromTextOrCache(extra_path);
                    if (extra_dict_) {
                        std::ignore = engine_.LoadDictionaryFile(extra_path);
                    }
                }
            }
        } else if (cfg.primary_dict == PrimaryDict::Haifeng) {
            const auto haifeng_path = ResolveBundledDataFile(module_dir_, L"wubi86_haifeng.txt");
            if (std::filesystem::exists(haifeng_path)) {
                primary_dict_ = WubiMappedDict::LoadFromTextOrCache(haifeng_path);
                if (primary_dict_) {
                    std::ignore = engine_.LoadDictionaryFile(haifeng_path);
                }
            }
        }
    }

    engine_.SetUserDictPath(user_dict_path_);
    if (!user_dict_path_.empty()) {
        std::string raw_utf8;
        if (ReadFileToUtf8String(user_dict_path_, raw_utf8)) {
            std::map<uint32_t, std::vector<std::wstring>> user_entries;
            WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_utf8)), user_entries);
            for (const auto& [packed, words] : user_entries) {
                const std::wstring code = UnpackWubiCode(packed);
                for (const auto& w : words) {
                    engine_.AddEntry(code, w);
                }
            }
        }
        std::ignore = engine_.SynchronizeUserDictionary(sync::InputBoundary::NewInput);
    }

    engine_.SetPhraseFreqPath(phrase_freq_path_);
    if (!phrase_freq_path_.empty()) {
        std::ignore = engine_.LoadPhraseFrequencies(phrase_freq_path_);
        std::ignore = engine_.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
    }

    RebuildRecords();
    QIQI_LOG_ACT_STOP(
        log::Level::info,
        log::Keyword::Dictionary,
        &activity_id,
        "DictManagerModel::ReloadAll(user={}, freq={}, system={})",
        user_entry_count_,
        phrase_freq_count_,
        system_entry_count_);
}

bool DictManagerModel::PollExternalChanges() {
    const uint64_t prev_user_inc = engine_.UserDictIncrementalSyncCount();
    const uint64_t prev_user_snap = engine_.UserDictSnapshotSyncCount();
    const uint64_t prev_freq_inc = engine_.PhraseFreqIncrementalSyncCount();
    const uint64_t prev_freq_snap = engine_.PhraseFreqSnapshotSyncCount();

    std::ignore = engine_.SynchronizeUserDictionary(sync::InputBoundary::NewInput);
    std::ignore = engine_.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);

    if (engine_.UserDictIncrementalSyncCount() != prev_user_inc ||
        engine_.UserDictSnapshotSyncCount() != prev_user_snap ||
        engine_.PhraseFreqIncrementalSyncCount() != prev_freq_inc ||
        engine_.PhraseFreqSnapshotSyncCount() != prev_freq_snap) {
        RebuildRecords();
        return true;
    }
    return false;
}

void DictManagerModel::RebuildRecords() {
    records_.clear();
    user_entry_count_ = 0;
    phrase_freq_count_ = 0;
    system_entry_count_ = 0;
    uint64_t next_id = 1;

    for (const auto& [packed, words] : engine_.UserDictionaryEntries()) {
        const std::wstring code = UnpackWubiCode(packed);
        uint32_t rank = 1;
        for (const auto& w : words) {
            records_.push_back(DictRecord{
                next_id++, packed, code, w, DictEntrySource::UserDict, rank++});
            ++user_entry_count_;
        }
    }

    for (const auto& [packed, words] : engine_.PromotedPhrases()) {
        const std::wstring code = UnpackWubiCode(packed);
        uint32_t rank = 1;
        for (const auto& w : words) {
            records_.push_back(DictRecord{
                next_id++, packed, code, w, DictEntrySource::PhraseFreq, rank++});
            ++phrase_freq_count_;
        }
    }

    if (primary_dict_) {
        uint32_t last_code = 0;
        uint32_t rank = 1;
        std::wstring code_str;
        primary_dict_->ForEachEntry([&](uint32_t packed, std::wstring_view word) {
            if (packed != last_code) {
                last_code = packed;
                code_str = UnpackWubiCode(packed);
                rank = 1;
            }
            records_.push_back(DictRecord{
                next_id++, packed, code_str, std::wstring(word), DictEntrySource::PrimaryDict, rank++});
            ++system_entry_count_;
        });
    }

    if (extra_dict_) {
        uint32_t last_code = 0;
        uint32_t rank = 1;
        std::wstring code_str;
        extra_dict_->ForEachEntry([&](uint32_t packed, std::wstring_view word) {
            if (packed != last_code) {
                last_code = packed;
                code_str = UnpackWubiCode(packed);
                rank = 1;
            }
            records_.push_back(DictRecord{
                next_id++, packed, code_str, std::wstring(word), DictEntrySource::ExtraDict, rank++});
            ++system_entry_count_;
        });
    }
}

const DictRecord* DictManagerModel::FindById(uint64_t id) const noexcept {
    if (id == 0) {
        return nullptr;
    }
    const auto it = std::lower_bound(
        records_.begin(),
        records_.end(),
        id,
        [](const DictRecord& rec, uint64_t target) noexcept { return rec.id < target; });
    return (it != records_.end() && it->id == id) ? &(*it) : nullptr;
}

std::wstring DictManagerModel::ComputeAutoCode(std::wstring_view word) const {
    const std::wstring trimmed = TrimWhitespace(word);
    if (trimmed.empty()) {
        return {};
    }
    std::wstring code = engine_.EncodePhraseWubi86(trimmed);
    if (code.empty()) {
        code = engine_.LookupCharacterCode(trimmed);
    }
    return code;
}

bool DictManagerModel::AddOrUpdateUserEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (PackWubiCode(norm_code) == 0 || norm_word.empty()) {
        return false;
    }
    if (!engine_.AddUserEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::ModifyUserEntry(
    std::wstring_view code, std::wstring_view old_word, std::wstring_view new_word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_old = TrimWhitespace(old_word);
    const std::wstring norm_new = TrimWhitespace(new_word);
    if (PackWubiCode(norm_code) == 0 || norm_old.empty() || norm_new.empty()) {
        return false;
    }
    if (!engine_.ModifyUserEntry(norm_code, norm_old, norm_new, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::RemoveUserEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.RemoveUserEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::PromotePhraseFrequency(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.PromoteUserPhraseFrequency(norm_code, norm_word, phrase_freq_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::RemovePhraseFrequency(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.RemovePhraseFrequency(norm_code, norm_word, phrase_freq_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::ClearAllPhraseFrequencies() {
    if (!engine_.ClearPhraseFrequencies(phrase_freq_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::ImportUserEntriesFromFile(
    const std::filesystem::path& import_path, size_t& out_imported_count) {
    out_imported_count = 0;
    std::string raw_utf8;
    if (!ReadFileToUtf8String(import_path, raw_utf8)) {
        return false;
    }
    std::map<uint32_t, std::vector<std::wstring>> parsed;
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_utf8)), parsed);
    if (parsed.empty()) {
        return false;
    }
    for (const auto& [packed, words] : parsed) {
        const std::wstring code = UnpackWubiCode(packed);
        for (const auto& w : words) {
            if (engine_.AddUserEntry(code, w, user_dict_path_)) {
                ++out_imported_count;
            }
        }
    }
    RebuildRecords();
    return out_imported_count > 0;
}

bool DictManagerModel::ExportRecordsToFile(
    const std::filesystem::path& export_path,
    std::span<const DictRecord> records) const {
    if (export_path.empty()) {
        return false;
    }
    std::map<uint32_t, std::vector<std::wstring>> grouped;
    for (const auto& rec : records) {
        if (rec.packed_code != 0 && !rec.word.empty()) {
            AppendUniqueWord(grouped[rec.packed_code], rec.word);
        }
    }
    std::string out;
    for (const auto& [packed, words] : grouped) {
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& w : words) {
            out += code_utf8;
            out.push_back(' ');
            out += WideToUtf8(w);
            out.push_back('\n');
        }
    }
    return WriteFileAtomically(export_path, codec::AsByteSpan(out));
}

std::wstring FormatRecordsForClipboard(std::span<const DictRecord> records) {
    std::wstring out;
    for (const auto& rec : records) {
        out += std::format(L"{}\t{}\r\n", rec.code, rec.word);
    }
    return out;
}

} // namespace qiqi::dict_manager
