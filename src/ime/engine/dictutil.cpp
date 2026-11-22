// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace qiqi::dictdetail {

std::vector<std::wstring_view> SplitUtf16Codepoints(std::wstring_view text) {
    std::vector<std::wstring_view> chars;
    size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = gsl::at(text, i);
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < text.size()) {
            const wchar_t low = gsl::at(text, i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                chars.push_back(text.substr(i, 2));
                i += 2;
                continue;
            }
        }
        chars.push_back(text.substr(i, 1));
        ++i;
    }
    return chars;
}

std::vector<uint8_t> EncodeDictEntryDelta(
    uint32_t packed_code, std::wstring_view word, std::wstring_view new_word) {
    std::vector<uint8_t> payload;
    codec::AppendLe32(payload, packed_code);
    codec::AppendWString(payload, word);
    if (!new_word.empty()) {
        codec::AppendWString(payload, new_word);
    }
    return payload;
}

bool DecodeDictEntryDelta(
    const sync::Change& change,
    uint32_t& out_packed_code,
    std::wstring& out_word,
    std::wstring& out_new_word) {
    codec::BinarySpanReader reader(change.payload);
    out_packed_code = reader.ReadLe32();
    out_word = reader.ReadWString(kMaxDeltaWordChars);
    out_new_word.clear();
    if (change.operation == sync::Operation::Modify) {
        out_new_word = reader.ReadWString(kMaxDeltaWordChars);
        if (out_new_word.empty()) {
            return false;
        }
    }
    return reader.Valid() && reader.Empty() && out_packed_code != 0 &&
        PackWubiCode(UnpackWubiCode(out_packed_code)) == out_packed_code && !out_word.empty();
}

std::vector<uint8_t> EncodePhraseFreqSnapshot(
    const std::map<uint32_t, std::vector<std::wstring>>& promoted_phrases) {
    std::vector<uint8_t> bytes;
    WubiMappedDict::CompileEntriesToBinaryBuffer(
        promoted_phrases, bytes, codec::kPhraseFreqBinaryMagic, codec::kPhraseFreqBinaryVer);
    return bytes;
}

void FilterMultiCharacterPhrasesOnly(std::map<uint32_t, std::vector<std::wstring>>& entries) {
    for (auto it = entries.begin(); it != entries.end();) {
        std::erase_if(it->second, [](const std::wstring& w) {
            return SplitUtf16Codepoints(w).size() < 2;
        });
        if (it->second.empty()) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

bool DecodePhraseFreqSnapshot(
    std::span<const uint8_t> bytes,
    std::map<uint32_t, std::vector<std::wstring>>& out_promoted) {
    std::map<uint32_t, std::vector<std::wstring>> parsed;
    if (!WubiMappedDict::DecodeBinaryBufferToEntries(
            bytes, parsed, codec::kPhraseFreqBinaryMagic, codec::kPhraseFreqBinaryVer)) {
        return false;
    }
    FilterMultiCharacterPhrasesOnly(parsed);
    out_promoted = std::move(parsed);
    return true;
}

void PromotePhraseInMap(
    std::map<uint32_t, std::vector<std::wstring>>& promoted_phrases,
    uint32_t packed_code,
    std::wstring_view word) {
    if (packed_code == 0 || SplitUtf16Codepoints(word).size() < 2) {
        return;
    }
    auto& list = promoted_phrases[packed_code];
    std::erase(list, word);
    list.insert(list.begin(), std::wstring(word));
}

std::string SerializeDictEntriesToUtf8(const std::map<uint32_t, std::vector<std::wstring>>& entries) {
    std::string out;
    for (const auto& [packed, words] : entries) {
        if (packed == 0 || words.empty()) {
            continue;
        }
        const std::string code_utf8 = WideToUtf8(UnpackWubiCode(packed));
        for (const auto& w : words) {
            if (!w.empty()) {
                out += code_utf8;
                out.push_back(' ');
                out += WideToUtf8(w);
                out.push_back('\n');
            }
        }
    }
    return out;
}

HRESULT ReadDictEntriesFromFileResult(
    const std::filesystem::path& file_path,
    std::map<uint32_t, std::vector<std::wstring>>& out_entries,
    std::string* out_raw_utf8,
    bool allow_missing) {
    out_entries.clear();
    std::error_code ec;
    const bool exists = std::filesystem::exists(file_path, ec);
    if (ec) {
        QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary,
            "ReadDictEntriesFromFile: existence check failed (error={})", ec.value());
        return HRESULT_FROM_WIN32(ec.value());
    }
    if (!exists) {
        if (!allow_missing) {
            constexpr HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            std::ignore = log::CheckHResult(hr, "ReadDictEntriesFromFile::required snapshot");
            return hr;
        }
        if (out_raw_utf8) {
            out_raw_utf8->clear();
        }
        return S_OK;
    }
    std::string raw_utf8;
    const HRESULT read_hr = ReadFileToUtf8StringResult(file_path, raw_utf8);
    if (FAILED(read_hr)) return read_hr;
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_utf8)), out_entries, true);
    if (out_raw_utf8) {
        *out_raw_utf8 = std::move(raw_utf8);
    }
    return S_OK;
}

