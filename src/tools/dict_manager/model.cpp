#include "tools/dict_manager/model.h"
#include "common/binary_codec.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/version.h"

#include <gsl/gsl>
#include <algorithm>
#include <charconv>
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

void DictFilter::SetQuery(std::wstring_view query) {
    std::wstring normalized = TrimWhitespace(query);
    const bool code = !normalized.empty() && std::ranges::all_of(normalized, [](wchar_t ch) noexcept {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
    });
    if (code) normalized = NormalizeAsciiLower(normalized);
    query_ = std::move(normalized);
    search_code_ = code;
}

bool DictFilter::Matches(const DictRecord& record) const noexcept {
    return (source == DictEntrySource::All || record.source == source) &&
        (query_.empty() || (search_code_ ? record.code : record.word).find(query_) != std::wstring::npos);
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
            WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_utf8)), user_entries, true);
            for (const auto& [packed, words] : user_entries) {
                const std::wstring code = UnpackWubiCode(packed);
                for (const auto& w : words) {
                    if (w.starts_with(L'^') || w.starts_with(L'!')) {
                        continue;
                    }
                    engine_.AddEntry(code, w);
                }
                for (auto it = words.rbegin(); it != words.rend(); ++it) {
                    if (it->starts_with(L'^') || it->starts_with(L'!')) {
                        engine_.AddEntry(code, *it);
                    }
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
    pinned_entry_count_ = 0;
    blocked_entry_count_ = 0;
    phrase_freq_count_ = 0;
    system_entry_count_ = 0;
    uint64_t next_id = 1;

    for (const auto& [packed, words] : engine_.PinnedEntries()) {
        const std::wstring code = UnpackWubiCode(packed);
        uint32_t rank = 1;
        for (const auto& w : words) {
            records_.push_back(DictRecord{
                next_id++, packed, code, w, DictEntrySource::Pinned, rank++});
            ++pinned_entry_count_;
        }
    }

    for (const auto& [packed, words] : engine_.UserDictionaryEntries()) {
        const std::wstring code = UnpackWubiCode(packed);
        uint32_t rank = 1;
        for (const auto& w : words) {
            records_.push_back(DictRecord{
                next_id++, packed, code, w, DictEntrySource::UserDict, rank++});
            ++user_entry_count_;
        }
    }

    for (const auto& [packed, words] : engine_.BlockedEntries()) {
        const std::wstring code = UnpackWubiCode(packed);
        uint32_t rank = 1;
        for (const auto& w : words) {
            records_.push_back(DictRecord{
                next_id++, packed, code, w, DictEntrySource::Blocked, rank++});
            ++blocked_entry_count_;
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

bool DictManagerModel::ReplaceUserEntry(
    std::wstring_view old_code, std::wstring_view old_word,
    std::wstring_view new_code, std::wstring_view new_word) {
    const auto code = NormalizeAsciiLower(new_code);
    const auto word = TrimWhitespace(new_word);
    if (PackWubiCode(code) == 0 || word.empty()) return false;
    if (!engine_.ReplaceUserEntry(old_code, old_word, code, word, user_dict_path_)) return false;
    RebuildRecords();
    return true;
}

bool DictManagerModel::PinUserEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.PinUserEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::UnpinUserEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.UnpinUserEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::BlockSystemEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.BlockSystemEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::UnblockSystemEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.UnblockSystemEntry(norm_code, norm_word, user_dict_path_)) {
        return false;
    }
    RebuildRecords();
    return true;
}

bool DictManagerModel::DeleteOrBlockEntry(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (!engine_.DeleteOrBlockUserOrSystemEntry(norm_code, norm_word, user_dict_path_)) {
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

namespace {

constexpr std::string_view kBackupMagicLine = "# QIQIIME_BACKUP_V1\n";
constexpr std::string_view kBackupChecksumPrefix = "# checksum_fnv1a64=";

std::string SerializeConfigSection(const QiQiConfig& cfg) {
    std::string out = "[config]\n";
    out += std::format("primary_dict={}\n", static_cast<int>(cfg.primary_dict));
    out += std::format("enable_jidian_extra={}\n", cfg.enable_jidian_extra ? 1 : 0);
    out += std::format("page_size={}\n", cfg.page_size);
    out += std::format("auto_commit_4code={}\n", cfg.auto_commit_4code ? 1 : 0);
    out += std::format("push_commit_5th={}\n", cfg.push_commit_5th ? 1 : 0);
    out += std::format("semicolon_quote_quick_select={}\n", cfg.semicolon_quote_quick_select ? 1 : 0);
    out += std::format("show_code_hint={}\n", cfg.show_code_hint ? 1 : 0);
    out += std::format("enable_z_temp_english={}\n", cfg.enable_z_temp_english ? 1 : 0);
    out += std::format("dynamic_phrase_freq={}\n", cfg.dynamic_phrase_freq ? 1 : 0);
    out += std::format("default_chinese_mode={}\n", cfg.default_chinese_mode ? 1 : 0);
    out += std::format("default_full_width={}\n", cfg.default_full_width ? 1 : 0);
    out += std::format("default_chinese_punct={}\n", cfg.default_chinese_punct ? 1 : 0);
    out += std::format("shift_toggle_mode={}\n", cfg.shift_toggle_mode ? 1 : 0);
    out += std::format("commit_on_english_switch={}\n", cfg.commit_on_english_switch ? 1 : 0);
    out += std::format("show_status_bar={}\n", cfg.show_status_bar ? 1 : 0);
    out += std::format("beep_on_empty_4code={}\n", cfg.beep_on_empty_4code ? 1 : 0);
    out += std::format("beep_on_dup_4code={}\n", cfg.beep_on_dup_4code ? 1 : 0);
    out += std::format("clear_on_empty_4code={}\n", cfg.clear_on_empty_4code ? 1 : 0);
    out += std::format("enable_key_sound={}\n", cfg.enable_key_sound ? 1 : 0);
    out += std::format("up_down_page_turn={}\n", cfg.up_down_page_turn ? 1 : 0);
    out += std::format("ui_language={}\n", static_cast<int>(cfg.ui_language));
    out += std::format("underline_style={}\n", static_cast<int>(cfg.underline_style));
    out += std::format("bold_underline={}\n", cfg.bold_underline ? 1 : 0);
    out += std::format("log_level={}\n", static_cast<int>(cfg.log_level));
    return out;
}

void ApplyConfigKeyValue(QiQiConfig& cfg, std::string_view key, int val) noexcept {
    if (key == "primary_dict" && val >= 0 && val <= 2) cfg.primary_dict = static_cast<PrimaryDict>(val);
    else if (key == "enable_jidian_extra") cfg.enable_jidian_extra = (val != 0);
    else if (key == "page_size" && val >= 1 && val <= 9) cfg.page_size = gsl::narrow_cast<size_t>(val);
    else if (key == "auto_commit_4code") cfg.auto_commit_4code = (val != 0);
    else if (key == "push_commit_5th") cfg.push_commit_5th = (val != 0);
    else if (key == "semicolon_quote_quick_select") cfg.semicolon_quote_quick_select = (val != 0);
    else if (key == "show_code_hint") cfg.show_code_hint = (val != 0);
    else if (key == "enable_z_temp_english") cfg.enable_z_temp_english = (val != 0);
    else if (key == "dynamic_phrase_freq") cfg.dynamic_phrase_freq = (val != 0);
    else if (key == "default_chinese_mode") cfg.default_chinese_mode = (val != 0);
    else if (key == "default_full_width") cfg.default_full_width = (val != 0);
    else if (key == "default_chinese_punct") cfg.default_chinese_punct = (val != 0);
    else if (key == "shift_toggle_mode") cfg.shift_toggle_mode = (val != 0);
    else if (key == "commit_on_english_switch") cfg.commit_on_english_switch = (val != 0);
    else if (key == "show_status_bar") cfg.show_status_bar = (val != 0);
    else if (key == "beep_on_empty_4code") cfg.beep_on_empty_4code = (val != 0);
    else if (key == "beep_on_dup_4code") cfg.beep_on_dup_4code = (val != 0);
    else if (key == "clear_on_empty_4code") cfg.clear_on_empty_4code = (val != 0);
    else if (key == "enable_key_sound") cfg.enable_key_sound = (val != 0);
    else if (key == "up_down_page_turn") cfg.up_down_page_turn = (val != 0);
    else if (key == "ui_language" && val >= 0 && val <= 2) cfg.ui_language = static_cast<UiLanguage>(val);
    else if (key == "underline_style" && val >= 0 && val <= 4) cfg.underline_style = static_cast<CompositionUnderlineStyle>(val);
    else if (key == "bold_underline") cfg.bold_underline = (val != 0);
    else if (key == "log_level" && val >= 0 && val <= 6) cfg.log_level = static_cast<log::Level>(val);
}

bool ParseHexU64(std::string_view sv, uint64_t& out) noexcept {
    if (sv.size() != 16) return false;
    uint64_t val = 0;
    for (const char ch : sv) {
        val <<= 4;
        if (ch >= '0' && ch <= '9') val |= gsl::narrow_cast<uint64_t>(ch - '0');
        else if (ch >= 'A' && ch <= 'F') val |= gsl::narrow_cast<uint64_t>(ch - 'A' + 10);
        else if (ch >= 'a' && ch <= 'f') val |= gsl::narrow_cast<uint64_t>(ch - 'a' + 10);
        else return false;
    }
    out = val;
    return true;
}

bool ParseDecInt(std::string_view sv, int& out) noexcept {
    if (sv.empty() || sv.size() > 9) return false;
    int val = 0;
    for (const char ch : sv) {
        if (ch < '0' || ch > '9') return false;
        val = val * 10 + (ch - '0');
    }
    out = val;
    return true;
}

} // namespace

bool DictManagerModel::CreateBackupBundle(const std::filesystem::path& backup_path) const {
    if (backup_path.empty()) {
        return false;
    }
    const QiQiConfig cfg = LoadConfig();
    std::string body = SerializeConfigSection(cfg);

    body += "\n[user_dict]\n";
    for (const auto& [packed, words] : engine_.PinnedEntries()) {
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& w : words) {
            if (!w.empty()) {
                body += code_utf8 + " ^" + WideToUtf8(w) + "\n";
            }
        }
    }
    for (const auto& [packed, words] : engine_.UserDictionaryEntries()) {
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        const auto pin_it = engine_.PinnedEntries().find(packed);
        for (const auto& w : words) {
            if (pin_it != engine_.PinnedEntries().end() &&
                std::find(pin_it->second.begin(), pin_it->second.end(), w) != pin_it->second.end()) {
                continue;
            }
            if (!w.empty()) {
                body += code_utf8 + " " + WideToUtf8(w) + "\n";
            }
        }
    }
    for (const auto& [packed, words] : engine_.BlockedEntries()) {
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& w : words) {
            if (!w.empty()) {
                body += code_utf8 + " !" + WideToUtf8(w) + "\n";
            }
        }
    }

    body += "\n[phrase_freq]\n";
    for (const auto& [packed, words] : engine_.PromotedPhrases()) {
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& w : words) {
            if (!w.empty()) {
                body += code_utf8 + " " + WideToUtf8(w) + "\n";
            }
        }
    }

    const uint64_t checksum = codec::ComputeFnv1a64(codec::AsByteSpan(body));
    const std::string header = std::format(
        "{}# version={}\n{}{:016X}\n",
        kBackupMagicLine,
        version::kFullVersion,
        kBackupChecksumPrefix,
        checksum);
    const std::string full = header + body;
    return WriteFileAtomically(backup_path, codec::AsByteSpan(full));
}

bool DictManagerModel::RestoreBackupBundle(
    const std::filesystem::path& backup_path,
    BackupConflictMode mode,
    BackupRestoreSummary& out_summary) {
    out_summary = {};
    std::string raw;
    if (!ReadFileToUtf8String(backup_path, raw)) {
        return false;
    }
    std::string_view sv = StripUtf8Bom(raw);
    if (!sv.starts_with(kBackupMagicLine)) {
        return false;
    }
    sv.remove_prefix(kBackupMagicLine.size());
    // Skip optional # version=... line
    if (sv.starts_with("# version=")) {
        const size_t nl = sv.find('\n');
        if (nl == std::string_view::npos) return false;
        sv.remove_prefix(nl + 1);
    }
    if (!sv.starts_with(kBackupChecksumPrefix)) {
        return false;
    }
    sv.remove_prefix(kBackupChecksumPrefix.size());
    const size_t checksum_nl = sv.find('\n');
    if (checksum_nl == std::string_view::npos || checksum_nl < 16) {
        return false;
    }
    uint64_t expected_checksum = 0;
    if (!ParseHexU64(sv.substr(0, 16), expected_checksum)) {
        return false;
    }
    const std::string_view body = sv.substr(checksum_nl + 1);
    if (codec::ComputeFnv1a64(codec::AsByteSpan(body)) != expected_checksum) {
        QIQI_LOG_CAT_WARN(log::Keyword::Dictionary, "RestoreBackupBundle rejected: checksum mismatch");
        return false;
    }

    QiQiConfig restored_cfg = LoadConfig();
    bool has_config_section = false;
    std::string user_dict_lines;
    std::string phrase_freq_lines;
    enum class Section { None, Config, UserDict, PhraseFreq } current = Section::None;

    std::string_view rem = body;
    while (!rem.empty()) {
        const size_t nl = rem.find('\n');
        std::string_view line = (nl == std::string_view::npos) ? rem : rem.substr(0, nl);
        rem.remove_prefix(nl == std::string_view::npos ? rem.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (line == "[config]") {
            current = Section::Config;
            has_config_section = true;
            continue;
        }
        if (line == "[user_dict]") {
            current = Section::UserDict;
            continue;
        }
        if (line == "[phrase_freq]") {
            current = Section::PhraseFreq;
            continue;
        }
        if (current == Section::Config) {
            const size_t eq = line.find('=');
            if (eq != std::string_view::npos) {
                const std::string_view k = line.substr(0, eq);
                const std::string_view v = line.substr(eq + 1);
                int parsed_val = 0;
                if (ParseDecInt(v, parsed_val)) {
                    ApplyConfigKeyValue(restored_cfg, k, parsed_val);
                }
            }
        } else if (current == Section::UserDict) {
            user_dict_lines.append(line);
            user_dict_lines.push_back('\n');
        } else if (current == Section::PhraseFreq) {
            phrase_freq_lines.append(line);
            phrase_freq_lines.push_back('\n');
        }
    }

    if (has_config_section) {
        out_summary.config_restored = SaveConfig(restored_cfg);
    }

    std::map<uint32_t, std::vector<std::wstring>> backup_user_entries;
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(user_dict_lines), backup_user_entries, true);

    std::map<uint32_t, std::vector<std::wstring>> backup_freq_entries;
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(phrase_freq_lines), backup_freq_entries, false);

    if (mode == BackupConflictMode::Replace) {
        if (!user_dict_path_.empty()) {
            std::error_code rm_ec;
            std::filesystem::remove(user_dict_path_, rm_ec);
            std::filesystem::path qdict_path = user_dict_path_;
            qdict_path.replace_extension(L".qdict");
            std::filesystem::remove(qdict_path, rm_ec);
            WubiMappedDict::InvalidateProcessCacheForPath(user_dict_path_);
            std::filesystem::remove_all(GetSyncChannelDirectoryForFile(user_dict_path_), rm_ec);
        }
        std::ignore = engine_.ClearPhraseFrequencies(phrase_freq_path_);
        ReloadAll();
    }

    for (const auto& [packed, words] : backup_user_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (const auto& w : words) {
            if (w.starts_with(L'^') || w.starts_with(L'!')) {
                continue;
            }
            if (engine_.AddUserEntry(code, w, user_dict_path_)) {
                ++out_summary.user_entries_applied;
            }
        }
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            if (it->starts_with(L'^')) {
                if (engine_.PinUserEntry(code, std::wstring_view(*it).substr(1), user_dict_path_)) {
                    ++out_summary.user_entries_applied;
                }
            } else if (it->starts_with(L'!')) {
                if (engine_.BlockSystemEntry(code, std::wstring_view(*it).substr(1), user_dict_path_)) {
                    ++out_summary.user_entries_applied;
                }
            }
        }
    }

    for (const auto& [packed, words] : backup_freq_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            if (engine_.PromoteUserPhraseFrequency(code, *it, phrase_freq_path_)) {
                ++out_summary.phrase_freq_applied;
            }
        }
    }

    RebuildRecords();
    return true;
}

std::wstring FormatRecordsForClipboard(std::span<const DictRecord> records) {
    std::wstring out;
    for (const auto& rec : records) {
        out += std::format(L"{}\t{}\r\n", rec.code, rec.word);
    }
    return out;
}

} // namespace qiqi::dict_manager
