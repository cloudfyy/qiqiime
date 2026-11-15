// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "common/RAII.h"
#include "common/logger.h"
#include "common/dpi.h"
#include "common/controls.h"

namespace qiqi {

inline constexpr int kSmallBrandIconSize = 16;
inline constexpr int kLargeBrandIconSize = 48;

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
    static bool IsValid(HGDIOBJ obj) noexcept { return obj != nullptr && obj != HGDI_ERROR; }
    static HGDIOBJ Acquire(HDC hdc, HGDIOBJ new_obj) noexcept {
        return (hdc && new_obj) ? ::SelectObject(hdc, new_obj) : nullptr;
    }
    static void Release(HDC hdc, HGDIOBJ old_obj) noexcept { ::SelectObject(hdc, old_obj); }
};

using ScopedWindowDC     = ScopedContextGuard<WindowDcGuardTraits>;
using ScopedSaveDC       = ScopedContextGuard<SaveDcGuardTraits>;
using ScopedSelectObject = ScopedContextGuard<SelectObjectGuardTraits>;

inline HRESULT InsertListViewColumns(
    HWND list, std::span<const wchar_t* const> labels, std::span<const int> widths, UINT dpi,
    UiTextScale scale = UiTextScale::Percent100) {
    if (!::IsWindow(list)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    std::array<wchar_t, 64> name{};
    if (!::GetClassNameW(list, name.data(), gsl::narrow_cast<int>(name.size()))) {
        const DWORD error = ::GetLastError();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    if (std::wcscmp(name.data(), WC_LISTVIEWW) != 0) return E_INVALIDARG;
    if (labels.size() != widths.size() ||
        labels.size() > gsl::narrow_cast<size_t>((std::numeric_limits<int>::max)())) return E_INVALIDARG;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (!gsl::at(labels, i) || gsl::at(widths, i) < 0) return E_INVALIDARG;
    }
    for (size_t i = 0; i < labels.size(); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        std::wstring text(gsl::at(labels, i));
        column.pszText = text.data();
        column.cx = ScaleDisplayPx(gsl::at(widths, i), dpi, scale);
        if (ListView_InsertColumn(list, gsl::narrow_cast<int>(i), &column) < 0) return E_FAIL;
    }
    return S_OK;
}

inline HRESULT InsertListViewColumns(
    HWND list, std::span<const wchar_t* const> labels, std::span<const int> widths,
    const FluentD2DThemeContext& theme) {
    return InsertListViewColumns(list, labels, widths, theme.dpi, theme.display_preferences.text_scale);
}

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
    ULONG last_open_error = ERROR_ACCESS_DENIED;
    for (int attempt = 0; attempt < 25; ++attempt) {
        const ScopedClipboard clip(owner);
        if (!clip) {
            last_open_error = clip.Error();
            const HWND clip_owner = ::GetOpenClipboardWindow();
            const DWORD holder_tid = clip_owner ? ::GetWindowThreadProcessId(clip_owner, nullptr) : 0;
            // Do not sleep if our own thread holds the clipboard or if the failure is not transient contention.
            if (last_open_error != ERROR_ACCESS_DENIED || holder_tid == ::GetCurrentThreadId()) {
                return last_open_error;
            }
            ::Sleep(2);
            continue;
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
    return last_open_error;
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

inline bool TryGetSystemUiFont(LOGFONTW& font) noexcept {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_ERROR("System UI font query failed (error={})", error);
        return false;
    }
    font = metrics.lfMessageFont;
    return true;
}

// Create a system UI font scaled to the specified pixel height and weight.
inline ScopedGdiHandle<HFONT> CreateUiFont(
    int height_px, LONG weight = FW_NORMAL, BYTE quality = CLEARTYPE_QUALITY) noexcept {
    LOGFONTW system_font{};
    if (!TryGetSystemUiFont(system_font)) return {};
    LOGFONTW lf{};
    lf.lfHeight = -std::max(1, height_px);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = quality;
    wcscpy_s(lf.lfFaceName, std::data(system_font.lfFaceName));
    ScopedGdiHandle<HFONT> font(::CreateFontIndirectW(&lf));
    if (!font) {
        QIQI_LOG_WARN("CreateUiFont::CreateFontIndirectW failed");
    }
    return font;
}

inline bool UpdateToolWindowDisplay(
    HWND window, FluentD2DThemeContext& theme, ScopedGdiHandle<HFONT>& font,
    DisplayPreferences preferences, UINT dpi, std::span<const HWND> lists) noexcept {
    preferences = NormalizeDisplayPreferences(preferences);
    constexpr int kColumnScaleReferenceDip = 100;
    const int previous_scale = theme.ScalePx(kColumnScaleReferenceDip);
    const int next_scale = ScaleDisplayPx(kColumnScaleReferenceDip, dpi, preferences.text_scale);
    const int next = ScaleDisplayPx(kFluentBodyFontSize, dpi, preferences.text_scale);
    const bool change = !font || theme.display_preferences != preferences || theme.dpi != dpi;
    if (!change) {
        for (const HWND list : lists) {
            ::SendMessageW(ListView_GetHeader(list), WM_SETFONT, reinterpret_cast<WPARAM>(font.Get()), FALSE);
        }
        theme.RefreshThemeBrushes();
        return true;
    }
    auto next_font = CreateUiFont(next);
    if (!next_font) {
        QIQI_LOG_ERROR("Tool display font update failed (error={})", ::GetLastError());
        return false;
    }
    theme.SetDisplayPreferences(preferences);
    if (theme.dpi != dpi) theme.Init(dpi);
    ::SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(next_font.Get()), FALSE);
    ::EnumChildWindows(window, [](HWND child, LPARAM handle) noexcept -> BOOL {
        ::SendMessageW(child, WM_SETFONT, handle, FALSE);
        std::array<wchar_t, 64> name{};
        if (::GetClassNameW(child, name.data(), gsl::narrow_cast<int>(name.size())) == 0) {
            QIQI_LOG_WARN("Tool font target class query failed (error={})", ::GetLastError());
        } else if (_wcsicmp(name.data(), TOOLBARCLASSNAMEW) == 0) {
            const auto tooltip = reinterpret_cast<HWND>(::SendMessageW(child, TB_GETTOOLTIPS, 0, 0));
            if (tooltip) ::SendMessageW(tooltip, WM_SETFONT, handle, FALSE);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(next_font.Get()));
    for (const HWND list : lists) {
        ::SendMessageW(ListView_GetHeader(list), WM_SETFONT, reinterpret_cast<WPARAM>(next_font.Get()), FALSE);
    }
    font = std::move(next_font);
    if (previous_scale != next_scale) {
        for (const HWND list : lists) {
            const int count = Header_GetItemCount(ListView_GetHeader(list));
            for (int column = 0; column < count; ++column) {
                const int width = ListView_GetColumnWidth(list, column);
                if (!ListView_SetColumnWidth(list, column, ::MulDiv(width, next_scale, previous_scale))) {
                    QIQI_LOG_WARN("Tool list column rescaling failed (column={})", column);
                    return false;
                }
            }
        }
    }
    return true;
}

inline void SetFluentComboHeights(HWND combo, const FluentD2DThemeContext& theme) noexcept {
    const int height = FluentControlHeight(theme, kFluentComboItemHeight);
    if (::SendMessageW(combo, CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), height) == CB_ERR ||
        ::SendMessageW(combo, CB_SETITEMHEIGHT, 0, height) == CB_ERR) {
        QIQI_LOG_WARN("Tool combo height update failed");
    }
}

inline bool SetFluentListRowHeight(
    HWND list, const FluentD2DThemeContext& theme, ScopedImageList& images) noexcept {
    const auto* const current = ListView_GetImageList(list, LVSIL_SMALL);
    if (current && current != images.Get()) {
        QIQI_LOG_ERROR("Tool row height cannot replace an externally owned image list");
        return false;
    }
    if (!theme.display_preferences.easy_reading) {
        if (current) ListView_SetImageList(list, nullptr, LVSIL_SMALL);
        images.Reset();
        return true;
    }
    const int height = FluentControlHeight(theme, kFluentComboItemHeight);
    int image_width = 0;
    int image_height = 0;
    if (images) {
        if (!::ImageList_GetIconSize(images.Get(), &image_width, &image_height)) {
            QIQI_LOG_ERROR("Tool row-height image-list size query failed");
            return false;
        }
        if (image_height == height) return true;
    }
    ScopedImageList next(::ImageList_Create(1, height, ILC_COLOR32, 1, 0));
    if (!next) {
        QIQI_LOG_ERROR("Tool row-height image list creation failed (error={})", ::GetLastError());
        return false;
    }
    ::SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = ::GetWindowLongPtrW(list, GWL_STYLE);
    if (!::SetWindowLongPtrW(list, GWL_STYLE, style | LVS_SHAREIMAGELISTS) && ::GetLastError() != ERROR_SUCCESS) {
        QIQI_LOG_ERROR("Tool shared image-list style update failed (error={})", ::GetLastError());
        return false;
    }
    ListView_SetImageList(list, next.Get(), LVSIL_SMALL);
    images = std::move(next);
    return true;
}

// Create a 32-bit ARGB transparent HICON containing a single centered glyph (no solid background fill).
inline ScopedIcon CreateTransparentGlyphIcon(
    wchar_t ch, int size_px, COLORREF text_color, LONG weight = FW_SEMIBOLD) noexcept {
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
        const ScopedGdiHandle<HFONT> font = CreateUiFont(sz - 2, weight, ANTIALIASED_QUALITY);
        if (!sel_bmp || !font) {
            QIQI_LOG_WARN("CreateTransparentGlyphIcon: glyph drawing resources unavailable");
            return {};
        }
        const ScopedSelectObject sel_font(mem_dc.Get(), font.Get());
        if (!sel_font) {
            QIQI_LOG_WARN("CreateTransparentGlyphIcon: font selection failed");
            return {};
        }

        RECT rc{0, 0, sz, sz};
        if (!::SetBkMode(mem_dc.Get(), TRANSPARENT) ||
            ::SetTextColor(mem_dc.Get(), RGB(255, 255, 255)) == CLR_INVALID) {
            QIQI_LOG_WARN("CreateTransparentGlyphIcon: text drawing setup failed");
            return {};
        }
        const std::array<wchar_t, 2> text = {ch, L'\0'};
        if (!::DrawTextW(mem_dc.Get(), text.data(), 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE)) {
            QIQI_LOG_WARN("CreateTransparentGlyphIcon: glyph drawing failed");
            return {};
        }
    }
    if (!::GdiFlush()) {
        QIQI_LOG_WARN("CreateTransparentGlyphIcon: GdiFlush failed");
        return {};
    }
    {
        const ScopedSelectObject sel_mask(mem_dc.Get(), mask_bmp.Get());
        if (!sel_mask || !::PatBlt(mem_dc.Get(), 0, 0, sz, sz, BLACKNESS)) {
            QIQI_LOG_WARN("CreateTransparentGlyphIcon: alpha icon mask initialization failed");
            return {};
        }
    }

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
