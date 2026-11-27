// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"
#include "common/dialog.h"

#include <gsl/gsl>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;

void DictManagerWindow::Impl::Command(int id, int notification) {
    if (!CanExecute(gsl::narrow_cast<ControlId>(id))) {
        if (editor.Active()) {
            last_message = Text(L"请先保存当前编辑，或按 Esc 取消。", L"Save the current edit or press Esc to cancel first.");
            UpdateStatus();
        }
        return;
    }
    switch (gsl::narrow_cast<ControlId>(id)) {
    case NewWord:
        BeginNew();
        break;
    case CancelEdit:
        CancelEditing();
        break;
    case Reload:
        model.ReloadAll();
        last_message = Text(L"已重新加载词库与词组排序", L"Reloaded dictionaries and phrase ordering");
        RebuildRows();
        UpdateStatus();
        break;
    case Copy:
        CopySelection();
        break;
    case SelectAll:
        SelectAllRows();
        break;
    case Exit:
        ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    case SelectPrimaryDict:
        SelectPrimaryDictDialog();
        break;
    case RestoreDefaultPrimaryDict:
        ExecuteRestoreDefaultPrimaryDict();
        break;
    case Import:
        ImportUserDictDialog();
        break;
    case Export:
        ExportSnapshotDialog();
        break;
    case Backup:
        BackupBundleDialog();
        break;
    case Restore:
        RestoreBundleDialog();
        break;
    case ClearFreq:
        ExecuteClearFrequencies();
        break;
    case About:
        std::ignore = ShowAboutDialog(instance, hwnd, ui_language, true);
        break;
    case SourceCombo:
        if (editor.Active() && notification == CBN_SETFOCUS) CancelEditing(false);
        if (!editor.Active() && !updating_filters && notification == CBN_SELCHANGE) {
            Refilter();
        }
        break;
    case SearchEdit:
        if (editor.Active() && notification == EN_SETFOCUS) CancelEditing(false);
        if (!editor.Active() && !updating_filters && notification == EN_CHANGE) {
            Refilter();
        }
        break;
    case EditWordInput:
        if (editor.Active() && !updating_editor && notification == EN_CHANGE) {
            if (editor.WantsAutomaticCode()) AutoFillCodeFromWord();
            else UpdateEditorState();
        }
        break;
    case EditCodeInput:
        if (editor.Active() && !updating_editor && notification == EN_CHANGE) {
            UpdateEditorState(CodeChange::Manual);
        }
        break;
    case AutoCode:
        AutoFillCodeFromWord();
        break;
    case SaveWord:
        ExecuteAddOrUpdate();
        break;
    case Pin:
        ExecutePinSelected();
        break;
    case Delete:
        ExecuteDeleteSelected();
        break;
    case Promote:
        ExecutePromoteSelected();
        break;
    case LanguageAuto:
    case LanguageZh:
    case LanguageEn:
    case LanguageFollowIme:
        if (id == std::to_underlying(LanguageFollowIme)) language_override.reset();
        else language_override = gsl::narrow_cast<UiLanguage>(id - std::to_underlying(LanguageAuto));
        RefreshDisplay(LoadConfig(), true);
        break;
    default:
        break;
    }
}

} // namespace qiqi::dict_manager
