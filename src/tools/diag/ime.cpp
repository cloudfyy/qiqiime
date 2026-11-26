// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"
#include "common/cfgjson.h"
#include "common/config.h"
#include "common/configclient.h"
#include "common/fileutil.h"
#include "common/skin.h"
#include <gsl/gsl>
#include <filesystem>
#include <format>
#include <bit>
#include <type_traits>
#include <utility>

namespace qiqi::diag::detail {

Json SkinPaletteJson(const SkinPalette& palette) {
    Json output = Json::object();
    for (size_t index = 0; index < kSkinColorCount; ++index) {
        const COLORREF color =
            palette.Get(std::bit_cast<SkinColor>(gsl::narrow_cast<std::underlying_type_t<SkinColor>>(index)));
        output[WideToUtf8(gsl::at(kSkinColorNames, index))] =
            std::format("#{:02X}{:02X}{:02X}", GetRValue(color), GetGValue(color), GetBValue(color));
    }
    return output;
}

Json SkinCatalogJson(const ImeSkinCatalog& catalog, ImeSkinId selected) {
    Json skins = Json::array();
    for (const ImeSkin& skin : catalog.skins) {
        skins.push_back(Json{
            {"id", ImeSkinIdToString(skin.id)},
            {"display_order", skin.display_order},
            {"name_zh", WideToUtf8(skin.name_zh)},
            {"name_en", WideToUtf8(skin.name_en)},
            {"light", SkinPaletteJson(skin.palettes.light)},
            {"dark", SkinPaletteJson(skin.palettes.dark)},
            {"selected", skin.id == selected},
        });
    }
    return Json{
        {"catalog_id", WideToUtf8(catalog.id)},
        {"skin_count", catalog.skins.size()},
        {"selected_id", ImeSkinIdToString(selected)},
        {"skins", std::move(skins)},
    };
}

Json CollectIme() {
    Json stage = NewStage("ime", "IME configuration and files");

    config::Snapshot snapshot;
    HRESULT result = config::ReadSnapshotFromRegistry(snapshot);
    Json snapshot_check = CheckResult("registry_snapshot", result);
    AddCheck(stage, snapshot_check);
    if (SUCCEEDED(result)) {
        std::string config_json;
        ConfigJsonError config_error{};
        const HRESULT serialize_result = SerializeConfigJson(snapshot.value, config_json, config_error);
        Json serialized_check = CheckResult("config_serialization", serialize_result);
        if (FAILED(serialize_result)) {
            serialized_check["config_issue"] = std::to_underlying(config_error.issue);
            serialized_check["field"] = config_error.field;
        }
        AddCheck(stage, serialized_check);
        if (SUCCEEDED(serialize_result)) {
            const Json document = Json::parse(config_json);
            stage["settings"] = document.at("settings");
        }
        stage["revision"] = snapshot.revision;
        stage["commit_revision"] = snapshot.commit_revision;
        stage["commit_trace_id"] = WideToUtf8(GuidString(snapshot.commit_trace_id));
        stage["commit_activity_id"] = WideToUtf8(GuidString(snapshot.commit_activity_id));
        stage["user_dictionary_sync_directory"] =
            WideToUtf8(RedactPathForReport(snapshot.user_dict_sync_directory.wstring()));

        const HRESULT catalog_result = ValidateImeSkinCatalog(snapshot.skin_catalog);
        AddCheck(stage, CheckResult("skin_catalog", catalog_result));
        if (SUCCEEDED(catalog_result)) {
            stage["skin"] = SkinCatalogJson(snapshot.skin_catalog, snapshot.value.ime_skin_id);
        }

        const std::filesystem::path executable = GetModuleFilePath(nullptr);
        const std::filesystem::path module_directory = executable.parent_path();
        Json files = Json::array();
        for (const auto file_name : kBundledDictFileNames) {
            files.push_back(InspectPath(ResolveBundledDataFile(module_directory, file_name), false, 0));
        }
        std::filesystem::path user_dict = snapshot.value.user_dict_path.empty()
                                              ? GetDefaultUserDictPath()
                                              : std::filesystem::path(snapshot.value.user_dict_path);
        auto user_dict_entry = InspectPath(user_dict, false, 0);
        user_dict_entry["role"] = "user_dictionary";
        files.push_back(std::move(user_dict_entry));
        stage["files"] = std::move(files);
    }

    ScopedRegKey config_key;
    result = OpenConfigRegistryKeyResult(config_key.Put(), KEY_READ | READ_CONTROL);
    Json config_acl_check = CheckResult("config_key_acl", result, FAILED(result) ? "warning" : "");
    Json config_security;
    if (SUCCEEDED(result)) {
        const HRESULT security_result = InspectRegistryHandleAcl(config_key.Get(), config_security);
        config_acl_check =
            CheckResult("config_key_acl", security_result, FAILED(security_result) ? "warning" : "");
        if (SUCCEEDED(security_result)) {
            config_security["all_packages_read"] = AclGrants(config_security, "S-1-15-2-1", KEY_READ);
            config_security["all_restricted_packages_read"] =
                AclGrants(config_security, "S-1-15-2-2", KEY_READ);
        }
    }
    stage["config_key"] = Json{
        {"path", "HKCU\\" + WideToUtf8(GetActiveConfigRegistrySubKey())},
        {"acl", config_security},
    };
    AddCheck(stage, config_acl_check);
    FinalizeStage(stage);
    return stage;
}

} // namespace qiqi::diag::detail
