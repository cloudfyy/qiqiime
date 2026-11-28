// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tokeninfo.h"

#include <sddl.h>

#include <algorithm>
#include <format>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <vector>

namespace qiqi::host::token {
namespace {

constexpr std::wstring_view kAllApplicationPackagesSid =
    L"S-1-15-2-1";
constexpr std::wstring_view kAllRestrictedApplicationPackagesSid =
    L"S-1-15-2-2";

struct TokenCloser {
    void operator()(HANDLE token) const noexcept {
        if (token) {
            ::CloseHandle(token);
        }
    }
};

using TokenBuffer = std::vector<std::byte>;

DWORD QueryToken(
    HANDLE token,
    TOKEN_INFORMATION_CLASS information_class,
    TokenBuffer& buffer) noexcept {
    DWORD length = 0;
    if (::GetTokenInformation(
            token,
            information_class,
            nullptr,
            0,
            &length)) {
        return length == 0
            ? ERROR_SUCCESS
            : ERROR_INVALID_DATA;
    }
    const DWORD length_error = ::GetLastError();
    if (length_error != ERROR_INSUFFICIENT_BUFFER ||
        length == 0) {
        return length_error == ERROR_SUCCESS
            ? ERROR_INVALID_DATA
            : length_error;
    }
    try {
        buffer.resize(static_cast<size_t>(length));
    } catch (const std::bad_alloc&) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!::GetTokenInformation(
            token,
            information_class,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &length)) {
        return ::GetLastError();
    }
    return ERROR_SUCCESS;
}

DWORD QueryTokenDword(
    HANDLE token,
    TOKEN_INFORMATION_CLASS information_class,
    bool& value) noexcept {
    DWORD raw = 0;
    DWORD length = 0;
    if (!::GetTokenInformation(
            token,
            information_class,
            &raw,
            sizeof(raw),
            &length)) {
        return ::GetLastError();
    }
    value = raw != 0;
    return ERROR_SUCCESS;
}

bool SidToString(
    PSID sid,
    std::wstring& output) noexcept {
    output.clear();
    wchar_t* raw = nullptr;
    if (!sid ||
        !::IsValidSid(sid) ||
        !::ConvertSidToStringSidW(sid, &raw) ||
        !raw) {
        return false;
    }
    try {
        output.assign(raw);
    } catch (const std::bad_alloc&) {
        ::LocalFree(raw);
        return false;
    }
    ::LocalFree(raw);
    return true;
}

bool IsSid(
    PSID candidate,
    std::wstring_view expected) noexcept {
    std::wstring actual;
    return SidToString(candidate, actual) &&
        actual == expected;
}

struct DerivedSidList {
    PSID* values = nullptr;
    DWORD count = 0;

    DerivedSidList() = default;
    DerivedSidList(const DerivedSidList&) = delete;
    DerivedSidList& operator=(const DerivedSidList&) = delete;

