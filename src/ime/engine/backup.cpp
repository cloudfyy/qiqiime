// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/backup.h"

#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "common/wubicode.h"
#include "common/zip.h"
#include "ime/engine/dict.h"
#include "ime/engine/fullcode.h"
#include "ime/engine/pinyin.h"

#include <gsl/gsl>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace qiqi {
namespace {

std::string SerializeUserDictionaryBackup(const WubiEngine& engine) {
    std::string output;
    const auto append_entry =
        [&output](uint32_t packed, std::wstring_view word, wchar_t marker = 0) {
            output += WideToUtf8(UnpackWubiCode(packed));
            output.push_back(' ');
            if (marker != 0) {
                output += WideToUtf8(std::wstring_view(&marker, 1));
            }
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
                std::find(pinned->second.begin(), pinned->second.end(), word) !=
                    pinned->second.end()) {
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

std::filesystem::path RestoredCustomPrimaryPath(
    const std::filesystem::path& user_dict_path) {
    std::filesystem::path parent = user_dict_path.parent_path();
    if (parent.empty()) parent = GetDefaultUserDictPath().parent_path();
    return parent / L"qiqiime-restored-primary.txt";
}

std::filesystem::path ValidationPath(
    const std::filesystem::path& target, bool binary) {
    static std::atomic<uint64_t> sequence{};
    std::filesystem::path path = target;
    if (!path.has_filename()) {
        path = std::filesystem::path(L"qiqiime-primary");
    }
    path.replace_extension();
    path += L"-validate-" +
        std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64()) + L"-" +
        std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
    path += binary ? L".qdict" : L".txt";
    return path;
}

HRESULT ValidatePrimaryDictionaryBackup(
    const BackupCustomPrimaryDictionary& custom,
    const std::filesystem::path& target) {
    const auto validation_path = ValidationPath(target, custom.binary);
    HRESULT result = WriteFileAtomicallyResult(
        validation_path, codec::AsByteSpan(custom.data));
    if (FAILED(result)) return result;
    bool cleanup_needed = true;
    const auto remove_validation_file =
        [&validation_path](std::string_view context) noexcept {
            std::error_code error;
            std::filesystem::remove(validation_path, error);
            if (error) {
                QIQI_LOG_WARN(
                    "Primary backup validation cleanup {} failed (error={})",
                    context, error.value());
            }
            return error;
        };
    const auto cleanup = gsl::finally([&]() noexcept {
        if (cleanup_needed) {
            std::ignore = remove_validation_file("on failure");
        }
    });
    try {
        {
            const PrimaryDictValidationResult validation =
                WubiMappedDict::ValidateAndLoadPrimaryDictionary(
                    validation_path);
            if (!validation || validation.diagnostics.invalid_lines != 0) {
                result = HRESULT_FROM_WIN32(
                    validation.win32_error != ERROR_SUCCESS
                        ? validation.win32_error
                        : ERROR_INVALID_DATA);
            }
        }
        const std::error_code remove_error =
            remove_validation_file("after validation");
        if (remove_error) {
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(remove_error.value()));
        }
        cleanup_needed = false;
        return result;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "Complete backup primary dictionary validation ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR(
            "Complete backup primary dictionary validation failed unexpectedly");
        return E_UNEXPECTED;
    }
}

} // namespace

CompleteBackup::CompleteBackup(
    const std::filesystem::path& module_dir,
    const std::filesystem::path& user_dict_path,
    const std::filesystem::path& phrase_freq_path)
    : module_dir_(module_dir) {
    const QiQiConfig config = LoadConfig();
    publish_user_dict_directory_ = user_dict_path.empty();
    user_dict_path_ = !user_dict_path.empty()
        ? user_dict_path
        : (config.user_dict_path.empty()
            ? GetDefaultUserDictPath()
            : ResolveConfiguredDictionaryPath(
                module_dir_, config.user_dict_path));
    phrase_freq_path_ = !phrase_freq_path.empty()
        ? phrase_freq_path
        : GetDefaultPhraseFreqPath();
}

