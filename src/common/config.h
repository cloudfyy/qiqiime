// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/trace.h"

#include <windows.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <gsl/gsl>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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

enum class WubiSpaceEnterMode : uint8_t {
    SpaceSelectEnterCode = 0,
    SpaceSelectEnterClear = 1,
    EnterSelectSpaceClear = 2
};

enum class UiTheme { System, Light, Dark, HighContrast };
enum class UiTextScale { Percent100, Percent125, Percent150, Percent200 };
enum class CandidateFontWeight {
    Auto, Thin, ExtraLight, Light, Normal, Medium, SemiBold, Bold, ExtraBold, Black
};

enum class ImeSkinId : uint32_t {
    Default, Firefly, Inkstone, JadePool, StarryAbyss, AutumnBrocade, BambooDew,
    IvoryPaper, ObsidianNight, IcySilk, InkSeal, TerracottaDusk, SmokyMist,
    SilverFrost, SunsetClouds, VioletPath, TeaMist, AzureTides, ThawingSnow,
    SpringShore, PeachBlossom, EmeraldHaze,
    Invalid = UINT32_MAX
};

inline constexpr std::array<std::string_view, 22> kImeSkinIds{
    "default", "firefly", "inkstone", "jade-pool", "starry-abyss", "autumn-brocade",
    "bamboo-dew", "ivory-paper", "obsidian-night", "icy-silk", "ink-seal",
    "terracotta-dusk", "smoky-mist", "silver-frost", "sunset-clouds", "violet-path",
    "tea-mist", "azure-tides", "thawing-snow", "spring-shore", "peach-blossom", "emerald-haze"
};

constexpr std::string_view ImeSkinIdToString(ImeSkinId id) noexcept {
    const auto index = std::to_underlying(id);
    return index < kImeSkinIds.size() ? gsl::at(kImeSkinIds, index) : std::string_view{};
}

template <typename Character>
constexpr std::optional<ImeSkinId> ParseImeSkinId(std::basic_string_view<Character> text) noexcept {
    for (const auto indexed_id : std::views::enumerate(kImeSkinIds)) {
        const auto index = std::get<0>(indexed_id);
        const std::string_view id = std::get<1>(indexed_id);
        if (std::ranges::equal(text, id)) {
            return ImeSkinId{
                gsl::narrow_cast<std::underlying_type_t<ImeSkinId>>(index)};
        }
    }
    return std::nullopt;
}

struct DisplayPreferences {
    UiTheme theme = UiTheme::System;
    UiTextScale text_scale = UiTextScale::Percent100;
    bool easy_reading = false;

    bool operator==(const DisplayPreferences&) const noexcept = default;
};

constexpr int TextScalePercent(UiTextScale scale) noexcept {
    switch (scale) {
        case UiTextScale::Percent125: return 125;
        case UiTextScale::Percent150: return 150;
        case UiTextScale::Percent200: return 200;
        default: return 100;
    }
}

constexpr LONG CandidateFontWeightToGdi(CandidateFontWeight weight) noexcept {
    switch (weight) {
        case CandidateFontWeight::Thin: return FW_THIN;
        case CandidateFontWeight::ExtraLight: return FW_EXTRALIGHT;
        case CandidateFontWeight::Light: return FW_LIGHT;
        case CandidateFontWeight::Normal: return FW_NORMAL;
        case CandidateFontWeight::Medium: return FW_MEDIUM;
        case CandidateFontWeight::SemiBold: return FW_SEMIBOLD;
        case CandidateFontWeight::Bold: return FW_BOLD;
        case CandidateFontWeight::ExtraBold: return FW_EXTRABOLD;
        case CandidateFontWeight::Black: return FW_HEAVY;
        case CandidateFontWeight::Auto:
        default: return FW_NORMAL;
    }
}

constexpr CandidateFontWeight CandidateFontWeightFromGdi(LONG weight) noexcept {
    if (weight <= 0) return CandidateFontWeight::Auto;
    const LONG rounded = std::clamp<LONG>((weight + 50) / 100, 1, 9);
    switch (rounded) {
        case 1: return CandidateFontWeight::Thin;
        case 2: return CandidateFontWeight::ExtraLight;
        case 3: return CandidateFontWeight::Light;
        case 4: return CandidateFontWeight::Normal;
        case 5: return CandidateFontWeight::Medium;
        case 6: return CandidateFontWeight::SemiBold;
        case 7: return CandidateFontWeight::Bold;
        case 8: return CandidateFontWeight::ExtraBold;
        case 9: return CandidateFontWeight::Black;
        default: return CandidateFontWeight::Auto;
    }
}

