#pragma once
#include <windows.h>
#include <functional>

#include "common/scoped_handle.h"
#include "ui/langbar_button.h"

namespace qiqi {

// Compact floating desktop IME status bar (disabled by default via config.show_status_bar = false).
// Displays [QiQi | 中/英 | 半/全 | 。/. | 设] and allows clicking buttons or dragging.
class StatusBarUI final {
public:
    using ActionCallback = std::function<void(LangBarMenuId action_id)>;

    static constexpr int kBarWidth = 176;
    static constexpr int kBarHeight = 32;

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
    HWND WindowHandle() const noexcept { return hwnd_; }

private:
    bool EnsureBarWindow() noexcept;
    void PaintBar(HDC hdc, const RECT& client_rect) const;
    void OnLeftButtonDown(POINT pt) const;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ScopedGdiHandle<HFONT> font_;
    bool visible_ = false;
    POINT pos_{0, 0};
    bool pos_initialized_ = false;
    LangBarState state_{};
    ActionCallback action_callback_;
};

} // namespace qiqi
