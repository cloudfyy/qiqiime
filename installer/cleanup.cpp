// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cleanup.h"
#include "internal.h"
#include "common/RAII.h"
#include "common/dictfiles.h"

#include <aclapi.h>
#include <objbase.h>
#include <sddl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <utility>
#include <vector>

namespace qiqi::installer {
namespace {

constexpr wchar_t kStateKey[] = L"Software\\QiQiIMEInstaller";
constexpr wchar_t kProfilesKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";
constexpr std::wstring_view kSecurity =
    L"O:BAG:BAD:P(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)(A;OICI;KR;;;BU)";
constexpr wchar_t kQuarantineDirectory[] = L"QiQiIMEInstaller.Pending";
constexpr std::wstring_view kQuarantineSecurity =
    L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;CI;0x001200A0;;;BU)";
constexpr std::wstring_view kTrustedInstallerSid =
    L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
constexpr DWORD kMaximumEntries = 4096;

struct Context {
    const detail::CleanupSandbox* sandbox = nullptr;
    HKEY Machine() const noexcept { return sandbox ? sandbox->machine : HKEY_LOCAL_MACHINE; }
    HKEY Users() const noexcept { return sandbox ? sandbox->users : HKEY_USERS; }
    const wchar_t* StatePath() const noexcept { return sandbox ? L"State" : kStateKey; }
    const wchar_t* ProfilesPath() const noexcept { return sandbox ? L"ProfileList" : kProfilesKey; }
};

constexpr bool Missing(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

HRESULT LastError() noexcept { return HRESULT_FROM_WIN32(::GetLastError()); }

void Record(HRESULT error, CleanupResult& result,
    CleanupOperation operation) noexcept {
    if (FAILED(error) && SUCCEEDED(result.error)) {
        result.error = error;
        result.operation = operation;
    }
}

void Trace(CleanupOperation* operation, CleanupOperation current) noexcept {
    if (operation) *operation = current;
}

bool ValidGuid(const wchar_t* text) noexcept {
    if (!text) return false;
    const std::wstring_view guid(text);
    if (guid.size() != 38 || gsl::at(guid, 0) != L'{' || gsl::at(guid, 37) != L'}') return false;
    for (gsl::index i = 1; i < 37; ++i) {
        const wchar_t digit = gsl::at(guid, i);
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (digit != L'-') return false;
        } else if (!((digit >= L'0' && digit <= L'9') ||
                     (digit >= L'a' && digit <= L'f') ||
                     (digit >= L'A' && digit <= L'F'))) {
            return false;
        }
    }
    GUID value{};
    return SUCCEEDED(::CLSIDFromString(text, &value)) && !::IsEqualGUID(value, GUID_NULL);
}

HRESULT NewGuid(std::wstring& text) {
    GUID value{};
    const HRESULT error = ::CoCreateGuid(&value);
    if (FAILED(error)) return error;
    std::array<wchar_t, 39> buffer{};
    if (!::StringFromGUID2(value, buffer.data(), gsl::narrow_cast<int>(buffer.size()))) return E_UNEXPECTED;
    text = buffer.data();
    return S_OK;
}

HRESULT SidFromString(const std::wstring& text, ScopedLocalAlloc& storage) noexcept {
    PSID sid = nullptr;
    if (!::ConvertStringSidToSidW(text.c_str(), &sid)) return LastError();
    storage.Reset(sid);
    if (!::IsValidSid(sid) ||
        ::IsWellKnownSid(sid, WinLocalSystemSid) ||
        ::IsWellKnownSid(sid, WinLocalServiceSid) ||
        ::IsWellKnownSid(sid, WinNetworkServiceSid)) return E_INVALIDARG;
    LPWSTR canonical = nullptr;
    if (!::ConvertSidToStringSidW(sid, &canonical)) return LastError();
    const ScopedLocalAlloc release(canonical);
    return wcscmp(text.c_str(), canonical) == 0 ? S_OK : E_INVALIDARG;
}

bool TrustedOwner(PSID owner, PSID user = nullptr) noexcept {
    return owner && ((user && ::EqualSid(owner, user)) ||
        ::IsWellKnownSid(owner, WinBuiltinAdministratorsSid) ||
        ::IsWellKnownSid(owner, WinLocalSystemSid));
}

HRESULT VerifyRegistryKey(HKEY key, const Context& context, bool root = false) noexcept {
    DWORD type = 0;
    const LSTATUS link = ::RegQueryValueExW(key, L"SymbolicLinkValue", nullptr, &type, nullptr, nullptr);
    if (link != ERROR_FILE_NOT_FOUND && link != ERROR_SUCCESS) return HRESULT_FROM_WIN32(link);
    if (link == ERROR_SUCCESS && type == REG_LINK) return E_ACCESSDENIED;
    if (context.sandbox) return S_OK;

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    const DWORD error = ::GetSecurityInfo(key, SE_REGISTRY_KEY,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr, &descriptor);
    const ScopedLocalAlloc release(descriptor);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    if (!TrustedOwner(owner) || !acl || !::IsValidAcl(acl)) return E_ACCESSDENIED;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!::GetSecurityDescriptorControl(descriptor, &control, &revision)) return LastError();
    if (root && !(control & SE_DACL_PROTECTED)) return E_ACCESSDENIED;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!::GetAce(acl, i, &raw)) return LastError();
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return E_ACCESSDENIED;
        auto* const ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        PSID sid = &ace->SidStart;
        if (!::IsValidSid(sid)) return E_ACCESSDENIED;
        constexpr ACCESS_MASK write = KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK |
            DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED;
        if ((ace->Mask & write) && !TrustedOwner(sid)) return E_ACCESSDENIED;
    }
    return S_OK;
}

HRESULT OpenKey(HKEY parent, const wchar_t* path, REGSAM access, const Context& context,
    ScopedRegKey& key, bool root = false) noexcept {
    const LSTATUS status = ::RegOpenKeyExW(parent, path, REG_OPTION_OPEN_LINK,
        access | KEY_QUERY_VALUE | READ_CONTROL | KEY_WOW64_64KEY, key.Put());
    if (status != ERROR_SUCCESS) return Missing(status) ? S_FALSE : HRESULT_FROM_WIN32(status);
    return VerifyRegistryKey(key.Get(), context, root);
}

