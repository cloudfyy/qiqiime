// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/logging.h"
#include "broker/registration.h"
#include "broker/runtime.h"
#include "broker/server.h"

#include "common/guids.h"
#include "common/logger.h"
#include "common/RAII.h"
#include "common/comobj.h"

#include <objbase.h>
#include <shellapi.h>
#include <gsl/gsl>
#include <algorithm>
#include <atomic>
#include <array>
#include <new>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace qiqi {

HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};

} // namespace qiqi

namespace {

constexpr DWORD kIdleWaitMs = 15'000;
constexpr ULONGLONG kShutdownWaitMs = 15'000;
constexpr DWORD kShutdownPollMs = 50;

struct BrokerArguments {
    std::wstring store_subkey;
    bool embedding = false;
    bool shutdown_requested = false;
    bool register_requested = false;
    bool unregister_requested = false;
};

bool IsSwitch(
    std::wstring_view argument,
    std::wstring_view expected) noexcept {
    return CompareStringOrdinal(
               argument.data(),
               gsl::narrow_cast<int>(
                   argument.size()),
               expected.data(),
               gsl::narrow_cast<int>(
                   expected.size()),
               TRUE) ==
        CSTR_EQUAL;
}

std::wstring InstanceName(
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
            hash ^= gsl::narrow_cast<uint16_t>(
                character);
            hash *= 1099511628211ull;
        }
        name += std::format(
            L".{:016X}",
            hash);
    }
    return name;
}

