#include "ui/langbar_button.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"

#include <array>
#include <algorithm>
#include <cwchar>
#include <cstring>
#include <utility>

namespace qiqi {
namespace {

class QuickSettingsMenu final {
public:
    QuickSettingsMenu(const LangBarState& state, POINT point, const LangBarButton::ActionCallback& callback,
                      bool bottom)
        : state_(state), point_(point), callback_(callback), bottom_(bottom) {}

    void Show() {
        const HINSTANCE instance = g_dll_instance ? g_dll_instance : ::GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kMenuWndClass;
        if (!::RegisterClassW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            QIQI_LOG_ERROR("Registering quick settings menu failed: {}", ::GetLastError());
            return;
        }
        hwnd_.Reset(::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kMenuWndClass, L"QiQi Wubi",
                                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, point_.x, point_.y, 1, 1, nullptr,
                                      nullptr, instance, this));
        if (!hwnd_ || !Refresh()) {
            QIQI_LOG_ERROR("Creating quick settings menu failed: {}", ::GetLastError());
            return;
        }
        {
            const HWND previous_focus = ::GetForegroundWindow();
            const auto close_menu = gsl::finally([&]() {
                const bool restore_focus = ::GetForegroundWindow() == hwnd_.Get();
                hwnd_.Reset();
                if (restore_focus && ::IsWindow(previous_focus)) {
                    ::SetForegroundWindow(previous_focus);
                }
            });
            const auto palette = Palette();
            ApplySystemThemeToMenuAndWindow(hwnd_.Get(), palette.is_dark);
            ::ShowWindow(hwnd_.Get(), SW_SHOW);
            ::SetForegroundWindow(hwnd_.Get());
            ::SetFocus(hwnd_.Get());
            MoveFocus(1);
            MSG msg{};
            while (::IsWindowVisible(hwnd_.Get())) {
                const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
                if (result <= 0) {
                    if (result == 0) {
                        ::PostQuitMessage(gsl::narrow_cast<int>(msg.wParam));
                    } else {
                        QIQI_LOG_ERROR("Quick settings message loop failed: {}", ::GetLastError());
                    }
                    break;
                }
                if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                     msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN) &&
                    msg.hwnd != hwnd_.Get() && !::IsChild(hwnd_.Get(), msg.hwnd)) {
                    ::ShowWindow(hwnd_.Get(), SW_HIDE);
                }
                if ((msg.hwnd == hwnd_.Get() || ::IsChild(hwnd_.Get(), msg.hwnd)) && msg.message == WM_KEYDOWN) {
                    if (msg.wParam == VK_ESCAPE) {
                        break;
                    }
                    if (msg.wParam == VK_DOWN || msg.wParam == VK_UP) {
                        MoveFocus(msg.wParam == VK_DOWN ? 1 : -1);
                        continue;
                    }
                    if (msg.wParam == VK_RETURN || msg.wParam == VK_SPACE) {
                        Select(gsl::narrow_cast<UINT>(::GetDlgCtrlID(::GetFocus())));
                        continue;
                    }
                }
                if (msg.message == WM_MOUSEMOVE) {
                    const HWND hovered = ::IsChild(hwnd_.Get(), msg.hwnd) ? msg.hwnd : nullptr;
                    if (hovered != hovered_) {
                        if (hovered_) {
                            ::InvalidateRect(hovered_, nullptr, FALSE);
                        }
                        hovered_ = hovered;
                        if (hovered_) {
                            ::InvalidateRect(hovered_, nullptr, FALSE);
                        }
                    }
                }
                if (!::IsDialogMessageW(hwnd_.Get(), &msg)) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
            }
        }
        // Close before invoking an action that can open another modal window.
        if (selected_ != 0) {
            callback_(static_cast<LangBarMenuId>(selected_));
        }
    }

