// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hoststate.h"

#include <appmodel.h>
#include <algorithm>
#include <format>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <tuple>
#include <vector>

namespace qiqi::host::fulltrust {
namespace {

constexpr std::wstring_view kConfigSubKey = L"Software\\QiQiIME";
constexpr std::wstring_view kShadowSkinId = L"default";
constexpr uint64_t kShadowSequenceIncrement = 4096;
constexpr DWORD kShadowMarkerValue = 1;

struct RegistryCloser {
    void operator()(HKEY key) const noexcept {
        if (key) {
            std::ignore = ::RegCloseKey(key);
        }
    }
};

using ScopedRegistryKey =
    std::unique_ptr<std::remove_pointer_t<HKEY>, RegistryCloser>;

HRESULT Win32HResult(DWORD error) noexcept {
    return HRESULT_FROM_WIN32(error);
}

std::optional<uint64_t> ReadQword(
    HKEY key,
    std::wstring_view name,
    HRESULT& hr) noexcept {
    uint64_t value = 0;
    DWORD size = sizeof(value);
    DWORD type = REG_NONE;
    const LSTATUS error = ::RegQueryValueExW(
        key,
        name.data(),
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(&value),
        &size);
    if (error != ERROR_SUCCESS) {
        hr = Win32HResult(error);
        return std::nullopt;
    }
    if (type != REG_QWORD || size != sizeof(value)) {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return std::nullopt;
    }
    hr = S_OK;
    return value;
}

std::optional<std::wstring> ReadString(
    HKEY key,
    std::wstring_view name,
    HRESULT& hr) {
    DWORD type = REG_NONE;
    DWORD size = 0;
    LSTATUS error = ::RegQueryValueExW(
        key,
        name.data(),
        nullptr,
        &type,
        nullptr,
        &size);
    if (error != ERROR_SUCCESS) {
        hr = Win32HResult(error);
        return std::nullopt;
    }
    if (type != REG_SZ ||
        size < sizeof(wchar_t) ||
        size > 4096 ||
        size % sizeof(wchar_t) != 0) {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return std::nullopt;
    }
    std::wstring value(
        static_cast<size_t>(size / sizeof(wchar_t)),
        L'\0');
    error = ::RegQueryValueExW(
        key,
        name.data(),
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(value.data()),
        &size);
    if (error != ERROR_SUCCESS) {
        hr = Win32HResult(error);
        return std::nullopt;
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    hr = S_OK;
    return value;
}

bool ValuePresent(
    HKEY key,
    std::wstring_view name,
    HRESULT& hr) noexcept {
    DWORD size = 0;
    DWORD type = REG_NONE;
    const LSTATUS error = ::RegQueryValueExW(
        key,
        name.data(),
        nullptr,
        &type,
        nullptr,
        &size);
    hr = Win32HResult(error);
    return error == ERROR_SUCCESS;
}

std::wstring DisplayOptional(
    const std::optional<std::wstring>& value) {
    return value ? *value : L"<missing>";
}

std::wstring DisplayHResult(HRESULT value) {
    return std::format(L"0x{:08X}", static_cast<uint32_t>(value));
}

} // namespace

PackageState ReadPackageState() noexcept {
    PackageState state;

    uint32_t length = 0;
    LONG error = ::GetCurrentPackageFullName(&length, nullptr);
    if (error == ERROR_INSUFFICIENT_BUFFER && length != 0) {
        try {
            std::wstring value(
                static_cast<size_t>(length) + 1,
                L'\0');
            error = ::GetCurrentPackageFullName(
                &length,
                value.data());
            if (error == ERROR_SUCCESS) {
                value.resize(std::wcslen(value.c_str()));
                state.package_full_name = std::move(value);
                state.has_package_identity = true;
            }
        } catch (const std::bad_alloc&) {
            error = ERROR_OUTOFMEMORY;
        }
    } else if (error == ERROR_SUCCESS) {
        state.has_package_identity = true;
    }
    state.package_name_error = static_cast<DWORD>(error);

    HANDLE raw_token = nullptr;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            &raw_token)) {
        state.app_container_error = ::GetLastError();
        state.integrity_error = state.app_container_error;
        return state;
    }
    const std::unique_ptr<void, decltype([](void* token) noexcept {
        if (token) {
            ::CloseHandle(static_cast<HANDLE>(token));
        }
    })> token(raw_token);

