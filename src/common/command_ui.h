#pragma once

#include "common/d2d_controls.h"

#include <commctrl.h>
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
    Events
};

struct ToolCommand {
    UINT id = 0; // Zero denotes a menu separator.
    const wchar_t* text = L"";
    ToolIcon icon = ToolIcon::None;
    bool enabled = true;
    bool checked = false;
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
    ToolCommandUi(ErrorSink error_sink, void* context) noexcept : error_sink_(error_sink), context_(context) {
    }
    ToolCommandUi(const ToolCommandUi&) = delete;
    ToolCommandUi& operator=(const ToolCommandUi&) = delete;

    void InitializeToolbar(HWND toolbar, bool compact = false);
    void SetMenu(HWND window, std::span<const ToolMenu> menus);
    void SetToolbar(HWND toolbar, std::span<const ToolCommand> commands);
    void UpdateCommand(HWND window, const ToolCommand& command);
    int LayoutToolbar(HWND toolbar, int width, const FluentD2DThemeContext& theme) const noexcept;
    void ApplyTheme(HWND window) noexcept;
    std::optional<LRESULT> HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam, HFONT font,
                                         const FluentD2DThemeContext& theme);

  private:
    struct Toolbar {
        HWND window;
        bool compact;
    };
    void Report(const char* operation, ULONG error = ERROR_SUCCESS) const noexcept;
    [[noreturn]] void Fail(const char* operation, ULONG error) const;
    void DrawIcon(HDC dc, const RECT& rect, ToolIcon icon, int size, int stroke, COLORREF color) const noexcept;
    LRESULT DrawToolbar(NMCUSTOMDRAW& draw, const Toolbar& toolbar, const FluentD2DThemeContext& theme) const noexcept;

    ErrorSink error_sink_;
    void* context_;
    ScopedGdiHandle<HBRUSH> menu_brush_;
    std::vector<std::wstring> headings_;
    std::vector<wchar_t> mnemonics_;
    std::vector<Toolbar> toolbars_;
};

} // namespace qiqi
