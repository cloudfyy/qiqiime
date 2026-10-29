#include "common/config.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>

namespace qiqi {

namespace {

std::string_view PrimaryDictToString(PrimaryDict dict) noexcept {
    switch (dict) {
        case PrimaryDict::Haifeng:     return "haifeng";
        case PrimaryDict::BuiltinOnly: return "builtin";
        case PrimaryDict::Jidian:
        default:                       return "jidian";
    }
}

std::string_view LogLevelToString(spdlog::level::level_enum lvl) noexcept {
    switch (lvl) {
        case spdlog::level::trace:    return "trace";
        case spdlog::level::debug:    return "debug";
        case spdlog::level::warn:     return "warn";
        case spdlog::level::err:      return "error";
        case spdlog::level::critical: return "critical";
        case spdlog::level::off:      return "off";
        case spdlog::level::info:
        default:                      return "info";
    }
}

constexpr std::string_view TrimAscii(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = gsl::narrow_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

std::string_view StripInlineComment(std::string_view value) noexcept {
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = gsl::at(value, i);
        if ((ch == '#' || ch == ';') &&
            (i == 0 || gsl::at(value, i - 1) == ' ' || gsl::at(value, i - 1) == '\t')) {
            return TrimAscii(value.substr(0, i));
        }
    }
    return TrimAscii(value);
}

bool ParseBool(std::string_view val, bool default_val) {
    const std::string lower = ToLowerAscii(val);
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    return default_val;
}

spdlog::level::level_enum ParseLogLevel(std::string_view val, spdlog::level::level_enum default_lvl) {
    const std::string lower = ToLowerAscii(val);
    if (lower == "trace") return spdlog::level::trace;
    if (lower == "debug") return spdlog::level::debug;
    if (lower == "info") return spdlog::level::info;
    if (lower == "warn" || lower == "warning") return spdlog::level::warn;
    if (lower == "error" || lower == "err") return spdlog::level::err;
    if (lower == "critical") return spdlog::level::critical;
    if (lower == "off" || lower == "none") return spdlog::level::off;
    return default_lvl;
}

PrimaryDict ParsePrimaryDict(std::string_view val, PrimaryDict default_dict) {
    const std::string lower = ToLowerAscii(val);
    if (lower == "jidian" || lower == "wubi86_jidian") return PrimaryDict::Jidian;
    if (lower == "haifeng" || lower == "wubi86_haifeng") return PrimaryDict::Haifeng;
    if (lower == "builtin" || lower == "none") return PrimaryDict::BuiltinOnly;
    return default_dict;
}

std::string_view UiLanguageToString(UiLanguage lang) noexcept {
    switch (lang) {
        case UiLanguage::ZhCn: return "zh-CN";
        case UiLanguage::EnUs: return "en-US";
        case UiLanguage::Auto:
        default:               return "auto";
    }
}

UiLanguage ParseUiLanguage(std::string_view val, UiLanguage default_lang) {
    const std::string lower = ToLowerAscii(val);
    if (lower == "auto" || lower == "system" || lower == "default") return UiLanguage::Auto;
    if (lower == "zh-cn" || lower == "zh_cn" || lower == "zh" || lower == "cn" || lower == "chinese") {
        return UiLanguage::ZhCn;
    }
    if (lower == "en-us" || lower == "en_us" || lower == "en" || lower == "english") {
        return UiLanguage::EnUs;
    }
    return default_lang;
}

} // namespace

std::filesystem::path GetDefaultUserConfigPath() {
    const std::filesystem::path app_dir = log::GetAppDataDirectory();
    if (app_dir.empty()) {
        return L"config.ini";
    }
    return app_dir / L"config.ini";
}

std::filesystem::path GetDefaultUserDictPath() {
    const std::filesystem::path app_dir = log::GetAppDataDirectory();
    if (app_dir.empty()) {
        return L"user_dict.txt";
    }
    return app_dir / L"user_dict.txt";
}

QiQiConfig ParseConfigFromUtf8(std::string_view utf8_content) {
    QiQiConfig cfg{};
    utf8_content = log::StripUtf8Bom(utf8_content);

    std::string current_section;
    while (!utf8_content.empty()) {
        const size_t nl = utf8_content.find('\n');
        std::string_view line = (nl == std::string_view::npos)
            ? utf8_content
            : utf8_content.substr(0, nl);
        utf8_content.remove_prefix(nl == std::string_view::npos ? utf8_content.size() : nl + 1);

        line = TrimAscii(line);
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
            continue;
        }

        if (line.starts_with('[') && line.ends_with(']')) {
            current_section = ToLowerAscii(TrimAscii(line.substr(1, line.size() - 2)));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }

        const std::string key = ToLowerAscii(TrimAscii(line.substr(0, eq)));
        const std::string_view val = StripInlineComment(line.substr(eq + 1));

        if (current_section == "dictionary") {
            if (key == "primary") {
                cfg.primary_dict = ParsePrimaryDict(val, cfg.primary_dict);
            } else if (key == "enable_jidian_extra") {
                cfg.enable_jidian_extra = ParseBool(val, cfg.enable_jidian_extra);
            } else if (key == "user_dict") {
                cfg.user_dict_path = log::Utf8ToWide(val);
            }
        } else if (current_section == "engine") {
            if (key == "page_size") {
                if (val.size() == 1 && val.front() >= '1' && val.front() <= '9') {
                    cfg.page_size = static_cast<size_t>(val.front()) - static_cast<size_t>('0');
                }
            } else if (key == "auto_commit_4code") {
                cfg.auto_commit_4code = ParseBool(val, cfg.auto_commit_4code);
            } else if (key == "push_commit_5th") {
                cfg.push_commit_5th = ParseBool(val, cfg.push_commit_5th);
            } else if (key == "semicolon_quote_quick_select") {
                cfg.semicolon_quote_quick_select = ParseBool(val, cfg.semicolon_quote_quick_select);
            } else if (key == "show_code_hint") {
                cfg.show_code_hint = ParseBool(val, cfg.show_code_hint);
            } else if (key == "default_chinese_mode") {
                cfg.default_chinese_mode = ParseBool(val, cfg.default_chinese_mode);
            } else if (key == "default_full_width") {
                cfg.default_full_width = ParseBool(val, cfg.default_full_width);
            } else if (key == "default_chinese_punct") {
                cfg.default_chinese_punct = ParseBool(val, cfg.default_chinese_punct);
            } else if (key == "shift_toggle_mode") {
                cfg.shift_toggle_mode = ParseBool(val, cfg.shift_toggle_mode);
            } else if (key == "show_status_bar") {
                cfg.show_status_bar = ParseBool(val, cfg.show_status_bar);
            } else if (key == "beep_on_empty_4code") {
                cfg.beep_on_empty_4code = ParseBool(val, cfg.beep_on_empty_4code);
            } else if (key == "beep_on_dup_4code") {
                cfg.beep_on_dup_4code = ParseBool(val, cfg.beep_on_dup_4code);
            } else if (key == "clear_on_empty_4code") {
                cfg.clear_on_empty_4code = ParseBool(val, cfg.clear_on_empty_4code);
            } else if (key == "enable_key_sound") {
                cfg.enable_key_sound = ParseBool(val, cfg.enable_key_sound);
            } else if (key == "ui_language" || key == "language") {
                cfg.ui_language = ParseUiLanguage(val, cfg.ui_language);
            }
        } else if (current_section == "log") {
            if (key == "level") {
                cfg.log_level = ParseLogLevel(val, cfg.log_level);
            }
        }
    }

