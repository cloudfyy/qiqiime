// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <sddl.h>
#include <userenv.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Mode {
    std::wstring argument;
    std::wstring moniker;
    const wchar_t* capability = nullptr;
    bool opt_out_all_application_packages = true;
    const wchar_t* host_executable =
        L"QiQiImePackageOnlyHost.exe";
};

struct LocalFreeDeleter {
    void operator()(void* value) const noexcept {
        if (value) {
            ::LocalFree(value);
        }
    }
};

using ScopedLocal =
    std::unique_ptr<void, LocalFreeDeleter>;

struct CoTaskMemDeleter {
    void operator()(void* value) const noexcept {
        if (value) {
            ::CoTaskMemFree(value);
        }
    }
};

using ScopedCoTaskMem =
    std::unique_ptr<void, CoTaskMemDeleter>;

struct SidFreeDeleter {
    void operator()(void* sid) const noexcept {
        if (sid) {
            ::FreeSid(sid);
        }
    }
};

using ScopedSid =
    std::unique_ptr<void, SidFreeDeleter>;

class CapabilitySids final {
public:
    ~CapabilitySids() noexcept {
        Clear();
    }

    CapabilitySids() noexcept = default;
    CapabilitySids(const CapabilitySids&) = delete;
    CapabilitySids& operator=(const CapabilitySids&) = delete;
    CapabilitySids(CapabilitySids&&) = delete;
    CapabilitySids& operator=(CapabilitySids&&) = delete;

    HRESULT Build(
        const wchar_t* capability_name) noexcept {
        if (!capability_name) {
            return S_OK;
        }
        PSID* capability_group_sids = nullptr;
        DWORD capability_group_count = 0;
        PSID* capability_sids = nullptr;
        DWORD capability_count = 0;
        if (!::DeriveCapabilitySidsFromName(
                capability_name,
                &capability_group_sids,
                &capability_group_count,
                &capability_sids,
                &capability_count)) {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        try {
            raw_capability_sids_ = capability_sids;
            raw_capability_count_ = capability_count;
            entries_.reserve(
                static_cast<size_t>(capability_count));
            for (DWORD index = 0;
                 index < capability_count;
                 ++index) {
                entries_.push_back(
                    SID_AND_ATTRIBUTES{
                        capability_sids[index],
                        SE_GROUP_ENABLED});
            }
        } catch (const std::bad_alloc&) {
            return FreeDerived(
                capability_group_sids,
                capability_group_count,
                capability_sids,
                capability_count);
        }
        for (DWORD index = 0;
             index < capability_group_count;
             ++index) {
            ::LocalFree(capability_group_sids[index]);
        }
        ::LocalFree(capability_group_sids);
        return S_OK;
    }

    PSID_AND_ATTRIBUTES Data() noexcept {
        return entries_.data();
    }

    DWORD Count() const noexcept {
        return static_cast<DWORD>(entries_.size());
    }

private:
    HRESULT FreeDerived(
        PSID* group_sids,
        DWORD group_count,
        PSID* capability_sids,
        DWORD capability_count) noexcept {
        for (DWORD index = 0;
             index < group_count;
             ++index) {
            ::LocalFree(group_sids[index]);
        }
        ::LocalFree(group_sids);
        for (DWORD index = 0;
             index < capability_count;
             ++index) {
            ::LocalFree(capability_sids[index]);
        }
        ::LocalFree(capability_sids);
        raw_capability_sids_ = nullptr;
        raw_capability_count_ = 0;
        entries_.clear();
        return E_OUTOFMEMORY;
    }

    void Clear() noexcept {
        if (raw_capability_sids_) {
            for (DWORD index = 0;
                 index < raw_capability_count_;
                 ++index) {
                ::LocalFree(raw_capability_sids_[index]);
            }
            ::LocalFree(raw_capability_sids_);
        }
        raw_capability_sids_ = nullptr;
        raw_capability_count_ = 0;
        entries_.clear();
    }

    std::vector<SID_AND_ATTRIBUTES> entries_;
    PSID* raw_capability_sids_ = nullptr;
    DWORD raw_capability_count_ = 0;
};

Mode ResolveMode(std::wstring_view argument) {
    if (argument == L"zero") {
        return {
            std::wstring(argument),
            L"QiQiImeAppContainer.Zero",
            nullptr,
            true};
    }
    if (argument == L"broadGroupCom") {
        return {
            L"broadGroupCom",
            L"QiQiImeAppContainer.BroadGroupCom",
            L"lpacCom",
            false,
            L"QiQiImeBroadGroupHost.exe"};
    }
    if (argument == L"packageOnlyCom") {
        return {
            L"packageOnlyCom",
            L"QiQiImeAppContainer.PackageOnlyCom",
            L"lpacCom",
            true,
            L"QiQiImePackageOnlyHost.exe"};
    }
    if (argument == L"registryRead" ||
        argument == L"registry") {
        return {
            L"registryRead",
            L"QiQiImeAppContainer.RegistryRead",
            L"registryRead",
            true};
    }
    if (argument == L"lpacCom" ||
        argument == L"com") {
        return {
            L"lpacCom",
            L"QiQiImeAppContainer.LpacCom",
            L"lpacCom",
            true};
    }
    throw std::invalid_argument(
        "Unsupported profile. Use broadGroupCom, packageOnlyCom, "
        "zero, registryRead, or lpacCom.");
}

std::filesystem::path CurrentExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "GetModuleFileNameW");
        }
        if (length >= buffer.size() - 1) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
}