HRESULT CompleteBackup::ReloadStateResult() {
    engine_ = WubiEngine{};
    active_primary_path_.clear();
    HRESULT result = S_OK;
    const auto check = [&result](HRESULT error) {
        if (FAILED(error) && SUCCEEDED(result)) result = error;
    };
    const auto exists = [&check](const std::filesystem::path& path) {
        std::error_code error;
        const bool present = std::filesystem::exists(path, error);
        if (error) {
            check(HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(error.value())));
        }
        return present && !error;
    };
    QiQiConfig config = LoadConfig();
    config.full_code_practice = false;
    if (publish_user_dict_directory_) {
        user_dict_path_ = config.user_dict_path.empty()
            ? GetDefaultUserDictPath()
            : ResolveConfiguredDictionaryPath(
                module_dir_, config.user_dict_path);
    }
    check(engine_.ApplyConfig(config));
    engine_.SetDynamicPhraseFreq(true);
    const PrimaryDictLoadSummary primary =
        engine_.LoadConfiguredPrimaryDictionaries(
            config, module_dir_, false);
    active_primary_ = primary.used_fallback
        ? PrimaryDict::Jidian
        : config.primary_dict;
    active_primary_path_ = primary.primary_dict
        ? primary.primary_dict->SourcePath()
        : std::filesystem::path{};
    check(primary.hr);

    engine_.SetUserDictPath(user_dict_path_);
    if (!user_dict_path_.empty()) {
        std::string raw;
        const bool present = exists(user_dict_path_);
        const HRESULT read_result = present
            ? ReadFileToUtf8StringResult(user_dict_path_, raw)
            : S_OK;
        check(read_result);
        if (present && SUCCEEDED(read_result)) {
            std::map<uint32_t, std::vector<std::wstring>> entries;
            WubiMappedDict::ParseTextDictEntries(
                Utf8ToWide(StripUtf8Bom(raw)), entries, true);
            for (const auto& [packed, words] : entries) {
                const std::wstring code = UnpackWubiCode(packed);
                for (const auto& word : words) {
                    if (word.starts_with(L'^')) {
                        engine_.PinEntry(code, word.substr(1));
                    } else if (word.starts_with(L'!')) {
                        engine_.BlockEntry(code, word.substr(1));
                    } else {
                        engine_.AddEntry(code, word);
                    }
                }
            }
        }
        check(publish_user_dict_directory_
            ? engine_.InitializeUserDictionarySharing()
            : engine_.SynchronizeUserDictionary(
                sync::InputBoundary::NewInput));
    }
    engine_.SetPhraseFreqPath(phrase_freq_path_);
    if (!phrase_freq_path_.empty()) {
        if (exists(phrase_freq_path_) &&
            !engine_.LoadPhraseFrequencies(phrase_freq_path_)) {
            check(E_FAIL);
        }
        check(engine_.SynchronizePhraseFrequencies(
            sync::InputBoundary::NewInput));
    }
    return result;
}

