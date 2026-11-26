// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"
#include "common/fileutil.h"
#include <sddl.h>
#include <gsl/gsl>
#include <bit>
#include <format>

namespace qiqi::diag::detail {

std::string SidText(PSID sid) {
    if (!sid || !::IsValidSid(sid)) {
        return {};
    }
    LPWSTR raw = nullptr;
    if (!::ConvertSidToStringSidW(sid, &raw)) {
        return {};
    }
    ScopedLocalAlloc owner(raw);
    return WideToUtf8(raw);
}

std::string SidLabel(std::string_view sid) noexcept {
    if (sid == "S-1-5-18")
        return "LocalSystem";
    if (sid == "S-1-5-32-544")
        return "Administrators";
    if (sid == "S-1-5-4")
        return "Interactive";
    if (sid == "S-1-15-2-1")
        return "All Application Packages";
    if (sid == "S-1-15-2-2")
        return "All Restricted Application Packages";
    return {};
}

Json SidJson(PSID sid) {
    Json output = Json::object();
    const std::string text = SidText(sid);
    output["sid"] = text;
    const std::string label = SidLabel(text);
    if (!label.empty()) {
        output["label"] = label;
    }
    return output;
}

std::string AceTypeName(BYTE type) noexcept {
    switch (type) {
    case ACCESS_ALLOWED_ACE_TYPE:
        return "allowed";
    case ACCESS_DENIED_ACE_TYPE:
        return "denied";
    case SYSTEM_AUDIT_ACE_TYPE:
        return "audit";
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        return "allowed-object";
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
        return "denied-object";
    case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        return "audit-object";
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        return "allowed-callback";
    case ACCESS_DENIED_CALLBACK_ACE_TYPE:
        return "denied-callback";
    case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
        return "mandatory-label";
    case SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE:
        return "resource-attribute";
    default:
        return std::format("type-0x{:02X}", type);
    }
}

bool IsStandardAce(BYTE type) noexcept {
    return type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_DENIED_ACE_TYPE || type == SYSTEM_AUDIT_ACE_TYPE;
}

HRESULT InspectSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor, Json& output) {
    if (!descriptor || !::IsValidSecurityDescriptor(descriptor)) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_SECURITY_DESCR);
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!::GetSecurityDescriptorControl(descriptor, &control, &revision)) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    output["revision"] = revision;
    output["dacl_protected"] = (control & SE_DACL_PROTECTED) != 0;

    PSID owner = nullptr;
    BOOL owner_defaulted = FALSE;
    if (::GetSecurityDescriptorOwner(descriptor, &owner, &owner_defaulted) != FALSE && owner) {
        output["owner"] = SidJson(owner);
    }
    PSID group = nullptr;
    BOOL group_defaulted = FALSE;
    if (::GetSecurityDescriptorGroup(descriptor, &group, &group_defaulted) != FALSE && group) {
        output["group"] = SidJson(group);
    }

    BOOL dacl_present = FALSE;
    PACL dacl = nullptr;
    BOOL dacl_defaulted = FALSE;
    if (!::GetSecurityDescriptorDacl(descriptor, &dacl_present, &dacl, &dacl_defaulted)) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    output["dacl_present"] = dacl_present != FALSE;
    output["dacl_defaulted"] = dacl_defaulted != FALSE;
    if (dacl_present == FALSE || !dacl) {
        output["aces"] = Json::array();
        return S_OK;
    }

    ACL_SIZE_INFORMATION information{};
    if (!::GetAclInformation(dacl, &information, sizeof(information), AclSizeInformation)) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    Json aces = Json::array();
    for (DWORD index = 0; index < information.AceCount; ++index) {
        LPVOID raw = nullptr;
        if (!::GetAce(dacl, index, &raw)) {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        const auto* header = std::bit_cast<ACE_HEADER*>(raw);
        Json ace{
            {"type", AceTypeName(header->AceType)},
            {"flags", gsl::narrow_cast<uint32_t>(header->AceFlags)},
            {"size", gsl::narrow_cast<uint32_t>(header->AceSize)},
        };
        if (IsStandardAce(header->AceType)) {
            const auto* standard = std::bit_cast<const ACCESS_ALLOWED_ACE*>(raw);
            ace["access_mask"] = standard->Mask;
            const PSID sid = std::bit_cast<PSID>(&standard->SidStart);
            if (::IsValidSid(sid)) {
                const auto principal = SidJson(sid);
                ace["sid"] = principal["sid"];
                if (principal.contains("label")) {
                    ace["principal"] = principal["label"];
                }
            }
        } else {
            ace["parsed"] = false;
        }
        aces.push_back(std::move(ace));
    }
    output["ace_count"] = information.AceCount;
    output["aces"] = std::move(aces);
    return S_OK;
}

bool AclGrants(const Json& security, std::string_view sid, ACCESS_MASK required) {
    if (!security.contains("aces") || !security["aces"].is_array()) {
        return false;
    }
    for (const auto& ace : security["aces"]) {
        if (ace.value("type", "") == "allowed" && ace.value("sid", "") == sid) {
            const ACCESS_MASK mask = ace.value<ACCESS_MASK>("access_mask", 0);
            if ((mask & required) == required) {
                return true;
            }
        }
    }
    return false;
}

HRESULT InspectRegistryHandleAcl(HKEY key, Json& output) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD result =
        ::GetSecurityInfo(key, SE_REGISTRY_KEY,
                          DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                          nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(result);
    }
    const ScopedLocalAlloc owner(descriptor);
    return InspectSecurityDescriptor(descriptor, output);
}

HRESULT InspectPathAcl(const std::filesystem::path& path, Json& output) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring native = path.wstring();
    const DWORD result = ::GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                                 DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
                                                     GROUP_SECURITY_INFORMATION,
                                                 nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(result);
    }
    const ScopedLocalAlloc owner(descriptor);
    return InspectSecurityDescriptor(descriptor, output);
}

Json InspectPath(const std::filesystem::path& path, bool include_acl, ACCESS_MASK required_mask) {
    Json output{
        {"path", WideToUtf8(RedactPathForReport(path.wstring()))},
    };
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        output["inspection"] = CheckResult("file", HRESULT_FROM_WIN32(::GetLastError()), "warning");
        return output;
    }
    output["exists"] = true;
    output["is_directory"] = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    output["size_bytes"] = (gsl::narrow_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    Json inspection = CheckResult("file", S_OK);
    output["inspection"] = inspection;
    if (!include_acl)
        return output;

    Json security;
    const HRESULT acl_result = InspectPathAcl(path, security);
    security["inspection"] = CheckResult("acl", acl_result, FAILED(acl_result) ? "warning" : "");
    if (SUCCEEDED(acl_result)) {
        security["all_packages_read_execute"] = AclGrants(security, "S-1-15-2-1", required_mask);
        security["all_restricted_packages_read_execute"] = AclGrants(security, "S-1-15-2-2", required_mask);
    }
    output["acl"] = std::move(security);
    return output;
}

} // namespace qiqi::diag::detail