std::wstring SidToString(PSID sid) {
    wchar_t* raw = nullptr;
    if (!::ConvertSidToStringSidW(sid, &raw)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "ConvertSidToStringSidW");
    }
    const ScopedLocal scoped(raw);
    return raw;
}

std::filesystem::path ProfileExecutablePath(
    const std::filesystem::path& host_executable,
    const std::wstring& sid_string) {
    wchar_t* raw = nullptr;
    const HRESULT result =
        ::GetAppContainerFolderPath(
            sid_string.c_str(),
            &raw);
    if (FAILED(result)) {
        throw std::system_error(
            result,
            std::generic_category(),
            "GetAppContainerFolderPath");
    }
    const ScopedCoTaskMem scoped(raw);
    const std::filesystem::path profile_directory(raw);
    const std::filesystem::path target_directory =
        profile_directory / L"Bin";
    std::filesystem::create_directories(target_directory);
    const std::filesystem::path target =
        target_directory /
        host_executable.filename();
    if (!::CopyFileW(
            host_executable.c_str(),
            target.c_str(),
            FALSE)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "CopyFileW");
    }
    return target;
}

HANDLE CreateJob() {
    const HANDLE job = ::CreateJobObjectW(
        nullptr,
        nullptr);
    if (!job) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "CreateJobObjectW");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        const DWORD error = ::GetLastError();
        ::CloseHandle(job);
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "SetInformationJobObject");
    }
    return job;
}

class ProfileScope final {
public:
    ProfileScope(
        std::wstring moniker,
        CapabilitySids& capabilities)
        : moniker_(std::move(moniker)) {
        PSID raw_sid = nullptr;
        HRESULT result = ::CreateAppContainerProfile(
            moniker_.c_str(),
            L"QiQiIME LPAC test host",
            L"Opt-in QiQiIME LPAC compatibility fixture",
            capabilities.Data(),
            capabilities.Count(),
            &raw_sid);
        if (result ==
            HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            result =
                ::DeriveAppContainerSidFromAppContainerName(
                    moniker_.c_str(),
                    &raw_sid);
        }
        if (FAILED(result)) {
            throw std::system_error(
                result,
                std::generic_category(),
                "CreateAppContainerProfile");
        }
        sid_ = ScopedSid(raw_sid);
        active_ = true;
    }

