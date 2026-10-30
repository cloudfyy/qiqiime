#pragma once
#include <windows.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace qiqi {

// Generic zero-overhead move-only RAII wrapper parameterized by a Traits policy.
template <typename Traits>
class ScopedHandle final {
public:
    using HandleType = typename Traits::HandleType;

    ScopedHandle() noexcept = default;
    explicit ScopedHandle(HandleType h) noexcept : handle_(h) {}
    ~ScopedHandle() noexcept { Reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, Traits::Invalid())) {}

    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = std::exchange(other.handle_, Traits::Invalid());
        }
        return *this;
    }

    void Reset(HandleType new_handle = Traits::Invalid()) noexcept {
        if (Traits::IsValid(handle_)) {
            Traits::Close(handle_);
        }
        handle_ = new_handle;
    }

    HandleType Get() const noexcept { return handle_; }

    // Release ownership of the underlying handle and return it to caller (e.g. COM [out] parameters).
    HandleType Detach() noexcept {
        return std::exchange(handle_, Traits::Invalid());
    }

    // Reset and return address of underlying handle for Win32 out-parameters (e.g. RegOpenKeyExW).
    HandleType* Put() noexcept {
        Reset();
        return &handle_;
    }

    bool IsValid() const noexcept { return Traits::IsValid(handle_); }
    explicit operator bool() const noexcept { return IsValid(); }

private:
    HandleType handle_ = Traits::Invalid();
};

struct FileHandleTraits {
    using HandleType = HANDLE;
    static HandleType Invalid() noexcept { return INVALID_HANDLE_VALUE; }
    static bool IsValid(const HandleType& h) noexcept { return h != INVALID_HANDLE_VALUE && h != nullptr; }
    static void Close(HandleType h) noexcept { ::CloseHandle(h); }
};

struct KernelHandleTraits {
    using HandleType = HANDLE;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& h) noexcept { return h != nullptr; }
    static void Close(HandleType h) noexcept { ::CloseHandle(h); }
};

struct MappedViewTraits {
    using HandleType = const void*;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& v) noexcept { return v != nullptr; }
    static void Close(HandleType v) noexcept { ::UnmapViewOfFile(v); }
};

struct RegKeyTraits {
    using HandleType = HKEY;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& k) noexcept { return k != nullptr; }
    static void Close(HandleType k) noexcept { ::RegCloseKey(k); }
};

struct CreatedDcTraits {
    using HandleType = HDC;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& hdc) noexcept { return hdc != nullptr; }
    static void Close(HandleType hdc) noexcept { ::DeleteDC(hdc); }
};

struct IconTraits {
    using HandleType = HICON;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& icon) noexcept { return icon != nullptr; }
    static void Close(HandleType icon) noexcept { ::DestroyIcon(icon); }
};

struct BstrTraits {
    using HandleType = BSTR;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& bstr) noexcept { return bstr != nullptr; }
    static void Close(HandleType bstr) noexcept { ::SysFreeString(bstr); }
};

struct MenuTraits {
    using HandleType = HMENU;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& menu) noexcept { return menu != nullptr; }
    static void Close(HandleType menu) noexcept { ::DestroyMenu(menu); }
};

struct WindowTraits {
    using HandleType = HWND;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& hwnd) noexcept { return hwnd != nullptr; }
    static void Close(HandleType hwnd) noexcept {
        if (::IsWindow(hwnd)) {
            ::DestroyWindow(hwnd);
        }
    }
};

struct ModuleTraits {
    using HandleType = HMODULE;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& mod) noexcept { return mod != nullptr; }
    static void Close(HandleType mod) noexcept { ::FreeLibrary(mod); }
};

template <typename GdiHandleT>
struct GdiObjectTraits {
    using HandleType = GdiHandleT;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& obj) noexcept { return obj != nullptr; }
    static void Close(HandleType obj) noexcept { ::DeleteObject(obj); }
};

using ScopedFileHandle = ScopedHandle<FileHandleTraits>;
using ScopedKernelHandle = ScopedHandle<KernelHandleTraits>;
using ScopedMappedView = ScopedHandle<MappedViewTraits>;
using ScopedRegKey = ScopedHandle<RegKeyTraits>;
using ScopedMemoryDC = ScopedHandle<CreatedDcTraits>;
using ScopedIcon = ScopedHandle<IconTraits>;
using ScopedBstr = ScopedHandle<BstrTraits>;
using ScopedMenu = ScopedHandle<MenuTraits>;
using ScopedWindow = ScopedHandle<WindowTraits>;
using ScopedModule = ScopedHandle<ModuleTraits>;

template <typename GdiHandleT>
using ScopedGdiHandle = ScopedHandle<GdiObjectTraits<GdiHandleT>>;

// RAII wrapper for ::GetDC(hwnd) / ::ReleaseDC(hwnd, hdc).
class ScopedWindowDC final {
public:
    explicit ScopedWindowDC(HWND hwnd = nullptr) noexcept
        : hwnd_(hwnd), hdc_(::GetDC(hwnd)) {}

    ~ScopedWindowDC() noexcept {
        if (hdc_) {
            ::ReleaseDC(hwnd_, hdc_);
        }
    }

