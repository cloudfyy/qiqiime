// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <optional>
#include "common/scoped_handle.h"
#include "common/config.h"
#include "common/logger.h"

namespace qiqi {

inline constexpr COLORREF kDarkWindowBg = RGB(32, 34, 38);
inline constexpr COLORREF kLightSurfaceBg = RGB(248, 250, 253);
inline constexpr COLORREF kDarkTextColor = RGB(238, 240, 245);
inline constexpr COLORREF kLightTitleColor = RGB(24, 30, 42);
inline constexpr COLORREF kDarkSecondaryTextColor = RGB(168, 176, 190);
inline constexpr COLORREF kLightSecondaryTextColor = RGB(85, 95, 110);
inline constexpr COLORREF kDarkDividerColor = RGB(64, 68, 78);
inline constexpr COLORREF kLightDividerColor = RGB(220, 225, 232);
inline constexpr COLORREF kDarkBodyTextColor = RGB(215, 220, 228);
inline constexpr COLORREF kLightBodyTextColor = RGB(45, 52, 64);
inline constexpr COLORREF kLightCaptionBg = RGB(243, 245, 249);
inline constexpr COLORREF kLightTextColor = RGB(28, 32, 40);
inline constexpr COLORREF kDarkWindowBorderColor = RGB(56, 60, 68);
inline constexpr COLORREF kLightWindowBorderColor = RGB(212, 218, 228);
inline constexpr COLORREF kDarkTaskbarIconColor = RGB(255, 255, 255);
inline constexpr COLORREF kLightTaskbarIconColor = RGB(0, 0, 0);

struct ThemePalette {
    bool is_dark = false;
    COLORREF window_bg = RGB(246, 248, 252);
    COLORREF border_color = RGB(205, 212, 224);
    COLORREF text_primary = RGB(30, 35, 45);
    COLORREF text_accent = RGB(20, 90, 190);
    COLORREF logo_bg = RGB(35, 105, 200);
    COLORREF logo_text = RGB(255, 255, 255);
    COLORREF hover_bg = RGB(226, 233, 246);
    COLORREF grip_dot = RGB(130, 142, 165);
    COLORREF tooltip_bg = RGB(255, 255, 255);
    COLORREF tooltip_text = RGB(30, 35, 45);
    COLORREF surface_bg = RGB(255, 255, 255);
    COLORREF input_bg = RGB(255, 255, 255);
    COLORREF text_secondary = kLightSecondaryTextColor;
    COLORREF selection_bg = RGB(20, 90, 190);
    COLORREF selection_text = RGB(255, 255, 255);
    COLORREF focus_color = RGB(20, 90, 190);
};

inline constexpr ThemePalette kLightThemePalette{};

inline constexpr ThemePalette kDarkThemePalette = {
    true,
    kDarkWindowBg,
    RGB(68, 72, 80),
    RGB(235, 238, 244),
    RGB(96, 170, 255),
    RGB(45, 115, 210),
    RGB(255, 255, 255),
    RGB(52, 56, 66),
    RGB(140, 148, 164),
    RGB(42, 45, 52),
    kDarkTextColor,
    RGB(43, 46, 52),
    RGB(54, 58, 68),
    kDarkSecondaryTextColor,
    RGB(18, 60, 110),
    RGB(255, 255, 255),
    RGB(150, 200, 255)
};

inline constexpr ThemePalette kHighContrastThemePalette{
    true, RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 255, 255),
    RGB(255, 255, 0), RGB(255, 255, 0), RGB(0, 0, 0), RGB(35, 35, 35),
    RGB(255, 255, 255), RGB(0, 0, 0), RGB(255, 255, 255),
    RGB(0, 0, 0), RGB(0, 0, 0), RGB(255, 255, 255),
    RGB(255, 255, 0), RGB(0, 0, 0), RGB(255, 255, 0)
};

bool IsHighContrastEnabled() noexcept;
inline bool IsSystemDarkTheme() noexcept;
constexpr const ThemePalette& GetThemePalette(bool is_dark) noexcept;

struct DisplayAppearance {
    ThemePalette palette{};
    bool high_contrast = false;
    bool system_high_contrast = false;
};

