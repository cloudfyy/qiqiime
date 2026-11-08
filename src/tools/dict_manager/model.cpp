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
#include <limits>
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
        user_dict_path_ = ResolveConfiguredDictionaryPath(module_dir_, cfg.user_dict_path);
    } else {
        user_dict_path_ = GetDefaultUserDictPath();
    }
    phrase_freq_path_ = !phrase_freq_path.empty() ? phrase_freq_path : GetDefaultPhraseFreqPath();
    ReloadAll();
}

void DictManagerModel::ReloadAll() {
    std::ignore = ReloadAllResult();
}

HRESULT DictManagerModel::ReloadAllResult() {
    const GUID activity_id = log::CreateActivityId();
    QIQI_LOG_ACT_START(log::Level::debug, log::Keyword::Dictionary, &activity_id, nullptr, "DictManagerModel::ReloadAll(begin)");
    engine_ = WubiEngine{};
    primary_dict_.reset();
    extra_dict_.reset();
    HRESULT result = S_OK;
    const auto check = [&result](HRESULT error) {
        if (FAILED(error) && SUCCEEDED(result)) result = error;
    };
    const auto exists = [&check](const std::filesystem::path& path) {
        std::error_code ec;
        const bool present = std::filesystem::exists(path, ec);
        if (ec) check(HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value())));
        return present && !ec;
    };

    const QiQiConfig cfg = LoadConfig();
    config_registry_sequence_ = QueryRegistryConfigSequence();
    engine_.ApplyConfig(cfg);
    engine_.SetDynamicPhraseFreq(true);

    const PrimaryDictLoadSummary primary_summary =
        engine_.LoadConfiguredPrimaryDictionaries(cfg, module_dir_, false);
    primary_dict_ = primary_summary.primary_dict;
    extra_dict_ = primary_summary.extra_dict;
    active_primary_dict_ = primary_summary.used_fallback ? PrimaryDict::Jidian : cfg.primary_dict;
    active_primary_dict_path_ = primary_dict_ ? primary_dict_->SourcePath() : std::filesystem::path{};
    check(primary_summary.hr);

    engine_.SetUserDictPath(user_dict_path_);
    if (!user_dict_path_.empty()) {
        std::string raw_utf8;
        const bool user_file_exists = exists(user_dict_path_);
        if (user_file_exists && !ReadFileToUtf8String(user_dict_path_, raw_utf8)) {
            check(E_FAIL);
        } else if (user_file_exists) {
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
        check(engine_.SynchronizeUserDictionary(sync::InputBoundary::NewInput));
    }

    engine_.SetPhraseFreqPath(phrase_freq_path_);
    if (!phrase_freq_path_.empty()) {
        if (exists(phrase_freq_path_) && !engine_.LoadPhraseFrequencies(phrase_freq_path_)) check(E_FAIL);
        check(engine_.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput));
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
    return result;
}

bool DictManagerModel::PollExternalChanges() {
    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq != 0 && reg_seq != config_registry_sequence_) {
        ReloadAll();
        return true;
    }

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

bool DictManagerModel::SelectCustomPrimaryDictionary(const std::filesystem::path& custom_path) {
    return SUCCEEDED(SelectCustomPrimaryDictionaryResult(custom_path));
}

HRESULT DictManagerModel::SelectCustomPrimaryDictionaryResult(const std::filesystem::path& custom_path) {
    const std::filesystem::path resolved =
        ResolveConfiguredDictionaryPath(module_dir_, custom_path.wstring());
    const PrimaryDictValidationResult validation =
        WubiMappedDict::ValidateAndLoadPrimaryDictionary(resolved);
    if (validation && validation.diagnostics.invalid_lines != 0) {
        QIQI_LOG_CAT_WARN(log::Keyword::Dictionary,
            "Custom dictionary selection requires partial-load confirmation");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return SelectValidatedCustomPrimaryDictionary(custom_path, validation);
}

HRESULT DictManagerModel::SelectValidatedCustomPrimaryDictionary(
    const std::filesystem::path& custom_path, const PrimaryDictValidationResult& validation) {
    if (!validation) {
        const DWORD err = validation.win32_error != ERROR_SUCCESS
            ? validation.win32_error
            : ERROR_INVALID_DATA;
        QIQI_LOG_CAT_WARN(
            log::Keyword::Dictionary,
            "DictManagerModel::SelectCustomPrimaryDictionary rejected '{}' (status={}, error={})",
            WideToUtf8(custom_path.wstring()),
            static_cast<unsigned int>(validation.status),
            err);
        return HRESULT_FROM_WIN32(err);
    }

    QiQiConfig cfg = LoadConfig();
    cfg.primary_dict = PrimaryDict::Custom;
    cfg.primary_dict_path = custom_path.wstring();
    if (!SaveConfig(cfg)) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Dictionary,
            "DictManagerModel::SelectCustomPrimaryDictionary failed to persist configuration");
        return E_FAIL;
    }
    return ReloadAllResult();
}

