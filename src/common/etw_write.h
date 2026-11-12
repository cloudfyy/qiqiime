// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <evntprov.h>
#include <gsl/gsl>
#include <array>
#include <cstdio>

namespace qiqi::log::detail {

inline ULONG WriteEtwEvent(
    REGHANDLE provider, PCEVENT_DESCRIPTOR descriptor, LPCGUID activity, LPCGUID related,
    ULONG count, PEVENT_DATA_DESCRIPTOR data,
    gsl::not_null<decltype(&::EventWriteTransfer)> write,
    gsl::not_null<decltype(&::OutputDebugStringA)> output) noexcept {
    const DWORD saved_error = ::GetLastError();
    const auto restore_error = gsl::finally([saved_error]() noexcept { ::SetLastError(saved_error); });
    const ULONG error = write(provider, descriptor, activity, related, count, data);
    if (error != ERROR_SUCCESS) {
        std::array<char, 128> message{};
        std::snprintf(message.data(), message.size(),
                      "QiQiIME: EventWriteTransfer failed (error=%lu)\n", error);
        output(message.data());
    }
    return error;
}

} // namespace qiqi::log::detail
