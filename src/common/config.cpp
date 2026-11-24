// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/config.h"
#include "common/configclient.h"
#include "common/comobj.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "common/RAII.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace qiqi {

namespace {

static_assert(
    std::is_nothrow_move_constructible_v<
        QiQiConfig>);
static_assert(
    std::is_nothrow_move_assignable_v<
        QiQiConfig>);

constexpr DWORD kMaxRegistryStringBytes = 4096;
constexpr const wchar_t* kCommitTraceValue =
    L"ConfigCommitTraceId";
constexpr const wchar_t* kCommitActivityValue =
    L"ConfigCommitActivityId";
constexpr const wchar_t* kCommitRevisionValue =
    L"ConfigCommitRevision";

HRESULT WriteRegDwordResult(HKEY key, const wchar_t* name, DWORD val) noexcept {
    const LSTATUS error = ::RegSetValueExW(
        key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    if (error != ERROR_SUCCESS) {
        QIQI_LOG_ERROR("WriteRegDword::RegSetValueExW failed (error={})", error);
    }
    return HRESULT_FROM_WIN32(error);
}

bool CheckRegistryRead(LSTATUS error, std::string_view operation) noexcept {
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
        QIQI_LOG_WARN("{} failed (error={})", operation, error);
    }
    return error == ERROR_SUCCESS;
}

HRESULT ReadRegDwordResult(HKEY key, const wchar_t* name, DWORD& out_val) noexcept {
    DWORD type = 0;
    DWORD value = 0;
    DWORD cb = sizeof(value);
    const LSTATUS error = ::RegQueryValueExW(
        key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &cb);
    if (!CheckRegistryRead(error, "ReadRegDword::RegQueryValueExW"))
        return HRESULT_FROM_WIN32(error);
    if (type != REG_DWORD || cb != sizeof(value)) {
        QIQI_LOG_WARN("ReadRegDword rejected an invalid type or size (hr=0x{:08X})",
            static_cast<unsigned long>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)));
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    out_val = value;
    return S_OK;
}

HRESULT WriteRegGuidResult(
    HKEY key,
    const wchar_t* name,
    const GUID& value) noexcept {
    const auto bytes =
        std::bit_cast<
            std::array<BYTE, sizeof(GUID)>>(
                value);
    const LSTATUS error =
        ::RegSetValueExW(
            key,
            name,
            0,
            REG_BINARY,
            bytes.data(),
            gsl::narrow_cast<DWORD>(
                bytes.size()));
    if (error != ERROR_SUCCESS) {
        QIQI_LOG_ERROR(
            "WriteRegGuid::RegSetValueExW failed "
            "(error={})",
            error);
    }
    return HRESULT_FROM_WIN32(error);
}

HRESULT ReadRegGuidResult(
    HKEY key,
    const wchar_t* name,
    GUID& value) noexcept {
    std::array<BYTE, sizeof(GUID)> bytes{};
    DWORD type = 0;
    DWORD size =
        gsl::narrow_cast<DWORD>(bytes.size());
    const LSTATUS error =
        ::RegQueryValueExW(
            key,
            name,
            nullptr,
            &type,
            bytes.data(),
            &size);
    if (!CheckRegistryRead(
            error,
            "ReadRegGuid::RegQueryValueExW")) {
        return HRESULT_FROM_WIN32(error);
    }
    if (type != REG_BINARY ||
        size != bytes.size()) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    value = std::bit_cast<GUID>(bytes);
    return S_OK;
}

HRESULT WriteRegQwordResult(
    HKEY key,
    const wchar_t* name,
    uint64_t value) noexcept {
    const auto bytes =
        std::bit_cast<
            std::array<BYTE, sizeof(value)>>(
                value);
    const LSTATUS error =
        ::RegSetValueExW(
            key,
            name,
            0,
            REG_QWORD,
            bytes.data(),
            gsl::narrow_cast<DWORD>(
                bytes.size()));
    if (error != ERROR_SUCCESS) {
        QIQI_LOG_ERROR(
            "WriteRegQword::RegSetValueExW failed "
            "(error={})",
            error);
    }
    return HRESULT_FROM_WIN32(error);
}

