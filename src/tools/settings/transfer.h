// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/settings/window.h"
#include "common/dlgutil.h"
#include "common/backup.h"

namespace qiqi::settings_detail {

using BackupRestoreModeChooser =
    std::optional<BackupConflictMode>(*)(HWND, UiLanguage) noexcept;

// Complete ZIP backup/restore follows Dictionary Manager semantics and applies immediately.
HRESULT RestoreSettingsBackup(
    HWND hwnd,
    const std::filesystem::path& path,
    BackupRestoreModeChooser chooser =
        &PromptBackupRestoreConflictMode);
HRESULT ExportSettingsBackup(HWND hwnd, const std::filesystem::path& path);
void PromptSettingsBackupTransfer(
    HWND hwnd, bool importing, const dialog_detail::FileDialogApi& api = {});

} // namespace qiqi::settings_detail
