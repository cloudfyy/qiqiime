// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/comobj.h"

namespace qiqi {

inline constexpr UINT kToolConfigChangedMessage = WM_APP + 0x40;
inline constexpr UINT kToolConfigLoadedMessage = WM_APP + 0x41;

class ToolConfigSubscription final {
public:
    HRESULT Subscribe(HWND window) noexcept;
    void RequestRefresh(HWND window) noexcept;
    bool CompleteRefresh(HWND window, WPARAM result, LPARAM request) noexcept;

private:
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
    ScopedTfCompartment compartment_;
    LPARAM request_ = 0;
    bool refresh_pending_ = false;
};

} // namespace qiqi