HRESULT ReadRegQwordResult(
    HKEY key,
    const wchar_t* name,
    uint64_t& value) noexcept {
    std::array<BYTE, sizeof(value)> bytes{};
    DWORD type = 0;
    DWORD size =
        gsl::narrow_cast<DWORD>(bytes.size());
    const LSTATUS error =
        ::RegQueryValueExW(
            key,
            name,
            nullptr,
            &type,
            bytes.data(),
            &size);
    if (!CheckRegistryRead(
            error,
            "ReadRegQword::RegQueryValueExW")) {
        return HRESULT_FROM_WIN32(error);
    }
    if (type != REG_QWORD ||
        size != bytes.size()) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    value = std::bit_cast<uint64_t>(bytes);
    return S_OK;
}

bool ReadRegDword(HKEY key, const wchar_t* name, DWORD& out_val) noexcept {
    return SUCCEEDED(ReadRegDwordResult(key, name, out_val));
}

HRESULT WriteRegStringResult(HKEY key, const wchar_t* name, std::wstring_view val) noexcept {
    const DWORD cb = gsl::narrow_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
    const LSTATUS error = ::RegSetValueExW(
        key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.data()), cb);
    if (error != ERROR_SUCCESS) {
        QIQI_LOG_ERROR("WriteRegString::RegSetValueExW failed (error={})", error);
    }
    return HRESULT_FROM_WIN32(error);
}

HRESULT ReadRegStringResult(HKEY key, const wchar_t* name, std::wstring& out_val) {
    DWORD type = 0;
    DWORD cb = 0;
    const LSTATUS size_error = ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb);
    if (!CheckRegistryRead(size_error, "ReadRegString::RegQueryValueExW(size)"))
        return HRESULT_FROM_WIN32(size_error);
    if (type != REG_SZ || cb < sizeof(wchar_t) || cb > kMaxRegistryStringBytes || cb % sizeof(wchar_t) != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    std::wstring buf(cb / sizeof(wchar_t), L'\0');
    const LSTATUS data_error = ::RegQueryValueExW(
        key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &cb);
    if (!CheckRegistryRead(data_error, "ReadRegString::RegQueryValueExW(data)"))
        return HRESULT_FROM_WIN32(data_error);
    if (type != REG_SZ || cb % sizeof(wchar_t) != 0) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    buf.resize(cb / sizeof(wchar_t));
    while (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    out_val = std::move(buf);
    return S_OK;
}

bool ReadRegString(HKEY key, const wchar_t* name, std::wstring& out_val) {
    return SUCCEEDED(ReadRegStringResult(key, name, out_val));
}

std::filesystem::path ResolveDefaultUserFilePath(std::wstring_view file_name) {
    const std::filesystem::path app_dir = GetAppDataDirectory();
    if (app_dir.empty()) {
        return std::filesystem::path(file_name);
    }
    return app_dir / file_name;
}

void GrantAppContainerReadAccessToKey(HKEY key) noexcept {
    // Grant S-1-15-2-1 (ALL APPLICATION PACKAGES) and S-1-15-2-2 (ALL RESTRICTED APPLICATION PACKAGES)
    // KEY_READ access while preserving the current user's existing DACL entries.
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd_raw = nullptr;
    const DWORD security_error = ::GetSecurityInfo(
            key,
            SE_REGISTRY_KEY,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &old_dacl,
            nullptr,
            &sd_raw);
    if (security_error != ERROR_SUCCESS) {
        QIQI_LOG_WARN("GrantAppContainerReadAccessToKey::GetSecurityInfo failed (error={})", security_error);
        return;
    }
    const ScopedLocalAlloc sd_guard(sd_raw);

    PSID sid_all_apps = nullptr;
    PSID sid_restricted_apps = nullptr;
    if (!::ConvertStringSidToSidW(L"S-1-15-2-1", &sid_all_apps)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("ConvertStringSidToSidW(all applications) failed (error={})", error);
    }
    if (!::ConvertStringSidToSidW(L"S-1-15-2-2", &sid_restricted_apps)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("ConvertStringSidToSidW(restricted applications) failed (error={})", error);
    }
    const ScopedLocalAlloc sid1_guard(sid_all_apps);
    const ScopedLocalAlloc sid2_guard(sid_restricted_apps);

    std::array<EXPLICIT_ACCESS_W, 2> ea{};
    ULONG ea_count = 0;
    for (PSID sid : {sid_all_apps, sid_restricted_apps}) {
        if (!sid) {
            continue;
        }
        auto& entry = gsl::at(ea, ea_count++);
        entry.grfAccessPermissions = KEY_READ;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    }
    if (ea_count == 0) {
        return;
    }

    PACL new_dacl = nullptr;
    const DWORD acl_error = ::SetEntriesInAclW(ea_count, ea.data(), old_dacl, &new_dacl);
    if (acl_error == ERROR_SUCCESS && new_dacl) {
        const ScopedLocalAlloc dacl_guard(new_dacl);
        const DWORD error = ::SetSecurityInfo(
            key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
        if (error != ERROR_SUCCESS) {
            QIQI_LOG_WARN("GrantAppContainerReadAccessToKey::SetSecurityInfo failed (error={})", error);
        }
    } else {
        QIQI_LOG_WARN("GrantAppContainerReadAccessToKey::SetEntriesInAclW failed (error={})", acl_error);
    }
}

ScopedRegKey OpenOrCreateConfigRegKey(
    const wchar_t* subkey, REGSAM access, HANDLE transaction = nullptr, LSTATUS* out_error = nullptr) noexcept {
    if (out_error) *out_error = ERROR_INVALID_PARAMETER;
    if (!subkey) {
        subkey = GetActiveConfigRegistrySubKey();
    }
    if (*subkey == L'\0') {
        return {};
    }
    ScopedRegKey key;
    if ((access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY)) != 0) {
        DWORD disp = 0;
        const auto create_key = [&](REGSAM sam) noexcept -> LSTATUS {
            if (transaction != nullptr && transaction != INVALID_HANDLE_VALUE) {
                return ::RegCreateKeyTransactedW(
                    HKEY_CURRENT_USER,
                    subkey,
                    0,
                    nullptr,
                    REG_OPTION_NON_VOLATILE,
                    sam,
                    nullptr,
                    key.Put(),
                    &disp,
                    transaction,
                    nullptr);
            }
            return ::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                subkey,
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                sam,
                nullptr,
                key.Put(),
                &disp);
        };
        const LSTATUS acl_error = create_key(access | WRITE_DAC | READ_CONTROL);
        if (acl_error == ERROR_SUCCESS) {
            if (out_error) *out_error = ERROR_SUCCESS;
            GrantAppContainerReadAccessToKey(key.Get());
            return key;
        }
        QIQI_LOG_DEBUG("Config::RegCreateKey with ACL access failed; retrying basic access (error={})", acl_error);
        const LSTATUS error = create_key(access);
        if (out_error) *out_error = error;
        if (error != ERROR_SUCCESS) {
            QIQI_LOG_ERROR("Config::RegCreateKey failed (error={})", error);
        }
    } else if (transaction != nullptr && transaction != INVALID_HANDLE_VALUE) {
        const LSTATUS error = ::RegOpenKeyTransactedW(
            HKEY_CURRENT_USER, subkey, 0, access, key.Put(), transaction, nullptr);
        if (out_error) *out_error = error;
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
            QIQI_LOG_WARN("Config::RegOpenKeyTransactedW failed (error={})", error);
        }
    } else {
        const LSTATUS error = ::RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, access, key.Put());
        if (out_error) *out_error = error;
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
            QIQI_LOG_WARN("Config::RegOpenKeyExW failed (error={})", error);
        }
    }
    return key;
}

