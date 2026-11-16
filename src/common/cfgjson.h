// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"

namespace qiqi {

inline constexpr size_t kMaxConfigJsonBytes = 64 * 1024;
inline constexpr std::string_view kConfigJsonFormat = "qiqiime-config";

enum class ConfigJsonIssue {
    None, Syntax, TooLarge, ObjectRequired, UnknownField, DuplicateField,
    MissingField, InvalidType, OutOfRange, InvalidEnum, UnsupportedFormat,
    UnsupportedVersion, CustomDictionaryRequired, ComfortScaleRequired
};

struct ConfigJsonError {
    ConfigJsonIssue issue = ConfigJsonIssue::None;
    std::string field;
    size_t byte = 0;
};

struct ConfigJsonChange {
    // Stable ASCII field keys and ASCII-escaped JSON scalar values.
    std::string key;
    std::string before;
    std::string after;
};

std::vector<ConfigJsonChange> GetConfigJsonChanges(const QiQiConfig& before, const QiQiConfig& after);

// Outputs are unchanged on failure; only scalar fields in kConfigFields are transferable.
HRESULT SerializeConfigJson(const QiQiConfig& config, std::string& output, ConfigJsonError& error);
HRESULT ParseConfigJson(
    std::string_view input, const QiQiConfig& current, QiQiConfig& output, ConfigJsonError& error);

} // namespace qiqi
