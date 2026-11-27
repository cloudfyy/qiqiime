// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/configclient.h"
#include "tools/settings/transfer.h"
#include "common/dlgutil.h"
#include "common/dictui.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/engine/dict.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <gsl/gsl>
#include <memory>
#include <new>
#include <utility>

namespace qiqi {

using namespace settings_detail;

DWORD settings_detail::OpenDictionaryManager(
    HWND owner, const std::filesystem::path& executable, UiLanguage language,
    gsl::not_null<decltype(&::ShellExecuteExW)> execute) {
#pragma warning(suppress : 26476) // Win32 requires this union-backed descriptor.
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = executable.c_str();
    info.nShow = SW_SHOWNORMAL;
    const BOOL launched = execute(&info);
    const DWORD error = launched ? ERROR_SUCCESS : ::GetLastError();
    const ScopedKernelHandle process(info.hProcess);
    if (!launched) {
        QIQI_LOG_CAT_WARN(log::Keyword::UI,
            "Settings failed to launch QiQiIMEDictManager.exe (error={})", error);
        ShowSettingsMessage(owner, language, SettingsMessageKind::DictManagerLaunchFailure);
    }
    return error;
}

namespace {

void ShowSettingsSaveFailure(HWND hwnd, UiLanguage ui_language) noexcept {
    ShowSettingsMessage(hwnd, ui_language, SettingsMessageKind::SaveFailure);
}

void CheckPrimaryDictRadio(HWND hwnd, PrimaryDict dict) noexcept {
    int target_id = kIdRadioJidian;
    if (dict == PrimaryDict::Haifeng) {
        target_id = kIdRadioHaifeng;
    } else if (dict == PrimaryDict::BuiltinOnly) {
        target_id = kIdRadioBuiltin;
    } else if (dict == PrimaryDict::Custom) {
        target_id = kIdRadioCustom;
    }
    for (const int id : {kIdRadioJidian, kIdRadioHaifeng, kIdRadioBuiltin, kIdRadioCustom}) {
        ::CheckDlgButton(hwnd, id, id == target_id ? BST_CHECKED : BST_UNCHECKED);
    }
}

} // namespace

std::filesystem::path settings_detail::GetSettingsModuleDirectory(
    const DialogContext& ctx) {
    const std::filesystem::path mod_path =
        GetModuleFilePath(ctx.instance ? ctx.instance : g_dll_instance);
    return mod_path.empty() ? std::filesystem::path{} : mod_path.parent_path();
}

namespace {

bool ValidateSettingsPrimaryDictionary(HWND hwnd, const DialogContext& ctx, const QiQiConfig& edited) {
    if (edited.primary_dict != PrimaryDict::Custom) {
        return true;
    }
    const auto resolved = ResolveConfiguredDictionaryPath(
        GetSettingsModuleDirectory(ctx), edited.primary_dict_path);
    const auto validation = WubiMappedDict::ValidateAndLoadPrimaryDictionary(resolved);
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    if (!validation) {
        ShowSettingsMessage(hwnd, language, SettingsMessageKind::InvalidPrimaryDict);
        return false;
    }
    return dictionary_ui::ConfirmPartialDictionary(
        hwnd, language, static_cast<bool>(validation), validation.diagnostics);
}

} // namespace

bool SelectCustomPrimaryDictionaryInSettings(
    HWND hwnd, const std::filesystem::path& file_path) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) {
        return false;
    }
    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ctx) {
        return false;
    }
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    try {
        const std::filesystem::path resolved =
            ResolveConfiguredDictionaryPath(GetSettingsModuleDirectory(*ctx), file_path.wstring());
        const PrimaryDictValidationResult validation =
            WubiMappedDict::ValidateAndLoadPrimaryDictionary(resolved);
        if (!validation) {
            QIQI_LOG_CAT_WARN(
                log::Keyword::Dictionary,
                "Settings rejected invalid custom primary dictionary '{}' (status={}, error={})",
                WideToUtf8(file_path.wstring()),
                static_cast<unsigned int>(validation.status),
                validation.win32_error);
            CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            ShowSettingsMessage(hwnd, lang, SettingsMessageKind::InvalidPrimaryDict);
            return false;
        }
        if (!dictionary_ui::ConfirmPartialDictionary(
                hwnd, lang, static_cast<bool>(validation),
                validation.diagnostics)) {
            CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return false;
        }
        ctx->custom_primary_dict_path = file_path.wstring();
        ctx->last_valid_primary_dict = PrimaryDict::Custom;
        CheckPrimaryDictRadio(hwnd, PrimaryDict::Custom);
        const auto& str = GetUiStrings(lang);
        ::SetDlgItemTextW(
            hwnd,
            kIdLblCustomPrimaryPath,
            ResolveDictionaryPathDisplayText(ctx->custom_primary_dict_path, str.dlg_lbl_custom_dict_none));
        UpdateExtraDictCheckboxState(hwnd);
        UpdateSettingsWindowTitle(hwnd, *ctx);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    } catch (...) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Dictionary,
            "SelectCustomPrimaryDictionaryInSettings failed with an exception");
        CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
        UpdateExtraDictCheckboxState(hwnd);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        ShowSettingsMessage(hwnd, lang, SettingsMessageKind::InvalidPrimaryDict);
        return false;
    }
}
namespace {

bool BrowseCustomPrimaryDictionary(HWND hwnd, const DialogContext& ctx) noexcept {
    try {
        const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
        std::filesystem::path selected_path;
        if (!dialog_detail::PromptForPrimaryDictionaryFile(
                hwnd, lang, ctx.custom_primary_dict_path, selected_path)) {
            return false;
        }
        return SelectCustomPrimaryDictionaryInSettings(hwnd, selected_path);
    } catch (...) {
        QIQI_LOG_CAT_ERROR(log::Keyword::UI, "BrowseCustomPrimaryDictionary failed with an exception");
        return false;
    }
}

} // namespace

