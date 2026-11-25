// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace qiqi {

HRESULT DiscoverWindowsSearchPackageSids(
    std::vector<std::wstring>& output) noexcept;

} // namespace qiqi
