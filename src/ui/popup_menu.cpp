#include "ui/langbar_button.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"

#include <array>
#include <string>
#include <vector>

namespace qiqi {
namespace {

class QuickSettingsMenu final {
  public:
    QuickSettingsMenu(const LangBarState &state, POINT point, const LangBarButton::ActionCallback &callback,
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
        const HWND previous_focus = ::GetForegroundWindow();
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
            if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN ||
                 msg.message == WM_MBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN ||
                 msg.message == WM_NCRBUTTONDOWN) &&
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
                    if (hovered_)
                        ::InvalidateRect(hovered_, nullptr, FALSE);
                    hovered_ = hovered;
                    if (hovered_)
                        ::InvalidateRect(hovered_, nullptr, FALSE);
                }
            }
            if (!::IsDialogMessageW(hwnd_.Get(), &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        // Close before invoking an action that can open another modal window.
        const bool restore_focus = ::GetForegroundWindow() == hwnd_.Get();
        hwnd_.Reset();
        if (restore_focus && ::IsWindow(previous_focus)) {
            ::SetForegroundWindow(previous_focus);
        }
        if (selected_ != 0) {
            callback_(static_cast<LangBarMenuId>(selected_));
        }
    }

  private:
    bool Refresh() {
        ScopedGdiHandle<HBRUSH> brush;
        ScopedMenu model = LangBarButton::BuildSharedPopupMenu(state_, nullptr, brush);
        if (!model)
            return false;
        model_ = std::move(model);
        const auto monitor = QueryMonitorForPoint(point_, hwnd_.Get());
        const int count = ::GetMenuItemCount(model_.Get());
        int base_height = 8;
        for (int i = 0; i < count; ++i) {
            const UINT flags = ::GetMenuState(model_.Get(), gsl::narrow_cast<UINT>(i), MF_BYPOSITION);
            base_height += (flags & MF_SEPARATOR) ? 8 : 28;
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
        ScopedGdiHandle<HFONT> font(::CreateFontIndirectW(&metrics.lfMenuFont));
        if (!font)
            return false;
        const ScopedWindowDC dc(hwnd_.Get());
        const ScopedSelectObject select_font(dc.Get(), font.Get());
        int width = scale(240);
        int y = scale(4);
        std::vector<RECT> rows;
        for (int i = 0; i < count; ++i) {
            const UINT id = ::GetMenuItemID(model_.Get(), i);
            const UINT flags = ::GetMenuState(model_.Get(), gsl::narrow_cast<UINT>(i), MF_BYPOSITION);
            const int height = scale((flags & MF_SEPARATOR) ? 8 : 28);
            rows.push_back(RECT{0, y, 0, y + height});
            y += height;
            if (flags & MF_SEPARATOR)
                continue;
            std::array<wchar_t, 256> text{};
            ::GetMenuStringW(model_.Get(), id, text.data(), gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND);
            SIZE text_size{};
            ::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(wcslen(text.data())), &text_size);
            width = std::max(width, gsl::narrow_cast<int>(text_size.cx) + scale(60));
            HWND button = ::GetDlgItem(hwnd_.Get(), gsl::narrow_cast<int>(id));
            if (!button) {
                button = ::CreateWindowExW(0, L"BUTTON", text.data(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                           0, 0, 1, 1, hwnd_.Get(), reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                           g_dll_instance, nullptr);
                if (!button)
                    return false;
            }
            ::SetWindowTextW(button, text.data());
            ::SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font.Get()), FALSE);
            ::EnableWindow(button, (flags & (MF_DISABLED | MF_GRAYED)) == 0);
        }
        font_ = std::move(font);
        const int height = y + scale(4);
        width = std::min(width, gsl::narrow_cast<int>(monitor.work_area.right - monitor.work_area.left) - 2);
        RECT outer{0, 0, width, height};
        ::AdjustWindowRectEx(&outer, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        POINT position = point_;
        if (bottom_)
            position.y -= outer.bottom - outer.top;
        position =
            ClampPointToWorkArea(position, outer.right - outer.left, outer.bottom - outer.top, monitor.work_area);
        ::SetWindowPos(hwnd_.Get(), nullptr, position.x, position.y, outer.right - outer.left, outer.bottom - outer.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        separators_.clear();
        for (int i = 0; i < count; ++i) {
            const RECT row = rows.at(gsl::narrow_cast<size_t>(i));
            const UINT flags = ::GetMenuState(model_.Get(), gsl::narrow_cast<UINT>(i), MF_BYPOSITION);
            if (flags & MF_SEPARATOR) {
                separators_.push_back((row.top + row.bottom) / 2);
            } else {
                const UINT id = ::GetMenuItemID(model_.Get(), i);
                ::MoveWindow(::GetDlgItem(hwnd_.Get(), gsl::narrow_cast<int>(id)), 0, row.top, width,
                             row.bottom - row.top, TRUE);
            }
        }
        ::RedrawWindow(hwnd_.Get(), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return true;
    }

    void Select(UINT id) {
        const UINT flags = ::GetMenuState(model_.Get(), id, MF_BYCOMMAND);
        if (flags == static_cast<UINT>(-1) || (flags & (MF_DISABLED | MF_GRAYED | MF_SEPARATOR)))
            return;
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
        const int count = ::GetMenuItemCount(model_.Get());
        const UINT current = gsl::narrow_cast<UINT>(::GetDlgCtrlID(::GetFocus()));
        int index = step > 0 ? -1 : 0;
        if (::IsChild(hwnd_.Get(), ::GetFocus())) {
            for (int i = 0; i < count; ++i) {
                if (::GetMenuItemID(model_.Get(), i) == current)
                    index = i;
            }
        }
        for (int i = 0; i < count; ++i) {
            index = (index + step + count) % count;
            const UINT flags = ::GetMenuState(model_.Get(), gsl::narrow_cast<UINT>(index), MF_BYPOSITION);
            if (!(flags & (MF_SEPARATOR | MF_DISABLED | MF_GRAYED))) {
                ::SetFocus(::GetDlgItem(hwnd_.Get(), gsl::narrow_cast<int>(::GetMenuItemID(model_.Get(), index))));
                return;
            }
        }
    }

    static ThemePalette Palette() noexcept {
        ThemePalette palette = GetCurrentThemePalette();
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

    void DrawItem(const DRAWITEMSTRUCT &item) const {
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
        if (::GetMenuState(model_.Get(), item.CtlID, MF_BYCOMMAND) & MF_CHECKED) {
            RECT check = item.rcItem;
            check.right = check.left + gutter;
            ::DrawTextW(item.hDC, L"\x2713", 1, &check, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        if (item.itemState & ODS_FOCUS)
            ::DrawFocusRect(item.hDC, &item.rcItem);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_NCCREATE) {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *self = reinterpret_cast<QuickSettingsMenu *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        switch (msg) {
            case WM_COMMAND:
                if (HIWORD(wparam) == BN_CLICKED)
                    self->Select(LOWORD(wparam));
                return 0;
            case WM_DRAWITEM:
                if (lparam)
                    self->DrawItem(*reinterpret_cast<const DRAWITEMSTRUCT *>(lparam));
                return TRUE;
            case WM_ACTIVATE:
                if (LOWORD(wparam) == WA_INACTIVE)
                    ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            case WM_CLOSE:
                ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED:
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
                for (const int y : self->separators_) {
                    const RECT separator{rect.left + 8, y, rect.right - 8, y + 1};
                    ::FillRect(dc.Get(), &separator, line.Get());
                }
                return 0;
            }
            default:
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    const LangBarState &state_;
    POINT point_;
    const LangBarButton::ActionCallback &callback_;
    bool bottom_;
    ScopedWindow hwnd_;
    ScopedMenu model_;
    ScopedGdiHandle<HFONT> font_;
    std::vector<int> separators_;
    HWND hovered_ = nullptr;
    UINT selected_ = 0;
};

} // namespace

void LangBarButton::ShowSharedPopupMenu(const LangBarState &state, POINT screen_pt, const ActionCallback &callback,
                                        bool align_bottom) {
    if (callback) {
        QuickSettingsMenu(state, screen_pt, callback, align_bottom).Show();
    }
}

} // namespace qiqi
