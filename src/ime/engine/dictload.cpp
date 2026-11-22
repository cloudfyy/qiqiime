// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/engine.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <algorithm>
#include <utility>

namespace qiqi {

bool WubiEngine::LoadDictionaryFromBinaryBuffer(std::vector<uint8_t> qdict_bytes) {
    auto mapped = WubiMappedDict::LoadFromBinaryBuffer(std::move(qdict_bytes));
    if (!mapped) {
        return false;
    }
    mapped_dicts_.push_back(std::move(mapped));
    return true;
}

bool WubiEngine::AttachMappedDictionary(std::shared_ptr<const WubiMappedDict> mapped) {
    if (!mapped || !mapped->IsValid()) {
        return false;
    }
    if (std::find(mapped_dicts_.begin(), mapped_dicts_.end(), mapped) == mapped_dicts_.end()) {
        QIQI_LOG_INFO("Attached external dictionary '{}' ({} codes, {} words, mapped={})",
                      WideToUtf8(mapped->SourcePath().wstring()),
                      mapped->CodeCount(),
                      mapped->WordCount(),
                      mapped->IsMemoryMapped());
        mapped_dicts_.push_back(std::move(mapped));
    }
    return true;
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    auto mapped = WubiMappedDict::LoadFromTextOrCache(file_path);
    if (!mapped) {
        QIQI_LOG_WARN("Failed to load or map external dictionary file: {}",
                      WideToUtf8(file_path.wstring()));
        return false;
    }
    return AttachMappedDictionary(std::move(mapped));
}

PrimaryDictLoadSummary WubiEngine::LoadConfiguredPrimaryDictionaries(
    const QiQiConfig& config,
    const std::filesystem::path& module_dir,
    bool retain_existing_on_custom_failure) {
    PrimaryDictLoadSummary summary{};
    const auto prev_primary = primary_mapped_dict_;
    const auto prev_extra = extra_mapped_dict_;

    PrimaryDict effective_primary = config.primary_dict;
    if (config.primary_dict == PrimaryDict::Custom) {
        if (config.primary_dict_path.empty()) {
            summary.used_fallback = true;
            effective_primary = PrimaryDict::Jidian;
            QIQI_LOG_CAT_WARN(
                log::Keyword::Dictionary,
                "PrimaryDict::Custom configured with empty PrimaryDictPath; falling back to bundled Jidian primary dictionary");
        } else {
            const std::filesystem::path custom_path =
                ResolveConfiguredDictionaryPath(module_dir, config.primary_dict_path);
            const PrimaryDictValidationResult validation =
                WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_path);
            if (validation) {
                ClearExternalDictionaries();
                if (AttachMappedDictionary(validation.dictionary)) {
                    primary_mapped_dict_ = validation.dictionary;
                    summary.primary_dict = validation.dictionary;
                    summary.loaded_count = 1;
                    return summary;
                }
                summary.hr = E_FAIL;
            } else {
                const DWORD err = validation.win32_error != ERROR_SUCCESS
                    ? validation.win32_error
                    : ERROR_INVALID_DATA;
                summary.hr = HRESULT_FROM_WIN32(err);
            }

            summary.used_fallback = true;
            if (retain_existing_on_custom_failure && prev_primary) {
                ClearExternalDictionaries();
                if (AttachMappedDictionary(prev_primary)) {
                    primary_mapped_dict_ = prev_primary;
                    summary.primary_dict = prev_primary;
                    summary.loaded_count = 1;
                }
                if (prev_extra && AttachMappedDictionary(prev_extra)) {
                    extra_mapped_dict_ = prev_extra;
                    summary.extra_dict = prev_extra;
                    ++summary.loaded_count;
                }
                summary.retained_previous = true;
                QIQI_LOG_CAT_WARN(
                    log::Keyword::Dictionary,
                    "Custom primary dictionary '{}' unavailable (hr=0x{:08X}); retained previously active primary dictionary",
                    WideToUtf8(custom_path.wstring()),
                    gsl::narrow_cast<unsigned int>(summary.hr));
                return summary;
            }

            QIQI_LOG_CAT_WARN(
                log::Keyword::Dictionary,
                "Custom primary dictionary '{}' unavailable (hr=0x{:08X}); falling back to bundled Jidian primary dictionary",
                WideToUtf8(custom_path.wstring()),
                gsl::narrow_cast<unsigned int>(summary.hr));
            effective_primary = PrimaryDict::Jidian;
        }
    }

    ClearExternalDictionaries();
    if (module_dir.empty()) {
        return summary;
    }

    for (const auto& source : GetBundledDictionaryLoadPlan(effective_primary, config.enable_jidian_extra)) {
        const std::filesystem::path path = ResolveBundledDataFile(module_dir, source.file_name);
        std::error_code ec;
        const bool present = std::filesystem::exists(path, ec);
        if (ec) {
            const HRESULT stat_hr = HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value()));
            if (SUCCEEDED(summary.hr)) {
                summary.hr = stat_hr;
            }
            continue;
        }
        if (!present) {
            continue;
        }
        auto dict = WubiMappedDict::LoadFromTextOrCache(path);
        if (!dict || !AttachMappedDictionary(dict)) {
            if (SUCCEEDED(summary.hr)) {
                summary.hr = E_FAIL;
            }
            continue;
        }
        if (source.role == DictionarySourceRole::Primary) {
            primary_mapped_dict_ = dict;
            summary.primary_dict = std::move(dict);
        } else {
            extra_mapped_dict_ = dict;
            summary.extra_dict = std::move(dict);
        }
        ++summary.loaded_count;
    }
    return summary;
}

bool WubiEngine::HasModifiedMappedDictionary() const noexcept {
    for (const auto& dict : mapped_dicts_) {
        if (dict && dict->IsSourceModified()) {
            return true;
        }
    }
    return false;
}

} // namespace qiqi
