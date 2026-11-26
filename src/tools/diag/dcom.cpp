// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/fileutil.h"

#include <array>
#include <utility>

namespace qiqi::diag::detail {

Json CollectDcomPolicy() {
    Json output = Json::object();
    ScopedRegKey ole;
    HRESULT result =
        OpenRegistryKeyRead(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Ole", KEY_READ | KEY_WOW64_64KEY, ole);
    output["inspection"] = CheckResult("dcom_registry", result);
    if (FAILED(result))
        return output;

    std::wstring enable;
    result = ReadRegistryString(ole.Get(), L"EnableDCOM", enable);
    if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        output["enabled"] = true;
        output["enabled_source"] = "Windows default (value absent)";
        output["enable_check"] = CheckResult("enable_dcom", S_OK);
    } else if (SUCCEEDED(result)) {
        output["enabled"] = _wcsicmp(enable.c_str(), L"Y") == 0;
        output["enable_value"] = WideToUtf8(enable);
        output["enable_check"] = CheckResult("enable_dcom", S_OK);
    } else {
        DWORD enabled_dword = 0;
        const HRESULT dword_result = ReadRegistryDword(ole.Get(), L"EnableDCOM", enabled_dword);
        output["enable_check"] = CheckResult("enable_dcom", dword_result, "warning");
        if (SUCCEEDED(dword_result)) {
            output["enabled"] = enabled_dword != 0;
            output["enable_value_dword"] = enabled_dword;
        }
    }
    constexpr std::array<std::pair<const wchar_t*, std::string_view>, 2> legacy_values{{
        {L"LegacyAuthenticationLevel", "legacy_authentication_level"},
        {L"LegacyImpersonationLevel", "legacy_impersonation_level"},
    }};
    for (const auto [value, property] : legacy_values) {
        DWORD setting = 0;
        const HRESULT setting_result = ReadRegistryDword(ole.Get(), value, setting);
        output[property] =
            CheckResult(std::string(property), setting_result, FAILED(setting_result) ? "not_set" : "");
        if (SUCCEEDED(setting_result)) {
            output[std::string(property) + "_value"] = setting;
        }
    }
    return output;
}

} // namespace qiqi::diag::detail
