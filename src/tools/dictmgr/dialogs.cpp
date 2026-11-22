// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"
#include "common/dictui.h"
#include "common/dlgutil.h"

#include <gsl/gsl>
#include <array>
#include <format>
#include <vector>

namespace qiqi::dict_manager {
using enum ControlId;

void DictManagerWindow::Impl::SelectPrimaryDictDialog() {
    std::filesystem::path selected;
    if (!dialog_detail::PromptForPrimaryDictionaryFile(
            hwnd, ui_language, model.ActivePrimaryDictPath().wstring(), selected)) {
        return;
    }
    const auto validation = WubiMappedDict::ValidateAndLoadPrimaryDictionary(selected);
    if (validation && !dictionary_ui::ConfirmPartialDictionary(
            hwnd, ui_language, true, validation.diagnostics)) {
        return;
    }
    if (SUCCEEDED(model.SelectValidatedCustomPrimaryDictionary(selected, validation))) {
        last_message = std::format(
            L"{}: {}",
            Text(L"已切换自定义主词库", L"Switched custom primary dictionary"),
            selected.filename().wstring());
        RebuildRows();
        UpdateStatus();
    } else {
        last_message = Text(
            L"所选主词库无效或无法读取，已保留当前词库",
            L"Selected primary dictionary is invalid or unreadable; kept current dictionary");
        UpdateStatus();
        ::MessageBoxW(
            hwnd,
            GetUiStrings(ui_language).dlg_invalid_primary_dict,
            GetUiStrings(ui_language).dict_manager_name,
            MB_OK | MB_ICONWARNING);
    }
}

void DictManagerWindow::Impl::ExecuteRestoreDefaultPrimaryDict() {
    if (model.RestoreDefaultPrimaryDictionary()) {
        last_message = Text(
            L"已恢复默认主词库（极点五笔 86）",
            L"Restored default primary dictionary (Jidian Wubi 86)");
        RebuildRows();
    } else {
        last_message = Text(
            L"恢复默认主词库失败",
            L"Failed to restore default primary dictionary");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ImportUserDictDialog() {
    std::array<wchar_t, kWin32PathBufferChars> path{};
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"导入用户词库文件 (UTF-8)", L"Import User Dictionary File (UTF-8)"),
            Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Open)) return;
    size_t imported = 0;
    if (model.ImportUserEntriesFromFile(path.data(), imported)) {
        last_message = std::format(L"{}: {}", Text(L"成功导入词条数", L"Imported entries"), imported);
        RebuildRows();
    } else {
        last_message = Text(L"导入失败或无有效词条", L"Import failed or no valid entries");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExportSnapshotDialog() {
    std::array<wchar_t, kWin32PathBufferChars> path{};
    wcscpy_s(path.data(), path.size(), L"qiqi_user_dict_export.txt");
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"导出当前筛选词条 (UTF-8)", L"Export Filtered Entries (UTF-8)"),
            Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Save, L"txt")) return;
    std::vector<DictRecord> snapshot;
    snapshot.reserve(rows.size());
    for (const uint64_t id : rows) {
        if (const auto* rec = model.FindById(id)) {
            snapshot.push_back(*rec);
        }
    }
    if (model.ExportRecordsToFile(path.data(), snapshot)) {
        last_message = std::format(L"{}: {}", Text(L"已导出词条数", L"Exported entries"), snapshot.size());
    } else {
        last_message = Text(L"导出文件失败", L"Failed to export file");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::BackupBundleDialog() {
    std::array<wchar_t, kWin32PathBufferChars> path{};
    wcscpy_s(path.data(), path.size(), L"qiqiime_backup.zip");
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"完整备份配置、词库与词组排序 (.zip)",
                 L"Complete Backup of Config and Dictionaries (.zip)"),
            Text(L"奇奇五笔完整备份 (*.zip)\0*.zip\0所有文件\0*.*\0",
                L"QiQi Complete Backup (*.zip)\0*.zip\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Save, L"zip")) return;
    const HRESULT result = model.CreateBackupBundleResult(path.data());
    if (SUCCEEDED(result)) {
        last_message = Text(
            L"已导出完整 ZIP 备份", L"Exported complete ZIP backup");
    } else {
        last_message = std::format(L"{} (0x{:08X})",
            Text(L"创建 ZIP 备份失败", L"Failed to create ZIP backup"),
            gsl::narrow_cast<uint32_t>(result));
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::RestoreBundleDialog() {
    std::array<wchar_t, kWin32PathBufferChars> path{};
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"选择要恢复的奇奇五笔 ZIP 备份",
                 L"Select QiQi ZIP Backup to Restore"),
            Text(L"奇奇五笔完整备份 (*.zip)\0*.zip\0所有文件\0*.*\0",
                L"QiQi Complete Backup (*.zip)\0*.zip\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Open)) return;

    const auto mode = PromptBackupRestoreConflictMode(hwnd, ui_language);
    if (!mode) return;
    const auto result = model.RestoreBackupBundle(path.data(), *mode);
    last_message = FormatBackupRestoreMessage(result, ui_language);
    if (result.stage != BackupRestoreStage::Read && result.stage != BackupRestoreStage::Validate) {
        RebuildRows();
    }
    UpdateStatus();
}

} // namespace qiqi::dict_manager