bool ReadDictEntriesFromFile(
    const std::filesystem::path& file_path,
    std::map<uint32_t, std::vector<std::wstring>>& out_entries,
    std::string* out_raw_utf8) {
    return SUCCEEDED(ReadDictEntriesFromFileResult(file_path, out_entries, out_raw_utf8));
}

bool ReadSourceMeta(const std::filesystem::path& meta_path, SourceMeta& out_meta) {
    std::string raw;
    if (!ReadFileToUtf8String(meta_path, raw) || raw.size() != 36) {
        return false;
    }
    codec::BinarySpanReader reader(codec::AsByteSpan(raw));
    if (reader.ReadLe64() != codec::kSourceMetaBinaryMagic ||
        reader.ReadLe32() != codec::kSourceMetaBinaryVer) {
        return false;
    }
    out_meta.size = reader.ReadLe64();
    out_meta.mtime = reader.ReadLe64();
    out_meta.hash = reader.ReadLe64();
    return reader.Valid() && reader.Empty();
}

HRESULT WriteSourceMeta(const std::filesystem::path& meta_path, const SourceMeta& meta) {
    std::vector<uint8_t> bytes;
    bytes.reserve(36);
    codec::AppendLe64(bytes, codec::kSourceMetaBinaryMagic);
    codec::AppendLe32(bytes, codec::kSourceMetaBinaryVer);
    codec::AppendLe64(bytes, meta.size);
    codec::AppendLe64(bytes, meta.mtime);
    codec::AppendLe64(bytes, meta.hash);
    return WriteFileAtomicallyResult(meta_path, bytes);
}

bool QueryFileSizeAndMtime(const std::filesystem::path& path, uint64_t& out_size, uint64_t& out_mtime) noexcept {
    std::error_code ec;
    out_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    out_mtime = gsl::narrow_cast<uint64_t>(
        std::filesystem::last_write_time(path, ec).time_since_epoch().count());
    return !ec;
}

HRESULT WriteUpdatedUserDictFiles(
    const std::filesystem::path& dict_path,
    const std::filesystem::path& channel_dir,
    const std::map<uint32_t, std::vector<std::wstring>>& entries) {
    const std::string serialized = SerializeDictEntriesToUtf8(entries);
    const std::span<const uint8_t> bytes = codec::AsByteSpan(serialized);
    HRESULT hr = WriteFileAtomicallyResult(dict_path, bytes);
    if (FAILED(hr)) {
        return hr;
    }
    hr = WriteFileAtomicallyResult(channel_dir / L"snapshot.txt", bytes);
    if (FAILED(hr)) {
        return hr;
    }
    std::filesystem::path user_qdict_path = dict_path;
    user_qdict_path.replace_extension(L".qdict");
    if (WubiMappedDict::CompileTextFileToQdict(dict_path, user_qdict_path)) {
        WubiMappedDict::InvalidateProcessCacheForPath(dict_path);
    }
    SourceMeta meta{};
    if (QueryFileSizeAndMtime(dict_path, meta.size, meta.mtime)) {
        meta.hash = codec::ComputeFnv1a64(bytes);
        std::ignore = WriteSourceMeta(channel_dir / L"source.meta", meta);
    }
    return S_OK;
}

