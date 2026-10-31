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
    return IsTaskbarDarkTheme() ? RGB(255, 255, 255) : RGB(0, 0, 0);
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
    if (hwnd && ::IsWindow(hwnd)) {
        static const HMODULE dwmapi = ::LoadLibraryW(L"dwmapi.dll");
        if (dwmapi) {
            using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            const auto set_attr = reinterpret_cast<DwmSetWindowAttributeFn>(
                ::GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
            if (set_attr) {
                const BOOL dark_flag = is_dark ? TRUE : FALSE;
                constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
                constexpr DWORD kDwmwaUseImmersiveDarkModeOld = 19;
                if (FAILED(set_attr(hwnd, kDwmwaUseImmersiveDarkMode, &dark_flag, sizeof(dark_flag)))) {
                    set_attr(hwnd, kDwmwaUseImmersiveDarkModeOld, &dark_flag, sizeof(dark_flag));
                }

                // Explicitly set caption and border color so dark title bar is guaranteed even on older Win10/11 builds
                constexpr DWORD kDwmwaBorderColor = 34;
                constexpr DWORD kDwmwaCaptionColor = 35;
                constexpr DWORD kDwmwaTextColor = 36;
                const COLORREF cap_color = is_dark ? RGB(32, 34, 38) : RGB(243, 245, 249);
                const COLORREF txt_color = is_dark ? RGB(238, 240, 245) : RGB(28, 32, 40);
                const COLORREF bdr_color = is_dark ? RGB(56, 60, 68) : RGB(212, 218, 228);
                set_attr(hwnd, kDwmwaCaptionColor, &cap_color, sizeof(cap_color));
                set_attr(hwnd, kDwmwaTextColor, &txt_color, sizeof(txt_color));
                set_attr(hwnd, kDwmwaBorderColor, &bdr_color, sizeof(bdr_color));

                // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
                constexpr DWORD kDwmwaWindowCornerPreference = 33;
                constexpr DWORD round_pref = 2;
                set_attr(hwnd, kDwmwaWindowCornerPreference, &round_pref, sizeof(round_pref));
            }
        }
    }
}

} // namespace qiqi
