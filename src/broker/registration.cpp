// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/registration.h"

#include "common/appcontainer.h"
#include "common/comobj.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/RAII.h"
#include "common/logger.h"
#include "common/version.h"

#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qiqi::broker {
namespace {

constexpr ULONG
    kLocalDcomAccessRights =
    COM_RIGHTS_EXECUTE |
    COM_RIGHTS_EXECUTE_LOCAL;
constexpr ULONG
    kLocalDcomLaunchRights =
    kLocalDcomAccessRights |
    COM_RIGHTS_ACTIVATE_LOCAL;

struct RegistryRoot {
    HKEY key = HKEY_LOCAL_MACHINE;
    std::wstring prefix;
    REGSAM view = 0;
};

std::pair<HKEY, std::wstring> RegistryBase() {
    if (::GetEnvironmentVariableW(
            L"QIQI_CONFIG_BROKER_PER_USER",
            nullptr,
            0) != 0) {
        return {
            HKEY_CURRENT_USER,
            L"Software\\Classes",
        };
    }
    return {
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Classes",
    };
}

std::filesystem::path Proxy32Path(
    const std::filesystem::path& executable) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(
            L"QIQI_CONFIG_BROKER_PROXY32_PATH",
            buffer.data(),
            gsl::narrow_cast<DWORD>(
                buffer.size()));
    if (length > 0 &&
        length < buffer.size()) {
        return std::filesystem::path(
            buffer.data());
    }
    return executable.parent_path()
        .parent_path() /
        L"x86" /
        L"QiQiIMEConfigProxy.dll";
}

std::wstring GuidString(REFGUID guid) {
    std::array<wchar_t, 64> value{};
    const int length = ::StringFromGUID2(
        guid,
        value.data(),
        gsl::narrow_cast<int>(
            value.size()));
    if (length == 0) {
        throw std::system_error(
            E_UNEXPECTED,
            std::generic_category(),
            "StringFromGUID2");
    }
    return value.data();
}

class SidOwner final {
public:
    enum class Allocation {
        Local,
        Sid,
    };

    SidOwner(
        PSID value,
        Allocation allocation) noexcept
        : value_(value),
          allocation_(allocation) {
    }

    ~SidOwner() noexcept {
        Release();
    }

    SidOwner(
        const SidOwner&) = delete;
    SidOwner& operator=(
        const SidOwner&) = delete;

    SidOwner(
        SidOwner&& other) noexcept
        : value_(
              std::exchange(
                  other.value_,
                  nullptr)),
          allocation_(
              other.allocation_) {
    }

    SidOwner& operator=(
        SidOwner&& other) noexcept {
        if (this != &other) {
            Release();
            value_ =
                std::exchange(
                    other.value_,
                    nullptr);
            allocation_ =
                other.allocation_;
        }
        return *this;
    }

    PSID Get() const noexcept {
        return value_;
    }

private:
    void Release() noexcept {
        if (!value_) {
            return;
        }
        if (allocation_ ==
            Allocation::Local) {
            ::LocalFree(value_);
        } else {
            ::FreeSid(value_);
        }
        value_ = nullptr;
    }

    PSID value_ = nullptr;
    Allocation allocation_ =
        Allocation::Local;
};

HRESULT SetString(
    HKEY key,
    const wchar_t* name,
    std::wstring_view value) noexcept {
    const LSTATUS status = ::RegSetValueExW(
        key,
        name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(
            value.data()),
        gsl::narrow_cast<DWORD>(
            (value.size() + 1) *
            sizeof(wchar_t)));
    return HRESULT_FROM_WIN32(status);
}

HRESULT SetDword(
    HKEY key,
    const wchar_t* name,
    DWORD value) noexcept {
    const LSTATUS status = ::RegSetValueExW(
        key,
        name,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(
            &value),
        sizeof(value));
    return HRESULT_FROM_WIN32(status);
}

