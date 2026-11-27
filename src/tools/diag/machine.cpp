// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"
#include "common/brokerctrl.h"
#include "common/fileutil.h"
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <bit>
#include <format>

namespace qiqi::diag::detail {

Json CollectMachine() {
    Json stage = NewStage("machine", "Machine information");
    Json operating_system = Json::object();
    HMODULE raw_ntdll = ::GetModuleHandleW(L"ntdll.dll");
    using RtlGetVersionFunction = LONG(NTAPI*)(OSVERSIONINFOEXW*);
    const auto rtl_get_version =
        raw_ntdll ? std::bit_cast<RtlGetVersionFunction>(::GetProcAddress(raw_ntdll, "RtlGetVersion"))
                  : nullptr;
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    HRESULT result = E_NOTIMPL;
    if (rtl_get_version) {
        const LONG status = rtl_get_version(&version);
        result = status >= 0 ? S_OK : HRESULT_FROM_NT(status);
    }
    Json version_check = CheckResult("windows_version", result);
    if (SUCCEEDED(result)) {
        operating_system["major"] = version.dwMajorVersion;
        operating_system["minor"] = version.dwMinorVersion;
        operating_system["build"] = version.dwBuildNumber;
        operating_system["platform_id"] = version.dwPlatformId;
        operating_system["service_pack_major"] = version.wServicePackMajor;
        operating_system["service_pack_minor"] = version.wServicePackMinor;
    }
    AddCheck(stage, version_check);

    ScopedRegKey current_version;
    result = OpenRegistryKeyRead(HKEY_LOCAL_MACHINE,
                                 L"SOFTWARE\\Microsoft\\Windows NT"
                                 L"\\CurrentVersion",
                                 KEY_READ | KEY_WOW64_64KEY, current_version);
    Json product_check = CheckResult("windows_product", result, FAILED(result) ? "warning" : "");
    if (SUCCEEDED(result)) {
        std::wstring text;
        if (SUCCEEDED(ReadRegistryString(current_version.Get(), L"ProductName", text))) {
            operating_system["registry_product_name"] = WideToUtf8(text);
            const bool is_windows_11 = version.dwMajorVersion >= 10 && version.dwBuildNumber >= 22000 &&
                                       (text.starts_with(L"Windows 10") || text.starts_with(L"Windows 11"));
            if (text.starts_with(L"Windows 10") && is_windows_11) {
                text.replace(0, std::wstring_view(L"Windows 10").size(), L"Windows 11");
            }
            operating_system["product_name"] = WideToUtf8(text);
            operating_system["is_windows_11"] = is_windows_11;
        }
        if (SUCCEEDED(ReadRegistryString(current_version.Get(), L"DisplayVersion", text))) {
            operating_system["display_version"] = WideToUtf8(text);
        }
        if (SUCCEEDED(ReadRegistryString(current_version.Get(), L"EditionID", text))) {
            operating_system["edition_id"] = WideToUtf8(text);
        }
        if (SUCCEEDED(ReadRegistryString(current_version.Get(), L"InstallationType", text))) {
            operating_system["installation_type"] = WideToUtf8(text);
        }
        DWORD value = 0;
        if (SUCCEEDED(ReadRegistryDword(current_version.Get(), L"UBR", value))) {
            operating_system["update_build_revision"] = value;
        }
    }
    AddCheck(stage, product_check);

    SYSTEM_INFO system_info{};
    ::GetNativeSystemInfo(&system_info);
    operating_system["architecture"] = ProcessorArchitectureName(system_info.wProcessorArchitecture);
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> locale{};
    if (::GetUserDefaultLocaleName(locale.data(), gsl::narrow_cast<int>(locale.size())) != 0) {
        operating_system["user_locale"] = WideToUtf8(locale.data());
    }
    stage["operating_system"] = std::move(operating_system);
    AddCheck(stage, CheckResult("system_architecture", S_OK));

    Json cpu = Json::object();
    ScopedRegKey processor;
    result = OpenRegistryKeyRead(HKEY_LOCAL_MACHINE,
                                 L"HARDWARE\\DESCRIPTION\\System"
                                 L"\\CentralProcessor\\0",
                                 KEY_READ | KEY_WOW64_64KEY, processor);
    Json cpu_check = CheckResult("cpu", result, FAILED(result) ? "warning" : "");
    if (SUCCEEDED(result)) {
        std::wstring value;
        if (SUCCEEDED(ReadRegistryString(processor.Get(), L"ProcessorNameString", value))) {
            cpu["name"] = WideToUtf8(value);
        }
        if (SUCCEEDED(ReadRegistryString(processor.Get(), L"VendorIdentifier", value))) {
            cpu["vendor_id"] = WideToUtf8(value);
        }
    }
    cpu["maximum_processor_count"] = ::GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
    CollectCpuTopology(cpu, stage);
    stage["cpu"] = std::move(cpu);
    AddCheck(stage, cpu_check);

    MEMORYSTATUSEX memory{.dwLength = sizeof(MEMORYSTATUSEX)};
    if (::GlobalMemoryStatusEx(&memory) == FALSE) {
        AddCheck(stage, CheckResult("memory", HRESULT_FROM_WIN32(::GetLastError())));
    } else {
        stage["memory"] = Json{
            {"load_percent", memory.dwMemoryLoad},
            {"physical_total_bytes", gsl::narrow_cast<uint64_t>(memory.ullTotalPhys)},
            {"physical_available_bytes", gsl::narrow_cast<uint64_t>(memory.ullAvailPhys)},
            {"page_file_total_bytes", gsl::narrow_cast<uint64_t>(memory.ullTotalPageFile)},
            {"page_file_available_bytes", gsl::narrow_cast<uint64_t>(memory.ullAvailPageFile)},
            {"virtual_total_bytes", gsl::narrow_cast<uint64_t>(memory.ullTotalVirtual)},
            {"virtual_available_bytes", gsl::narrow_cast<uint64_t>(memory.ullAvailVirtual)},
        };
        AddCheck(stage, CheckResult("memory", S_OK));
    }
    CollectDisplayInformation(stage);
    CollectFontInformation(stage);
    CollectAccessibilityInformation(stage);
    CollectThemeInformation(stage);
    FinalizeStage(stage);
    return stage;
}

bool IsRelevantProcessName(std::wstring_view name) noexcept {
    constexpr std::array names{
        L"QiQiIMESettings.exe",     L"QiQiIMELogMonitor.exe",
        L"QiQiIMEDictManager.exe",  L"QiQiIMEDiag.exe",
        L"QiQiIMEConfigBroker.exe", L"Narrator.exe",
        L"TextInputHost.exe",       L"ctfmon.exe",
        L"SearchHost.exe",          L"StartMenuExperienceHost.exe",
        L"ShellExperienceHost.exe", L"ApplicationFrameHost.exe",
        L"FeedbackHub.exe",         L"explorer.exe",
    };
    return std::ranges::any_of(
        names, [&](const wchar_t* candidate) { return _wcsicmp(name.data(), candidate) == 0; });
}

Json ProcessDetails(const PROCESSENTRY32W& process) {
    Json output{
        {"name", WideToUtf8(Win32StringView(process.szExeFile))},
        {"pid", process.th32ProcessID},
        {"parent_pid", process.th32ParentProcessID},
        {"thread_count", process.cntThreads},
    };
    DWORD session = 0;
    const BOOL has_session = ::ProcessIdToSessionId(process.th32ProcessID, &session);
    if (has_session != FALSE) {
        output["session_id"] = session;
    } else {
        output["session_lookup"] =
            CheckResult("session_lookup", HRESULT_FROM_WIN32(::GetLastError()), "warning");
    }

    const HANDLE raw_process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
    ScopedKernelHandle scoped_process(raw_process);
    if (!scoped_process) {
        output["integrity"] = CheckResult("integrity", HRESULT_FROM_WIN32(::GetLastError()), "warning");
        return output;
    }
    FILETIME creation{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (::GetProcessTimes(scoped_process.Get(), &creation, &exit_time, &kernel_time, &user_time) != FALSE) {
        SYSTEMTIME start{};
        const FILETIME creation_utc = creation;
        if (::FileTimeToSystemTime(&creation_utc, &start) != FALSE) {
            output["start_time_utc"] =
                std::format("{:04d}-{:02d}-{:02d}T"
                            "{:02d}:{:02d}:{:02d}Z",
                            start.wYear, start.wMonth, start.wDay, start.wHour, start.wMinute, start.wSecond);
        }
    } else {
        output["start_time_lookup"] =
            CheckResult("start_time", HRESULT_FROM_WIN32(::GetLastError()), "warning");
    }
    USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;
    const BOOL machine_result = ::IsWow64Process2(scoped_process.Get(), &process_machine, &native_machine);
    if (machine_result != FALSE) {
        output["architecture"] = ArchitectureName(
            process_machine == IMAGE_FILE_MACHINE_UNKNOWN ? native_machine : process_machine);
        output["native_architecture"] = ArchitectureName(native_machine);
    } else {
        output["architecture_lookup"] =
            CheckResult("architecture", HRESULT_FROM_WIN32(::GetLastError()), "warning");
    }

    HANDLE raw_token = nullptr;
    if (!::OpenProcessToken(scoped_process.Get(), TOKEN_QUERY, &raw_token)) {
        output["integrity"] = CheckResult("integrity", HRESULT_FROM_WIN32(::GetLastError()), "warning");
        return output;
    }
    ScopedKernelHandle token(raw_token);
    DWORD rid = 0;
    const HRESULT result = qiqi::config::TokenIntegrityRid(token.Get(), rid);
    if (FAILED(result)) {
        output["integrity"] = CheckResult("integrity", result, "warning");
        return output;
    }
    output["integrity"] = Json{
        {"status", "ok"},
        {"level", IntegrityName(rid)},
        {"rid", rid},
        {"hresult", HResultText(S_OK)},
    };
    TOKEN_ELEVATION elevation{};
    DWORD token_size = 0;
    if (::GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof(elevation), &token_size) !=
        FALSE) {
        output["elevated"] = elevation.TokenIsElevated != 0;
    } else {
        output["elevation_lookup"] =
            CheckResult("elevation", HRESULT_FROM_WIN32(::GetLastError()), "warning");
    }
    return output;
}

Json CollectProcesses() {
    Json stage = NewStage("processes", "Related process information");
    HANDLE raw_snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ScopedKernelHandle snapshot(raw_snapshot);
    if (!snapshot) {
        AddCheck(stage, CheckResult("process_snapshot", HRESULT_FROM_WIN32(::GetLastError())));
        FinalizeStage(stage);
        return stage;
    }
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    Json observed = Json::array();
    if (::Process32FirstW(snapshot.Get(), &process) == FALSE) {
        AddCheck(stage, CheckResult("process_snapshot", HRESULT_FROM_WIN32(::GetLastError())));
    } else {
        do {
            const std::wstring_view name = Win32StringView(process.szExeFile);
            if (IsRelevantProcessName(name)) {
                observed.push_back(ProcessDetails(process));
            }
        } while (::Process32NextW(snapshot.Get(), &process) != FALSE);
        const DWORD error = ::GetLastError();
        AddCheck(stage, CheckResult("process_snapshot",
                                    error == ERROR_NO_MORE_FILES ? S_OK : HRESULT_FROM_WIN32(error)));
    }
    stage["observed"] = std::move(observed);
    stage["privacy"] = "Process command lines, open handles, and memory are not collected.";
    FinalizeStage(stage);
    return stage;
}

} // namespace qiqi::diag::detail
