// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace qiqi::broker {

HRESULT RegisterServer(
    HINSTANCE instance,
    std::wstring_view store_subkey = {}) noexcept;
HRESULT UnregisterServer() noexcept;

} // namespace qiqi::broker
