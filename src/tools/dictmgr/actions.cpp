// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"
#include "common/uiutil.h"

#include <gsl/gsl>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace qiqi::dict_manager {
using enum ControlId;

void DictManagerWindow::Impl::AutoFillCodeFromWord() {
    if (!editor.CanExecute(EditorCommand::AutoCode)) return;
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    const std::wstring auto_code = model.ComputeAutoCode(word);
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditCodeInput), auto_code.c_str());
    UpdateEditorState();
}

void DictManagerWindow::Impl::ExecuteAddOrUpdate() {
    if (!editor.CanExecute(EditorCommand::Save) || ime_composing) return;
    UpdateEditorState();
    if (editor.PrepareSave(model.Records()) == EditorSaveAction::Unchanged) {
        CancelEditing();
        last_message = Text(L"内容未修改，无需保存。", L"No changes to save.");
        UpdateStatus();
        return;
    }
    if (editor.Code().empty() && !editor.Word().empty() && editor.WantsAutomaticCode()) {
        AutoFillCodeFromWord();
    }
    if (model.PollExternalChanges()) RebuildRows();
    const auto action = editor.PrepareSave(model.Records());
    if (action == EditorSaveAction::MissingOriginal) {
        last_message = Text(L"原词条已被其他进程修改或删除；草稿已保留，请取消后重新编辑。",
            L"The original entry changed or was removed externally; draft preserved. Cancel and edit again.");
        UpdateStatus();
        return;
    }
    HRESULT save_hr = E_UNEXPECTED;
    if (action == EditorSaveAction::ReplaceUser) {
        const auto* original = editor.Original();
        save_hr = model.ReplaceUserEntryResult(original->code, original->word, editor.Code(), editor.Word());
    } else {
        save_hr = model.AddOrUpdateUserEntryResult(editor.Code(), editor.Word());
    }
    const bool ok = log::CheckHResult(save_hr, "DictManager::SaveWord");
    editor.CompleteSave(ok);
    if (!ok) {
        const bool permission_denied =
            save_hr == E_ACCESSDENIED || save_hr == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD) ||
            save_hr == HRESULT_FROM_WIN32(ERROR_WRITE_PROTECT);
        last_message = std::format(L"{} (HRESULT=0x{:08X})",
            permission_denied
                ? Text(L"保存失败：访问被拒绝或写保护，草稿已保留。请检查词库及同步目录权限、只读属性和文件占用，或改用当前用户可写的词库目录。",
                       L"Save failed: access denied or write-protected. Draft preserved. Check dictionary/sync permissions, read-only attributes and file locks, or use a writable user directory.")
                : Text(L"保存失败，草稿已保留；请检查编码、词条及外部更改。",
                       L"Save failed; draft preserved. Check code, word and external changes."),
            gsl::narrow_cast<unsigned int>(save_hr));
    } else {
        editing_row = -1;
        last_message = Text(L"已保存用户词条并同步", L"Saved user entry and synchronized");
        RebuildRows();
        UpdateCommandState();
        ::SetFocus(Child(List));
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePinSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    const bool pinned = record->source == DictEntrySource::Pinned;
    if (pinned ? model.UnpinUserEntry(record->code, record->word) : model.PinUserEntry(record->code, record->word)) {
        last_message = pinned ? Text(L"已取消固定置顶并同步", L"Unpinned entry and synchronized")
            : Text(L"已固定置顶并同步", L"Pinned entry to top and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"固定置顶失败（请检查五笔编码与词条）", L"Failed to pin entry");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteDeleteSelected() {
    const auto selected = CollectSelectedRecords();
    if (selected.empty()) return;
    size_t changed_count = 0;
    for (const auto& rec : selected) {
        switch (rec.source) {
        case DictEntrySource::UserDict:
            if (model.RemoveUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Pinned:
            if (model.UnpinUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Blocked:
            if (model.UnblockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PhraseFreq:
            if (model.RemovePhraseFrequency(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PrimaryDict:
        case DictEntrySource::ExtraDict:
            if (model.BlockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        default:
            break;
        }
    }
    if (changed_count > 0) {
        last_message = std::format(
            L"{}: {}",
            Text(L"已处理（删除/屏蔽/恢复）记录数", L"Processed (deleted/blocked/restored) records"),
            changed_count);
        RebuildRows();
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePromoteSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    if (model.PromotePhraseFrequency(record->code, record->word)) {
        last_message = Text(L"已将词组置前并同步", L"Prioritized phrase and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"词组置前仅支持双字及以上词组", L"Prioritizing requires a multi-character phrase");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteClearFrequencies() {
    if (model.ClearAllPhraseFrequencies()) {
        last_message = Text(L"已重置全部词组排序", L"Reset all phrase ordering");
        RebuildRows();
    } else {
        last_message = Text(L"重置词组排序失败", L"Failed to reset phrase ordering");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::CopySelection() {
    const auto items = CollectSelectedRecords();
    if (items.empty()) return;
    const auto text = FormatRecordsForClipboard(items);
    const ULONG error = CopyTextToClipboard(hwnd, text);
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"复制到剪贴板失败", L"Copy to clipboard failed"), error);
    } else {
        last_message = std::format(L"{}: {}", Text(L"已复制词条数", L"Copied entries"), items.size());
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::ShowListContextMenu(POINT screen_pt) {
    screen_pt = ResolveListViewContextMenuPoint(Child(List), screen_pt, theme);
    const auto selected = CollectSelectedRecords();
    const bool has_sel = !selected.empty();
    std::vector<ToolCommand> commands;
    for (const auto id : {Copy, SelectAll, Pin, Promote, Delete}) {
        const bool enabled = (id == SelectAll ? !rows.empty() : has_sel) && CanExecute(id);
        commands.push_back({gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), ToolIcon::None, enabled});
    }
    const UINT cmd = command_ui.TrackContextMenu(hwnd, screen_pt, commands, theme);
    if (cmd) Command(cmd, 0);
}

} // namespace qiqi::dict_manager