    ~DerivedSidList() noexcept {
        if (values) {
            for (const PSID sid : std::span(values, count)) ::LocalFree(sid);
            ::LocalFree(values);
        }
    }
};

DWORD MatchCapability(std::span<const std::wstring> entries, const wchar_t* name, bool& matched) noexcept {
    DerivedSidList groups, capability;
    if (!::DeriveCapabilitySidsFromName(name, &groups.values, &groups.count, &capability.values, &capability.count)) {
        return ::GetLastError();
    }
    if (!capability.values || capability.count == 0) return ERROR_INVALID_DATA;
    for (const PSID sid : std::span(capability.values, capability.count)) {
        std::wstring expected;
        if (!SidToString(sid, expected)) return ERROR_INVALID_SID;
        matched |= std::ranges::contains(entries, expected);
    }
    return ERROR_SUCCESS;
}

DWORD QueryCapabilities(
    HANDLE token,
    State& state) noexcept {
    TokenBuffer buffer;
    const DWORD query_error =
        QueryToken(token, TokenCapabilities, buffer);
    if (query_error != ERROR_SUCCESS) {
        return query_error;
    }
    if (buffer.empty()) {
        return ERROR_SUCCESS;
    }
    constexpr size_t offset = offsetof(TOKEN_GROUPS, Groups);
    if (buffer.size() < offset) {
        return ERROR_INVALID_DATA;
    }
    const auto* groups =
        reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    if (groups->GroupCount >
        (buffer.size() - offset) /
            sizeof(SID_AND_ATTRIBUTES)) {
        return ERROR_INVALID_DATA;
    }
    if (groups->GroupCount == 0) return ERROR_SUCCESS;
    const std::span<const SID_AND_ATTRIBUTES> entries(groups->Groups, groups->GroupCount);
    try {
        state.capability_sids.reserve(entries.size());
        for (const auto& entry : entries) {
            if ((entry.Attributes & SE_GROUP_ENABLED) == 0) continue;
            std::wstring value;
            if (!SidToString(entry.Sid, value)) return ERROR_INVALID_SID;
            state.capability_sids.push_back(std::move(value));
        }
    } catch (const std::bad_alloc&) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    const DWORD com = MatchCapability(state.capability_sids, L"lpacCom", state.has_lpac_com);
    return com != ERROR_SUCCESS ? com
        : MatchCapability(state.capability_sids, L"registryRead", state.has_registry_read);
}

DWORD QueryGroups(
    HANDLE token,
    State& state) noexcept {
    TokenBuffer buffer;
    const DWORD query_error =
        QueryToken(token, TokenGroups, buffer);
    if (query_error != ERROR_SUCCESS) {
        return query_error;
    }
    if (buffer.size() < sizeof(TOKEN_GROUPS)) {
        return ERROR_INVALID_DATA;
    }
    const auto* groups =
        reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    const size_t offset = offsetof(TOKEN_GROUPS, Groups);
    if (groups->GroupCount >
        (buffer.size() - offset) /
            sizeof(SID_AND_ATTRIBUTES)) {
        return ERROR_INVALID_DATA;
    }
    const size_t required =
        offset +
        static_cast<size_t>(groups->GroupCount) *
            sizeof(SID_AND_ATTRIBUTES);
    if (required > buffer.size()) {
        return ERROR_INVALID_DATA;
    }
    for (DWORD index = 0;
         index < groups->GroupCount;
         ++index) {
        state.has_all_application_packages |= IsSid(
            groups->Groups[index].Sid,
            kAllApplicationPackagesSid);
        state.has_all_restricted_application_packages |= IsSid(
            groups->Groups[index].Sid,
            kAllRestrictedApplicationPackagesSid);
    }
    return ERROR_SUCCESS;
}

DWORD QueryPackageSid(
    HANDLE token,
    State& state) noexcept {
    TokenBuffer buffer;
    const DWORD query_error =
        QueryToken(token, TokenAppContainerSid, buffer);
    if (query_error != ERROR_SUCCESS) {
        return query_error;
    }
    if (buffer.size() <
        sizeof(TOKEN_APPCONTAINER_INFORMATION)) {
        return ERROR_INVALID_DATA;
    }
    const auto* information =
        reinterpret_cast<
            const TOKEN_APPCONTAINER_INFORMATION*>(
                buffer.data());
    if (!SidToString(
            information->TokenAppContainer,
            state.package_sid)) {
        return ERROR_INVALID_SID;
    }
    return ERROR_SUCCESS;
}

std::wstring JoinStrings(
    const std::vector<std::wstring>& values) {
    std::wstring joined;
    for (const auto& value : values) {
        if (!joined.empty()) {
            joined += L", ";
        }
        joined += value;
    }
    return joined;
}

} // namespace

State ReadCurrentProcessState() noexcept {
    State state;
    HANDLE raw_token = nullptr;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            &raw_token)) {
        state.open_error = ::GetLastError();
        return state;
    }
    state.open_error = ERROR_SUCCESS;
    const std::unique_ptr<void, TokenCloser> token(raw_token);

    state.app_container_error = QueryTokenDword(
        token.get(),
        TokenIsAppContainer,
        state.is_app_container);
    state.less_privileged_error = QueryTokenDword(
        token.get(),
        TokenIsLessPrivilegedAppContainer,
        state.is_less_privileged_app_container);
    state.package_sid_error =
        QueryPackageSid(token.get(), state);
    state.capabilities_error =
        QueryCapabilities(token.get(), state);
    state.groups_error =
        QueryGroups(token.get(), state);
    if (state.app_container_error == ERROR_SUCCESS &&
        state.groups_error == ERROR_SUCCESS &&
        state.is_app_container &&
        !state.has_all_application_packages) {
        state.inferred_less_privileged_app_container = true;
    }
    return state;
}

std::wstring FormatState(const State& state) {
    return std::format(
        L"AppContainer={} (error={}); LPAC={} (error={}); "
        L"LPAC-inferred={} ; "
        L"AllApplicationPackages={} ; "
        L"AllRestrictedApplicationPackages={} (groups_error={}); "
        L"PackageSID={} (error={}); "
        L"lpacCom={}; registryRead={}; Capabilities=[{}] (count={}, error={})",
        state.is_app_container ? L"true" : L"false",
        state.app_container_error,
        state.is_less_privileged_app_container ? L"true" : L"false",
        state.less_privileged_error,
        state.inferred_less_privileged_app_container
            ? L"true"
            : L"false",
        state.has_all_application_packages ? L"true" : L"false",
        state.has_all_restricted_application_packages
            ? L"true"
            : L"false",
        state.groups_error,
        state.package_sid.empty()
            ? L"<unavailable>"
            : state.package_sid,
        state.package_sid_error,
        state.has_lpac_com,
        state.has_registry_read,
        JoinStrings(state.capability_sids),
        state.capability_sids.size(),
        state.capabilities_error);
}

} // namespace qiqi::host::token