bool DictManagerModel::RestoreDefaultPrimaryDictionary() {
    return SUCCEEDED(RestoreDefaultPrimaryDictionaryResult());
}

HRESULT DictManagerModel::RestoreDefaultPrimaryDictionaryResult() {
    QiQiConfig cfg = LoadConfig();
    cfg.primary_dict = PrimaryDict::Jidian;
    if (!SaveConfig(cfg)) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Dictionary,
            "DictManagerModel::RestoreDefaultPrimaryDictionary failed to persist configuration");
        return E_FAIL;
    }
    return ReloadAllResult();
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
    return SUCCEEDED(AddOrUpdateUserEntryResult(code, word));
}

HRESULT DictManagerModel::AddOrUpdateUserEntryResult(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    if (PackWubiCode(norm_code) == 0 || norm_word.empty()) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "DictManager::AddOrUpdateUserEntry");
        return E_INVALIDARG;
    }
    const HRESULT hr = engine_.AddUserEntryResult(norm_code, norm_word, user_dict_path_);
    if (FAILED(hr)) return hr;
    RebuildRecords();
    return S_OK;
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
    return SUCCEEDED(ReplaceUserEntryResult(old_code, old_word, new_code, new_word));
}

HRESULT DictManagerModel::ReplaceUserEntryResult(
    std::wstring_view old_code, std::wstring_view old_word,
    std::wstring_view new_code, std::wstring_view new_word) {
    const auto code = NormalizeAsciiLower(new_code);
    const auto word = TrimWhitespace(new_word);
    if (PackWubiCode(code) == 0 || word.empty()) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "DictManager::ReplaceUserEntry");
        return E_INVALIDARG;
    }
    const HRESULT hr = engine_.ReplaceUserEntryResult(old_code, old_word, code, word, user_dict_path_);
    if (FAILED(hr)) return hr;
    RebuildRecords();
    return S_OK;
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
    ForEachConfigField([&](const auto& field) {
        out += std::format("{}={}\n", field.backup_key, field.Value(cfg));
    });
    return out;
}

bool ApplyConfigKeyValue(QiQiConfig& cfg, std::string_view key, int val) noexcept {
    if (val < 0) return false;
    bool valid = true; // Unknown numeric keys remain forward-compatible.
    ForEachConfigField([&](const auto& field) {
        if (key == field.backup_key) valid = field.Assign(cfg, gsl::narrow_cast<uint64_t>(val));
    });
    return valid;
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

struct RestorePlan {
    QiQiConfig config;
    bool has_config = false;
    std::vector<std::pair<std::string, int>> config_values;
    std::map<uint32_t, std::vector<std::wstring>> user_entries;
    std::map<uint32_t, std::vector<std::wstring>> freq_entries;
};

bool ValidateDictionaryLine(std::string_view line, bool user_dictionary) {
    if (line.find('\0') != std::string_view::npos ||
        line.size() > gsl::narrow_cast<size_t>((std::numeric_limits<int>::max)()) ||
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(),
            gsl::narrow_cast<int>(line.size()), nullptr, 0) == 0) return false;
    const std::wstring wide = Utf8ToWide(line);
    std::wstring_view rest(wide);
    const auto next = [&rest]() {
        const size_t start = rest.find_first_not_of(L" \t\r\n");
        if (start == std::wstring_view::npos) {
            rest = {};
            return std::wstring_view{};
        }
        rest.remove_prefix(start);
        const size_t end = rest.find_first_of(L" \t\r\n");
        const auto token = rest.substr(0, end);
        rest.remove_prefix(end == std::wstring_view::npos ? rest.size() : end);
        return token;
    };
    const auto is_code = [](std::wstring_view token) noexcept {
        return !token.empty() && token.size() <= 4 && std::ranges::all_of(token, [](wchar_t ch) noexcept {
            return ch >= L'a' && ch <= L'z';
        });
    };
    const auto valid_word = [user_dictionary](std::wstring_view word) noexcept {
        if (word.empty() || word == L"^" || word == L"!") return false;
        if (user_dictionary) return true;
        if (word.starts_with(L'!')) return false;
        if (word.starts_with(L'^')) word.remove_prefix(1);
        // A surrogate pair is one character, not a promotable phrase.
        return word.size() >= 2 &&
            !(word.size() == 2 && IS_HIGH_SURROGATE(word.front()) && IS_LOW_SURROGATE(word.back()));
    };
    const auto first = next();
    const auto second = next();
    if (is_code(first)) {
        if (!valid_word(second)) return false;
        for (auto word = next(); !word.empty(); word = next()) {
            if (!valid_word(word)) return false;
        }
        return true;
    }
    return !user_dictionary && valid_word(first) && is_code(second) && next().empty();
}