private:
    bool Refresh() {
        model_ = LangBarButton::BuildSharedMenuItems(state_);
        const auto monitor = QueryMonitorForPoint(point_, hwnd_.Get());
        const int count = gsl::narrow_cast<int>(model_.size());
        int base_height = 8;
        for (const auto& item : model_) {
            base_height += item.IsSeparator() ? 8 : 28;
        }
        const int available_height =
            std::max(1, gsl::narrow_cast<int>(monitor.work_area.bottom - monitor.work_area.top) - 2);
        // Fit all actions on short work areas even when the monitor uses large text scaling.
        const UINT layout_dpi = std::min(
            monitor.dpi,
            gsl::narrow_cast<UINT>(std::max(1, ::MulDiv(std::max(1, available_height - count - 2), 96, base_height))));
        const auto scale = [layout_dpi](int px) { return ScalePxByDpi(px, layout_dpi); };
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            return false;
        }
        metrics.lfMenuFont.lfHeight = -scale(14);
        ScopedGdiHandle<HFONT> font;
        if (!font_ || std::memcmp(&font_description_, &metrics.lfMenuFont, sizeof(LOGFONTW)) != 0) {
            font.Reset(::CreateFontIndirectW(&metrics.lfMenuFont));
            if (!font) {
                return false;
            }
        }
        int width = scale(240);
        int y = scale(4);
        std::array<RECT, std::tuple_size_v<LangBarButton::MenuItems>> rows{};
        {
            const ScopedWindowDC dc(hwnd_.Get());
            if (!dc) {
                return false;
            }
            const ScopedSelectObject select_font(dc.Get(), font ? font.Get() : font_.Get());
            for (int i = 0; i < count; ++i) {
                const auto& item = model_.at(gsl::narrow_cast<size_t>(i));
                const int height = scale(item.IsSeparator() ? 8 : 28);
                rows.at(gsl::narrow_cast<size_t>(i)) = RECT{0, y, 0, y + height};
                y += height;
                if (item.IsSeparator()) {
                    continue;
                }
                SIZE text_size{};
                if (!::GetTextExtentPoint32W(dc.Get(), item.text, gsl::narrow_cast<int>(wcslen(item.text)), &text_size)) {
                    return false;
                }
                width = std::max(width, gsl::narrow_cast<int>(text_size.cx) + scale(60));
            }
        }
        for (const auto& item : model_) {
            if (item.IsSeparator()) {
                continue;
            }
            const UINT id = static_cast<UINT>(item.id);
            HWND button = ::GetDlgItem(hwnd_.Get(), gsl::narrow_cast<int>(id));
            if (!button) {
                button = ::CreateWindowExW(
                    0, L"BUTTON", item.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, hwnd_.Get(),
                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                    reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hwnd_.Get(), GWLP_HINSTANCE)), nullptr);
                if (!button) {
                    return false;
                }
            }
        }
        // Retain the old font until every child has switched to the new one.
        if (font) {
            std::swap(font_, font);
            font_description_ = metrics.lfMenuFont;
        }
        rows_ = rows;
        const int height = y + scale(4);
        width = std::min(width, gsl::narrow_cast<int>(monitor.work_area.right - monitor.work_area.left) - 2);
        RECT outer{0, 0, width, height};
        ::AdjustWindowRectEx(&outer, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        POINT position = point_;
        if (bottom_) {
            position.y -= outer.bottom - outer.top;
        }
        position =
            ClampPointToWorkArea(position, outer.right - outer.left, outer.bottom - outer.top, monitor.work_area);
        ::SetWindowPos(hwnd_.Get(), nullptr, position.x, position.y, outer.right - outer.left, outer.bottom - outer.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        for (int i = 0; i < count; ++i) {
            const RECT row = rows.at(gsl::narrow_cast<size_t>(i));
            const auto& item = model_.at(gsl::narrow_cast<size_t>(i));
            if (!item.IsSeparator()) {
                const HWND button = ::GetDlgItem(hwnd_.Get(), static_cast<int>(item.id));
                ::SetWindowTextW(button, item.text);
                ::SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font_.Get()), FALSE);
                ::EnableWindow(button, item.enabled);
                ::MoveWindow(button, 0, row.top, width, row.bottom - row.top, TRUE);
            }
        }
        ::RedrawWindow(hwnd_.Get(), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return true;
    }

    const LangBarMenuItem* FindItem(UINT id) const noexcept {
        const auto found = std::ranges::find(model_, static_cast<LangBarMenuId>(id), &LangBarMenuItem::id);
        return found == model_.end() ? nullptr : &*found;
    }

    void Select(UINT id) {
        const auto* item = FindItem(id);
        if (!item || item->IsSeparator() || !item->enabled) {
            return;
        }
        const auto action = static_cast<LangBarMenuId>(id);
        if (LangBarButton::KeepsMenuOpen(action)) {
            callback_(action);
            if (!Refresh()) {
                QIQI_LOG_ERROR("Refreshing quick settings menu failed: {}", ::GetLastError());
                ::ShowWindow(hwnd_.Get(), SW_HIDE);
            }
        } else {
            selected_ = id;
            ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
    }

    void MoveFocus(int step) const {
        const int count = gsl::narrow_cast<int>(model_.size());
        const UINT current = gsl::narrow_cast<UINT>(::GetDlgCtrlID(::GetFocus()));
        int index = step > 0 ? -1 : 0;
        if (::IsChild(hwnd_.Get(), ::GetFocus())) {
            for (int i = 0; i < count; ++i) {
                if (static_cast<UINT>(model_.at(gsl::narrow_cast<size_t>(i)).id) == current) {
                    index = i;
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            index = (index + step + count) % count;
            const auto& item = model_.at(gsl::narrow_cast<size_t>(index));
            if (!item.IsSeparator() && item.enabled) {
                ::SetFocus(::GetDlgItem(hwnd_.Get(), static_cast<int>(item.id)));
                return;
            }
        }
    }

    static ThemePalette Palette() noexcept {
        ThemePalette palette = GetTaskbarThemePalette();
        palette.text_accent = palette.text_primary;
        HIGHCONTRASTW contrast{};
        contrast.cbSize = sizeof(contrast);
        if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            palette.window_bg = ::GetSysColor(COLOR_MENU);
            palette.text_primary = ::GetSysColor(COLOR_MENUTEXT);
            palette.text_accent = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
            palette.hover_bg = ::GetSysColor(COLOR_HIGHLIGHT);
            palette.border_color = ::GetSysColor(COLOR_MENUTEXT);
        }
        return palette;
    }

    void DrawItem(const DRAWITEMSTRUCT& item) const {
        const int saved_dc = ::SaveDC(item.hDC);
        if (!saved_dc) {
            QIQI_LOG_ERROR("Saving quick settings drawing context failed: {}", ::GetLastError());
            return;
        }
        const auto restore_dc = gsl::finally([&]() { ::RestoreDC(item.hDC, saved_dc); });
        const auto palette = Palette();
        const bool selected = (item.itemState & (ODS_SELECTED | ODS_FOCUS)) || item.hwndItem == hovered_;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(selected ? palette.hover_bg : palette.window_bg));
        ::FillRect(item.hDC, &item.rcItem, background.Get());
        ::SetBkMode(item.hDC, TRANSPARENT);
        ::SetTextColor(item.hDC, disabled ? ::GetSysColor(COLOR_GRAYTEXT)
                                          : (selected ? palette.text_accent : palette.text_primary));
        const ScopedSelectObject select_font(item.hDC, font_.Get());
        RECT text_rect = item.rcItem;
        const int gutter = item.rcItem.bottom - item.rcItem.top;
        text_rect.left += gutter + gutter / 4;
        text_rect.right -= gutter / 2;
        std::array<wchar_t, 256> text{};
        ::GetWindowTextW(item.hwndItem, text.data(), gsl::narrow_cast<int>(text.size()));
        ::DrawTextW(item.hDC, text.data(), -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        const auto* menu_item = FindItem(item.CtlID);
        if (menu_item && menu_item->checked) {
            RECT check = item.rcItem;
            check.right = check.left + gutter;
            ::DrawTextW(item.hDC, L"\x2713", 1, &check, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        if (item.itemState & ODS_FOCUS) {
            ::DrawFocusRect(item.hDC, &item.rcItem);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<QuickSettingsMenu*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) {
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        switch (msg) {
            case WM_COMMAND:
                if (HIWORD(wparam) == BN_CLICKED) {
                    self->Select(LOWORD(wparam));
                }
                return 0;
            case WM_DRAWITEM:
                if (lparam) {
                    self->DrawItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
                }
                return TRUE;
            case WM_ACTIVATE:
                if (LOWORD(wparam) == WA_INACTIVE) {
                    ::ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
            case WM_CLOSE:
                ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED:
                ApplySystemThemeToMenuAndWindow(hwnd, Palette().is_dark);
                ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                const ScopedPaintDC dc(hwnd);
                RECT rect{};
                ::GetClientRect(hwnd, &rect);
                const auto palette = Palette();
                const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(palette.window_bg));
                const ScopedGdiHandle<HBRUSH> line(::CreateSolidBrush(palette.border_color));
                ::FillRect(dc.Get(), &rect, background.Get());
                for (size_t i = 0; i < self->model_.size(); ++i) {
                    if (!self->model_.at(i).IsSeparator()) {
                        continue;
                    }
                    const RECT row = self->rows_.at(i);
                    const int y = (row.top + row.bottom) / 2;
                    const RECT separator{rect.left + 8, y, rect.right - 8, y + 1};
                    ::FillRect(dc.Get(), &separator, line.Get());
                }
                return 0;
            }
            default:
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    const LangBarState& state_;
    POINT point_;
    const LangBarButton::ActionCallback callback_;
    bool bottom_;
    LangBarButton::MenuItems model_{};
    ScopedGdiHandle<HFONT> font_;
    LOGFONTW font_description_{};
    std::array<RECT, std::tuple_size_v<LangBarButton::MenuItems>> rows_{};
    HWND hovered_ = nullptr;
    UINT selected_ = 0;
    // Destroy the window and its controls before their font and drawing state.
    ScopedWindow hwnd_;
};

} // namespace

void LangBarButton::ShowSharedPopupMenu(const LangBarState& state, POINT screen_pt, const ActionCallback& callback,
                                        bool align_bottom) {
    if (callback) {
        QuickSettingsMenu(state, screen_pt, callback, align_bottom).Show();
    }
}

} // namespace qiqi