HRESULT BumpRegistrySequenceResult(
    HKEY key,
    uint64_t& sequence) noexcept {
    uint64_t seq = 0;
    DWORD cb = sizeof(seq);
    DWORD type = 0;
    std::ignore = CheckRegistryRead(::RegQueryValueExW(
        key, L"ConfigSequence", nullptr, &type, reinterpret_cast<LPBYTE>(&seq), &cb),
        "BumpRegistrySequence::RegQueryValueExW");
    ++seq;
    const LSTATUS result = ::RegSetValueExW(
        key,
        L"ConfigSequence",
        0,
        REG_QWORD,
        reinterpret_cast<const BYTE*>(&seq),
        sizeof(seq));
    if (result != ERROR_SUCCESS) {
        QIQI_LOG_ERROR("Failed to update registry configuration sequence (error={})", result);
        return HRESULT_FROM_WIN32(result);
    }
    sequence = seq;
    return S_OK;
}

HRESULT BumpRegistrySequenceResult(
    HKEY key) noexcept {
    uint64_t ignored = 0;
    return BumpRegistrySequenceResult(
        key,
        ignored);
}

HRESULT WriteCommitTraceResult(
    HKEY key,
    uint64_t revision,
    const GUID* trace_id,
    const GUID* activity_id) noexcept {
    if (!trace_id && !activity_id) {
        return S_OK;
    }
    if (!trace_id ||
        !activity_id ||
        *trace_id == GUID_NULL ||
        *activity_id == GUID_NULL) {
        return E_INVALIDARG;
    }
    HRESULT result =
        WriteRegGuidResult(
            key,
            kCommitTraceValue,
            *trace_id);
    if (FAILED(result)) {
        return result;
    }
    result =
        WriteRegGuidResult(
            key,
            kCommitActivityValue,
            *activity_id);
    if (FAILED(result)) {
        return result;
    }
    return WriteRegQwordResult(
        key,
        kCommitRevisionValue,
        revision);
}

