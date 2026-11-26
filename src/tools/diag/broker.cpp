// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"
#include "common/config.h"
#include "common/configbrokercontrol.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace qiqi::diag::detail {

struct RegistryView {
    HKEY root = nullptr;
    const wchar_t* base = nullptr;
    REGSAM view = 0;
    std::string_view root_name;
    std::string_view view_name;
};

std::vector<RegistryView> RegistryViews() {
    SYSTEM_INFO system{};
    ::GetNativeSystemInfo(&system);
    const bool is_64_bit = IsNative64BitProcessor(system.wProcessorArchitecture);
    std::vector<RegistryView> output;
    if (is_64_bit) {
        output.push_back({
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes",
            KEY_WOW64_64KEY,
            "HKLM",
            "x64",
        });
    }
    output.push_back({
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes",
        is_64_bit ? KEY_WOW64_32KEY : REGSAM{0},
        "HKLM",
        "x86",
    });
    if (is_64_bit) {
        output.push_back({
            HKEY_CURRENT_USER,
            L"Software\\Classes",
            KEY_WOW64_64KEY,
            "HKCU",
            "x64",
        });
    }
    output.push_back({
        HKEY_CURRENT_USER,
        L"Software\\Classes",
        is_64_bit ? KEY_WOW64_32KEY : REGSAM{0},
        "HKCU",
        "x86",
    });
    return output;
}

std::wstring RegistryPath(std::wstring_view base, std::wstring_view child) {
    std::wstring output(base);
    if (!output.empty() && output.back() != L'\\') {
        output.push_back(L'\\');
    }
    output.append(child);
    return output;
}

HRESULT ReadPermissionValue(HKEY key, const wchar_t* name, ACCESS_MASK required_app_container_mask,
                            Json& output) {
    DWORD type = 0;
    std::vector<uint8_t> bytes;
    HRESULT result = ReadRegistryValue(key, name, type, bytes);
    if (FAILED(result))
        return result;
    if (type != REG_BINARY || bytes.empty()) {
        return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    result = InspectSecurityDescriptor(bytes.data(), output);
    if (FAILED(result))
        return result;
    output["all_packages_access"] = AclGrants(output, "S-1-15-2-1", required_app_container_mask);
    output["all_restricted_packages_access"] = AclGrants(output, "S-1-15-2-2", required_app_container_mask);
    return S_OK;
}

void AppendUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty())
        return;
    const auto found = std::ranges::find_if(
        paths, [&](const auto& existing) { return _wcsicmp(existing.c_str(), path.c_str()) == 0; });
    if (found == paths.end()) {
        paths.push_back(path);
    }
}

