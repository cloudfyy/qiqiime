// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include "common/dialog.h"
#include "common/config.h"
#include "common/RAII.h"
#include "common/settings.h"
#include "common/skin.h"

namespace qiqi {

struct SkinSelectionState {
    ImeSkinId selected = ImeSkinId::Default;
    bool accepted = false;
    bool done = false;
};

// The state must outlive the returned window.
ScopedWindow CreateSkinSelectionWindow(HINSTANCE instance, HWND parent,
    const ImeSkinCatalog& catalog, DisplayPreferences display, UiLanguage language,
    SkinSelectionState* state, HRESULT* result = nullptr,
    const CandidateFontPreferences& candidate_font = CandidateFontPreferences{}) noexcept;
HRESULT ShowSkinSelectionDialog(HINSTANCE instance, HWND parent,
    const ImeSkinCatalog& catalog, DisplayPreferences display, UiLanguage language, ImeSkinId& selection,
    const CandidateFontPreferences& candidate_font = CandidateFontPreferences{}) noexcept;

// Apply control states from an open settings window to QiQiConfig.
void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept;

// Populate an open settings window's controls from QiQiConfig.
void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept;
HRESULT WriteSettingsToWindowControlsResult(HWND hwnd, const QiQiConfig& config) noexcept;

// Validate and apply a custom primary dictionary file path to an open settings window.
// If invalid, displays SettingsMessageKind::InvalidPrimaryDict and preserves existing selection.
bool SelectCustomPrimaryDictionaryInSettings(
    HWND hwnd, const std::filesystem::path& file_path) noexcept;

// Create a modeless settings window (used by both ShowSettingsDialog and unit tests).
ScopedWindow CreateSettingsWindow(
    HINSTANCE instance, HWND parent, QiQiConfig* bound_config, bool save_on_accept = false) noexcept;

// Returns true on acceptance. With save_on_accept, persistence must succeed before acceptance.
// Failed persistence keeps the window open and does not publish local edits to in_out_config.
// Owns the single-instance mutex; callers must not create it before calling.
bool ShowSettingsDialog(
    HINSTANCE instance, HWND parent, QiQiConfig& in_out_config, bool save_on_accept = false) noexcept;

void ReportSettingsNotificationFailure(HWND parent, UiLanguage ui_language) noexcept;

enum class SettingsMessageKind {
    SaveFailure,
    NotificationFailure,
    DictManagerLaunchFailure,
    InvalidPrimaryDict,
    SaveConflict
};

ScopedWindow CreateSettingsMessageWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

// Returns a button ID, or zero on creation/modal-loop failure.
int ShowSettingsMessage(HWND parent, UiLanguage ui_language, SettingsMessageKind kind) noexcept;

} // namespace qiqi
