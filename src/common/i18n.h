// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <cwchar>
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
    const wchar_t* menu_open_help;
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
    const wchar_t* dlg_grp_jidian;
    const wchar_t* dlg_radio_jidian;
    const wchar_t* dlg_radio_haifeng;
    const wchar_t* dlg_radio_builtin;
    const wchar_t* dlg_radio_custom;
    const wchar_t* dlg_btn_browse_dict;
    const wchar_t* dlg_lbl_custom_dict_none;
    const wchar_t* dlg_open_primary_dict_title;
    const wchar_t* dlg_open_primary_dict_filter;
    const wchar_t* dlg_invalid_primary_dict;
    const wchar_t* dlg_chk_extra;
    const wchar_t* dlg_chk_auto_commit4;
    const wchar_t* dlg_chk_push_commit5;
    const wchar_t* dlg_chk_quick_select;
    const wchar_t* dlg_chk_code_hint;
    const wchar_t* dlg_chk_dynamic_freq;
    const wchar_t* dlg_chk_shift_toggle;
    const wchar_t* dlg_lbl_english_switch;
    const wchar_t* dlg_english_switch_commit;
    const wchar_t* dlg_english_switch_clear;
    const wchar_t* dlg_chk_up_down_page_turn;
    const wchar_t* dlg_chk_status_bar;
    const wchar_t* dlg_chk_beep_empty4;
    const wchar_t* dlg_chk_beep_dup4;
    const wchar_t* dlg_chk_clear_empty4;
    const wchar_t* dlg_chk_key_sound;
    const wchar_t* dlg_lbl_page_size;
    const wchar_t* dlg_candidate_horizontal;
    const wchar_t* dlg_candidate_vertical;
    const wchar_t* dlg_lbl_ui_lang;
    const wchar_t* dlg_lang_auto;
    const wchar_t* dlg_lang_zh;
    const wchar_t* dlg_lang_en;
    const wchar_t* dlg_lbl_underline_style;
    const wchar_t* dlg_underline_dot;
    const wchar_t* dlg_underline_solid;
    const wchar_t* dlg_underline_dash;
    const wchar_t* dlg_underline_squiggle;
    const wchar_t* dlg_underline_none;
    const wchar_t* dlg_chk_bold_underline;
    const wchar_t* dlg_btn_about;
    const wchar_t* dlg_btn_dict_mgr;
    const wchar_t* dlg_btn_help;
    const wchar_t* dlg_btn_restore_defaults;
    const wchar_t* dlg_btn_ok;
    const wchar_t* dlg_btn_cancel;
    const wchar_t* dlg_save_failed;
    const wchar_t* dlg_notify_failed;
    const wchar_t* dlg_dict_manager_launch_failed;
    const wchar_t* quick_settings_save_failed;
    const wchar_t* quick_settings_notify_failed;
    const wchar_t* dlg_btn_yes;
    const wchar_t* dlg_btn_no;

    // About & Help dialogs
    const wchar_t* about_title;
    const wchar_t* about_app_name;
    const wchar_t* about_version_prefix;
    const wchar_t* about_license_info;
    const wchar_t* about_btn_github;
    const wchar_t* help_title;
    const wchar_t* help_header_title;
    const wchar_t* help_header_subtitle;
    const wchar_t* help_shortcuts_info;
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
    L"快捷键与帮助...",
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
    L"极点五笔 86 词库",
    L"极点五笔 86 核心词库",
    L"海峰五笔 86 超大字符集词库 (含生僻字)",
    L"仅使用内置精简保底词库",
    L"自定义五笔 86 主词库",
    L"浏览...",
    L"尚未选择自定义主词库文件",
    L"选择五笔 86 主词库文件 (UTF-8 / .qdict)",
    L"五笔词库文件 (*.txt;*.yaml;*.yml;*.qdict)\0*.txt;*.yaml;*.yml;*.qdict\0所有文件 (*.*)\0*.*\0",
    L"所选主词库不存在、无法读取、不是有效 UTF-8 文本、二进制结构损坏或没有有效词条。请选择 UTF-8 码表（.txt / .yaml）或有效的 .qdict 文件。格式校验不保证码表属于五笔 86。当前已保留原有词库配置。",
    L"启用极点现代扩展词库 (Extra，需先选择上方核心词库)",
    L"四码唯一自动上屏",
    L"第五码自动顶字上屏",
    L"分号(;)与单引号(')选二、三重码",
    L"显示后续编码提示 (~code)",
    L"启用词组动态调频 (单字不调频)",
    L"允许单按 Shift 键切换中英文",
    L"切换英文时:",
    L"英文上屏",
    L"清除输入文字",
    L"使用 ↑ / ↓ 方向键翻页",
    L"显示桌面浮动状态条",
    L"四码空码时发出提示音",
    L"四码重码时发出提示音",
    L"空码时自动清空输入",
    L"启用打字按键音效",
    L"每页候选 / 排列:",
    L"横排",
    L"竖排",
    L"界面语言 (Language):",
    L"跟随系统 (Auto)",
    L"简体中文 (zh-CN)",
    L"English (en-US)",
    L"编码下划线样式:",
    L"点状虚线",
    L"实线",
    L"短横虚线",
    L"波浪线",
    L"无下划线",
    L"加粗编码下划线",
    L"关于...",
    L"词库与词频管理...",
    L"快捷键说明...",
    L"恢复默认值",
    L"确定",
    L"取消",
    L"保存设置失败，请重试。",
    L"设置已保存，切换窗口后全面生效。",
    L"无法启动词库管理器。请确认 QiQiIMEDictManager.exe 已安装，且当前用户有权运行该程序。具体错误码已记录到日志。",
    L"保存设置失败，请重试。",
    L"设置已保存，切换窗口后全面生效。",
    L"是",
    L"否",

    L"关于 奇奇五笔",
    L"奇奇五笔 (QiQi Wubi IME)",
    L"版本: v",
    L"开源许可协议 (Licenses):\n"
    L"• 奇奇五笔 (QiQiIME) 主程序 (cloudfyy): GPL-3.0-or-later\n"
    L"• 极点五笔 86 词库 (KyleBing): Apache License 2.0\n"
    L"• 海峰五笔 86 词库 (孙海峰): BSD 3-Clause License",
    L"访问 GitHub",
    L"快捷键与使用帮助",
    L"奇奇五笔 快捷键与操作指南",
    L"常用输入、选词、调序与快捷造词热键一览",
    L"【模式切换】\n"
    L"• 单按 Shift / Ctrl + Space：切换中 / 英文输入模式\n"
    L"• Shift + Space：切换全角 / 半角字符模式\n"
    L"• Ctrl + .（句号）：切换中文 / 英文标点符号\n\n"
    L"【选词与翻页】\n"
    L"• Space（空格）/ 数字 1–9（含小键盘）：首选上屏 / 按序号选词\n"
    L"• ;（分号）/ '（单引号）：选第 2 / 第 3 候选词（可在设置中开关）\n"
    L"• - = / PageUp PageDown / ↑ ↓：候选词上翻页 / 下翻页\n\n"
    L"【特色输入】\n"
    L"• 首码 z + 英文或符号：临时英文输入模式（回车或空格上屏）\n"
    L"• 第 2–4 码输入 z：万能键通配查询并提示编码\n"
    L"• 编码中按 CapsLock：已输编码转英文上屏并切换大写锁定\n\n"
    L"【词库与调序】\n"
    L"• Ctrl + =（等号）：剪贴板快捷造词（复制 2–16 字词组后按键入库）\n"
    L"• Ctrl + 数字 1–9：组合态下将对应候选词置顶 / 取消置顶\n"
    L"• Ctrl + Shift + 数字 1–9（或 Ctrl + Delete）：删除用户词 / 屏蔽系统词"
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
    L"Shortcuts & Help...",
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
    L"Jidian Wubi 86 Dictionary",
    L"Jidian Wubi 86 Core Dictionary",
    L"Haifeng Wubi 86 Extended CJK Dictionary",
    L"Built-in Minimal Fallback Dictionary Only",
    L"Custom Wubi 86 Primary Dictionary",
    L"Browse...",
    L"No custom primary dictionary selected",
    L"Select Wubi 86 Primary Dictionary File (UTF-8 / .qdict)",
    L"Wubi Dictionary (*.txt;*.yaml;*.yml;*.qdict)\0*.txt;*.yaml;*.yml;*.qdict\0All Files (*.*)\0*.*\0",
    L"The primary dictionary is missing, unreadable, not valid UTF-8, a damaged binary file, or contains no valid entries. Select a UTF-8 table (.txt / .yaml) or valid .qdict file. Format validation does not verify Wubi 86 semantics. The previous dictionary configuration has been retained.",
    L"Enable Jidian Extra Dictionary (requires Jidian Core)",
    L"Auto-commit unique 4-code word",
    L"Push-commit on 5th code key",
    L"Select 2nd/3rd candidate with ; and '",
    L"Show progressive code hints (~code)",
    L"Dynamic phrase frequency (single chars fixed)",
    L"Toggle CN/EN mode with Shift key",
    L"Switch to English:",
    L"Commit raw text",
    L"Clear input text",
    L"Turn candidate pages with Up/Down keys",
    L"Show desktop floating status bar",
    L"Beep on empty 4-letter code",
    L"Chime on duplicate 4-letter code",
    L"Auto-clear input when no code matches",
    L"Enable typing key sound effects",
    L"Page size / layout:",
    L"Horizontal",
    L"Vertical",
    L"UI Language:",
    L"System Default (Auto)",
    L"Simplified Chinese (zh-CN)",
    L"English (en-US)",
    L"Underline style:",
    L"Dotted",
    L"Solid",
    L"Dashed",
    L"Squiggle",
    L"None",
    L"Bold composition underline",
    L"About...",
    L"Dictionary Manager...",
    L"Shortcuts...",
    L"Restore defaults",
    L"OK",
    L"Cancel",
    L"Settings could not be saved. Your edits remain in this window. Please retry.",
    L"Settings were saved, but the running IME could not be notified. Reactivate the IME to load the new settings.",
    L"Cannot start Dictionary Manager. Check that QiQiIMEDictManager.exe is installed and the current user can run it. The error code was recorded in the log.",
    L"Could not save setting. The previous setting is unchanged.",
    L"Setting saved, but the running IME could not be notified.",
    L"Yes",
    L"No",

    L"About QiQi Wubi IME",
    L"QiQi Wubi IME",
    L"Version: v",
    L"Open Source Licenses:\n"
    L"• QiQi Wubi IME Core (cloudfyy): GPL-3.0-or-later\n"
    L"• Jidian Wubi 86 Dict (KyleBing): Apache License 2.0\n"
    L"• Haifeng Wubi 86 Dict (SUN Haifeng): BSD 3-Clause License",
    L"Visit GitHub",
    L"Keyboard Shortcuts & Help",
    L"QiQi Wubi Shortcuts & Guide",
    L"Quick reference for mode switching, selection, and dictionary hotkeys",
    L"[Mode Switching]\n"
    L"• Tap Shift / Ctrl + Space: Toggle Chinese / English mode\n"
    L"• Shift + Space: Toggle Full-Width / Half-Width characters\n"
    L"• Ctrl + . (Period): Toggle Chinese / English punctuation\n\n"
    L"[Candidate Selection & Paging]\n"
    L"• Space / Digits 1–9 (incl. Numpad): Commit 1st / Nth candidate\n"
    L"• ; (Semicolon) / ' (Apostrophe): Select 2nd / 3rd candidate (optional)\n"
    L"• - = / PageUp PageDown / Up Down: Previous / Next candidate page\n\n"
    L"[Special Input]\n"
    L"• Leading 'z' + ASCII text: Temporary English mode (Enter/Space commits)\n"
    L"• 'z' in 2nd–4th code position: Wildcard code query with code hints\n"
    L"• CapsLock while composing: Commit raw code and toggle CapsLock\n\n"
    L"[Dictionary & Ordering]\n"
    L"• Ctrl + = (Equals): Add copied clipboard phrase (2–16 chars) to user dict\n"
    L"• Ctrl + Digit 1–9: Pin / Unpin candidate at that index\n"
    L"• Ctrl + Shift + Digit 1–9 (or Ctrl + Delete): Delete / Block candidate"
};