Json CollectBrokerRegistrationView(const RegistryView& view,
                                   std::vector<std::filesystem::path>& broker_files) {
    const std::wstring broker_clsid = GuidString(kClsidQiQiConfigBroker);
    const std::wstring appid = GuidString(kAppIdQiQiConfigBroker);
    const std::wstring proxy_clsid = GuidString(kIIDQiQiConfigBroker);
    const std::wstring base_path = RegistryPath(view.base, std::wstring(L"CLSID\\") + broker_clsid);
    Json output{
        {"root", view.root_name},
        {"view", view.view_name},
        {"key", WideToUtf8(Utf8ToWide(view.root_name) + L"\\" + base_path)},
    };

    ScopedRegKey clsid;
    HRESULT result = OpenRegistryKeyRead(view.root, base_path, KEY_READ | view.view, clsid);
    output["registration"] = CheckResult(
        "registration", result, result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? "not_present" : "");
    if (FAILED(result))
        return output;

    std::wstring actual_appid;
    result = ReadRegistryString(clsid.Get(), L"AppID", actual_appid);
    output["appid_link"] = CheckResult("appid_link", result);
    if (SUCCEEDED(result)) {
        output["appid"] = WideToUtf8(actual_appid);
        output["appid_matches_expected"] = _wcsicmp(actual_appid.c_str(), appid.c_str()) == 0;
    }

    const std::wstring local_path = base_path + L"\\LocalServer32";
    ScopedRegKey local_server;
    result = OpenRegistryKeyRead(view.root, local_path, KEY_READ | view.view, local_server);
    Json local_server_check = CheckResult("local_server", result);
    Json local_server_json = Json::object();
    if (SUCCEEDED(result)) {
        std::wstring command;
        result = ReadRegistryString(local_server.Get(), nullptr, command);
        local_server_check = CheckResult("local_server", result);
        if (SUCCEEDED(result)) {
            const std::filesystem::path executable = ExtractLocalServerExecutable(command);
            local_server_json["executable"] = WideToUtf8(RedactPathForReport(executable.wstring()));
            AppendUniquePath(broker_files, executable);
        }
    }
    local_server_json["inspection"] = local_server_check;
    output["local_server"] = std::move(local_server_json);

    const std::wstring appid_path = RegistryPath(view.base, L"AppID\\" + appid);
    ScopedRegKey appid_key;
    result = OpenRegistryKeyRead(view.root, appid_path, KEY_READ | view.view, appid_key);
    Json appid_output = Json::object();
    appid_output["inspection"] = CheckResult("appid", result);
    if (SUCCEEDED(result)) {
        std::wstring run_as;
        const HRESULT run_as_result = ReadRegistryString(appid_key.Get(), L"RunAs", run_as);
        appid_output["run_as"] = CheckResult("run_as", run_as_result, FAILED(run_as_result) ? "warning" : "");
        if (SUCCEEDED(run_as_result)) {
            appid_output["run_as_value"] = WideToUtf8(run_as);
        }
        DWORD authentication = 0;
        const HRESULT authentication_result =
            ReadRegistryDword(appid_key.Get(), L"AuthenticationLevel", authentication);
        appid_output["authentication_level"] = CheckResult("authentication_level", authentication_result,
                                                           FAILED(authentication_result) ? "warning" : "");
        if (SUCCEEDED(authentication_result)) {
            appid_output["authentication_level_value"] = authentication;
        }

        Json access;
        const HRESULT access_result = ReadPermissionValue(
            appid_key.Get(), L"AccessPermission", COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL, access);
        access["inspection"] = CheckResult("access_permission", access_result);
        appid_output["access_permission"] = std::move(access);

        Json launch;
        const HRESULT launch_result = ReadPermissionValue(
            appid_key.Get(), L"LaunchPermission",
            COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL | COM_RIGHTS_ACTIVATE_LOCAL, launch);
        launch["inspection"] = CheckResult("launch_permission", launch_result);
        appid_output["launch_permission"] = std::move(launch);
    }
    output["appid_details"] = std::move(appid_output);

    const std::wstring proxy_path = RegistryPath(view.base, L"CLSID\\" + proxy_clsid + L"\\InprocServer32");
    ScopedRegKey proxy;
    result = OpenRegistryKeyRead(view.root, proxy_path, KEY_READ | view.view, proxy);
    Json proxy_output = Json::object();
    proxy_output["inspection"] = CheckResult("proxy_registration", result);
    if (SUCCEEDED(result)) {
        std::wstring proxy_file;
        const HRESULT proxy_result = ReadRegistryString(proxy.Get(), nullptr, proxy_file);
        proxy_output["file_read"] = CheckResult("proxy_file", proxy_result);
        if (SUCCEEDED(proxy_result)) {
            proxy_output["file"] = WideToUtf8(RedactPathForReport(proxy_file));
            AppendUniquePath(broker_files, proxy_file);
        }
    }
    output["proxy"] = std::move(proxy_output);
    return output;
}

