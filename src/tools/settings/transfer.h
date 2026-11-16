// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/settings/window.h"
#include "common/config_json.h"
#include "common/dlgutil.h"

namespace qiqi::settings_detail {

inline constexpr const wchar_t* kSettingsImportPreviewWndClass = L"QiQiIME.SettingsImportPreview";
inline constexpr int kIdConfigChanges = 101;

std::wstring FormatConfigJsonError(const ConfigJsonError& error, HRESULT result, UiLanguage language);
std::wstring FormatSettingsConfigChanges(
    const QiQiConfig& before, const QiQiConfig& after, UiLanguage language);

// Confirmation stages edits only; the settings window's OK/Cancel still owns persistence.
HRESULT ImportSettingsConfiguration(HWND hwnd, const std::filesystem::path& path);
HRESULT ExportSettingsConfiguration(HWND hwnd, const std::filesystem::path& path);
void PromptSettingsConfigTransfer(
    HWND hwnd, bool importing, const dialog_detail::FileDialogApi& api = {});

} // namespace qiqi::settings_detail