    DWORD is_app_container = 0;
    DWORD returned = 0;
    if (::GetTokenInformation(
            token.get(),
            TokenIsAppContainer,
            &is_app_container,
            sizeof(is_app_container),
            &returned)) {
        state.is_app_container = is_app_container != 0;
        state.app_container_error = ERROR_SUCCESS;
    } else {
        state.app_container_error = ::GetLastError();
    }

    DWORD size = 0;
    ::GetTokenInformation(
        token.get(),
        TokenIntegrityLevel,
        nullptr,
        0,
        &size);
    const DWORD size_error = ::GetLastError();
    if (size == 0) {
        state.integrity_error =
            size_error == ERROR_SUCCESS
                ? ERROR_INVALID_DATA
                : size_error;
        return state;
    }
    try {
        std::vector<BYTE> buffer(static_cast<size_t>(size));
        if (!::GetTokenInformation(
                token.get(),
                TokenIntegrityLevel,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &returned)) {
            state.integrity_error = ::GetLastError();
            return state;
        }
        const auto* integrity =
            reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
        if (!integrity->Label.Sid ||
            !::IsValidSid(integrity->Label.Sid)) {
            state.integrity_error = ERROR_INVALID_SID;
            return state;
        }
        const DWORD count =
            *::GetSidSubAuthorityCount(integrity->Label.Sid);
        if (count == 0) {
            state.integrity_error = ERROR_INVALID_SID;
            return state;
        }
        state.integrity_rid =
            *::GetSidSubAuthority(
                integrity->Label.Sid,
                count - 1);
        state.integrity_error = ERROR_SUCCESS;
    } catch (const std::bad_alloc&) {
        state.integrity_error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return state;
}

RegistryState ReadQiQiRegistryState() noexcept {
    RegistryState state;
    HKEY raw_config = nullptr;
    const LSTATUS root_error = ::RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kConfigSubKey.data(),
        0,
        KEY_READ,
        &raw_config);
    state.root_hr = Win32HResult(root_error);
    if (root_error != ERROR_SUCCESS) {
        state.sequence_hr = state.root_hr;
        state.skin_hr = state.root_hr;
        state.sync_directory_hr = state.root_hr;
        state.marker_hr = state.root_hr;
        state.catalog_hr = state.root_hr;
        return state;
    }
    const ScopedRegistryKey config_key(raw_config);

    try {
        state.config_sequence = ReadQword(
            config_key.get(),
            L"ConfigSequence",
            state.sequence_hr);
        state.skin_id = ReadString(
            config_key.get(),
            L"ImeSkinId",
            state.skin_hr);
        state.sync_directory_present = ValuePresent(
            config_key.get(),
            L"UserDictSyncDirectory",
            state.sync_directory_hr);
        state.shadow_marker_present = ValuePresent(
            config_key.get(),
            L"PackagedHostShadow",
            state.marker_hr);
    } catch (const std::bad_alloc&) {
        state.sequence_hr = E_OUTOFMEMORY;
        state.skin_hr = E_OUTOFMEMORY;
    }

    HKEY raw_skins = nullptr;
    const LSTATUS skins_error = ::RegOpenKeyExW(
        config_key.get(),
        L"Skins",
        0,
        KEY_READ,
        &raw_skins);
    if (skins_error == ERROR_SUCCESS) {
        const ScopedRegistryKey skins_key(raw_skins);
        try {
            state.active_catalog = ReadString(
                skins_key.get(),
                L"ActiveCatalog",
                state.catalog_hr);
        } catch (const std::bad_alloc&) {
            state.catalog_hr = E_OUTOFMEMORY;
        }
    } else {
        state.catalog_hr = Win32HResult(skins_error);
    }
    return state;
}

