// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <span>
#include <string_view>
#include "common/config.h"
#include "common/RAII.h"

namespace qiqi {

std::wstring_view FormatAboutVersionText(
    UiLanguage language, std::span<wchar_t> output) noexcept;

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
