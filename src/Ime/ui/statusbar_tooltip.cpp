#include "Ime/ui/statusbar.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"

#include <gsl/gsl>
#include <algorithm>

namespace qiqi {

bool StatusBarUI::EnsureTooltipWindow() noexcept {
    if (tooltip_hwnd_ && ::IsWindow(tooltip_hwnd_.Get())) {
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &StatusBarUI::TooltipWndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1);
    wc.lpszClassName = kStatusBarTooltipWndClass;
    ::RegisterClassExW(&wc);

    tooltip_hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kStatusBarTooltipWndClass,
        L"",
        WS_POPUP | WS_BORDER,
        pos_.x, pos_.y - 28, 160, 24,
        hwnd_.Get(), nullptr, instance_, this));
    if (!tooltip_hwnd_) {
        QIQI_LOG_ERROR("StatusBarUI::EnsureTooltipWindow CreateWindowExW failed (error={})", ::GetLastError());
    }
    return tooltip_hwnd_.IsValid();
}

void StatusBarUI::UpdateTooltipForZone(int zone, int client_x) {
    if (zone < 0) {
        HideTooltip();
        return;
    }
    active_tooltip_text_ = GetTooltipTextForX(client_x);
    if (active_tooltip_text_.empty() || !EnsureTooltipWindow()) {
        return;
    }

    int tip_w = ScaleByDpi(180);
    const ScopedWindowDC dc(tooltip_hwnd_.Get());
    if (dc) {
        const ScopedSelectObject sel_font(dc.Get(), tooltip_font_.Get());
        SIZE sz{};
        if (::GetTextExtentPoint32W(
                dc.Get(),
                active_tooltip_text_.c_str(),
                gsl::narrow_cast<int>(active_tooltip_text_.size()),
                &sz)) {
            tip_w = std::max<int>(ScaleByDpi(80), sz.cx + ScaleByDpi(16));
        }
    }

    RECT bar_rc{};
    if (hwnd_) {
        ::GetWindowRect(hwnd_.Get(), &bar_rc);
    } else {
        bar_rc = {pos_.x, pos_.y, pos_.x + CurrentWidth(), pos_.y + CurrentHeight()};
    }
    const MonitorMetrics mon = QueryMonitorForPoint(POINT{bar_rc.left, bar_rc.top}, hwnd_.Get());
    const int tip_h = ScaleByDpi(24);
    const int gap = ScaleByDpi(4);
    POINT tip_pos{
        bar_rc.left,
        (bar_rc.top - tip_h - gap >= mon.work_area.top) ? (bar_rc.top - tip_h - gap) : (bar_rc.bottom + gap)
    };
    tip_pos = ClampPointToWorkArea(tip_pos, tip_w, tip_h, mon.work_area);

    ::SetWindowPos(
        tooltip_hwnd_.Get(),
        HWND_TOPMOST,
        tip_pos.x,
        tip_pos.y,
        tip_w,
        tip_h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::InvalidateRect(tooltip_hwnd_.Get(), nullptr, TRUE);
    ::UpdateWindow(tooltip_hwnd_.Get());
}

void StatusBarUI::HideTooltip() noexcept {
    active_tooltip_text_.clear();
    if (tooltip_hwnd_ && ::IsWindow(tooltip_hwnd_.Get())) {
        ::ShowWindow(tooltip_hwnd_.Get(), SW_HIDE);
    }
}

void StatusBarUI::PaintTooltip(HDC hdc, const RECT& client_rect) const noexcept {
    const ScopedSaveDC save_dc(hdc);
    const auto& palette = GetCurrentThemePalette();
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(palette.tooltip_bg));
    ::FillRect(hdc, &client_rect, bg_brush.Get());

    const ScopedSelectObject sel_font(hdc, tooltip_font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, palette.tooltip_text);
    RECT text_rc = client_rect;
    text_rc.left += 6;
    text_rc.right -= 6;
    ::DrawTextW(
        hdc,
        active_tooltip_text_.c_str(),
        gsl::narrow_cast<int>(active_tooltip_text_.size()),
        &text_rc,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK StatusBarUI::TooltipWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    const auto* self = BindOrGetWindowUserData<const StatusBarUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [self](HDC hdc, const RECT& rc) noexcept { self->PaintTooltip(hdc, rc); });
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace qiqi
