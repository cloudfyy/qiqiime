#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "common/scoped_handle.h"

namespace qiqi {

// ComReleaseTraits<T>: NullHandleTraits specialization that calls p->Release() on non-null IUnknown/D2D/DWrite pointers.
template <typename T>
struct ComReleaseTraits {
    using HandleType = T*;
    static constexpr T* Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const T* p) noexcept { return p != nullptr; }
    static void Close(T* p) noexcept {
        if (p) {
            p->Release();
        }
    }
};

template <typename T>
using ScopedD2DObject = ScopedHandle<ComReleaseTraits<T>>;

using ScopedD2DFactory       = ScopedD2DObject<ID2D1Factory>;
using ScopedD2DDcTarget      = ScopedD2DObject<ID2D1DCRenderTarget>;
using ScopedD2DBrush         = ScopedD2DObject<ID2D1SolidColorBrush>;
using ScopedDWriteFactory    = ScopedD2DObject<IDWriteFactory>;
using ScopedDWriteTextFormat = ScopedD2DObject<IDWriteTextFormat>;
using ScopedDWriteTextLayout = ScopedD2DObject<IDWriteTextLayout>;

// Convert a Win32 COLORREF (0x00bbggrr) to a D2D1_COLOR_F.
constexpr D2D1_COLOR_F ColorRefToD2DColor(COLORREF c, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(c)) / 255.0f,
        static_cast<float>(GetGValue(c)) / 255.0f,
        static_cast<float>(GetBValue(c)) / 255.0f,
        alpha);
}

// Construct a D2D1_COLOR_F from 8-bit RGB components.
constexpr D2D1_COLOR_F MakeD2DColor(uint8_t r, uint8_t g, uint8_t b, float alpha = 1.0f) noexcept {
    return ColorRefToD2DColor(RGB(r, g, b), alpha);
}

// Create a single-threaded ID2D1Factory, shared IDWriteFactory, and B8G8R8A8_UNORM ID2D1DCRenderTarget.
inline bool CreateD2DDcTargetAndDWrite(
    ScopedD2DFactory& out_d2d_factory,
    ScopedDWriteFactory& out_dwrite_factory,
    ScopedD2DDcTarget& out_dc_target) noexcept {
    std::ignore = ::D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(out_d2d_factory.Put()));

    std::ignore = ::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(out_dwrite_factory.Put()));

    if (out_d2d_factory) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        std::ignore = out_d2d_factory->CreateDCRenderTarget(&props, out_dc_target.Put());
    }
    return out_d2d_factory.IsValid() && out_dwrite_factory.IsValid() && out_dc_target.IsValid();
}

// Create an ID2D1SolidColorBrush on a render target, returning a ScopedD2DBrush.
inline ScopedD2DBrush CreateSolidBrush(
    ID2D1RenderTarget* target, const D2D1_COLOR_F& color) noexcept {
    ScopedD2DBrush brush;
    if (target) {
        std::ignore = target->CreateSolidColorBrush(color, brush.Put());
    }
    return brush;
}

// ScopedContextGuard traits for ID2D1DCRenderTarget::BindDC + BeginDraw / EndDraw.
struct D2DDcDrawGuardTraits {
    using ContextType  = ID2D1DCRenderTarget*;
    using ResourceType = HRESULT*;
    static constexpr HRESULT* Invalid() noexcept { return nullptr; }
    static constexpr bool IsValid(const HRESULT* marker) noexcept { return marker != nullptr; }
    static HRESULT* Acquire(
        ID2D1DCRenderTarget* target, HDC hdc, const RECT& rc, HRESULT* out_end_hr = nullptr) noexcept {
        if (!target || !hdc || FAILED(target->BindDC(hdc, &rc))) {
            return nullptr;
        }
        target->BeginDraw();
        static HRESULT s_dummy_hr = S_OK;
        return out_end_hr ? out_end_hr : &s_dummy_hr;
    }
    static void Release(ID2D1DCRenderTarget* target, HRESULT* out_end_hr) noexcept {
        if (target) {
            const HRESULT hr = target->EndDraw();
            if (out_end_hr) {
                *out_end_hr = hr;
            }
        }
    }
};

using ScopedD2DDcDraw = ScopedContextGuard<D2DDcDrawGuardTraits>;

} // namespace qiqi
