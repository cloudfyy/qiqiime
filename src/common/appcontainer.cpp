// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/appcontainer.h"

#include "common/logger.h"
#include "common/RAII.h"

#include <gsl/gsl>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <span>
#include <system_error>

namespace qiqi {
namespace {

HRESULT SidToString(
    PSID sid,
    std::wstring& output) noexcept {
    LPWSTR raw = nullptr;
    if (!::ConvertSidToStringSidW(
            sid,
            &raw)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const std::unique_ptr<
        wchar_t,
        decltype([](wchar_t* value) noexcept {
            ::LocalFree(value);
        })> owner(raw);
    try {
        output.assign(raw);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT AppendPackageSids(
    std::wstring_view family,
    std::vector<std::wstring>& output) {
    PSID raw = nullptr;
    const HRESULT result =
        ::DeriveAppContainerSidFromAppContainerName(
            std::wstring(family).c_str(),
            &raw);
    if (FAILED(result)) {
        return result;
    }
    const std::unique_ptr<
        void,
        decltype([](void* value) noexcept {
            ::FreeSid(value);
        })> owner(raw);

    std::wstring app_container;
    HRESULT sid_result =
        SidToString(raw, app_container);
    if (FAILED(sid_result)) {
        return sid_result;
    }
    output.push_back(
        std::move(app_container));

    const auto* count =
        ::GetSidSubAuthorityCount(raw);
    if (!count || *count == 0) {
        return E_UNEXPECTED;
    }
    std::vector<uint8_t> copy(
        ::GetLengthSid(raw));
    if (!::CopySid(
            gsl::narrow_cast<DWORD>(
                copy.size()),
            copy.data(),
            raw)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    PSID package_capability =
        copy.data();
    auto* first =
        ::GetSidSubAuthority(
            package_capability,
            0);
    if (!first ||
        *first != 2) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_SID);
    }
    *first = 3;
    std::wstring capability;
    sid_result =
        SidToString(
            package_capability,
            capability);
    if (FAILED(sid_result)) {
        return sid_result;
    }
    output.push_back(
        std::move(capability));
    return S_OK;
}

} // namespace

DerivedCapabilitySids::DerivedCapabilitySids(
    const wchar_t* name) noexcept
    : name_(name) {}

DerivedCapabilitySids::
    ~DerivedCapabilitySids() noexcept {
    if (groups_) {
        for (const PSID group :
             std::span(
                 groups_,
                 group_count_)) {
            if (group) {
                ::LocalFree(group);
            }
        }
        ::LocalFree(groups_);
    }
    if (capabilities_) {
        for (const PSID capability :
             std::span(
                 capabilities_,
                 capability_count_)) {
            if (capability) {
                ::LocalFree(capability);
            }
        }
        ::LocalFree(capabilities_);
    }
}

HRESULT DerivedCapabilitySids::Build() noexcept {
    if (!name_ || !*name_) {
        return E_INVALIDARG;
    }
    if (!::DeriveCapabilitySidsFromName(
            name_,
            &groups_,
            &group_count_,
            &capabilities_,
            &capability_count_)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if ((group_count_ != 0 &&
         !groups_) ||
        (capability_count_ != 0 &&
         !capabilities_)) {
        return E_UNEXPECTED;
    }
    return S_OK;
}

std::span<PSID>
DerivedCapabilitySids::Capabilities() noexcept {
    return {
        capabilities_,
        capability_count_};
}

std::span<const PSID>
DerivedCapabilitySids::Capabilities()
    const noexcept {
    return {
        capabilities_,
        capability_count_};
}

bool DerivedCapabilitySids::Contains(
    PSID candidate) const noexcept {
    return candidate &&
        ::IsValidSid(candidate) &&
        std::ranges::any_of(
            Capabilities(),
            [candidate](
                const PSID capability) noexcept {
                return capability &&
                    ::IsValidSid(
                        capability) &&
                    ::EqualSid(
                        candidate,
                        capability);
            });
}

namespace {

HRESULT QueryTokenCapabilities(
    HANDLE token,
    std::vector<uint8_t>& output) noexcept {
    DWORD length = 0;
    if (::GetTokenInformation(
            token,
            TokenCapabilities,
            nullptr,
            0,
            &length)) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    const DWORD query_error =
        ::GetLastError();
    if (query_error !=
            ERROR_INSUFFICIENT_BUFFER ||
        length == 0) {
        return HRESULT_FROM_WIN32(
            query_error != ERROR_SUCCESS
                ? query_error
                : ERROR_INVALID_DATA);
    }
    try {
        output.resize(length);
    } catch (
        const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
    DWORD copied = length;
    if (!::GetTokenInformation(
            token,
            TokenCapabilities,
            output.data(),
            length,
            &copied)) {
        const DWORD read_error =
            ::GetLastError();
        return HRESULT_FROM_WIN32(
            read_error != ERROR_SUCCESS
                ? read_error
                : ERROR_INVALID_DATA);
    }
    if (copied > length) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    output.resize(copied);
    return S_OK;
}

} // namespace

HRESULT
QueryCurrentProcessAppContainerCapabilities(
    AppContainerCapabilityState& output)
    noexcept {
    output = {};
    ScopedKernelHandle token;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            token.Put())) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    DWORD app_container = 0;
    DWORD length = 0;
    if (!::GetTokenInformation(
            token.Get(),
            TokenIsAppContainer,
            &app_container,
            sizeof(app_container),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    output.is_app_container =
        app_container != 0;
    if (!output.is_app_container) {
        return S_OK;
    }

    DerivedCapabilitySids lpac_com(
        L"lpacCom");
    DerivedCapabilitySids registry_read(
        L"registryRead");
    HRESULT result =
        lpac_com.Build();
    if (FAILED(result)) {
        return result;
    }
    result =
        registry_read.Build();
    if (FAILED(result)) {
        return result;
    }
    std::vector<uint8_t> buffer;
    result =
        QueryTokenCapabilities(
            token.Get(),
            buffer);
    if (FAILED(result)) {
        return result;
    }
    constexpr size_t entries_offset =
        offsetof(TOKEN_GROUPS, Groups);
    if (buffer.size() < entries_offset) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    const auto* groups =
        reinterpret_cast<
            const TOKEN_GROUPS*>(
                buffer.data());
    if (groups->GroupCount >
        (buffer.size() -
         entries_offset) /
            sizeof(SID_AND_ATTRIBUTES)) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    if (groups->GroupCount == 0) {
        return S_OK;
    }
    const auto* const first_capability =
        std::addressof(
            gsl::at(
                groups->Groups,
                0));
    const std::span<
        const SID_AND_ATTRIBUTES>
        capabilities(
            first_capability,
            groups->GroupCount);
    for (const auto& capability :
         capabilities) {
        if ((capability.Attributes &
             SE_GROUP_ENABLED) == 0) {
            continue;
        }
        if (!capability.Sid ||
            !::IsValidSid(
                capability.Sid)) {
            return HRESULT_FROM_WIN32(
                ERROR_INVALID_DATA);
        }
        output.has_lpac_com |=
            lpac_com.Contains(
                capability.Sid);
        output.has_registry_read |=
            registry_read.Contains(
                capability.Sid);
    }
    return S_OK;
}

HRESULT DiscoverWindowsSearchPackageSids(
    std::vector<std::wstring>& output) noexcept {
    output.clear();
    std::array<wchar_t, 32768> windows{};
    const UINT length =
        ::GetWindowsDirectoryW(
            windows.data(),
            gsl::narrow_cast<UINT>(
                windows.size()));
    if (length == 0 ||
        length >= windows.size()) {
        return HRESULT_FROM_WIN32(
            length == 0
                ? ::GetLastError()
                : ERROR_INSUFFICIENT_BUFFER);
    }
    try {
        const std::filesystem::path system_apps =
            std::filesystem::path(
                windows.data()) /
            L"SystemApps";
        std::error_code error;
        std::filesystem::directory_iterator it(
            system_apps,
            error);
        if (error) {
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(
                    error.value()));
        }
        const std::filesystem::
            directory_iterator end;
        for (; it != end; it.increment(error)) {
            if (error) {
                return HRESULT_FROM_WIN32(
                    gsl::narrow_cast<DWORD>(
                        error.value()));
            }
            const bool directory =
                it->is_directory(error);
            if (error) {
                return HRESULT_FROM_WIN32(
                    gsl::narrow_cast<DWORD>(
                        error.value()));
            }
            if (!directory) {
                continue;
            }
            const auto path =
                it->path();
            const bool search_host =
                std::filesystem::exists(
                    path /
                        L"SearchHost.exe",
                    error);
            if (error) {
                return HRESULT_FROM_WIN32(
                    gsl::narrow_cast<DWORD>(
                        error.value()));
            }
            const bool search_app =
                std::filesystem::exists(
                    path /
                        L"SearchApp.exe",
                    error);
            if (error) {
                return HRESULT_FROM_WIN32(
                    gsl::narrow_cast<DWORD>(
                        error.value()));
            }
            if (!search_host &&
                !search_app) {
                continue;
            }
            const HRESULT sid_result =
                AppendPackageSids(
                    path.filename()
                        .wstring(),
                    output);
            if (FAILED(sid_result)) {
                return sid_result;
            }
        }
        if (error) {
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(
                    error.value()));
        }
        std::ranges::sort(output);
        output.erase(
            std::unique(
                output.begin(),
                output.end()),
            output.end());
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (
        const std::system_error& error) {
        return HRESULT_FROM_WIN32(
            gsl::narrow_cast<DWORD>(
                error.code().value()));
    }
}

} // namespace qiqi