inline DisplayAppearance ResolveDisplayAppearance(const DisplayPreferences& preferences) noexcept {
    if (IsHighContrastEnabled()) {
        ThemePalette palette;
        palette.window_bg = ::GetSysColor(COLOR_WINDOW);
        palette.surface_bg = ::GetSysColor(COLOR_BTNFACE);
        palette.input_bg = ::GetSysColor(COLOR_WINDOW);
        palette.border_color = ::GetSysColor(COLOR_WINDOWTEXT);
        palette.text_primary = ::GetSysColor(COLOR_WINDOWTEXT);
        palette.text_secondary = palette.text_primary;
        palette.text_accent = palette.text_primary;
        palette.logo_bg = ::GetSysColor(COLOR_HIGHLIGHT);
        palette.logo_text = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
        palette.hover_bg = palette.logo_bg;
        palette.grip_dot = palette.text_primary;
        palette.tooltip_bg = palette.window_bg;
        palette.tooltip_text = palette.text_primary;
        palette.selection_bg = palette.logo_bg;
        palette.selection_text = palette.logo_text;
        palette.focus_color = palette.text_primary;
        return {palette, true, true};
    }
    if (preferences.theme == UiTheme::HighContrast) return {kHighContrastThemePalette, true, false};
    const bool dark = preferences.theme == UiTheme::Dark ||
        (preferences.theme == UiTheme::System && IsSystemDarkTheme());
    return {GetThemePalette(dark), false, false};
}

inline int ScaleDisplayPx(int px, UINT dpi, UiTextScale scale) noexcept {
    return ::MulDiv(px, gsl::narrow_cast<int>((dpi > 0 ? dpi : 96u) *
        gsl::narrow_cast<UINT>(TextScalePercent(scale))), 9600);
}

inline void ApplyDisplayThemeToWindow(HWND hwnd, const DisplayAppearance& appearance) noexcept {
    const auto set_attribute = [hwnd](DWORD attribute, COLORREF value) noexcept {
        return ::DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
    };
    const BOOL dark = appearance.palette.is_dark ? TRUE : FALSE;
    std::ignore = log::CheckHResult(
        ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)),
        "Display::SetImmersiveDarkMode", log::Level::debug);
    const COLORREF caption = appearance.high_contrast ? DWMWA_COLOR_DEFAULT : appearance.palette.window_bg;
    const COLORREF text = appearance.high_contrast ? DWMWA_COLOR_DEFAULT : appearance.palette.text_primary;
    std::ignore = log::CheckHResult(set_attribute(DWMWA_CAPTION_COLOR, caption),
        "Display::SetCaptionColor", log::Level::debug);
    std::ignore = log::CheckHResult(set_attribute(DWMWA_TEXT_COLOR, text),
        "Display::SetCaptionTextColor", log::Level::debug);
    std::ignore = log::CheckHResult(set_attribute(DWMWA_BORDER_COLOR,
        appearance.high_contrast ? DWMWA_COLOR_DEFAULT : appearance.palette.border_color),
        "Display::SetBorderColor", log::Level::debug);
}

namespace detail {
inline constexpr WORD kSetPreferredAppModeOrdinal = 135;
inline constexpr WORD kFlushMenuThemesOrdinal = 136;
inline constexpr DWORD kLegacyImmersiveDarkModeAttribute = 19;

enum class PreferredAppMode : int {
    ForceDark = 2,
    ForceLight = 3
};

inline std::optional<bool>& ThemeDarkOverrideStorage() noexcept {
    static std::optional<bool> override_val;
    return override_val;
}

inline std::optional<bool>& TaskbarDarkOverrideStorage() noexcept {
    static std::optional<bool> override_val;
    return override_val;
}

inline bool QueryPersonalizeLightThemeValue(const wchar_t* value_name, bool default_is_light) noexcept {
    ScopedRegKey key;
    if (::RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0,
            KEY_READ,
            key.Put()) != ERROR_SUCCESS) {
        return default_is_light;
    }

    DWORD use_light = default_is_light ? 1 : 0;
    DWORD cb_data = sizeof(use_light);
    if (::RegQueryValueExW(
            key.Get(),
            value_name,
            nullptr,
            nullptr,
            reinterpret_cast<LPBYTE>(&use_light),
            &cb_data) == ERROR_SUCCESS) {
        return use_light != 0;
    }
    return default_is_light;
}
} // namespace detail

// Allow unit tests to deterministically force Light (false) or Dark (true) theme, or std::nullopt for OS setting.
inline void SetThemeDarkOverrideForTest(std::optional<bool> is_dark) noexcept {
    detail::ThemeDarkOverrideStorage() = is_dark;
}