inline UiLanguage DetectSystemPreferredUiLanguage() noexcept {
    ULONG num_langs = 0;
    ULONG cch_buf = 0;
    std::array<wchar_t, 128> buf{};
    cch_buf = gsl::narrow_cast<ULONG>(buf.size());
    if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_langs, buf.data(), &cch_buf) &&
        num_langs > 0 && gsl::at(buf, 0) != L'\0') {
        if (_wcsnicmp(buf.data(), L"zh", 2) == 0) {
            return UiLanguage::ZhCn;
        }
        return UiLanguage::EnUs;
    }
    const LANGID lang_id = ::GetUserDefaultUILanguage();
    return (PRIMARYLANGID(lang_id) == LANG_CHINESE) ? UiLanguage::ZhCn : UiLanguage::EnUs;
}

inline UiLanguage ResolveUiLanguage(UiLanguage configured) noexcept {
    if (configured == UiLanguage::ZhCn || configured == UiLanguage::EnUs) {
        return configured;
    }
    return DetectSystemPreferredUiLanguage();
}

inline const UiStrings& GetUiStrings(UiLanguage configured) noexcept {
    return (ResolveUiLanguage(configured) == UiLanguage::EnUs) ? kEnUsStrings : kZhCnStrings;
}

} // namespace qiqi