HRESULT CreateKey(HKEY parent, const wchar_t* path, const Context& context,
    ScopedRegKey& key, bool root = false, DWORD options = REG_OPTION_NON_VOLATILE,
    DWORD* disposition = nullptr) noexcept {
    const HRESULT opened = OpenKey(parent, path, KEY_READ | KEY_WRITE, context, key, root);
    if (opened != S_FALSE) {
        if (opened == S_OK && disposition) *disposition = REG_OPENED_EXISTING_KEY;
        return opened;
    }
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!context.sandbox &&
        !::ConvertStringSecurityDescriptorToSecurityDescriptorW(kSecurity.data(), SDDL_REVISION_1,
            &descriptor, nullptr)) return LastError();
    const ScopedLocalAlloc release(descriptor);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const LSTATUS status = ::RegCreateKeyExW(parent, path, 0, nullptr, options,
        KEY_READ | KEY_WRITE | READ_CONTROL | KEY_WOW64_64KEY,
        descriptor ? &security : nullptr, key.Put(), disposition);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    return VerifyRegistryKey(key.Get(), context, root);
}

HRESULT SetNumber(HKEY key, const wchar_t* name, DWORD value) noexcept {
    return HRESULT_FROM_WIN32(::RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)));
}

HRESULT SetText(HKEY key, const wchar_t* name, const std::wstring& value) noexcept {
    if (value.size() >= 32767) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    return HRESULT_FROM_WIN32(::RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        gsl::narrow_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
}

HRESULT ReadNumber(HKEY key, const wchar_t* name, DWORD& value) noexcept {
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = ::RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size);
    if (status == ERROR_MORE_DATA) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    return type == REG_DWORD && size == sizeof(value) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT ReadText(HKEY key, const wchar_t* name, std::wstring& value, bool expandable = false) {
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    if ((type != REG_SZ && !(expandable && type == REG_EXPAND_SZ)) ||
        size < sizeof(wchar_t) || size > 65534 || size % sizeof(wchar_t)) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::vector<wchar_t> text(size / sizeof(wchar_t));
    const DWORD expected = size;
    status = ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &size);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    if (size != expected || (type != REG_SZ && !(expandable && type == REG_EXPAND_SZ)) ||
        text.back() != L'\0' || wcslen(text.data()) + 1 != text.size()) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    value.assign(text.data(), text.size() - 1);
    return S_OK;
}

HRESULT Flush(HKEY key) noexcept { return HRESULT_FROM_WIN32(::RegFlushKey(key)); }

HRESULT SaveResult(HKEY job, const CleanupResult& result) noexcept {
    HRESULT error = SetNumber(job, L"Complete", 0);
    if (FAILED(error)) return error;
    error = SetNumber(job, L"Error", gsl::narrow_cast<DWORD>(result.error));
    if (FAILED(error)) return error;
    error = SetNumber(job, L"Operation", static_cast<DWORD>(result.operation));
    if (FAILED(error)) return error;
    error = SetNumber(job, L"Deleted", result.deleted);
    if (FAILED(error)) return error;
    error = SetNumber(job, L"Scheduled", result.scheduled);
    if (FAILED(error)) return error;
    error = SetNumber(job, L"RestartRequired", result.restart_required ? 1 : 0);
    if (FAILED(error)) return error;
    error = SetNumber(job, L"Complete", 1);
    return FAILED(error) ? error : Flush(job);
}

HRESULT LoadResult(HKEY job, CleanupResult& result) noexcept {
    DWORD version = 0, complete = 0;
    HRESULT error = ReadNumber(job, L"Version", version);
    if (FAILED(error)) return error;
    if (version != 1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    error = ReadNumber(job, L"Complete", complete);
    if (FAILED(error)) return error;
    if (complete != 1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    DWORD stored_error = 0, deleted = 0, scheduled = 0, restart = 0, operation = 0;
    error = ReadNumber(job, L"Error", stored_error);
    if (FAILED(error)) return error;
    error = ReadNumber(job, L"Deleted", deleted);
    if (FAILED(error)) return error;
    error = ReadNumber(job, L"Scheduled", scheduled);
    if (FAILED(error)) return error;
    error = ReadNumber(job, L"RestartRequired", restart);
    if (FAILED(error)) return error;
    error = ReadNumber(job, L"Operation", operation);
    // Older version-1 jobs did not persist an operation.
    if (error == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) operation = 0;
    else if (FAILED(error)) return error;
    if (restart > 1 || (scheduled != 0 && restart == 0) ||
        operation > static_cast<DWORD>(CleanupOperation::delete_configuration_key) ||
        (stored_error == S_OK && operation != 0) ||
        (stored_error != S_OK && SUCCEEDED(gsl::narrow_cast<HRESULT>(stored_error)))) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    result = {gsl::narrow_cast<HRESULT>(stored_error), deleted, scheduled, restart != 0,
        static_cast<CleanupOperation>(operation)};
    return S_OK;
}

bool ValidLocalPath(const std::wstring& path) noexcept {
    if (path.size() < 3 || path.size() > 32000 ||
        !((gsl::at(path, 0) >= L'A' && gsl::at(path, 0) <= L'Z') ||
          (gsl::at(path, 0) >= L'a' && gsl::at(path, 0) <= L'z')) ||
        gsl::at(path, 1) != L':' || gsl::at(path, 2) != L'\\' || path.back() == L'\\') return false;
    size_t start = 3;
    for (size_t i = 3; i <= path.size(); ++i) {
        if (i != path.size() && gsl::at(path, gsl::narrow_cast<gsl::index>(i)) != L'\\') {
            const wchar_t character = gsl::at(path, gsl::narrow_cast<gsl::index>(i));
            if (character < 32 || wcschr(L"/:*?\"<>|%", character)) return false;
            continue;
        }
        const size_t count = i - start;
        if (count == 0 || count > 255 ||
            gsl::at(path, gsl::narrow_cast<gsl::index>(i - 1)) == L'.' ||
            gsl::at(path, gsl::narrow_cast<gsl::index>(i - 1)) == L' ') return false;
        start = i + 1;
    }
    return true;
}

std::wstring NativePath(const std::filesystem::path& path) {
    const auto text = path.wstring();
    return text.starts_with(L"\\\\?\\") ? text : L"\\\\?\\" + text;
}

HRESULT ResolveProfile(const CleanupRequest& request, const Context& context,
    std::filesystem::path& profile) {
    // ProfileList is the only production source; never consult elevated HKCU or environment.
    ScopedRegKey profiles;
    LSTATUS status = ::RegOpenKeyExW(context.Machine(), context.ProfilesPath(), REG_OPTION_OPEN_LINK,
        KEY_READ | KEY_WOW64_64KEY, profiles.Put());
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    ScopedRegKey entry;
    status = ::RegOpenKeyExW(profiles.Get(), request.sid.c_str(), REG_OPTION_OPEN_LINK,
        KEY_READ | KEY_WOW64_64KEY, entry.Put());
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    std::wstring text;
    const HRESULT error = ReadText(entry.Get(), L"ProfileImagePath", text, true);
    if (FAILED(error)) return error;
    // REG_EXPAND_SZ normally already contains C:\Users\name. Refuse unresolved
    // variables rather than expanding them in the elevated account's environment.
    if (!ValidLocalPath(text)) return E_INVALIDARG;
    const std::wstring drive = text.substr(0, 3);
    const UINT type = ::GetDriveTypeW(drive.c_str());
    if (type != DRIVE_FIXED && type != DRIVE_RAMDISK) return E_ACCESSDENIED;
    profile = text;
    return S_OK;
}

HRESULT VerifyFile(HANDLE file, bool directory, PSID user,
    BY_HANDLE_FILE_INFORMATION* identity = nullptr) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(file, &info)) return LastError();
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (!directory && ((info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) || info.nNumberOfLinks != 1))) {
        return E_ACCESSDENIED;
    }
    if (user) {
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD error = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
            &owner, nullptr, nullptr, nullptr, &descriptor);
        const ScopedLocalAlloc release(descriptor);
        if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
        if (!TrustedOwner(owner, user)) return E_ACCESSDENIED;
    }
    if (identity) *identity = info;
    return S_OK;
}