const wchar_t* g_config_reg_subkey_override = nullptr;
std::atomic<bool>
    g_sync_directory_publish_inflight{false};

} // namespace

void SetConfigRegistrySubKeyOverrideForTest(const wchar_t* subkey_or_null) noexcept {
    g_config_reg_subkey_override = subkey_or_null;
}

const wchar_t* GetActiveConfigRegistrySubKey() noexcept {
    return g_config_reg_subkey_override ? g_config_reg_subkey_override : kDefaultConfigRegSubKey;
}

bool HasConfigRegistrySubKeyOverride() noexcept {
    return g_config_reg_subkey_override != nullptr;
}

bool IsProcessAppContainer() noexcept {
    ScopedKernelHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put())) {
        return false;
    }
    DWORD is_app_container = 0;
    DWORD ret_len = 0;
    if (::GetTokenInformation(
            token.Get(),
            TokenIsAppContainer,
            &is_app_container,
            sizeof(is_app_container),
            &ret_len)) {
        return is_app_container != 0;
    }
    return false;
}

std::filesystem::path GetDefaultUserDictPath() {
    return ResolveDefaultUserFilePath(L"user_dict.txt");
}

std::filesystem::path GetDefaultPhraseFreqPath() {
    return ResolveDefaultUserFilePath(L"phrase_freq.qfreq");
}

