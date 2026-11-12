#include "ime/ui/statusbar.h"
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
constexpr int kIconSize = 20;
constexpr int kMinimumIconSizePx = 12;

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

// Create a 32-bit premultiplied ARGB DIB section rendered with 4x4 supersampled anti-aliasing.
ScopedGdiHandle<HBITMAP> CreateAntialiasedIconBitmap(
    StatusBarIconKind kind, int icon_px, COLORREF fg_color) noexcept {
    const int sz = std::max(kMinimumIconSizePx, icon_px);
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
        QIQI_LOG_WARN("CreateAntialiasedIconBitmap::CreateDIBSection failed");
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
                const double ny =
                    ((static_cast<double>(y) + (static_cast<double>(sy) + 0.5) / kSubSamples) * inv_sz) - 1.0;
                for (int sx = 0; sx < kSubSamples; ++sx) {
                    const double nx =
                        ((static_cast<double>(x) + (static_cast<double>(sx) + 0.5) / kSubSamples) * inv_sz) - 1.0;
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

} // namespace

bool StatusBarUI::HitTestStatusBarIcon(StatusBarIconKind kind, double nx, double ny) noexcept {
    const auto idx = static_cast<size_t>(kind);
    if (idx >= kIconHitTesters.size()) {
        return false;
    }
    return gsl::at(kIconHitTesters, idx)(nx, ny);
}

void StatusBarUI::RebuildIconBitmaps(COLORREF fg_color) noexcept {
    const int target_px = ScaleByDpi(kIconSize);
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

} // namespace qiqi
