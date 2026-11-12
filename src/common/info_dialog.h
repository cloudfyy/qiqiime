// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include "common/config.h"
#include "common/scoped_handle.h"

namespace qiqi {

// Shared About and Help UI; embedded-icon mode uses resource 1 from instance.
ScopedWindow CreateAboutWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

// Show modal About dialog displaying IME name, icon, version, GitHub URL, and licenses.
bool ShowAboutDialog(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

// Create or show modal Help & Keyboard Shortcuts dialog.
ScopedWindow CreateHelpWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;
bool ShowHelpDialog(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

} // namespace qiqi