struct CandidateFontPreferences {
    std::wstring family;
    size_t point_size = 0;
    CandidateFontWeight weight = CandidateFontWeight::Auto;
    bool italic = false;

    bool operator==(const CandidateFontPreferences&) const noexcept = default;
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
    bool enable_code_completion = true;
    bool single_character_mode = false;
    bool enable_z_temp_english = true;
    bool dynamic_phrase_freq = false;
    bool default_chinese_mode = true;
    bool default_full_width = false;
    bool default_chinese_punct = true;
    bool shift_toggle_mode = true;
    // Keep the persisted key for compatibility; also applies when switching away from this IME.
    bool commit_on_english_switch = true;
    bool show_status_bar = false;
    bool beep_on_empty_4code = true;
    bool beep_on_dup_4code = false;
    bool clear_on_empty_4code = false;
    bool enable_key_sound = false;
    bool up_down_page_turn = false;
    WubiSpaceEnterMode space_enter_mode = WubiSpaceEnterMode::SpaceSelectEnterCode;
    UiLanguage ui_language = UiLanguage::Auto;
    CompositionUnderlineStyle underline_style = CompositionUnderlineStyle::Dot;
    bool bold_underline = false;
    UiTheme ui_theme = UiTheme::System;
    UiTextScale ui_text_scale = UiTextScale::Percent100;
    bool easy_reading = false;
    ImeSkinId ime_skin_id = ImeSkinId::Default;
    std::wstring candidate_font_family;
    size_t candidate_font_size = 0;
    CandidateFontWeight candidate_font_weight = CandidateFontWeight::Auto;
    bool candidate_font_italic = false;
};

constexpr DisplayPreferences GetDisplayPreferences(const QiQiConfig& config) noexcept {
    return {config.ui_theme, config.ui_text_scale, config.easy_reading};
}

inline CandidateFontPreferences GetCandidateFontPreferences(const QiQiConfig& config) {
    return {
        config.candidate_font_family,
        config.candidate_font_size,
        config.candidate_font_weight,
        config.candidate_font_italic,
    };
}

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
            return gsl::narrow_cast<uint64_t>(std::to_underlying(config.*member));
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
                config.*member = T{gsl::narrow_cast<std::underlying_type_t<T>>(value)};
            } else {
                config.*member = gsl::narrow_cast<T>(value);
            }
        }
        return true;
    }
};

// Shared scalar field metadata; order and stable keys define registry and JSON transfer.
// Machine-specific paths remain registry-only.
inline constexpr ConfigField kUiLanguageConfigField{
    "ui_language", L"UiLanguage", &QiQiConfig::ui_language, 0, 2, true};
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
    kUiLanguageConfigField,
    ConfigField{"underline_style", L"UnderlineStyle", &QiQiConfig::underline_style, 0, 4, true},
    ConfigField{"bold_underline", L"BoldUnderline", &QiQiConfig::bold_underline, 0, 1, true},
    ConfigField{"ui_theme", L"UiTheme", &QiQiConfig::ui_theme, 0, 3, true},
    ConfigField{"ui_text_scale", L"UiTextScale", &QiQiConfig::ui_text_scale, 0, 3, true},
    ConfigField{"easy_reading", L"EasyReading", &QiQiConfig::easy_reading, 0, 1, true},
    ConfigField{"enable_code_completion", L"EnableCodeCompletion", &QiQiConfig::enable_code_completion, 0, 1, true},
    ConfigField{"ime_skin_id", L"ImeSkinId", &QiQiConfig::ime_skin_id, 0, 21, true},
    ConfigField{"candidate_font_size", L"CandidateFontSize", &QiQiConfig::candidate_font_size, 0, 72, true},
    ConfigField{"candidate_font_weight", L"CandidateFontWeight", &QiQiConfig::candidate_font_weight, 0, 9, true},
    ConfigField{"candidate_font_italic", L"CandidateFontItalic", &QiQiConfig::candidate_font_italic, 0, 1, true},
    ConfigField{"space_enter_mode", L"SpaceEnterMode", &QiQiConfig::space_enter_mode, 0,
        std::to_underlying(WubiSpaceEnterMode::EnterSelectSpaceClear), true},
    ConfigField{"single_character_mode", L"SingleCharacterMode", &QiQiConfig::single_character_mode, 0, 1, true}
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

