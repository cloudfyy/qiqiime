// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi {

inline constexpr std::string_view kBackupArchiveFormat = "qiqiime-backup";
inline constexpr int kBackupArchiveVersion = 1;
inline constexpr std::string_view kBackupManifestEntry = "manifest.json";
inline constexpr std::string_view kBackupConfigEntry = "config.json";
inline constexpr std::string_view kBackupUserDictionaryEntry = "user-dictionary.txt";
inline constexpr std::string_view kBackupPhraseOrderEntry = "phrase-order.txt";
inline constexpr std::string_view kBackupCustomPrimaryTextEntry =
    "custom-primary.txt";
inline constexpr std::string_view kBackupCustomPrimaryBinaryEntry =
    "custom-primary.qdict";

struct BackupCustomPrimaryDictionary {
    bool binary = false;
    std::string data;
};

struct BackupArchivePayload {
    QiQiConfig config;
    std::string user_dictionary;
    std::string phrase_order;
    std::optional<BackupCustomPrimaryDictionary> custom_primary_dictionary;
};

struct BackupArchivePlan {
    QiQiConfig config;
    bool config_changed = false;
    bool has_custom_primary_dictionary = false;
    std::filesystem::path restored_custom_primary_path;
    std::optional<BackupCustomPrimaryDictionary> custom_primary_dictionary;
    std::map<uint32_t, std::vector<std::wstring>> user_entries;
    std::map<uint32_t, std::vector<std::wstring>> phrase_entries;
};

enum class BackupArchiveReadStage : uint8_t { Read, Validate };

enum class BackupConflictMode : uint8_t {
    Merge = 0,
    Replace = 1,
};

struct BackupRestoreSummary {
    bool config_restored = false;
    bool custom_primary_dict_restored = false;
    size_t user_entries_applied = 0;
    size_t phrase_freq_applied = 0;
    size_t user_entries_failed = 0;
    size_t phrase_freq_failed = 0;
};

enum class BackupRestoreStatus : uint8_t { Failed, Partial, Success };
enum class BackupRestoreStage : uint8_t {
    Read, Validate, Prepare, UserDictionary, PhraseFrequency,
    CustomDictionary, Config, Refresh, Complete
};

struct BackupRestoreResult {
    BackupRestoreStatus status = BackupRestoreStatus::Failed;
    BackupRestoreStage stage = BackupRestoreStage::Validate;
    HRESULT error = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    HRESULT refresh_error = S_FALSE;
    BackupRestoreSummary summary;
    size_t operations_succeeded = 0;
    size_t operations_failed = 0;
    bool effects_possible = false;
};

std::optional<BackupConflictMode> PromptBackupRestoreConflictMode(
    HWND owner, UiLanguage language) noexcept;
std::wstring FormatBackupRestoreMessage(
    const BackupRestoreResult& result, UiLanguage language);

HRESULT WriteBackupArchiveFileResult(
    const std::filesystem::path& path, const BackupArchivePayload& payload);
HRESULT ReadBackupArchiveFileResult(
    const std::filesystem::path& path,
    const QiQiConfig& current_config,
    const std::filesystem::path& restored_custom_primary_path,
    BackupArchivePlan& plan,
    BackupArchiveReadStage& stage);

} // namespace qiqi
