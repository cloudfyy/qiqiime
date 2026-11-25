// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/appcontainer.h"

#include "common/logger.h"

#include <gsl/gsl>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>
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
