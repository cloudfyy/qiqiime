#pragma once
#include <windows.h>
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
    static bool IsValid(HandleType h) noexcept { return h != INVALID_HANDLE_VALUE && h != nullptr; }
    static void Close(HandleType h) noexcept { ::CloseHandle(h); }
};

struct KernelHandleTraits {
    using HandleType = HANDLE;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HandleType h) noexcept { return h != nullptr; }
    static void Close(HandleType h) noexcept { ::CloseHandle(h); }
};

struct MappedViewTraits {
    using HandleType = const void*;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HandleType v) noexcept { return v != nullptr; }
    static void Close(HandleType v) noexcept { ::UnmapViewOfFile(v); }
};

struct RegKeyTraits {
    using HandleType = HKEY;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HandleType k) noexcept { return k != nullptr; }
    static void Close(HandleType k) noexcept { ::RegCloseKey(k); }
};

template <typename GdiHandleT>
struct GdiObjectTraits {
    using HandleType = GdiHandleT;
    static constexpr HandleType Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(HandleType obj) noexcept { return obj != nullptr; }
    static void Close(HandleType obj) noexcept { ::DeleteObject(obj); }
};

using ScopedFileHandle = ScopedHandle<FileHandleTraits>;
using ScopedKernelHandle = ScopedHandle<KernelHandleTraits>;
using ScopedMappedView = ScopedHandle<MappedViewTraits>;
using ScopedRegKey = ScopedHandle<RegKeyTraits>;

template <typename GdiHandleT>
using ScopedGdiHandle = ScopedHandle<GdiObjectTraits<GdiHandleT>>;

} // namespace qiqi
