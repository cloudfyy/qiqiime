// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.h"
#include "cleanup.h"
#include "wizard.h"
#include "common/RAII.h"

#include <msiquery.h>
#include <objbase.h>
#include <gsl/gsl>
#include <array>
#include <algorithm>
#include <filesystem>
#include <format>
#include <new>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace qiqi::installer {
namespace {

constexpr std::wstring_view kExplicitUninstall = L"Installed AND REMOVE=\"ALL\" AND NOT UPGRADINGPRODUCTCODE";

UINT ReportFailure(MSIHANDLE install, const wchar_t* operation, HRESULT error) noexcept {
    const MSIHANDLE record = ::MsiCreateRecord(2);
    const auto close = gsl::finally([record]() noexcept {
        if (record) ::MsiCloseHandle(record);
    });
    if (!record ||
        ::MsiRecordSetStringW(record, 0, L"QiQiIME installer: [1] failed (HRESULT=[2]).") != ERROR_SUCCESS ||
        ::MsiRecordSetStringW(record, 1, operation) != ERROR_SUCCESS ||
        ::MsiRecordSetInteger(record, 2, error) != ERROR_SUCCESS ||
        ::MsiProcessMessage(install, INSTALLMESSAGE_INFO, record) == -1) {
        ::OutputDebugStringW(L"QiQiIME installer failure; MSI diagnostic delivery also failed.\n");
    }
    return ERROR_INSTALL_FAILURE;
}

HRESULT Property(MSIHANDLE install, const wchar_t* name, std::wstring& value) {
    DWORD length = 0;
    wchar_t empty = L'\0';
    const UINT sized = ::MsiGetPropertyW(install, name, &empty, &length);
    if (sized != ERROR_MORE_DATA && sized != ERROR_SUCCESS) return HRESULT_FROM_WIN32(sized);
    value.resize(gsl::narrow_cast<size_t>(length) + 1);
    DWORD capacity = gsl::narrow<DWORD>(value.size());
    const UINT read = ::MsiGetPropertyW(install, name, value.data(), &capacity);
    if (read != ERROR_SUCCESS) return HRESULT_FROM_WIN32(read);
    value.resize(capacity);
    return S_OK;
}

HRESULT SetProperty(MSIHANDLE install, const wchar_t* name, const wchar_t* value) noexcept {
    return HRESULT_FROM_WIN32(::MsiSetPropertyW(install, name, value));
}

HRESULT Condition(MSIHANDLE install, const wchar_t* text, bool& matches) noexcept {
    const MSICONDITION result = ::MsiEvaluateConditionW(install, text);
    matches = result == MSICONDITION_TRUE;
    return result == MSICONDITION_TRUE || result == MSICONDITION_FALSE ? S_OK : E_INVALIDARG;
}

HRESULT ReadOptions(MSIHANDLE install, CleanupRequest& request) {
    std::wstring legacy;
    std::wstring configuration;
    std::wstring dictionaries;
    HRESULT hr = Property(install, L"QIQI_REMOVE_USER_DATA", legacy);
    if (SUCCEEDED(hr)) hr = Property(install, L"QIQI_REMOVE_CONFIGURATION", configuration);
    if (SUCCEEDED(hr)) hr = Property(install, L"QIQI_REMOVE_DICTIONARIES", dictionaries);
    if (FAILED(hr)) return hr;
    return ParseCleanupChoices(legacy, configuration, dictionaries, request);
}

HRESULT ReadRequest(MSIHANDLE install, CleanupRequest& request) {
    HRESULT hr = ReadOptions(install, request);
    if (SUCCEEDED(hr)) hr = Property(install, L"UserSID", request.sid);
    if (SUCCEEDED(hr)) hr = Property(install, L"QiQiCleanupTransaction", request.transaction);
    return SUCCEEDED(hr) ? ValidateRequest(request) : hr;
}

HRESULT NormalizeInstallationDirectory(std::wstring_view value, std::wstring& normalized) {
    const std::filesystem::path directory(value);
    if (!directory.is_absolute() || directory == directory.root_path() ||
        directory.native().find_first_of(L"|\r\n") != std::wstring::npos) return E_INVALIDARG;
    normalized = directory.lexically_normal().wstring();
    return S_OK;
}

class RestartManagerSession final {
public:
    RestartManagerSession() = default;
    ~RestartManagerSession() noexcept {
        if (session_) {
            std::ignore = ::RmEndSession(session_);
        }
    }
    RestartManagerSession(const RestartManagerSession&) = delete;
    RestartManagerSession& operator=(const RestartManagerSession&) = delete;
    RestartManagerSession(RestartManagerSession&&) = delete;
    RestartManagerSession& operator=(RestartManagerSession&&) = delete;