bool ParseRestorePlan(std::string_view body, RestorePlan& plan) {
    std::string user_lines;
    std::string freq_lines;
    enum class Section { None, Config, UserDict, PhraseFreq } current = Section::None;
    bool has_section = false;
    while (!body.empty()) {
        const size_t nl = body.find('\n');
        auto line = body.substr(0, nl);
        body.remove_prefix(nl == std::string_view::npos ? body.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line.substr(first).starts_with('#')) continue;
        if (line == "[config]") {
            current = Section::Config;
            plan.has_config = true;
        } else if (line == "[user_dict]") {
            current = Section::UserDict;
        } else if (line == "[phrase_freq]") {
            current = Section::PhraseFreq;
        } else {
            switch (current) {
            case Section::Config: {
                const size_t eq = line.find('=');
                int value = 0;
                if (eq == std::string_view::npos || eq == 0 ||
                    !ParseDecInt(line.substr(eq + 1), value) ||
                    !ApplyConfigKeyValue(plan.config, line.substr(0, eq), value)) return false;
                plan.config_values.emplace_back(line.substr(0, eq), value);
                break;
            }
            case Section::UserDict:
            case Section::PhraseFreq: {
                const bool user = current == Section::UserDict;
                if (!ValidateDictionaryLine(line, user)) return false;
                auto& lines = user ? user_lines : freq_lines;
                lines.append(line);
                lines.push_back('\n');
                break;
            }
            case Section::None:
                return false;
            }
            continue;
        }
        has_section = true;
    }
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(user_lines), plan.user_entries, true);
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(freq_lines), plan.freq_entries, false);
    return has_section;
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