    return cfg;
}

std::string SerializeConfigToUtf8(const QiQiConfig& config) {
    return std::format(
        "# QiQi 五笔输入法配置文件 (UTF-8 INI 格式)\n"
        "# 默认用户配置路径: %LOCALAPPDATA%\\QiQiIME\\config.ini\n\n"
        "[dictionary]\n"
        "# 主词库选择: jidian (极点86主词库) | haifeng (海峰86超大字符集) | builtin (仅内置)\n"
        "primary = {}\n"
        "# 是否叠加加载极点五笔现代扩展词库 (wubi86_jidian_extra.txt)\n"
        "enable_jidian_extra = {}\n"
        "# 用户自定义扩展码表路径 (可选)\n"
        "user_dict = {}\n\n"
        "[engine]\n"
        "# 候选窗口每页显示的候选词数量 (1 - 9)\n"
        "page_size = {}\n"
        "# 四码唯一时是否自动上屏\n"
        "auto_commit_4code = {}\n"
        "# 第五码是否顶字上屏\n"
        "push_commit_5th = {}\n"
        "# 是否启用分号(;)与单引号(')快捷选择第 2、3 候选词\n"
        "semicolon_quote_quick_select = {}\n"
        "# 输入 1~3 码时是否在候选词旁显示后续编码提示 (~code)\n"
        "show_code_hint = {}\n"
        "# 默认初始模式是否中文\n"
        "default_chinese_mode = {}\n"
        "# 默认是否启用全角字符模式 (运行期可通过 Shift+Space 切换)\n"
        "default_full_width = {}\n"
        "# 默认是否启用中文全角标点 (运行期可通过 Ctrl+. 切换)\n"
        "default_chinese_punct = {}\n"
        "# 是否允许单按 Shift 键切换中英文模式\n"
        "shift_toggle_mode = {}\n"
        "# 是否显示桌面浮动状态条 (默认关闭)\n"
        "show_status_bar = {}\n"
        "# 四码空码（无匹配词）时是否发出提示音\n"
        "beep_on_empty_4code = {}\n"
        "# 四码重码（有多个候选词）时是否发出提示音 (默认关闭)\n"
        "beep_on_dup_4code = {}\n"
        "# 空码（无匹配词）时是否自动清空已输入编码 (默认关闭)\n"
        "clear_on_empty_4code = {}\n"
        "# 是否启用打字按键音效 (默认关闭)\n"
        "enable_key_sound = {}\n"
        "# 界面语言: auto (跟随系统) | zh-CN (简体中文) | en-US (English)\n"
        "ui_language = {}\n\n"
        "[log]\n"
        "# 日志级别: trace | debug | info | warn | error | off\n"
        "level = {}\n",
        PrimaryDictToString(config.primary_dict),
        config.enable_jidian_extra ? "true" : "false",
        log::WideToUtf8(config.user_dict_path),
        std::clamp<size_t>(config.page_size, 1, 9),
        config.auto_commit_4code ? "true" : "false",
        config.push_commit_5th ? "true" : "false",
        config.semicolon_quote_quick_select ? "true" : "false",
        config.show_code_hint ? "true" : "false",
        config.default_chinese_mode ? "true" : "false",
        config.default_full_width ? "true" : "false",
        config.default_chinese_punct ? "true" : "false",
        config.shift_toggle_mode ? "true" : "false",
        config.show_status_bar ? "true" : "false",
        config.beep_on_empty_4code ? "true" : "false",
        config.beep_on_dup_4code ? "true" : "false",
        config.clear_on_empty_4code ? "true" : "false",
        config.enable_key_sound ? "true" : "false",
        UiLanguageToString(config.ui_language),
        LogLevelToString(config.log_level));
}

