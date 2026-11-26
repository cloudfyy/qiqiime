// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configbrokercontrol.h"

#include "common/RAII.h"
#include "common/comobj.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace qiqi::config {

BrokerHandoffReason SelectBrokerHandoff(
    bool mutation,
    HRESULT result,
    bool handoff_attempted,
    bool app_container) noexcept {
    if (handoff_attempted ||
        app_container) {
        return BrokerHandoffReason::None;
    }
    if (mutation &&
        result ==
            kBrokerMutationHandoffRequired) {
        return BrokerHandoffReason::
            MutationRequired;
    }
    if (result ==
        REGDB_E_CLASSNOTREG) {
        return BrokerHandoffReason::
            ActivationUnavailable;
    }
    return BrokerHandoffReason::None;
}

namespace {

constexpr ULONGLONG kHandoffWaitMs = 15'000;
constexpr DWORD kHandoffPollMs = 50;

extern "C" IMAGE_DOS_HEADER __ImageBase;

HRESULT OpenCurrentProcessToken(
    ScopedKernelHandle& token) noexcept {
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            token.Put())) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    return S_OK;
}

HRESULT BrokerTestStoreSubkey(
    std::wstring& output) noexcept {
    output.clear();
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(
            L"QIQI_CONFIG_BROKER_TEST_STORE_SUBKEY",
            buffer.data(),
            gsl::narrow_cast<DWORD>(
                buffer.size()));
    if (length == 0) {
        const DWORD error =
            ::GetLastError();
        return error ==
                ERROR_ENVVAR_NOT_FOUND
            ? S_OK
            : HRESULT_FROM_WIN32(error);
    }
    if (length >= buffer.size()) {
        return HRESULT_FROM_WIN32(
            ERROR_INSUFFICIENT_BUFFER);
    }
    try {
        output.assign(
            buffer.data(),
            length);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT WaitForBrokerExit(
    std::wstring_view store_subkey) {
    const std::wstring name =
        BrokerSessionObjectName(
            L"ConfigBroker",
            store_subkey);
    const ULONGLONG deadline =
        ::GetTickCount64() +
        kHandoffWaitMs;
    for (;;) {
        ScopedKernelHandle mutex(
            ::OpenMutexW(
                SYNCHRONIZE,
                FALSE,
                name.c_str()));
        if (!mutex) {
            const DWORD error =
                ::GetLastError();
            return error ==
                    ERROR_FILE_NOT_FOUND
                ? S_OK
                : HRESULT_FROM_WIN32(error);
        }
        const ULONGLONG now =
            ::GetTickCount64();
        if (now >= deadline) {
            return HRESULT_FROM_WIN32(
                ERROR_TIMEOUT);
        }
        ::Sleep(
            std::min(
                kHandoffPollMs,
                gsl::narrow_cast<DWORD>(
                    deadline - now)));
    }
}

HRESULT EnsureMediumIntegrity(
    HANDLE token) noexcept {
    DWORD integrity_rid = 0;
    HRESULT result =
        TokenIntegrityRid(
            token,
            integrity_rid);
    if (FAILED(result)) {
        return result;
    }
    if (integrity_rid <
        SECURITY_MANDATORY_MEDIUM_RID) {
        return E_ACCESSDENIED;
    }
    if (integrity_rid !=
        SECURITY_MANDATORY_MEDIUM_RID) {
        PSID medium_sid = nullptr;
        if (!::ConvertStringSidToSidW(
                L"S-1-16-8192",
                &medium_sid)) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        const ScopedLocalAlloc
            medium_owner(medium_sid);
        TOKEN_MANDATORY_LABEL label{
            .Label = {
                .Sid = medium_sid,
                .Attributes =
                    SE_GROUP_INTEGRITY,
            },
        };
        if (!::SetTokenInformation(
                token,
                TokenIntegrityLevel,
                &label,
                sizeof(label) +
                    ::GetLengthSid(
                        medium_sid))) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
    }
    result =
        TokenIntegrityRid(
            token,
            integrity_rid);
    if (FAILED(result)) {
        return result;
    }
    return integrity_rid ==
            SECURITY_MANDATORY_MEDIUM_RID
        ? S_OK
        : HRESULT_FROM_WIN32(
              ERROR_BAD_TOKEN_TYPE);
}

HRESULT PrepareBrokerProcessToken(
    HANDLE process,
    std::string_view& failure_stage) noexcept {
    failure_stage = "open-token";
    ScopedKernelHandle token;
    if (!::OpenProcessToken(
            process,
            TOKEN_QUERY |
                TOKEN_ADJUST_DEFAULT |
                TOKEN_ADJUST_GROUPS |
                TOKEN_ADJUST_PRIVILEGES,
            token.Put())) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    failure_stage = "integrity";
    HRESULT result =
        EnsureMediumIntegrity(
            token.Get());
    if (FAILED(result)) {
        return result;
    }

    failure_stage = "disable-privileges";
    ::SetLastError(ERROR_SUCCESS);
    if (!::AdjustTokenPrivileges(
            token.Get(),
            TRUE,
            nullptr,
            0,
            nullptr,
            nullptr)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    failure_stage = "lookup-traverse";
    LUID change_notify{};
    if (!::LookupPrivilegeValueW(
            nullptr,
            SE_CHANGE_NOTIFY_NAME,
            &change_notify)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    failure_stage = "enable-traverse";
    ::SetLastError(ERROR_SUCCESS);
    TOKEN_PRIVILEGES traverse{};
    traverse.PrivilegeCount = 1;
    traverse.Privileges[0].Luid =
        change_notify;
    traverse.Privileges[0].Attributes =
        SE_PRIVILEGE_ENABLED;
    if (!::AdjustTokenPrivileges(
            token.Get(),
            FALSE,
            &traverse,
            0,
            nullptr,
            nullptr)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const DWORD privilege_error =
        ::GetLastError();
    if (privilege_error !=
            ERROR_SUCCESS &&
        privilege_error !=
            ERROR_NOT_ALL_ASSIGNED) {
        return HRESULT_FROM_WIN32(
            privilege_error);
    }
    if (privilege_error ==
        ERROR_NOT_ALL_ASSIGNED) {
        QIQI_LOG_CAT_WARN(
            log::Keyword::Config,
            "Replacement Broker token does "
            "not contain "
            "SeChangeNotifyPrivilege; "
            "continuing with its existing "
            "access checks (error={})",
            privilege_error);
    }
    failure_stage = "complete";
    QIQI_LOG_CAT_INFO(
        log::Keyword::Config,
        "Replacement Broker token "
        "prepared (integrity_rid={})",
        SECURITY_MANDATORY_MEDIUM_RID);
    return S_OK;
}

HRESULT StartReplacementBroker(
    std::wstring_view store_subkey) {
    std::array<wchar_t, 32768> module_path{};
    const DWORD module_length =
        ::GetModuleFileNameW(
            reinterpret_cast<HINSTANCE>(
                &__ImageBase),
            module_path.data(),
            gsl::narrow_cast<DWORD>(
                module_path.size()));
    if (module_length == 0 ||
        module_length >=
            module_path.size()) {
        return HRESULT_FROM_WIN32(
            module_length == 0
                ? ::GetLastError()
                : ERROR_INSUFFICIENT_BUFFER);
    }
    const auto module_directory =
        std::filesystem::path(
            module_path.data())
            .parent_path();
    auto broker_path =
        module_directory /
        L"QiQiIMEConfigBroker.exe";
    BOOL wow64 = FALSE;
    if (::IsWow64Process(
            ::GetCurrentProcess(),
            &wow64) &&
        wow64) {
        const auto native_broker =
            module_directory
                .parent_path() /
            L"x64" /
            L"QiQiIMEConfigBroker.exe";
        if (std::filesystem::exists(
                native_broker)) {
            broker_path = native_broker;
        }
    }
    if (!std::filesystem::exists(
            broker_path)) {
        return HRESULT_FROM_WIN32(
            ERROR_FILE_NOT_FOUND);
    }
    std::wstring command =
        L"\"" +
        broker_path.wstring() +
        L"\" -Embedding";
    if (!store_subkey.empty()) {
        command +=
            L" --store-subkey=\"" +
            std::wstring(
                store_subkey) +
            L"\"";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created =
        ::CreateProcessW(
            broker_path.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW |
                CREATE_SUSPENDED,
            nullptr,
            broker_path
                .parent_path()
                .c_str(),
            &startup,
            &process);
    if (!created) {
        const HRESULT create_result =
            HRESULT_FROM_WIN32(
                ::GetLastError());
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Replacement Broker process "
            "creation failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<
                unsigned long>(
                create_result));
        return create_result;
    }
    const ScopedKernelHandle thread(
        process.hThread);
    const ScopedKernelHandle child(
        process.hProcess);
    std::string_view token_stage =
        "prepare";
    HRESULT prepare_result =
        PrepareBrokerProcessToken(
            child.Get(),
            token_stage);
    if (FAILED(prepare_result)) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Replacement Broker token "
            "preparation failed "
            "(stage={}, hr=0x{:08X})",
            token_stage,
            gsl::narrow_cast<
                unsigned long>(
                prepare_result));
        ::TerminateProcess(
            child.Get(),
            ERROR_ACCESS_DENIED);
        return prepare_result;
    }
    if (::ResumeThread(
            thread.Get()) ==
        (std::numeric_limits<DWORD>::max)()) {
        prepare_result =
            HRESULT_FROM_WIN32(
                ::GetLastError());
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Replacement Broker resume "
            "failed (hr=0x{:08X})",
            gsl::narrow_cast<
                unsigned long>(
                prepare_result));
        ::TerminateProcess(
            child.Get(),
            ERROR_ACCESS_DENIED);
        return prepare_result;
    }
    const std::wstring writable_name =
        BrokerSessionObjectName(
            L"ConfigBrokerWritable",
            store_subkey);
    const ULONGLONG deadline =
        ::GetTickCount64() +
        kHandoffWaitMs;
    for (;;) {
        ScopedKernelHandle writable(
            ::OpenEventW(
                SYNCHRONIZE,
                FALSE,
                writable_name.c_str()));
        if (writable) {
            const DWORD wait =
                ::WaitForSingleObject(
                    writable.Get(),
                    0);
            if (wait == WAIT_OBJECT_0) {
                return S_OK;
            }
            if (wait == WAIT_FAILED) {
                return HRESULT_FROM_WIN32(
                    ::GetLastError());
            }
        } else if (
            ::GetLastError() !=
            ERROR_FILE_NOT_FOUND) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        const DWORD child_wait =
            ::WaitForSingleObject(
                child.Get(),
                0);
        if (child_wait == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            if (!::GetExitCodeProcess(
                    child.Get(),
                    &exit_code)) {
                return HRESULT_FROM_WIN32(
                    ::GetLastError());
            }
            if (exit_code != 0) {
                QIQI_LOG_CAT_ERROR(
                    log::Keyword::Config,
                    "Replacement Broker "
                    "exited during startup "
                    "(exit_code={})",
                    exit_code);
                return E_FAIL;
            }
            QIQI_LOG_CAT_INFO(
                log::Keyword::Config,
                "Replacement Broker "
                "completed without owning "
                "the class factory");
            ::Sleep(kHandoffPollMs);
            continue;
        }
        if (child_wait == WAIT_FAILED) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        const ULONGLONG now =
            ::GetTickCount64();
        if (now >= deadline) {
            return HRESULT_FROM_WIN32(
                ERROR_TIMEOUT);
        }
        ::Sleep(
            std::min(
                kHandoffPollMs,
                gsl::narrow_cast<DWORD>(
                    deadline - now)));
    }
}

} // namespace

std::wstring BrokerSessionObjectName(
    std::wstring_view prefix,
    std::wstring_view store_subkey) {
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(
            ::GetCurrentProcessId(),
            &session_id)) {
        throw std::system_error(
            gsl::narrow_cast<int>(
                ::GetLastError()),
            std::system_category(),
            "ProcessIdToSessionId");
    }
    std::wstring name =
        std::format(
            L"Local\\QiQiIME.{}.{}",
            prefix,
            session_id);
    if (!store_subkey.empty()) {
        uint64_t hash =
            14695981039346656037ull;
        for (const wchar_t character :
             store_subkey) {
            hash ^=
                gsl::narrow_cast<uint16_t>(
                    character);
            hash *=
                1099511628211ull;
        }
        name += std::format(
            L".{:016X}",
            hash);
    }
    return name;
}