bool ComputeBoundedDictDiff(
    const std::map<uint32_t, std::vector<std::wstring>>& old_entries,
    const std::map<uint32_t, std::vector<std::wstring>>& new_entries,
    std::vector<DictDiffOp>& out_ops,
    size_t max_ops) {
    out_ops.clear();
    auto it_old = old_entries.begin();
    auto it_new = new_entries.begin();

    auto diff_code_words = [&](uint32_t packed,
                               const std::vector<std::wstring>& old_words,
                               const std::vector<std::wstring>& new_words) -> bool {
        // Check that surviving words kept their relative order; if reordered, require a full snapshot.
        size_t last_new_idx = 0;
        bool first_survivor = true;
        for (const auto& w : old_words) {
            const auto found = std::find(new_words.begin(), new_words.end(), w);
            if (found == new_words.end()) {
                out_ops.push_back({sync::Operation::Remove, packed, w});
                if (out_ops.size() > max_ops) {
                    return false;
                }
            } else {
                const size_t idx = gsl::narrow_cast<size_t>(found - new_words.begin());
                if (!first_survivor && idx < last_new_idx) {
                    return false;
                }
                last_new_idx = idx;
                first_survivor = false;
            }
        }
        // Any new words must appear after existing survivors (since AddEntry appends to the code's word list).
        size_t survivor_count = 0;
        for (size_t i = 0; i < new_words.size(); ++i) {
            const auto& w = gsl::at(new_words, i);
            if (std::find(old_words.begin(), old_words.end(), w) != old_words.end()) {
                if (i != survivor_count) {
                    return false; // New word was inserted in the middle rather than appended
                }
                ++survivor_count;
            } else {
                out_ops.push_back({sync::Operation::Add, packed, w});
                if (out_ops.size() > max_ops) {
                    return false;
                }
            }
        }
        return true;
    };

    while (it_old != old_entries.end() || it_new != new_entries.end()) {
        if (it_new == new_entries.end() || (it_old != old_entries.end() && it_old->first < it_new->first)) {
            if (!diff_code_words(it_old->first, it_old->second, {})) {
                return false;
            }
            ++it_old;
        } else if (it_old == old_entries.end() || it_new->first < it_old->first) {
            if (!diff_code_words(it_new->first, {}, it_new->second)) {
                return false;
            }
            ++it_new;
        } else {
            if (it_old->second != it_new->second &&
                !diff_code_words(it_old->first, it_old->second, it_new->second)) {
                return false;
            }
            ++it_old;
            ++it_new;
        }
    }
    return !out_ops.empty() && out_ops.size() <= max_ops;
}

bool EraseWordFromCodeMap(
    std::map<uint32_t, std::vector<std::wstring>>& entries,
    uint32_t packed,
    std::wstring_view word) {
    if (const auto it = entries.find(packed); it != entries.end()) {
        const size_t removed = std::erase(it->second, word);
        if (it->second.empty()) {
            entries.erase(it);
        }
        return removed > 0;
    }
    return false;
}

bool ReplaceWordInCodeMap(
    std::map<uint32_t, std::vector<std::wstring>>& entries,
    uint32_t packed,
    std::wstring_view old_word,
    std::wstring_view new_word) {
    if (const auto it = entries.find(packed); it != entries.end()) {
        const auto pos = std::find(it->second.begin(), it->second.end(), old_word);
        if (pos != it->second.end()) {
            if (std::find(it->second.begin(), it->second.end(), new_word) == it->second.end()) {
                *pos = std::wstring(new_word);
            } else {
                it->second.erase(pos);
            }
            return true;
        }
    }
    return false;
}

} // namespace qiqi::dictdetail