    ~ProfileScope() noexcept {
        Delete();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

    PSID Sid() const noexcept {
        return sid_.get();
    }

    const std::wstring& Moniker() const noexcept {
        return moniker_;
    }

    HRESULT Delete() noexcept {
        if (!active_) {
            return S_OK;
        }
        const HRESULT result =
            ::DeleteAppContainerProfile(
                moniker_.c_str());
        if (SUCCEEDED(result)) {
            active_ = false;
        }
        return result;
    }

private:
    std::wstring moniker_;
    ScopedSid sid_;
    bool active_ = false;
};

class LauncherMutex final {
public:
    explicit LauncherMutex(std::wstring name) noexcept
        : handle_(::CreateMutexW(
              nullptr,
              FALSE,
              name.c_str())) {
    }

    ~LauncherMutex() noexcept {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
    }

    bool Acquire() const noexcept {
        if (!handle_) {
            return false;
        }
        return ::WaitForSingleObject(
                   handle_,
                   0) ==
            WAIT_OBJECT_0;
    }

private:
    HANDLE handle_ = nullptr;
};

class AttributeListScope final {
public:
    explicit AttributeListScope(
        LPPROC_THREAD_ATTRIBUTE_LIST list) noexcept
        : list_(list) {
    }

    ~AttributeListScope() noexcept {
        if (list_) {
            ::DeleteProcThreadAttributeList(list_);
        }
    }

