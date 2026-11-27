// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/controls.h"
#include "common/theme.h"

#include <commctrl.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qiqi {

enum class ToolIcon {
    None,
    New,
    Reload,
    Copy,
    Import,
    Export,
    Save,
    Pin,
    Delete,
    Promote,
    Start,
    Stop,
    Pause,
    Resume,
    Diagnostics,
    Events,
    Edit,
    Metrics
};

struct ToolCommand {
    UINT id = 0; // Zero denotes a menu or toolbar separator.
    const wchar_t* text = L"";
    ToolIcon icon = ToolIcon::None;
    bool enabled = true;
    bool checked = false;
    bool radio = false;
    HFONT popup_font = nullptr; // Borrowed until the synchronous popup closes.
};

struct ToolMenu {
    const wchar_t* text;
    wchar_t mnemonic;
    std::span<const ToolCommand> commands;
    bool radio = false;
};

class ToolCommandUi final {
  public:
    using ErrorSink = void (*)(void*, const char*, ULONG) noexcept;
    enum class PreferenceSource { SavedConfig, Window };
    ToolCommandUi(ErrorSink error_sink, void* context,
        PreferenceSource preferences = PreferenceSource::SavedConfig) noexcept
        : error_sink_(error_sink), context_(context), preferences_source_(preferences) {
    }
    ToolCommandUi(const ToolCommandUi&) = delete;
    ToolCommandUi& operator=(const ToolCommandUi&) = delete;

    void InitializeToolbar(HWND toolbar, bool compact = false);
    void SetMenu(HWND window, std::span<const ToolMenu> menus, const FluentD2DThemeContext& theme);
    UINT TrackContextMenu(HWND window, POINT point, std::span<const ToolCommand> commands,
        const FluentD2DThemeContext& theme, TPMPARAMS* placement = nullptr,
        gsl::not_null<decltype(&::TrackPopupMenuEx)> track_menu = &::TrackPopupMenuEx);
    void SetToolbar(HWND toolbar, std::span<const ToolCommand> commands);
    void UpdateCommand(HWND window, const ToolCommand& command);
    int LayoutToolbar(HWND toolbar, int width, const FluentD2DThemeContext& theme) const noexcept;
    void ApplyTheme(HWND window, const FluentD2DThemeContext& theme) noexcept;
    bool MenuActive() const noexcept;
    void DrawIcon(HDC dc, const RECT& rect, ToolIcon icon, int size, int stroke, COLORREF color) const noexcept;
    std::optional<LRESULT> HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                         const FluentD2DThemeContext& theme);

  private:
    struct MenuItem {
        HMENU menu;
        UINT id;
        std::wstring text;
        wchar_t mnemonic;
        bool heading;
        bool separator;
        bool radio;
        HFONT font = nullptr;
    };
    using MenuItems = std::vector<std::unique_ptr<MenuItem>>;
    struct Toolbar {
        HWND window;
        bool compact;
    };
    void Report(const char* operation, ULONG error = ERROR_SUCCESS) const noexcept;
    [[noreturn]] void Fail(const char* operation, ULONG error) const;
    LRESULT DrawToolbar(NMCUSTOMDRAW& draw, const Toolbar& toolbar, const FluentD2DThemeContext& theme) const noexcept;
    void AppendMenuCommand(HMENU menu, const ToolCommand& command, bool radio, MenuItems& items,
        HFONT font = nullptr);
    const MenuItem* FindMenuItem(ULONG_PTR data) const noexcept;
    void RefreshMenuAppearance(HWND window, UINT dpi, const FluentD2DThemeContext& theme) noexcept;
    void ApplyMenuBackground(HMENU menu) noexcept;
    void InvalidateMenuMeasurements(const MenuItems& items) noexcept;
    int MenuScale(int px) const noexcept;
    HFONT MenuItemFont(const MenuItem& item) const noexcept;
    LRESULT MeasureMenuItem(HWND window, MEASUREITEMSTRUCT& measure, const MenuItem& item) const noexcept;
    LRESULT DrawMenuItem(const DRAWITEMSTRUCT& draw, const MenuItem& item) const noexcept;

    ErrorSink error_sink_;
    void* context_;
    PreferenceSource preferences_source_;
    ScopedGdiHandle<HBRUSH> menu_brush_;
    std::wstring tooltip_buffer_;
    MenuItems menu_items_;
    const MenuItems* context_items_ = nullptr;
    DisplayPreferences menu_preferences_;
    DisplayAppearance menu_appearance_;
    LOGFONTW menu_font_face_{};
    ScopedGdiHandle<HFONT> menu_font_;
    UINT menu_dpi_ = 96;
    UINT menu_max_height_ = 0;
    std::vector<Toolbar> toolbars_;
};

} // namespace qiqi
