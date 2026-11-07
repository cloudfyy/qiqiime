#pragma once

#include <windows.h>
#include "common/config.h"
#include "common/scoped_handle.h"

namespace qiqi {

// Settings and Log Monitor share the About UI; embedded-icon mode uses resource 1 from instance.
ScopedWindow CreateAboutWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

// Show modal About dialog displaying IME name, icon, version, GitHub URL, and licenses.
bool ShowAboutDialog(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

// Create or show modal Help & Keyboard Shortcuts dialog sharing the About dialog infrastructure.
ScopedWindow CreateHelpWindow(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;
bool ShowHelpDialog(
    HINSTANCE instance, HWND parent, UiLanguage ui_language = UiLanguage::Auto, bool use_embedded_icon = false) noexcept;

} // namespace qiqi