std::filesystem::path GetSyncChannelDirectoryForFile(const std::filesystem::path& file_path) {
    if (file_path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path abs_path = std::filesystem::absolute(file_path, ec);
    if (ec || abs_path.empty()) {
        abs_path = file_path;
    }
    return std::filesystem::path(abs_path.wstring() + L".sync");
}

std::filesystem::path ResolveBundledDataFile(
    const std::filesystem::path& dll_dir, std::wstring_view file_name) {
    if (dll_dir.empty() || file_name.empty()) {
        return std::filesystem::path(file_name);
    }
    std::error_code ec;
    const std::filesystem::path direct = dll_dir / file_name;
    if (std::filesystem::exists(direct, ec)) {
        return direct;
    }
    if (dll_dir.has_parent_path()) {
        const std::filesystem::path parent_dir = dll_dir.parent_path();
        const std::filesystem::path shared_data = parent_dir / L"data" / file_name;
        if (std::filesystem::exists(shared_data, ec)) {
            return shared_data;
        }
        const std::filesystem::path shared_parent = parent_dir / file_name;
        if (std::filesystem::exists(shared_parent, ec)) {
            return shared_parent;
        }
    }
    return direct;
}

std::filesystem::path ResolveConfiguredDictionaryPath(
    const std::filesystem::path& module_dir,
    std::wstring_view configured_path) {
    if (configured_path.empty()) {
        return {};
    }
    std::filesystem::path resolved(configured_path);
    if (resolved.is_relative() && !module_dir.empty()) {
        resolved = ResolveBundledDataFile(module_dir, configured_path);
    }
    return resolved;
}

HRESULT SaveConfigToRegistryResult(
    const QiQiConfig& config,
    const wchar_t* subkey,
    const GUID* commit_trace_id,
    const GUID* commit_activity_id) noexcept {
    try {
        ScopedKernelTransaction txn;
        if (!txn) {
            QIQI_LOG_ERROR("Failed to create registry transaction (error={})", txn.Error());
            return HRESULT_FROM_WIN32(txn.Error());
        }
        LSTATUS open_error = ERROR_SUCCESS;
        ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ | KEY_WRITE, txn.Get(), &open_error);
        if (!key) {
            QIQI_LOG_ERROR("SaveConfigToRegistry::OpenOrCreateConfigRegKey failed (error={})", open_error);
            return HRESULT_FROM_WIN32(open_error);
        }
        HRESULT result = S_OK;
        ForEachConfigField([&](const auto& field) {
            if (SUCCEEDED(result)) {
                using T = std::remove_cvref_t<decltype(config.*(field.member))>;
                if constexpr (std::is_same_v<T, ImeSkinId>) {
                    const auto id = ImeSkinIdToString(config.*(field.member));
                    if (id.empty()) {
                        QIQI_LOG_ERROR("Saving an invalid skin selection was rejected (hr=0x{:08X})",
                            static_cast<unsigned long>(E_INVALIDARG));
                        result = E_INVALIDARG;
                    } else {
                        result = WriteRegStringResult(key.Get(), field.registry_key, Utf8ToWide(id));
                    }
                } else {
                    result = WriteRegDwordResult(
                        key.Get(), field.registry_key, gsl::narrow_cast<DWORD>(field.RegistryValue(config)));
                }
            }
        });
        ForEachConfigStringField([&](const auto& field) {
            if (SUCCEEDED(result)) {
                const std::wstring& value = config.*(field.member);
                if (field.backup_key == "candidate_font_family" &&
                    !IsValidCandidateFontFamily(value)) {
                    QIQI_LOG_ERROR("Saving an invalid candidate font family was rejected (hr=0x{:08X})",
                        static_cast<unsigned long>(E_INVALIDARG));
                    result = E_INVALIDARG;
                } else {
                    result = WriteRegStringResult(key.Get(), field.registry_key, value);
                }
            }
        });
        if (FAILED(result)) return result;
        result = WriteRegStringResult(key.Get(), L"PrimaryDictPath", config.primary_dict_path);
        if (FAILED(result)) return result;
        result = WriteRegStringResult(key.Get(), L"UserDictPath", config.user_dict_path);
        if (FAILED(result)) return result;
        uint64_t revision = 0;
        result = BumpRegistrySequenceResult(
            key.Get(),
            revision);
        if (FAILED(result)) return result;
        result = WriteCommitTraceResult(
            key.Get(),
            revision,
            commit_trace_id,
            commit_activity_id);
        if (FAILED(result)) return result;
        key.Reset();
        if (!txn.Commit()) {
            QIQI_LOG_ERROR("SaveConfigToRegistry::CommitTransaction failed (error={})", txn.Error());
            return HRESULT_FROM_WIN32(txn.Error());
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("SaveConfigToRegistry allocation failed");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("SaveConfigToRegistry failed with a C++ exception");
        return E_FAIL;
    }
}

bool SaveConfigToRegistry(const QiQiConfig& config, const wchar_t* subkey) noexcept {
    return SUCCEEDED(SaveConfigToRegistryResult(config, subkey));
}

HRESULT LoadConfigCommitTraceFromRegistry(
    GUID& trace_id,
    GUID& activity_id,
    uint64_t& revision,
    const wchar_t* subkey) noexcept {
    trace_id = GUID_NULL;
    activity_id = GUID_NULL;
    revision = 0;
    LSTATUS open_error = ERROR_SUCCESS;
    const ScopedRegKey key =
        OpenOrCreateConfigRegKey(
            subkey,
            KEY_READ,
            nullptr,
            &open_error);
    if (!key) {
        return HRESULT_FROM_WIN32(
            open_error);
    }
    GUID loaded_trace = GUID_NULL;
    GUID loaded_activity = GUID_NULL;
    uint64_t loaded_revision = 0;
    HRESULT result =
        ReadRegGuidResult(
            key.Get(),
            kCommitTraceValue,
            loaded_trace);
    if (FAILED(result)) {
        return result;
    }
    result =
        ReadRegGuidResult(
            key.Get(),
            kCommitActivityValue,
            loaded_activity);
    if (FAILED(result)) {
        return result;
    }
    result =
        ReadRegQwordResult(
            key.Get(),
            kCommitRevisionValue,
            loaded_revision);
    if (FAILED(result)) {
        return result;
    }
    if (loaded_trace == GUID_NULL ||
        loaded_activity == GUID_NULL) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    trace_id = loaded_trace;
    activity_id = loaded_activity;
    revision = loaded_revision;
    return S_OK;
}

bool LoadConfigFromRegistry(
    QiQiConfig& out_config,
    uint64_t* out_sequence,
    const wchar_t* subkey) noexcept {
    try {
        const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ);
        if (!key) {
            return false;
        }
        uint64_t seq = 0;
        DWORD cb_seq = sizeof(seq);
        DWORD type_seq = 0;
        if (!CheckRegistryRead(::RegQueryValueExW(
                key.Get(),
                L"ConfigSequence",
                nullptr,
                &type_seq,
                reinterpret_cast<LPBYTE>(&seq),
                &cb_seq), "LoadConfigFromRegistry::RegQueryValueExW(ConfigSequence)") ||
            type_seq != REG_QWORD || seq == 0) {
            return false;
        }
        if (out_sequence) {
            *out_sequence = seq;
        }

        QiQiConfig cfg{};
        DWORD dw = 0;
        ForEachConfigField([&](const auto& field) {
            using T = std::remove_cvref_t<decltype(cfg.*(field.member))>;
            if constexpr (std::is_same_v<T, ImeSkinId>) {
                std::wstring id;
                const HRESULT read = ReadRegStringResult(key.Get(), field.registry_key, id);
                if (SUCCEEDED(read)) {
                    cfg.*(field.member) = ParseImeSkinId(std::wstring_view(id)).value_or(ImeSkinId::Invalid);
                    if (cfg.*(field.member) == ImeSkinId::Invalid) {
                        QIQI_LOG_WARN("Configured skin ID is invalid (hr=0x{:08X})",
                            static_cast<unsigned long>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)));
                    }
                } else if (read != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
                    cfg.*(field.member) = ImeSkinId::Invalid;
                    QIQI_LOG_WARN("Reading the skin selection failed (hr=0x{:08X})",
                        static_cast<unsigned long>(read));
                }
            } else {
                if (ReadRegDword(key.Get(), field.registry_key, dw)) {
                    std::ignore = field.Assign(cfg, dw);
                }
            }
        });
        ForEachConfigStringField([&](const auto& field) {
            std::wstring value;
            const HRESULT read = ReadRegStringResult(key.Get(), field.registry_key, value);
            if (SUCCEEDED(read)) {
                if (field.backup_key != "candidate_font_family" ||
                    IsValidCandidateFontFamily(value)) {
                    cfg.*(field.member) = std::move(value);
                } else {
                    QIQI_LOG_WARN("Configured candidate font family is invalid (hr=0x{:08X})",
                        static_cast<unsigned long>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)));
                }
            } else if (read != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
                QIQI_LOG_WARN("Reading configuration string failed (field={}, hr=0x{:08X})",
                    field.backup_key,
                    static_cast<unsigned long>(read));
            }
        });
        std::ignore = ReadRegString(key.Get(), L"PrimaryDictPath", cfg.primary_dict_path);
        std::ignore = ReadRegString(key.Get(), L"UserDictPath", cfg.user_dict_path);
        out_config = std::move(cfg);
        return true;
    } catch (...) {
        QIQI_LOG_ERROR("LoadConfigFromRegistry failed with a C++ exception");
        return false;
    }
}

