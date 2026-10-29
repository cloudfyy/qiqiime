#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>

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
    spdlog::level::level_enum log_level = spdlog::level::info;
};

// Return default user configuration path: %LOCALAPPDATA%\QiQiIME\config.ini
std::filesystem::path GetDefaultUserConfigPath();

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
