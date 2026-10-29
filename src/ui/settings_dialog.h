#pragma once
#include <windows.h>
#include "common/config.h"

namespace qiqi {

// Apply control states from an open settings window to QiQiConfig.
void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept;

// Populate an open settings window's controls from QiQiConfig.
void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept;

// Create a modeless settings window (used by both ShowSettingsDialog and unit tests).
HWND CreateSettingsWindow(HINSTANCE instance, HWND parent, QiQiConfig* bound_config) noexcept;

// Show modal settings dialog to edit QiQiConfig. Returns true if user clicked Save (OK).
bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept;

} // namespace qiqi
