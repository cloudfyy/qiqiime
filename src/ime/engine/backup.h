// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/backup.h"
#include "ime/engine/engine.h"

namespace qiqi {

class CompleteBackup final {
public:
    CompleteBackup(
        const std::filesystem::path& module_dir,
        const std::filesystem::path& user_dict_path = {},
        const std::filesystem::path& phrase_freq_path = {});

    HRESULT CreateFileResult(const std::filesystem::path& path);
    BackupRestoreResult RestoreFile(
        const std::filesystem::path& path, BackupConflictMode mode);

private:
    HRESULT ReloadStateResult();

    WubiEngine engine_;
    std::filesystem::path module_dir_;
    std::filesystem::path user_dict_path_;
    std::filesystem::path phrase_freq_path_;
    std::filesystem::path active_primary_path_;
    PrimaryDict active_primary_ = PrimaryDict::Jidian;
    bool publish_user_dict_directory_ = false;
};

} // namespace qiqi