HRESULT OpenConfigRegistryKeyResult(
    HKEY* key, REGSAM access, const wchar_t* subkey, HANDLE transaction) noexcept {
    if (!key) return E_POINTER;
    *key = nullptr;
    LSTATUS error = ERROR_SUCCESS;
    auto opened = OpenOrCreateConfigRegKey(subkey, access, transaction, &error);
    if (!opened) return HRESULT_FROM_WIN32(error);
    *key = opened.Detach();
    return S_OK;
}

HRESULT WriteRegistryDwordResult(HKEY key, const wchar_t* name, DWORD value) noexcept {
    return WriteRegDwordResult(key, name, value);
}

HRESULT ReadRegistryDwordResult(HKEY key, const wchar_t* name, DWORD& value) noexcept {
    return ReadRegDwordResult(key, name, value);
}

HRESULT WriteRegistryStringResult(HKEY key, const wchar_t* name, std::wstring_view value) noexcept {
    return WriteRegStringResult(key, name, value);
}

HRESULT ReadRegistryStringResult(HKEY key, const wchar_t* name, std::wstring& value) {
    return ReadRegStringResult(key, name, value);
}

HRESULT BumpConfigRegistrySequenceResult(HKEY key) noexcept {
    return BumpRegistrySequenceResult(key);
}

void MergeSettingsConfig(
    QiQiConfig& external, const QiQiConfig& baseline, const QiQiConfig& local) noexcept {
    ForEachConfigField([&](const auto& field) noexcept {
        if (field.HasSettingsChange(baseline, local)) {
            external.*(field.member) = local.*(field.member);
        }
    });
    ForEachConfigStringField([&](const auto& field) noexcept {
        if (field.HasSettingsChange(baseline, local)) {
            external.*(field.member) = local.*(field.member);
        }
    });
    if (local.primary_dict_path != baseline.primary_dict_path) {
        external.primary_dict_path = local.primary_dict_path;
    }
}

