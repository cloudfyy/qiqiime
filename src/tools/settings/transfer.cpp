// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/transfer.h"
#include "tools/settings/internal.h"

#include "common/dlgutil.h"
#include "common/configclient.h"
#include "common/i18n.h"
#include "ime/engine/backup.h"

#include <format>
#include <new>

namespace qiqi::settings_detail {
namespace {

DialogContext* Context(HWND hwnd) noexcept {
    if (!::IsWindow(hwnd)) return nullptr;
    return reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

class TransferScope final {
public:
    explicit TransferScope(HWND hwnd) noexcept : hwnd_(hwnd), context_(Context(hwnd)) {
        if (!context_ || !context_->config || context_->done || context_->transfer_in_progress) {
            QIQI_LOG_WARN("Settings backup transfer requires an idle bound settings window");
            context_ = nullptr;
        } else {
            context_->transfer_in_progress = true;
        }
    }
    TransferScope(const TransferScope&) = delete;
    TransferScope& operator=(const TransferScope&) = delete;
    TransferScope(TransferScope&&) = delete;
    TransferScope& operator=(TransferScope&&) = delete;
    ~TransferScope() {
        if (context_ && Context(hwnd_) == context_) {
            context_->transfer_in_progress = false;
            if (!::PostMessageW(hwnd_, kSettingsExternalConfigChangedMessage, 0, 0)) {
                QIQI_LOG_WARN("Settings post-backup sync failed (error={})", ::GetLastError());
            }
        }
    }
    DialogContext* Get() const noexcept { return context_; }

private:
    HWND hwnd_;
    DialogContext* context_;
};

void ShowBackupExportResult(HWND hwnd, UiLanguage language, HRESULT result) {
    const bool success = SUCCEEDED(result);
    const std::wstring message = success
        ? std::wstring(LocalizedUiText(
            language, L"已导出完整 ZIP 备份。", L"Complete ZIP backup exported."))
        : std::format(L"{}\r\nHRESULT: 0x{:08X}",
            LocalizedUiText(
                language, L"创建完整 ZIP 备份失败。", L"Failed to create complete ZIP backup."),
            static_cast<unsigned long>(result));
    if (!::MessageBoxW(
            hwnd, message.c_str(), GetUiStrings(language).dlg_title,
            MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR))) {
        QIQI_LOG_ERROR("Backup export result dialog failed (error={})", ::GetLastError());
    }
}

void ShowBackupRestoreResult(
    HWND hwnd, UiLanguage language, const BackupRestoreResult& result) {
    const auto message = FormatBackupRestoreMessage(result, language);
    const UINT icon = result.status == BackupRestoreStatus::Success
        ? MB_ICONINFORMATION
        : MB_ICONWARNING;
    if (!::MessageBoxW(
            hwnd, message.c_str(), GetUiStrings(language).dlg_title, MB_OK | icon)) {
        QIQI_LOG_ERROR("Backup restore result dialog failed (error={})", ::GetLastError());
    }
}

HRESULT RefreshSettingsAfterRestore(HWND hwnd, DialogContext& context) {
    QiQiConfig restored;
    config::Snapshot snapshot;
    const HRESULT read_result =
        HasConfigRegistrySubKeyOverride()
            ? (LoadConfigFromRegistry(
                   snapshot.value,
                   &snapshot.revision)
                   ? S_OK
                   : E_FAIL)
            : config::CopyCachedSnapshot(
                  snapshot);
    if (FAILED(read_result)) {
        QIQI_LOG_WARN(
            "Restored backup configuration "
            "could not be reloaded "
            "(hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                read_result));
        return read_result;
    }
    if (read_result != S_OK ||
        snapshot.revision == 0) {
        QIQI_LOG_WARN("Restored backup configuration could not be reloaded");
        return E_UNEXPECTED;
    }
    restored = std::move(snapshot.value);
    const uint64_t sequence =
        snapshot.revision;
    try {
        QiQiConfig bound = restored;
        QiQiConfig baseline = restored;
        baseline.enable_jidian_extra =
            restored.primary_dict == PrimaryDict::Jidian && restored.enable_jidian_extra;
        if (restored.easy_reading) {
            baseline.ui_text_scale =
                std::max(restored.ui_text_scale, UiTextScale::Percent150);
        }
        const HRESULT render = RenderSettingsDraft(hwnd, context, restored);
        if (FAILED(render)) return render;
        *context.config = std::move(bound);
        context.baseline_config = std::move(baseline);
        context.last_valid_primary_dict = restored.primary_dict;
        context.custom_primary_dict_path = restored.primary_dict_path;
        context.config_registry_sequence = sequence;
        UpdateSettingsWindowTitle(hwnd, context);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Restored backup settings refresh ran out of memory");
        return E_OUTOFMEMORY;
    }
}

} // namespace

HRESULT ExportSettingsBackup(HWND hwnd, const std::filesystem::path& path) {
    const TransferScope transfer(hwnd);
    const auto* ctx = transfer.Get();
    if (!ctx) return E_ABORT;
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    HRESULT result = S_OK;
    try {
        CompleteBackup backup(GetSettingsModuleDirectory(*ctx));
        result = backup.CreateFileResult(path);
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
    } catch (...) {
        result = E_UNEXPECTED;
    }
    std::ignore = log::CheckHResult(result, "ExportSettingsBackup");
    ShowBackupExportResult(hwnd, language, result);
    return result;
}

HRESULT RestoreSettingsBackup(
    HWND hwnd,
    const std::filesystem::path& path,
    BackupRestoreModeChooser chooser) {
    const TransferScope transfer(hwnd);
    auto* ctx = transfer.Get();
    if (!ctx) return E_ABORT;
    if (!chooser) return E_INVALIDARG;
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    const auto mode = chooser(hwnd, language);
    if (!mode) return S_FALSE;

    BackupRestoreResult restore;
    try {
        CompleteBackup backup(GetSettingsModuleDirectory(*ctx));
        restore = backup.RestoreFile(path, *mode);
    } catch (const std::bad_alloc&) {
        restore.status = BackupRestoreStatus::Failed;
        restore.stage = BackupRestoreStage::Read;
        restore.error = E_OUTOFMEMORY;
    } catch (...) {
        restore.status = BackupRestoreStatus::Failed;
        restore.stage = BackupRestoreStage::Read;
        restore.error = E_UNEXPECTED;
    }

    if (restore.summary.config_restored ||
        restore.status == BackupRestoreStatus::Success) {
        const HRESULT refreshed = RefreshSettingsAfterRestore(hwnd, *ctx);
        if (FAILED(refreshed)) {
            restore.status = BackupRestoreStatus::Partial;
            restore.stage = BackupRestoreStage::Refresh;
            restore.error = refreshed;
            restore.refresh_error = refreshed;
            ++restore.operations_failed;
        }
    }
    ShowBackupRestoreResult(hwnd, language, restore);
    return restore.status == BackupRestoreStatus::Success
        ? S_OK
        : restore.error;
}

void PromptSettingsBackupTransfer(
    HWND hwnd, bool importing, const dialog_detail::FileDialogApi& api) {
    const auto* ctx = Context(hwnd);
    if (!ctx || !ctx->config) {
        QIQI_LOG_WARN("Backup file dialog requires a bound settings window");
        return;
    }
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    std::array<wchar_t, kWin32PathBufferChars> path{};
    if (!importing) wcscpy_s(path.data(), path.size(), L"qiqiime_backup.zip");
    dialog_detail::FileDialogResult selected;
    {
        const TransferScope transfer(hwnd);
        if (!transfer.Get()) return;
        selected = dialog_detail::PromptForFile(hwnd,
            LocalizedUiText(
                language,
                importing ? L"选择奇奇五笔完整备份" : L"创建配置与词库完整备份",
                importing ? L"Select QiQi complete backup" : L"Create complete settings and dictionary backup"),
            LocalizedUiText(
                language,
                L"奇奇五笔完整备份 (*.zip)\0*.zip\0所有文件 (*.*)\0*.*\0",
                L"QiQi complete backup (*.zip)\0*.zip\0All files (*.*)\0*.*\0"),
            path, importing ? dialog_detail::FileDialogMode::Open : dialog_detail::FileDialogMode::Save,
            L"zip", api);
    }
    if (selected.status == dialog_detail::FileDialogStatus::Failed) {
        const auto message = std::format(L"{}\r\nCommDlgExtendedError: 0x{:08X}",
            LocalizedUiText(
                language, L"无法打开备份文件选择窗口。", L"Could not open the backup file dialog."),
            selected.error);
        if (!::MessageBoxW(
                hwnd, message.c_str(), GetUiStrings(language).dlg_title, MB_OK | MB_ICONERROR)) {
            QIQI_LOG_ERROR("Backup file-dialog error message failed (error={})", ::GetLastError());
        }
    } else if (selected) {
        if (importing) std::ignore = RestoreSettingsBackup(hwnd, path.data());
        else std::ignore = ExportSettingsBackup(hwnd, path.data());
    }
}

} // namespace qiqi::settings_detail
