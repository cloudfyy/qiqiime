// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ime/engine/engine.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::dictdetail {

inline constexpr size_t kMaxDeltaWordChars = sync::Channel::kMaxPayloadBytes / sizeof(uint16_t);
inline constexpr int kMaxSyncPublishAttempts = 16;
inline constexpr size_t kMaxIncrementalDictDiffOperations = 16;

struct SourceMeta {
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint64_t hash = 0;
};

struct DictDiffOp {
    sync::Operation op = sync::Operation::Add;
    uint32_t packed_code = 0;
    std::wstring word;
};

constexpr size_t LeadingUtf16CodepointUnits(std::wstring_view text) noexcept {
    if (text.empty()) return 0;
    return text.size() >= 2 && IS_HIGH_SURROGATE(text.front()) && IS_LOW_SURROGATE(gsl::at(text, 1)) ? 2u : 1u;
}

constexpr bool IsSingleUtf16Codepoint(std::wstring_view text) noexcept {
    const size_t units = LeadingUtf16CodepointUnits(text);
    return text.size() == units &&
        (units == 2 || (units == 1 && !IS_HIGH_SURROGATE(text.front()) && !IS_LOW_SURROGATE(text.front())));
}

constexpr uint32_t SingleUtf16Scalar(std::wstring_view text) noexcept {
    if (!IsSingleUtf16Codepoint(text)) return 0;
    const uint32_t first = text.front();
    return text.size() == 1 ? first :
        0x10000u + ((first - 0xD800u) << 10) + (gsl::at(text, 1) - 0xDC00u);
}

std::vector<std::wstring_view> SplitUtf16Codepoints(std::wstring_view text);

bool MatchesFullWubiCode(const FullCodeIndex& index, uint32_t packed_code, std::wstring_view word);

std::vector<uint8_t> EncodeDictEntryDelta(
    uint32_t packed_code, std::wstring_view word, std::wstring_view new_word = {});

bool DecodeDictEntryDelta(
    const sync::Change& change,
    uint32_t& out_packed_code,
    std::wstring& out_word,
    std::wstring& out_new_word);

std::vector<uint8_t> EncodePhraseFreqSnapshot(
    const std::map<uint32_t, std::vector<std::wstring>>& promoted_phrases);

void FilterMultiCharacterPhrasesOnly(
    std::map<uint32_t, std::vector<std::wstring>>& entries);

bool DecodePhraseFreqSnapshot(
    std::span<const uint8_t> bytes,
    std::map<uint32_t, std::vector<std::wstring>>& out_promoted);

void PromotePhraseInMap(
    std::map<uint32_t, std::vector<std::wstring>>& promoted_phrases,
    uint32_t packed_code,
    std::wstring_view word);

std::string SerializeDictEntriesToUtf8(
    const std::map<uint32_t, std::vector<std::wstring>>& entries);

HRESULT ReadDictEntriesFromFileResult(
    const std::filesystem::path& file_path,
    std::map<uint32_t, std::vector<std::wstring>>& out_entries,
    std::string* out_raw_utf8 = nullptr,
    bool allow_missing = true);

bool ReadDictEntriesFromFile(
    const std::filesystem::path& file_path,
    std::map<uint32_t, std::vector<std::wstring>>& out_entries,
    std::string* out_raw_utf8 = nullptr);

bool ReadSourceMeta(const std::filesystem::path& meta_path, SourceMeta& out_meta);
HRESULT WriteSourceMeta(const std::filesystem::path& meta_path, const SourceMeta& meta);
bool QueryFileSizeAndMtime(
    const std::filesystem::path& path, uint64_t& out_size, uint64_t& out_mtime) noexcept;
HRESULT WriteUpdatedUserDictFiles(
    const std::filesystem::path& dict_path,
    const std::filesystem::path& channel_dir,
    const std::map<uint32_t, std::vector<std::wstring>>& entries);

bool ComputeBoundedDictDiff(
    const std::map<uint32_t, std::vector<std::wstring>>& old_entries,
    const std::map<uint32_t, std::vector<std::wstring>>& new_entries,
    std::vector<DictDiffOp>& out_ops,
    size_t max_ops = kMaxIncrementalDictDiffOperations);

bool EraseWordFromCodeMap(
    std::map<uint32_t, std::vector<std::wstring>>& entries,
    uint32_t packed,
    std::wstring_view word);

bool ReplaceWordInCodeMap(
    std::map<uint32_t, std::vector<std::wstring>>& entries,
    uint32_t packed,
    std::wstring_view old_word,
    std::wstring_view new_word);

} // namespace qiqi::dictdetail
