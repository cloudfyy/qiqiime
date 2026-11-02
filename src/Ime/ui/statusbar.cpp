#include "Ime/ui/statusbar.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cmath>

namespace qiqi {

namespace {

using SelectTextFn = const wchar_t*(*)(const LangBarState& st, const UiStrings& str) noexcept;
using SelectIconFn = StatusBarIconKind(*)(const LangBarState& st) noexcept;

struct StatusBarCellDescriptor {
    LangBarMenuId click_action;
    const wchar_t* UiStrings::* tooltip_field;
    SelectTextFn select_text;
    SelectIconFn select_icon;
};

constexpr const wchar_t* SelectModeCellText(const LangBarState& st, const UiStrings& str) noexcept {
    return st.chinese_mode ? str.status_mode_cn : str.status_mode_en;
}

constexpr StatusBarIconKind SelectWidthCellIcon(const LangBarState& st) noexcept {
    return st.full_width ? StatusBarIconKind::FullWidthSun : StatusBarIconKind::HalfWidthMoon;
}

constexpr StatusBarIconKind SelectPunctCellIcon(const LangBarState& st) noexcept {
    return st.chinese_punct ? StatusBarIconKind::ChinesePunct : StatusBarIconKind::EnglishPunct;
}

constexpr StatusBarIconKind SelectSettingsCellIcon(const LangBarState&) noexcept {
    return StatusBarIconKind::SettingsGear;
}

// Table-driven descriptor table for all interactive toolbar cells.
constexpr std::array<StatusBarCellDescriptor, StatusBarUI::kCellCount> kStatusBarCells = {{
    {LangBarMenuId::ToggleChinese,     &UiStrings::tip_mode,     &SelectModeCellText, nullptr},
    {LangBarMenuId::ToggleFullWidth,   &UiStrings::tip_width,    nullptr,             &SelectWidthCellIcon},
    {LangBarMenuId::TogglePunctuation, &UiStrings::tip_punct,    nullptr,             &SelectPunctCellIcon},
    {LangBarMenuId::OpenSettings,      &UiStrings::tip_settings, nullptr,             &SelectSettingsCellIcon},
}};

consteval bool AllStatusBarCellsValid() noexcept {
    for (const auto& cell : kStatusBarCells) {
        if (cell.tooltip_field == nullptr) {
            return false;
        }
        if ((cell.select_text == nullptr) == (cell.select_icon == nullptr)) {
            return false;
        }
    }
    return true;
}
static_assert(AllStatusBarCellsValid(), "Every StatusBarCellDescriptor must bind a tooltip and either text or icon");

void DrawAlphaBitmapCentered(HDC hdc, HBITMAP bmp, int bmp_size, const RECT& cell_rc) noexcept {
    if (!hdc || !bmp || bmp_size <= 0) {
        return;
    }
    const ScopedMemoryDC mem_dc(::CreateCompatibleDC(hdc));
    if (!mem_dc) {
        return;
    }
    const ScopedSelectObject sel_bmp(mem_dc.Get(), bmp);

    const int dst_x = cell_rc.left + ((cell_rc.right - cell_rc.left) - bmp_size) / 2;
    const int dst_y = cell_rc.top + ((cell_rc.bottom - cell_rc.top) - bmp_size) / 2;

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    ::GdiAlphaBlend(
        hdc, dst_x, dst_y, bmp_size, bmp_size,
        mem_dc.Get(), 0, 0, bmp_size, bmp_size, bf);
}

} // namespace

StatusBarUI::StatusBarUI(HINSTANCE instance, ActionCallback cb) noexcept
    : instance_(instance), action_callback_(std::move(cb)) {
    UpdateDpiAndFonts(96);
}

StatusBarUI::~StatusBarUI() noexcept {
    std::ignore = DestroyBarWindow();
}

int StatusBarUI::ScaleByDpi(int px) const noexcept {
    return ScalePxByDpi(px, dpi_);
}

void StatusBarUI::UpdateDpiAndFonts(UINT new_dpi) noexcept {
    const UINT effective_dpi = new_dpi > 0 ? new_dpi : 96u;
    if (font_.IsValid() && tooltip_font_.IsValid() && dpi_ == effective_dpi) {
        return;
    }
    dpi_ = effective_dpi;

    font_ = CreateUiFont(ScaleByDpi(18), FW_MEDIUM);
    tooltip_font_ = CreateUiFont(ScaleByDpi(13), FW_NORMAL);

    RebuildIconBitmaps(GetCurrentThemePalette().text_primary);
}

void StatusBarUI::SetPosition(POINT new_pos) noexcept {
    pos_ = new_pos;
    pos_initialized_ = true;
    ClampToCurrentMonitor();
}

void StatusBarUI::ClampToCurrentMonitor() noexcept {
    const MonitorMetrics mon = QueryMonitorForPoint(pos_, hwnd_.Get());
    if (mon.dpi != dpi_) {
        UpdateDpiAndFonts(mon.dpi);
    }
    pos_ = ClampPointToWorkArea(pos_, CurrentWidth(), CurrentHeight(), mon.work_area);
    if (hwnd_ && ::IsWindow(hwnd_.Get())) {
        ::SetWindowPos(
            hwnd_.Get(),
            HWND_TOPMOST,
            pos_.x,
            pos_.y,
            CurrentWidth(),
            CurrentHeight(),
            SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

void StatusBarUI::Show(bool visible) noexcept {
    visible = visible && presentation_enabled_ && !destroying_window_;
    visible_ = visible;
    if (visible) {
        if (EnsureBarWindow()) {
            ApplySystemThemeToMenuAndWindow(hwnd_.Get(), IsSystemDarkTheme());
            ::ShowWindow(hwnd_.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
        }
    } else {
        HideTooltip();
        if (hwnd_) {
            ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
    }
}

void StatusBarUI::UpdateState(const LangBarState& state) noexcept {
    state_ = state;
    Show(state.show_status_bar);
    if (visible_ && hwnd_) {
        ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
    }
}

bool StatusBarUI::EnsureBarWindow() noexcept {
    if (!presentation_enabled_ || destroying_window_) return false;
    if (hwnd_ && ::IsWindow(hwnd_.Get())) {
        return true;
    }

    if (!pos_initialized_) {
        const MonitorMetrics active_mon = QueryActiveMonitor();
        UpdateDpiAndFonts(active_mon.dpi);
        pos_.x = active_mon.work_area.right - CurrentWidth() - ScaleByDpi(24);
        pos_.y = active_mon.work_area.bottom - CurrentHeight() - ScaleByDpi(24);
        pos_ = ClampPointToWorkArea(pos_, CurrentWidth(), CurrentHeight(), active_mon.work_area);
        pos_initialized_ = true;
    } else {
        const MonitorMetrics mon = QueryMonitorForPoint(pos_);
        UpdateDpiAndFonts(mon.dpi);
        pos_ = ClampPointToWorkArea(pos_, CurrentWidth(), CurrentHeight(), mon.work_area);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &StatusBarUI::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kStatusBarWndClass;
    ::RegisterClassExW(&wc);

    hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kStatusBarWndClass,
        L"",
        WS_POPUP,
        pos_.x, pos_.y, CurrentWidth(), CurrentHeight(),
        nullptr, nullptr, instance_, this));
    if (hwnd_) {
        ApplySystemThemeToMenuAndWindow(hwnd_.Get(), IsSystemDarkTheme());
        QIQI_LOG_DEBUG("StatusBarUI window created (pos=({}, {}), size={}x{}, dpi={})",
                       pos_.x, pos_.y, CurrentWidth(), CurrentHeight(), dpi_);
    } else {
        QIQI_LOG_ERROR("StatusBarUI::EnsureBarWindow CreateWindowExW failed (error={})", ::GetLastError());
    }
    return hwnd_.IsValid();
}

bool StatusBarUI::DestroyBarWindow() noexcept {
    if (destroying_window_) return true;
    if (!CheckWindowOwnerThread(hwnd_.Get(), "StatusBarUI::DestroyBarWindow") ||
        !CheckWindowOwnerThread(tooltip_hwnd_.Get(), "StatusBarUI::DestroyTooltipWindow")) return false;
    destroying_window_ = true;
    const auto finish = gsl::finally([this]() noexcept { destroying_window_ = false; });
    visible_ = false;
    HideTooltip();
    EndDragTracking(false);
    const bool tooltip_destroyed = DestroyWindowOnOwnerThread(tooltip_hwnd_, "StatusBarUI::DestroyTooltipWindow");
    const bool bar_destroyed = DestroyWindowOnOwnerThread(hwnd_, "StatusBarUI::DestroyBarWindow");
    hovered_zone_ = -1;
    tracking_mouse_ = false;
    return tooltip_destroyed && bar_destroyed;
}

bool StatusBarUI::IsDraggableBlankPoint(POINT client_pt) const noexcept {
    const int bar_w = CurrentWidth();
    const int bar_h = CurrentHeight();
    if (client_pt.x < 0 || client_pt.x >= bar_w || client_pt.y < 0 || client_pt.y >= bar_h) {
        return false;
    }
    const int buttons_offset = ScaleByDpi(kButtonsOffset);
    const int v_margin = std::max(2, ScaleByDpi(3));
    if (client_pt.x < buttons_offset || client_pt.y < v_margin || client_pt.y >= bar_h - v_margin) {
        return true;
    }
    const int cell_w = std::max(1, ScaleByDpi(kCellWidth));
    const int rel_x = client_pt.x - buttons_offset;
    const int cell_idx = rel_x / cell_w;
    if (cell_idx < 0 || cell_idx >= kCellCount) {
        return true;
    }
    const int in_cell_x = rel_x % cell_w;
    const int pad_x = std::max(2, ScaleByDpi(3));
    return in_cell_x < pad_x || in_cell_x >= (cell_w - pad_x);
}

int StatusBarUI::HitTestZone(int client_x) const noexcept {
    const int bar_w = CurrentWidth();
    const int buttons_offset = ScaleByDpi(kButtonsOffset);
    const int cell_w = ScaleByDpi(kCellWidth);
    if (client_x < 0 || client_x >= bar_w) {
        return -1;
    }
    if (client_x < buttons_offset) {
        return 0; // Gripper drag zone
    }
    const int cell_idx = (client_x - buttons_offset) / std::max(1, cell_w);
    if (cell_idx >= 0 && cell_idx < kCellCount) {
        return cell_idx + 1; // 1..4
    }
    return -1;
}

const wchar_t* StatusBarUI::GetTooltipTextForX(int client_x) const noexcept {
    const auto& str = GetUiStrings(state_.ui_language);
    const int zone = HitTestZone(client_x);
    if (zone == 0) {
        return str.tip_grip;
    }
    if (zone >= 1 && zone <= kCellCount) {
        const auto& cell = gsl::at(kStatusBarCells, gsl::narrow_cast<size_t>(zone - 1));
        return str.*(cell.tooltip_field);
    }
    return L"";
}

void StatusBarUI::PaintBar(HDC hdc, const RECT& client_rect) noexcept {
    const ScopedSaveDC save_dc(hdc);
    const auto& palette = GetCurrentThemePalette();
    const int grip_w = ScaleByDpi(kGripWidth);
    const int buttons_offset = ScaleByDpi(kButtonsOffset);
    const int cell_w = ScaleByDpi(kCellWidth);
    const int corner_r = ScaleByDpi(10);

    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(palette.window_bg));
    const ScopedGdiHandle<HPEN> border_pen(::CreatePen(PS_SOLID, 1, palette.border_color));
    ::FillRect(hdc, &client_rect, bg_brush.Get());
    {
        const ScopedSelectObject sel_pen(hdc, border_pen.Get());
        const ScopedSelectObject sel_brush(hdc, bg_brush.Get());
        ::RoundRect(hdc, client_rect.left, client_rect.top, client_rect.right, client_rect.bottom, corner_r, corner_r);
    }

    // 1. Leftmost drag gripper area [0, grip_w):
    //    Draw centered vertical rounded pill bar (|) + 1px vertical divider line on its right edge
    {
        const ScopedGdiHandle<HBRUSH> pill_brush(::CreateSolidBrush(palette.grip_dot));
        const ScopedGdiHandle<HPEN> pill_pen(::CreatePen(PS_SOLID, 1, palette.grip_dot));
        const ScopedSelectObject sel_pill_pen(hdc, pill_pen.Get());
        const ScopedSelectObject sel_pill_brush(hdc, pill_brush.Get());
        const int pill_w = std::max(3, ScaleByDpi(4));
        const int pill_h = std::max(12, ScaleByDpi(16));
        const int pill_left = (grip_w - pill_w) / 2;
        const int pill_top = (client_rect.bottom - pill_h) / 2;
        ::RoundRect(hdc, pill_left, pill_top, pill_left + pill_w, pill_top + pill_h, pill_w, pill_w);
    }
    {
        const ScopedSelectObject sel_div_pen(hdc, border_pen.Get());
        ::MoveToEx(hdc, grip_w - 1, 2, nullptr);
        ::LineTo(hdc, grip_w - 1, client_rect.bottom - 2);
    }

    const ScopedSelectObject sel_font(hdc, font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    // 2. Interactive cells driven by kStatusBarCells descriptor table:
    //    Cell 0: ClearType "中" / "英" (or "CN" / "EN")
    //    Cell 1: Antialiased bitmap for Half-width Crescent Moon / Full-width Circle
    //    Cell 2: Antialiased bitmap for Chinese Punctuation (。，) / English Punctuation (.,)
    //    Cell 3: Antialiased bitmap for Settings Gear
    RebuildIconBitmaps(palette.text_primary);

    const auto& str = GetUiStrings(state_.ui_language);
    const ScopedGdiHandle<HBRUSH> hover_brush(::CreateSolidBrush(palette.hover_bg));
    ::SetTextColor(hdc, palette.text_primary);
    for (int i = 0; i < kCellCount; ++i) {
        const int left = buttons_offset + i * cell_w;
        RECT cell_rc{left, 2, left + cell_w - 1, client_rect.bottom - 2};
        if (hovered_zone_ == i + 1) {
            ::FillRect(hdc, &cell_rc, hover_brush.Get());
        }
        const auto& cell = gsl::at(kStatusBarCells, gsl::narrow_cast<size_t>(i));
        if (cell.select_text != nullptr) {
            const wchar_t* label = cell.select_text(state_, str);
            ::DrawTextW(hdc, label, -1, &cell_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            const HBITMAP bmp = GetIconBitmap(cell.select_icon(state_));
            DrawAlphaBitmapCentered(hdc, bmp, icon_px_, cell_rc);
        }
    }
}

void StatusBarUI::OnMouseMove(POINT pt) {
    if (left_button_down_ && hwnd_) {
        POINT screen_pt = pt;
        ::ClientToScreen(hwnd_.Get(), &screen_pt);
        const int dx = screen_pt.x - drag_start_screen_.x;
        const int dy = screen_pt.y - drag_start_screen_.y;
        const int drag_threshold = std::max(3, ScaleByDpi(4));
        if (!is_dragging_ && (std::abs(dx) >= drag_threshold || std::abs(dy) >= drag_threshold)) {
            is_dragging_ = true;
            hovered_zone_ = -1;
            HideTooltip();
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
        }
        if (is_dragging_) {
            pos_.x = drag_start_pos_.x + dx;
            pos_.y = drag_start_pos_.y + dy;
            ::SetWindowPos(
                hwnd_.Get(),
                HWND_TOPMOST,
                pos_.x,
                pos_.y,
                CurrentWidth(),
                CurrentHeight(),
                SWP_NOACTIVATE | SWP_NOSIZE);
            return;
        }
    }

    if (!tracking_mouse_ && hwnd_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_.Get();
        tracking_mouse_ = (::TrackMouseEvent(&tme) != FALSE);
    }
    const int zone = HitTestZone(pt.x);
    if (zone != hovered_zone_) {
        hovered_zone_ = zone;
        if (hwnd_) {
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
        }
        UpdateTooltipForZone(zone, pt.x);
    }
}

void StatusBarUI::OnMouseLeave() noexcept {
    tracking_mouse_ = false;
    if (hovered_zone_ != -1) {
        hovered_zone_ = -1;
        if (hwnd_) {
            ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
        }
    }
    HideTooltip();
}

void StatusBarUI::EndDragTracking(bool clamp_position) noexcept {
    const bool was_dragging = is_dragging_;
    left_button_down_ = false;
    is_dragging_ = false;
    if (hwnd_ && ::GetCapture() == hwnd_.Get()) {
        ::ReleaseCapture();
    }
    if (was_dragging && clamp_position) {
        ClampToCurrentMonitor();
        if (pos_changed_callback_) {
            try {
                pos_changed_callback_(pos_);
            } catch (...) {
                QIQI_LOG_ERROR("StatusBarUI::EndDragTracking position notification failed with a C++ exception");
            }
        }
    }
}

void StatusBarUI::OnLeftButtonDown(POINT pt) noexcept {
    left_button_down_ = true;
    is_dragging_ = false;
    drag_start_pos_ = pos_;
    drag_start_screen_ = pt;
    if (hwnd_) {
        ::ClientToScreen(hwnd_.Get(), &drag_start_screen_);
        ::SetCapture(hwnd_.Get());
    }
}

void StatusBarUI::OnLeftButtonUp(POINT pt) {
    const bool was_down = left_button_down_;
    const bool was_dragging = is_dragging_;
    EndDragTracking(true);
    if (!was_down || was_dragging || !action_callback_) {
        return;
    }
    const int zone = HitTestZone(pt.x);
    if (zone >= 1 && zone <= kCellCount) {
        const auto& cell = gsl::at(kStatusBarCells, gsl::narrow_cast<size_t>(zone - 1));
        action_callback_(cell.click_action);
    }
}

ScopedMenu StatusBarUI::BuildContextMenu(ScopedGdiHandle<HBRUSH>& out_bg_brush) const noexcept {
    return LangBarButton::BuildSharedPopupMenu(state_, hwnd_.Get(), out_bg_brush);
}

void StatusBarUI::OnRightButtonUp(POINT client_pt) const {
    if (!hwnd_) {
        return;
    }
    if (action_callback_) {
        action_callback_(LangBarMenuId::SyncMenuState);
    }
    POINT screen_pt = client_pt;
    ::ClientToScreen(hwnd_.Get(), &screen_pt);
    LangBarButton::ShowSharedPopupMenu(state_, screen_pt, action_callback_);
}

void StatusBarUI::OnDpiChanged(UINT new_dpi, const RECT* suggested_rect) noexcept {
    UpdateDpiAndFonts(new_dpi);
    if (suggested_rect) {
        pos_.x = suggested_rect->left;
        pos_.y = suggested_rect->top;
    }
    ClampToCurrentMonitor();
    if (hwnd_) {
        ::InvalidateRect(hwnd_.Get(), nullptr, TRUE);
    }
}

LRESULT StatusBarUI::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_NCDESTROY:
            if (hwnd_.Get() == hwnd) {
                std::ignore = hwnd_.Detach();
                visible_ = false;
                left_button_down_ = false;
                is_dragging_ = false;
                hovered_zone_ = -1;
                tracking_mouse_ = false;
                HideTooltip();
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST: {
            const POINT pt = ClientPointFromLParam(hwnd, lparam, true);
            return IsDraggableBlankPoint(pt) ? HTCAPTION : HTCLIENT;
        }
        case WM_SETCURSOR:
            ::SetCursor(::LoadCursorW(
                nullptr,
                (LOWORD(lparam) == HTCAPTION || is_dragging_) ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        case WM_MOVE:
            pos_ = PointFromLParam(lparam);
            HideTooltip();
            return 0;
        case WM_EXITSIZEMOVE:
            ClampToCurrentMonitor();
            if (pos_changed_callback_) {
                pos_changed_callback_(pos_);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            ClampToCurrentMonitor();
            return 0;
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wparam), reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_MOUSEMOVE:
        case WM_NCMOUSEMOVE:
            OnMouseMove(ClientPointFromLParam(hwnd, lparam, msg == WM_NCMOUSEMOVE));
            return 0;
        case WM_MOUSELEAVE:
        case WM_NCMOUSELEAVE:
            OnMouseLeave();
            return 0;
        case WM_LBUTTONDOWN:
            HideTooltip();
            OnLeftButtonDown(PointFromLParam(lparam));
            return 0;
        case WM_LBUTTONUP:
            OnLeftButtonUp(PointFromLParam(lparam));
            return 0;
        case WM_CAPTURECHANGED:
            left_button_down_ = false;
            is_dragging_ = false;
            return 0;
        case WM_RBUTTONUP:
        case WM_NCRBUTTONUP:
            HideTooltip();
            OnRightButtonUp(ClientPointFromLParam(hwnd, lparam, msg == WM_NCRBUTTONUP));
            return 0;
        case WM_COMMAND:
            if (action_callback_) {
                action_callback_(static_cast<LangBarMenuId>(LOWORD(wparam)));
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
            if (action_callback_) {
                action_callback_(LangBarMenuId::SyncMenuState);
            }
            ::InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [this](HDC hdc, const RECT& rc) noexcept { PaintBar(hdc, rc); });
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK StatusBarUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = BindOrGetWindowUserData<StatusBarUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return self->HandleMessage(hwnd, msg, wparam, lparam);
}

} // namespace qiqi
