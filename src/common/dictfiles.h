// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace qiqi::dictionary_files {

inline constexpr std::array kUserFiles{L"user_dict.txt", L"user_dict.qdict", L"phrase_freq.qfreq"};
inline constexpr std::array kSyncDirectories{L"user_dict.txt.sync", L"phrase_freq.qfreq.sync"};
inline constexpr std::array kSyncFiles{L"snapshot.txt", L"snapshot.qdict", L"snapshot.qfreq",
    L"source.meta", L"journal.bin", L"writer.lock"};

// Only generated cache names in the managed cache directory, never source dictionaries.
constexpr bool IsCompiledCache(std::wstring_view name) noexcept {
    if (!name.ends_with(L".qdict")) return false;
    const auto stem = name.substr(0, name.size() - 6);
    for (const std::wstring_view bundled : {L"wubi86_jidian", L"wubi86_jidian_extra", L"wubi86_haifeng"}) {
        if (stem == bundled) return true;
    }
    const auto separator = stem.rfind(L'-');
    if (separator == std::wstring_view::npos || separator == 0 || stem.size() - separator != 17) return false;
    const auto source = stem.substr(0, separator);
    if (source.back() == L'.' || source.back() == L' ' ||
        std::ranges::any_of(source, [](wchar_t character) noexcept {
            return character < 32 || std::wstring_view(L"\\/:*?\"<>|").contains(character);
        })) return false;
    return std::ranges::all_of(stem.substr(separator + 1), [](wchar_t character) noexcept {
        return (character >= L'0' && character <= L'9') || (character >= L'A' && character <= L'F');
    });
}

} // namespace qiqi::dictionary_files
