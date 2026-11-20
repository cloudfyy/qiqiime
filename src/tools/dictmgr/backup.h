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

namespace qiqi::dict_manager::detail {

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

HRESULT WriteBackupArchiveFileResult(
    const std::filesystem::path& path, const BackupArchivePayload& payload);
HRESULT ReadBackupArchiveFileResult(
    const std::filesystem::path& path,
    const QiQiConfig& current_config,
    const std::filesystem::path& restored_custom_primary_path,
    BackupArchivePlan& plan,
    BackupArchiveReadStage& stage);

} // namespace qiqi::dict_manager::detail