inline void SetTaskbarDarkOverrideForTest(std::optional<bool> is_dark) noexcept {
    detail::TaskbarDarkOverrideStorage() = is_dark;
}

// Query Windows 10/11 Personalize registry for application Light vs Dark mode.
inline bool IsSystemDarkTheme() noexcept {
    if (const auto& ov = detail::ThemeDarkOverrideStorage(); ov.has_value()) {
        return *ov;
    }
    return !detail::QueryPersonalizeLightThemeValue(L"AppsUseLightTheme", true);
}

// Query Windows 10/11 Personalize registry for system shell / taskbar Light vs Dark mode.
inline bool IsTaskbarDarkTheme() noexcept {
    if (const auto& ov = detail::TaskbarDarkOverrideStorage(); ov.has_value()) {
        return *ov;
    }
    if (const auto& ov = detail::ThemeDarkOverrideStorage(); ov.has_value()) {
        return *ov;
    }
    return !detail::QueryPersonalizeLightThemeValue(L"SystemUsesLightTheme", false);
}

inline COLORREF GetTaskbarIconColor() noexcept {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
        (hc.dwFlags & HCF_HIGHCONTRASTON) != 0) {
        return ::GetSysColor(COLOR_BTNTEXT);
    }
    return IsTaskbarDarkTheme() ? kDarkTaskbarIconColor : kLightTaskbarIconColor;
}

constexpr const ThemePalette& GetThemePalette(bool is_dark) noexcept {
    return is_dark ? kDarkThemePalette : kLightThemePalette;
}

inline const ThemePalette& GetCurrentThemePalette() noexcept {
    return GetThemePalette(IsSystemDarkTheme());
}

inline const ThemePalette& GetTaskbarThemePalette() noexcept {
    return GetThemePalette(IsTaskbarDarkTheme());
}

// Sync Win32 popup menus (uxtheme ordinal 135/136) and window border with Windows 10/11 dark/light theme.
inline void ApplySystemThemeToMenuAndWindow(HWND hwnd, bool is_dark) noexcept {
    static const HMODULE uxtheme = ::LoadLibraryW(L"uxtheme.dll");
    if (uxtheme) {
        // Undocumented uxtheme exports use the PreferredAppMode ABI.
        using SetPreferredAppModeFn = int(WINAPI*)(int);
        using FlushMenuThemesFn = void(WINAPI*)();

        const auto set_mode = reinterpret_cast<SetPreferredAppModeFn>(
            ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(detail::kSetPreferredAppModeOrdinal)));
        const auto flush_menus = reinterpret_cast<FlushMenuThemesFn>(
            ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(detail::kFlushMenuThemesOrdinal)));
        if (set_mode) {
            set_mode(static_cast<int>(is_dark ? detail::PreferredAppMode::ForceDark :
                                               detail::PreferredAppMode::ForceLight));
        }
        if (flush_menus) {
            flush_menus();
        }
    }
    if (hwnd && ::IsWindow(hwnd)) {
        static const HMODULE dwmapi = ::LoadLibraryW(L"dwmapi.dll");
        if (dwmapi) {
            using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            const auto set_attr = reinterpret_cast<DwmSetWindowAttributeFn>(
                ::GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
            if (set_attr) {
                const BOOL dark_flag = is_dark ? TRUE : FALSE;
                if (FAILED(set_attr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_flag, sizeof(dark_flag)))) {
                    set_attr(hwnd, detail::kLegacyImmersiveDarkModeAttribute, &dark_flag, sizeof(dark_flag));
                }

                // Explicitly set caption and border color so dark title bar is guaranteed even on older Win10/11 builds
                const COLORREF cap_color = is_dark ? kDarkWindowBg : kLightCaptionBg;
                const COLORREF txt_color = is_dark ? kDarkTextColor : kLightTextColor;
                const COLORREF bdr_color = is_dark ? kDarkWindowBorderColor : kLightWindowBorderColor;
                set_attr(hwnd, DWMWA_CAPTION_COLOR, &cap_color, sizeof(cap_color));
                set_attr(hwnd, DWMWA_TEXT_COLOR, &txt_color, sizeof(txt_color));
                set_attr(hwnd, DWMWA_BORDER_COLOR, &bdr_color, sizeof(bdr_color));

                constexpr DWORD round_pref = DWMWCP_ROUND;
                set_attr(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round_pref, sizeof(round_pref));
            }
        }
    }
}

} // namespace qiqi