BackupRestoreResult DictManagerModel::RestoreBackupBundle(
    const std::filesystem::path& backup_path,
    BackupConflictMode mode) {
    BackupRestoreResult result;
    result.operations_failed = 1;
    std::string raw;
    if (!ReadFileToUtf8String(backup_path, raw)) {
        result.stage = BackupRestoreStage::Read;
        result.error = E_FAIL; // The read helper exposes only a boolean.
        return result;
    }
    std::string_view sv = StripUtf8Bom(raw);
    if (!sv.starts_with(kBackupMagicLine)) {
        return result;
    }
    sv.remove_prefix(kBackupMagicLine.size());
    // Skip optional # version=... line
    if (sv.starts_with("# version=")) {
        const size_t nl = sv.find('\n');
        if (nl == std::string_view::npos) return result;
        sv.remove_prefix(nl + 1);
    }
    if (!sv.starts_with(kBackupChecksumPrefix)) {
        return result;
    }
    sv.remove_prefix(kBackupChecksumPrefix.size());
    const size_t checksum_nl = sv.find('\n');
    if (checksum_nl == std::string_view::npos || checksum_nl < 16) {
        return result;
    }
    uint64_t expected_checksum = 0;
    if (!ParseHexU64(sv.substr(0, 16), expected_checksum)) {
        return result;
    }
    const std::string_view body = sv.substr(checksum_nl + 1);
    if (codec::ComputeFnv1a64(codec::AsByteSpan(body)) != expected_checksum) {
        QIQI_LOG_CAT_WARN(log::Keyword::Dictionary, "RestoreBackupBundle rejected: checksum mismatch");
        return result;
    }

    RestorePlan plan;
    if ((mode != BackupConflictMode::Merge && mode != BackupConflictMode::Replace) ||
        !ParseRestorePlan(body, plan)) return result;

    result.operations_failed = 0;
    result.error = S_OK;
    const auto fail = [&result](BackupRestoreStage stage, HRESULT error) {
        if (result.operations_failed == 0) {
            result.stage = stage;
            result.error = error;
        }
        ++result.operations_failed;
    };
    const auto refresh = [this, &result, &fail]() {
        const HRESULT error = ReloadAllResult();
        if (FAILED(error)) {
            if (SUCCEEDED(result.refresh_error)) result.refresh_error = error;
            fail(BackupRestoreStage::Refresh, error);
        } else if (result.refresh_error == S_FALSE) {
            result.refresh_error = S_OK;
        }
        return SUCCEEDED(error);
    };
    const auto finish = [&result, &refresh](bool reload = true) {
        // A fresh engine discards speculative failed-write state even when its persisted view
        // cannot be fully loaded. Surface that incomplete view independently of application errors.
        if (reload) std::ignore = refresh();
        result.status = result.operations_failed == 0 ? BackupRestoreStatus::Success :
            (result.operations_succeeded == 0 && !result.effects_possible ?
                BackupRestoreStatus::Failed : BackupRestoreStatus::Partial);
        if (result.status == BackupRestoreStatus::Success) result.stage = BackupRestoreStage::Complete;
        return result;
    };

    if (user_dict_path_.empty() || phrase_freq_path_.empty() || user_dict_path_ == phrase_freq_path_) {
        fail(BackupRestoreStage::Prepare, E_INVALIDARG);
        return result;
    }

    if (mode == BackupConflictMode::Replace) {
        const auto remove_file = [&result, &fail](const std::filesystem::path& path) {
            if (::DeleteFileW(path.c_str())) {
                ++result.operations_succeeded;
                return true;
            }
            const DWORD error = ::GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;
            fail(BackupRestoreStage::Prepare, HRESULT_FROM_WIN32(error));
            return false;
        };
        auto qdict_path = user_dict_path_;
        qdict_path.replace_extension(L".qdict");
        if (!remove_file(user_dict_path_)) return finish();
        WubiMappedDict::InvalidateProcessCacheForPath(user_dict_path_);
        if (!remove_file(qdict_path)) return finish();
        std::error_code ec;
        const auto channel_directory = GetSyncChannelDirectoryForFile(user_dict_path_);
        std::vector<std::filesystem::path> channel_paths;
        if (std::filesystem::exists(channel_directory, ec)) {
            std::filesystem::recursive_directory_iterator it(channel_directory, ec);
            const std::filesystem::recursive_directory_iterator end;
            while (!ec && it != end) {
                channel_paths.push_back(it->path());
                it.increment(ec);
            }
            channel_paths.insert(channel_paths.begin(), channel_directory);
        }
        if (ec) {
            fail(BackupRestoreStage::Prepare, HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value())));
            return finish();
        }
        for (auto it = channel_paths.rbegin(); it != channel_paths.rend(); ++it) {
            const bool removed = std::filesystem::remove(*it, ec);
            if (ec) {
                fail(BackupRestoreStage::Prepare, HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value())));
                return finish();
            }
            if (removed) ++result.operations_succeeded;
        }
        if (!remove_file(GetSyncChannelDirectoryForFile(phrase_freq_path_) / L"journal.bin")) return finish();
        result.effects_possible = true;
        if (!engine_.ClearPhraseFrequencies(phrase_freq_path_)) {
            fail(BackupRestoreStage::Prepare, E_FAIL);
            return finish();
        }
        ++result.operations_succeeded;
        if (!refresh()) return finish(false);
    }

    const auto user_operation = [&result, &fail](bool succeeded) {
        if (succeeded) {
            ++result.summary.user_entries_applied;
            ++result.operations_succeeded;
        } else {
            ++result.summary.user_entries_failed;
            fail(BackupRestoreStage::UserDictionary, E_FAIL);
        }
    };
    for (const auto& [packed, words] : plan.user_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (const auto& w : words) {
            if (w.starts_with(L'^') || w.starts_with(L'!')) {
                continue;
            }
            result.effects_possible = true;
            user_operation(engine_.AddUserEntry(code, w, user_dict_path_));
        }
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            if (it->starts_with(L'^')) {
                result.effects_possible = true;
                user_operation(engine_.PinUserEntry(code, std::wstring_view(*it).substr(1), user_dict_path_));
            } else if (it->starts_with(L'!')) {
                result.effects_possible = true;
                user_operation(engine_.BlockSystemEntry(code, std::wstring_view(*it).substr(1), user_dict_path_));
            }
        }
    }

    for (const auto& [packed, words] : plan.freq_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            result.effects_possible = true;
            if (engine_.PromoteUserPhraseFrequency(code, *it, phrase_freq_path_)) {
                ++result.summary.phrase_freq_applied;
                ++result.operations_succeeded;
            } else {
                ++result.summary.phrase_freq_failed;
                fail(BackupRestoreStage::PhraseFrequency, E_FAIL);
            }
        }
    }

    // Do not install configuration describing a restore whose dictionary operations failed.
    if (result.operations_failed == 0 && plan.has_config) {
        QiQiConfig config = LoadConfig();
        for (const auto& [key, value] : plan.config_values) {
            std::ignore = ApplyConfigKeyValue(config, key, value);
        }
        if (SaveConfig(config)) {
            result.summary.config_restored = true;
            ++result.operations_succeeded;
        } else {
            fail(BackupRestoreStage::Config, E_FAIL);
        }
    }
    return finish();
}

bool DictManagerModel::RestoreBackupBundle(
    const std::filesystem::path& backup_path,
    BackupConflictMode mode,
    BackupRestoreSummary& out_summary) {
    const auto result = RestoreBackupBundle(backup_path, mode);
    out_summary = result.summary;
    return result.status == BackupRestoreStatus::Success;
}

std::wstring FormatRecordsForClipboard(std::span<const DictRecord> records) {
    std::wstring out;
    for (const auto& rec : records) {
        out += std::format(L"{}\t{}\r\n", rec.code, rec.word);
    }
    return out;
}

} // namespace qiqi::dict_manager
