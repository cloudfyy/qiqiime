// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;

namespace {
constexpr std::array toolbar_commands{
    Reload, NewWord, Copy, Import, Export, Pin, Delete, Promote};
}

ToolCommand DictManagerWindow::Impl::UiCommand(ControlId id) const noexcept {
    ToolIcon icon = ToolIcon::None;
    switch (id) {
    case NewWord: icon = ToolIcon::New; break;
    case Reload: icon = ToolIcon::Reload; break;
    case Copy: icon = ToolIcon::Copy; break;
    case Import: icon = ToolIcon::Import; break;
    case Export: icon = ToolIcon::Save; break;
    case SaveWord: icon = ToolIcon::Save; break;
    case Pin: icon = ToolIcon::Pin; break;
    case Delete: icon = ToolIcon::Delete; break;
    case Promote: icon = ToolIcon::Promote; break;
    default: break;
    }
    return {id == Separator ? 0u : gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), icon, true,
        id >= LanguageAuto && id <= LanguageEn &&
            std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(ui_language)};
}

const wchar_t* DictManagerWindow::Impl::CommandText(ControlId id) const noexcept {
    switch (id) {
    case Reload: return Text(L"刷新", L"Reload");
    case Copy: return Text(L"复制", L"Copy");
    case Import: return Text(L"导入词库...", L"Import...");
    case Export: return Text(L"导出快照...", L"Export...");
    case SelectPrimaryDict: return Text(L"选择自定义主词库...", L"Select Custom Primary Dict...");
    case RestoreDefaultPrimaryDict: return Text(L"恢复默认主词库 (极点 86)", L"Restore Default Primary Dict (Jidian 86)");
    case Backup: return Text(L"备份全部...", L"Backup...");
    case Restore: return Text(L"恢复备份...", L"Restore...");
    case ClearFreq: return Text(L"重置词组排序", L"Reset Phrase Ordering");
    case About: return Text(L"关于...", L"About...");
    case AutoCode: return Text(L"自动取码", L"Auto Code");
    case NewWord: return Text(L"新增词条", L"New Word");
    case CancelEdit: return Text(L"取消编辑\tEsc", L"Cancel Edit\tEsc");
    case SaveWord: return Text(L"保存用户词", L"Save Word");
    case Pin: return Text(L"置顶 / 取消置顶", L"Pin / Unpin");
    case Delete: return Text(L"删除 / 屏蔽 / 恢复", L"Delete / Block / Restore");
    case Promote: return Text(L"词组置前", L"Prioritize Phrase");
    case SelectAll: return Text(L"全选", L"Select All");
    case LanguageAuto: return GetUiStrings(ui_language).dlg_lang_auto;
    case LanguageZh: return GetUiStrings(ui_language).dlg_lang_zh;
    case LanguageEn: return GetUiStrings(ui_language).dlg_lang_en;
    case Exit: return Text(L"退出", L"Exit");
    default: return L"";
    }
}

const wchar_t* DictManagerWindow::Impl::MenuHeading(size_t index) const noexcept {
    switch (index) {
    case 0: return Text(L"文件(&F)", L"&File");
    case 1: return Text(L"编辑(&E)", L"&Edit");
    case 2: return Text(L"语言(&L)", L"&Language");
    default: return Text(L"帮助(&H)", L"&Help");
    }
}

void DictManagerWindow::Impl::UpdateCommands() {
    const std::array file{UiCommand(Reload), UiCommand(Separator), UiCommand(SelectPrimaryDict),
        UiCommand(RestoreDefaultPrimaryDict), UiCommand(Separator), UiCommand(Import), UiCommand(Export),
        UiCommand(Separator), UiCommand(Backup), UiCommand(Restore), UiCommand(Separator), UiCommand(Exit)};
    const std::array edit{UiCommand(NewWord), UiCommand(SaveWord), UiCommand(CancelEdit), UiCommand(AutoCode),
        UiCommand(Separator), UiCommand(Copy), UiCommand(SelectAll), UiCommand(Pin), UiCommand(Delete),
        UiCommand(Promote), UiCommand(Separator), UiCommand(ClearFreq)};
    const std::array language{UiCommand(LanguageAuto), UiCommand(LanguageZh), UiCommand(LanguageEn)};
    const std::array help{UiCommand(About)};
    const std::array menus{
        ToolMenu{MenuHeading(0), L'f', file}, ToolMenu{MenuHeading(1), L'e', edit},
        ToolMenu{MenuHeading(2), L'l', language, true}, ToolMenu{MenuHeading(3), L'h', help}};
    command_ui.SetMenu(hwnd, menus);
    std::array<ToolCommand, toolbar_commands.size()> buttons{};
    std::ranges::transform(toolbar_commands, buttons.begin(), [this](ControlId id) { return UiCommand(id); });
    command_ui.SetToolbar(Child(Toolbar), buttons);
    const std::array save{UiCommand(SaveWord)};
    command_ui.SetToolbar(Child(InlineSave), save);
    UpdateCommandState();
}

bool DictManagerWindow::Impl::CanExecute(ControlId id) const noexcept {
    switch (id) {
    case NewWord: return editor.CanExecute(EditorCommand::NewWord);
    case SaveWord: return editor.CanExecute(EditorCommand::Save);
    case CancelEdit: return editor.CanExecute(EditorCommand::Cancel);
    case AutoCode: return editor.CanExecute(EditorCommand::AutoCode);
    case Reload:
    case SelectPrimaryDict:
    case RestoreDefaultPrimaryDict:
    case Import:
    case Restore:
    case Pin:
    case Delete:
    case Promote:
    case ClearFreq: return editor.CanExecute(EditorCommand::ChangeRecords);
    default: return true;
    }
}

void DictManagerWindow::Impl::UpdateCommandState() noexcept {
    for (const auto id : {Reload, NewWord, SelectPrimaryDict, RestoreDefaultPrimaryDict, Import, Restore, Pin, Delete, Promote, ClearFreq, SaveWord, CancelEdit, AutoCode}) {
        const bool enabled = CanExecute(id);
        ::EnableMenuItem(::GetMenu(hwnd), std::to_underlying(id), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
        ::SendMessageW(Child(Toolbar), TB_ENABLEBUTTON, std::to_underlying(id), MAKELONG(enabled, 0));
    }
    ::SendMessageW(Child(InlineSave), TB_ENABLEBUTTON, std::to_underlying(SaveWord), MAKELONG(CanExecute(SaveWord), 0));
}

} // namespace qiqi::dict_manager