void HandleSettingsCommand(HWND hwnd, WPARAM wparam, DialogContext& ctx) {
    if (ctx.transfer_in_progress) return;
    switch (LOWORD(wparam)) {
        case kIdRadioJidian:
            CheckPrimaryDictRadio(hwnd, PrimaryDict::Jidian);
            ctx.last_valid_primary_dict = PrimaryDict::Jidian;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioHaifeng:
            CheckPrimaryDictRadio(hwnd, PrimaryDict::Haifeng);
            ctx.last_valid_primary_dict = PrimaryDict::Haifeng;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioBuiltin:
            CheckPrimaryDictRadio(hwnd, PrimaryDict::BuiltinOnly);
            ctx.last_valid_primary_dict = PrimaryDict::BuiltinOnly;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioCustom:
            CheckPrimaryDictRadio(hwnd, PrimaryDict::Custom);
            ctx.last_valid_primary_dict = PrimaryDict::Custom;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnBrowsePrimaryDict:
            if (::IsDlgButtonChecked(hwnd, kIdRadioCustom) != BST_CHECKED) {
                QIQI_LOG_DEBUG("Settings ignored Browse command without a selected custom dictionary");
                return;
            }
            std::ignore = BrowseCustomPrimaryDictionary(hwnd, ctx);
            return;
        case kIdComboUiLang:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                RenderSettingsLanguage(hwnd, ctx);
            }
            return;
        case kIdChkEasyReading:
            if (HIWORD(wparam) == BN_CLICKED) {
                ApplySettingsDisplayPreview(hwnd, ctx);
            }
            return;
        case kIdComboUiTheme:
            if (HIWORD(wparam) == CBN_SELCHANGE) ApplySettingsDisplayPreview(hwnd, ctx);
            return;
        case kIdComboImeSkin:
            if (HIWORD(wparam) == CBN_DROPDOWN) {
                const HWND combo = ::GetDlgItem(hwnd, kIdComboImeSkin);
                const LRESULT count = ::SendMessageW(combo, CB_GETCOUNT, 0, 0);
                const LRESULT item_height = ::SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0);
                if (count <= 0 || item_height <= 0) {
                    QIQI_LOG_WARN("Settings skin dropdown item metrics unavailable");
                    return;
                }
                RECT bounds{};
                if (!::GetWindowRect(combo, &bounds)) {
                    QIQI_LOG_WARN("Settings skin dropdown bounds query failed (error={})", ::GetLastError());
                    return;
                }
                const RECT work = QueryActiveMonitor(combo).work_area;
                const LONG above = std::clamp(bounds.top, work.top, work.bottom) - work.top;
                const LONG below = work.bottom - std::clamp(bounds.bottom, work.top, work.bottom);
                const LRESULT edge =
                    gsl::narrow_cast<LRESULT>(
                        ::GetSystemMetricsForDpi(
                            SM_CYEDGE, ctx.d2d.dpi));
                const LRESULT available =
                    std::max(
                        gsl::narrow_cast<LRESULT>(above),
                        gsl::narrow_cast<LRESULT>(below)) -
                    2 * edge;
                const LRESULT visible = std::clamp(available / item_height, LRESULT{1}, count);
                if (!::SendMessageW(combo, CB_SETMINVISIBLE, gsl::narrow_cast<WPARAM>(visible), 0)) {
                    QIQI_LOG_WARN("Settings skin dropdown visible row count update failed");
                }
            } else if (HIWORD(wparam) == CBN_SELCHANGE) {
                HandleSettingsSkinSelection(hwnd, ctx);
            }
            return;
        case kIdBtnCandidateFont:
            if (HIWORD(wparam) == BN_CLICKED) {
                std::ignore = ShowSettingsCandidateFontMenu(hwnd, ctx);
            }
            return;
        case kIdComboUiTextScale:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                if (::IsDlgButtonChecked(hwnd, kIdChkEasyReading) == BST_CHECKED &&
                    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_GETCURSEL, 0, 0) <
                        static_cast<LRESULT>(UiTextScale::Percent150)) {
                    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL,
                        static_cast<WPARAM>(std::max(ctx.preview_scale, UiTextScale::Percent150)), 0);
                }
                ApplySettingsDisplayPreview(hwnd, ctx);
            }
            return;
        case kIdComboUnderlineStyle:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                UpdateExtraDictCheckboxState(hwnd);
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        case kIdChkBoldUnderline:
            if (const HWND combo = ::GetDlgItem(hwnd, kIdComboUnderlineStyle)) {
                ::InvalidateRect(combo, nullptr, FALSE);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnRestoreDefaults: {
            // Restored values are unsaved edits, not a new external-configuration baseline.
            std::ignore = log::CheckHResult(RenderSettingsDraft(hwnd, ctx, QiQiConfig{}),
                                           "Settings::RestoreDefaults");
            return;
        }
        case kIdBtnImportConfig:
        case kIdBtnExportConfig:
            PromptSettingsBackupTransfer(hwnd, LOWORD(wparam) == kIdBtnImportConfig);
            return;
        case kIdBtnOk: {
            if (ctx.config) {
                QiQiConfig edited;
                QiQiConfig baseline;
                try {
                    edited = *ctx.config;
                    ReadSettingsFromWindowControls(hwnd, edited);
                    baseline = edited;
                } catch (const std::bad_alloc&) {
                    QIQI_LOG_ERROR("Settings save could not copy configuration");
                    ShowSettingsSaveFailure(hwnd, GetSelectedUiLanguageFromCombo(hwnd, ctx));
                    return;
                }
                if (!ValidateSettingsPrimaryDictionary(hwnd, ctx, edited)) {
                    return;
                }
                if (edited.ime_skin_id != ImeSkinId::Default) {
                    if (FAILED(
                            ctx.skin_catalog_result)) {
                        ReportSkinConfigurationFailure(
                            hwnd,
                            edited.ui_language,
                            ctx.skin_catalog_result);
                        return;
                    }
                    const HRESULT result =
                        ValidateImeSkinCatalog(
                            ctx.skin_catalog);
                    if (FAILED(result) || !FindImeSkin(
                            ctx.skin_catalog,
                            edited.ime_skin_id)) {
                        ReportSkinConfigurationFailure(hwnd, edited.ui_language,
                            FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
                        return;
                    }
                }
                if (ctx.save_on_accept) {
                    const HRESULT saved =
                        SaveConfigResult(edited);
                    if (FAILED(saved)) {
                        QIQI_LOG_ERROR("Settings save failed; retaining edits for retry (hr=0x{:08X})",
                            gsl::narrow_cast<unsigned long>(saved));
                        ShowSettingsSaveFailure(hwnd, edited.ui_language);
                        return;
                    }
                    config::Snapshot published;
                    if (config::CopyCachedSnapshot(
                            published) == S_OK &&
                        FAILED(published
                                   .notification_result)) {
                        ReportSettingsNotificationFailure(
                            hwnd,
                            edited.ui_language);
                    }
                }
                if (ctx.save_on_accept) {
                    ctx.config_registry_sequence =
                        config::CachedRevision();
                }
                ctx.baseline_config = std::move(baseline);
                *ctx.config = std::move(edited);
            }
            ctx.accepted = true;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        }
        case kIdBtnCancel:
            ctx.accepted = false;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnOpenGitHub: {
            const HINSTANCE result = ::ShellExecuteW(
                hwnd, L"open", kGitHubRepoUrl, nullptr, nullptr,
                SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                QIQI_LOG_ERROR(
                    "Settings GitHub launch failed (error={})",
                    reinterpret_cast<INT_PTR>(result));
            }
            return;
        }
        case kIdBtnDictManager: {
            const std::filesystem::path mod_path = GetModuleFilePath(ctx.instance ? ctx.instance : g_dll_instance);
            const std::filesystem::path exe_path =
                mod_path.empty() ? std::filesystem::path(L"QiQiIMEDictManager.exe")
                                 : (mod_path.parent_path() / L"QiQiIMEDictManager.exe");
            std::ignore = OpenDictionaryManager(
                hwnd, exe_path, GetSelectedUiLanguageFromCombo(hwnd, ctx));
            return;
        }
        default:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
}

} // namespace qiqi
