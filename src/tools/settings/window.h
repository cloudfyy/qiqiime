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
ScopedWindow CreateSettingsWindow(
    HINSTANCE instance, HWND parent, QiQiConfig* bound_config, bool save_on_accept = false) noexcept;

// Returns true on acceptance. With save_on_accept, persistence must succeed before acceptance.
// Failed persistence keeps the window open and leaves in_out_config unchanged.
bool ShowSettingsDialog(
    HINSTANCE instance, HWND parent, QiQiConfig& in_out_config, bool save_on_accept = false) noexcept;

void ReportSettingsNotificationFailure(HWND parent, UiLanguage ui_language) noexcept;

enum class SettingsMessageKind {
    PrivacyConfirmation,
    SaveFailure,
    NotificationFailure
};

enum class SettingsToastKind {
    SaveFailure,
    NotificationFailure
};

ScopedWindow CreateSettingsMessageWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

// Returns a button ID, or zero on creation/modal-loop failure. Privacy confirmation defaults to No.
int ShowSettingsMessage(HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

void ShowSettingsToast(UiLanguage ui_language, SettingsToastKind kind) noexcept;
void UpdateSettingsToastLanguage(UiLanguage ui_language) noexcept;

// Create a modeless About window (used by both ShowAboutDialog and unit tests).
ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto) noexcept;

// Show modal About dialog displaying IME name ("奇奇五笔"), icon, version, GitHub URL, and licenses.
bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto) noexcept;

} // namespace qiqi
