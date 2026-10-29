#include "ui/status_bar.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace qiqi {

namespace {

enum class StatusBarIconKind {
    HalfWidthMoon,
    FullWidthSun,
    ChinesePunct,
    EnglishPunct,
    SettingsGear
};

void AppendPopupItem(HMENU menu, LangBarMenuId id, const wchar_t* text, bool checked = false, bool enabled = true) noexcept {
    if (!menu || !text) {
        return;
    }
    const UINT flags = MF_STRING |
        (checked ? MF_CHECKED : MF_UNCHECKED) |
        (enabled ? MF_ENABLED : (MF_GRAYED | MF_DISABLED));
    ::AppendMenuW(menu, flags, static_cast<UINT_PTR>(id), text);
}

// Signed distance to a line segment [a, b] in normalized [-1, 1] icon coordinates.
double DistToSegment(double px, double py, double ax, double ay, double bx, double by) noexcept {
    const double vx = bx - ax;
    const double vy = by - ay;
    const double wx = px - ax;
    const double wy = py - ay;
    const double c1 = wx * vx + wy * vy;
    if (c1 <= 0.0) {
        return std::hypot(px - ax, py - ay);
    }
    const double c2 = vx * vx + vy * vy;
    if (c2 <= c1) {
        return std::hypot(px - bx, py - by);
    }
    const double t = c1 / c2;
    return std::hypot(px - (ax + t * vx), py - (ay + t * vy));
}

// Evaluate whether normalized point (nx, ny) in [-1, 1]^2 lies inside the target icon shape.
bool SampleStatusBarIcon(StatusBarIconKind kind, double nx, double ny) noexcept {
    constexpr double kStroke = 0.135;
    switch (kind) {
        case StatusBarIconKind::HalfWidthMoon: {
            // Outline crescent moon matching the reference screenshot:
            // outer circle C1(0.05, 0.0, R1=0.68) minus inner circle C2(-0.28, -0.14, R2=0.56)
            const double d1 = std::hypot(nx - 0.05, ny) - 0.68;
            const double d2 = std::hypot(nx + 0.28, ny + 0.14) - 0.56;
            const double d_crescent = std::max(d1, -d2);
            return std::abs(d_crescent) <= (kStroke * 0.52);
        }

        case StatusBarIconKind::FullWidthSun: {
            // Full-width circle icon: crisp hollow ring + solid inner core
            const double r = std::hypot(nx, ny);
            return (std::abs(r - 0.64) <= (kStroke * 0.55)) || (r <= 0.34);
        }

        case StatusBarIconKind::ChinesePunct: {
            // Top-left hollow ring '。' + bottom-right solid comma '，' matching the reference screenshot
            const double d_ring = std::abs(std::hypot(nx + 0.34, ny + 0.28) - 0.23);
            if (d_ring <= (kStroke * 0.50)) {
                return true;
            }
            // Comma head at (0.20, 0.08) + curved tail sweeping to (0.04, 0.56)
            const double d_head = std::hypot(nx - 0.20, ny - 0.08);
            if (d_head <= 0.19) {
                return true;
            }
            const double d_tail = DistToSegment(nx, ny, 0.24, 0.12, 0.05, 0.56);
            return d_tail <= 0.105;
        }

        case StatusBarIconKind::EnglishPunct: {
            // Solid period '.' at bottom-left + comma ',' at bottom-right
            const double d_dot = std::hypot(nx + 0.34, ny - 0.22);
            if (d_dot <= 0.16) {
                return true;
            }
            const double d_head = std::hypot(nx - 0.22, ny - 0.06);
            if (d_head <= 0.16) {
                return true;
            }
            const double d_tail = DistToSegment(nx, ny, 0.25, -0.02, 0.08, 0.44);
            return d_tail <= 0.095;
        }

        case StatusBarIconKind::SettingsGear: {
            // 8-tooth outline gear with center circular hole matching the reference screenshot
            const double r = std::hypot(nx, ny);
            const double angle = std::atan2(ny, nx);
            // Smooth 8-tooth modulation around base radius 0.58
            const double tooth_wave = std::clamp(std::cos(8.0 * angle) * 2.2, -1.0, 1.0);
            const double outer_r = 0.58 + 0.13 * (tooth_wave * 0.5 + 0.5);
            const double d_outer = std::abs(r - outer_r);
            const double d_inner = std::abs(r - 0.24);
            return (d_outer <= (kStroke * 0.52)) || (d_inner <= (kStroke * 0.50));
        }
    }
    return false;
}

// Create a 32-bit premultiplied ARGB DIB section rendered with 4x4 supersampled anti-aliasing.
ScopedGdiHandle<HBITMAP> CreateAntialiasedIconBitmap(
    StatusBarIconKind kind, int icon_px, COLORREF fg_color) noexcept {
    const int sz = std::max(12, icon_px);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz;
    bmi.bmiHeader.biHeight = -sz; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    ScopedGdiHandle<HBITMAP> dib(::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!dib || !bits) {
        return {};
    }

    const uint32_t r_fg = GetRValue(fg_color);
    const uint32_t g_fg = GetGValue(fg_color);
    const uint32_t b_fg = GetBValue(fg_color);
    const std::span<uint32_t> pixels(
         static_cast<uint32_t*>(bits), gsl::narrow_cast<size_t>(sz) * gsl::narrow_cast<size_t>(sz));

    constexpr int kSubSamples = 4;
    constexpr int kTotalSub = kSubSamples * kSubSamples;
    const double inv_sz = 2.0 / static_cast<double>(sz);

    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            int hits = 0;
            for (int sy = 0; sy < kSubSamples; ++sy) {
                const double ny = ((static_cast<double>(y) + (static_cast<double>(sy) + 0.5) / kSubSamples) * inv_sz) - 1.0;
                for (int sx = 0; sx < kSubSamples; ++sx) {
                    const double nx = ((static_cast<double>(x) + (static_cast<double>(sx) + 0.5) / kSubSamples) * inv_sz) - 1.0;
                    if (SampleStatusBarIcon(kind, nx, ny)) {
                        ++hits;
                    }
                }
            }
            const uint32_t alpha = gsl::narrow_cast<uint32_t>((hits * 255) / kTotalSub);
            // Premultiplied BGRA for GdiAlphaBlend
            const uint32_t pr = (r_fg * alpha) / 255u;
            const uint32_t pg = (g_fg * alpha) / 255u;
            const uint32_t pb = (b_fg * alpha) / 255u;
            gsl::at(pixels, gsl::narrow_cast<size_t>(y * sz + x)) =
                (alpha << 24u) | (pr << 16u) | (pg << 8u) | pb;
        }
    }
    return dib;
}

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
    DestroyBarWindow();
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

    LOGFONTW lf{};
    lf.lfHeight = -ScaleByDpi(18);
    lf.lfWeight = FW_MEDIUM;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font_.Reset(::CreateFontIndirectW(&lf));

    lf.lfHeight = -ScaleByDpi(13);
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    tooltip_font_.Reset(::CreateFontIndirectW(&lf));

    RebuildIconBitmaps(GetCurrentThemePalette().text_primary);
}

