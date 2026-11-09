#include "common/config.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <string>

namespace qiqi {

namespace {

constexpr DWORD kMaxRegistryStringBytes = 4096;

bool WriteRegDword(HKEY key, const wchar_t* name, DWORD val) noexcept {
    const LSTATUS error = ::RegSetValueExW(
        key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    if (error != ERROR_SUCCESS) {
        QIQI_LOG_ERROR("WriteRegDword::RegSetValueExW failed (error={})", error);
    }
    return error == ERROR_SUCCESS;
}

bool CheckRegistryRead(LSTATUS error, std::string_view operation) noexcept {
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
        QIQI_LOG_WARN("{} failed (error={})", operation, error);
    }
    return error == ERROR_SUCCESS;
}

bool ReadRegDword(HKEY key, const wchar_t* name, DWORD& out_val) noexcept {
    DWORD type = 0;
    DWORD cb = sizeof(out_val);
    return CheckRegistryRead(::RegQueryValueExW(
               key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&out_val), &cb), "ReadRegDword::RegQueryValueExW") &&
           type == REG_DWORD;
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

bool WriteRegString(HKEY key, const wchar_t* name, std::wstring_view val) noexcept {
    return SUCCEEDED(WriteRegStringResult(key, name, val));
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
    if ((access & KEY_WRITE) != 0) {
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

HRESULT BumpRegistrySequenceResult(HKEY key) noexcept {
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
    return S_OK;
}

bool BumpRegistrySequence(HKEY key) noexcept {
    return SUCCEEDED(BumpRegistrySequenceResult(key));
}

const wchar_t* g_config_reg_subkey_override = nullptr;

} // namespace

void SetConfigRegistrySubKeyOverrideForTest(const wchar_t* subkey_or_null) noexcept {
    g_config_reg_subkey_override = subkey_or_null;
}

const wchar_t* GetActiveConfigRegistrySubKey() noexcept {
    return g_config_reg_subkey_override ? g_config_reg_subkey_override : kDefaultConfigRegSubKey;
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

bool SaveConfigToRegistry(const QiQiConfig& config, const wchar_t* subkey) noexcept {
    try {
        ScopedKernelTransaction txn;
        if (!txn) {
            QIQI_LOG_ERROR("Failed to create registry transaction (error={})", txn.Error());
            return false;
        }
        ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ | KEY_WRITE, txn.Get());
        if (!key) {
            return false;
        }
        bool fields_saved = true;
        ForEachConfigField([&](const auto& field) {
            if (fields_saved) {
                fields_saved = WriteRegDword(
                    key.Get(), field.registry_key, gsl::narrow_cast<DWORD>(field.RegistryValue(config)));
            }
        });
        if (!fields_saved ||
            !WriteRegString(key.Get(), L"PrimaryDictPath", config.primary_dict_path) ||
            !WriteRegString(key.Get(), L"UserDictPath", config.user_dict_path)) {
            return false;
        }
        if (!BumpRegistrySequence(key.Get())) {
            return false;
        }
        key.Reset();
        if (!txn.Commit()) {
            QIQI_LOG_ERROR("SaveConfigToRegistry::CommitTransaction failed (error={})", txn.Error());
            return false;
        }
        return true;
    } catch (...) {
        QIQI_LOG_ERROR("SaveConfigToRegistry failed with a C++ exception");
        return false;
    }
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
            if (ReadRegDword(key.Get(), field.registry_key, dw)) {
                std::ignore = field.Assign(cfg, dw);
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

void MergeSettingsConfig(
    QiQiConfig& external, const QiQiConfig& baseline, const QiQiConfig& local) noexcept {
    ForEachConfigField([&](const auto& field) noexcept {
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

HRESULT SaveUserDictSyncDirectory(const std::filesystem::path& directory, const wchar_t* subkey) {
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
        const HRESULT sequence_hr = BumpRegistrySequenceResult(key.Get());
        if (FAILED(sequence_hr)) return sequence_hr;
        key.Reset();
        return txn.Commit() ? S_OK : HRESULT_FROM_WIN32(txn.Error());
    };
    const HRESULT hr = save();
    std::ignore = log::CheckHResult(hr, "SaveUserDictSyncDirectory");
    return hr;
}

HRESULT LoadUserDictSyncDirectory(std::filesystem::path& directory, const wchar_t* subkey) {
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

bool SaveConfig(const QiQiConfig& config) noexcept {
    return SaveConfigToRegistry(config);
}

QiQiConfig LoadConfig(void* /*module_handle*/) noexcept {
    QiQiConfig cfg{};
    if (LoadConfigFromRegistry(cfg)) {
        QIQI_LOG_INFO("Loaded user configuration from HKCU\\Software\\QiQiIME (dict={}, page_size={})",
                      PrimaryDictToString(cfg.primary_dict), cfg.page_size);
        return cfg;
    }
    if (!SaveConfigToRegistry(cfg)) {
        QIQI_LOG_WARN("LoadConfig: using in-memory defaults; initial configuration could not be saved");
    }
    return cfg;
}

} // namespace qiqi