HRESULT ParseArguments(
    BrokerArguments& output) noexcept {
    int argument_count = 0;
    wchar_t** const arguments =
        ::CommandLineToArgvW(
            ::GetCommandLineW(),
            &argument_count);
    if (!arguments) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const qiqi::ScopedLocalAlloc owner(
        arguments);
    try {
        const std::span<wchar_t*> argument_span(
            arguments,
            gsl::narrow_cast<size_t>(
                argument_count));
        constexpr std::wstring_view prefix =
            L"--store-subkey=";
        for (int index = 1;
             index < argument_count;
             ++index) {
            const std::wstring_view value(
                 gsl::at(
                     argument_span,
                     gsl::narrow_cast<size_t>(
                         index)));
            if (value.starts_with(prefix)) {
                output.store_subkey.assign(
                    value.substr(
                        prefix.size()));
                continue;
            }
            output.embedding |=
                value == L"-Embedding" ||
                value == L"/Embedding";
            output.shutdown_requested |=
                value == L"--shutdown";
            output.register_requested |=
                IsSwitch(
                    value,
                    L"/RegisterServer") ||
                IsSwitch(
                    value,
                    L"--register");
            output.unregister_requested |=
                IsSwitch(
                    value,
                    L"/UnregisterServer") ||
                IsSwitch(
                    value,
                    L"--unregister");
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT ServerInstanceMutex(
    std::wstring_view store_subkey,
    qiqi::ScopedKernelHandle& mutex) noexcept {
    std::wstring name;
    try {
        name = InstanceName(
            L"ConfigBroker",
            store_subkey);
    } catch (const std::system_error& error) {
        return qiqi::SystemErrorToHResult(
            error);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
    ::SetLastError(ERROR_SUCCESS);
    mutex.Reset(::CreateMutexW(
        nullptr,
        FALSE,
        name.c_str()));
    const DWORD create_error =
        ::GetLastError();
    if (!mutex) {
        return HRESULT_FROM_WIN32(
            create_error);
    }
    return create_error ==
            ERROR_ALREADY_EXISTS
        ? S_FALSE
        : S_OK;
}

HRESULT CreateStopEvent(
    std::wstring_view store_subkey,
    qiqi::ScopedKernelHandle& output) noexcept {
    std::wstring name;
    try {
        name = InstanceName(
            L"ConfigBrokerStop",
            store_subkey);
    } catch (const std::system_error& error) {
        return qiqi::SystemErrorToHResult(
            error);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
    output.Reset(::CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        name.c_str()));
    return output
        ? S_OK
        : HRESULT_FROM_WIN32(
              ::GetLastError());
}

HRESULT SignalStopEvent(
    std::wstring_view store_subkey) noexcept {
    std::wstring name;
    try {
        name = InstanceName(
            L"ConfigBrokerStop",
            store_subkey);
    } catch (const std::system_error& error) {
        return qiqi::SystemErrorToHResult(
            error);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
    qiqi::ScopedKernelHandle event(
        ::OpenEventW(
            EVENT_MODIFY_STATE,
            FALSE,
            name.c_str()));
    if (!event) {
        const DWORD error =
            ::GetLastError();
        return error ==
                ERROR_FILE_NOT_FOUND
            ? S_OK
            : HRESULT_FROM_WIN32(error);
    }
    return ::SetEvent(event.Get())
        ? S_OK
        : HRESULT_FROM_WIN32(
              ::GetLastError());
}

HRESULT WaitForBrokerExit(
    std::wstring_view store_subkey) noexcept {
    std::wstring name;
    try {
        name = InstanceName(
            L"ConfigBroker",
            store_subkey);
    } catch (const std::system_error& error) {
        return qiqi::SystemErrorToHResult(
            error);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
    const ULONGLONG deadline =
        ::GetTickCount64() +
        kShutdownWaitMs;
    for (;;) {
        const qiqi::ScopedKernelHandle mutex(
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
                WAIT_TIMEOUT);
        }
        ::Sleep(
            gsl::narrow_cast<DWORD>(
                std::min(
                    kShutdownPollMs,
                    gsl::narrow_cast<DWORD>(
                        deadline - now))));
    }
}

HRESULT RunServer(
    std::wstring subkey) noexcept {
    qiqi::ScopedKernelHandle
        instance_mutex;
    const HRESULT mutex_result =
        ServerInstanceMutex(
            subkey,
            instance_mutex);
    if (mutex_result == S_FALSE) {
        QIQI_LOG_INFO(
            "Broker instance already "
            "running in this session");
        return S_OK;
    }
    if (FAILED(mutex_result)) {
        QIQI_LOG_ERROR(
            "Broker singleton mutex failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                mutex_result));
        return mutex_result;
    }
    qiqi::ScopedKernelHandle stop_event;
    const HRESULT event_result =
        CreateStopEvent(
            subkey,
            stop_event);
    if (FAILED(event_result)) {
        QIQI_LOG_ERROR(
            "Broker stop event failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                event_result));
        return event_result;
    }

    const HRESULT co_result =
        ::CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);
    if (FAILED(co_result)) {
        QIQI_LOG_ERROR(
            "CoInitializeEx failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                co_result));
        return co_result;
    }
    const auto uninitialize =
        gsl::finally(
            [co_result]() noexcept {
                if (SUCCEEDED(co_result)) {
                    ::CoUninitialize();
                }
            });

    GUID appid =
        qiqi::kAppIdQiQiConfigBroker;
    const HRESULT security_result =
        ::CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            &appid,
            EOAC_APPID,
            nullptr);
    if (FAILED(security_result)) {
        QIQI_LOG_ERROR(
            "CoInitializeSecurity failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                security_result));
        return security_result;
    }

    try {
        qiqi::broker::Runtime runtime(
            std::move(subkey));
        const auto factory =
            qiqi::broker::
                CreateConfigBrokerFactory(
                    runtime);
        if (!factory) {
            return E_OUTOFMEMORY;
        }
        DWORD cookie = 0;
        HRESULT result =
            ::CoRegisterClassObject(
                qiqi::
                    kClsidQiQiConfigBroker,
                factory.Get(),
                CLSCTX_LOCAL_SERVER,
                REGCLS_MULTIPLEUSE |
                    REGCLS_SUSPENDED,
                &cookie);
        if (FAILED(result)) {
            QIQI_LOG_ERROR(
                "CoRegisterClassObject failed "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    result));
            return result;
        }
        const auto revoke =
            gsl::finally(
                [cookie]() noexcept {
                    std::ignore =
                        ::CoRevokeClassObject(
                            cookie);
                });
        result =
            ::CoResumeClassObjects();
        if (FAILED(result)) {
            QIQI_LOG_ERROR(
                "CoResumeClassObjects failed "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    result));
            return result;
        }
        runtime.Touch();
        QIQI_LOG_INFO(
            "Broker started "
            "(provider={}, "
            "async_interface=true, "
            "pid={})",
            "QiQiIME.ConfigBroker.TraceLogging",
            ::GetCurrentProcessId());

        for (;;) {
            const DWORD wait =
                ::WaitForSingleObject(
                    stop_event.Get(),
                    kIdleWaitMs);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (runtime.ShouldExit()) {
                break;
            }
        }
        QIQI_LOG_INFO(
            "Broker exiting "
            "(pid={})",
            ::GetCurrentProcessId());
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "Broker startup allocation "
            "failed");
        return E_OUTOFMEMORY;
    } catch (const std::system_error& error) {
        QIQI_LOG_ERROR(
            "Broker startup failed "
            "(error={})",
            error.code().value());
        return qiqi::SystemErrorToHResult(
            error);
    }
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {
    qiqi::g_dll_instance = instance;
    const HRESULT logging =
        qiqi::broker::diag::Initialize();
    const auto shutdown_logging =
        gsl::finally(
            []() noexcept {
                qiqi::broker::diag::
                    Shutdown();
            });

    HRESULT result =
        S_OK;
    BrokerArguments arguments;
    result = ParseArguments(arguments);
    if (FAILED(result)) {
        QIQI_LOG_ERROR(
            "Broker command line parsing failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                result));
        return SUCCEEDED(result) ? 0 : 1;
    }
    if (arguments.shutdown_requested) {
        result = SignalStopEvent(
            arguments.store_subkey);
        if (SUCCEEDED(result)) {
            result =
                WaitForBrokerExit(
                    arguments.store_subkey);
        }
    } else if (
        arguments.register_requested) {
        result =
            qiqi::broker::
                RegisterServer(instance);
    } else if (
        arguments.unregister_requested) {
        result =
            qiqi::broker::
                UnregisterServer();
    } else {
        if (!arguments.embedding) {
            QIQI_LOG_WARN(
                "Broker rejected direct launch; "
                "only COM -Embedding is allowed");
            result = E_ABORT;
        } else {
            std::ignore = logging;
            result = RunServer(
                std::move(
                    arguments.store_subkey));
        }
    }
    QIQI_LOG_INFO(
        "Broker process completed "
        "(hr=0x{:08X})",
        gsl::narrow_cast<unsigned long>(
            result));
    return SUCCEEDED(result) ? 0 : 1;
}
