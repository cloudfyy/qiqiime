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

struct BoolRegBinding {
    const wchar_t* value_name;
    bool QiQiConfig::* field;
};

constexpr std::array<BoolRegBinding, 15> kBoolRegBindings = {{
    {L"EnableJidianExtra",         &QiQiConfig::enable_jidian_extra},
    {L"AutoCommit4Code",           &QiQiConfig::auto_commit_4code},
    {L"PushCommit5th",             &QiQiConfig::push_commit_5th},
    {L"SemicolonQuoteQuickSelect", &QiQiConfig::semicolon_quote_quick_select},
    {L"ShowCodeHint",              &QiQiConfig::show_code_hint},
    {L"EnableZTempEnglish",        &QiQiConfig::enable_z_temp_english},
    {L"DynamicPhraseFreq",         &QiQiConfig::dynamic_phrase_freq},
    {L"DefaultChineseMode",        &QiQiConfig::default_chinese_mode},
    {L"DefaultFullWidth",          &QiQiConfig::default_full_width},
    {L"DefaultChinesePunct",       &QiQiConfig::default_chinese_punct},
    {L"ShiftToggleMode",           &QiQiConfig::shift_toggle_mode},
    {L"ShowStatusBar",             &QiQiConfig::show_status_bar},
    {L"BeepOnEmpty4Code",          &QiQiConfig::beep_on_empty_4code},
    {L"BeepOnDup4Code",            &QiQiConfig::beep_on_dup_4code},
    {L"ClearOnEmpty4Code",         &QiQiConfig::clear_on_empty_4code},
}};

bool WriteRegDword(HKEY key, const wchar_t* name, DWORD val) noexcept {
    return ::RegSetValueExW(
               key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val)) == ERROR_SUCCESS;
}

bool ReadRegDword(HKEY key, const wchar_t* name, DWORD& out_val) noexcept {
    DWORD type = 0;
    DWORD cb = sizeof(out_val);
    return ::RegQueryValueExW(
               key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&out_val), &cb) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

bool WriteRegString(HKEY key, const wchar_t* name, std::wstring_view val) noexcept {
    const DWORD cb = gsl::narrow_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
    return ::RegSetValueExW(
               key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.data()), cb) == ERROR_SUCCESS;
}

bool ReadRegString(HKEY key, const wchar_t* name, std::wstring& out_val) {
    DWORD type = 0;
    DWORD cb = 0;
    constexpr DWORD kMaxStringBytes = 4096;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS ||
        type != REG_SZ || cb < sizeof(wchar_t) || cb > kMaxStringBytes) {
        return false;
    }
    std::wstring buf(cb / sizeof(wchar_t), L'\0');
    if (::RegQueryValueExW(
            key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS) {
        return false;
    }
    while (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    out_val = std::move(buf);
    return true;
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
    if (::GetSecurityInfo(
            key,
            SE_REGISTRY_KEY,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &old_dacl,
            nullptr,
            &sd_raw) != ERROR_SUCCESS) {
        return;
    }
    const ScopedLocalAlloc sd_guard(sd_raw);

    PSID sid_all_apps = nullptr;
    PSID sid_restricted_apps = nullptr;
    std::ignore = ::ConvertStringSidToSidW(L"S-1-15-2-1", &sid_all_apps);
    std::ignore = ::ConvertStringSidToSidW(L"S-1-15-2-2", &sid_restricted_apps);
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
    if (::SetEntriesInAclW(ea_count, ea.data(), old_dacl, &new_dacl) == ERROR_SUCCESS && new_dacl) {
        const ScopedLocalAlloc dacl_guard(new_dacl);
        std::ignore = ::SetSecurityInfo(
            key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
    }
}

ScopedRegKey OpenOrCreateConfigRegKey(const wchar_t* subkey, REGSAM access) noexcept {
    if (!subkey) {
        subkey = GetActiveConfigRegistrySubKey();
    }
    if (*subkey == L'\0') {
        return {};
    }
    ScopedRegKey key;
    if ((access & KEY_WRITE) != 0) {
        DWORD disp = 0;
        if (::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                subkey,
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                access | WRITE_DAC | READ_CONTROL,
                nullptr,
                key.Put(),
                &disp) == ERROR_SUCCESS) {
            GrantAppContainerReadAccessToKey(key.Get());
            return key;
        }
        std::ignore = ::RegCreateKeyExW(
            HKEY_CURRENT_USER, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, key.Put(), nullptr);
    } else {
        std::ignore = ::RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, access, key.Put());
    }
    return key;
}

void BumpRegistrySequenceLocked(HKEY key) noexcept {
    uint64_t seq = 0;
    DWORD cb = sizeof(seq);
    DWORD type = 0;
    std::ignore = ::RegQueryValueExW(
        key, L"ConfigSequence", nullptr, &type, reinterpret_cast<LPBYTE>(&seq), &cb);
    ++seq;
    std::ignore = ::RegSetValueExW(
        key,
        L"ConfigSequence",
        0,
        REG_QWORD,
        reinterpret_cast<const BYTE*>(&seq),
        sizeof(seq));
}

const wchar_t* g_config_reg_subkey_override = nullptr;

} // namespace

void SetConfigRegistrySubKeyOverrideForTest(const wchar_t* subkey_or_null) noexcept {
    g_config_reg_subkey_override = subkey_or_null;
}

const wchar_t* GetActiveConfigRegistrySubKey() noexcept {
    return g_config_reg_subkey_override ? g_config_reg_subkey_override : kDefaultConfigRegSubKey;
}