uint64_t QueryRegistryConfigSequence(const wchar_t* subkey) noexcept {
    const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ);
    if (!key) {
        return 0;
    }
    uint64_t seq = 0;
    DWORD cb = sizeof(seq);
    DWORD type = 0;
    if (CheckRegistryRead(::RegQueryValueExW(
            key.Get(),
            L"ConfigSequence",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&seq),
            &cb), "QueryRegistryConfigSequence::RegQueryValueExW") &&
        type == REG_QWORD) {
        return seq;
    }
    return 0;
}

HRESULT SaveUserDictSyncDirectoryToRegistry(
    const std::filesystem::path& directory,
    const wchar_t* subkey,
    const GUID* commit_trace_id,
    const GUID* commit_activity_id,
    bool* changed) {
    if (changed) {
        *changed = false;
    }
    const auto save = [&]() -> HRESULT {
        if (IsProcessAppContainer()) return E_ACCESSDENIED;
        if (!directory.empty() && !directory.is_absolute()) return E_INVALIDARG;
        const std::wstring path = directory.wstring();
        if ((path.size() + 1) * sizeof(wchar_t) > kMaxRegistryStringBytes) return E_INVALIDARG;
        ScopedKernelTransaction txn;
        if (!txn) return HRESULT_FROM_WIN32(txn.Error());
        LSTATUS open_error = ERROR_SUCCESS;
        ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ | KEY_WRITE, txn.Get(), &open_error);
        if (!key) return HRESULT_FROM_WIN32(open_error);
        std::wstring previous;
        const HRESULT read_hr = ReadRegStringResult(key.Get(), L"UserDictSyncDirectory", previous);
        if (SUCCEEDED(read_hr) && previous == path) return S_OK;
        if (FAILED(read_hr) && read_hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return read_hr;
        const HRESULT write_hr = WriteRegStringResult(key.Get(), L"UserDictSyncDirectory", path);
        if (FAILED(write_hr)) return write_hr;
        uint64_t revision = 0;
        const HRESULT sequence_hr =
            BumpRegistrySequenceResult(
                key.Get(),
                revision);
        if (FAILED(sequence_hr)) return sequence_hr;
        const HRESULT trace_hr =
            WriteCommitTraceResult(
                key.Get(),
                revision,
                commit_trace_id,
                commit_activity_id);
        if (FAILED(trace_hr)) return trace_hr;
        key.Reset();
        if (!txn.Commit()) {
            return HRESULT_FROM_WIN32(
                txn.Error());
        }
        if (changed) {
            *changed = true;
        }
        return S_OK;
    };
    const HRESULT hr = save();
    std::ignore = log::CheckHResult(hr, "SaveUserDictSyncDirectory");
    return hr;
}

