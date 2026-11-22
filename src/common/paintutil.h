// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/controls.h"

#include "common/logger.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <span>

namespace qiqi::paint {

inline ScopedGdiHandle<HBRUSH> CreateThemeBrush(COLORREF color) noexcept {
    ScopedGdiHandle<HBRUSH> brush(::CreateSolidBrush(color));
    if (!brush) QIQI_LOG_ERROR("Fluent CreateSolidBrush failed (error={})", ::GetLastError());
    return brush;
}

template <size_t N>
void AppendDisabledSuffix(std::array<wchar_t, N>& text, const wchar_t* suffix) noexcept {
    if (!suffix) return;
    const size_t length = std::wcslen(text.data());
    const size_t extra = std::wcslen(suffix);
    if (length + extra >= N) {
        QIQI_LOG_WARN("Fluent disabled-status label exceeds text buffer");
        return;
    }
    const std::span<const wchar_t> source(suffix, extra + 1);
    const auto destination = std::span(text).subspan(length, extra + 1);
    std::copy(source.begin(), source.end(), destination.begin());
}

} // namespace qiqi::paint
