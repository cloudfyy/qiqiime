// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <gsl/gsl>
#include <string>
#include <string_view>

namespace qiqi {

constexpr uint32_t PackWubiCode(std::wstring_view code) noexcept {
    if (code.empty() || code.size() > 4) {
        return 0;
    }
    uint32_t packed = 0;
    for (size_t index = 0; index < code.size(); ++index) {
        wchar_t ch = gsl::at(code, index);
        if (ch >= L'A' && ch <= L'Z') {
            ch = gsl::narrow_cast<wchar_t>(ch - L'A' + L'a');
        }
        if (ch < L'a' || ch > L'z') {
            return 0;
        }
        const uint32_t byte_value =
            gsl::narrow_cast<uint32_t>(ch - L'a' + 1);
        packed |= byte_value << ((3 - index) * 8);
    }
    return packed;
}

inline std::wstring UnpackWubiCode(uint32_t packed_code) {
    std::wstring result;
    result.reserve(4);
    for (int index = 0; index < 4; ++index) {
        const uint32_t byte_value =
            (packed_code >> ((3 - index) * 8)) & 0xffu;
        if (byte_value >= 1 && byte_value <= 26) {
            result.push_back(
                gsl::narrow_cast<wchar_t>(L'a' + byte_value - 1));
        } else {
            break;
        }
    }
    return result;
}

constexpr uint32_t PackWubiPrefixUpperBound(std::wstring_view prefix) noexcept {
    const uint32_t low = PackWubiCode(prefix);
    if (low == 0 || prefix.size() >= 4) return low;
    const uint32_t mask = (1u << ((4 - prefix.size()) * 8)) - 1u;
    return low | mask;
}

} // namespace qiqi
