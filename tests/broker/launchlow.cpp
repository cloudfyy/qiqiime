// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configbrokercontrol.h"
#include "common/RAII.h"

#include <windows.h>
#include <sddl.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace {

std::wstring SubkeyArgument(
    int argc,
    wchar_t** argv) {
    constexpr std::wstring_view prefix =
        L"--store-subkey=";
    for (int index = 3;
         index < argc;
         ++index) {
        const std::wstring_view value(
            argv[index]);
        if (value.starts_with(prefix)) {
            return std::wstring(
                value.substr(
                    prefix.size()));
        }
    }
    return {};
}

std::wstring CommandLine(
    int argc,
    wchar_t** argv) {
    std::wstring command;
    for (int index = 1;
         index < argc;
         ++index) {
        if (index > 1) {
            command += L" ";
        }
        command += L"\"";
        command += argv[index];
        command += L"\"";
    }
    return command;
}

int LaunchLowBroker(
    int argc,
    wchar_t** argv) {
    std::wstring command =
        CommandLine(argc, argv);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(
            argv[1],
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED |
                CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        std::cerr
            << "CreateProcessW error="
            << ::GetLastError()
            << '\n';
        return 1;
    }
    const qiqi::ScopedKernelHandle
        process_handle(process.hProcess);
    const qiqi::ScopedKernelHandle
        thread_handle(process.hThread);
    qiqi::ScopedKernelHandle token;
    if (!::OpenProcessToken(
            process_handle.Get(),
            TOKEN_QUERY |
                TOKEN_ADJUST_DEFAULT,
            token.Put())) {
        std::cerr
            << "OpenProcessToken error="
            << ::GetLastError()
            << '\n';
        ::TerminateProcess(
            process_handle.Get(),
            1);
        return 1;
    }
    PSID low_sid = nullptr;
    if (!::ConvertStringSidToSidW(
            L"S-1-16-4096",
            &low_sid)) {
        std::cerr
            << "ConvertStringSidToSidW error="
            << ::GetLastError()
            << '\n';
        ::TerminateProcess(
            process_handle.Get(),
            1);
        return 1;
    }
    const std::unique_ptr<
        void,
        decltype([](
            void* value) noexcept {
            ::LocalFree(
                value);
        })> sid_owner(low_sid);
    TOKEN_MANDATORY_LABEL label{
        .Label = {
            .Sid = low_sid,
            .Attributes =
                SE_GROUP_INTEGRITY}};
    if (!::SetTokenInformation(
            token.Get(),
            TokenIntegrityLevel,
            &label,
            sizeof(label) +
                ::GetLengthSid(low_sid))) {
        std::cerr
            << "SetTokenInformation error="
            << ::GetLastError()
            << '\n';
        ::TerminateProcess(
            process_handle.Get(),
            1);
        return 1;
    }
    if (::ResumeThread(
            thread_handle.Get()) ==
        static_cast<DWORD>(-1)) {
        std::cerr
            << "ResumeThread error="
            << ::GetLastError()
            << '\n';
        ::TerminateProcess(
            process_handle.Get(),
            1);
        return 1;
    }

    const std::wstring ready_name =
        qiqi::config::
            BrokerSessionObjectName(
                L"ConfigBrokerReady",
                SubkeyArgument(argc, argv));
    for (int attempt = 0;
         attempt < 300;
         ++attempt) {
        const qiqi::ScopedKernelHandle ready(
            ::OpenEventW(
                SYNCHRONIZE,
                FALSE,
                ready_name.c_str()));
        if (ready) {
            const DWORD wait =
                ::WaitForSingleObject(
                    ready.Get(),
                    0);
            if (wait == WAIT_OBJECT_0) {
                std::wcout
                    << process.dwProcessId
                    << L"\n";
                return 0;
            }
            if (wait == WAIT_FAILED) {
                std::cerr
                    << "WaitForSingleObject "
                       "ready error="
                    << ::GetLastError()
                    << '\n';
                ::TerminateProcess(
                    process_handle.Get(),
                    1);
                return 1;
            }
        } else if (
            ::GetLastError() !=
            ERROR_FILE_NOT_FOUND) {
            std::cerr
                << "OpenEventW error="
                << ::GetLastError()
                << '\n';
            ::TerminateProcess(
                process_handle.Get(),
                1);
            return 1;
        }
        const DWORD process_wait =
            ::WaitForSingleObject(
                process.hProcess,
                0);
        if (process_wait ==
            WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            std::ignore =
                ::GetExitCodeProcess(
                    process.hProcess,
                    &exit_code);
            std::cerr
                << "Broker exited during "
                   "startup with code "
                << exit_code
                << '\n';
            return 1;
        }
        if (process_wait ==
            WAIT_FAILED) {
            std::cerr
                << "WaitForSingleObject "
                   "process error="
                << ::GetLastError()
                << '\n';
            ::TerminateProcess(
                process_handle.Get(),
                1);
            return 1;
        }
        ::Sleep(50);
    }
    std::cerr
        << "Broker ready event timed out\n";
    ::TerminateProcess(
        process_handle.Get(),
        1);
    return 1;
}

} // namespace

int wmain(
    int argc,
    wchar_t** argv) {
    if (argc < 3 ||
        std::wstring_view(argv[2]) !=
            L"-Embedding") {
        std::wcerr
            << L"usage: "
               L"qiqi_broker_launchlow.exe "
               L"Broker.exe -Embedding "
               L"[--store-subkey=KEY]\n";
        return 2;
    }
    try {
        return LaunchLowBroker(
            argc,
            argv);
    } catch (
        const std::exception& error) {
        std::cerr
            << "Low Broker launch failed: "
            << error.what()
            << '\n';
        return 1;
    }
}
