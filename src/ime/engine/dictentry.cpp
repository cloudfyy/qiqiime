// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "ime/engine/builtindict.h"
#include "ime/engine/fullcode.h"
#include "common/logger.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace qiqi {

using dictdetail::EraseWordFromCodeMap;
using dictdetail::ReplaceWordInCodeMap;
using dictdetail::SplitUtf16Codepoints;

bool WubiEngine::IsEntryRemoved(uint32_t packed_code, std::wstring_view word) const {
    if (const auto it = removed_entries_.find(packed_code); it != removed_entries_.end()) {
        return std::ranges::contains(it->second, word);
    }
    return false;
}

void WubiEngine::SwapDictionaryState(WubiEngine& other) noexcept {
    dict_.swap(other.dict_);
    pinned_entries_.swap(other.pinned_entries_);
    removed_entries_.swap(other.removed_entries_);
    promoted_phrases_.swap(other.promoted_phrases_);
}

void WubiEngine::AddEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return;
    }
    if (word.starts_with(L'^')) {
        word.remove_prefix(1);
        PinEntry(code, word);
        return;
    }
    if (word.starts_with(L'!')) {
        word.remove_prefix(1);
        BlockEntry(code, word);
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }
    EraseWordFromCodeMap(removed_entries_, packed, word);
    AppendUniqueWord(dict_[packed], word);
}

bool WubiEngine::RemoveEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return false;
    }
    if (word.starts_with(L'^')) {
        word.remove_prefix(1);
        return UnpinEntry(code, word);
    }
    if (word.starts_with(L'!')) {
        word.remove_prefix(1);
        return UnblockEntry(code, word);
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return false;
    }
    EraseWordFromCodeMap(dict_, packed, word);
    EraseWordFromCodeMap(pinned_entries_, packed, word);
    EraseWordFromCodeMap(promoted_phrases_, packed, word);
    return true;
}

void WubiEngine::PinEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }
    EraseWordFromCodeMap(removed_entries_, packed, word);
    auto& list = pinned_entries_[packed];
    std::erase(list, word);
    list.insert(list.begin(), std::wstring(word));
    AppendUniqueWord(dict_[packed], word);
}

bool WubiEngine::UnpinEntry(std::wstring_view code, std::wstring_view word) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty()) {
        return false;
    }
    const bool removed = EraseWordFromCodeMap(pinned_entries_, packed, word);
    if (removed) {
        AppendUniqueWord(dict_[packed], word);
    }
    return removed;
}

void WubiEngine::BlockEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }
    EraseWordFromCodeMap(dict_, packed, word);
    EraseWordFromCodeMap(pinned_entries_, packed, word);
    EraseWordFromCodeMap(promoted_phrases_, packed, word);
    AppendUniqueWord(removed_entries_[packed], word);
}

bool WubiEngine::UnblockEntry(std::wstring_view code, std::wstring_view word) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty()) {
        return false;
    }
    return EraseWordFromCodeMap(removed_entries_, packed, word);
}

bool WubiEngine::ModifyEntry(std::wstring_view code, std::wstring_view old_word, std::wstring_view new_word) {
    if (old_word.empty() || new_word.empty()) {
        return false;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return false;
    }
    if (old_word == new_word) {
        return true;
    }
    if (ReplaceWordInCodeMap(dict_, packed, old_word, new_word)) {
        EraseWordFromCodeMap(removed_entries_, packed, new_word);
        return true;
    }
    BlockEntry(code, old_word);
    AddEntry(code, new_word);
    return true;
}

std::wstring WubiEngine::LookupCharacterCode(std::wstring_view ch) const {
    if (ch.empty()) {
        return {};
    }
    std::wstring best;
    for (const auto& mapped : mapped_dicts_) {
        std::wstring code = mapped->FindLongestCodeForWord(ch);
        if (code.size() > best.size()) {
            best = std::move(code);
            if (best.size() >= kMaxCodeLength) {
                return best;
            }
        }
    }
    for (const auto& [packed, words] : dict_) {
        if (std::find(words.begin(), words.end(), ch) != words.end()) {
            std::wstring code = UnpackWubiCode(packed);
            if (code.size() > best.size()) {
                best = std::move(code);
            }
        }
    }
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.word == ch) {
            std::wstring code = UnpackWubiCode(entry.packed_code);
            if (code.size() > best.size()) {
                best = std::move(code);
            }
        }
    }
    return best;
}

namespace {

std::wstring EncodePhraseFromCodes(std::span<const std::wstring_view> chars, const auto& lookup_code) {
    if (chars.size() < 2) {
        return {};
    }

    const auto resolve_char_code = [&lookup_code](std::wstring_view ch) {
        auto c = lookup_code(ch);
        if (c.size() == 1) {
            c.push_back(c.front());
        }
        return c;
    };

    if (chars.size() == 2) {
        const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
        const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
        if (c1.size() < 2 || c2.size() < 2) {
            return {};
        }
        return std::wstring(c1.substr(0, 2)) + std::wstring(c2.substr(0, 2));
    }

    if (chars.size() == 3) {
        const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
        const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
        const std::wstring c3 = resolve_char_code(gsl::at(chars, 2));
        if (c1.empty() || c2.empty() || c3.size() < 2) {
            return {};
        }
        std::wstring res;
        res.push_back(c1.front());
        res.push_back(c2.front());
        res.append(c3.substr(0, 2));
        return res;
    }

    const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
    const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
    const std::wstring c3 = resolve_char_code(gsl::at(chars, 2));
    const std::wstring c_last = resolve_char_code(chars.back());
    if (c1.empty() || c2.empty() || c3.empty() || c_last.empty()) {
        return {};
    }
    return {c1.front(), c2.front(), c3.front(), c_last.front()};
}

} // namespace

std::wstring WubiEngine::EncodePhraseWubi86(std::wstring_view phrase) const {
    const auto characters = SplitUtf16Codepoints(phrase);
    return EncodePhraseFromCodes(characters, [this](std::wstring_view character) {
        return LookupCharacterCode(character);
    });
}

bool dictdetail::MatchesFullWubiCode(const FullCodeIndex& index, uint32_t packed_code, std::wstring_view word) {
    if (packed_code == 0) return false;
    if (IsSingleUtf16Codepoint(word)) return packed_code == index.CharacterCode(word);
    if ((packed_code & 0xFFu) == 0) return false; // The Wubi 86 phrase rule produces four keys.
    const auto characters = SplitUtf16Codepoints(word);
    if (std::ranges::any_of(characters, [&index](std::wstring_view character) noexcept {
            return index.CharacterCode(character) == 0;
        })) return false;
    return packed_code == PackWubiCode(EncodePhraseFromCodes(characters, [&index](std::wstring_view character) {
        return UnpackWubiCode(index.CharacterCode(character));
    }));
}

bool WubiEngine::AddUserPhrase(std::wstring_view phrase, const std::filesystem::path& user_dict_path) {
    const std::wstring code = EncodePhraseWubi86(phrase);
    if (code.empty()) {
        return false;
    }
    if (!AddUserEntry(code, phrase, user_dict_path)) {
        return false;
    }
    QIQI_LOG_CAT_INFO(log::Keyword::Dictionary, "Added user phrase (length={})", SplitUtf16Codepoints(phrase).size());
    return true;
}

} // namespace qiqi