ShadowSeedResult SeedPackagePrivateQiQiShadow(
    const PackageState& package_state) noexcept {
    ShadowSeedResult result;
    if (!package_state.has_package_identity ||
        package_state.is_app_container) {
        result.hr = E_ILLEGAL_METHOD_CALL;
        return result;
    }

    const RegistryState before = ReadQiQiRegistryState();
    if (before.config_sequence) {
        result.previous_sequence = *before.config_sequence;
    }
    if (before.skin_id) {
        result.previous_skin = *before.skin_id;
    }
    result.shadow_sequence =
        result.previous_sequence >
            (std::numeric_limits<uint64_t>::max() - kShadowSequenceIncrement)
        ? std::numeric_limits<uint64_t>::max()
        : result.previous_sequence + kShadowSequenceIncrement;
    result.shadow_skin.assign(kShadowSkinId);

    HKEY raw_key = nullptr;
    const LSTATUS open_error = ::RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kConfigSubKey.data(),
        0,
        KEY_READ | KEY_SET_VALUE,
        &raw_key);
    if (open_error != ERROR_SUCCESS) {
        result.hr = Win32HResult(open_error);
        return result;
    }
    const ScopedRegistryKey key(raw_key);

    const LSTATUS marker_error = ::RegSetValueExW(
        key.get(),
        L"PackagedHostShadow",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&kShadowMarkerValue),
        sizeof(kShadowMarkerValue));
    if (marker_error != ERROR_SUCCESS) {
        result.hr = Win32HResult(marker_error);
        return result;
    }

    const LSTATUS skin_error = ::RegSetValueExW(
        key.get(),
        L"ImeSkinId",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(result.shadow_skin.c_str()),
        static_cast<DWORD>(
            (result.shadow_skin.size() + 1) * sizeof(wchar_t)));
    if (skin_error != ERROR_SUCCESS) {
        result.hr = Win32HResult(skin_error);
        return result;
    }

    const LSTATUS sequence_error = ::RegSetValueExW(
        key.get(),
        L"ConfigSequence",
        0,
        REG_QWORD,
        reinterpret_cast<const BYTE*>(&result.shadow_sequence),
        sizeof(result.shadow_sequence));
    if (sequence_error != ERROR_SUCCESS) {
        result.hr = Win32HResult(sequence_error);
        return result;
    }

    const RegistryState after = ReadQiQiRegistryState();
    if (!after.shadow_marker_present ||
        !after.config_sequence ||
        *after.config_sequence != result.shadow_sequence ||
        !after.skin_id ||
        *after.skin_id != result.shadow_skin) {
        result.hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        return result;
    }
    result.hr = S_OK;
    return result;
}

std::wstring IntegrityLabel(DWORD rid) noexcept {
    switch (rid) {
    case SECURITY_MANDATORY_UNTRUSTED_RID:
        return L"untrusted";
    case SECURITY_MANDATORY_LOW_RID:
        return L"low";
    case SECURITY_MANDATORY_MEDIUM_RID:
        return L"medium";
    case SECURITY_MANDATORY_MEDIUM_PLUS_RID:
        return L"medium-plus";
    case SECURITY_MANDATORY_HIGH_RID:
        return L"high";
    case SECURITY_MANDATORY_SYSTEM_RID:
        return L"system";
    default:
        return std::format(L"rid={}", rid);
    }
}

std::wstring FormatPackageState(const PackageState& state) {
    return std::format(
        L"Package identity: {} (name_error={})\r\n"
        L"TokenIsAppContainer: {} (error={})\r\n"
        L"Integrity: {} (error={})\r\n"
        L"Package: {}",
        state.has_package_identity ? L"true" : L"false",
        state.package_name_error,
        state.is_app_container ? L"true" : L"false",
        state.app_container_error,
        IntegrityLabel(state.integrity_rid),
        state.integrity_error,
        state.package_full_name.empty()
            ? L"<unavailable>"
            : state.package_full_name);
}

std::wstring FormatRegistryState(const RegistryState& state) {
    std::wstring sequence = state.config_sequence
        ? std::to_wstring(*state.config_sequence)
        : L"<missing>";
    return std::format(
        L"QiQi root: {}\r\n"
        L"ConfigSequence: {} ({})\r\n"
        L"ImeSkinId: {} ({})\r\n"
        L"ActiveCatalog: {} ({})\r\n"
        L"UserDictSyncDirectory present: {} ({})\r\n"
        L"Package shadow marker: {} ({})",
        DisplayHResult(state.root_hr),
        sequence,
        DisplayHResult(state.sequence_hr),
        DisplayOptional(state.skin_id),
        DisplayHResult(state.skin_hr),
        DisplayOptional(state.active_catalog),
        DisplayHResult(state.catalog_hr),
        state.sync_directory_present ? L"true" : L"false",
        DisplayHResult(state.sync_directory_hr),
        state.shadow_marker_present ? L"true" : L"false",
        DisplayHResult(state.marker_hr));
}

} // namespace qiqi::host::fulltrust

