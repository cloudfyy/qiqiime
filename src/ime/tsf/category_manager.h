// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/guids.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <msctf.h>
#include <wrl/client.h>

namespace qiqi::detail {

struct CategoryManagerApi {
    static ScopedModule AcquireModule() noexcept {
        HMODULE module = nullptr;
        if (!::GetModuleHandleExW(0, L"msctf.dll", &module)) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_DEBUG("CategoryManager: GetModuleHandleExW(msctf.dll) failed; trying COM fallback (error={})",
                           error);
        }
        return ScopedModule(module);
    }

    static HRESULT CreateFromExport(HMODULE module, ITfCategoryMgr** manager) noexcept {
        using Factory = HRESULT(WINAPI*)(ITfCategoryMgr**);
        const auto factory = reinterpret_cast<Factory>(::GetProcAddress(module, "TF_CreateCategoryMgr"));
        if (!factory) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_DEBUG("CategoryManager: GetProcAddress(TF_CreateCategoryMgr) failed (error={})", error);
            return HRESULT_FROM_WIN32(error);
        }
        return factory(manager);
    }

    static HRESULT CreateFromCom(ITfCategoryMgr** manager) {
        return ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(manager));
    }
};

// Keep the acquired module reference alive until all exported COM interfaces have been released.
template <typename Api = CategoryManagerApi>
TfGuidAtom RegisterCompositionDisplayAttributeAtom(Api api = {}) {
    const auto module = api.AcquireModule();
    Microsoft::WRL::ComPtr<ITfCategoryMgr> manager;
    if (module) {
        HRESULT hr = api.CreateFromExport(module.Get(), manager.GetAddressOf());
        if (SUCCEEDED(hr) && !manager) {
            hr = E_UNEXPECTED;
        }
        if (!log::CheckHResult(hr, "CategoryManager::TF_CreateCategoryMgr; trying COM fallback",
                               log::Level::debug)) {
            manager.Reset();
        }
    }
    if (!manager) {
        HRESULT hr = api.CreateFromCom(manager.GetAddressOf());
        if (SUCCEEDED(hr) && !manager) {
            hr = E_UNEXPECTED;
        }
        if (!log::CheckHResult(hr, "RegisterCompositionDisplayAttributeAtom::CoCreateInstance(ITfCategoryMgr)")) {
            return TF_INVALID_GUIDATOM;
        }
    }
    TfGuidAtom atom = TF_INVALID_GUIDATOM;
    if (!log::CheckHResult(manager->RegisterGUID(kGuidDisplayAttributeInput, &atom),
                           "RegisterCompositionDisplayAttributeAtom::RegisterGUID")) {
        return TF_INVALID_GUIDATOM;
    }
    return atom;
}

} // namespace qiqi::detail
