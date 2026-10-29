#pragma once
#include <windows.h>
#include "common/config.h"

namespace qiqi {

struct UiStrings {
    // Candidate popup
    const wchar_t* candidate_header_prefix;
    const wchar_t* candidate_empty;

    // Taskbar LangBar tray button & context menu
    const wchar_t* langbar_desc;
    const wchar_t* langbar_tooltip;
    const wchar_t* menu_toggle_chinese;
    const wchar_t* menu_toggle_full_width;
    const wchar_t* menu_toggle_punct;
    const wchar_t* menu_dict_jidian;
    const wchar_t* menu_dict_haifeng;
    const wchar_t* menu_toggle_extra_dict;
    const wchar_t* menu_toggle_status_bar;
    const wchar_t* menu_toggle_ui_lang;
    const wchar_t* menu_open_settings;
    const wchar_t* menu_open_config_file;
    const wchar_t* menu_open_about;

    // Floating desktop status bar
    const wchar_t* status_logo;
    const wchar_t* status_mode_cn;
    const wchar_t* status_mode_en;
    const wchar_t* status_width_full;
    const wchar_t* status_width_half;
    const wchar_t* status_settings;
    const wchar_t* tip_grip;
    const wchar_t* tip_mode;
    const wchar_t* tip_width;
    const wchar_t* tip_punct;
    const wchar_t* tip_settings;

    // Settings dialog & ITfFnConfigure
    const wchar_t* configure_display_name;
    const wchar_t* dlg_title;
    const wchar_t* dlg_radio_jidian;
    const wchar_t* dlg_radio_haifeng;
    const wchar_t* dlg_radio_builtin;
    const wchar_t* dlg_chk_extra;
    const wchar_t* dlg_chk_auto_commit4;
    const wchar_t* dlg_chk_push_commit5;
    const wchar_t* dlg_chk_quick_select;
    const wchar_t* dlg_chk_code_hint;
    const wchar_t* dlg_chk_dynamic_freq;
    const wchar_t* dlg_chk_shift_toggle;
    const wchar_t* dlg_chk_status_bar;
    const wchar_t* dlg_chk_beep_empty4;
    const wchar_t* dlg_chk_beep_dup4;
    const wchar_t* dlg_chk_clear_empty4;
    const wchar_t* dlg_chk_key_sound;
    const wchar_t* dlg_lbl_page_size;
    const wchar_t* dlg_lbl_ui_lang;
    const wchar_t* dlg_lang_auto;
    const wchar_t* dlg_lang_zh;
    const wchar_t* dlg_lang_en;
    const wchar_t* dlg_btn_open_ini;
    const wchar_t* dlg_btn_about;
    const wchar_t* dlg_btn_ok;
    const wchar_t* dlg_btn_cancel;

