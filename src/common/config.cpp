#include "common/config.h"
#include "common/logger.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string>

namespace qiqi {

namespace {

constexpr std::string_view kDefaultConfigTemplate =
    "# QiQi 五笔输入法配置文件 (UTF-8 INI 格式)\n"
    "# 默认用户配置路径: %LOCALAPPDATA%\\QiQiIME\\config.ini\n\n"
    "[dictionary]\n"
    "# 主词库选择: jidian (极点86主词库) | haifeng (海峰86超大字符集) | builtin (仅内置)\n"
    "primary = jidian\n"
    "# 是否叠加加载极点五笔现代扩展词库 (wubi86_jidian_extra.txt)\n"
    "enable_jidian_extra = true\n"
    "# 用户自定义扩展码表路径 (可选)\n"
    "user_dict =\n\n"
    "[engine]\n"
    "# 候选窗口每页显示的候选词数量 (1 - 9)\n"
    "page_size = 5\n"
    "# 四码唯一时是否自动上屏\n"
    "auto_commit_4code = true\n"
    "# 第五码是否顶字上屏\n"
    "push_commit_5th = true\n"
    "# 默认初始模式是否中文\n"
    "default_chinese_mode = true\n"
    "# 是否允许单按 Shift 键切换中英文模式\n"
    "shift_toggle_mode = true\n\n"
    "[log]\n"
    "# 日志级别: trace | debug | info | warn | error | off\n"
    "level = info\n";

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
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

std::string_view StripInlineComment(std::string_view value) noexcept {
    for (size_t i = 0; i < value.size(); ++i) {
        if ((value[i] == '#' || value[i] == ';') &&
            (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t')) {
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

} // namespace

std::filesystem::path GetDefaultUserConfigPath() {
    const std::filesystem::path app_dir = log::GetAppDataDirectory();
    if (app_dir.empty()) {
        return L"config.ini";
    }
    return app_dir / L"config.ini";
}

QiQiConfig ParseConfigFromUtf8(std::string_view utf8_content) {
    QiQiConfig cfg{};
    if (utf8_content.starts_with("\xEF\xBB\xBF")) {
        utf8_content.remove_prefix(3);
    }

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
                if (val.size() == 1 && val[0] >= '1' && val[0] <= '9') {
                    cfg.page_size = static_cast<size_t>(val[0] - '0');
                }
            } else if (key == "auto_commit_4code") {
                cfg.auto_commit_4code = ParseBool(val, cfg.auto_commit_4code);
            } else if (key == "push_commit_5th") {
                cfg.push_commit_5th = ParseBool(val, cfg.push_commit_5th);
            } else if (key == "default_chinese_mode") {
                cfg.default_chinese_mode = ParseBool(val, cfg.default_chinese_mode);
            } else if (key == "shift_toggle_mode") {
                cfg.shift_toggle_mode = ParseBool(val, cfg.shift_toggle_mode);
            }
        } else if (current_section == "log") {
            if (key == "level") {
                cfg.log_level = ParseLogLevel(val, cfg.log_level);
            }
        }
    }

    return cfg;
}

bool LoadConfigFile(const std::filesystem::path& file_path, QiQiConfig& out_config) {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const auto file_size = file.tellg();
    std::string content;
    if (file_size > 0) {
        content.resize(static_cast<size_t>(file_size));
        file.seekg(0, std::ios::beg);
        file.read(content.data(), file_size);
    }
    out_config = ParseConfigFromUtf8(content);
    return true;
}

bool EnsureDefaultConfigFile(const std::filesystem::path& file_path) {
    try {
        if (std::filesystem::exists(file_path)) {
            return true;
        }
        if (file_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(file_path.parent_path(), ec);
        }
        std::ofstream ofs(file_path, std::ios::binary);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(kDefaultConfigTemplate.data(), static_cast<std::streamsize>(kDefaultConfigTemplate.size()));
        return ofs.good();
    } catch (...) {
        return false;
    }
}

QiQiConfig LoadConfig(void* module_handle) {
    QiQiConfig cfg{};

    // 1. Check user config path: %LOCALAPPDATA%\QiQiIME\config.ini
    const std::filesystem::path user_cfg_path = GetDefaultUserConfigPath();
    if (std::filesystem::exists(user_cfg_path) && LoadConfigFile(user_cfg_path, cfg)) {
        return cfg;
    }

    // 2. Check config.ini next to the DLL / executable
    const std::filesystem::path module_path = log::GetModuleFilePath(module_handle);
    if (!module_path.empty()) {
        const std::filesystem::path dll_cfg_path = module_path.parent_path() / L"config.ini";
        if (std::filesystem::exists(dll_cfg_path)) {
            LoadConfigFile(dll_cfg_path, cfg);
        }
    }

    // 3. Create default user config file for easy future customization
    EnsureDefaultConfigFile(user_cfg_path);
    return cfg;
}

} // namespace qiqi
