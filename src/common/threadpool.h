// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/RAII.h"

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace qiqi {
namespace detail {

extern "C" IMAGE_DOS_HEADER __ImageBase;

template <typename Work>
struct ModuleCallback {
    ScopedModule module;
    Work work;

    static void CALLBACK Invoke(PTP_CALLBACK_INSTANCE instance, void* context) noexcept {
        const std::unique_ptr<ModuleCallback> pending(static_cast<ModuleCallback*>(context));
        if (pending->module) {
            // Release after the callback returns, including teardown of its captures.
            ::FreeLibraryWhenCallbackReturns(instance, pending->module.Detach());
        }
        pending->work();
    }
};

} // namespace detail

// Keep the submitting image loaded until queued work and its captures are finished.
template <typename Work>
HRESULT SubmitModuleCallback(Work work) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<Work> && std::is_nothrow_invocable_v<Work&>);
    ScopedModule module;
    if (reinterpret_cast<HMODULE>(&detail::__ImageBase) != ::GetModuleHandleW(nullptr) &&
        !::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&detail::__ImageBase), module.Put())) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    try {
        using Callback = detail::ModuleCallback<Work>;
        auto pending = std::make_unique<Callback>(std::move(module), std::move(work));
        // The callback can finish before submission returns.
        auto* const context = pending.release();
        if (!::TrySubmitThreadpoolCallback(&Callback::Invoke, context, nullptr)) {
            const DWORD error = ::GetLastError();
            pending.reset(context);
            return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_FUNCTION_FAILED);
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

} // namespace qiqi