void StatusBarUI::RebuildIconBitmaps(COLORREF fg_color) noexcept {
    const int target_px = ScaleByDpi(20);
    if (bmp_half_width_.IsValid() && icon_px_ == target_px && icon_fg_color_ == fg_color) {
        return;
    }
    icon_px_ = target_px;
    icon_fg_color_ = fg_color;
    bmp_half_width_ = CreateAntialiasedIconBitmap(StatusBarIconKind::HalfWidthMoon, target_px, fg_color);
    bmp_full_width_ = CreateAntialiasedIconBitmap(StatusBarIconKind::FullWidthSun, target_px, fg_color);
    bmp_cn_punct_ = CreateAntialiasedIconBitmap(StatusBarIconKind::ChinesePunct, target_px, fg_color);
    bmp_en_punct_ = CreateAntialiasedIconBitmap(StatusBarIconKind::EnglishPunct, target_px, fg_color);
    bmp_settings_ = CreateAntialiasedIconBitmap(StatusBarIconKind::SettingsGear, target_px, fg_color);
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
    return tooltip_hwnd_.IsValid();
}

void StatusBarUI::DestroyBarWindow() noexcept {
    HideTooltip();
    tooltip_hwnd_.Reset();
    hwnd_.Reset();
    visible_ = false;
    hovered_zone_ = -1;
    tracking_mouse_ = false;
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
    switch (zone) {
        case 0: return str.tip_grip;
        case 1: return str.tip_mode;
        case 2: return str.tip_width;
        case 3: return str.tip_punct;
        case 4: return str.tip_settings;
        default: return L"";
    }
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

void StatusBarUI::PaintBar(HDC hdc, const RECT& client_rect) noexcept {
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

    // 2. 4 interactive cells matching the modern Windows IME toolbar style:
    //    Cell 0: ClearType "中" / "英" (or "CN" / "EN")
    //    Cell 1: Antialiased bitmap for Half-width Crescent Moon / Full-width Circle
    //    Cell 2: Antialiased bitmap for Chinese Punctuation (。，) / English Punctuation (.,)
    //    Cell 3: Antialiased bitmap for Settings Gear
    RebuildIconBitmaps(palette.text_primary);

    const ScopedGdiHandle<HBRUSH> hover_brush(::CreateSolidBrush(palette.hover_bg));
    ::SetTextColor(hdc, palette.text_primary);
    for (int i = 0; i < kCellCount; ++i) {
        const int left = buttons_offset + i * cell_w;
        RECT cell_rc{left, 2, left + cell_w - 1, client_rect.bottom - 2};
        if (hovered_zone_ == i + 1) {
            ::FillRect(hdc, &cell_rc, hover_brush.Get());
        }
        if (i == 0) {
            const auto& str = GetUiStrings(state_.ui_language);
            const wchar_t* mode_label = state_.chinese_mode ? str.status_mode_cn : str.status_mode_en;
            ::DrawTextW(hdc, mode_label, -1, &cell_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (i == 1) {
            const HBITMAP bmp = state_.full_width ? bmp_full_width_.Get() : bmp_half_width_.Get();
            DrawAlphaBitmapCentered(hdc, bmp, icon_px_, cell_rc);
        } else if (i == 2) {
            const HBITMAP bmp = state_.chinese_punct ? bmp_cn_punct_.Get() : bmp_en_punct_.Get();
            DrawAlphaBitmapCentered(hdc, bmp, icon_px_, cell_rc);
        } else if (i == 3) {
            DrawAlphaBitmapCentered(hdc, bmp_settings_.Get(), icon_px_, cell_rc);
        }
    }
}

void StatusBarUI::PaintTooltip(HDC hdc, const RECT& client_rect) const noexcept {
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

void StatusBarUI::OnMouseMove(POINT pt) {
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

void StatusBarUI::OnLeftButtonDown(POINT pt) const {
    const int buttons_offset = ScaleByDpi(kButtonsOffset);
    const int cell_w = ScaleByDpi(kCellWidth);
    if (!action_callback_ || pt.x < buttons_offset) {
        return;
    }
    const int cell_idx = (pt.x - buttons_offset) / std::max(1, cell_w);
    switch (cell_idx) {
        case 0: action_callback_(LangBarMenuId::ToggleChinese); break;
        case 1: action_callback_(LangBarMenuId::ToggleFullWidth); break;
        case 2: action_callback_(LangBarMenuId::TogglePunctuation); break;
        case 3: action_callback_(LangBarMenuId::OpenSettings); break;
        default: break;
    }
}

ScopedMenu StatusBarUI::BuildContextMenu(ScopedGdiHandle<HBRUSH>& out_bg_brush) const noexcept {
    const auto& palette = GetCurrentThemePalette();
    ApplySystemThemeToMenuAndWindow(hwnd_.Get(), palette.is_dark);

    ScopedMenu menu(::CreatePopupMenu());
    if (!menu) {
        return menu;
    }

    out_bg_brush.Reset(::CreateSolidBrush(palette.window_bg));
    if (out_bg_brush) {
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND;
        mi.hbrBack = out_bg_brush.Get();
        ::SetMenuInfo(menu.Get(), &mi);
    }

    const auto& str = GetUiStrings(state_.ui_language);
    AppendPopupItem(menu.Get(), LangBarMenuId::ToggleChinese, str.menu_toggle_chinese, state_.chinese_mode);
    AppendPopupItem(menu.Get(), LangBarMenuId::ToggleFullWidth, str.menu_toggle_full_width, state_.full_width);
    AppendPopupItem(menu.Get(), LangBarMenuId::TogglePunctuation, str.menu_toggle_punct, state_.chinese_punct);
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    const bool is_jidian = (state_.primary_dict == PrimaryDict::Jidian);
    AppendPopupItem(menu.Get(), LangBarMenuId::DictJidian, str.menu_dict_jidian, is_jidian);
    AppendPopupItem(menu.Get(), LangBarMenuId::DictHaifeng, str.menu_dict_haifeng, state_.primary_dict == PrimaryDict::Haifeng);
    AppendPopupItem(menu.Get(), LangBarMenuId::ToggleExtraDict, str.menu_toggle_extra_dict, is_jidian && state_.enable_jidian_extra, is_jidian);
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    AppendPopupItem(menu.Get(), LangBarMenuId::ToggleStatusBar, str.menu_toggle_status_bar, state_.show_status_bar);
    AppendPopupItem(menu.Get(), LangBarMenuId::ToggleUiLanguage, str.menu_toggle_ui_lang);
    AppendPopupItem(menu.Get(), LangBarMenuId::OpenSettings, str.menu_open_settings);
    AppendPopupItem(menu.Get(), LangBarMenuId::OpenConfigFile, str.menu_open_config_file);
    AppendPopupItem(menu.Get(), LangBarMenuId::OpenAbout, str.menu_open_about);
    return menu;
}

void StatusBarUI::OnRightButtonUp(POINT client_pt) const {
    if (!hwnd_) {
        return;
    }
    ScopedGdiHandle<HBRUSH> menu_bg;
    const ScopedMenu menu = BuildContextMenu(menu_bg);
    if (!menu) {
        return;
    }
    POINT screen_pt = client_pt;
    ::ClientToScreen(hwnd_.Get(), &screen_pt);
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(),
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        screen_pt.x,
        screen_pt.y,
        0,
        hwnd_.Get(),
        nullptr);
    if (cmd > 0 && action_callback_) {
        action_callback_(static_cast<LangBarMenuId>(cmd));
    }
}

LRESULT CALLBACK StatusBarUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* self = reinterpret_cast<StatusBarUI*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (msg == WM_NCHITTEST) {
        POINT pt = PointFromLParam(lparam);
        ::ScreenToClient(hwnd, &pt);
        if (pt.x >= 0 && pt.x < self->ScaleByDpi(kButtonsOffset)) {
            return HTCAPTION; // Dragging by the leftmost gripper moves the bar
        }
        return HTCLIENT;
    }
    if (msg == WM_SETCURSOR) {
        const WORD hit = LOWORD(lparam);
        ::SetCursor(::LoadCursorW(nullptr, hit == HTCAPTION ? IDC_SIZEALL : IDC_HAND));
        return TRUE;
    }
    if (msg == WM_MOVE) {
        self->pos_ = PointFromLParam(lparam);
        self->HideTooltip();
        return 0;
    }
    if (msg == WM_EXITSIZEMOVE || msg == WM_DISPLAYCHANGE) {
        self->ClampToCurrentMonitor();
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        self->UpdateDpiAndFonts(HIWORD(wparam));
        if (lparam != 0) {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            self->pos_.x = suggested->left;
            self->pos_.y = suggested->top;
        }
        self->ClampToCurrentMonitor();
        ::InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    if (msg == WM_NCMOUSEMOVE) {
        POINT pt = PointFromLParam(lparam);
        ::ScreenToClient(hwnd, &pt);
        self->OnMouseMove(pt);
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        self->OnMouseMove(PointFromLParam(lparam));
        return 0;
    }
    if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE) {
        self->OnMouseLeave();
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        self->HideTooltip();
        self->OnLeftButtonDown(PointFromLParam(lparam));
        return 0;
    }
    if (msg == WM_RBUTTONUP || msg == WM_NCRBUTTONUP) {
        self->HideTooltip();
        POINT pt = PointFromLParam(lparam);
        if (msg == WM_NCRBUTTONUP) {
            ::ScreenToClient(hwnd, &pt);
        }
        self->OnRightButtonUp(pt);
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (self->action_callback_) {
            self->action_callback_(static_cast<LangBarMenuId>(LOWORD(wparam)));
        }
        return 0;
    }
    if (msg == WM_SETTINGCHANGE) {
        ApplySystemThemeToMenuAndWindow(hwnd, IsSystemDarkTheme());
        ::InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    if (msg == WM_PAINT) {
        const ScopedPaintDC paint_dc(hwnd);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        self->PaintBar(paint_dc.Get(), rc);
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK StatusBarUI::TooltipWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    const auto* self = reinterpret_cast<const StatusBarUI*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (msg == WM_PAINT) {
        const ScopedPaintDC paint_dc(hwnd);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        self->PaintTooltip(paint_dc.Get(), rc);
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace qiqi
