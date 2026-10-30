#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "common/logger.h"

namespace qiqi {

enum class PrimaryDict {
    Jidian,      // wubi86_jidian.txt (default)
    Haifeng,     // wubi86_haifeng.txt
    BuiltinOnly  // Built-in core dictionary only
};

enum class UiLanguage {
    Auto,  // Follow Windows user default UI language (default)
    ZhCn,  // Simplified Chinese (zh-CN)
    EnUs   // English (en-US)
};

struct QiQiConfig {
    // [dictionary]
    PrimaryDict primary_dict = PrimaryDict::Jidian;
    bool enable_jidian_extra = true;
    std::wstring user_dict_path;

    // [engine]
    size_t page_size = 5;
    bool auto_commit_4code = true;
    bool push_commit_5th = true;
    bool semicolon_quote_quick_select = true;
    bool show_code_hint = true;
    bool enable_z_temp_english = true;
    bool dynamic_phrase_freq = false;
    bool default_chinese_mode = true;
    bool default_full_width = false;
    bool default_chinese_punct = true;
    bool shift_toggle_mode = true;
    bool show_status_bar = false;
    bool beep_on_empty_4code = true;
    bool beep_on_dup_4code = false;
    bool clear_on_empty_4code = false;
    bool enable_key_sound = false;
    UiLanguage ui_language = UiLanguage::Auto;

    // [log]
    log::Level log_level = log::Level::info;
    // Collect detailed keystroke/candidate trace into log file for troubleshooting (disabled by default for privacy)
    bool log_keystrokes = false;
};

// Return canonical string name of PrimaryDict ("jidian" | "haifeng" | "builtin").
constexpr std::string_view PrimaryDictToString(PrimaryDict dict) noexcept {
    switch (dict) {
        case PrimaryDict::Haifeng:     return "haifeng";
        case PrimaryDict::BuiltinOnly: return "builtin";
        case PrimaryDict::Jidian:
        default:                       return "jidian";
    }
}

// Bundled dictionary file names shared between RegisterServer prebuild, UnregisterServer cleanup, and runtime loading.
inline constexpr std::array<std::wstring_view, 3> kBundledDictFileNames = {
    L"wubi86_jidian.txt",
    L"wubi86_jidian_extra.txt",
    L"wubi86_haifeng.txt"
};

// Default HKCU registry subkey for cross-process & UWP AppContainer configuration/user-dict synchronization.
inline constexpr const wchar_t* kDefaultConfigRegSubKey = L"Software\\QiQiIME";
void SetConfigRegistrySubKeyOverrideForTest(const wchar_t* subkey_or_null) noexcept;
const wchar_t* GetActiveConfigRegistrySubKey() noexcept;

// Mirror or load QiQiConfig in HKCU\<subkey> (grants S-1-15-2-1 / S-1-15-2-2 KEY_READ for UWP AppContainer).
bool SaveConfigToRegistry(
    const QiQiConfig& config,
    const wchar_t* subkey = nullptr) noexcept;
bool LoadConfigFromRegistry(
    QiQiConfig& out_config,
    uint64_t* out_sequence = nullptr,
    const wchar_t* subkey = nullptr) noexcept;
uint64_t QueryRegistryConfigSequence(
    const wchar_t* subkey = nullptr) noexcept;

// Mirror or load compiled .qdict binary image for user_dict in HKCU\<subkey> so UWP AppContainer processes
// can load user custom words without accessing %LOCALAPPDATA%.
bool SaveUserDictBinaryToRegistry(
    std::span<const uint8_t> qdict_bytes,
    const wchar_t* subkey = nullptr) noexcept;
bool LoadUserDictBinaryFromRegistry(
    std::vector<uint8_t>& out_qdict_bytes,
    const wchar_t* subkey = nullptr);

// Return default user configuration path: %LOCALAPPDATA%\QiQiIME\config.ini
std::filesystem::path GetDefaultUserConfigPath();

// Return default user custom dictionary path: %LOCALAPPDATA%\QiQiIME\user_dict.txt
std::filesystem::path GetDefaultUserDictPath();

// Resolve a bundled data file (e.g. wubi86_jidian.txt or config.ini) relative to the DLL directory,
// checking <dll_dir>/<name>, <dll_dir>/../data/<name>, and <dll_dir>/../<name> so x64 and x86 can share one data/ folder.
std::filesystem::path ResolveBundledDataFile(
    const std::filesystem::path& dll_dir, std::wstring_view file_name);

// Parse UTF-8 INI content into QiQiConfig (starting from defaults).
QiQiConfig ParseConfigFromUtf8(std::string_view utf8_content);

// Serialize QiQiConfig to UTF-8 INI text.
std::string SerializeConfigToUtf8(const QiQiConfig& config);

// Load configuration from a specific INI file path. Returns false if file cannot be opened.
bool LoadConfigFile(const std::filesystem::path& file_path, QiQiConfig& out_config);

// Save QiQiConfig to a specific INI file path.
bool SaveConfigFile(const std::filesystem::path& file_path, const QiQiConfig& config);

// Write a default commented UTF-8 config.ini file if it does not already exist.
bool EnsureDefaultConfigFile(const std::filesystem::path& file_path);

// Discover and load configuration:
// 1. %LOCALAPPDATA%\QiQiIME\config.ini (auto-created with defaults if writable)
// 2. <DLL directory>\config.ini fallback
// 3. Built-in defaults
QiQiConfig LoadConfig(void* module_handle = nullptr);

} // namespace qiqi