bool LoadConfigFile(const std::filesystem::path& file_path, QiQiConfig& out_config) {
    std::string content;
    if (!log::ReadFileToUtf8String(file_path, content)) {
        QIQI_LOG_DEBUG("LoadConfigFile: could not open or read '{}'", log::WideToUtf8(file_path.wstring()));
        return false;
    }
    out_config = ParseConfigFromUtf8(content);
    QIQI_LOG_DEBUG(
        "Loaded config from '{}': dict={}, extra={}, page_size={}, auto4={}, push5={}, "
        "beep_empty4={}, clear_empty4={}, key_sound={}, status_bar={}, lang={}, log_level={}",
        log::WideToUtf8(file_path.wstring()),
        PrimaryDictToString(out_config.primary_dict),
        out_config.enable_jidian_extra,
        out_config.page_size,
        out_config.auto_commit_4code,
        out_config.push_commit_5th,
        out_config.beep_on_empty_4code,
        out_config.clear_on_empty_4code,
        out_config.enable_key_sound,
        out_config.show_status_bar,
        UiLanguageToString(out_config.ui_language),
        LogLevelToString(out_config.log_level));
    return true;
}

bool SaveConfigFile(const std::filesystem::path& file_path, const QiQiConfig& config) {
    try {
        std::error_code ec;
        if (std::filesystem::is_directory(file_path, ec)) {
            QIQI_LOG_WARN("SaveConfigFile: target path '{}' is a directory", log::WideToUtf8(file_path.wstring()));
            return false;
        }
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path(), ec);
        }

        const std::string text = SerializeConfigToUtf8(config);
        const std::filesystem::path tmp_path =
            file_path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId());

        {
            // Exclusive write access (dwShareMode = 0), write-only permission (GENERIC_WRITE),
            // and anonymous SQOS to prevent named-pipe impersonation.
            const ScopedFileHandle file(::CreateFileW(
                tmp_path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                nullptr));
            if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                QIQI_LOG_WARN("SaveConfigFile: failed to create temp file '{}' (error={})",
                              log::WideToUtf8(tmp_path.wstring()), ::GetLastError());
                return false;
            }

            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(text.size());
            if (to_write > 0 &&
                (!::WriteFile(file.Get(), text.data(), to_write, &written, nullptr) || written != to_write)) {
                QIQI_LOG_WARN("SaveConfigFile: failed to write temp file '{}' (error={})",
                              log::WideToUtf8(tmp_path.wstring()), ::GetLastError());
                return false;
            }
        }

        if (!::MoveFileExW(tmp_path.c_str(), file_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            QIQI_LOG_WARN("SaveConfigFile: MoveFileExW '{}' -> '{}' failed (error={})",
                          log::WideToUtf8(tmp_path.wstring()),
                          log::WideToUtf8(file_path.wstring()),
                          ::GetLastError());
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
        QIQI_LOG_INFO("Saved configuration to '{}'", log::WideToUtf8(file_path.wstring()));
        return true;
    } catch (...) {
        return false;
    }
}