HRESULT TokenIntegrityRid(
    HANDLE token,
    DWORD& output) noexcept {
    output = 0;
    DWORD length = 0;
    if (::GetTokenInformation(
            token,
            TokenIntegrityLevel,
            nullptr,
            0,
            &length)) {
        return E_UNEXPECTED;
    }
    const DWORD size_error =
        ::GetLastError();
    if (size_error !=
            ERROR_INSUFFICIENT_BUFFER ||
        length == 0) {
        return HRESULT_FROM_WIN32(
            size_error);
    }
    try {
        std::vector<uint8_t> buffer(
            length);
        if (!::GetTokenInformation(
                token,
                TokenIntegrityLevel,
                buffer.data(),
                length,
                &length)) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        const auto* label =
            reinterpret_cast<
                const TOKEN_MANDATORY_LABEL*>(
                    buffer.data());
        if (!::IsValidSid(
                label->Label.Sid)) {
            return HRESULT_FROM_WIN32(
                ERROR_INVALID_SID);
        }
        const auto* count =
            ::GetSidSubAuthorityCount(
                label->Label.Sid);
        if (!count || *count == 0) {
            return E_UNEXPECTED;
        }
        const auto* authority =
            ::GetSidSubAuthority(
                label->Label.Sid,
                *count - 1);
        if (!authority) {
            return E_UNEXPECTED;
        }
        output = *authority;
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT RequestBrokerHandoff() noexcept {
    ScopedKernelHandle token;
    HRESULT result =
        OpenCurrentProcessToken(token);
    if (FAILED(result)) {
        return result;
    }
    DWORD integrity_rid = 0;
    result = TokenIntegrityRid(
        token.Get(),
        integrity_rid);
    if (FAILED(result)) {
        return result;
    }
    if (integrity_rid <
        SECURITY_MANDATORY_MEDIUM_RID) {
        return E_ACCESSDENIED;
    }
    std::wstring test_store_subkey;
    result = BrokerTestStoreSubkey(
        test_store_subkey);
    if (FAILED(result)) {
        return result;
    }
    try {
        const std::wstring handoff_name =
            BrokerSessionObjectName(
                L"ConfigBrokerHandoff",
                test_store_subkey);
        const ScopedKernelHandle handoff(
            ::CreateMutexW(
                nullptr,
                FALSE,
                handoff_name.c_str()));
        if (!handoff) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        const DWORD handoff_wait =
            ::WaitForSingleObject(
                handoff.Get(),
                gsl::narrow_cast<DWORD>(
                    kHandoffWaitMs));
        if (handoff_wait ==
            WAIT_TIMEOUT) {
            return HRESULT_FROM_WIN32(
                ERROR_TIMEOUT);
        }
        if (handoff_wait !=
                WAIT_OBJECT_0 &&
            handoff_wait !=
                WAIT_ABANDONED) {
            return HRESULT_FROM_WIN32(
                ERROR_INVALID_STATE);
        }
        const std::wstring writable_name =
            BrokerSessionObjectName(
                L"ConfigBrokerWritable",
                test_store_subkey);
        ScopedKernelHandle writable(
            ::OpenEventW(
                SYNCHRONIZE,
                FALSE,
                writable_name.c_str()));
        if (writable) {
            const DWORD writable_wait =
                ::WaitForSingleObject(
                    writable.Get(),
                    0);
            if (writable_wait ==
                WAIT_OBJECT_0) {
                return S_OK;
            }
            if (writable_wait ==
                WAIT_FAILED) {
                return HRESULT_FROM_WIN32(
                    ::GetLastError());
            }
        } else if (
            ::GetLastError() !=
            ERROR_FILE_NOT_FOUND) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        writable.Reset();
        const std::wstring stop_name =
            BrokerSessionObjectName(
                L"ConfigBrokerStop",
                test_store_subkey);
        const std::wstring mutex_name =
            BrokerSessionObjectName(
                L"ConfigBroker",
                test_store_subkey);
        ScopedKernelHandle mutex(
            ::OpenMutexW(
                SYNCHRONIZE,
                FALSE,
                mutex_name.c_str()));
        if (!mutex) {
            const DWORD error =
                ::GetLastError();
            if (error ==
                ERROR_FILE_NOT_FOUND) {
                return StartReplacementBroker(
                    test_store_subkey);
            }
            return HRESULT_FROM_WIN32(
                error);
        }
        ScopedKernelHandle stop(
            ::OpenEventW(
                EVENT_MODIFY_STATE,
                FALSE,
                stop_name.c_str()));
        if (!stop) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        if (!::SetEvent(stop.Get())) {
            return HRESULT_FROM_WIN32(
                ::GetLastError());
        }
        stop.Reset();
        mutex.Reset();
        result = WaitForBrokerExit(
            test_store_subkey);
        if (FAILED(result)) {
            return result;
        }
        return StartReplacementBroker(
            test_store_subkey);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (const std::system_error& error) {
        return SystemErrorToHResult(
            error);
    }
}

} // namespace qiqi::config
