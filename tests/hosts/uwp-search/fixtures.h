// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <winrt/Windows.Foundation.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace qiqi::host {

struct TestRecord {
    winrt::hstring id;
    winrt::hstring title;
    std::vector<winrt::hstring> terms;
};

struct LinguisticFixture {
    winrt::hstring id;
    winrt::hstring query;
    std::vector<winrt::hstring> alternatives;
    uint32_t composition_start = 0;
    uint32_t composition_length = 0;
    size_t expected_matches = 0;
};

struct FixtureSet {
    std::vector<TestRecord> records;
    std::vector<LinguisticFixture> fixtures;
};

struct Observation {
    winrt::hstring query;
    std::vector<winrt::hstring> alternatives;
    uint32_t composition_start = 0;
    uint32_t composition_length = 0;
    bool has_linguistic_details = false;
};

struct FixtureResult {
    winrt::hstring id;
    size_t expected_matches = 0;
    size_t actual_matches = 0;

    bool Passed() const noexcept {
        return expected_matches == actual_matches;
    }
};

[[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
ReadPackagedFixtureTextAsync();

[[nodiscard]] FixtureSet ParseFixtureSet(std::wstring_view text);

[[nodiscard]] std::vector<size_t> MatchRecords(
    const std::vector<TestRecord>& records,
    const Observation& observation);

[[nodiscard]] FixtureResult EvaluateFixture(
    const FixtureSet& fixture_set,
    const LinguisticFixture& fixture);

} // namespace qiqi::host
