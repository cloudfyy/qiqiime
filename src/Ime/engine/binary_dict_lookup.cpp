#include "Ime/engine/binary_dict.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qiqi {

std::wstring_view WubiMappedDict::GetWordView(const DictWordSlice& slice) const noexcept {
    if (static_cast<size_t>(slice.char_offset) + slice.char_length > char_pool_.size()) {
        return {};
    }
    const auto sub = char_pool_.subspan(slice.char_offset, slice.char_length);
    return std::wstring_view(sub.data(), sub.size());
}

bool WubiMappedDict::AppendWordsForCodeEntry(
    const DictCodeIndex& entry,
    std::vector<std::wstring>& out_words,
    size_t max_results) const {
    const size_t end_idx = std::min<size_t>(
        static_cast<size_t>(entry.first_word_idx) + entry.word_count, word_slices_.size());
    for (size_t i = entry.first_word_idx; i < end_idx; ++i) {
        const std::wstring_view word = GetWordView(gsl::at(word_slices_, i));
        if (!word.empty()) {
            AppendUniqueWord(out_words, word);
            if (out_words.size() >= max_results) {
                return true;
            }
        }
    }
    return false;
}

void WubiMappedDict::LookupExact(
    uint32_t packed_code, std::vector<std::wstring>& out_words) const {
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
    AppendWordsForCodeEntry(*it, out_words, SIZE_MAX);
}

void WubiMappedDict::LookupPrefix(
    uint32_t exact_code,
    uint32_t prefix_lo,
    uint32_t prefix_hi,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    std::wstring_view typed_prefix,
    std::vector<std::wstring>* out_display_labels) const {
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
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results);
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
    std::vector<std::wstring>* out_display_labels) const {
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
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results);
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

} // namespace qiqi
