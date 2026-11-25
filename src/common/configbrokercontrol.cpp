// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configbrokercontrol.h"

#include "common/RAII.h"
#include "common/comobj.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <new>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace qiqi::config {
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

HRESULT OpenMediumBrokerLaunchToken(
    ScopedKernelHandle& output) noexcept {
    output.Reset();
    ScopedKernelHandle current_token;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            current_token.Put())) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    TOKEN_ELEVATION_TYPE elevation_type =
        TokenElevationTypeDefault;
    DWORD length = 0;
    if (!::GetTokenInformation(
            current_token.Get(),
            TokenElevationType,
            &elevation_type,
            sizeof(elevation_type),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if (elevation_type !=
        TokenElevationTypeFull) {
        return S_OK;
    }

    TOKEN_LINKED_TOKEN linked{};
    if (!::GetTokenInformation(
            current_token.Get(),
            TokenLinkedToken,
            &linked,
            sizeof(linked),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const ScopedKernelHandle
        linked_token(linked.LinkedToken);
    if (!::DuplicateTokenEx(
            linked_token.Get(),
            TOKEN_ASSIGN_PRIMARY |
                TOKEN_DUPLICATE |
                TOKEN_QUERY |
                TOKEN_ADJUST_DEFAULT,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            output.Put())) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    QIQI_LOG_CAT_INFO(
        log::Keyword::Config,
        "Replacement Broker will use "
        "the linked non-elevated token");
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
    ScopedKernelHandle launch_token;
    const HRESULT token_result =
        OpenMediumBrokerLaunchToken(
            launch_token);
    if (FAILED(token_result)) {
        return token_result;
    }
    const BOOL created =
        launch_token
            ? ::CreateProcessAsUserW(
                  launch_token.Get(),
                  broker_path.c_str(),
                  command.data(),
                  nullptr,
                  nullptr,
                  FALSE,
                  CREATE_NO_WINDOW,
                  nullptr,
                  broker_path
                      .parent_path()
                      .c_str(),
                  &startup,
                  &process)
            : ::CreateProcessW(
                  broker_path.c_str(),
                  command.data(),
                  nullptr,
                  nullptr,
                  FALSE,
                  CREATE_NO_WINDOW,
                  nullptr,
                  broker_path
                      .parent_path()
                      .c_str(),
                  &startup,
                  &process);
    if (!created) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const ScopedKernelHandle thread(
        process.hThread);
    const ScopedKernelHandle child(
        process.hProcess);
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
