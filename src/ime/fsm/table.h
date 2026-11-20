// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <gsl/gsl>
#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace qiqi {

template <typename T>
concept CountedFsmEnum =
    std::is_scoped_enum_v<T> &&
    requires {
        {
            static_cast<size_t>(T::Count)
        } -> std::convertible_to<size_t>;
    };

template <CountedFsmEnum State, CountedFsmEnum Event, typename Result>
using FsmTable = std::array<
    std::array<Result, static_cast<size_t>(Event::Count)>,
    static_cast<size_t>(State::Count)>;

template <CountedFsmEnum State, CountedFsmEnum Event, typename Result>
[[nodiscard]] constexpr FsmTable<State, Event, Result>
MakeFsmTable(const Result& initial)
    noexcept(std::is_nothrow_copy_assignable_v<Result>) {
    FsmTable<State, Event, Result> table{};
    for (auto& row : table) row.fill(initial);
    return table;
}

template <CountedFsmEnum State, CountedFsmEnum Event, typename Result>
constexpr void SetFsmTransition(
    FsmTable<State, Event, Result>& table,
    State state, Event event, const Result& result)
    noexcept(std::is_nothrow_copy_assignable_v<Result>) {
    gsl::at(
        gsl::at(table, std::to_underlying(state)),
        std::to_underlying(event)) = result;
}

template <CountedFsmEnum State, CountedFsmEnum Event, typename Result>
[[nodiscard]] constexpr std::optional<Result> LookupFsmTransition(
    const FsmTable<State, Event, Result>& table,
    State state, Event event)
    noexcept(std::is_nothrow_copy_constructible_v<Result>) {
    const size_t state_index = std::to_underlying(state);
    const size_t event_index = std::to_underlying(event);
    if (state_index >= static_cast<size_t>(State::Count) ||
        event_index >= static_cast<size_t>(Event::Count)) {
        return std::nullopt;
    }
    return gsl::at(gsl::at(table, state_index), event_index);
}

} // namespace qiqi