bool EnsureDefaultConfigFile(const std::filesystem::path& file_path) {
    try {
        if (std::filesystem::exists(file_path)) {
            return true;
        }
        QIQI_LOG_INFO("Creating default user config file at '{}'", log::WideToUtf8(file_path.wstring()));
        return SaveConfigFile(file_path, QiQiConfig{});
    } catch (...) {
        return false;
    }
}

QiQiConfig LoadConfig(void* module_handle) {
    QiQiConfig cfg{};

    // 1. Check user config path: %LOCALAPPDATA%\QiQiIME\config.ini
    const std::filesystem::path user_cfg_path = GetDefaultUserConfigPath();
    if (std::filesystem::exists(user_cfg_path) && LoadConfigFile(user_cfg_path, cfg)) {
        QIQI_LOG_INFO("Loaded user configuration from '{}'", log::WideToUtf8(user_cfg_path.wstring()));
        return cfg;
    }

    // 2. Check config.ini next to the DLL / executable
    const std::filesystem::path module_path = log::GetModuleFilePath(module_handle);
    if (!module_path.empty()) {
        const std::filesystem::path dll_cfg_path = module_path.parent_path() / L"config.ini";
        if (std::filesystem::exists(dll_cfg_path) && LoadConfigFile(dll_cfg_path, cfg)) {
            QIQI_LOG_INFO("Loaded fallback configuration from '{}'", log::WideToUtf8(dll_cfg_path.wstring()));
        }
    }

    // 3. Create default user config file for easy future customization
    EnsureDefaultConfigFile(user_cfg_path);
    return cfg;
}

} // namespace qiqi