    // About dialog
    const wchar_t* about_title;
    const wchar_t* about_app_name;
    const wchar_t* about_version_prefix;
    const wchar_t* about_license_info;
    const wchar_t* about_btn_github;
};

inline constexpr UiStrings kZhCnStrings = {
    L"五笔 [",
    L"(无候选)",

    L"QiQi 五笔状态与菜单",
    L"QiQi 五笔 (左键切换中英，右键打开菜单)",
    L"中文输入模式 (Shift / Ctrl+Space)",
    L"全角字符模式 (Shift+Space)",
    L"中文标点符号 (Ctrl+.)",
    L"词库：极点五笔 86",
    L"词库：海峰五笔 86 (超大字符集)",
    L"启用极点现代扩展词库",
    L"显示桌面浮动状态条",
    L"Switch to English UI (英文界面)",
    L"输入法属性设置...",
    L"编辑配置文件 (config.ini)",
    L"关于奇奇五笔...",

    L"五笔",
    L"中",
    L"英",
    L"全",
    L"半",
    L"设",
    L"按住拖动状态栏，右键打开菜单",
    L"切换中/英文模式 (Shift / Ctrl+Space)",
    L"切换全/半角模式 (Shift+Space)",
    L"切换中/英文标点 (Ctrl+.)",
    L"打开输入法属性设置",

    L"QiQi 五笔属性设置",
    L"QiQi 五笔输入法 - 属性设置",
    L"极点五笔 86 核心词库 (推荐)",
    L"海峰五笔 86 超大字符集词库 (含生僻字)",
    L"仅使用内置精简保底词库",
    L"启用极点现代扩展词库 (Extra)",
    L"四码唯一自动上屏",
    L"第五码自动顶字上屏",
    L"分号(;)与单引号(')选二、三重码",
    L"显示后续编码提示 (~code)",
    L"启用词组动态调频 (单字不调频)",
    L"允许单按 Shift 键切换中英文",
    L"显示桌面浮动状态条",
    L"四码空码时发出提示音",
    L"四码重码时发出提示音",
    L"空码时自动清空输入",
    L"启用打字按键音效",
    L"每页候选词数量:",
    L"界面语言 (Language):",
    L"跟随系统 (Auto)",
    L"简体中文 (zh-CN)",
    L"English (en-US)",
    L"编辑 config.ini",
    L"关于...",
    L"确定",
    L"取消",

    L"关于 奇奇五笔",
    L"奇奇五笔 (QiQi Wubi IME)",
    L"版本: v",
    L"开源许可协议 (Licenses):\n"
    L"• 奇奇五笔 (QiQiIME) 主程序 (cloudfyy): GPL License\n"
    L"• 极点五笔 86 词库 (KyleBing): Apache License 2.0\n"
    L"• 海峰五笔 86 词库 (孙海峰): BSD 3-Clause License",
    L"访问 GitHub"
};

inline constexpr UiStrings kEnUsStrings = {
    L"Wubi [",
    L"(No candidates)",

    L"QiQi Wubi Status & Menu",
    L"QiQi Wubi (Left-click: CN/EN, Right-click: Menu)",
    L"Chinese Input Mode (Shift / Ctrl+Space)",
    L"Full-Width Character Mode (Shift+Space)",
    L"Chinese Punctuation (Ctrl+.)",
    L"Dictionary: Jidian Wubi 86",
    L"Dictionary: Haifeng Wubi 86 (Extended CJK)",
    L"Enable Jidian Extra Dictionary",
    L"Show Floating Status Bar",
    L"切换中文界面 (Chinese UI)",
    L"IME Settings...",
    L"Edit Config File (config.ini)",
    L"About QiQi Wubi...",

    L"Wubi",
    L"CN",
    L"EN",
    L"FW",
    L"HW",
    L"Cfg",
    L"Drag to move status bar, right-click for menu",
    L"Toggle Chinese/English Mode (Shift / Ctrl+Space)",
    L"Toggle Full/Half Width (Shift+Space)",
    L"Toggle Chinese/English Punctuation (Ctrl+.)",
    L"Open IME Settings",

    L"QiQi Wubi Settings",
    L"QiQi Wubi IME - Settings",
    L"Jidian Wubi 86 Core Dictionary (Recommended)",
    L"Haifeng Wubi 86 Extended CJK Dictionary",
    L"Built-in Minimal Fallback Dictionary Only",
    L"Enable Jidian Extra Dictionary",
    L"Auto-commit unique 4-code word",
    L"Push-commit on 5th code key",
    L"Select 2nd/3rd candidate with ; and '",
    L"Show progressive code hints (~code)",
    L"Dynamic phrase frequency (single chars fixed)",
    L"Toggle CN/EN mode with Shift key",
    L"Show desktop floating status bar",
    L"Beep on empty 4-letter code",
    L"Chime on duplicate 4-letter code",
    L"Auto-clear input when no code matches",
    L"Enable typing key sound effects",
    L"Candidates per page:",
    L"UI Language:",
    L"System Default (Auto)",
    L"Simplified Chinese (zh-CN)",
    L"English (en-US)",
    L"Edit config.ini",
    L"About...",
    L"OK",
    L"Cancel",

    L"About 奇奇五笔 (QiQi Wubi)",
    L"奇奇五笔 (QiQi Wubi IME)",
    L"Version: v",
    L"Open Source Licenses:\n"
    L"• 奇奇五笔 (QiQiIME) Core (cloudfyy): GPL License\n"
    L"• Jidian Wubi 86 Dict (KyleBing): Apache License 2.0\n"
    L"• Haifeng Wubi 86 Dict (SUN Haifeng): BSD 3-Clause License",
    L"Visit GitHub"
};

inline UiLanguage ResolveUiLanguage(UiLanguage configured) noexcept {
    if (configured == UiLanguage::ZhCn || configured == UiLanguage::EnUs) {
        return configured;
    }
    const LANGID lang_id = ::GetUserDefaultUILanguage();
    return (PRIMARYLANGID(lang_id) == LANG_CHINESE) ? UiLanguage::ZhCn : UiLanguage::EnUs;
}

inline const UiStrings& GetUiStrings(UiLanguage configured) noexcept {
    return (ResolveUiLanguage(configured) == UiLanguage::EnUs) ? kEnUsStrings : kZhCnStrings;
}

} // namespace qiqi
