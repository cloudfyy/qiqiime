#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

#include "common/scoped_handle.h"

namespace qiqi {

// Convert a Win32 COLORREF (0x00bbggrr) or RGB components to a D2D1_COLOR_F.
constexpr D2D1_COLOR_F ColorRefToD2DColor(COLORREF c, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(c)) / 255.0f,
        static_cast<float>(GetGValue(c)) / 255.0f,
        static_cast<float>(GetBValue(c)) / 255.0f,
        alpha);
}

// Create a single-threaded ID2D1Factory, shared IDWriteFactory, and B8G8R8A8_UNORM ID2D1DCRenderTarget.
inline bool CreateD2DDcTargetAndDWrite(
    Microsoft::WRL::ComPtr<ID2D1Factory>& out_d2d_factory,
    Microsoft::WRL::ComPtr<IDWriteFactory>& out_dwrite_factory,
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>& out_dc_target) noexcept {
    std::ignore = ::D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(out_d2d_factory.ReleaseAndGetAddressOf()));

    std::ignore = ::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(out_dwrite_factory.ReleaseAndGetAddressOf()));

    if (out_d2d_factory) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        std::ignore = out_d2d_factory->CreateDCRenderTarget(&props, &out_dc_target);
    }
    return out_d2d_factory != nullptr && out_dwrite_factory != nullptr && out_dc_target != nullptr;
}

struct WindowDcGuardTraits {
    using ContextType  = HWND;
    using ResourceType = HDC;
    static constexpr HDC Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HDC hdc) noexcept { return hdc != nullptr; }
    static HDC Acquire(HWND hwnd) noexcept { return ::GetDC(hwnd); }
    static void Release(HWND hwnd, HDC hdc) noexcept { ::ReleaseDC(hwnd, hdc); }
};

struct SaveDcGuardTraits {
    using ContextType  = HDC;
    using ResourceType = int;
    static constexpr int Invalid() noexcept { return 0; }
    static constexpr bool IsValid(int saved) noexcept { return saved != 0; }
    static int Acquire(HDC hdc) noexcept { return hdc ? ::SaveDC(hdc) : 0; }
    static void Release(HDC hdc, int saved) noexcept { ::RestoreDC(hdc, saved); }
};

struct SelectObjectGuardTraits {
    using ContextType  = HDC;
    using ResourceType = HGDIOBJ;
    static constexpr HGDIOBJ Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HGDIOBJ obj) noexcept { return obj != nullptr; }
    static HGDIOBJ Acquire(HDC hdc, HGDIOBJ new_obj) noexcept {
        return (hdc && new_obj) ? ::SelectObject(hdc, new_obj) : nullptr;
    }
    static void Release(HDC hdc, HGDIOBJ old_obj) noexcept { ::SelectObject(hdc, old_obj); }
};

using ScopedWindowDC     = ScopedContextGuard<WindowDcGuardTraits>;
using ScopedSaveDC       = ScopedContextGuard<SaveDcGuardTraits>;
using ScopedSelectObject = ScopedContextGuard<SelectObjectGuardTraits>;

// RAII wrapper for ::BeginPaint(hwnd, &ps) / ::EndPaint(hwnd, &ps).
class ScopedPaintDC final {
public:
    explicit ScopedPaintDC(HWND hwnd) noexcept
        : hwnd_(hwnd), hdc_(::BeginPaint(hwnd, &ps_)) {}

    ~ScopedPaintDC() noexcept {
        ::EndPaint(hwnd_, &ps_);
    }

    ScopedPaintDC(const ScopedPaintDC&) = delete;
    ScopedPaintDC& operator=(const ScopedPaintDC&) = delete;
    ScopedPaintDC(ScopedPaintDC&&) = delete;
    ScopedPaintDC& operator=(ScopedPaintDC&&) = delete;

    HDC Get() const noexcept { return hdc_; }

private:
    HWND hwnd_ = nullptr;
    PAINTSTRUCT ps_{};
    HDC hdc_ = nullptr;
};

    // Extract signed client/screen coordinates from mouse message LPARAM using gsl::narrow_cast.
constexpr POINT PointFromLParam(LPARAM lparam) noexcept {
    return POINT{
        gsl::narrow_cast<short>(LOWORD(lparam)),
        gsl::narrow_cast<short>(HIWORD(lparam))
    };
}

// Pack a 2D screen POINT into a 32-bit LONG for TSF Global Compartment storage (returns 0 only for (0,0)).
constexpr LONG PackScreenPointToLong(POINT pt) noexcept {
    const auto ux = static_cast<uint32_t>(static_cast<uint16_t>(gsl::narrow_cast<int16_t>(pt.x)));
    const auto uy = static_cast<uint32_t>(static_cast<uint16_t>(gsl::narrow_cast<int16_t>(pt.y)));
    return gsl::narrow_cast<LONG>(ux | (uy << 16u));
}