struct ConfigStringField {
    std::string_view backup_key;
    const wchar_t* registry_key;
    std::wstring QiQiConfig::* member;
    bool settings_editable;

    constexpr bool HasSettingsChange(const QiQiConfig& baseline, const QiQiConfig& current) const noexcept {
        return settings_editable && current.*member != baseline.*member;
    }
};

inline constexpr auto kConfigStringFields = std::tuple{
    ConfigStringField{"candidate_font_family", L"CandidateFontFamily",
        &QiQiConfig::candidate_font_family, true},
};

template <typename Visitor>
constexpr void ForEachConfigStringField(Visitor&& visitor)
    noexcept(std::apply([](const auto&... fields) {
        return (std::is_nothrow_invocable_v<Visitor&, decltype(fields)> && ...);
    }, kConfigStringFields)) {
    std::apply([&visitor](const auto&... fields)
        noexcept((std::is_nothrow_invocable_v<Visitor&, decltype(fields)> && ...)) {
        (visitor(fields), ...);
    }, kConfigStringFields);
}

constexpr bool IsValidCandidateFontFamily(std::wstring_view family) noexcept {
    return family.size() < LF_FACESIZE && family.find(L'\0') == std::wstring_view::npos;
}

void MergeSettingsConfig(
    QiQiConfig& external, const QiQiConfig& baseline, const QiQiConfig& local);

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
bool HasConfigRegistrySubKeyOverride() noexcept;

HRESULT OpenConfigRegistryKeyResult(
    HKEY* key, REGSAM access, const wchar_t* subkey = nullptr, HANDLE transaction = nullptr) noexcept;
HRESULT WriteRegistryDwordResult(HKEY key, const wchar_t* name, DWORD value) noexcept;
HRESULT ReadRegistryDwordResult(HKEY key, const wchar_t* name, DWORD& value) noexcept;
HRESULT WriteRegistryStringResult(HKEY key, const wchar_t* name, std::wstring_view value) noexcept;
HRESULT ReadRegistryStringResult(HKEY key, const wchar_t* name, std::wstring& value);
HRESULT BumpConfigRegistrySequenceResult(HKEY key) noexcept;

// Save or load QiQiConfig as native REG_DWORD / REG_SZ values in HKCU\<subkey>
// (grants S-1-15-2-1 / S-1-15-2-2 KEY_READ for UWP AppContainer).
// The result variant retains Win32 failure codes; the bool variant preserves existing callers.
HRESULT SaveConfigToRegistryResult(
    const QiQiConfig& config,
    const wchar_t* subkey = nullptr,
    const GUID* commit_trace_id = nullptr,
    const GUID* commit_activity_id = nullptr) noexcept;
HRESULT LoadConfigCommitTraceFromRegistry(
    GUID& trace_id,
    GUID& activity_id,
    uint64_t& revision,
    const wchar_t* subkey = nullptr) noexcept;
HRESULT EnsureConfigAppContainerReadAccess(
    const wchar_t* subkey = nullptr) noexcept;
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
HRESULT SaveUserDictSyncDirectoryToRegistry(
    const std::filesystem::path& directory,
    const wchar_t* subkey = nullptr,
    const GUID* commit_trace_id = nullptr,
    const GUID* commit_activity_id = nullptr,
    bool* changed = nullptr);
HRESULT LoadUserDictSyncDirectoryFromRegistry(
    std::filesystem::path& directory,
    const wchar_t* subkey = nullptr);

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
HRESULT SaveConfigResult(
    const QiQiConfig& config) noexcept;
HRESULT SaveConfigResult(
    const QiQiConfig& config,
    const trace::Context& trace) noexcept;

} // namespace qiqi
