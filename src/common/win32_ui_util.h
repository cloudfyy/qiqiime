#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include "common/scoped_handle.h"
#include "common/logger.h"

namespace qiqi {

struct WindowDcGuardTraits {
    using ContextType  = HWND;
    using ResourceType = HDC;
    static constexpr HDC Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const ResourceType& hdc) noexcept { return hdc != nullptr; }
    static HDC Acquire(HWND hwnd) noexcept {
        const HDC dc = ::GetDC(hwnd);
        if (!dc) {
            QIQI_LOG_DEBUG("ScopedWindowDC::GetDC failed");
        }
        return dc;
    }
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

// Replace an unsorted string combo's items, preserving the index or selecting the first item.
// The caller reports failures.
inline HRESULT ReplaceComboBoxItems(HWND combo, std::span<const wchar_t* const> items) noexcept {
    if (!::IsWindow(combo)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    std::array<wchar_t, 32> name{};
    if (!::GetClassNameW(combo, name.data(), gsl::narrow_cast<int>(name.size())) ||
        std::wcscmp(name.data(), L"ComboBox") != 0) return E_INVALIDARG;
    const LONG_PTR style = ::GetWindowLongPtrW(combo, GWL_STYLE);
    if ((style & CBS_SORT) != 0 ||
        ((style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) != 0 && (style & CBS_HASSTRINGS) == 0)) return E_INVALIDARG;
    for (const auto* item : items) {
        if (!item) return E_INVALIDARG;
    }
    const LRESULT selection = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto* item : items) {
        const LRESULT result = ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        if (result == CB_ERRSPACE) return E_OUTOFMEMORY;
        if (result == CB_ERR) return E_FAIL;
    }
    if (!items.empty()) {
        const auto index = selection >= 0 && gsl::narrow_cast<size_t>(selection) < items.size() ? selection : 0;
        if (::SendMessageW(combo, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(index), 0) == CB_ERR) return E_FAIL;
    }
    return S_OK;
}

// Copy wide text to the clipboard as CF_UNICODETEXT, replacing embedded NULs with U+2400.
inline ULONG CopyTextToClipboard(HWND owner, std::wstring_view text) noexcept {
    if (text.size() >= (std::numeric_limits<SIZE_T>::max() / sizeof(wchar_t))) return ERROR_BUFFER_OVERFLOW;
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    ScopedGlobalAlloc memory(::GlobalAlloc(GMEM_MOVEABLE, bytes));
    if (!memory) {
        const DWORD error = ::GetLastError();
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    {
        auto* buffer = static_cast<wchar_t*>(::GlobalLock(memory.Get()));
        if (!buffer) {
            const DWORD error = ::GetLastError();
            return error ? error : ERROR_LOCK_FAILED;
        }
        const auto unlock = gsl::finally([raw = memory.Get()]() noexcept { ::GlobalUnlock(raw); });
        const std::span<wchar_t> dst(buffer, text.size() + 1);
        for (size_t i = 0; i < text.size(); ++i) {
            const wchar_t ch = gsl::at(text, i);
            gsl::at(dst, i) = ch == L'\0' ? L'\x2400' : ch;
        }
        dst.back() = L'\0';
    }
    const ScopedClipboard clip(owner);
    if (!clip) {
        return clip.Error();
    }
    if (!::EmptyClipboard()) {
        const DWORD error = ::GetLastError();
        return error ? error : ERROR_GEN_FAILURE;
    }
    if (!::SetClipboardData(CF_UNICODETEXT, memory.Get())) {
        const DWORD error = ::GetLastError();
        return error ? error : ERROR_GEN_FAILURE;
    }
    std::ignore = memory.Detach();
    return ERROR_SUCCESS;
}

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
    const auto ux = gsl::narrow_cast<uint32_t>(
        gsl::narrow_cast<uint16_t>(gsl::narrow_cast<int16_t>(pt.x)));
    const auto uy = gsl::narrow_cast<uint32_t>(
        gsl::narrow_cast<uint16_t>(gsl::narrow_cast<int16_t>(pt.y)));
    return gsl::narrow_cast<LONG>(ux | (uy << 16u));
}

// Unpack a 32-bit LONG from TSF Global Compartment back into a signed 2D screen POINT.
constexpr POINT UnpackLongToScreenPoint(LONG packed) noexcept {
    const auto u = gsl::narrow_cast<uint32_t>(packed);
    return POINT{
        gsl::narrow_cast<LONG>(gsl::narrow_cast<int16_t>(gsl::narrow_cast<uint16_t>(u & 0xFFFFu))),
        gsl::narrow_cast<LONG>(
            gsl::narrow_cast<int16_t>(gsl::narrow_cast<uint16_t>((u >> 16u) & 0xFFFFu)))
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

inline bool CheckWindowOwnerThread(HWND hwnd, std::string_view operation) noexcept {
    if (!hwnd) return true;
    const DWORD owner_thread = ::GetWindowThreadProcessId(hwnd, nullptr);
    if (owner_thread == 0) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("{}: GetWindowThreadProcessId failed (error={})", operation, error);
        return false;
    }
    if (owner_thread != ::GetCurrentThreadId()) {
        QIQI_LOG_WARN("{}: window destruction rejected on wrong thread (owner_thread={}, current_thread={})",
                      operation, owner_thread, ::GetCurrentThreadId());
        return false;
    }
    return true;
}

inline bool DestroyWindowOnOwnerThread(ScopedWindow& window, std::string_view operation) noexcept {
    const HWND hwnd = window.Get();
    if (!hwnd) return true;
    if (!CheckWindowOwnerThread(hwnd, operation)) return false;
    if (!::DestroyWindow(hwnd)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("{}: DestroyWindow failed (error={})", operation, error);
        return false;
    }
    if (window.Get() == hwnd) std::ignore = window.Detach();
    return true;
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
    ScopedGdiHandle<HFONT> font(::CreateFontIndirectW(&lf));
    if (!font) {
        QIQI_LOG_WARN("CreateUiFont::CreateFontIndirectW failed");
    }
    return font;
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
        QIQI_LOG_WARN("CreateTransparentGlyphIcon::CreateCompatibleDC failed");
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
        QIQI_LOG_WARN("CreateTransparentGlyphIcon: bitmap creation failed");
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
    ScopedIcon icon(::CreateIconIndirect(&ii));
    if (!icon) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("CreateTransparentGlyphIcon::CreateIconIndirect failed (error={})", error);
    }
    return icon;
}

// Load a DPI-scaled icon from a module's resource (ID 1 by default), falling back to a transparent
// brand '奇' glyph icon if the module has no icon resource or uses the white taskbar-only icon.
inline ScopedIcon LoadAppIconOrBrandFallback(
    HINSTANCE instance, int size_px, WORD resource_id = 1, bool allow_module_resource = true) noexcept {
    const int sz = std::max(16, size_px);
    if (allow_module_resource && instance) {
        ScopedIcon loaded(static_cast<HICON>(::LoadImageW(
            instance, MAKEINTRESOURCEW(resource_id), IMAGE_ICON, sz, sz, LR_DEFAULTCOLOR)));
        if (loaded) {
            return loaded;
        }
    }
    return CreateTransparentGlyphIcon(L'奇', sz, RGB(35, 105, 200));
}

} // namespace qiqi
