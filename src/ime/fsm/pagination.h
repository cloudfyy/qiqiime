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
    return (total_items + page_size - 1) / page_size;
}

constexpr std::pair<std::size_t, std::size_t> ComputePageRange(
    std::size_t total_items, std::size_t page_size, std::size_t current_page) noexcept {
    const std::size_t start = current_page * page_size;
    if (start >= total_items || page_size == 0) {
        return {0, 0};
    }
    return {start, std::min(start + page_size, total_items)};
}

} // namespace qiqi
