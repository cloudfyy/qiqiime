#pragma once
#include <windows.h>
#include <oleauto.h>
#include <gsl/gsl>
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

// Extract signed client/screen coordinates from mouse message LPARAM using gsl::narrow_cast.
constexpr POINT PointFromLParam(LPARAM lparam) noexcept {
    return POINT{
        gsl::narrow_cast<short>(LOWORD(lparam)),
        gsl::narrow_cast<short>(HIWORD(lparam))
    };
}

} // namespace qiqi
