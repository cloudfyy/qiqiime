// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "ime/engine/builtindict.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace qiqi {

using dictdetail::SplitUtf16Codepoints;

namespace {

constexpr bool AllowsCandidateWord(std::wstring_view word, bool single_character_only) noexcept {
    return !single_character_only || dictdetail::IsSingleUtf16Codepoint(word);
}

bool AllowsExactCandidate(uint32_t packed_code, std::wstring_view word,
                          bool single_character_only, const FullCodeIndex* full_codes) {
    return AllowsCandidateWord(word, single_character_only) &&
        (!full_codes || dictdetail::MatchesFullWubiCode(*full_codes, packed_code, word));
}

} // namespace

std::wstring_view WubiMappedDict::GetWordView(const DictWordSlice& slice) const noexcept {
    if (gsl::narrow_cast<size_t>(slice.char_offset) + slice.char_length > char_pool_.size()) {
        return {};
    }
    const auto sub = char_pool_.subspan(slice.char_offset, slice.char_length);
    return std::wstring_view(sub.data(), sub.size());
}

bool WubiMappedDict::AppendWordsForCodeEntry(
    const DictCodeIndex& entry,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    bool single_character_only, const FullCodeIndex* full_codes) const {
    const size_t end_idx = std::min<size_t>(
        gsl::narrow_cast<size_t>(entry.first_word_idx) + entry.word_count, word_slices_.size());
    for (size_t i = entry.first_word_idx; i < end_idx; ++i) {
        const std::wstring_view word = GetWordView(gsl::at(word_slices_, i));
        if (!word.empty() && AllowsExactCandidate(entry.packed_code, word, single_character_only, full_codes)) {
            AppendUniqueWord(out_words, word);
            if (out_words.size() >= max_results) {
                return true;
            }
        }
    }
    return false;
}

void WubiMappedDict::LookupExact(
    uint32_t packed_code, std::vector<std::wstring>& out_words,
    bool single_character_only, const FullCodeIndex* full_codes) const {
    if (packed_code == 0 || code_index_.empty()) {
        return;
    }
    const auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        packed_code,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });
    if (it == code_index_.end() || it->packed_code != packed_code) {
        return;
    }
    AppendWordsForCodeEntry(*it, out_words, SIZE_MAX, single_character_only, full_codes);
}

void WubiMappedDict::LookupPrefix(
    uint32_t exact_code,
    uint32_t prefix_lo,
    uint32_t prefix_hi,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    std::wstring_view typed_prefix,
    std::vector<std::wstring>* out_display_labels,
    bool single_character_only) const {
    if (prefix_lo == 0 || code_index_.empty() || out_words.size() >= max_results) {
        return;
    }
    auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        prefix_lo,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });

    for (; it != code_index_.end() && it->packed_code <= prefix_hi; ++it) {
        if (it->packed_code == exact_code) {
            continue;
        }
        const size_t prev_count = out_words.size();
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results, single_character_only);
        if (out_display_labels && out_words.size() > prev_count) {
            const std::wstring code_str = UnpackWubiCode(it->packed_code);
            const std::wstring_view suffix = (code_str.size() > typed_prefix.size())
                ? std::wstring_view(code_str).substr(typed_prefix.size())
                : std::wstring_view(code_str);
            for (size_t idx = prev_count; idx < out_words.size(); ++idx) {
                out_display_labels->push_back(
                    gsl::at(out_words, idx) + L"(~" + std::wstring(suffix) + L")");
            }
        }
        if (reached_cap) {
            return;
        }
    }
}

void WubiMappedDict::LookupWildcard(
    std::wstring_view pattern,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    std::vector<std::wstring>* out_display_labels,
    bool single_character_only) const {
    if (pattern.empty() || code_index_.empty() || out_words.size() >= max_results) {
        return;
    }

    uint32_t lo = 0;
    uint32_t hi = 0xFFFFFFFFu;
    if (pattern.front() != L'z' && pattern.front() != L'Z') {
        const std::wstring_view first_char = pattern.substr(0, 1);
        lo = PackWubiCode(first_char);
        hi = PackWubiPrefixUpperBound(first_char);
    }

    auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        lo,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });

    for (; it != code_index_.end() && it->packed_code <= hi; ++it) {
        if (!MatchesWildcardPattern(it->packed_code, pattern)) {
            continue;
        }
        const size_t prev_count = out_words.size();
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results, single_character_only);
        if (out_display_labels && out_words.size() > prev_count) {
            const std::wstring code_str = UnpackWubiCode(it->packed_code);
            for (size_t idx = prev_count; idx < out_words.size(); ++idx) {
                out_display_labels->push_back(gsl::at(out_words, idx) + L"(" + code_str + L")");
            }
        }
        if (reached_cap) {
            return;
        }
    }
}

