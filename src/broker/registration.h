// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

namespace qiqi::broker {

HRESULT RegisterServer(HINSTANCE instance) noexcept;
HRESULT UnregisterServer() noexcept;

} // namespace qiqi::broker