HRESULT CompleteBackup::CreateFileResult(
    const std::filesystem::path& path) {
    try {
        if (path.empty()) return E_INVALIDARG;
        const HRESULT reload = ReloadStateResult();
        if (FAILED(reload)) return reload;
        BackupArchivePayload payload;
        payload.config = LoadConfig();
        payload.user_dictionary =
            SerializeUserDictionaryBackup(engine_);
        payload.phrase_order = SerializePhraseOrderBackup(engine_);
        if (payload.config.primary_dict == PrimaryDict::Custom) {
            if (active_primary_ != PrimaryDict::Custom ||
                active_primary_path_.empty()) {
                QIQI_LOG_WARN(
                    "ZIP backup rejected because the custom primary "
                    "dictionary is unavailable");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            std::string custom_dictionary;
            const HRESULT read = ReadFileToUtf8StringResult(
                active_primary_path_,
                custom_dictionary,
                kMaxZipEntryBytes);
            if (FAILED(read)) return read;
            payload.custom_primary_dictionary =
                BackupCustomPrimaryDictionary{
                    .binary =
                        _wcsicmp(
                            active_primary_path_.extension().c_str(),
                            L".qdict") == 0,
                    .data = std::move(custom_dictionary)};
            const HRESULT validation = ValidatePrimaryDictionaryBackup(
                *payload.custom_primary_dictionary,
                active_primary_path_);
            if (FAILED(validation)) return validation;
        }
        return WriteBackupArchiveFileResult(path, payload);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Complete ZIP backup creation ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("Complete ZIP backup creation failed unexpectedly");
        return E_UNEXPECTED;
    }
}

BackupRestoreResult CompleteBackup::RestoreFile(
    const std::filesystem::path& path, BackupConflictMode mode) {
    BackupRestoreResult result;
    result.operations_failed = 1;
    if (mode != BackupConflictMode::Merge &&
        mode != BackupConflictMode::Replace) {
        result.error = E_INVALIDARG;
        return result;
    }
    if (user_dict_path_.empty() || phrase_freq_path_.empty() ||
        user_dict_path_ == phrase_freq_path_) {
        result.stage = BackupRestoreStage::Prepare;
        result.error = E_INVALIDARG;
        return result;
    }
    const auto custom_primary_path =
        RestoredCustomPrimaryPath(user_dict_path_);
    BackupArchivePlan plan;
    BackupArchiveReadStage read_stage =
        BackupArchiveReadStage::Read;
    const HRESULT read_result = ReadBackupArchiveFileResult(
        path, LoadConfig(), custom_primary_path, plan, read_stage);
    if (FAILED(read_result)) {
        result.stage = read_stage == BackupArchiveReadStage::Read
            ? BackupRestoreStage::Read
            : BackupRestoreStage::Validate;
        result.error = read_result;
        return result;
    }
    if (plan.config_changed && plan.config.full_code_practice) {
        FullCodeIndex index;
        if (const HRESULT opened = index.OpenBundled(); FAILED(opened)) {
            result.stage = BackupRestoreStage::Validate;
            result.error = opened;
            return result;
        }
    }
    if (plan.config_changed && plan.config.enable_z_pinyin_lookup) {
        PinyinIndex index;
        if (const HRESULT opened = index.OpenBundled(); FAILED(opened)) {
            std::ignore = log::CheckHResult(opened, "Backup restore::PinyinIndex");
            result.stage = BackupRestoreStage::Validate;
            result.error = opened;
            return result;
        }
    }
    if (plan.custom_primary_dictionary) {
        const HRESULT validation =
            ValidatePrimaryDictionaryBackup(
                *plan.custom_primary_dictionary,
                plan.restored_custom_primary_path);
        if (FAILED(validation)) {
            result.stage = BackupRestoreStage::Validate;
            result.error = validation;
            return result;
        }
    }

    engine_ = WubiEngine{};
    auto engine_config = LoadConfig();
    engine_config.full_code_practice = false;
    if (const HRESULT configured = engine_.ApplyConfig(engine_config); FAILED(configured)) {
        result.stage = BackupRestoreStage::Prepare;
        result.error = configured;
        return result;
    }
    engine_.SetDynamicPhraseFreq(true);
    engine_.SetUserDictPath(user_dict_path_);
    engine_.SetPhraseFreqPath(phrase_freq_path_);
    result.operations_failed = 0;
    result.error = S_OK;
    const auto fail = [&result](
        BackupRestoreStage stage, HRESULT error) {
        if (result.operations_failed == 0) {
            result.stage = stage;
            result.error = error;
        }
        ++result.operations_failed;
    };
    const auto refresh = [this, &result, &fail] {
        const HRESULT error = ReloadStateResult();
        if (FAILED(error)) {
            if (SUCCEEDED(result.refresh_error)) {
                result.refresh_error = error;
            }
            fail(BackupRestoreStage::Refresh, error);
        } else if (result.refresh_error == S_FALSE) {
            result.refresh_error = S_OK;
        }
        return SUCCEEDED(error);
    };
    const auto finish = [&result, &refresh](bool reload = true) {
        if (reload) std::ignore = refresh();
        result.status = result.operations_failed == 0
            ? BackupRestoreStatus::Success
            : (result.operations_succeeded == 0 &&
                    !result.effects_possible
                ? BackupRestoreStatus::Failed
                : BackupRestoreStatus::Partial);
        if (result.status == BackupRestoreStatus::Success) {
            result.stage = BackupRestoreStage::Complete;
        }
        return result;
    };

    if (mode == BackupConflictMode::Replace) {
        ScopedFileHandle user_channel, phrase_channel;
        const auto open_channel = [](const std::filesystem::path& source, ScopedFileHandle& folder) {
            return OpenDataItemNoReparse(GetSyncChannelDirectoryForFile(source), true, 0, folder);
        };
        HRESULT channel_status = open_channel(user_dict_path_, user_channel);
        if (SUCCEEDED(channel_status)) channel_status = open_channel(phrase_freq_path_, phrase_channel);
        if (!log::CheckHResult(channel_status, "Backup restore::OpenSyncChannel")) {
            fail(BackupRestoreStage::Prepare, channel_status);
            return finish(false);
        }

        const auto remove_file =
            [&result, &fail](const std::filesystem::path& file) {
                if (::DeleteFileW(file.c_str())) {
                    ++result.operations_succeeded;
                    return true;
                }
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND ||
                    error == ERROR_PATH_NOT_FOUND) {
                    return true;
                }
                fail(
                    BackupRestoreStage::Prepare,
                    HRESULT_FROM_WIN32(error));
                return false;
            };
        auto qdict_path = user_dict_path_;
        qdict_path.replace_extension(L".qdict");
        if (!remove_file(user_dict_path_)) return finish();
        WubiMappedDict::InvalidateProcessCacheForPath(
            user_dict_path_);
        if (!remove_file(qdict_path)) return finish();
        size_t removed = 0;
        const HRESULT cleared = sync::Channel::ClearOrphanedFiles(user_dict_path_, &removed);
        result.operations_succeeded += removed;
        if (cleared != S_OK) {
            const HRESULT error = cleared == S_FALSE ? HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) : cleared;
            std::ignore = log::CheckHResult(error, "Backup restore::ClearOrphanedFiles");
            fail(BackupRestoreStage::Prepare, error);
            return finish();
        }
        result.effects_possible = true;
        const HRESULT phrases = engine_.ClearPhraseFrequenciesResult(phrase_freq_path_);
        if (FAILED(phrases)) {
            fail(BackupRestoreStage::Prepare, phrases);
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
        for (const auto& word : words) {
            if (word.starts_with(L'^') ||
                word.starts_with(L'!')) {
                continue;
            }
            result.effects_possible = true;
            user_operation(engine_.AddUserEntryResult(
                code, word, user_dict_path_));
        }
        for (auto iterator = words.rbegin();
             iterator != words.rend(); ++iterator) {
            if (iterator->starts_with(L'^')) {
                result.effects_possible = true;
                user_operation(engine_.PinUserEntryResult(
                    code,
                    std::wstring_view(*iterator).substr(1),
                    user_dict_path_));
            } else if (iterator->starts_with(L'!')) {
                result.effects_possible = true;
                user_operation(engine_.BlockSystemEntryResult(
                    code,
                    std::wstring_view(*iterator).substr(1),
                    user_dict_path_));
            }
        }
    }

    for (const auto& [packed, words] : plan.phrase_entries) {
        const std::wstring code = UnpackWubiCode(packed);
        for (auto iterator = words.rbegin();
             iterator != words.rend(); ++iterator) {
            result.effects_possible = true;
            const HRESULT error =
                engine_.PromoteUserPhraseFrequencyResult(
                    code, *iterator, phrase_freq_path_);
            if (SUCCEEDED(error)) {
                ++result.summary.phrase_freq_applied;
                ++result.operations_succeeded;
            } else {
                ++result.summary.phrase_freq_failed;
                fail(BackupRestoreStage::PhraseFrequency, error);
            }
        }
    }

    if (result.operations_failed == 0) {
        if (plan.has_custom_primary_dictionary) {
            const HRESULT write_custom =
                WriteFileAtomicallyResult(
                    plan.restored_custom_primary_path,
                    codec::AsByteSpan(
                        plan.custom_primary_dictionary->data));
            if (FAILED(write_custom)) {
                fail(
                    BackupRestoreStage::CustomDictionary,
                    write_custom);
            } else {
                WubiMappedDict::InvalidateProcessCacheForPath(
                    plan.restored_custom_primary_path);
                result.summary.custom_primary_dict_restored = true;
                result.effects_possible = true;
                ++result.operations_succeeded;
            }
        }
        if (result.operations_failed == 0 &&
            plan.config_changed) {
            const HRESULT config_result =
                SaveConfigResult(plan.config);
            if (SUCCEEDED(config_result)) {
                result.summary.config_restored = true;
                ++result.operations_succeeded;
            } else {
                fail(
                    BackupRestoreStage::Config,
                    config_result);
            }
        }
    }
    return finish();
}

} // namespace qiqi
