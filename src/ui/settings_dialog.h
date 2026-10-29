#pragma once
#include <windows.h>
#include "common/config.h"
#include "common/scoped_handle.h"

namespace qiqi {

// Apply control states from an open settings window to QiQiConfig.
void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept;

// Populate an open settings window's controls from QiQiConfig.
void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept;

// Create a modeless settings window (used by both ShowSettingsDialog and unit tests).
ScopedWindow CreateSettingsWindow(HINSTANCE instance, HWND parent, QiQiConfig* bound_config) noexcept;

// Show modal settings dialog to edit QiQiConfig. Returns true if user clicked Save (OK).
bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept;

// Create a modeless About window (used by both ShowAboutDialog and unit tests).
ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto) noexcept;

// Show modal About dialog displaying IME name ("奇奇五笔"), icon, version, GitHub URL, and licenses.
bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto) noexcept;

} // namespace qiqi
