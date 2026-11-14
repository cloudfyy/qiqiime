// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>

namespace qiqi {

// Shared pagination helpers used by both WubiEngine and CandidateUI
constexpr std::size_t ComputeTotalPages(std::size_t total_items, std::size_t page_size) noexcept {
    if (total_items == 0 || page_size == 0) {
        return 0;
    }
    return total_items / page_size + (total_items % page_size != 0 ? 1 : 0);
}

constexpr std::pair<std::size_t, std::size_t> ComputePageRange(
    std::size_t total_items, std::size_t page_size, std::size_t current_page) noexcept {
    if (page_size == 0 || current_page >= ComputeTotalPages(total_items, page_size)) {
        return {0, 0};
    }
    const std::size_t start = current_page * page_size;
    return {start, start + std::min(page_size, total_items - start)};
}

} // namespace qiqi