    ScopedWindowDC(const ScopedWindowDC&) = delete;
    ScopedWindowDC& operator=(const ScopedWindowDC&) = delete;
    ScopedWindowDC(ScopedWindowDC&&) = delete;
    ScopedWindowDC& operator=(ScopedWindowDC&&) = delete;

    HDC Get() const noexcept { return hdc_; }
    bool IsValid() const noexcept { return hdc_ != nullptr; }
    explicit operator bool() const noexcept { return IsValid(); }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
};

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

// RAII wrapper for ::SaveDC(hdc) / ::RestoreDC(hdc, saved_state).
class ScopedSaveDC final {
public:
    explicit ScopedSaveDC(HDC hdc) noexcept
        : hdc_(hdc), saved_(hdc ? ::SaveDC(hdc) : 0) {}

    ~ScopedSaveDC() noexcept {
        if (hdc_ && saved_ != 0) {
            ::RestoreDC(hdc_, saved_);
        }
    }

    ScopedSaveDC(const ScopedSaveDC&) = delete;
    ScopedSaveDC& operator=(const ScopedSaveDC&) = delete;
    ScopedSaveDC(ScopedSaveDC&&) = delete;
    ScopedSaveDC& operator=(ScopedSaveDC&&) = delete;

    explicit operator bool() const noexcept { return saved_ != 0; }

private:
    HDC hdc_ = nullptr;
    int saved_ = 0;
};

// RAII wrapper for ::SelectObject(hdc, new_obj) that restores the previous GDI object on scope exit.
class ScopedSelectObject final {
public:
    ScopedSelectObject(HDC hdc, HGDIOBJ new_obj) noexcept
        : hdc_(hdc),
          old_obj_((hdc && new_obj) ? ::SelectObject(hdc, new_obj) : nullptr) {}

    ~ScopedSelectObject() noexcept {
        if (hdc_ && old_obj_) {
            ::SelectObject(hdc_, old_obj_);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;
    ScopedSelectObject(ScopedSelectObject&&) = delete;
    ScopedSelectObject& operator=(ScopedSelectObject&&) = delete;

private:
    HDC hdc_ = nullptr;
    HGDIOBJ old_obj_ = nullptr;
};

// RAII wrapper for ::OpenClipboard(hwnd) / ::CloseClipboard().
class ScopedClipboard final {
public:
    explicit ScopedClipboard(HWND owner = nullptr) noexcept
        : opened_(::OpenClipboard(owner) != FALSE) {}

    ~ScopedClipboard() noexcept {
        if (opened_) {
            ::CloseClipboard();
        }
    }

    ScopedClipboard(const ScopedClipboard&) = delete;
    ScopedClipboard& operator=(const ScopedClipboard&) = delete;
    ScopedClipboard(ScopedClipboard&&) = delete;
    ScopedClipboard& operator=(ScopedClipboard&&) = delete;

    bool IsOpen() const noexcept { return opened_; }
    explicit operator bool() const noexcept { return opened_; }

private:
    bool opened_ = false;
};

// RAII wrapper for ::GlobalLock(hmem) / ::GlobalUnlock(hmem).
template <typename T>
class ScopedGlobalLock final {
public:
    explicit ScopedGlobalLock(HGLOBAL hmem) noexcept
        : hmem_(hmem),
          ptr_(hmem ? static_cast<const T*>(::GlobalLock(hmem)) : nullptr) {}

    ~ScopedGlobalLock() noexcept {
        if (hmem_ && ptr_) {
            ::GlobalUnlock(hmem_);
        }
    }

    ScopedGlobalLock(const ScopedGlobalLock&) = delete;
    ScopedGlobalLock& operator=(const ScopedGlobalLock&) = delete;
    ScopedGlobalLock(ScopedGlobalLock&&) = delete;
    ScopedGlobalLock& operator=(ScopedGlobalLock&&) = delete;

    const T* Get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    HGLOBAL hmem_ = nullptr;
    const T* ptr_ = nullptr;
};

// Extract signed client/screen coordinates from mouse message LPARAM using gsl::narrow_cast.
constexpr POINT PointFromLParam(LPARAM lparam) noexcept {
    return POINT{
        gsl::narrow_cast<short>(LOWORD(lparam)),
        gsl::narrow_cast<short>(HIWORD(lparam))
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

// Allocate a BSTR of known length via ::SysAllocStringLen using ScopedBstr RAII and detach to [out] BSTR*.
// Always initializes *pbstr = nullptr per COM [out] parameter rules.
inline HRESULT CopyToBstr(std::wstring_view sv, BSTR* pbstr) noexcept {
    if (!pbstr) {
        return E_INVALIDARG;
    }
    *pbstr = nullptr;
    ScopedBstr bstr(::SysAllocStringLen(sv.data(), gsl::narrow_cast<UINT>(sv.size())));
    if (!bstr) {
        return E_OUTOFMEMORY;
    }
    *pbstr = bstr.Detach();
    return S_OK;
}

// Construct a VT_I4 Win32 VARIANT for TSF compartment / property calls.
constexpr VARIANT MakeInt4Variant(LONG val) noexcept {
#pragma warning(suppress : 26476)
    VARIANT var{};
    var.vt = VT_I4;
#pragma warning(suppress : 26476)
    var.lVal = val;
    return var;
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
