// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/model.h"
#include "common/backup.h"
#include "common/dictcodec.h"
#include "common/config.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "ime/engine/backup.h"

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

HRESULT DictManagerModel::CreateBackupBundleResult(
    const std::filesystem::path& backup_path) const {
    try {
        CompleteBackup backup(
            module_dir_, user_dict_path_, phrase_freq_path_);
        return backup.CreateFileResult(backup_path);
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
    try {
        CompleteBackup backup(
            module_dir_, user_dict_path_, phrase_freq_path_);
        auto result = backup.RestoreFile(backup_path, mode);
        if (result.stage != BackupRestoreStage::Read &&
            result.stage != BackupRestoreStage::Validate) {
            const bool refresh_already_failed =
                FAILED(result.refresh_error);
            const HRESULT refresh = ReloadAllResult();
            if (FAILED(refresh)) {
                if (result.operations_failed == 0) {
                    result.stage = BackupRestoreStage::Refresh;
                    result.error = refresh;
                }
                if (!refresh_already_failed) {
                    result.refresh_error = refresh;
                    ++result.operations_failed;
                }
                result.status = result.operations_succeeded == 0 &&
                        !result.effects_possible
                    ? BackupRestoreStatus::Failed
                    : BackupRestoreStatus::Partial;
            } else if (result.operations_failed == 0) {
                result.refresh_error = S_OK;
                result.stage = BackupRestoreStage::Complete;
                result.status = BackupRestoreStatus::Success;
            }
        }
        return result;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("RestoreBackupBundle ran out of memory");
        BackupRestoreResult result;
        result.error = E_OUTOFMEMORY;
        return result;
    } catch (...) {
        QIQI_LOG_ERROR("RestoreBackupBundle failed unexpectedly");
        BackupRestoreResult result;
        result.error = E_UNEXPECTED;
        return result;
    }
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
