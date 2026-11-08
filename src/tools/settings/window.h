#pragma once
#include <windows.h>
#include "common/about_dialog.h"
#include "common/config.h"
#include "common/scoped_handle.h"

namespace qiqi {

// Apply control states from an open settings window to QiQiConfig.
void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept;

// Populate an open settings window's controls from QiQiConfig.
void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept;

// Validate and apply a custom primary dictionary file path to an open settings window.
// If invalid, displays SettingsMessageKind::InvalidPrimaryDict and preserves existing selection.
bool SelectCustomPrimaryDictionaryInSettings(
    HWND hwnd, const std::filesystem::path& file_path) noexcept;

// If a Settings dialog window is already open, restore it (if minimized) and activate its
// foreground target (either its active owned modal popup or the Settings window itself).
bool ActivateExistingSettingsWindow() noexcept;

// Create a modeless settings window (used by both ShowSettingsDialog and unit tests).
ScopedWindow CreateSettingsWindow(
    HINSTANCE instance, HWND parent, QiQiConfig* bound_config, bool save_on_accept = false) noexcept;

// Returns true on acceptance. With save_on_accept, persistence must succeed before acceptance.
// Failed persistence keeps the window open and leaves in_out_config unchanged.
// Owns the single-instance mutex; callers must not create it before calling.
bool ShowSettingsDialog(
    HINSTANCE instance, HWND parent, QiQiConfig& in_out_config, bool save_on_accept = false) noexcept;

void ReportSettingsNotificationFailure(HWND parent, UiLanguage ui_language) noexcept;

enum class SettingsMessageKind {
    SaveFailure,
    NotificationFailure,
    DictManagerLaunchFailure,
    InvalidPrimaryDict
};

enum class SettingsToastKind {
    SaveFailure,
    NotificationFailure
};

ScopedWindow CreateSettingsMessageWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

// Returns a button ID, or zero on creation/modal-loop failure.
int ShowSettingsMessage(HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

void ShowSettingsToast(UiLanguage ui_language, SettingsToastKind kind) noexcept;
void UpdateSettingsToastLanguage(UiLanguage ui_language) noexcept;

} // namespace qiqi