    HRESULT Start() noexcept {
        std::array<wchar_t, CCH_RM_SESSION_KEY + 1> key{};
        const DWORD error = ::RmStartSession(
            &session_, 0, key.data());
        return HRESULT_FROM_WIN32(error);
    }

    DWORD Get() const noexcept {
        return session_;
    }

private:
    DWORD session_ = 0;
};

HRESULT DetectPendingProgramRestart(
    std::wstring_view installation_directory,
    bool& required) {
    required = false;
    ScopedRegKey key;
    LSTATUS status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_QUERY_VALUE, key.Put());
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    DWORD type = 0;
    DWORD bytes = 0;
    status = ::RegQueryValueExW(key.Get(), L"PendingFileRenameOperations", nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return S_OK;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    if (type != REG_MULTI_SZ || bytes % sizeof(wchar_t) != 0 || bytes > 1024 * 1024) return E_INVALIDARG;
    std::wstring paths(bytes / sizeof(wchar_t), L'\0');
    status = ::RegQueryValueExW(key.Get(), L"PendingFileRenameOperations", nullptr, &type,
        reinterpret_cast<BYTE*>(paths.data()), &bytes);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    if (type != REG_MULTI_SZ || bytes < 2 * sizeof(wchar_t) || bytes > paths.size() * sizeof(wchar_t) ||
        bytes % sizeof(wchar_t) != 0) return E_INVALIDARG;
    std::wstring_view remaining(paths.data(), bytes / sizeof(wchar_t));
    while (!remaining.empty() && remaining.front() != L'\0') {
        for (unsigned int part = 0; part < 2; ++part) {
            const auto end = remaining.find(L'\0');
            if (end == std::wstring_view::npos) return E_INVALIDARG;
            required = required || IsPendingProgramFile(remaining.substr(0, end), installation_directory);
            remaining.remove_prefix(end + 1);
        }
    }
    return !remaining.empty() &&
        std::all_of(remaining.begin(), remaining.end(), [](wchar_t character) noexcept { return character == L'\0'; })
        ? S_OK : E_INVALIDARG;
}

HRESULT QueryProcessExecutable(
    DWORD process_id,
    std::wstring& output) {
    const ScopedKernelHandle process(
        ::OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            process_id));
    if (!process) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    std::array<wchar_t, 32768> buffer{};
    DWORD length =
        gsl::narrow_cast<DWORD>(
            buffer.size());
    if (!::QueryFullProcessImageNameW(
            process.Get(),
            0,
            buffer.data(),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    output.assign(
        buffer.data(),
        length);
    return S_OK;
}

HRESULT DetectLoadedProgramRestart(
    std::wstring_view installation_directory,
    bool& required) {
    required = false;
    RestartManagerSession session;
    HRESULT hr = session.Start();
    if (FAILED(hr)) {
        return hr;
    }
    const auto directory =
        std::filesystem::path(
            installation_directory);
    const std::array<std::wstring, 2>
        files{{
            (directory / L"x64" /
                L"qiqiime.dll").wstring(),
            (directory / L"x86" /
                L"qiqiime.dll").wstring(),
        }};
    std::array<const wchar_t*, 2>
        resources{{
            gsl::at(files, 0).c_str(),
            gsl::at(files, 1).c_str(),
        }};
    const DWORD register_error =
        ::RmRegisterResources(
            session.Get(),
            gsl::narrow_cast<UINT>(
                resources.size()),
            resources.data(),
            0,
            nullptr,
            0,
            nullptr);
    if (register_error !=
        ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(
            register_error);
    }

    std::array<wchar_t, 32768>
        windows{};
    const UINT windows_length =
        ::GetWindowsDirectoryW(
            windows.data(),
            gsl::narrow_cast<UINT>(
                windows.size()));
    if (windows_length == 0 ||
        windows_length >=
            windows.size()) {
        return HRESULT_FROM_WIN32(
            windows_length == 0
                ? ::GetLastError()
                : ERROR_INSUFFICIENT_BUFFER);
    }
    const std::wstring_view
        windows_directory(
            windows.data(),
            windows_length);

    std::vector<RM_PROCESS_INFO>
        processes;
    DWORD reboot_reasons =
        RmRebootReasonNone;
    bool listed = false;
    for (unsigned int attempt = 0;
         attempt < 3;
         ++attempt) {
        UINT needed = 0;
        UINT count = 0;
        DWORD list_error =
            ::RmGetList(
                session.Get(),
                &needed,
                &count,
                nullptr,
                &reboot_reasons);
        if (list_error !=
                ERROR_SUCCESS &&
            list_error !=
                ERROR_MORE_DATA) {
            return HRESULT_FROM_WIN32(
                list_error);
        }
        if (needed == 0) {
            required =
                reboot_reasons !=
                RmRebootReasonNone;
            return S_OK;
        }
        if (needed > 4096) {
            return HRESULT_FROM_WIN32(
                ERROR_OUTOFMEMORY);
        }
        processes.resize(needed);
        count =
            gsl::narrow_cast<UINT>(
                processes.size());
        list_error =
            ::RmGetList(
                session.Get(),
                &needed,
                &count,
                processes.data(),
                &reboot_reasons);
        if (list_error ==
            ERROR_MORE_DATA) {
            continue;
        }
        if (list_error !=
            ERROR_SUCCESS) {
            return HRESULT_FROM_WIN32(
                list_error);
        }
        processes.resize(count);
        listed = true;
        break;
    }
    if (!listed) {
        return HRESULT_FROM_WIN32(
            ERROR_MORE_DATA);
    }
    if (reboot_reasons !=
        RmRebootReasonNone) {
        required = true;
        return S_OK;
    }
    for (const RM_PROCESS_INFO& process :
         processes) {
        std::wstring executable;
        hr = QueryProcessExecutable(
            process.Process.dwProcessId,
            executable);
        if (FAILED(hr)) {
            const DWORD error =
                HRESULT_CODE(hr);
            if (error ==
                    ERROR_INVALID_PARAMETER ||
                error ==
                    ERROR_NOT_FOUND) {
                continue;
            }
            required = true;
            return S_OK;
        }
        if (RequiresRestartForProgramLock(
                process.ApplicationType,
                process.bRestartable !=
                    FALSE,
                executable,
                windows_directory)) {
            required = true;
            return S_OK;
        }
    }
    return S_OK;
}

HRESULT DetectProgramRestart(
    std::wstring_view installation_directory,
    bool& required) {
    bool pending = false;
    HRESULT result =
        DetectPendingProgramRestart(
            installation_directory,
            pending);
    if (FAILED(result)) {
        return result;
    }
    bool loaded = false;
    result =
        DetectLoadedProgramRestart(
            installation_directory,
            loaded);
    required = pending || loaded;
    return result;
}

UINT Message(MSIHANDLE install, const wchar_t* property, UINT style, const CleanupResult& result) {
    std::wstring text;
    const HRESULT hr = Property(install, property, text);
    if (FAILED(hr) || text.empty()) return ReportFailure(install, L"LocalizedMessage", FAILED(hr) ? hr : E_INVALIDARG);
    PMSIHANDLE record = ::MsiCreateRecord(4);
    if (!record) return ReportFailure(install, L"MsiCreateRecord", E_OUTOFMEMORY);
    UINT error = ::MsiRecordSetStringW(record, 0, text.c_str());
    if (error == ERROR_SUCCESS) error = ::MsiRecordSetInteger(record, 1, gsl::narrow<int>(result.deleted));
    if (error == ERROR_SUCCESS) error = ::MsiRecordSetInteger(record, 2, gsl::narrow<int>(result.scheduled));
    if (error == ERROR_SUCCESS) {
        const auto code = std::format(L"0x{:08X}", gsl::narrow_cast<uint32_t>(result.error));
        error = ::MsiRecordSetStringW(record, 3, code.c_str());
    }
    if (error == ERROR_SUCCESS) {
        error = ::MsiRecordSetStringW(record, 4, CleanupOperationName(result.operation));
    }
    if (error != ERROR_SUCCESS) return ReportFailure(install, L"MsiRecordSet", HRESULT_FROM_WIN32(error));
    const int delivered = ::MsiProcessMessage(install, static_cast<INSTALLMESSAGE>(style), record);
    return delivered == -1 ? ReportFailure(install, L"MsiProcessMessage", E_FAIL) : ERROR_SUCCESS;
}

template<typename Action>
UINT AtMsiBoundary(MSIHANDLE install, const wchar_t* operation, Action&& action) noexcept {
    try {
        return action();
    } catch (const std::bad_alloc&) {
        return ReportFailure(install, operation, E_OUTOFMEMORY);
    } catch (const std::system_error& error) {
        return ReportFailure(install, operation, HRESULT_FROM_WIN32(error.code().value()));
    } catch (...) {
        // No C++ exception may cross the Windows Installer ABI.
        return ReportFailure(install, operation, E_UNEXPECTED);
    }
}

} // namespace

bool RequiresRestartForProgramLock(
    RM_APP_TYPE application_type,
    bool restartable,
    std::wstring_view executable,
    std::wstring_view windows_directory) noexcept {
    if (!restartable ||
        application_type ==
            RmCritical ||
        application_type ==
            RmService ||
        application_type ==
            RmExplorer) {
        return true;
    }
    if (executable.size() < 2 ||
        windows_directory.empty()) {
        return false;
    }
    if (executable.starts_with(
            L"\\\\?\\")) {
        executable.remove_prefix(4);
    }
    if (windows_directory.starts_with(
            L"\\\\?\\")) {
        windows_directory.remove_prefix(4);
    }
    while (!windows_directory.empty() &&
           (windows_directory.back() ==
                L'\\' ||
            windows_directory.back() ==
                L'/')) {
        windows_directory.remove_suffix(
            1);
    }
    if (executable.size() <=
            windows_directory.size() ||
        ::CompareStringOrdinal(
            executable.data(),
            gsl::narrow_cast<int>(
                windows_directory.size()),
            windows_directory.data(),
            gsl::narrow_cast<int>(
                windows_directory.size()),
            TRUE) != CSTR_EQUAL) {
        return false;
    }
    const std::wstring_view remainder =
        executable.substr(
            windows_directory.size());
    return remainder.starts_with(
               L"\\") ||
           remainder.starts_with(
               L"/");
}

HRESULT ParseCleanupChoices(std::wstring_view legacy, std::wstring_view configuration,
                            std::wstring_view dictionaries, CleanupRequest& request) noexcept {
    const auto valid = [](std::wstring_view value) noexcept {
        return value.empty() || value == L"0" || value == L"1";
    };
    if (!valid(legacy) || !valid(configuration) || !valid(dictionaries)) return E_INVALIDARG;
    request.configuration = configuration.empty() ? legacy == L"1" : configuration == L"1";
    request.dictionaries = dictionaries.empty() ? legacy == L"1" : dictionaries == L"1";
    return S_OK;
}

HRESULT SerializeCleanupRequest(const CleanupRequest& request, std::wstring& serialized,
                                std::wstring_view installation_directory) {
    const HRESULT valid = ValidateRequest(request);
    if (FAILED(valid)) return valid;
    serialized = request.sid + L"|" + request.transaction + L"|" +
        (request.configuration ? L"1" : L"0") + (request.dictionaries ? L"1" : L"0");
    if (!installation_directory.empty()) {
        std::wstring directory;
        const HRESULT error = NormalizeInstallationDirectory(installation_directory, directory);
        if (FAILED(error)) return error;
        serialized += L"|" + directory;
    }
    return S_OK;
}

HRESULT ParseCleanupRequest(std::wstring_view serialized, CleanupRequest& request,
                            std::wstring* installation_directory) {
    const auto first = serialized.find(L'|');
    const auto second = first == std::wstring_view::npos ? first : serialized.find(L'|', first + 1);
    if (first == std::wstring_view::npos || second == std::wstring_view::npos ||
        serialized.size() < second + 3) return E_INVALIDARG;
    request.sid = serialized.substr(0, first);
    request.transaction = serialized.substr(first + 1, second - first - 1);
    const auto flags = serialized.substr(second + 1, 2);
    if ((flags.front() != L'0' && flags.front() != L'1') ||
        (flags.back() != L'0' && flags.back() != L'1')) return E_INVALIDARG;
    request.configuration = flags.front() == L'1';
    request.dictionaries = flags.back() == L'1';
    std::wstring directory;
    if (serialized.size() != second + 3) {
        if (serialized.at(second + 3) != L'|' || serialized.size() == second + 4) return E_INVALIDARG;
        const HRESULT error = NormalizeInstallationDirectory(serialized.substr(second + 4), directory);
        if (FAILED(error)) return error;
    }
    if (installation_directory) *installation_directory = std::move(directory);
    return ValidateRequest(request);
}

bool IsPendingProgramFile(std::wstring_view path, std::wstring_view installation_directory) {
    if (path.starts_with(L"!")) path.remove_prefix(1);
    if (path.starts_with(L"\\??\\") || path.starts_with(L"\\\\?\\")) path.remove_prefix(4);
    if (path.empty() || installation_directory.empty()) return false;
    auto directory = std::filesystem::path(installation_directory).lexically_normal().wstring();
    while (!directory.empty() && (directory.back() == L'\\' || directory.back() == L'/')) directory.pop_back();
    directory += L"\\";
    const auto normalized = std::filesystem::path(path).lexically_normal().wstring();
    if (normalized.size() <= directory.size() ||
        ::CompareStringOrdinal(normalized.data(), gsl::narrow<int>(directory.size()),
            directory.data(), gsl::narrow<int>(directory.size()), TRUE) != CSTR_EQUAL) return false;
    const auto relative = std::wstring_view(normalized).substr(directory.size());
    for (const auto* expected : {L"x64\\qiqiime.dll", L"x86\\qiqiime.dll",
            L"x64\\QiQiIMESettings.exe", L"x86\\QiQiIMESettings.exe",
            L"x64\\QiQiIMELogMonitor.exe", L"x86\\QiQiIMELogMonitor.exe",
            L"x64\\QiQiIMEDictManager.exe", L"x86\\QiQiIMEDictManager.exe",
            L"data\\wubi86_jidian.txt", L"data\\wubi86_jidian_extra.txt", L"data\\wubi86_haifeng.txt",
            L"data\\wubi86_jidian.qdict", L"data\\wubi86_jidian_extra.qdict", L"data\\wubi86_haifeng.qdict"}) {
        if (::CompareStringOrdinal(relative.data(), gsl::narrow<int>(relative.size()), expected, -1, TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

} // namespace qiqi::installer

extern "C" UINT __stdcall ChooseUninstallData(MSIHANDLE install) noexcept {
    using namespace qiqi::installer;
    return AtMsiBoundary(install, L"ChooseUninstallData", [&]() -> UINT {
        bool show = false;
        HRESULT hr = Condition(install,
            L"Installed AND REMOVE=\"ALL\" AND NOT UPGRADINGPRODUCTCODE "
            L"AND UILevel >= 3 AND NOT QIQI_WIZARD_SHOWN", show);
        if (FAILED(hr)) return ReportFailure(install, L"WizardCondition", hr);
        if (!show) return ERROR_SUCCESS;
        CleanupRequest choices;
        hr = ReadOptions(install, choices);
        WizardText text;
        for (const auto& field : {
                std::pair{L"QIQI_WIZARD_TITLE", &text.title},
                std::pair{L"QIQI_WIZARD_DESCRIPTION", &text.description},
                std::pair{L"QIQI_WIZARD_CONFIGURATION", &text.configuration},
                std::pair{L"QIQI_WIZARD_DICTIONARIES", &text.dictionaries},
                std::pair{L"QIQI_WIZARD_WARNING", &text.warning},
                std::pair{L"QIQI_WIZARD_REMOVE", &text.remove},
                std::pair{L"QIQI_WIZARD_CANCEL", &text.cancel}}) {
            if (SUCCEEDED(hr)) hr = Property(install, field.first, *field.second);
            if (SUCCEEDED(hr) && field.second->empty()) hr = E_INVALIDARG;
        }
        if (FAILED(hr)) return ReportFailure(install, L"WizardProperties", hr);
        std::wstring product_name;
        hr = Property(install, L"ProductName", product_name);
        if (FAILED(hr) || product_name.empty()) {
            return ReportFailure(install, L"WizardProductName", FAILED(hr) ? hr : E_INVALIDARG);
        }
        bool accepted = false;
        hr = ShowDataChoices(text, choices.configuration, choices.dictionaries, accepted,
            FindInstallerOwner(product_name.c_str()));
        if (FAILED(hr)) return ReportFailure(install, L"ShowDataChoices", hr);
        if (!accepted) return ERROR_INSTALL_USEREXIT;
        hr = SetProperty(install, L"QIQI_REMOVE_CONFIGURATION", choices.configuration ? L"1" : L"0");
        if (SUCCEEDED(hr)) hr = SetProperty(install, L"QIQI_REMOVE_DICTIONARIES", choices.dictionaries ? L"1" : L"0");
        if (SUCCEEDED(hr)) hr = SetProperty(install, L"QIQI_WIZARD_SHOWN", L"1");
        return SUCCEEDED(hr) ? ERROR_SUCCESS : ReportFailure(install, L"WizardSelection", hr);
    });
}

extern "C" UINT __stdcall PrepareUninstall(MSIHANDLE install) noexcept {
    using namespace qiqi::installer;
    return AtMsiBoundary(install, L"PrepareUninstall", [&]() -> UINT {
        bool explicit_uninstall = false;
        HRESULT hr = Condition(install, kExplicitUninstall.data(), explicit_uninstall);
        if (FAILED(hr)) return ReportFailure(install, L"PrepareCondition", hr);
        if (!explicit_uninstall) return ERROR_SUCCESS;
        CleanupRequest request;
        hr = ReadOptions(install, request);
        if (FAILED(hr)) return ReportFailure(install, L"CleanupChoices", hr);
        GUID guid{};
        hr = ::CoCreateGuid(&guid);
        std::array<wchar_t, 39> transaction{};
        if (SUCCEEDED(hr) && ::StringFromGUID2(guid, transaction.data(), gsl::narrow_cast<int>(transaction.size())) == 0) {
            hr = E_UNEXPECTED;
        }
        if (SUCCEEDED(hr)) hr = SetProperty(install, L"QiQiCleanupTransaction", transaction.data());
        if (SUCCEEDED(hr)) hr = ReadRequest(install, request);
        std::wstring serialized;
        std::wstring directory;
        if (SUCCEEDED(hr)) {
            hr = Property(
                install,
                L"INSTALLFOLDER",
                directory);
        }
        if (SUCCEEDED(hr) &&
            directory.empty()) {
            hr = E_INVALIDARG;
        }
        bool program_restart = false;
        if (SUCCEEDED(hr)) {
            hr =
                DetectProgramRestart(
                    directory,
                    program_restart);
        }
        if (SUCCEEDED(hr)) {
            hr = SetProperty(
                install,
                L"QIQI_SYSTEM_RESTART_REQUIRED",
                program_restart
                    ? L"1"
                    : nullptr);
        }
        if (SUCCEEDED(hr) &&
            program_restart) {
            const UINT mode =
                ::MsiSetMode(
                    install,
                    MSIRUNMODE_REBOOTATEND,
                    TRUE);
            hr = HRESULT_FROM_WIN32(
                mode);
        }
        if (SUCCEEDED(hr)) {
            hr =
                SerializeCleanupRequest(
                    request,
                    serialized,
                    directory);
        }
        if (SUCCEEDED(hr)) {
            hr = SetProperty(
                install,
                L"ExecuteCleanup",
                serialized.c_str());
        }
        return SUCCEEDED(hr) ? ERROR_SUCCESS : ReportFailure(install, L"PrepareCleanupRequest", hr);
    });
}

extern "C" UINT __stdcall ExecuteCleanup(MSIHANDLE install) noexcept {
    using namespace qiqi::installer;
    return AtMsiBoundary(install, L"ExecuteCleanup", [&]() -> UINT {
        std::wstring serialized;
        std::wstring directory;
        CleanupRequest request;
        HRESULT hr = Property(install, L"CustomActionData", serialized);
        if (SUCCEEDED(hr)) hr = ParseCleanupRequest(serialized, request, &directory);
        if (SUCCEEDED(hr) && directory.empty()) hr = E_INVALIDARG;
        if (FAILED(hr)) return ReportFailure(install, L"DeferredCleanupRequest", hr);
        bool program_restart = false;
        hr = DetectProgramRestart(directory, program_restart);
        if (FAILED(hr)) return ReportFailure(install, L"PendingProgramFiles", hr);
        const CleanupResult result = PerformCleanup(request, program_restart);
        if (FAILED(result.error)) {
            return ReportFailure(install, CleanupOperationName(result.operation), result.error);
        }
        return ERROR_SUCCESS;
    });
}

extern "C" UINT __stdcall CompleteUninstall(MSIHANDLE install) noexcept {
    using namespace qiqi::installer;
    return AtMsiBoundary(install, L"CompleteUninstall", [&]() -> UINT {
        bool explicit_uninstall = false;
        HRESULT hr = Condition(install, kExplicitUninstall.data(), explicit_uninstall);
        if (FAILED(hr)) return ReportFailure(install, L"CompletionCondition", hr);
        if (!explicit_uninstall) return ERROR_SUCCESS;
        CleanupRequest request;
        CleanupResult result;
        hr = ReadOptions(install, request);
        if (SUCCEEDED(hr)) {
            hr = ReadRequest(install, request);
            if (SUCCEEDED(hr)) hr = ReadCleanupResult(request, result);
        }
        if (FAILED(hr)) result.error = hr;
        if (result.restart_required) {
            const UINT mode = ::MsiSetMode(install, MSIRUNMODE_REBOOTATEND, TRUE);
            if (mode != ERROR_SUCCESS) result.error = HRESULT_FROM_WIN32(mode);
        }
        if (!ShouldPromptUninstallResult(
                result.error,
                result.restart_required,
                result.scheduled)) {
            return ERROR_SUCCESS;
        }
        const wchar_t* message =
            FAILED(result.error)
                ? L"QIQI_COMPLETE_INCOMPLETE"
                : L"QIQI_COMPLETE_RESTART";
        bool interactive = false;
        hr = Condition(install, L"UILevel >= 3", interactive);
        if (FAILED(hr)) return ReportFailure(install, L"CompletionUiLevel", hr);
        const UINT delivered = Message(install, message,
            interactive ? INSTALLMESSAGE_USER | MB_OK |
                (FAILED(result.error) ? MB_ICONWARNING : MB_ICONINFORMATION) : INSTALLMESSAGE_INFO, result);
        if (delivered != ERROR_SUCCESS) return delivered;
        // This action runs after InstallFinalize: report partial cleanup without undoing program removal.
        if (FAILED(result.error)) {
            return ReportFailure(install, CleanupOperationName(result.operation), result.error);
        }
        return ERROR_SUCCESS;
    });
}

extern "C" UINT __stdcall CheckInstallPendingCleanup(MSIHANDLE install) noexcept {
    using namespace qiqi::installer;
    return AtMsiBoundary(install, L"CheckInstallPendingCleanup", [&]() -> UINT {
        bool installing = false;
        HRESULT hr = Condition(install, L"NOT (REMOVE=\"ALL\")", installing);
        if (FAILED(hr)) return ReportFailure(install, L"InstallCondition", hr);
        if (!installing) return ERROR_SUCCESS;
        PendingState state = PendingState::clear;
        hr = CheckPendingCleanup(state);
        if (SUCCEEDED(hr) && state == PendingState::clear) return ERROR_SUCCESS;
        CleanupResult result;
        result.error = hr;
        const UINT delivered = Message(install,
            SUCCEEDED(hr) && state == PendingState::restart_required
                ? L"QIQI_INSTALL_RESTART" : L"QIQI_INSTALL_INCOMPLETE",
            INSTALLMESSAGE_ERROR | MB_OK | MB_ICONWARNING, result);
        if (delivered != ERROR_SUCCESS) return delivered;
        return ReportFailure(install, L"InstallPendingCleanup",
            FAILED(hr) ? hr : HRESULT_FROM_WIN32(state == PendingState::restart_required
                ? ERROR_FAIL_REBOOT_REQUIRED : ERROR_INSTALL_FAILURE));
    });
}
