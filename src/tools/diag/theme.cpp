// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/fileutil.h"

#include <array>
#include <utility>

namespace qiqi::diag::detail {

void CollectThemeInformation(Json& stage) {
    Json theme = Json::object();
    ScopedRegKey personalize;
    const HRESULT result = OpenRegistryKeyRead(HKEY_CURRENT_USER,
                                               L"Software\\Microsoft\\Windows"
                                               L"\\CurrentVersion\\Themes"
                                               L"\\Personalize",
                                               KEY_READ, personalize);
    const Json theme_check = CheckResult("theme_registry", result, FAILED(result) ? "warning" : "");
    theme["inspection"] = theme_check;
    AddCheck(stage, theme_check);
    if (SUCCEEDED(result)) {
        constexpr std::array<std::pair<const wchar_t*, const char*>, 4> values{{
            {L"AppsUseLightTheme", "application_mode"},
            {L"SystemUsesLightTheme", "system_mode"},
            {L"EnableTransparency", "transparency"},
            {L"ColorPrevalence", "accent_on_surfaces"},
        }};
        for (const auto [name, property] : values) {
            DWORD value = 0;
            const HRESULT read_result = ReadRegistryDword(personalize.Get(), name, value);
            theme[property] =
                CheckResult(std::string(property), read_result, FAILED(read_result) ? "not_set" : "");
            if (SUCCEEDED(read_result)) {
                if (std::string_view(property) == "application_mode" ||
                    std::string_view(property) == "system_mode") {
                    theme[std::string(property) + "_value"] = value != 0 ? "light" : "dark";
                } else {
                    theme[std::string(property) + "_value"] = value != 0;
                }
            }
        }
    }

    ScopedRegKey themes;
    const HRESULT themes_result = OpenRegistryKeyRead(HKEY_CURRENT_USER,
                                                      L"Software\\Microsoft\\Windows"
                                                      L"\\CurrentVersion\\Themes",
                                                      KEY_READ, themes);
    if (SUCCEEDED(themes_result)) {
        std::wstring current_theme;
        const HRESULT current_result = ReadRegistryString(themes.Get(), L"CurrentTheme", current_theme);
        if (SUCCEEDED(current_result)) {
            theme["current_theme_path"] = WideToUtf8(RedactPathForReport(current_theme));
        }
    }
    stage["theme"] = std::move(theme);
}

} // namespace qiqi::diag::detail
