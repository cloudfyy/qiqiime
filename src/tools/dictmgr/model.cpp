// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/model.h"
#include "tools/dictmgr/backup.h"
#include "common/dictcodec.h"
#include "common/config.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "common/zip.h"

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
    publish_user_dict_directory_ = user_dict_path.empty();
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
    if (publish_user_dict_directory_) {
        user_dict_path_ = cfg.user_dict_path.empty() ? GetDefaultUserDictPath() :
            ResolveConfiguredDictionaryPath(module_dir_, cfg.user_dict_path);
    }
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
        check(publish_user_dict_directory_ ? engine_.InitializeUserDictionarySharing() :
            engine_.SynchronizeUserDictionary(sync::InputBoundary::NewInput));
    }

    engine_.SetPhraseFreqPath(phrase_freq_path_);
    if (!phrase_freq_path_.empty()) {
        if (exists(phrase_freq_path_) && !engine_.LoadPhraseFrequencies(phrase_freq_path_)) check(E_FAIL);
        check(engine_.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput));
    }

    RebuildRecords();
    config_registry_sequence_ = QueryRegistryConfigSequence();
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
    return SUCCEEDED(PromotePhraseFrequencyResult(code, word));
}

HRESULT DictManagerModel::PromotePhraseFrequencyResult(std::wstring_view code, std::wstring_view word) {
    const std::wstring norm_code = NormalizeAsciiLower(code);
    const std::wstring norm_word = TrimWhitespace(word);
    const HRESULT hr = engine_.PromoteUserPhraseFrequencyResult(norm_code, norm_word, phrase_freq_path_);
    if (FAILED(hr)) return hr;
    RebuildRecords();
    return hr;
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

std::string SerializeUserDictionaryBackup(const WubiEngine& engine) {
    std::string output;
    const auto append_entry = [&output](uint32_t packed, std::wstring_view word, wchar_t marker = 0) {
        output += WideToUtf8(UnpackWubiCode(packed));
        output.push_back(' ');
        if (marker != 0) output += WideToUtf8(std::wstring_view(&marker, 1));
        output += WideToUtf8(word);
        output.push_back('\n');
    };
    for (const auto& [packed, words] : engine.PinnedEntries()) {
        for (const auto& word : words) {
            if (!word.empty()) append_entry(packed, word, L'^');
        }
    }
    for (const auto& [packed, words] : engine.UserDictionaryEntries()) {
        const auto pinned = engine.PinnedEntries().find(packed);
        for (const auto& word : words) {
            if (pinned != engine.PinnedEntries().end() &&
                std::find(pinned->second.begin(), pinned->second.end(), word) != pinned->second.end()) {
                continue;
            }
            if (!word.empty()) append_entry(packed, word);
        }
    }
    for (const auto& [packed, words] : engine.BlockedEntries()) {
        for (const auto& word : words) {
            if (!word.empty()) append_entry(packed, word, L'!');
        }
    }
    return output;
}

std::string SerializePhraseOrderBackup(const WubiEngine& engine) {
    std::string output;
    for (const auto& [packed, words] : engine.PromotedPhrases()) {
        const std::string code = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& word : words) {
            if (!word.empty()) {
                output += code + " " + WideToUtf8(word) + "\n";
            }
        }
    }
    return output;
}

std::filesystem::path GetRestoredCustomPrimaryPath(
    const std::filesystem::path& user_dict_path) {
    std::filesystem::path parent = user_dict_path.parent_path();
    if (parent.empty()) {
        parent = GetDefaultUserDictPath().parent_path();
    }
    return parent / L"qiqiime-restored-primary.txt";
}

} // namespace