Json ObjectState(std::wstring_view kind, std::wstring_view name) {
    Json output{
        {"name", WideToUtf8(name)},
        {"kind", WideToUtf8(kind)},
    };
    if (kind == L"mutex") {
        ScopedKernelHandle handle(::OpenMutexW(SYNCHRONIZE, FALSE, name.data()));
        if (!handle) {
            const HRESULT result = HRESULT_FROM_WIN32(::GetLastError());
            output["exists"] = false;
            output["inspection"] = CheckResult(
                "open", result, result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? "not_present" : "error");
            return output;
        }
        output["exists"] = true;
        output["inspection"] = CheckResult("open", S_OK);
        return output;
    }

    ScopedKernelHandle handle(::OpenEventW(SYNCHRONIZE, FALSE, name.data()));
    if (!handle) {
        const HRESULT result = HRESULT_FROM_WIN32(::GetLastError());
        output["exists"] = false;
        output["inspection"] = CheckResult(
            "open", result, result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? "not_present" : "error");
        return output;
    }
    output["exists"] = true;
    output["inspection"] = CheckResult("open", S_OK);
    return output;
}

Json CollectBrokerRuntime() {
    Json output{
        {"processes", Json::array()},
        {"session_objects", Json::array()},
    };
    ScopedKernelHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        output["process_snapshot"] = CheckResult("process_snapshot", HRESULT_FROM_WIN32(::GetLastError()));
        return output;
    }
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (::Process32FirstW(snapshot.Get(), &process) == FALSE) {
        output["process_snapshot"] = CheckResult("process_snapshot", HRESULT_FROM_WIN32(::GetLastError()));
        return output;
    }
    do {
        if (_wcsicmp(process.szExeFile, L"QiQiIMEConfigBroker.exe") == 0) {
            output["processes"].push_back(ProcessDetails(process));
        }
    } while (::Process32NextW(snapshot.Get(), &process) != FALSE);
    const DWORD enumeration_error = ::GetLastError();
    output["process_snapshot"] =
        CheckResult("process_snapshot",
                    enumeration_error == ERROR_NO_MORE_FILES ? S_OK : HRESULT_FROM_WIN32(enumeration_error));
    if (output["processes"].empty()) {
        output["running"] = false;
        output["state"] =
            CheckResult("broker_process", HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE), "not_running");
    } else {
        output["running"] = true;
        output["state"] = CheckResult("broker_process", S_OK);
    }

    const std::wstring_view store_subkey = GetActiveConfigRegistrySubKey();
    output["session_objects"].push_back(
        ObjectState(L"mutex", config::BrokerSessionObjectName(L"ConfigBroker", store_subkey)));
    output["session_objects"].push_back(
        ObjectState(L"event", config::BrokerSessionObjectName(L"ConfigBrokerWritable", store_subkey)));
    output["session_objects"].push_back(
        ObjectState(L"event", config::BrokerSessionObjectName(L"ConfigBrokerStop", store_subkey)));
    return output;
}

Json CollectBroker() {
    Json stage = NewStage("broker", "Broker and DCOM information");
    std::vector<std::filesystem::path> broker_files;
    Json registrations = Json::array();
    for (const RegistryView& view : RegistryViews()) {
        Json registration = CollectBrokerRegistrationView(view, broker_files);
        registrations.push_back(std::move(registration));
    }
    stage["registrations"] = registrations;
    const bool registered = std::ranges::any_of(registrations, [](const Json& registration) {
        return registration.contains("registration") &&
               registration["registration"].value("status", "") == "ok";
    });
    AddCheck(stage,
             CheckResult("broker_registration", registered ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                         registered ? "" : "error"));

    Json files = Json::array();
    for (const auto& path : broker_files) {
        files.push_back(InspectPath(path, true, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE));
    }
    stage["files"] = std::move(files);
    stage["dcom"] = CollectDcomPolicy();
    stage["runtime"] = CollectBrokerRuntime();
    FinalizeStage(stage);
    return stage;
}

} // namespace qiqi::diag::detail