struct PinnedDirectory {
    std::filesystem::path path;
    std::vector<ScopedFileHandle> handles;
};

HRESULT PinDirectory(const std::filesystem::path& path, const std::filesystem::path& profile,
    PSID user, PinnedDirectory& pinned, CleanupOperation* operation = nullptr) {
    Trace(operation, CleanupOperation::open_source_directory);
    const std::wstring text = path.wstring();
    if (!ValidLocalPath(text)) return E_INVALIDARG;
    pinned.path = path;
    size_t end = 2;
    for (;;) {
        const std::wstring part = text.substr(0, end == 2 ? 3 : end);
        ScopedFileHandle directory(::CreateFileW((L"\\\\?\\" + part).c_str(),
            FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!directory) {
            const DWORD error = ::GetLastError();
            return Missing(error) ? S_FALSE : HRESULT_FROM_WIN32(error);
        }
        Trace(operation, CleanupOperation::verify_source_directory);
        const HRESULT verified = VerifyFile(directory.Get(), true,
            part.size() >= profile.wstring().size() ? user : nullptr);
        if (FAILED(verified)) return verified;
        pinned.handles.push_back(std::move(directory));
        if (end == text.size()) break;
        Trace(operation, CleanupOperation::open_source_directory);
        end = text.find(L'\\', end + 1);
        if (end == std::wstring::npos) end = text.size();
    }
    return S_OK;
}

bool ValidVolumeName(const std::wstring& text) noexcept {
    constexpr std::wstring_view prefix = L"\\\\?\\Volume";
    if (!text.starts_with(prefix) || text.size() != prefix.size() + 39 || text.back() != L'\\') return false;
    std::array<wchar_t, 39> guid{};
    std::ranges::copy(std::wstring_view(text).substr(prefix.size(), 38), guid.begin());
    return ValidGuid(guid.data());
}

HRESULT ValidateQuarantineDescriptor(PSECURITY_DESCRIPTOR descriptor, bool volume_root) noexcept {
    if (!descriptor || !::IsValidSecurityDescriptor(descriptor)) return E_ACCESSDENIED;
    PSID owner = nullptr;
    PACL acl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    if (!::GetSecurityDescriptorOwner(descriptor, &owner, &defaulted)) return LastError();
    if (!::GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted)) return LastError();
    if (!present || !acl || !::IsValidAcl(acl)) return E_ACCESSDENIED;
    ScopedLocalAlloc servicing;
    if (volume_root) {
        PSID sid = nullptr;
        if (!::ConvertStringSidToSidW(kTrustedInstallerSid.data(), &sid)) return LastError();
        servicing.Reset(sid);
    }
    const auto trusted = [&](PSID sid) noexcept {
        return TrustedOwner(sid) || (volume_root && sid && ::EqualSid(sid, servicing.Get()));
    };
    if (!trusted(owner)) return E_ACCESSDENIED;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!::GetSecurityDescriptorControl(descriptor, &control, &revision)) return LastError();
    if (!volume_root && !(control & SE_DACL_PROTECTED)) return E_ACCESSDENIED;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!::GetAce(acl, i, &raw)) return LastError();
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return E_ACCESSDENIED;
        auto* const ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        PSID sid = &ace->SidStart;
        if (!::IsValidSid(sid)) return E_ACCESSDENIED;
        if (header->AceFlags & INHERIT_ONLY_ACE) continue;
        if (trusted(sid)) continue;
        if (volume_root) {
            // DELETE_CHILD on the volume root could bypass the quarantine's
            // own protected DACL. Ordinary create-folder permission is safe.
            constexpr ACCESS_MASK unsafe = FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER |
                GENERIC_ALL | MAXIMUM_ALLOWED;
            if (ace->Mask & unsafe) return E_ACCESSDENIED;
        } else {
            constexpr ACCESS_MASK visibility =
                FILE_TRAVERSE | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE;
            if (ace->Mask & ~visibility) return E_ACCESSDENIED;
        }
    }
    return S_OK;
}

HRESULT QuarantineSecurity(HANDLE directory, const Context& context, bool volume_root) noexcept {
    if (context.sandbox) return S_OK;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD status = ::GetSecurityInfo(directory, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, &descriptor);
    const ScopedLocalAlloc release(descriptor);
    return status == ERROR_SUCCESS
        ? ValidateQuarantineDescriptor(descriptor, volume_root) : HRESULT_FROM_WIN32(status);
}

struct Quarantine {
    std::filesystem::path root;
    std::filesystem::path parent;
    std::filesystem::path scheduled_parent;
    std::wstring volume_name;
    DWORD serial = 0;
    ScopedFileHandle volume;
    PinnedDirectory root_pins;
    PinnedDirectory parent_pins;
};