HRESULT DictManagerModel::CreateBackupBundleResult(
    const std::filesystem::path& backup_path) const {
    try {
        if (backup_path.empty()) return E_INVALIDARG;
        detail::BackupArchivePayload payload;
        payload.config = LoadConfig();
        payload.user_dictionary = SerializeUserDictionaryBackup(engine_);
        payload.phrase_order = SerializePhraseOrderBackup(engine_);
        if (payload.config.primary_dict == PrimaryDict::Custom) {
            if (active_primary_dict_ != PrimaryDict::Custom ||
                active_primary_dict_path_.empty()) {
                QIQI_LOG_WARN(
                    "ZIP backup rejected because the selected custom primary dictionary is unavailable");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            std::string custom_dictionary;
            const HRESULT read_result = ReadFileToUtf8StringResult(
                active_primary_dict_path_, custom_dictionary, kMaxZipEntryBytes);
            if (FAILED(read_result)) return read_result;
            payload.custom_primary_dictionary =
                detail::BackupCustomPrimaryDictionary{
                    .binary = _wcsicmp(
                        active_primary_dict_path_.extension().c_str(),
                        L".qdict") == 0,
                    .data = std::move(custom_dictionary)};
        }
        return detail::WriteBackupArchiveFileResult(backup_path, payload);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("CreateBackupBundleResult ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("CreateBackupBundleResult failed unexpectedly");
        return E_UNEXPECTED;
    }
}

bool DictManagerModel::CreateBackupBundle(const std::filesystem::path& backup_path) const {
    return SUCCEEDED(CreateBackupBundleResult(backup_path));
}

BackupRestoreResult DictManagerModel::RestoreBackupBundle(
    const std::filesystem::path& backup_path,
    BackupConflictMode mode) {
    BackupRestoreResult result;
    result.operations_failed = 1;
    if (mode != BackupConflictMode::Merge &&
        mode != BackupConflictMode::Replace) {
        result.error = E_INVALIDARG;
        return result;
    }
    const auto custom_primary_path =
        GetRestoredCustomPrimaryPath(user_dict_path_);
    detail::BackupArchivePlan plan;
    detail::BackupArchiveReadStage read_stage =
        detail::BackupArchiveReadStage::Read;
    const HRESULT read_result = detail::ReadBackupArchiveFileResult(
        backup_path, LoadConfig(), custom_primary_path, plan, read_stage);
    if (FAILED(read_result)) {
        result.stage = read_stage == detail::BackupArchiveReadStage::Read
            ? BackupRestoreStage::Read
            : BackupRestoreStage::Validate;
        result.error = read_result;
        return result;
    }

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

    const auto user_operation = [&result, &fail](HRESULT error) {
        if (SUCCEEDED(error)) {
            ++result.summary.user_entries_applied;
            ++result.operations_succeeded;
        } else {
            ++result.summary.user_entries_failed;
            fail(BackupRestoreStage::UserDictionary, error);
        }
    };
    for (const auto& [packed, words] : plan.user_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (const auto& w : words) {
            if (w.starts_with(L'^') || w.starts_with(L'!')) {
                continue;
            }
            result.effects_possible = true;
            user_operation(engine_.AddUserEntryResult(code, w, user_dict_path_));
        }
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            if (it->starts_with(L'^')) {
                result.effects_possible = true;
                user_operation(engine_.PinUserEntryResult(code, std::wstring_view(*it).substr(1), user_dict_path_));
            } else if (it->starts_with(L'!')) {
                result.effects_possible = true;
                user_operation(engine_.BlockSystemEntryResult(code, std::wstring_view(*it).substr(1), user_dict_path_));
            }
        }
    }

    for (const auto& [packed, words] : plan.phrase_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (auto it = words.rbegin(); it != words.rend(); ++it) {
            result.effects_possible = true;
            const HRESULT error = engine_.PromoteUserPhraseFrequencyResult(code, *it, phrase_freq_path_);
            if (SUCCEEDED(error)) {
                ++result.summary.phrase_freq_applied;
                ++result.operations_succeeded;
            } else {
                ++result.summary.phrase_freq_failed;
                fail(BackupRestoreStage::PhraseFrequency, error);
            }
        }
    }

    // Do not install configuration or a custom primary dictionary after dictionary failures.
    if (result.operations_failed == 0) {
        if (plan.has_custom_primary_dictionary) {
            const HRESULT write_custom_result = WriteFileAtomicallyResult(
                plan.restored_custom_primary_path,
                codec::AsByteSpan(
                    plan.custom_primary_dictionary->data));
            if (FAILED(write_custom_result)) {
                fail(
                    BackupRestoreStage::CustomDictionary,
                    write_custom_result);
            } else {
                WubiMappedDict::InvalidateProcessCacheForPath(
                    plan.restored_custom_primary_path);
                result.summary.custom_primary_dict_restored = true;
                result.effects_possible = true;
                ++result.operations_succeeded;
            }
        }
        if (result.operations_failed == 0 && plan.config_changed) {
            const HRESULT config_result =
                SaveConfigToRegistryResult(plan.config);
            if (SUCCEEDED(config_result)) {
                result.summary.config_restored = true;
                ++result.operations_succeeded;
            } else {
                fail(BackupRestoreStage::Config, config_result);
            }
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