std::filesystem::path GetDefaultUserDictPath() {
    return ResolveDefaultUserFilePath(L"user_dict.txt");
}

std::filesystem::path GetDefaultPhraseFreqPath() {
    return ResolveDefaultUserFilePath(L"phrase_freq.txt");
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

bool SaveConfigToRegistry(const QiQiConfig& config, const wchar_t* subkey) noexcept {
    try {
        const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ | KEY_WRITE);
        if (!key) {
            return false;
        }
        if (!WriteRegDword(key.Get(), L"PrimaryDict", static_cast<DWORD>(config.primary_dict)) ||
            !WriteRegDword(key.Get(), L"PageSize", gsl::narrow_cast<DWORD>(std::clamp<size_t>(config.page_size, 1, 9))) ||
            !WriteRegDword(key.Get(), L"UiLanguage", static_cast<DWORD>(config.ui_language)) ||
            !WriteRegDword(key.Get(), L"LogLevel", static_cast<DWORD>(config.log_level)) ||
            !WriteRegDword(key.Get(), L"EnableKeySound", config.enable_key_sound ? 1u : 0u) ||
            !WriteRegDword(key.Get(), L"LogKeystrokes", config.log_keystrokes ? 1u : 0u) ||
            !WriteRegString(key.Get(), L"UserDictPath", config.user_dict_path)) {
            return false;
        }
        for (const auto& binding : kBoolRegBindings) {
            if (!WriteRegDword(key.Get(), binding.value_name, (config.*(binding.field)) ? 1u : 0u)) {
                return false;
            }
        }
        BumpRegistrySequenceLocked(key.Get());
        return true;
    } catch (...) {
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
        if (::RegQueryValueExW(
                key.Get(),
                L"ConfigSequence",
                nullptr,
                &type_seq,
                reinterpret_cast<LPBYTE>(&seq),
                &cb_seq) != ERROR_SUCCESS ||
            type_seq != REG_QWORD || seq == 0) {
            return false;
        }
        if (out_sequence) {
            *out_sequence = seq;
        }

        QiQiConfig cfg{};
        DWORD dw = 0;
        if (ReadRegDword(key.Get(), L"PrimaryDict", dw) && dw <= static_cast<DWORD>(PrimaryDict::BuiltinOnly)) {
            cfg.primary_dict = static_cast<PrimaryDict>(dw);
        }
        if (ReadRegDword(key.Get(), L"PageSize", dw) && dw >= 1 && dw <= 9) {
            cfg.page_size = static_cast<size_t>(dw);
        }
        if (ReadRegDword(key.Get(), L"UiLanguage", dw) && dw <= static_cast<DWORD>(UiLanguage::EnUs)) {
            cfg.ui_language = static_cast<UiLanguage>(dw);
        }
        if (ReadRegDword(key.Get(), L"LogLevel", dw) && dw <= static_cast<DWORD>(log::Level::off)) {
            cfg.log_level = static_cast<log::Level>(dw);
        }
        if (ReadRegDword(key.Get(), L"EnableKeySound", dw)) {
            cfg.enable_key_sound = (dw != 0);
        }
        if (ReadRegDword(key.Get(), L"LogKeystrokes", dw)) {
            cfg.log_keystrokes = (dw != 0);
        }
        std::ignore = ReadRegString(key.Get(), L"UserDictPath", cfg.user_dict_path);
        for (const auto& binding : kBoolRegBindings) {
            if (ReadRegDword(key.Get(), binding.value_name, dw)) {
                cfg.*(binding.field) = (dw != 0);
            }
        }
        out_config = std::move(cfg);
        return true;
    } catch (...) {
        return false;
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
    if (::RegQueryValueExW(
            key.Get(),
            L"ConfigSequence",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&seq),
            &cb) == ERROR_SUCCESS &&
        type == REG_QWORD) {
        return seq;
    }
    return 0;
}

bool SaveUserDictBinaryToRegistry(
    std::span<const uint8_t> qdict_bytes,
    const wchar_t* subkey) noexcept {
    try {
        constexpr size_t kMaxRegDictBytes = 512 * 1024;
        if (qdict_bytes.empty() || qdict_bytes.size() > kMaxRegDictBytes) {
            return false;
        }
        const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ | KEY_WRITE);
        if (!key) {
            return false;
        }
        if (::RegSetValueExW(
                key.Get(),
                L"UserDictQdict",
                0,
                REG_BINARY,
                qdict_bytes.data(),
                gsl::narrow_cast<DWORD>(qdict_bytes.size())) != ERROR_SUCCESS) {
            return false;
        }
        BumpRegistrySequenceLocked(key.Get());
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadUserDictBinaryFromRegistry(
    std::vector<uint8_t>& out_qdict_bytes,
    const wchar_t* subkey) {
    const ScopedRegKey key = OpenOrCreateConfigRegKey(subkey, KEY_READ);
    if (!key) {
        return false;
    }
    DWORD type = 0;
    DWORD cb = 0;
    constexpr DWORD kMaxRegDictBytes = 512 * 1024;
    if (::RegQueryValueExW(key.Get(), L"UserDictQdict", nullptr, &type, nullptr, &cb) != ERROR_SUCCESS ||
        type != REG_BINARY || cb == 0 || cb > kMaxRegDictBytes) {
        return false;
    }
    out_qdict_bytes.resize(cb);
    return ::RegQueryValueExW(
               key.Get(),
               L"UserDictQdict",
               nullptr,
               &type,
               out_qdict_bytes.data(),
               &cb) == ERROR_SUCCESS;
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
    SaveConfigToRegistry(cfg);
    return cfg;
}

} // namespace qiqi