// Unpack a 32-bit LONG from TSF Global Compartment back into a signed 2D screen POINT.
constexpr POINT UnpackLongToScreenPoint(LONG packed) noexcept {
    const auto u = static_cast<uint32_t>(packed);
    return POINT{
        static_cast<LONG>(static_cast<int16_t>(static_cast<uint16_t>(u & 0xFFFFu))),
        static_cast<LONG>(static_cast<int16_t>(static_cast<uint16_t>((u >> 16u) & 0xFFFFu)))
    };
}

// Extract client-space coordinates from a mouse message LPARAM (converting screen coordinates if is_non_client is true).
inline POINT ClientPointFromLParam(HWND hwnd, LPARAM lparam, bool is_non_client = false) noexcept {
    POINT pt = PointFromLParam(lparam);
    if (is_non_client && hwnd) {
        ::ScreenToClient(hwnd, &pt);
    }
    return pt;
}

// Standard WM_PAINT wrapper that creates a ScopedPaintDC, queries GetClientRect, and invokes paint_fn(hdc, rc).
template <typename PaintFn>
LRESULT PaintWindowClient(HWND hwnd, PaintFn&& paint_fn) {
    const ScopedPaintDC paint_dc(hwnd);
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    std::forward<PaintFn>(paint_fn)(paint_dc.Get(), rc);
    return 0;
}

// Bind CREATESTRUCTW::lpCreateParams to GWLP_USERDATA on WM_NCCREATE and retrieve the typed window context pointer.
template <typename T>
T* BindOrGetWindowUserData(HWND hwnd, UINT msg, LPARAM lparam) noexcept {
    if (msg == WM_NCCREATE && lparam != 0) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    return reinterpret_cast<T*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// Create a Microsoft YaHei UI font scaled to the specified pixel height and weight.
inline ScopedGdiHandle<HFONT> CreateUiFont(
    int height_px, LONG weight = FW_NORMAL, BYTE quality = CLEARTYPE_QUALITY) noexcept {
    LOGFONTW lf{};
    lf.lfHeight = -std::max(1, height_px);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = quality;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    return ScopedGdiHandle<HFONT>(::CreateFontIndirectW(&lf));
}

// Create a 32-bit ARGB transparent HICON containing a single centered glyph (no solid background fill).
inline ScopedIcon CreateTransparentGlyphIcon(wchar_t ch, int size_px, COLORREF text_color) noexcept {
    const int sz = std::max(16, size_px);
    const ScopedWindowDC screen_dc(nullptr);
    if (!screen_dc) {
        return {};
    }
    const ScopedMemoryDC mem_dc(::CreateCompatibleDC(screen_dc.Get()));
    if (!mem_dc) {
        return {};
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz;
    bmi.bmiHeader.biHeight = -sz; // Top-down 32-bit ARGB DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const ScopedGdiHandle<HBITMAP> color_bmp(
        ::CreateDIBSection(screen_dc.Get(), &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    const ScopedGdiHandle<HBITMAP> mask_bmp(::CreateBitmap(sz, sz, 1, 1, nullptr));
    if (!color_bmp || !bits || !mask_bmp) {
        return {};
    }

    const size_t pixel_count = gsl::narrow_cast<size_t>(sz) * gsl::narrow_cast<size_t>(sz);
    std::memset(bits, 0, pixel_count * sizeof(uint32_t));

    {
        const ScopedSelectObject sel_bmp(mem_dc.Get(), color_bmp.Get());
        const ScopedGdiHandle<HFONT> font = CreateUiFont(sz - 2, FW_SEMIBOLD, ANTIALIASED_QUALITY);
        const ScopedSelectObject sel_font(mem_dc.Get(), font.Get());

        RECT rc{0, 0, sz, sz};
        ::SetBkMode(mem_dc.Get(), TRANSPARENT);
        ::SetTextColor(mem_dc.Get(), RGB(255, 255, 255));
        const std::array<wchar_t, 2> text = {ch, L'\0'};
        ::DrawTextW(mem_dc.Get(), text.data(), 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    ::GdiFlush();

    // Convert grayscale glyph intensity into 32-bit premultiplied ARGB with transparent background
    const uint32_t r_fg = GetRValue(text_color);
    const uint32_t g_fg = GetGValue(text_color);
    const uint32_t b_fg = GetBValue(text_color);
#pragma warning(suppress : 26490)
    const std::span<uint32_t> pixels(static_cast<uint32_t*>(bits), pixel_count);
    for (uint32_t& px : pixels) {
        const uint32_t lum = px & 0xFFu;
        if (lum == 0) {
            px = 0;
        } else {
            const uint32_t pr = (r_fg * lum) / 255u;
            const uint32_t pg = (g_fg * lum) / 255u;
            const uint32_t pb = (b_fg * lum) / 255u;
            px = (lum << 24u) | (pr << 16u) | (pg << 8u) | pb;
        }
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp.Get();
    ii.hbmColor = color_bmp.Get();
    return ScopedIcon(::CreateIconIndirect(&ii));
}

} // namespace qiqi