    AttributeListScope(const AttributeListScope&) = delete;
    AttributeListScope& operator=(
        const AttributeListScope&) = delete;

private:
    LPPROC_THREAD_ATTRIBUTE_LIST list_ =
        nullptr;
};

void RunHost(
    const std::filesystem::path& host_executable,
    const Mode& mode,
    std::wstring_view host_option) {
    CapabilitySids capabilities;
    const HRESULT capability_result =
        capabilities.Build(mode.capability);
    if (FAILED(capability_result)) {
        throw std::system_error(
            capability_result,
            std::generic_category(),
            "DeriveCapabilitySidsFromName");
    }

    ProfileScope profile(
        mode.moniker,
        capabilities);
    const std::wstring sid_string =
        SidToString(profile.Sid());
    const std::filesystem::path launched_executable =
        ProfileExecutablePath(
            host_executable,
            sid_string);

    SECURITY_CAPABILITIES security_capabilities{
        .AppContainerSid = profile.Sid(),
        .Capabilities = capabilities.Data(),
        .CapabilityCount = capabilities.Count(),
        .Reserved = 0,
    };
    DWORD all_application_packages_policy =
        PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;

    SIZE_T attribute_size = 0;
    const DWORD attribute_count =
        mode.opt_out_all_application_packages
            ? 2
            : 1;
    ::InitializeProcThreadAttributeList(
        nullptr,
        attribute_count,
        0,
        &attribute_size);
    if (::GetLastError() !=
        ERROR_INSUFFICIENT_BUFFER) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "InitializeProcThreadAttributeList(size)");
    }
    std::vector<std::byte> attribute_storage(
        attribute_size);
    auto* attribute_list =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            attribute_storage.data());
    if (!::InitializeProcThreadAttributeList(
            attribute_list,
            attribute_count,
            0,
            &attribute_size)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "InitializeProcThreadAttributeList");
    }
    const AttributeListScope attribute_scope(
        attribute_list);
    if (!::UpdateProcThreadAttribute(
            attribute_list,
            0,
            PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
            &security_capabilities,
            sizeof(security_capabilities),
            nullptr,
            nullptr)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "UpdateProcThreadAttribute(SECURITY_CAPABILITIES)");
    }
    if (mode.opt_out_all_application_packages) {
        if (!::UpdateProcThreadAttribute(
                attribute_list,
                0,
                PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
                &all_application_packages_policy,
                sizeof(all_application_packages_policy),
                nullptr,
                nullptr)) {
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "UpdateProcThreadAttribute(ALL_APPLICATION_PACKAGES)");
        }
    }

    STARTUPINFOEXW startup{
        .StartupInfo = {},
    };
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attribute_list;
    std::wstring command =
        L"\"" +
        launched_executable.wstring() +
        L"\" --profile=" +
        mode.argument;
    if (!host_option.empty()) {
        command += L" ";
        command += host_option;
    }
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(
            launched_executable.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            EXTENDED_STARTUPINFO_PRESENT |
                CREATE_SUSPENDED,
            nullptr,
            launched_executable.parent_path().c_str(),
            &startup.StartupInfo,
            &process)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "CreateProcessW");
    }
    const std::unique_ptr<void, decltype([](void* handle) noexcept {
        if (handle) {
            ::CloseHandle(static_cast<HANDLE>(handle));
        }
    })> process_handle(process.hProcess);
    const std::unique_ptr<void, decltype([](void* handle) noexcept {
        if (handle) {
            ::CloseHandle(static_cast<HANDLE>(handle));
        }
    })> thread_handle(process.hThread);
    const std::unique_ptr<void, decltype([](void* handle) noexcept {
        if (handle) {
            ::CloseHandle(static_cast<HANDLE>(handle));
        }
    })> job_handle(CreateJob());
    if (!::AssignProcessToJobObject(
            job_handle.get(),
            process_handle.get())) {
        ::TerminateProcess(process_handle.get(), 1);
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "AssignProcessToJobObject");
    }
    if (::ResumeThread(thread_handle.get()) ==
        MAXDWORD) {
        ::TerminateProcess(process_handle.get(), 1);
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "ResumeThread");
    }

    std::wcout
        << L"Started AppContainer permission host: profile="
        << mode.argument
        << L", pid="
        << process.dwProcessId
        << L"\n";
    const DWORD wait =
        ::WaitForSingleObject(
            process_handle.get(),
            INFINITE);
    if (wait != WAIT_OBJECT_0) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "WaitForSingleObject");
    }
    DWORD exit_code = 0;
    if (!::GetExitCodeProcess(
            process_handle.get(),
            &exit_code)) {
        throw std::system_error(
            static_cast<int>(::GetLastError()),
            std::system_category(),
            "GetExitCodeProcess");
    }
    if (exit_code != 0) {
        throw std::runtime_error(std::format(
            "AppContainer permission host exited with code {}",
            exit_code));
    }
    const HRESULT cleanup_result =
        profile.Delete();
    if (FAILED(cleanup_result)) {
        throw std::system_error(
            cleanup_result,
            std::generic_category(),
            "DeleteAppContainerProfile");
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        if (argc > 3 ||
            (argc == 3 &&
             std::wstring_view(argv[2]) != L"--smoke" &&
             std::wstring_view(argv[2]) != L"--broker-smoke")) {
            std::wcerr
                << L"Usage: QiQiImePermissionLauncher.exe "
                   L"[broadGroupCom|packageOnlyCom|"
                   L"zero|registryRead|lpacCom] "
                   L"[--smoke|--broker-smoke]\n";
            return 2;
        }
        const Mode mode = ResolveMode(
            argc >= 2
                ? argv[1]
                : L"packageOnlyCom");
        const LauncherMutex launcher_mutex(
            L"Local\\QiQiImeAppContainerPermissions." +
            mode.argument);
        if (!launcher_mutex.Acquire()) {
            std::wcerr
                << L"A launcher for profile "
                << mode.argument
                << L" is already running.\n";
            return 3;
        }
        const std::filesystem::path launcher =
            CurrentExecutablePath();
        const std::filesystem::path host =
            launcher.parent_path() /
            mode.host_executable;
        if (!std::filesystem::exists(host)) {
            std::wcerr
                << L"Missing "
                << mode.host_executable
                << L" next to the launcher.\n";
            return 4;
        }
        RunHost(
            host,
            mode,
            argc == 3
                ? std::wstring_view(
                      argv[2])
                : std::wstring_view{});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Permission launcher failed: "
                  << error.what()
                  << '\n';
        return 1;
    }
}
