#include "Ime/ui/status_bar.h"
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

constexpr double kIconStroke = 0.135;

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

bool HitTestHalfWidthMoon(double nx, double ny) noexcept {
    // Outline crescent moon matching the reference screenshot:
    // outer circle C1(0.05, 0.0, R1=0.68) minus inner circle C2(-0.28, -0.14, R2=0.56)
    const double d1 = std::hypot(nx - 0.05, ny) - 0.68;
    const double d2 = std::hypot(nx + 0.28, ny + 0.14) - 0.56;
    const double d_crescent = std::max(d1, -d2);
    return std::abs(d_crescent) <= (kIconStroke * 0.52);
}

bool HitTestFullWidthSun(double nx, double ny) noexcept {
    // Full-width circle icon: crisp hollow ring + solid inner core
    const double r = std::hypot(nx, ny);
    return (std::abs(r - 0.64) <= (kIconStroke * 0.55)) || (r <= 0.34);
}

bool HitTestChinesePunct(double nx, double ny) noexcept {
    // Top-left hollow ring '。' + bottom-right solid comma '，' matching the reference screenshot
    const double d_ring = std::abs(std::hypot(nx + 0.34, ny + 0.28) - 0.23);
    if (d_ring <= (kIconStroke * 0.50)) {
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

bool HitTestEnglishPunct(double nx, double ny) noexcept {
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

bool HitTestSettingsGear(double nx, double ny) noexcept {
    // 8-tooth outline gear with center circular hole matching the reference screenshot
    const double r = std::hypot(nx, ny);
    const double angle = std::atan2(ny, nx);
    // Smooth 8-tooth modulation around base radius 0.58
    const double tooth_wave = std::clamp(std::cos(8.0 * angle) * 2.2, -1.0, 1.0);
    const double outer_r = 0.58 + 0.13 * (tooth_wave * 0.5 + 0.5);
    const double d_outer = std::abs(r - outer_r);
    const double d_inner = std::abs(r - 0.24);
    return (d_outer <= (kIconStroke * 0.52)) || (d_inner <= (kIconStroke * 0.50));
}

using IconHitTestFn = bool(*)(double nx, double ny) noexcept;

constexpr std::array<IconHitTestFn, StatusBarUI::kIconCount> kIconHitTesters = {
    &HitTestHalfWidthMoon, // StatusBarIconKind::HalfWidthMoon
    &HitTestFullWidthSun,  // StatusBarIconKind::FullWidthSun
    &HitTestChinesePunct,  // StatusBarIconKind::ChinesePunct
    &HitTestEnglishPunct,  // StatusBarIconKind::EnglishPunct
    &HitTestSettingsGear   // StatusBarIconKind::SettingsGear
};

consteval bool AllIconHitTestersBound() noexcept {
    for (const auto fn : kIconHitTesters) {
        if (fn == nullptr) {
            return false;
        }
    }
    return true;
}
static_assert(AllIconHitTestersBound(), "Every StatusBarIconKind must have a non-null hit-tester");

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
                    if (StatusBarUI::HitTestStatusBarIcon(kind, nx, ny)) {
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

bool StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind kind, double nx, double ny) noexcept {
    const auto idx = static_cast<size_t>(kind);
    if (idx >= kIconHitTesters.size()) {
        return false;
    }
    return gsl::at(kIconHitTesters, idx)(nx, ny);
}

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

    font_ = CreateUiFont(ScaleByDpi(18), FW_MEDIUM);
    tooltip_font_ = CreateUiFont(ScaleByDpi(13), FW_NORMAL);

    RebuildIconBitmaps(GetCurrentThemePalette().text_primary);
}

void StatusBarUI::RebuildIconBitmaps(COLORREF fg_color) noexcept {
    const int target_px = ScaleByDpi(20);
    if (gsl::at(icon_bitmaps_, 0).IsValid() && icon_px_ == target_px && icon_fg_color_ == fg_color) {
        return;
    }
    icon_px_ = target_px;
    icon_fg_color_ = fg_color;
    for (size_t i = 0; i < kIconCount; ++i) {
        gsl::at(icon_bitmaps_, i) = CreateAntialiasedIconBitmap(
            static_cast<StatusBarIconKind>(i), target_px, fg_color);
    }
}

HBITMAP StatusBarUI::GetIconBitmap(StatusBarIconKind kind) const noexcept {
    const auto idx = static_cast<size_t>(kind);
    if (idx >= icon_bitmaps_.size()) {
        return nullptr;
    }
    return gsl::at(icon_bitmaps_, idx).Get();
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
    if (!tooltip_hwnd_) {
        QIQI_LOG_ERROR("StatusBarUI::EnsureTooltipWindow CreateWindowExW failed (error={})", ::GetLastError());
    }
    return tooltip_hwnd_.IsValid();
}

void StatusBarUI::DestroyBarWindow() noexcept {
    HideTooltip();
    EndDragTracking(false);
    tooltip_hwnd_.Reset();
    hwnd_.Reset();
    visible_ = false;
    hovered_zone_ = -1;
    tracking_mouse_ = false;
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