HRESULT AppendSid(
    std::wstring_view text,
    std::vector<PSID>& sids,
    std::vector<SidOwner>& owners) {
    PSID sid = nullptr;
    if (!::ConvertStringSidToSidW(
            std::wstring(text).c_str(),
            &sid)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    owners.emplace_back(
        sid,
        SidOwner::Allocation::Local);
    sids.push_back(sid);
    return S_OK;
}

HRESULT AppendWindowsSearchHostSids(
    std::vector<PSID>& sids,
    std::vector<SidOwner>& owners) {
    std::vector<std::wstring> sid_texts;
    const HRESULT discovery =
        DiscoverWindowsSearchPackageSids(
            sid_texts);
    if (FAILED(discovery)) {
        return discovery;
    }
    for (const auto& sid_text :
         sid_texts) {
        const HRESULT result =
            AppendSid(
                sid_text,
                sids,
                owners);
        if (FAILED(result)) {
            return result;
        }
    }
    return S_OK;
}

HRESULT AppendCapabilitySids(
    std::vector<PSID>& sids,
    std::vector<SidOwner>& owners) {
    DerivedCapabilitySids lpac_com(
        L"lpacCom");
    const HRESULT derived =
        lpac_com.Build();
    if (FAILED(derived)) {
        return derived;
    }
    const auto capabilities =
        lpac_com.Capabilities();
    if (capabilities.empty()) {
        return S_OK;
    }
    owners.reserve(
        owners.size() +
        capabilities.size());
    sids.reserve(
        sids.size() +
        capabilities.size());
    for (PSID& capability :
         capabilities) {
        if (!capability ||
            !::IsValidSid(capability)) {
            return E_UNEXPECTED;
        }
        const PSID transferred =
            capability;
        owners.emplace_back(
            transferred,
            SidOwner::Allocation::Local);
        capability = nullptr;
        sids.push_back(transferred);
    }
    return S_OK;
}

HRESULT AppendAppContainerAccessSids(
    std::vector<PSID>& sids,
    std::vector<SidOwner>& owners) {
    // ALL APPLICATION PACKAGES (ordinary AppContainer callers).
    HRESULT result = AppendSid(
        L"S-1-15-2-1",
        sids,
        owners);
    if (FAILED(result)) return result;
    // ALL RESTRICTED APPLICATION PACKAGES (LPAC callers).
    result = AppendSid(
        L"S-1-15-2-2",
        sids,
        owners);
    if (FAILED(result)) return result;
    // Group/capability SIDs derived from the named lpacCom capability.
    result = AppendCapabilitySids(
        sids,
        owners);
    if (FAILED(result)) return result;
    return AppendWindowsSearchHostSids(
        sids,
        owners);
}

HRESULT BuildDcomDacl(
    const ULONG rights,
    std::vector<uint8_t>& output) {
    std::vector<PSID> sids;
    std::vector<SidOwner> owners;
    // NT AUTHORITY\SYSTEM.
    HRESULT result =
        AppendSid(
            L"S-1-5-18",
            sids,
            owners);
    if (FAILED(result)) return result;
    // BUILTIN\Administrators.
    result = AppendSid(
        L"S-1-5-32-544",
        sids,
        owners);
    if (FAILED(result)) return result;
    // NT AUTHORITY\INTERACTIVE (tokens logged on at this machine).
    result = AppendSid(
        L"S-1-5-4",
        sids,
        owners);
    if (FAILED(result)) return result;
    result = AppendAppContainerAccessSids(
        sids,
        owners);
    if (FAILED(result)) return result;

    std::vector<EXPLICIT_ACCESS_W> entries;
    entries.reserve(sids.size());
    for (const PSID sid : sids) {
        EXPLICIT_ACCESS_W entry{};
        entry.grfAccessPermissions =
            rights;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance =
            NO_INHERITANCE;
        entry.Trustee.TrusteeForm =
            TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType =
            TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName =
            static_cast<LPWSTR>(sid);
        entries.push_back(entry);
    }

    PACL raw_acl = nullptr;
    const DWORD acl_result =
        ::SetEntriesInAclW(
            gsl::narrow_cast<ULONG>(
                entries.size()),
            entries.data(),
            nullptr,
            &raw_acl);
    if (acl_result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(
            acl_result);
    }
    const ScopedLocalAlloc acl_guard(
        raw_acl);

    SECURITY_DESCRIPTOR descriptor{};
    if (!::InitializeSecurityDescriptor(
            &descriptor,
            SECURITY_DESCRIPTOR_REVISION)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if (!::SetSecurityDescriptorOwner(
            &descriptor,
            sids.front(),
            FALSE)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if (!::SetSecurityDescriptorGroup(
            &descriptor,
            sids.front(),
            FALSE)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if (!::SetSecurityDescriptorDacl(
            &descriptor,
            TRUE,
            raw_acl,
            FALSE)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    if (!::SetSecurityDescriptorControl(
            &descriptor,
            SE_DACL_PROTECTED,
            SE_DACL_PROTECTED)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    DWORD length = 0;
    if (!::MakeSelfRelativeSD(
            &descriptor,
            nullptr,
            &length)) {
        const DWORD error =
            ::GetLastError();
        if (error !=
            ERROR_INSUFFICIENT_BUFFER) {
            return HRESULT_FROM_WIN32(error);
        }
    }
    std::vector<uint8_t> bytes(length);
    if (!::MakeSelfRelativeSD(
            &descriptor,
            bytes.data(),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    output = std::move(bytes);
    return S_OK;
}

HRESULT GrantReadExecuteAcl(
    const std::filesystem::path& path,
    DWORD rights,
    DWORD inheritance,
    std::span<PSID> sids) {
    std::wstring native_path =
        path.wstring();
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR raw_descriptor =
        nullptr;
    const DWORD security_result =
        ::GetNamedSecurityInfoW(
            native_path.data(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &old_dacl,
            nullptr,
            &raw_descriptor);
    if (security_result !=
        ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(
            security_result);
    }
    const ScopedLocalAlloc descriptor_owner(
        raw_descriptor);

    std::vector<EXPLICIT_ACCESS_W> entries;
    entries.reserve(sids.size());
    for (const PSID sid : sids) {
        EXPLICIT_ACCESS_W entry{};
        entry.grfAccessPermissions =
            rights;
        entry.grfAccessMode =
            GRANT_ACCESS;
        entry.grfInheritance =
            inheritance;
        entry.Trustee.TrusteeForm =
            TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType =
            TRUSTEE_IS_GROUP;
        entry.Trustee.ptstrName =
            static_cast<LPWSTR>(sid);
        entries.push_back(entry);
    }

    PACL raw_acl = nullptr;
    const DWORD acl_result =
        ::SetEntriesInAclW(
            gsl::narrow_cast<ULONG>(
                entries.size()),
            entries.data(),
            old_dacl,
            &raw_acl);
    if (acl_result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(
            acl_result);
    }
    const ScopedLocalAlloc acl_owner(
        raw_acl);
    const DWORD set_result =
        ::SetNamedSecurityInfoW(
            native_path.data(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            raw_acl,
            nullptr);
    return HRESULT_FROM_WIN32(
        set_result);
}

HRESULT GrantBrokerAppContainerAccess(
    const std::filesystem::path& executable,
    const std::filesystem::path& proxy,
    const std::filesystem::path* proxy32) {
    if (!executable.is_absolute() ||
        !proxy.is_absolute() ||
        !std::filesystem::exists(
            executable) ||
        !std::filesystem::exists(
            proxy)) {
        return HRESULT_FROM_WIN32(
            ERROR_FILE_NOT_FOUND);
    }

    std::vector<PSID> sids;
    std::vector<SidOwner> owners;
    HRESULT result =
        AppendAppContainerAccessSids(
            sids,
            owners);
    if (FAILED(result)) return result;

    std::vector<std::filesystem::path>
        files{executable, proxy};
    if (proxy32) {
        if (!proxy32->is_absolute() ||
            !std::filesystem::exists(
                *proxy32)) {
            return HRESULT_FROM_WIN32(
                ERROR_FILE_NOT_FOUND);
        }
        files.push_back(*proxy32);
    }

    std::vector<std::filesystem::path>
        directories;
    for (const auto& file : files) {
        const auto directory =
            file.parent_path();
        if (std::find(
                directories.begin(),
                directories.end(),
                directory) ==
            directories.end()) {
            directories.push_back(
                directory);
        }
    }

    for (const auto& directory :
         directories) {
        result = GrantReadExecuteAcl(
            directory,
            FILE_GENERIC_READ |
                FILE_GENERIC_EXECUTE,
            SUB_CONTAINERS_AND_OBJECTS_INHERIT,
            sids);
        if (FAILED(result)) return result;
    }
    for (const auto& file : files) {
        result = GrantReadExecuteAcl(
            file,
            FILE_GENERIC_READ |
                FILE_GENERIC_EXECUTE,
            NO_INHERITANCE,
            sids);
        if (FAILED(result)) return result;
    }
    return S_OK;
}

HRESULT CreateKey(
    const RegistryRoot& root,
    std::wstring_view path,
    REGSAM access,
    ScopedRegKey& key) {
    const LSTATUS status =
        ::RegCreateKeyExW(
            root.key,
            (root.prefix + L"\\" +
             std::wstring(path)).c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            access | root.view,
            nullptr,
            key.Put(),
            nullptr);
    return HRESULT_FROM_WIN32(status);
}

HRESULT SetSecurityValue(
    HKEY key,
    const wchar_t* name,
    std::span<const uint8_t> bytes) noexcept {
    const LSTATUS status =
        ::RegSetValueExW(
            key,
            name,
            0,
            REG_BINARY,
            bytes.data(),
            gsl::narrow_cast<DWORD>(
                bytes.size()));
    return HRESULT_FROM_WIN32(status);
}

HRESULT RegisterProxy(
    const std::filesystem::path& proxy,
    const RegistryRoot& root) {
    if (!std::filesystem::exists(proxy)) {
        return HRESULT_FROM_WIN32(
            ERROR_FILE_NOT_FOUND);
    }
    const std::wstring proxy_clsid =
        GuidString(kIIDQiQiConfigBroker);
    const std::wstring proxy_key =
        L"CLSID\\" + proxy_clsid;
    const std::wstring inproc_key =
        proxy_key + L"\\InprocServer32";
    ScopedRegKey inproc;
    HRESULT result = CreateKey(
        root,
        inproc_key,
        KEY_WRITE,
        inproc);
    if (FAILED(result)) return result;
    result = SetString(
        inproc.Get(),
        nullptr,
        proxy.wstring());
    if (FAILED(result)) return result;
    result = SetString(
        inproc.Get(),
        L"ThreadingModel",
        L"Both");
    if (FAILED(result)) return result;

    const auto register_interface =
        [&](const GUID& iid) -> HRESULT {
            const std::wstring interface_key =
                L"Interface\\" +
                GuidString(iid);
            ScopedRegKey interface_reg;
            HRESULT interface_result =
                CreateKey(
                    root,
                    interface_key,
                    KEY_WRITE,
                    interface_reg);
            if (FAILED(interface_result)) {
                return interface_result;
            }
            ScopedRegKey proxy_stub;
            interface_result = CreateKey(
                root,
                interface_key +
                L"\\ProxyStubClsid32",
                KEY_WRITE,
                proxy_stub);
            if (FAILED(interface_result)) {
                return interface_result;
            }
            return SetString(
                proxy_stub.Get(),
                nullptr,
                proxy_clsid);
        };
    result = register_interface(
        kIIDQiQiConfigBroker);
    if (FAILED(result)) {
        return result;
    }
    result = register_interface(
        kIIDAsyncQiQiConfigBroker);
    if (FAILED(result)) {
        return result;
    }

    ScopedRegKey sync_async;
    result = CreateKey(
        root,
        L"Interface\\" +
            proxy_clsid +
            L"\\AsynchronousInterface",
        KEY_WRITE,
        sync_async);
    if (FAILED(result)) return result;
    result = SetString(
        sync_async.Get(),
        nullptr,
        GuidString(
            kIIDAsyncQiQiConfigBroker));
    if (FAILED(result)) return result;

    ScopedRegKey async_sync;
    result = CreateKey(
        root,
        L"Interface\\" +
            GuidString(
                kIIDAsyncQiQiConfigBroker) +
            L"\\SynchronousInterface",
        KEY_WRITE,
        async_sync);
    if (FAILED(result)) return result;
    result = SetString(
        async_sync.Get(),
        nullptr,
        proxy_clsid);
    if (FAILED(result)) return result;
    return S_OK;
}

HRESULT DeleteTree(
    const RegistryRoot& root,
    std::wstring_view path) {
    ScopedRegKey base;
    const LSTATUS open =
        ::RegOpenKeyExW(
            root.key,
            root.prefix.c_str(),
            0,
            DELETE | root.view,
            base.Put());
    if (open ==
        ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    if (open != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(open);
    }
    const LSTATUS status =
        ::RegDeleteTreeW(
            base.Get(),
            std::wstring(path).c_str());
    if (status == ERROR_SUCCESS ||
        status == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(status);
}

} // namespace

HRESULT RegisterView(
    const std::filesystem::path& executable,
    const std::filesystem::path& proxy,
    const RegistryRoot& root,
    std::wstring_view store_subkey) {
    const std::wstring clsid =
        GuidString(kClsidQiQiConfigBroker);
    const std::wstring appid =
        GuidString(kAppIdQiQiConfigBroker);
    const std::wstring clsid_key =
        L"CLSID\\" + clsid;
    const std::wstring local_server_key =
        clsid_key + L"\\LocalServer32";
    const std::wstring appid_key =
        L"AppID\\" + appid;

    ScopedRegKey clsid_reg;
    HRESULT result = CreateKey(
        root,
        clsid_key,
        KEY_WRITE,
        clsid_reg);
    if (FAILED(result)) return result;
    result = SetString(
        clsid_reg.Get(),
        nullptr,
        L"QiQiIME Configuration Broker");
    if (FAILED(result)) return result;
    result = SetString(
        clsid_reg.Get(),
        L"AppID",
        appid);
    if (FAILED(result)) return result;

    ScopedRegKey local_server;
    result = CreateKey(
        root,
        local_server_key,
        KEY_WRITE,
        local_server);
    if (FAILED(result)) return result;
    const std::wstring command =
        [&]() {
            std::wstring result =
                L"\"" +
                executable.wstring() +
                L"\" -Embedding";
            if (!store_subkey.empty()) {
                result +=
                    L" --store-subkey=\"" +
                    std::wstring(
                        store_subkey) +
                    L"\"";
            }
            return result;
        }();
    result = SetString(
        local_server.Get(),
        nullptr,
        command);
    if (FAILED(result)) return result;

    ScopedRegKey appid_reg;
    result = CreateKey(
        root,
        appid_key,
        KEY_WRITE,
        appid_reg);
    if (FAILED(result)) return result;
    result = SetString(
        appid_reg.Get(),
        nullptr,
        L"QiQiIME Configuration Broker");
    if (FAILED(result)) return result;
    result = SetString(
        appid_reg.Get(),
        L"RunAs",
        L"Interactive User");
    if (FAILED(result)) return result;
    result = SetDword(
        appid_reg.Get(),
        L"AuthenticationLevel",
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY);
    if (FAILED(result)) return result;

    std::vector<uint8_t> access_dacl;
    result = BuildDcomDacl(
        kLocalDcomAccessRights,
        access_dacl);
    if (FAILED(result)) return result;
    result = SetSecurityValue(
        appid_reg.Get(),
        L"AccessPermission",
        access_dacl);
    if (FAILED(result)) return result;
    std::vector<uint8_t> launch_dacl;
    result = BuildDcomDacl(
        kLocalDcomLaunchRights,
        launch_dacl);
    if (FAILED(result)) return result;
    result = SetSecurityValue(
        appid_reg.Get(),
        L"LaunchPermission",
        launch_dacl);
    if (FAILED(result)) return result;
    return RegisterProxy(
        proxy,
        root);
}

HRESULT RegisterServer(
    HINSTANCE instance,
    std::wstring_view store_subkey) noexcept {
    try {
        if (store_subkey.find(L'"') !=
                std::wstring_view::npos ||
            store_subkey.find(L'\r') !=
                std::wstring_view::npos ||
            store_subkey.find(L'\n') !=
                std::wstring_view::npos) {
            return E_INVALIDARG;
        }
        const std::filesystem::path executable =
            GetModuleFilePath(instance);
        if (executable.empty()) {
            return E_UNEXPECTED;
        }
        const auto [base_key, prefix] =
            RegistryBase();
        const std::filesystem::path proxy =
            executable.parent_path() /
            L"QiQiIMEConfigProxy.dll";
#ifdef _WIN64
        const std::filesystem::path proxy32 =
            Proxy32Path(executable);
        const bool proxy32_exists =
            std::filesystem::exists(
                proxy32);
        HRESULT result =
            GrantBrokerAppContainerAccess(
                executable,
                proxy,
                proxy32_exists
                    ? &proxy32
                    : nullptr);
        if (FAILED(result)) return result;
        RegistryRoot root{
            base_key,
            prefix,
            KEY_WOW64_64KEY,
        };
        result = RegisterView(
            executable,
            proxy,
            root,
            store_subkey);
        if (FAILED(result)) return result;
        if (proxy32_exists) {
            result = RegisterView(
                executable,
                proxy32,
                RegistryRoot{
                    base_key,
                    prefix,
                    KEY_WOW64_32KEY,
                },
                store_subkey);
            if (FAILED(result)) {
                return result;
            }
        } else {
            QIQI_LOG_CAT_WARN(
                log::Keyword::Config,
                "32-bit Broker proxy not "
                "present during registration; "
                "WOW64 registration skipped");
        }
#else
        BOOL wow64 = FALSE;
        std::ignore =
            ::IsWow64Process(
                ::GetCurrentProcess(),
                &wow64);
        RegistryRoot root{
            base_key,
            prefix,
            wow64 ? KEY_WOW64_32KEY
                  : REGSAM{0},
        };
        HRESULT result =
            GrantBrokerAppContainerAccess(
                executable,
                proxy,
                nullptr);
        if (FAILED(result)) return result;
        result = RegisterView(
            executable,
            proxy,
            root,
            store_subkey);
        if (FAILED(result)) return result;
#endif

        QIQI_LOG_CAT_INFO(
            log::Keyword::Config,
            "COM server registered "
            "(provider_id={}, arch={})",
            "8E7B9D31-5A42-4C81-9F11-202610290011",
            version::kArch);
        return S_OK;
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

HRESULT UnregisterServer() noexcept {
    try {
        const auto [base_key, prefix] =
            RegistryBase();
        const std::wstring clsid =
            L"CLSID\\" +
            GuidString(kClsidQiQiConfigBroker);
        const std::wstring appid =
            L"AppID\\" +
            GuidString(kAppIdQiQiConfigBroker);
        const std::array paths{
            clsid,
            appid,
            L"CLSID\\" +
                GuidString(
                    kIIDQiQiConfigBroker),
            L"Interface\\" +
                GuidString(
                    kIIDQiQiConfigBroker),
            L"Interface\\" +
                GuidString(
                    kIIDAsyncQiQiConfigBroker),
        };
        std::vector<RegistryRoot> roots;
#ifdef _WIN64
        roots.push_back(
            {
                base_key,
                prefix,
                KEY_WOW64_64KEY,
            });
        roots.push_back(
            {
                base_key,
                prefix,
                KEY_WOW64_32KEY,
            });
#else
        BOOL wow64 = FALSE;
        std::ignore =
            ::IsWow64Process(
                ::GetCurrentProcess(),
                &wow64);
        roots.push_back(
            {
                base_key,
                prefix,
                wow64 ? KEY_WOW64_32KEY
                      : REGSAM{0},
            });
#endif
        for (const auto& root : roots) {
            for (const auto& path :
                 paths) {
                const HRESULT result =
                    DeleteTree(root, path);
                if (FAILED(result)) {
                    return result;
                }
            }
        }
        return S_OK;
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

} // namespace qiqi::broker
