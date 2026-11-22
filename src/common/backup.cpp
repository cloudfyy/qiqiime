// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/backup.h"

#include "common/cfgjson.h"
#include "common/i18n.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "common/wubicode.h"
#include "common/zip.h"

#include <gsl/gsl>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cwctype>
#include <exception>
#include <format>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace qiqi {
namespace {

using Json = nlohmann::ordered_json;

class InvalidBackup final : public std::exception {};

[[noreturn]] void RejectBackup(std::string_view reason) {
    QIQI_LOG_WARN("Complete ZIP backup rejected (reason={})", reason);
    throw InvalidBackup{};
}

std::vector<uint8_t> Bytes(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string Text(std::span<const uint8_t> bytes) {
    return {bytes.begin(), bytes.end()};
}

HRESULT DecodeUtf8(std::string_view input, std::wstring& output) noexcept {
    input = StripUtf8Bom(input);
    if (input.find('\0') != std::string_view::npos ||
        input.size() > gsl::narrow_cast<size_t>((std::numeric_limits<int>::max)())) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    try {
        return Utf8ToWideResult(
            input, output, Utf8ErrorPolicy::Reject);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Complete backup UTF-8 decode ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("Complete backup UTF-8 decode failed unexpectedly");
        return E_UNEXPECTED;
    }
}

bool IsCode(std::wstring_view token) noexcept {
    return !token.empty() && token.size() <= 4 &&
        std::ranges::all_of(token, [](wchar_t ch) noexcept {
            return ch >= L'a' && ch <= L'z';
        });
}

bool ValidateWord(std::wstring_view word, bool user_dictionary) noexcept {
    if (word.empty() || word == L"^" || word == L"!") return false;
    if (user_dictionary) return true;
    if (word.starts_with(L'!')) return false;
    if (word.starts_with(L'^')) word.remove_prefix(1);
    return word.size() >= 2 &&
        !(word.size() == 2 &&
          IS_HIGH_SURROGATE(word.front()) &&
          IS_LOW_SURROGATE(word.back()));
}

bool ValidateDictionaryLine(std::wstring_view line, bool user_dictionary) noexcept {
    if (std::ranges::any_of(line, [](wchar_t ch) noexcept {
            return (ch < L' ' && ch != L'\t' && ch != L'\r') ||
                ch == L'\x7f';
        })) {
        return false;
    }
    std::wstring_view rest = line;
    const auto next = [&rest]() noexcept {
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
    const auto first = next();
    const auto second = next();
    if (IsCode(first)) {
        if (!ValidateWord(second, user_dictionary)) return false;
        for (auto word = next(); !word.empty(); word = next()) {
            if (!ValidateWord(word, user_dictionary)) return false;
        }
        return true;
    }
    return !user_dictionary &&
        ValidateWord(first, false) &&
        IsCode(second) &&
        next().empty();
}

HRESULT ParseDictionary(
    std::string_view input,
    bool user_dictionary,
    std::map<uint32_t, std::vector<std::wstring>>& entries) {
    std::wstring decoded;
    const HRESULT decode_result = DecodeUtf8(input, decoded);
    if (FAILED(decode_result)) return decode_result;
    try {
        std::map<uint32_t, std::vector<std::wstring>> parsed;
        std::wstring_view remaining(decoded);
        while (!remaining.empty()) {
            const size_t newline = remaining.find(L'\n');
            std::wstring_view line = remaining.substr(0, newline);
            remaining.remove_prefix(
                newline == std::wstring_view::npos
                    ? remaining.size()
                    : newline + 1);
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            const size_t start = line.find_first_not_of(L" \t");
            if (start == std::wstring_view::npos ||
                line.substr(start).starts_with(L'#')) {
                continue;
            }
            if (!ValidateDictionaryLine(line, user_dictionary)) {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }

            std::wstring_view tokens = line;
            const auto next = [&tokens]() {
                const size_t token_start =
                    tokens.find_first_not_of(L" \t\r\n");
                if (token_start == std::wstring_view::npos) {
                    tokens = {};
                    return std::wstring_view{};
                }
                tokens.remove_prefix(token_start);
                const size_t token_end =
                    tokens.find_first_of(L" \t\r\n");
                const auto token = tokens.substr(0, token_end);
                tokens.remove_prefix(
                    token_end == std::wstring_view::npos
                        ? tokens.size()
                        : token_end);
                return token;
            };
            const auto first = next();
            const bool code_first = IsCode(first);
            const uint32_t packed = PackWubiCode(
                code_first ? first : next());
            auto& words = parsed[packed];
            if (code_first) {
                for (auto word = next(); !word.empty(); word = next()) {
                    if (!user_dictionary) {
                        if (word.starts_with(L'^')) {
                            word.remove_prefix(1);
                        } else if (word.starts_with(L'!')) {
                            continue;
                        }
                    }
                    if (std::ranges::find(words, word) == words.end()) {
                        words.emplace_back(word);
                    }
                }
            } else if (std::ranges::find(words, first) == words.end()) {
                words.emplace_back(first);
            }
        }
        entries = std::move(parsed);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Complete backup dictionary parse ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("Complete backup dictionary parse failed unexpectedly");
        return E_UNEXPECTED;
    }
}

std::string SerializeManifest(
    const std::optional<BackupCustomPrimaryDictionary>& custom) {
    Json files{
        {"config", std::string(kBackupConfigEntry)},
        {"user_dictionary", std::string(kBackupUserDictionaryEntry)},
        {"phrase_order", std::string(kBackupPhraseOrderEntry)}};
    if (custom) {
        files["custom_primary_dictionary"] =
            std::string(custom->binary
                ? kBackupCustomPrimaryBinaryEntry
                : kBackupCustomPrimaryTextEntry);
    }
    return Json{
        {"format", std::string(kBackupArchiveFormat)},
        {"version", kBackupArchiveVersion},
        {"files", std::move(files)}}.dump(2) + "\n";
}

bool ParseManifest(
    std::string_view input,
    std::optional<bool>& custom_primary_binary) {
    if (input.size() > 16 * 1024) RejectBackup("manifest-too-large");
    std::vector<std::set<std::string>> keys;
    const auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
        switch (event) {
        case Json::parse_event_t::object_start:
            if (depth > 1) RejectBackup("manifest-invalid-object");
            keys.emplace_back();
            break;
        case Json::parse_event_t::key:
            if (keys.empty() ||
                !keys.back().emplace(value.get<std::string>()).second) {
                RejectBackup("manifest-duplicate-key");
            }
            break;
        case Json::parse_event_t::object_end:
            if (!keys.empty()) keys.pop_back();
            break;
        case Json::parse_event_t::array_start:
            RejectBackup("manifest-array");
            break;
        default:
            break;
        }
        return true;
    };
    const Json manifest = Json::parse(
        input.begin(), input.end(), callback);
    if (!manifest.is_object() ||
        manifest.size() != 3 ||
        !manifest.contains("format") ||
        !manifest.contains("version") ||
        !manifest.contains("files") ||
        !manifest["format"].is_string() ||
        manifest["format"].get_ref<const Json::string_t&>() !=
            kBackupArchiveFormat ||
        !manifest["version"].is_number_integer() ||
        manifest["version"] != kBackupArchiveVersion) {
        RejectBackup("manifest-header");
    }
    const auto& files = manifest["files"];
    if (!files.is_object() || files.size() < 3 || files.size() > 4 ||
        !files.contains("config") ||
        !files.contains("user_dictionary") ||
        !files.contains("phrase_order")) {
        RejectBackup("manifest-files");
    }
    for (const auto& item : files.items()) {
        if (item.key() != "config" &&
            item.key() != "user_dictionary" &&
            item.key() != "phrase_order" &&
            item.key() != "custom_primary_dictionary") {
            RejectBackup("manifest-file-key");
        }
    }
    const auto exact_reference = [&files](std::string_view key, std::string_view value) {
        if (!files.at(std::string(key)).is_string() ||
            files.at(std::string(key))
                .get_ref<const Json::string_t&>() != value) {
            RejectBackup("manifest-file-reference");
        }
    };
    exact_reference("config", kBackupConfigEntry);
    exact_reference("user_dictionary", kBackupUserDictionaryEntry);
    exact_reference("phrase_order", kBackupPhraseOrderEntry);
    custom_primary_binary.reset();
    if (files.contains("custom_primary_dictionary")) {
        const auto& reference =
            files.at("custom_primary_dictionary");
        if (!reference.is_string()) {
            RejectBackup("manifest-custom-reference");
        }
        if (reference.get_ref<const Json::string_t&>() ==
            kBackupCustomPrimaryTextEntry) {
            custom_primary_binary = false;
        } else if (reference.get_ref<const Json::string_t&>() ==
                   kBackupCustomPrimaryBinaryEntry) {
            custom_primary_binary = true;
        } else {
            RejectBackup("manifest-custom-reference");
        }
    }
    return true;
}

ZipArchiveEntry ToEntry(std::string_view name, std::string_view data) {
    return {std::string(name), Bytes(data)};
}

std::map<std::string, const ZipArchiveEntry*> EntryMap(
    std::span<const ZipArchiveEntry> entries) {
    std::map<std::string, const ZipArchiveEntry*> result;
    for (const auto& entry : entries) {
        if (!result.emplace(entry.name, &entry).second) {
            RejectBackup("duplicate-entry");
        }
    }
    return result;
}

const ZipArchiveEntry& RequireEntry(
    const std::map<std::string, const ZipArchiveEntry*>& entries,
    std::string_view name) {
    const auto found = entries.find(std::string(name));
    if (found == entries.end()) RejectBackup("missing-entry");
    return *found->second;
}

} // namespace

HRESULT WriteBackupArchiveFileResult(
    const std::filesystem::path& path, const BackupArchivePayload& payload) {
    try {
        const bool has_custom =
            payload.custom_primary_dictionary.has_value();
        if ((payload.config.primary_dict == PrimaryDict::Custom) != has_custom) {
            return E_INVALIDARG;
        }
        std::map<uint32_t, std::vector<std::wstring>> parsed_user;
        HRESULT result = ParseDictionary(
            payload.user_dictionary, true, parsed_user);
        if (FAILED(result)) return result;
        std::map<uint32_t, std::vector<std::wstring>> parsed_phrase;
        result = ParseDictionary(
            payload.phrase_order, false, parsed_phrase);
        if (FAILED(result)) return result;
        std::string config;
        ConfigJsonError config_error;
        result = SerializeConfigJson(
            payload.config, config, config_error);
        if (FAILED(result)) return result;

        std::vector<ZipArchiveEntry> entries;
        entries.reserve(has_custom ? 5 : 4);
        entries.push_back(ToEntry(
            kBackupManifestEntry,
            SerializeManifest(payload.custom_primary_dictionary)));
        entries.push_back(ToEntry(kBackupConfigEntry, config));
        entries.push_back(ToEntry(
            kBackupUserDictionaryEntry, payload.user_dictionary));
        entries.push_back(ToEntry(
            kBackupPhraseOrderEntry, payload.phrase_order));
        if (has_custom) {
            entries.push_back(ToEntry(
                payload.custom_primary_dictionary->binary
                    ? kBackupCustomPrimaryBinaryEntry
                    : kBackupCustomPrimaryTextEntry,
                payload.custom_primary_dictionary->data));
        }
        return WriteZipArchiveFileResult(path, entries);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Complete ZIP backup creation ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("Complete ZIP backup creation failed unexpectedly");
        return E_UNEXPECTED;
    }
}

HRESULT ReadBackupArchiveFileResult(
    const std::filesystem::path& path,
    const QiQiConfig& current_config,
    const std::filesystem::path& restored_custom_primary_path,
    BackupArchivePlan& plan,
    BackupArchiveReadStage& stage) {
    try {
        stage = BackupArchiveReadStage::Read;
        std::string bytes;
        const HRESULT file_result =
            ReadFileToUtf8StringResult(path, bytes, kMaxZipArchiveBytes);
        if (FAILED(file_result)) return file_result;
        stage = BackupArchiveReadStage::Validate;
        std::vector<ZipArchiveEntry> entries;
        const std::vector<uint8_t> binary(bytes.begin(), bytes.end());
        const HRESULT read_result =
            ReadZipArchive(binary, entries);
        if (FAILED(read_result)) return read_result;
        const auto indexed = EntryMap(entries);
        const auto& manifest_entry =
            RequireEntry(indexed, kBackupManifestEntry);
        std::optional<bool> custom_primary_binary;
        ParseManifest(
            Text(manifest_entry.data), custom_primary_binary);
        const bool has_custom = custom_primary_binary.has_value();
        const size_t expected_count = has_custom ? 5 : 4;
        if (entries.size() != expected_count) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        RequireEntry(indexed, kBackupConfigEntry);
        RequireEntry(indexed, kBackupUserDictionaryEntry);
        RequireEntry(indexed, kBackupPhraseOrderEntry);
        const std::string_view custom_entry_name = has_custom
            ? (*custom_primary_binary
                ? kBackupCustomPrimaryBinaryEntry
                : kBackupCustomPrimaryTextEntry)
            : std::string_view{};
        if (has_custom) RequireEntry(indexed, custom_entry_name);

        QiQiConfig baseline = current_config;
        if (has_custom) {
            if (restored_custom_primary_path.empty()) return E_INVALIDARG;
            auto custom_target = restored_custom_primary_path;
            custom_target.replace_extension(
                *custom_primary_binary ? L".qdict" : L".txt");
            baseline.primary_dict_path = custom_target.wstring();
        }
        BackupArchivePlan parsed;
        ConfigJsonError config_error;
        const auto& config_entry =
            RequireEntry(indexed, kBackupConfigEntry);
        const HRESULT config_result = ParseConfigJson(
            Text(config_entry.data), baseline, parsed.config, config_error);
        if (FAILED(config_result)) return config_result;
        if ((parsed.config.primary_dict == PrimaryDict::Custom) !=
            has_custom) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (has_custom) {
            parsed.config.primary_dict_path =
                baseline.primary_dict_path;
        }
        parsed.config_changed = has_custom ||
            !GetConfigJsonChanges(current_config, parsed.config).empty() ||
            current_config.primary_dict_path !=
                parsed.config.primary_dict_path;
        parsed.has_custom_primary_dictionary = has_custom;
        if (has_custom) {
            parsed.restored_custom_primary_path =
                std::filesystem::path(
                    baseline.primary_dict_path);
        }

        const auto& user_entry =
            RequireEntry(indexed, kBackupUserDictionaryEntry);
        HRESULT result = ParseDictionary(
            Text(user_entry.data), true, parsed.user_entries);
        if (FAILED(result)) return result;
        const auto& phrase_entry =
            RequireEntry(indexed, kBackupPhraseOrderEntry);
        result = ParseDictionary(
            Text(phrase_entry.data), false, parsed.phrase_entries);
        if (FAILED(result)) return result;
        if (has_custom) {
            const auto& custom_entry =
                RequireEntry(indexed, custom_entry_name);
            parsed.custom_primary_dictionary = BackupCustomPrimaryDictionary{
                .binary = *custom_primary_binary,
                .data = Text(custom_entry.data)};
        }

        plan = std::move(parsed);
        return S_OK;
    } catch (const InvalidBackup&) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    } catch (const Json::exception&) {
        QIQI_LOG_WARN("Complete ZIP backup manifest JSON failed");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Complete ZIP backup read ran out of memory");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("Complete ZIP backup read failed unexpectedly");
        return E_UNEXPECTED;
    }
}

