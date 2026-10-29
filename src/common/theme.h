#pragma once
#include <windows.h>
#include <optional>
#include "common/scoped_handle.h"

namespace qiqi {

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
};

inline constexpr ThemePalette kLightThemePalette = {
    false,
    RGB(246, 248, 252),
    RGB(205, 212, 224),
    RGB(30, 35, 45),
    RGB(20, 90, 190),
    RGB(35, 105, 200),
    RGB(255, 255, 255),
    RGB(226, 233, 246),
    RGB(130, 142, 165),
    RGB(255, 255, 255),
    RGB(30, 35, 45)
};

inline constexpr ThemePalette kDarkThemePalette = {
    true,
    RGB(32, 34, 38),
    RGB(68, 72, 80),
    RGB(235, 238, 244),
    RGB(96, 170, 255),
    RGB(45, 115, 210),
    RGB(255, 255, 255),
    RGB(52, 56, 66),
    RGB(140, 148, 164),
    RGB(42, 45, 52),
    RGB(238, 240, 245)
};

namespace detail {
inline std::optional<bool>& ThemeDarkOverrideStorage() noexcept {
    static std::optional<bool> override_val;
    return override_val;
}
} // namespace detail

// Allow unit tests to deterministically force Light (false) or Dark (true) theme, or std::nullopt for OS setting.
inline void SetThemeDarkOverrideForTest(std::optional<bool> is_dark) noexcept {
    detail::ThemeDarkOverrideStorage() = is_dark;
}

// Query Windows 10/11 Personalize registry for Light vs Dark mode.
inline bool IsSystemDarkTheme() noexcept {
    if (const auto& ov = detail::ThemeDarkOverrideStorage(); ov.has_value()) {
        return *ov;
    }

    ScopedRegKey key;
    if (::RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0,
            KEY_READ,
            key.Put()) != ERROR_SUCCESS) {
        return false;
    }

    DWORD use_light = 1;
    DWORD cb_data = sizeof(use_light);
    if (::RegQueryValueExW(
            key.Get(),
            L"AppsUseLightTheme",
            nullptr,
            nullptr,
            reinterpret_cast<LPBYTE>(&use_light),
            &cb_data) == ERROR_SUCCESS) {
        return use_light == 0;
    }
    return false;
}

constexpr const ThemePalette& GetThemePalette(bool is_dark) noexcept {
    return is_dark ? kDarkThemePalette : kLightThemePalette;
}

inline const ThemePalette& GetCurrentThemePalette() noexcept {
    return GetThemePalette(IsSystemDarkTheme());
}

// Sync Win32 popup menus (uxtheme ordinal 135/136) and window border with Windows 10/11 dark/light theme.
inline void ApplySystemThemeToMenuAndWindow(HWND hwnd, bool is_dark) noexcept {
    if (const HMODULE uxtheme = ::GetModuleHandleW(L"uxtheme.dll")) {
        // Ordinal 135: SetPreferredAppMode(int mode): 1 = AllowDark, 2 = ForceDark, 3 = ForceLight
        using SetPreferredAppModeFn = int(WINAPI*)(int);
        // Ordinal 136: FlushMenuThemes()
        using FlushMenuThemesFn = void(WINAPI*)();

        const auto set_mode = reinterpret_cast<SetPreferredAppModeFn>(
            ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
        const auto flush_menus = reinterpret_cast<FlushMenuThemesFn>(
            ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
        if (set_mode) {
            set_mode(is_dark ? 2 : 3);
        }
        if (flush_menus) {
            flush_menus();
        }
    }
    if (hwnd) {
        if (const HMODULE dwmapi = ::GetModuleHandleW(L"dwmapi.dll")) {
            using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            const auto set_attr = reinterpret_cast<DwmSetWindowAttributeFn>(
                ::GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
            if (set_attr) {
                const BOOL dark_flag = is_dark ? TRUE : FALSE;
                constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
                set_attr(hwnd, kDwmwaUseImmersiveDarkMode, &dark_flag, sizeof(dark_flag));

                // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
                constexpr DWORD kDwmwaWindowCornerPreference = 33;
                const DWORD round_pref = 2;
                set_attr(hwnd, kDwmwaWindowCornerPreference, &round_pref, sizeof(round_pref));
            }
        }
    }
}

} // namespace qiqi
