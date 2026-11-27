// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"

namespace qiqi {

enum class SettingsToastKind {
    SaveFailure,
    NotificationFailure,
    LaunchFailure,
    FullCodeUnavailable,
};

using SettingsLauncherForTest =
    HRESULT (*)(HINSTANCE module, HWND owner) noexcept;

bool ActivateExistingSettingsWindow() noexcept;
HRESULT OpenStandaloneSettings(
    HINSTANCE module, HWND owner, UiLanguage language) noexcept;
void SetSettingsLauncherForTest(
    SettingsLauncherForTest launcher) noexcept;

void ShowSettingsToast(
    UiLanguage language, SettingsToastKind kind) noexcept;
void UpdateSettingsToastLanguage(UiLanguage language) noexcept;

} // namespace qiqi
