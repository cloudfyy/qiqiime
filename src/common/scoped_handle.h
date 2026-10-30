#pragma once
#include <windows.h>
#include <oleauto.h>
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

    // Pointer-like access when HandleType is an interface/struct pointer (e.g. ScopedD2DObject<T>).
    HandleType operator->() const noexcept { return handle_; }

    bool IsValid() const noexcept { return Traits::IsValid(handle_); }
    explicit operator bool() const noexcept { return IsValid(); }

private:
    HandleType handle_ = Traits::Invalid();
};

// Generic Traits template for Win32/COM pointer handles whose invalid value is nullptr.
template <typename T, auto CloseFn>
struct NullHandleTraits {
    using HandleType = T;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HandleType& h) noexcept { return h != nullptr; }
    static void Close(HandleType h) noexcept { CloseFn(h); }
};

struct FileHandleTraits {
    using HandleType = HANDLE;
    static HandleType Invalid() noexcept { return INVALID_HANDLE_VALUE; }
    static bool IsValid(const HandleType& h) noexcept { return h != INVALID_HANDLE_VALUE && h != nullptr; }
    static void Close(HandleType h) noexcept { ::CloseHandle(h); }
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

using ScopedFileHandle   = ScopedHandle<FileHandleTraits>;
using ScopedKernelHandle = ScopedHandle<NullHandleTraits<HANDLE, &::CloseHandle>>;
using ScopedMappedView   = ScopedHandle<NullHandleTraits<const void*, &::UnmapViewOfFile>>;
using ScopedRegKey       = ScopedHandle<NullHandleTraits<HKEY, &::RegCloseKey>>;
using ScopedLocalAlloc   = ScopedHandle<NullHandleTraits<HLOCAL, &::LocalFree>>;
using ScopedMemoryDC     = ScopedHandle<NullHandleTraits<HDC, &::DeleteDC>>;
using ScopedIcon         = ScopedHandle<NullHandleTraits<HICON, &::DestroyIcon>>;
using ScopedBstr         = ScopedHandle<NullHandleTraits<BSTR, &::SysFreeString>>;
using ScopedMenu         = ScopedHandle<NullHandleTraits<HMENU, &::DestroyMenu>>;
using ScopedModule       = ScopedHandle<NullHandleTraits<HMODULE, &::FreeLibrary>>;
using ScopedWindow       = ScopedHandle<WindowTraits>;

template <typename GdiHandleT>
using ScopedGdiHandle    = ScopedHandle<NullHandleTraits<GdiHandleT, &::DeleteObject>>;

// Generic non-movable RAII guard for scope-bound resources whose cleanup requires both a context and a resource state.
template <typename Traits>
class ScopedContextGuard final {
public:
    using ContextType  = typename Traits::ContextType;
    using ResourceType = typename Traits::ResourceType;

    template <typename... Args>
    explicit ScopedContextGuard(ContextType ctx = ContextType{}, Args&&... args) noexcept
        : ctx_(ctx), res_(Traits::Acquire(ctx, std::forward<Args>(args)...)) {}

    ~ScopedContextGuard() noexcept {
        if (Traits::IsValid(res_)) {
            Traits::Release(ctx_, res_);
        }
    }

    ScopedContextGuard(const ScopedContextGuard&) = delete;
    ScopedContextGuard& operator=(const ScopedContextGuard&) = delete;
    ScopedContextGuard(ScopedContextGuard&&) = delete;
    ScopedContextGuard& operator=(ScopedContextGuard&&) = delete;

    ResourceType Get() const noexcept { return res_; }
    bool IsValid() const noexcept { return Traits::IsValid(res_); }
    bool IsOpen() const noexcept { return IsValid(); }
    explicit operator bool() const noexcept { return IsValid(); }

private:
    ContextType ctx_{};
    ResourceType res_ = Traits::Invalid();
};

struct ClipboardGuardTraits {
    using ContextType  = HWND;
    using ResourceType = bool;
    static constexpr bool Invalid() noexcept { return false; }
    static constexpr bool IsValid(bool opened) noexcept { return opened; }
    static bool Acquire(HWND owner) noexcept { return ::OpenClipboard(owner) != FALSE; }
    static void Release(HWND /*owner*/, bool /*opened*/) noexcept { ::CloseClipboard(); }
};

template <typename T>
struct GlobalLockGuardTraits {
    using ContextType  = HGLOBAL;
    using ResourceType = const T*;
    static constexpr const T* Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const T* ptr) noexcept { return ptr != nullptr; }
    static const T* Acquire(HGLOBAL hmem) noexcept {
        return hmem ? static_cast<const T*>(::GlobalLock(hmem)) : nullptr;
    }
    static void Release(HGLOBAL hmem, const T* /*ptr*/) noexcept { ::GlobalUnlock(hmem); }
};

using ScopedClipboard = ScopedContextGuard<ClipboardGuardTraits>;

template <typename T>
using ScopedGlobalLock = ScopedContextGuard<GlobalLockGuardTraits<T>>;

} // namespace qiqi