std::optional<BackupConflictMode> PromptBackupRestoreConflictMode(
    HWND owner, UiLanguage language) noexcept {
    const int choice = ::MessageBoxW(
        owner,
        LocalizedUiText(
            language,
            L"请选择恢复方式：\r\n"
            L"• 点击“是”：合并模式（保留现有用户词库与词组排序，合并备份项）\r\n"
            L"• 点击“否”：覆盖模式（用备份完全替换现有用户词库与词组排序）\r\n"
            L"• 如备份含自定义主词库，将使用备份副本并保存到本机用户数据目录\r\n"
            L"• 校验通过后配置与数据立即恢复；已应用部分不会全局回滚\r\n"
            L"• 点击“取消”：中止恢复",
            L"Choose restore conflict mode:\r\n"
            L"• Yes: Merge (keep existing user words & phrase ordering, merge backup items)\r\n"
            L"• No: Replace (overwrite existing user dictionary & phrase ordering with backup)\r\n"
            L"• A bundled custom primary dictionary is restored into this user's data directory\r\n"
            L"• Configuration and data are restored immediately after validation; applied changes are not globally rolled back\r\n"
            L"• Cancel: Abort restore"),
        LocalizedUiText(language, L"恢复配置与词库备份", L"Restore settings and dictionary backup"),
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (choice == IDYES) return BackupConflictMode::Merge;
    if (choice == IDNO) return BackupConflictMode::Replace;
    if (choice == IDCANCEL) return std::nullopt;
    QIQI_LOG_ERROR(
        "Backup restore conflict dialog failed (error={})",
        ::GetLastError());
    return std::nullopt;
}

std::wstring FormatBackupRestoreMessage(
    const BackupRestoreResult& result, UiLanguage language) {
    const auto text = [language](const wchar_t* zh, const wchar_t* en) noexcept {
        return LocalizedUiText(language, zh, en);
    };
    if (result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK) {
        return std::format(L"{} ({}: {}, {}: {})",
            text(L"已恢复备份", L"Backup restored"),
            text(L"词库规则", L"Dictionary rules"), result.summary.user_entries_applied,
            text(L"词组排序", L"Phrase ordering"), result.summary.phrase_freq_applied);
    }
    const wchar_t* stage = L"";
    switch (result.stage) {
        case BackupRestoreStage::Read: stage = text(L"读取", L"read"); break;
        case BackupRestoreStage::Validate: stage = text(L"校验", L"validation"); break;
        case BackupRestoreStage::Prepare: stage = text(L"准备", L"preparation"); break;
        case BackupRestoreStage::UserDictionary: stage = text(L"词库规则", L"dictionary"); break;
        case BackupRestoreStage::PhraseFrequency: stage = text(L"词组排序", L"phrase ordering"); break;
        case BackupRestoreStage::CustomDictionary:
            stage = text(L"自定义主词库", L"custom primary dictionary"); break;
        case BackupRestoreStage::Config: stage = text(L"配置", L"configuration"); break;
        case BackupRestoreStage::Refresh: stage = text(L"刷新", L"refresh"); break;
        case BackupRestoreStage::Complete: break;
    }
    std::wstring message = std::format(L"{} ({}; {}: {}, {}: {}; 0x{:08X})",
        result.status == BackupRestoreStatus::Partial
            ? text(L"备份恢复未完成；可能已应用部分更改",
                L"Backup restore incomplete; changes may already have been applied")
            : text(L"恢复备份失败", L"Backup restore failed"),
        stage, text(L"成功操作", L"Succeeded operations"), result.operations_succeeded,
        text(L"失败操作", L"Failed operations"), result.operations_failed,
        gsl::narrow_cast<uint32_t>(result.error));
    if (FAILED(result.refresh_error)) {
        message += std::format(L" {} (0x{:08X})",
            text(L"刷新失败；当前显示可能不完整",
                L"Refresh failed; the displayed view may be incomplete"),
            gsl::narrow_cast<uint32_t>(result.refresh_error));
    }
    return message;
}

} // namespace qiqi
