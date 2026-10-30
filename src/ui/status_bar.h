#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <functional>
#include <string>

#include "common/scoped_handle.h"
#include "common/win32_ui_util.h"
#include "ui/langbar_button.h"

namespace qiqi {

enum class StatusBarIconKind : size_t {
    HalfWidthMoon = 0,
    FullWidthSun,
    ChinesePunct,
    EnglishPunct,
    SettingsGear,
    Count
};

// Compact floating desktop IME status/language bar (disabled by default via config.show_status_bar = false).
// Layout: [ | Grip(22) | 中/英(36) | ☾/●(36) | °，/.,(36) | ⚙(36) ]
// Supports leftmost vertical pill gripper dragging, per-button tooltips, right-click context menu, and system Light/Dark theme.
class StatusBarUI final {
public:
    using ActionCallback = std::function<void(LangBarMenuId action_id)>;
    using PositionChangedCallback = std::function<void(POINT new_pos)>;

    static constexpr int kGripWidth = 22;
    static constexpr int kButtonsOffset = kGripWidth; // 22
    static constexpr int kCellWidth = 36;
    static constexpr int kCellCount = 4;
    static constexpr int kBarWidth = kButtonsOffset + kCellWidth * kCellCount; // 166
    static constexpr int kBarHeight = 36;
    static constexpr size_t kIconCount = static_cast<size_t>(StatusBarIconKind::Count);

    // Evaluate whether normalized point (nx, ny) in [-1, 1]^2 lies inside the target icon shape.
    static bool HitTestStatusBarIcon(StatusBarIconKind kind, double nx, double ny) noexcept;

    explicit StatusBarUI(HINSTANCE instance, ActionCallback cb = {}) noexcept;
    ~StatusBarUI() noexcept;

    StatusBarUI(const StatusBarUI&) = delete;
    StatusBarUI& operator=(const StatusBarUI&) = delete;
    StatusBarUI(StatusBarUI&&) = delete;
    StatusBarUI& operator=(StatusBarUI&&) = delete;

    void Show(bool visible) noexcept;
    bool IsVisible() const noexcept { return visible_; }
    void UpdateState(const LangBarState& state) noexcept;
    void DestroyBarWindow() noexcept;

    HWND WindowHandle() const noexcept { return hwnd_.Get(); }
    HWND TooltipWindowHandle() const noexcept { return tooltip_hwnd_.Get(); }
    POINT Position() const noexcept { return pos_; }
    void SetPosition(POINT new_pos) noexcept;
    void SetPositionChangedCallback(PositionChangedCallback cb) noexcept {
        pos_changed_callback_ = std::move(cb);
    }
    UINT CurrentDpi() const noexcept { return dpi_; }
    int CurrentWidth() const noexcept { return ScaleByDpi(kBarWidth); }
    int CurrentHeight() const noexcept { return ScaleByDpi(kBarHeight); }
    int HoveredZone() const noexcept { return hovered_zone_; }
    const std::wstring& ActiveTooltipText() const noexcept { return active_tooltip_text_; }

    // Return localized tooltip text for a client X coordinate on the status bar.
    const wchar_t* GetTooltipTextForX(int client_x) const noexcept;

    // Build a themed Win32 popup context menu matching the current state and UI language.
    ScopedMenu BuildContextMenu(ScopedGdiHandle<HBRUSH>& out_bg_brush) const noexcept;

    HBITMAP GetIconBitmap(StatusBarIconKind kind) const noexcept;

private:
    int ScaleByDpi(int px) const noexcept;
    void UpdateDpiAndFonts(UINT new_dpi) noexcept;
    void RebuildIconBitmaps(COLORREF fg_color) noexcept;
    void ClampToCurrentMonitor() noexcept;
    int HitTestZone(int client_x) const noexcept;
    bool EnsureBarWindow() noexcept;
    bool EnsureTooltipWindow() noexcept;
    bool IsDraggableBlankPoint(POINT client_pt) const noexcept;
    void UpdateTooltipForZone(int zone, int client_x);
    void HideTooltip() noexcept;
    void PaintBar(HDC hdc, const RECT& client_rect) noexcept;
    void PaintTooltip(HDC hdc, const RECT& client_rect) const noexcept;
    void OnMouseMove(POINT pt);
    void OnMouseLeave() noexcept;
    void OnLeftButtonDown(POINT pt) noexcept;
    void OnLeftButtonUp(POINT pt);
    void EndDragTracking(bool clamp_position) noexcept;
    void OnRightButtonUp(POINT client_pt) const;
    void OnDpiChanged(UINT new_dpi, const RECT* suggested_rect) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;

    HINSTANCE instance_ = nullptr;
    ScopedGdiHandle<HFONT> font_;
    ScopedGdiHandle<HFONT> tooltip_font_;
    std::array<ScopedGdiHandle<HBITMAP>, kIconCount> icon_bitmaps_;
    int icon_px_ = 0;
    COLORREF icon_fg_color_ = CLR_INVALID;
    UINT dpi_ = 96;
    bool visible_ = false;
    POINT pos_{0, 0};
    bool pos_initialized_ = false;
    int hovered_zone_ = -1; // -1 = none, 0 = grip/logo, 1..kCellCount = cells 0..kCellCount-1
    bool tracking_mouse_ = false;
    bool left_button_down_ = false;
    bool is_dragging_ = false;
    POINT drag_start_screen_{0, 0};
    POINT drag_start_pos_{0, 0};
    std::wstring active_tooltip_text_;
    LangBarState state_{};
    ActionCallback action_callback_;
    PositionChangedCallback pos_changed_callback_;
    // Destroy tooltip and bar windows first before releasing fonts, bitmaps, and state.
    ScopedWindow hwnd_;
    ScopedWindow tooltip_hwnd_;
};

} // namespace qiqi