HRESULT LoadUserDictSyncDirectoryFromRegistry(
    std::filesystem::path& directory,
    const wchar_t* subkey) {
    directory.clear();
    LSTATUS open_error = ERROR_SUCCESS;
    const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ, nullptr, &open_error);
    std::wstring path;
    HRESULT hr = key ? ReadRegStringResult(key.Get(), L"UserDictSyncDirectory", path) :
        HRESULT_FROM_WIN32(open_error);
    if (SUCCEEDED(hr) && !path.empty()) {
        directory = std::filesystem::path(path);
        if (!directory.is_absolute()) {
            directory.clear();
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    std::ignore = log::CheckHResult(hr, "LoadUserDictSyncDirectory");
    return hr;
}

HRESULT SaveUserDictSyncDirectory(
    const std::filesystem::path& directory,
    const wchar_t* subkey) {
    if (subkey ||
        g_config_reg_subkey_override) {
        const HRESULT result =
            SaveUserDictSyncDirectoryToRegistry(
            directory,
            subkey);
        return result;
    }
    config::Snapshot snapshot;
    const HRESULT result =
        config::CopyCachedSnapshot(snapshot);
    if (result != S_OK) {
        return result;
    }
    if (snapshot.revision == 0) {
        return E_UNEXPECTED;
    }
    if (snapshot.user_dict_sync_directory ==
        directory) {
        return S_OK;
    }
    bool expected = false;
    if (!g_sync_directory_publish_inflight
             .compare_exchange_strong(
                 expected,
                 true,
                 std::memory_order_acq_rel)) {
        return S_OK;
    }
    try {
        const HRESULT submit_result =
            config::PublishSyncDirectoryAsync(
                {
                    snapshot.revision,
                    directory,
                },
                [](HRESULT publish_result,
                   const config::Snapshot&) {
                    g_sync_directory_publish_inflight
                        .store(
                            false,
                            std::memory_order_release);
                    if (FAILED(
                            publish_result)) {
                        QIQI_LOG_ERROR(
                            "SaveUserDictSyncDirectory "
                            "broker publish failed "
                            "(hr=0x{:08X})",
                            gsl::narrow_cast<
                                unsigned long>(
                                    publish_result));
                    }
                });
        if (FAILED(submit_result)) {
            g_sync_directory_publish_inflight
                .store(
                    false,
                    std::memory_order_release);
        }
        return submit_result;
    } catch (const std::bad_alloc&) {
        g_sync_directory_publish_inflight
            .store(
                false,
                std::memory_order_release);
        return E_OUTOFMEMORY;
    }
}

HRESULT LoadUserDictSyncDirectory(
    std::filesystem::path& directory,
    const wchar_t* subkey) {
    if (subkey ||
        g_config_reg_subkey_override) {
        return LoadUserDictSyncDirectoryFromRegistry(
            directory,
            subkey);
    }
    config::Snapshot snapshot;
    const HRESULT result =
        config::CopyCachedSnapshot(snapshot);
    if (result != S_OK ||
        snapshot.revision == 0) {
        directory.clear();
        return result != S_OK
            ? result
            : E_UNEXPECTED;
    }
    directory =
        snapshot.user_dict_sync_directory;
    return S_OK;
}

bool SaveConfig(const QiQiConfig& config) noexcept {
    const HRESULT result =
        SaveConfigResult(config);
    return SUCCEEDED(result);
}

HRESULT SaveConfigResult(
    const QiQiConfig& config) noexcept {
    return SaveConfigResult(
        config,
        trace::CreateContext());
}

HRESULT SaveConfigResult(
    const QiQiConfig& config,
    const trace::Context& trace) noexcept {
    if (g_config_reg_subkey_override) {
        const HRESULT save_result =
            SaveConfigToRegistryResult(
            config,
            g_config_reg_subkey_override);
        if (SUCCEEDED(save_result)) {
            std::ignore =
                BroadcastStandaloneGlobalCompartmentIncrement(
                    kGuidCompartmentConfigVersion);
        }
        return save_result;
    }
    config::Snapshot snapshot;
    HRESULT result =
        config::CopyCachedSnapshot(snapshot);
    if (result == S_FALSE) {
        const trace::Context read_trace =
            trace::CreateChildContext(
                trace);
        result =
            config::ReadSnapshot(
                read_trace,
                snapshot);
    }
    if (FAILED(result) ||
        snapshot.revision == 0) {
        QIQI_LOG_ERROR(
            "SaveConfig: broker snapshot "
            "unavailable (hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                FAILED(result)
                    ? result
                    : E_UNEXPECTED));
        return FAILED(result)
            ? result
            : E_UNEXPECTED;
    }
    result = config::SaveSnapshot(
        {
            snapshot.revision,
            config,
        },
        trace,
        snapshot);
    if (FAILED(result)) {
        QIQI_LOG_ERROR(
            "SaveConfig: broker save failed "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                result));
    }
    return result;
}

QiQiConfig LoadConfig(void* /*module_handle*/) noexcept {
    QiQiConfig cfg{};
    if (g_config_reg_subkey_override) {
        if (LoadConfigFromRegistry(
                cfg,
                nullptr,
                g_config_reg_subkey_override)) {
            return cfg;
        }
        std::ignore =
            SaveConfigToRegistry(
                cfg,
                g_config_reg_subkey_override);
        return cfg;
    }

    config::Snapshot snapshot;
    HRESULT result =
        config::CopyCachedSnapshot(snapshot);
    if (result != S_OK) {
        result =
            config::ReadSnapshot(snapshot);
    }
    if (SUCCEEDED(result)) {
        QIQI_LOG_INFO(
            "Loaded user configuration "
            "from broker "
            "(dict={}, page_size={}, "
            "revision={})",
            PrimaryDictToString(
                snapshot.value.primary_dict),
            snapshot.value.page_size,
            snapshot.revision);
        return std::move(snapshot.value);
    }
    QIQI_LOG_WARN(
        "LoadConfig: using last safe "
        "in-memory defaults because broker "
        "read failed (hr=0x{:08X})",
        gsl::narrow_cast<unsigned long>(
            result));
    return cfg;
}

} // namespace qiqi
