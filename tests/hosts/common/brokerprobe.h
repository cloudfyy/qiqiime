// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <bit>
#include <cstdint>

namespace qiqi::host::broker {

struct ProbeResult {
    HRESULT coinitialize = E_PENDING;
    HRESULT create = E_PENDING;
    HRESULT blanket = E_PENDING;
    HRESULT read = E_PENDING;
    ULONG snapshot_size = 0;
    uint64_t revision = 0;

    uint32_t ApartmentBits() const noexcept {
        return std::bit_cast<uint32_t>(
            coinitialize);
    }

    bool ApartmentSucceeded() const noexcept {
        return SUCCEEDED(coinitialize);
    }
};

ProbeResult ProbeConfiguration() noexcept;

} // namespace qiqi::host::broker
