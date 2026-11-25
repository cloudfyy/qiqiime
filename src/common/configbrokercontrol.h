// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace qiqi::config {

inline constexpr HRESULT
    kBrokerMutationHandoffRequired =
        HRESULT_FROM_WIN32(ERROR_RETRY);

std::wstring BrokerSessionObjectName(
    std::wstring_view prefix,
    std::wstring_view store_subkey);

HRESULT TokenIntegrityRid(
    HANDLE token,
    DWORD& output) noexcept;

HRESULT RequestBrokerMutationHandoff() noexcept;

} // namespace qiqi::config
