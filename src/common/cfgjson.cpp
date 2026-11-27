// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/cfgjson.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <nlohmann/json.hpp>
#include <exception>
#include <new>
#include <set>
#include <utility>

namespace qiqi {
namespace {

using Json = nlohmann::ordered_json;

template <typename T>
constexpr auto EnumNames() noexcept {
    if constexpr (std::is_same_v<T, PrimaryDict>) {
        return std::array{
            PrimaryDictToString(PrimaryDict::Jidian), PrimaryDictToString(PrimaryDict::Haifeng),
            PrimaryDictToString(PrimaryDict::BuiltinOnly), PrimaryDictToString(PrimaryDict::Custom)};
    } else if constexpr (std::is_same_v<T, CandidateLayout>) {
        return std::array<std::string_view, 2>{"horizontal", "vertical"};
    } else if constexpr (std::is_same_v<T, WubiSpaceEnterMode>) {
        return std::array<std::string_view, 3>{
            "space-select-enter-code", "space-select-enter-clear", "enter-select-space-clear"};
    } else if constexpr (std::is_same_v<T, UiLanguage>) {
        return std::array<std::string_view, 3>{"auto", "zh-CN", "en-US"};
    } else if constexpr (std::is_same_v<T, CompositionUnderlineStyle>) {
        return std::array<std::string_view, 5>{"dot", "solid", "dash", "squiggle", "none"};
    } else if constexpr (std::is_same_v<T, UiTheme>) {
        return std::array<std::string_view, 4>{"system", "light", "dark", "high_contrast"};
    } else if constexpr (std::is_same_v<T, ImeSkinId>) {
        return kImeSkinIds;
    } else if constexpr (std::is_same_v<T, CandidateFontWeight>) {
        return std::array<std::string_view, 10>{
            "auto", "thin", "extra-light", "light", "normal",
            "medium", "semi-bold", "bold", "extra-bold", "black"};
    } else {
        static_assert(std::is_same_v<T, UiTextScale>);
        return std::array<std::string_view, 4>{"100%", "125%", "150%", "200%"};
    }
}

class RejectedJson final : public std::exception {};

void Reject(ConfigJsonError& error, ConfigJsonIssue issue, std::string_view field = {}) {
    error = {issue, std::string(field), 0};
    throw RejectedJson{};
}

HRESULT ReportFailure(const ConfigJsonError& error, HRESULT result) noexcept {
    QIQI_LOG_WARN("Configuration JSON failed (issue={}, byte={}, hr=0x{:08X})",
        std::to_underlying(error.issue), error.byte, static_cast<unsigned long>(result));
    return result;
}

template <typename T>
Json FieldJson(const ConfigField<T>& field, const QiQiConfig& config) noexcept(!std::is_enum_v<T>) {
    const auto value = field.Value(config);
    if constexpr (std::is_enum_v<T>) {
        constexpr auto names = EnumNames<T>();
        return gsl::at(names, gsl::narrow_cast<size_t>(value));
    } else {
        return config.*(field.member);
    }
}

template <typename T>
void ReadField(
    const ConfigField<T>& field, const Json& value, QiQiConfig& config, ConfigJsonError& error) {
    uint64_t number = 0;
    const auto path = "settings." + std::string(field.backup_key);
    if constexpr (std::is_same_v<T, bool>) {
        if (!value.is_boolean()) Reject(error, ConfigJsonIssue::InvalidType, path);
        number = value.get<bool>() ? 1 : 0;
    } else if constexpr (std::is_enum_v<T>) {
        if (!value.is_string()) Reject(error, ConfigJsonIssue::InvalidType, path);
        const auto& name = value.get_ref<const Json::string_t&>();
        constexpr auto names = EnumNames<T>();
        const auto found = std::ranges::find(names, name);
        if (found == names.end()) Reject(error, ConfigJsonIssue::InvalidEnum, path);
        number = gsl::narrow_cast<uint64_t>(found - names.begin());
    } else {
        if (!value.is_number_integer()) Reject(error, ConfigJsonIssue::InvalidType, path);
        if (!value.is_number_unsigned() && value.get<int64_t>() < 0)
            Reject(error, ConfigJsonIssue::OutOfRange, path);
        number = value.get<uint64_t>();
    }
    if (number < field.minimum || number > field.maximum || !field.Assign(config, number))
        Reject(error, ConfigJsonIssue::OutOfRange, path);
}

} // namespace

std::vector<ConfigJsonChange> GetConfigJsonChanges(const QiQiConfig& before, const QiQiConfig& after) {
    std::vector<ConfigJsonChange> changes;
    ForEachConfigField([&](const auto& field) {
        if (field.Value(before) != field.Value(after)) {
            changes.push_back({std::string(field.backup_key),
                FieldJson(field, before).dump(-1, ' ', true), FieldJson(field, after).dump(-1, ' ', true)});
        }
    });
    ForEachConfigStringField([&](const auto& field) {
        const auto& before_value = before.*(field.member);
        const auto& after_value = after.*(field.member);
        if (before_value != after_value) {
            changes.push_back({std::string(field.backup_key),
                Json(WideToUtf8(before_value)).dump(-1, ' ', true),
                Json(WideToUtf8(after_value)).dump(-1, ' ', true)});
        }
    });
    return changes;
}

HRESULT SerializeConfigJson(const QiQiConfig& config, std::string& output, ConfigJsonError& error) {
    error = {};
    try {
        Json settings = Json::object();
        ForEachConfigField([&](const auto& field) {
            const auto value = field.Value(config);
            if (value < field.minimum || value > field.maximum)
                Reject(error, ConfigJsonIssue::OutOfRange, "settings." + std::string(field.backup_key));
            settings[std::string(field.backup_key)] = FieldJson(field, config);
        });
        ForEachConfigStringField([&](const auto& field) {
            const auto& value = config.*(field.member);
            if (field.backup_key == "candidate_font_family" &&
                !IsValidCandidateFontFamily(value))
                Reject(error, ConfigJsonIssue::OutOfRange,
                    "settings." + std::string(field.backup_key));
            settings[std::string(field.backup_key)] = WideToUtf8(value);
        });
        const Json document{{"format", kConfigJsonFormat}, {"version", 1}, {"settings", std::move(settings)}};
        auto text = document.dump(2) + "\n";
        output = std::move(text);
        return S_OK;
    } catch (const RejectedJson&) {
        return ReportFailure(error, E_INVALIDARG);
    } catch (const std::bad_alloc&) {
        return ReportFailure(error, E_OUTOFMEMORY);
    }
}

HRESULT ParseConfigJson(
    std::string_view input, const QiQiConfig& current, QiQiConfig& output, ConfigJsonError& error) {
    error = {};
    try {
        if (input.size() > kMaxConfigJsonBytes) Reject(error, ConfigJsonIssue::TooLarge);
        input = StripUtf8Bom(input);
        std::vector<std::set<std::string>> keys;
        std::string last_key;
        const auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
            switch (event) {
                case Json::parse_event_t::object_start:
                    if (depth > 1) Reject(error, ConfigJsonIssue::InvalidType, "settings." + last_key);
                    keys.emplace_back();
                    break;
                case Json::parse_event_t::key: {
                    last_key = value.get<std::string>();
                    const auto path = depth == 1 ? last_key : "settings." + last_key;
                    error.field = path;
                    if (!keys.back().insert(last_key).second)
                        Reject(error, ConfigJsonIssue::DuplicateField, path);
                    break;
                }
                case Json::parse_event_t::object_end:
                    keys.pop_back();
                    break;
                case Json::parse_event_t::array_start:
                    Reject(error, ConfigJsonIssue::InvalidType,
                        depth == 0 ? std::string{} : (depth == 1 ? last_key : "settings." + last_key));
                    break;
                default: break;
            }
            return true;
        };
        const Json document = Json::parse(input.begin(), input.end(), callback);
        if (!document.is_object()) Reject(error, ConfigJsonIssue::ObjectRequired);
        for (const auto& item : document.items()) {
            if (item.key() != "format" && item.key() != "version" && item.key() != "settings")
                Reject(error, ConfigJsonIssue::UnknownField, item.key());
        }
        for (const auto key : {"format", "version", "settings"}) {
            if (!document.contains(key)) Reject(error, ConfigJsonIssue::MissingField, key);
        }
        if (!document["format"].is_string()) Reject(error, ConfigJsonIssue::InvalidType, "format");
        if (document["format"].get_ref<const Json::string_t&>() != kConfigJsonFormat)
            Reject(error, ConfigJsonIssue::UnsupportedFormat, "format");
        if (!document["version"].is_number_integer()) Reject(error, ConfigJsonIssue::InvalidType, "version");
        if (document["version"] != 1) Reject(error, ConfigJsonIssue::UnsupportedVersion, "version");
        const auto& settings = document["settings"];
        if (!settings.is_object()) Reject(error, ConfigJsonIssue::ObjectRequired, "settings");
        QiQiConfig candidate = current;
        for (const auto& item : settings.items()) {
            bool known = false;
            ForEachConfigField([&](const auto& field) {
                if (item.key() == field.backup_key) {
                    known = true;
                    ReadField(field, item.value(), candidate, error);
                }
            });
            ForEachConfigStringField([&](const auto& field) {
                if (item.key() != field.backup_key) return;
                known = true;
                if (!item.value().is_string())
                    Reject(error, ConfigJsonIssue::InvalidType, "settings." + item.key());
                std::wstring decoded;
                const auto& encoded = item.value().get_ref<const Json::string_t&>();
                const HRESULT result = Utf8ToWideResult(encoded, decoded, Utf8ErrorPolicy::Reject);
                if (FAILED(result) ||
                    (field.backup_key == "candidate_font_family" &&
                     !IsValidCandidateFontFamily(decoded)))
                    Reject(error, ConfigJsonIssue::InvalidType, "settings." + item.key());
                candidate.*(field.member) = std::move(decoded);
            });
            if (!known) Reject(error, ConfigJsonIssue::UnknownField, "settings." + item.key());
        }
        if (candidate.primary_dict == PrimaryDict::Custom && candidate.primary_dict_path.empty())
            Reject(error, ConfigJsonIssue::CustomDictionaryRequired, "settings.primary_dict");
        if (candidate.easy_reading && candidate.ui_text_scale < UiTextScale::Percent150)
            Reject(error, ConfigJsonIssue::ComfortScaleRequired, "settings.ui_text_scale");
        output = std::move(candidate);
        error = {};
        return S_OK;
    } catch (const RejectedJson&) {
        return ReportFailure(error, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    } catch (const Json::parse_error& failure) {
        error = {ConfigJsonIssue::Syntax, {}, failure.byte};
        return ReportFailure(error, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    } catch (const Json::out_of_range&) {
        error.issue = ConfigJsonIssue::OutOfRange;
        return ReportFailure(error, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    } catch (const std::bad_alloc&) {
        return ReportFailure(error, E_OUTOFMEMORY);
    }
}

} // namespace qiqi