std::wstring WubiMappedDict::FindLongestCodeForWord(std::wstring_view word) const {
    if (word.empty() || code_index_.empty()) {
        return {};
    }
    std::wstring best_code;
    for (const auto& entry : code_index_) {
        const size_t end_idx = std::min<size_t>(
            static_cast<size_t>(entry.first_word_idx) + entry.word_count, word_slices_.size());
        for (size_t i = entry.first_word_idx; i < end_idx; ++i) {
            if (GetWordView(gsl::at(word_slices_, i)) == word) {
                std::wstring candidate = UnpackWubiCode(entry.packed_code);
                if (candidate.size() > best_code.size()) {
                    best_code = std::move(candidate);
                    if (best_code.size() >= 4) {
                        return best_code;
                    }
                }
                break;
            }
        }
    }
    return best_code;
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    return LookupExact(PackWubiCode(code), nullptr);
}

std::vector<std::wstring> WubiEngine::LookupInputExact(uint32_t packed) const {
    return LookupExact(packed, full_code_practice_ ? full_code_index_.get() : nullptr);
}

std::vector<std::wstring> WubiEngine::LookupExact(uint32_t packed, const FullCodeIndex* full_codes) const {
    if (packed == 0) {
        return {};
    }

    std::vector<std::wstring> results;
    const auto append_exact_word = [&](std::wstring_view word) {
        if (AllowsExactCandidate(packed, word, single_character_mode_, full_codes) &&
            !IsEntryRemoved(packed, word)) {
            AppendUniqueWord(results, word);
        }
    };
    if (const auto pin_it = pinned_entries_.find(packed); pin_it != pinned_entries_.end()) {
        for (const auto& w : pin_it->second) {
            append_exact_word(w);
        }
    }
    const size_t pinned_count = results.size();

    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupExact(packed, results, single_character_mode_, full_codes);
    }
    if (!removed_entries_.empty()) {
        std::erase_if(results, [&](const std::wstring& w) { return IsEntryRemoved(packed, w); });
    }
    if (const auto it = dict_.find(packed); it != dict_.end()) {
        for (const auto& w : it->second) {
            append_exact_word(w);
        }
    }
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code == packed) {
            append_exact_word(entry.word);
        } else if (entry.packed_code > packed) {
            break;
        }
    }

    if (!single_character_mode_ && dynamic_phrase_freq_ && results.size() > pinned_count + 1) {
        if (const auto pit = promoted_phrases_.find(packed); pit != promoted_phrases_.end()) {
            // Keep manually pinned entries at the very front and single-character entries in their fixed positions;
            // reorder only non-pinned multi-character phrase slots so promoted phrases appear first.
            std::vector<size_t> phrase_slots;
            std::vector<std::wstring> phrases;
            for (size_t i = pinned_count; i < results.size(); ++i) {
                if (SplitUtf16Codepoints(gsl::at(results, i)).size() >= 2) {
                    phrase_slots.push_back(i);
                    phrases.push_back(gsl::at(results, i));
                }
            }
            if (phrases.size() > 1) {
                std::vector<std::wstring> ordered;
                ordered.reserve(phrases.size());
                for (const auto& fav : pit->second) {
                    if (std::find(phrases.begin(), phrases.end(), fav) != phrases.end()) {
                        ordered.push_back(fav);
                    }
                }
                for (auto& ph : phrases) {
                    if (std::find(ordered.begin(), ordered.end(), ph) == ordered.end()) {
                        ordered.push_back(std::move(ph));
                    }
                }
                for (size_t i = 0; i < phrase_slots.size(); ++i) {
                    gsl::at(results, gsl::at(phrase_slots, i)) = std::move(gsl::at(ordered, i));
                }
            }
        }
    }
    return results;
}

