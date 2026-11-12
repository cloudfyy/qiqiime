// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <gsl/gsl>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>
#include "common/logger.h"

namespace qiqi {

enum class PrimaryDict {
    Jidian,      // wubi86_jidian.txt (default)
    Haifeng,     // wubi86_haifeng.txt
    BuiltinOnly, // Built-in core dictionary only
    Custom       // User-selected custom Wubi 86 primary dictionary file (primary_dict_path)
};

enum class UiLanguage {
    Auto,  // Follow Windows user default UI language (default)
    ZhCn,  // Simplified Chinese (zh-CN)
    EnUs   // English (en-US)
};

enum class CompositionUnderlineStyle {
    Dot,      // TF_LS_DOT (default)
    Solid,    // TF_LS_SOLID
    Dash,     // TF_LS_DASH
    Squiggle, // TF_LS_SQUIGGLE
    None      // TF_LS_NONE
};

enum class CandidateLayout {
    Horizontal,
    Vertical
};

struct QiQiConfig {
    // [dictionary]
    PrimaryDict primary_dict = PrimaryDict::Jidian;
    bool enable_jidian_extra = true;
    std::wstring primary_dict_path;
    std::wstring user_dict_path;

    // [engine]
    size_t page_size = 5;
    CandidateLayout candidate_layout = CandidateLayout::Horizontal;
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
    bool commit_on_english_switch = true;
    bool show_status_bar = false;
    bool beep_on_empty_4code = true;
    bool beep_on_dup_4code = false;
    bool clear_on_empty_4code = false;
    bool enable_key_sound = false;
    bool up_down_page_turn = false;
    UiLanguage ui_language = UiLanguage::Auto;
    CompositionUnderlineStyle underline_style = CompositionUnderlineStyle::Dot;
    bool bold_underline = false;

    // [log]
    log::Level log_level = log::Level::info;
};

template <typename T>
struct ConfigField {
    std::string_view backup_key;
    const wchar_t* registry_key;
    T QiQiConfig::* member;
    uint64_t minimum;
    uint64_t maximum;
    bool settings_editable;

    constexpr bool HasSettingsChange(const QiQiConfig& baseline, const QiQiConfig& current) const noexcept {
        return settings_editable && current.*member != baseline.*member;
    }

    constexpr uint64_t Value(const QiQiConfig& config) const noexcept {
        if constexpr (std::is_enum_v<T>) {
            return static_cast<uint64_t>(config.*member);
        } else {
            return gsl::narrow_cast<uint64_t>(config.*member);
        }
    }

    constexpr uint64_t RegistryValue(const QiQiConfig& config) const noexcept {
        if constexpr (std::is_same_v<T, size_t>) {
            return std::clamp(Value(config), minimum, maximum);
        } else {
            return Value(config);
        }
    }

