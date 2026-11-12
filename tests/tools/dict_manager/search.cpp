// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestSmartSearch() {
    const DictRecord record{1, PackWubiCode(L"khlg"), L"khlg", L"中国IME2026!", DictEntrySource::UserDict, 1};
    DictFilter filter;
    for (const auto& [query, expected] : std::array{
             std::pair{L"", true}, std::pair{L" \t\r\n ", true},
             std::pair{L" KH ", true}, std::pair{L"hL", true}, std::pair{L"khlg", true},
             std::pair{L"khlga", false}, std::pair{L"aaaa", false},
             std::pair{L"中国", true}, std::pair{L" 国 ", true},
             std::pair{L"不存在", false}, std::pair{L"IME", false},
             std::pair{L"国IME", true}, std::pair{L"2026", true}, std::pair{L"!", true},
             std::pair{L"k h", false}, std::pair{L"kh中国", false}}) {
        filter.SetQuery(query);
        QIQI_TEST_CHECK(filter.Matches(record) == expected);
    }
    filter.SetQuery(L"中国");
    filter.source = DictEntrySource::PrimaryDict;
    QIQI_TEST_CHECK(!filter.Matches(record));
    filter.source = DictEntrySource::UserDict;
    QIQI_TEST_CHECK(filter.Matches(record));
    filter.SetQuery(L"KH");
    QIQI_TEST_CHECK(filter.Matches(record));
    filter.SetQuery(L" \t ");
    filter.source = DictEntrySource::PrimaryDict;
    QIQI_TEST_CHECK(!filter.Matches(record));
}

} // namespace qiqi::dict_manager::tests