std::vector<std::wstring> WubiEngine::LookupCandidates(
    std::wstring_view code, std::vector<std::wstring>* out_display_labels) const {
    const uint32_t exact_code = PackWubiCode(code);
    if (exact_code == 0) {
        return {};
    }

    constexpr size_t kMaxCandidateResults = 50;
    std::vector<std::wstring> results;
    if (out_display_labels) {
        out_display_labels->clear();
    }

    // Wildcard query if 'z' is present
    const bool has_wildcard = (code.find(L'z') != std::wstring_view::npos) ||
                              (code.find(L'Z') != std::wstring_view::npos);
    if (has_wildcard) {
        if (full_code_practice_) return results;
        const auto append_wildcard_word = [&](uint32_t packed_code, std::wstring_view word) {
            if (!AllowsCandidateWord(word, single_character_mode_)) return false;
            const size_t prev_sz = results.size();
            AppendUniqueWord(results, word);
            if (out_display_labels && results.size() > prev_sz) {
                out_display_labels->push_back(std::wstring(word) + L"(" + UnpackWubiCode(packed_code) + L")");
            }
            return results.size() >= kMaxCandidateResults;
        };
        for (const auto& mapped : mapped_dicts_) {
            mapped->LookupWildcard(code, results, kMaxCandidateResults, out_display_labels, single_character_mode_);
            if (results.size() >= kMaxCandidateResults) {
                return results;
            }
        }
        for (const auto& [k, words] : dict_) {
            if (MatchesWildcardPattern(k, code)) {
                for (const auto& w : words) {
                    if (append_wildcard_word(k, w)) {
                        return results;
                    }
                }
            }
        }
        for (const auto& entry : kSortedBuiltinWubi86Dict) {
            if (MatchesWildcardPattern(entry.packed_code, code) &&
                append_wildcard_word(entry.packed_code, entry.word)) {
                return results;
            }
        }
        return results;
    }

    // 1. Exact matches first
    results = LookupInputExact(exact_code);
    if (results.size() > kMaxCandidateResults) {
        results.resize(kMaxCandidateResults);
    }
    if (out_display_labels) {
        *out_display_labels = results;
    }
    if (full_code_practice_ || !enable_code_completion_ || results.size() >= kMaxCandidateResults) {
        return results;
    }

    // 2. Prefix matches in (exact_code, prefix_hi]
    const uint32_t prefix_hi = PackWubiPrefixUpperBound(code);
    const auto append_prefix_word = [&](uint32_t packed_code, std::wstring_view word) {
        if (!AllowsCandidateWord(word, single_character_mode_)) return false;
        const size_t prev_sz = results.size();
        AppendUniqueWord(results, word);
        if (out_display_labels && results.size() > prev_sz) {
            const std::wstring code_str = UnpackWubiCode(packed_code);
            const std::wstring_view suffix = (code_str.size() > code.size())
                ? std::wstring_view(code_str).substr(code.size())
                : std::wstring_view(code_str);
            out_display_labels->push_back(std::wstring(word) + L"(~" + std::wstring(suffix) + L")");
        }
        return results.size() >= kMaxCandidateResults;
    };
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupPrefix(
            exact_code, exact_code, prefix_hi, results, kMaxCandidateResults, code, out_display_labels,
            single_character_mode_);
        if (results.size() >= kMaxCandidateResults) {
            return results;
        }
    }
    for (auto it = dict_.upper_bound(exact_code); it != dict_.end() && it->first <= prefix_hi; ++it) {
        for (const auto& w : it->second) {
            if (!IsEntryRemoved(it->first, w) && append_prefix_word(it->first, w)) {
                return results;
            }
        }
    }
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code <= exact_code) {
            continue;
        }
        if (entry.packed_code > prefix_hi) {
            break;
        }
        if (!IsEntryRemoved(entry.packed_code, entry.word) &&
            append_prefix_word(entry.packed_code, entry.word)) {
            return results;
        }
    }
    return results;
}

} // namespace qiqi