HRESULT LocateQuarantine(const CleanupRequest& request, const std::filesystem::path& profile,
    const Context& context, Quarantine& quarantine, CleanupOperation* operation = nullptr) {
    Trace(operation, CleanupOperation::open_volume_root);
    const auto volume_root = profile.root_path();
    if (!ValidLocalPath(profile.wstring()) || volume_root.wstring().size() != 3) return E_INVALIDARG;
    quarantine.volume.Reset(::CreateFileW(NativePath(volume_root).c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!quarantine.volume) return LastError();
    BY_HANDLE_FILE_INFORMATION identity{};
    Trace(operation, CleanupOperation::verify_volume_root);
    HRESULT error = VerifyFile(quarantine.volume.Get(), true, nullptr, &identity);
    if (FAILED(error)) return error;
    Trace(operation, CleanupOperation::verify_volume_security);
    error = QuarantineSecurity(quarantine.volume.Get(), context, true);
    if (FAILED(error)) return error;
    quarantine.serial = identity.dwVolumeSerialNumber;
    std::array<wchar_t, MAX_PATH> name{};
    Trace(operation, CleanupOperation::read_volume_name);
    if (!::GetVolumeNameForVolumeMountPointW(volume_root.c_str(), name.data(),
        gsl::narrow_cast<DWORD>(name.size()))) return LastError();
    quarantine.volume_name = name.data();
    if (!ValidVolumeName(quarantine.volume_name)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (context.sandbox) {
        quarantine.root = context.sandbox->quarantine_root;
        if (!ValidLocalPath(quarantine.root.wstring()) ||
            _wcsicmp(quarantine.root.root_path().c_str(), volume_root.c_str())) return E_INVALIDARG;
        quarantine.scheduled_parent = quarantine.root / request.transaction;
    } else {
        quarantine.root = volume_root / kQuarantineDirectory;
        // A boot-time operation must not depend on a per-session DOS drive alias.
        quarantine.scheduled_parent = std::filesystem::path(quarantine.volume_name) /
            kQuarantineDirectory / request.transaction;
    }
    quarantine.parent = quarantine.root / request.transaction;
    return S_OK;
}

HRESULT PinQuarantineDirectory(const std::filesystem::path& path, const Context& context,
    DWORD volume_serial, bool create, PinnedDirectory& pins, CleanupOperation* operation) {
    if (create) {
        Trace(operation, CleanupOperation::create_quarantine_directory);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!context.sandbox &&
            !::ConvertStringSecurityDescriptorToSecurityDescriptorW(kQuarantineSecurity.data(),
                SDDL_REVISION_1, &descriptor, nullptr)) return LastError();
        const ScopedLocalAlloc release(descriptor);
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        if (!::CreateDirectoryW(NativePath(path).c_str(), descriptor ? &security : nullptr)) {
            const DWORD error = ::GetLastError();
            if (error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
        }
    }
    CleanupOperation pin_operation = CleanupOperation::unknown;
    const HRESULT opened = PinDirectory(path, {}, nullptr, pins, &pin_operation);
    Trace(operation, pin_operation == CleanupOperation::verify_source_directory
        ? CleanupOperation::verify_quarantine_directory : CleanupOperation::open_quarantine_directory);
    if (opened != S_OK) return opened;
    Trace(operation, CleanupOperation::verify_quarantine_directory);
    BY_HANDLE_FILE_INFORMATION identity{};
    if (!::GetFileInformationByHandle(pins.handles.back().Get(), &identity)) return LastError();
    if (identity.dwVolumeSerialNumber != volume_serial) return HRESULT_FROM_WIN32(ERROR_NOT_SAME_DEVICE);
    return QuarantineSecurity(pins.handles.back().Get(), context, false);
}

HRESULT PinQuarantine(Quarantine& quarantine, const Context& context, bool create,
    CleanupOperation* operation = nullptr) {
    const HRESULT error = PinQuarantineDirectory(quarantine.root, context, quarantine.serial, create,
        quarantine.root_pins, operation);
    if (error != S_OK) return error;
    return PinQuarantineDirectory(quarantine.parent, context, quarantine.serial, create,
        quarantine.parent_pins, operation);
}

HRESULT BootToken(const Context& context, HKEY root, bool create, std::wstring& token) {
    if (context.sandbox) {
        token = context.sandbox->boot_session;
        return token.empty() && !create ? S_OK : (ValidGuid(token.c_str()) ? S_OK : E_INVALIDARG);
    }
    ScopedRegKey boot;
    DWORD disposition = REG_OPENED_EXISTING_KEY;
    const HRESULT opened = create
        ? CreateKey(root, L"BootSession", context, boot, false, REG_OPTION_VOLATILE, &disposition)
        : OpenKey(root, L"BootSession", KEY_READ, context, boot);
    if (opened == S_FALSE) { token.clear(); return S_OK; }
    if (FAILED(opened)) return opened;
    if (disposition == REG_CREATED_NEW_KEY) {
        const HRESULT error = NewGuid(token);
        if (FAILED(error)) return error;
        return SetText(boot.Get(), L"Token", token);
    }
    const HRESULT error = ReadText(boot.Get(), L"Token", token);
    return FAILED(error) ? error : (ValidGuid(token.c_str()) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
}

HRESULT RenameFile(HANDLE file, const std::filesystem::path& target) {
    const std::wstring text = NativePath(target);
    // Win32 also consumes a terminated path; FileNameLength excludes that NUL.
    const size_t size = offsetof(FILE_RENAME_INFO, FileName) + (text.size() + 1) * sizeof(wchar_t);
    std::vector<BYTE> buffer(size);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = gsl::narrow_cast<DWORD>(text.size() * sizeof(wchar_t));
    const auto filename = gsl::span<BYTE>(buffer).subspan(offsetof(FILE_RENAME_INFO, FileName));
    std::memcpy(filename.data(), text.c_str(), filename.size_bytes());
    return ::SetFileInformationByHandle(file, FileRenameInfo, rename, gsl::narrow_cast<DWORD>(size))
        ? S_OK : LastError();
}

struct Operation {
    const CleanupRequest& request;
    const Context& context;
    HKEY root;
    HKEY job;
    std::filesystem::path profile;
    PSID user;
    CleanupResult& result;
    CleanupOperation current = CleanupOperation::unknown;
};

HRESULT DeferFile(Operation& operation, HANDLE file, const BY_HANDLE_FILE_INFORMATION& identity) {
    if (operation.context.sandbox && !operation.context.sandbox->schedule) return E_INVALIDARG;
    Quarantine quarantine;
    HRESULT error = LocateQuarantine(operation.request, operation.profile, operation.context, quarantine,
        &operation.current);
    if (FAILED(error)) return error;
    operation.current = CleanupOperation::verify_volume_root;
    if (quarantine.serial != identity.dwVolumeSerialNumber) return HRESULT_FROM_WIN32(ERROR_NOT_SAME_DEVICE);
    error = PinQuarantine(quarantine, operation.context, true, &operation.current);
    if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) : error;
    std::wstring id;
    operation.current = CleanupOperation::generate_quarantine_name;
    error = NewGuid(id);
    if (FAILED(error)) return error;
    const std::wstring leaf = id + L".pending";
    const auto target = quarantine.parent / leaf;
    const auto scheduled_target = quarantine.scheduled_parent / leaf;
    std::wstring boot;
    operation.current = CleanupOperation::boot_session;
    error = BootToken(operation.context, operation.root, true, boot);
    if (FAILED(error)) return error;
    operation.current = CleanupOperation::persist_pending_target;
    if (operation.context.sandbox && operation.context.sandbox->before_pending_persist) {
        error = operation.context.sandbox->before_pending_persist();
        if (FAILED(error)) return error;
    }
    ScopedRegKey targets, pending;
    error = CreateKey(operation.job, L"Targets", operation.context, targets);
    if (FAILED(error)) return error;
    error = CreateKey(targets.Get(), id.c_str(), operation.context, pending);
    if (FAILED(error)) return error;
    // Commit the recovery identity before touching the reusable original pathname.
    for (const auto& item : {
            std::pair{L"BootToken", boot}, std::pair{L"Path", scheduled_target.wstring()},
            std::pair{L"VolumeName", quarantine.volume_name}}) {
        error = SetText(pending.Get(), item.first, item.second);
        if (FAILED(error)) return error;
    }
    for (const auto& item : {
            std::pair{L"Volume", identity.dwVolumeSerialNumber},
            std::pair{L"IndexHigh", identity.nFileIndexHigh},
            std::pair{L"IndexLow", identity.nFileIndexLow},
            std::pair{L"Status", DWORD{0}}}) {
        error = SetNumber(pending.Get(), item.first, item.second);
        if (FAILED(error)) return error;
    }
    error = Flush(pending.Get());
    if (FAILED(error)) return error;
    operation.current = CleanupOperation::rename_to_quarantine;
    error = RenameFile(file, target);
    if (FAILED(error)) return error;
    // Keep status 0 until dispatch succeeds. A crash or persistence failure in
    // this window is incomplete, not evidence that reboot will finish the job.
    operation.current = CleanupOperation::schedule_reboot_delete;
    const HRESULT scheduled = operation.context.sandbox
        ? operation.context.sandbox->schedule(scheduled_target)
        : (::MoveFileExW(scheduled_target.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) ? S_OK : LastError());
    if (SUCCEEDED(scheduled)) {
        ++operation.result.scheduled;
        operation.result.restart_required = true;
    }
    error = SetNumber(pending.Get(), L"Status", SUCCEEDED(scheduled) ? 2 : 3);
    if (SUCCEEDED(error)) error = Flush(pending.Get());
    if (SUCCEEDED(scheduled)) operation.current = CleanupOperation::persist_pending_target;
    return FAILED(scheduled) ? scheduled : error;
}

HRESULT RemoveFile(Operation& operation, const PinnedDirectory& parent, const wchar_t* leaf) {
    const auto path = parent.path / leaf;
    operation.current = CleanupOperation::open_source_file;
    const ScopedFileHandle file(::CreateFileW(NativePath(path).c_str(),
        DELETE | FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        const DWORD error = ::GetLastError();
        return Missing(error) ? S_OK : HRESULT_FROM_WIN32(error);
    }
    BY_HANDLE_FILE_INFORMATION identity{};
    operation.current = CleanupOperation::verify_source_file;
    HRESULT error = VerifyFile(file.Get(), false, operation.user, &identity);
    if (FAILED(error)) return error;
    FILE_DISPOSITION_INFO disposition{TRUE};
    operation.current = CleanupOperation::delete_source_file;
    if (::SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
        ++operation.result.deleted;
        return S_OK;
    }
    const DWORD deletion_error = ::GetLastError();
    // DELETE access has already been checked by the kernel, and readonly,
    // directory, hard-link and reparse cases were rejected before this fallback.
    if (deletion_error != ERROR_USER_MAPPED_FILE && deletion_error != ERROR_SHARING_VIOLATION &&
        deletion_error != ERROR_ACCESS_DENIED) return HRESULT_FROM_WIN32(deletion_error);
    operation.current = CleanupOperation::verify_source_file;
    error = VerifyFile(file.Get(), false, operation.user);
    if (FAILED(error)) return error;
    return DeferFile(operation, file.Get(), identity);
}

void RemoveData(Operation& operation) {
    const auto directory = operation.profile / L"AppData" / L"Local" / L"QiQiIME";
    PinnedDirectory root;
    const HRESULT opened = PinDirectory(directory, operation.profile, operation.user, root, &operation.current);
    Record(opened, operation.result, operation.current);
    if (opened != S_OK) return;
    for (const auto* name : dictionary_files::kUserFiles) {
        const HRESULT error = RemoveFile(operation, root, name);
        Record(error, operation.result, operation.current);
    }
    const auto remove_directory = [&](const wchar_t* name, bool cache) {
        PinnedDirectory child;
        const HRESULT error = PinDirectory(directory / name, operation.profile, operation.user, child,
            &operation.current);
        Record(error, operation.result, operation.current);
        if (error != S_OK) return;
        if (!cache) {
            for (const auto* file : dictionary_files::kSyncFiles) {
                const HRESULT removed = RemoveFile(operation, child, file);
                Record(removed, operation.result, operation.current);
            }
            return;
        }
        WIN32_FIND_DATAW found{};
        const HANDLE search = ::FindFirstFileW(NativePath(child.path / L"*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD find_error = ::GetLastError();
            if (!Missing(find_error)) Record(HRESULT_FROM_WIN32(find_error), operation.result,
                CleanupOperation::enumerate_cache);
            return;
        }
        const auto close = gsl::finally([search]() noexcept { ::FindClose(search); });
        DWORD count = 0;
        do {
            if (++count > kMaximumEntries) {
                Record(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW), operation.result,
                    CleanupOperation::enumerate_cache);
                break;
            }
            if (dictionary_files::IsCompiledCache(std::data(found.cFileName))) {
                const HRESULT removed = RemoveFile(operation, child, std::data(found.cFileName));
                Record(removed, operation.result, operation.current);
            }
        } while (::FindNextFileW(search, &found));
        if (count <= kMaximumEntries && ::GetLastError() != ERROR_NO_MORE_FILES) {
            Record(LastError(), operation.result, CleanupOperation::enumerate_cache);
        }
    };
    for (const auto* name : dictionary_files::kSyncDirectories) remove_directory(name, false);
    remove_directory(L"cache", true);
}

HRESULT RejectRegistryLink(HKEY key) noexcept {
    DWORD type = 0;
    const LSTATUS status = ::RegQueryValueExW(key, L"SymbolicLinkValue", nullptr, &type, nullptr, nullptr);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(status);
    return status == ERROR_SUCCESS && type == REG_LINK ? E_ACCESSDENIED : S_OK;
}

HRESULT RemoveRegistryTree(HKEY key, DWORD& remaining, CleanupOperation& operation) {
    operation = CleanupOperation::check_configuration_link;
    const HRESULT verified = RejectRegistryLink(key);
    if (FAILED(verified)) return verified;
    operation = CleanupOperation::enumerate_configuration_keys;
    if (remaining == 0) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    --remaining;
    for (;;) {
        operation = CleanupOperation::enumerate_configuration_keys;
        std::array<wchar_t, 256> name{};
        DWORD length = gsl::narrow_cast<DWORD>(name.size());
        const LSTATUS status = ::RegEnumKeyExW(key, 0, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        ScopedRegKey child;
        operation = CleanupOperation::open_configuration_key;
        const LSTATUS opened = ::RegOpenKeyExW(key, name.data(), REG_OPTION_OPEN_LINK,
            KEY_READ | DELETE | KEY_WOW64_64KEY, child.Put());
        if (opened != ERROR_SUCCESS) return HRESULT_FROM_WIN32(opened);
        const HRESULT error = RemoveRegistryTree(child.Get(), remaining, operation);
        if (FAILED(error)) return error;
    }
    // Unlike a recursive pathname API, NtDeleteKey deletes this exact opened
    // object even if the user concurrently renames/replaces one of its ancestors.
    operation = CleanupOperation::delete_configuration_key;
    using DeleteKey = LONG (NTAPI*)(HANDLE);
    const HMODULE module = ::GetModuleHandleW(L"ntdll.dll");
    if (!module) return LastError();
    const auto remove = reinterpret_cast<DeleteKey>(::GetProcAddress(module, "NtDeleteKey"));
    if (!remove) return LastError();
    const LONG status = remove(key);
    return status < 0 ? HRESULT_FROM_NT(status) : S_OK;
}

HRESULT RemoveConfiguration(const CleanupRequest& request, const Context& context,
    CleanupOperation& operation) {
    // Opening the exact user hive also distinguishes "not loaded" from an empty
    // configuration; silently treating an unavailable hive as clean would lie.
    ScopedRegKey user;
    operation = CleanupOperation::open_configuration_key;
    const LSTATUS opened = ::RegOpenKeyExW(context.Users(), request.sid.c_str(), REG_OPTION_OPEN_LINK,
        KEY_READ | KEY_WOW64_64KEY, user.Put());
    if (opened != ERROR_SUCCESS) return HRESULT_FROM_WIN32(opened);
    operation = CleanupOperation::check_configuration_link;
    HRESULT error = RejectRegistryLink(user.Get());
    if (FAILED(error)) return error;
    ScopedRegKey software, preferences;
    operation = CleanupOperation::open_configuration_key;
    LSTATUS status = ::RegOpenKeyExW(user.Get(), L"Software", REG_OPTION_OPEN_LINK,
        KEY_READ | KEY_WOW64_64KEY, software.Put());
    if (Missing(status)) return S_OK;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    operation = CleanupOperation::check_configuration_link;
    error = RejectRegistryLink(software.Get());
    if (FAILED(error)) return error;
    operation = CleanupOperation::open_configuration_key;
    status = ::RegOpenKeyExW(software.Get(), L"QiQiIME", REG_OPTION_OPEN_LINK,
        KEY_READ | DELETE | KEY_WOW64_64KEY, preferences.Put());
    if (Missing(status)) return S_OK;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    DWORD remaining = kMaximumEntries;
    return RemoveRegistryTree(preferences.Get(), remaining, operation);
}

HRESULT OpenJob(const CleanupRequest& request, const Context& context,
    ScopedRegKey& root, ScopedRegKey& job) {
    HRESULT error = ValidateRequest(request);
    if (FAILED(error)) return error;
    error = OpenKey(context.Machine(), context.StatePath(), KEY_READ, context, root, true);
    if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : error;
    ScopedRegKey jobs;
    error = OpenKey(root.Get(), L"Jobs", KEY_READ, context, jobs);
    if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : error;
    error = OpenKey(jobs.Get(), request.transaction.c_str(), KEY_READ, context, job);
    if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : error;
    std::wstring sid;
    error = ReadText(job.Get(), L"SID", sid);
    return FAILED(error) ? error : (sid == request.sid ? S_OK : E_ACCESSDENIED);
}

CleanupResult Perform(const CleanupRequest& request, const Context& context, bool system_restart_required) {
    CleanupResult result;
    Record(ValidateRequest(request), result, CleanupOperation::validate_request);
    if (FAILED(result.error)) return result;
    ScopedRegKey root, jobs, job;
    Record(CreateKey(context.Machine(), context.StatePath(), context, root, true), result,
        CleanupOperation::open_state);
    if (FAILED(result.error)) return result;
    Record(CreateKey(root.Get(), L"Jobs", context, jobs), result, CleanupOperation::open_job);
    if (FAILED(result.error)) return result;
    DWORD disposition = 0;
    Record(CreateKey(jobs.Get(), request.transaction.c_str(), context, job, false,
        REG_OPTION_NON_VOLATILE, &disposition), result, CleanupOperation::open_job);
    if (FAILED(result.error)) return result;
    if (disposition == REG_OPENED_EXISTING_KEY) {
        std::wstring sid;
        Record(ReadText(job.Get(), L"SID", sid), result, CleanupOperation::read_job);
        if (SUCCEEDED(result.error) && sid != request.sid) {
            Record(E_ACCESSDENIED, result, CleanupOperation::read_job);
        }
        if (SUCCEEDED(result.error)) Record(LoadResult(job.Get(), result), result, CleanupOperation::read_job);
        return result;
    }
    Record(SetText(job.Get(), L"SID", request.sid), result, CleanupOperation::persist_job);
    Record(SetNumber(job.Get(), L"Version", 1), result, CleanupOperation::persist_job);
    Record(SetNumber(job.Get(), L"Complete", 0), result, CleanupOperation::persist_job);
    Record(SetNumber(job.Get(), L"SystemRestartRequired", system_restart_required ? 1 : 0), result,
        CleanupOperation::persist_job);
    if (FAILED(result.error)) return result;
    if (system_restart_required) {
        result.restart_required = true;
        std::wstring boot;
        Record(BootToken(context, root.Get(), true, boot), result, CleanupOperation::boot_session);
        if (SUCCEEDED(result.error)) Record(SetText(job.Get(), L"BootToken", boot), result,
            CleanupOperation::persist_job);
        if (SUCCEEDED(result.error)) Record(Flush(job.Get()), result, CleanupOperation::persist_job);
        if (FAILED(result.error)) {
            Record(SaveResult(job.Get(), result), result, CleanupOperation::persist_result);
            return result;
        }
    }
    if (request.configuration) {
        CleanupOperation operation = CleanupOperation::remove_configuration;
        const HRESULT error = RemoveConfiguration(request, context, operation);
        Record(error, result, operation);
    }
    if (request.dictionaries) {
        std::filesystem::path profile;
        const HRESULT resolved = ResolveProfile(request, context, profile);
        Record(resolved, result, CleanupOperation::resolve_profile);
        if (SUCCEEDED(resolved)) {
            const HRESULT persisted = SetText(job.Get(), L"Profile", profile.wstring());
            Record(persisted, result, CleanupOperation::persist_job);
            if (SUCCEEDED(persisted)) {
                ScopedLocalAlloc user;
                const HRESULT parsed = SidFromString(request.sid, user);
                Record(parsed, result, CleanupOperation::validate_request);
                if (SUCCEEDED(parsed)) {
                    Operation operation{request, context, root.Get(), job.Get(), profile, user.Get(), result};
                    RemoveData(operation);
                }
            }
        }
    }
    Record(SaveResult(job.Get(), result), result, CleanupOperation::persist_result);
    return result;
}

HRESULT EnumerateKeys(HKEY key, std::vector<std::wstring>& names) {
    for (DWORD index = 0; index <= kMaximumEntries; ++index) {
        std::array<wchar_t, 256> name{};
        DWORD size = gsl::narrow_cast<DWORD>(name.size());
        const LSTATUS status = ::RegEnumKeyExW(key, index, name.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) return S_OK;
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        if (index == kMaximumEntries) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        names.emplace_back(name.data(), size);
    }
    return E_UNEXPECTED;
}

HRESULT CheckTarget(HKEY pending, const std::wstring& id, const CleanupRequest& request, const Context& context,
    const std::filesystem::path& profile, const std::wstring& current_boot, PendingState& state) {
    std::wstring stored_path, stored_volume, boot;
    HRESULT error = ReadText(pending, L"Path", stored_path);
    if (FAILED(error)) return error;
    error = ReadText(pending, L"BootToken", boot);
    if (FAILED(error)) return error;
    error = ReadText(pending, L"VolumeName", stored_volume);
    if (FAILED(error)) return error;
    if (!ValidGuid(boot.c_str()) || !ValidVolumeName(stored_volume)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    DWORD status = 0, volume = 0, high = 0, low = 0;
    for (const auto& item : {std::pair{L"Status", &status}, std::pair{L"Volume", &volume},
                            std::pair{L"IndexHigh", &high}, std::pair{L"IndexLow", &low}}) {
        error = ReadNumber(pending, item.first, *item.second);
        if (FAILED(error)) return error;
    }
    if (status > 3 || status == 1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    const bool waiting_for_boot = status == 2 && boot == current_boot;
    if (waiting_for_boot && state != PendingState::incomplete) state = PendingState::restart_required;
    ScopedLocalAlloc sid;
    error = SidFromString(request.sid, sid);
    if (FAILED(error)) return error;
    Quarantine quarantine;
    // The source profile was pinned and ownership-checked during elevated
    // isolation. A later caller need not have access to that private profile:
    // ProfileList binds the expected volume, and only the protected quarantine
    // is traversed here (including when checking another user's fulfilled job).
    error = LocateQuarantine(request, profile, context, quarantine);
    if (FAILED(error)) return error;
    if (stored_volume != quarantine.volume_name || volume != quarantine.serial) return E_ACCESSDENIED;
    const auto leaf = id + L".pending";
    const auto expected = quarantine.scheduled_parent / leaf;
    // Never open a registry-supplied path. Require the exact generated grammar
    // and current trusted profile volume, then use the independently built path.
    if (stored_path != expected.wstring()) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    error = PinQuarantine(quarantine, context, false);
    if (error == S_FALSE) return S_OK;
    if (FAILED(error)) return error;
    const auto path = quarantine.parent / leaf;
    const ScopedFileHandle file(::CreateFileW(NativePath(path).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        const DWORD file_error = ::GetLastError();
        return Missing(file_error) ? S_OK : HRESULT_FROM_WIN32(file_error);
    }
    BY_HANDLE_FILE_INFORMATION identity{};
    error = VerifyFile(file.Get(), false, sid.Get(), &identity);
    if (FAILED(error)) return error;
    if (volume != identity.dwVolumeSerialNumber || high != identity.nFileIndexHigh || low != identity.nFileIndexLow) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (!waiting_for_boot) state = PendingState::incomplete;
    return S_OK;
}

HRESULT CheckPending(PendingState& state, const Context& context) {
    // No create, delete, flush, security repair, or marker retirement on this path.
    state = PendingState::incomplete;
    ScopedRegKey root, jobs;
    HRESULT error = OpenKey(context.Machine(), context.StatePath(), KEY_READ, context, root, true);
    if (error == S_FALSE) { state = PendingState::clear; return S_OK; }
    if (FAILED(error)) return error;
    error = OpenKey(root.Get(), L"Jobs", KEY_READ, context, jobs);
    if (error == S_FALSE) { state = PendingState::clear; return S_OK; }
    if (FAILED(error)) return error;
    std::wstring current_boot;
    error = BootToken(context, root.Get(), false, current_boot);
    if (FAILED(error)) return error;
    std::vector<std::wstring> transactions;
    error = EnumerateKeys(jobs.Get(), transactions);
    if (FAILED(error)) return error;
    PendingState observed = PendingState::clear;
    for (const auto& transaction : transactions) {
        if (!ValidGuid(transaction.c_str())) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        ScopedRegKey job, targets;
        error = OpenKey(jobs.Get(), transaction.c_str(), KEY_READ, context, job);
        if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : error;
        CleanupRequest request;
        request.transaction = transaction;
        error = ReadText(job.Get(), L"SID", request.sid);
        if (FAILED(error)) return error;
        error = ValidateRequest(request);
        if (FAILED(error)) return error;
        DWORD version = 0;
        error = ReadNumber(job.Get(), L"Version", version);
        if (FAILED(error)) return error;
        if (version != 1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        DWORD system_restart = 0;
        error = ReadNumber(job.Get(), L"SystemRestartRequired", system_restart);
        if (FAILED(error)) return error;
        if (system_restart > 1) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (system_restart) {
            std::wstring boot;
            error = ReadText(job.Get(), L"BootToken", boot);
            if (FAILED(error)) return error;
            if (!ValidGuid(boot.c_str())) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            if (boot == current_boot && observed != PendingState::incomplete) observed = PendingState::restart_required;
        }
        error = OpenKey(job.Get(), L"Targets", KEY_READ, context, targets);
        if (error == S_FALSE) continue;
        if (FAILED(error)) return error;
        std::vector<std::wstring> entries;
        error = EnumerateKeys(targets.Get(), entries);
        if (FAILED(error)) return error;
        if (entries.empty()) continue;
        std::filesystem::path profile;
        error = ResolveProfile(request, context, profile);
        if (FAILED(error)) return error;
        std::wstring stored_profile;
        error = ReadText(job.Get(), L"Profile", stored_profile);
        if (FAILED(error)) return error;
        if (_wcsicmp(stored_profile.c_str(), profile.c_str())) return E_ACCESSDENIED;
        for (const auto& entry : entries) {
            if (!ValidGuid(entry.c_str())) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            ScopedRegKey target;
            error = OpenKey(targets.Get(), entry.c_str(), KEY_READ, context, target);
            if (error != S_OK) return error == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : error;
            error = CheckTarget(target.Get(), entry, request, context, profile, current_boot, observed);
            if (FAILED(error)) return error;
        }
    }
    state = observed;
    return S_OK;
}

} // namespace

HRESULT ValidateRequest(const CleanupRequest& request) noexcept {
    if (request.sid.empty() || request.sid.size() > SECURITY_MAX_SID_STRING_CHARACTERS ||
        request.sid.find(L'\0') != std::wstring::npos ||
        request.transaction.size() != 38 || !ValidGuid(request.transaction.c_str())) return E_INVALIDARG;
    ScopedLocalAlloc sid;
    return SidFromString(request.sid, sid);
}

const wchar_t* CleanupOperationName(CleanupOperation operation) noexcept {
    switch (operation) {
    case CleanupOperation::unknown: return L"unknown";
    case CleanupOperation::validate_request: return L"validate-request";
    case CleanupOperation::open_state: return L"open-state-registry";
    case CleanupOperation::open_job: return L"open-job-registry";
    case CleanupOperation::read_job: return L"read-job-registry";
    case CleanupOperation::persist_job: return L"persist-job-registry";
    case CleanupOperation::remove_configuration: return L"remove-configuration-registry";
    case CleanupOperation::resolve_profile: return L"resolve-profile-registry";
    case CleanupOperation::open_source_directory: return L"CreateFileW(source-directory)";
    case CleanupOperation::verify_source_directory: return L"verify-source-directory";
    case CleanupOperation::enumerate_cache: return L"FindFirstFileW/FindNextFileW(cache)";
    case CleanupOperation::open_source_file: return L"CreateFileW(source-file)";
    case CleanupOperation::verify_source_file: return L"verify-source-file";
    case CleanupOperation::delete_source_file: return L"SetFileInformationByHandle(FileDispositionInfo)";
    case CleanupOperation::open_volume_root: return L"CreateFileW(volume-root)";
    case CleanupOperation::verify_volume_root: return L"verify-volume-root";
    case CleanupOperation::verify_volume_security: return L"verify-volume-root-security";
    case CleanupOperation::read_volume_name: return L"GetVolumeNameForVolumeMountPointW";
    case CleanupOperation::create_quarantine_directory: return L"CreateDirectoryW(quarantine)";
    case CleanupOperation::open_quarantine_directory: return L"CreateFileW(quarantine-directory)";
    case CleanupOperation::verify_quarantine_directory: return L"verify-quarantine-directory";
    case CleanupOperation::boot_session: return L"boot-session-registry";
    case CleanupOperation::persist_pending_target: return L"persist-pending-target-registry";
    case CleanupOperation::rename_to_quarantine: return L"SetFileInformationByHandle(FileRenameInfo)";
    case CleanupOperation::schedule_reboot_delete: return L"MoveFileExW(delay-until-reboot)";
    case CleanupOperation::persist_result: return L"persist-cleanup-result-registry";
    case CleanupOperation::generate_quarantine_name: return L"CoCreateGuid(quarantine-name)";
    case CleanupOperation::open_configuration_key: return L"RegOpenKeyExW(configuration)";
    case CleanupOperation::check_configuration_link: return L"RegQueryValueExW(configuration-link)";
    case CleanupOperation::enumerate_configuration_keys: return L"RegEnumKeyExW(configuration)";
    case CleanupOperation::delete_configuration_key: return L"NtDeleteKey(configuration)";
    }
    return L"unknown";
}

CleanupResult PerformCleanup(const CleanupRequest& request, bool system_restart_required) {
    return Perform(request, {}, system_restart_required);
}

HRESULT ReadCleanupResult(const CleanupRequest& request, CleanupResult& result) {
    ScopedRegKey root, job;
    const HRESULT error = OpenJob(request, {}, root, job);
    return FAILED(error) ? error : LoadResult(job.Get(), result);
}

HRESULT CheckPendingCleanup(PendingState& state) { return CheckPending(state, {}); }

CleanupResult detail::PerformCleanup(const CleanupRequest& request, const CleanupSandbox& sandbox,
    bool system_restart_required) {
    return Perform(request, {&sandbox}, system_restart_required);
}

HRESULT detail::ReadCleanupResult(const CleanupRequest& request, CleanupResult& result,
    const CleanupSandbox& sandbox) {
    ScopedRegKey root, job;
    const HRESULT error = OpenJob(request, {&sandbox}, root, job);
    return FAILED(error) ? error : LoadResult(job.Get(), result);
}

HRESULT detail::CheckPendingCleanup(PendingState& state, const CleanupSandbox& sandbox) {
    return CheckPending(state, {&sandbox});
}

HRESULT detail::ValidateQuarantineSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor, bool volume_root) noexcept {
    return ValidateQuarantineDescriptor(descriptor, volume_root);
}

} // namespace qiqi::installer