    constexpr bool Assign(QiQiConfig& config, uint64_t value) const noexcept {
        if constexpr (std::is_same_v<T, bool>) {
            config.*member = (value != 0);
        } else {
            if (value < minimum || value > maximum) return false;
            if constexpr (std::is_enum_v<T>) {
                config.*member = static_cast<T>(value);
            } else {
                config.*member = gsl::narrow_cast<T>(value);
            }
        }
        return true;
    }
};

// Order and keys preserve the backup format; paths remain registry-only.
inline constexpr auto kConfigFields = std::tuple{
    ConfigField{"primary_dict", L"PrimaryDict", &QiQiConfig::primary_dict, 0, 3, true},
    ConfigField{"enable_jidian_extra", L"EnableJidianExtra", &QiQiConfig::enable_jidian_extra, 0, 1, true},
    ConfigField{"page_size", L"PageSize", &QiQiConfig::page_size, 1, 9, true},
    ConfigField{"candidate_layout", L"CandidateLayout", &QiQiConfig::candidate_layout, 0, 1, true},
    ConfigField{"auto_commit_4code", L"AutoCommit4Code", &QiQiConfig::auto_commit_4code, 0, 1, true},
    ConfigField{"push_commit_5th", L"PushCommit5th", &QiQiConfig::push_commit_5th, 0, 1, true},
    ConfigField{"semicolon_quote_quick_select", L"SemicolonQuoteQuickSelect", &QiQiConfig::semicolon_quote_quick_select, 0, 1, true},
    ConfigField{"show_code_hint", L"ShowCodeHint", &QiQiConfig::show_code_hint, 0, 1, true},
    ConfigField{"enable_z_temp_english", L"EnableZTempEnglish", &QiQiConfig::enable_z_temp_english, 0, 1, false},
    ConfigField{"dynamic_phrase_freq", L"DynamicPhraseFreq", &QiQiConfig::dynamic_phrase_freq, 0, 1, true},
    ConfigField{"default_chinese_mode", L"DefaultChineseMode", &QiQiConfig::default_chinese_mode, 0, 1, false},
    ConfigField{"default_full_width", L"DefaultFullWidth", &QiQiConfig::default_full_width, 0, 1, false},
    ConfigField{"default_chinese_punct", L"DefaultChinesePunct", &QiQiConfig::default_chinese_punct, 0, 1, false},
    ConfigField{"shift_toggle_mode", L"ShiftToggleMode", &QiQiConfig::shift_toggle_mode, 0, 1, true},
    ConfigField{"commit_on_english_switch", L"CommitOnEnglishSwitch", &QiQiConfig::commit_on_english_switch, 0, 1, true},
    ConfigField{"show_status_bar", L"ShowStatusBar", &QiQiConfig::show_status_bar, 0, 1, true},
    ConfigField{"beep_on_empty_4code", L"BeepOnEmpty4Code", &QiQiConfig::beep_on_empty_4code, 0, 1, true},
    ConfigField{"beep_on_dup_4code", L"BeepOnDup4Code", &QiQiConfig::beep_on_dup_4code, 0, 1, true},
    ConfigField{"clear_on_empty_4code", L"ClearOnEmpty4Code", &QiQiConfig::clear_on_empty_4code, 0, 1, true},
    ConfigField{"enable_key_sound", L"EnableKeySound", &QiQiConfig::enable_key_sound, 0, 1, true},
    ConfigField{"up_down_page_turn", L"UpDownPageTurn", &QiQiConfig::up_down_page_turn, 0, 1, true},
    ConfigField{"ui_language", L"UiLanguage", &QiQiConfig::ui_language, 0, 2, true},
    ConfigField{"underline_style", L"UnderlineStyle", &QiQiConfig::underline_style, 0, 4, true},
    ConfigField{"bold_underline", L"BoldUnderline", &QiQiConfig::bold_underline, 0, 1, true},
    ConfigField{"log_level", L"LogLevel", &QiQiConfig::log_level, 0, 6, false}
};

template <typename Visitor>
constexpr void ForEachConfigField(Visitor&& visitor)
    noexcept(std::apply([](const auto&... fields) {
        return (std::is_nothrow_invocable_v<Visitor&, decltype(fields)> && ...);
    }, kConfigFields)) {
    std::apply([&visitor](const auto&... fields)
        noexcept((std::is_nothrow_invocable_v<Visitor&, decltype(fields)> && ...)) {
        (visitor(fields), ...);
    }, kConfigFields);
}

void MergeSettingsConfig(
    QiQiConfig& external, const QiQiConfig& baseline, const QiQiConfig& local) noexcept;

// Return canonical string name of PrimaryDict ("jidian" | "haifeng" | "builtin" | "custom").
constexpr std::string_view PrimaryDictToString(PrimaryDict dict) noexcept {
    switch (dict) {
        case PrimaryDict::Haifeng:     return "haifeng";
        case PrimaryDict::BuiltinOnly: return "builtin";
        case PrimaryDict::Custom:      return "custom";
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

enum class DictionarySourceRole { Primary, Extra };

struct DictionarySource {
    DictionarySourceRole role;
    std::wstring_view file_name;
};

inline constexpr std::array<DictionarySource, 3> kBundledDictionarySources{{
    {DictionarySourceRole::Primary, std::get<0>(kBundledDictFileNames)},
    {DictionarySourceRole::Extra, std::get<1>(kBundledDictFileNames)},
    {DictionarySourceRole::Primary, std::get<2>(kBundledDictFileNames)}
}};

constexpr std::span<const DictionarySource> GetBundledDictionaryLoadPlan(
    PrimaryDict primary, bool enable_extra) noexcept {
    const std::span<const DictionarySource> sources(kBundledDictionarySources);
    switch (primary) {
        case PrimaryDict::Jidian: return sources.first(enable_extra ? 2 : 1);
        case PrimaryDict::Haifeng: return sources.subspan(2, 1);
        case PrimaryDict::BuiltinOnly:
        case PrimaryDict::Custom:
        default: return {};
    }
}

// Resolve a configured dictionary path (either absolute or relative to module_dir).
std::filesystem::path ResolveConfiguredDictionaryPath(
    const std::filesystem::path& module_dir,
    std::wstring_view configured_path);

// Return the display C-string for a configured dictionary path (or empty_placeholder when unset).
constexpr const wchar_t* ResolveDictionaryPathDisplayText(
    const std::wstring& configured_path,
    const wchar_t* empty_placeholder) noexcept {
    return configured_path.empty() ? empty_placeholder : configured_path.c_str();
}

// Default HKCU registry subkey for cross-process & UWP AppContainer configuration/user-dict synchronization.
inline constexpr const wchar_t* kDefaultConfigRegSubKey = L"Software\\QiQiIME";
void SetConfigRegistrySubKeyOverrideForTest(const wchar_t* subkey_or_null) noexcept;
const wchar_t* GetActiveConfigRegistrySubKey() noexcept;

// Save or load QiQiConfig as native REG_DWORD / REG_SZ values in HKCU\<subkey>
// (grants S-1-15-2-1 / S-1-15-2-2 KEY_READ for UWP AppContainer).
bool SaveConfigToRegistry(
    const QiQiConfig& config,
    const wchar_t* subkey = nullptr) noexcept;
bool LoadConfigFromRegistry(
    QiQiConfig& out_config,
    uint64_t* out_sequence = nullptr,
    const wchar_t* subkey = nullptr) noexcept;
uint64_t QueryRegistryConfigSequence(
    const wchar_t* subkey = nullptr) noexcept;

// Publish only the desktop dictionary channel address, never dictionary contents.
HRESULT SaveUserDictSyncDirectory(const std::filesystem::path& directory, const wchar_t* subkey = nullptr);
HRESULT LoadUserDictSyncDirectory(std::filesystem::path& directory, const wchar_t* subkey = nullptr);

// Return whether the current process is running inside a Windows AppContainer sandbox.
bool IsProcessAppContainer() noexcept;

// Return default user custom dictionary path: %LOCALAPPDATA%\QiQiIME\user_dict.txt
std::filesystem::path GetDefaultUserDictPath();

// Return default user dynamic phrase frequency table path: %LOCALAPPDATA%\QiQiIME\phrase_freq.qfreq
std::filesystem::path GetDefaultPhraseFreqPath();

// Return the dedicated cross-process synchronization channel directory for a given data file path.
std::filesystem::path GetSyncChannelDirectoryForFile(const std::filesystem::path& file_path);

// Resolve a bundled data file (e.g. wubi86_jidian.txt) relative to the DLL directory,
// checking <dll_dir>/<name>, <dll_dir>/../data/<name>, and <dll_dir>/../<name> so x64 and x86 can share one data/ folder.
std::filesystem::path ResolveBundledDataFile(
    const std::filesystem::path& dll_dir, std::wstring_view file_name);

// Load configuration from HKCU\Software\QiQiIME (auto-creating default values on first run).
QiQiConfig LoadConfig(void* module_handle = nullptr) noexcept;
bool SaveConfig(const QiQiConfig& config) noexcept;

} // namespace qiqi
