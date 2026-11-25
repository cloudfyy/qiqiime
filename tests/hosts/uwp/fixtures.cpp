// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

#include <cwctype>
#include <ranges>
#include <string>
#include <utility>

namespace qiqi::host {
namespace {

bool EqualTerm(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return std::ranges::equal(left, right, [](wchar_t a, wchar_t b) noexcept {
        return std::towlower(a) == std::towlower(b);
    });
}

std::wstring_view HStringView(winrt::hstring const& value) noexcept {
    return {value.c_str(), value.size()};
}

std::vector<winrt::hstring> ReadStringArray(
    winrt::Windows::Data::Json::JsonArray const& values) {
    std::vector<winrt::hstring> result;
    result.reserve(values.Size());
    for (const auto& value : values) {
        result.push_back(value.GetString());
    }
    return result;
}

} // namespace

winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
ReadPackagedFixtureTextAsync() {
    const auto file =
        co_await winrt::Windows::Storage::StorageFile::GetFileFromApplicationUriAsync(
            winrt::Windows::Foundation::Uri(L"ms-appx:///Assets/TestData.json"));
    co_return co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
}

FixtureSet ParseFixtureSet(std::wstring_view text) {
    const auto root =
        winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring(text));
    FixtureSet result;

    const auto record_values = root.GetNamedArray(L"records");
    result.records.reserve(record_values.Size());
    for (const auto& value : record_values) {
        const auto object = value.GetObject();
        TestRecord record{
            .id = object.GetNamedString(L"id"),
            .title = object.GetNamedString(L"title"),
            .terms = ReadStringArray(object.GetNamedArray(L"terms")),
        };
        result.records.push_back(std::move(record));
    }

    const auto fixture_values = root.GetNamedArray(L"fixtures");
    result.fixtures.reserve(fixture_values.Size());
    for (const auto& value : fixture_values) {
        const auto object = value.GetObject();
        LinguisticFixture fixture{
            .id = object.GetNamedString(L"id"),
            .query = object.GetNamedString(L"query"),
            .alternatives = ReadStringArray(object.GetNamedArray(L"alternatives")),
            .composition_start = static_cast<uint32_t>(
                object.GetNamedNumber(L"compositionStart")),
            .composition_length = static_cast<uint32_t>(
                object.GetNamedNumber(L"compositionLength")),
            .expected_matches = static_cast<size_t>(
                object.GetNamedNumber(L"expectedMatches")),
        };
        result.fixtures.push_back(std::move(fixture));
    }
    return result;
}

std::vector<size_t> MatchRecords(
    const std::vector<TestRecord>& records,
    const Observation& observation) {
    std::vector<size_t> matches;
    for (size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        const bool matched = std::ranges::any_of(
            record.terms,
            [&observation](winrt::hstring const& term) {
                if (EqualTerm(HStringView(term), HStringView(observation.query))) {
                    return true;
                }
                return std::ranges::any_of(
                    observation.alternatives,
                    [&term](winrt::hstring const& alternative) {
                        return EqualTerm(
                            HStringView(term),
                            HStringView(alternative));
                    });
            });
        if (matched) {
            matches.push_back(index);
        }
    }
    return matches;
}

FixtureResult EvaluateFixture(
    const FixtureSet& fixture_set,
    const LinguisticFixture& fixture) {
    const Observation observation{
        .query = fixture.query,
        .alternatives = fixture.alternatives,
        .composition_start = fixture.composition_start,
        .composition_length = fixture.composition_length,
        .has_linguistic_details = true,
    };
    const auto matches = MatchRecords(fixture_set.records, observation);
    return FixtureResult{
        .id = fixture.id,
        .expected_matches = fixture.expected_matches,
        .actual_matches = matches.size(),
    };
}

} // namespace qiqi::host
