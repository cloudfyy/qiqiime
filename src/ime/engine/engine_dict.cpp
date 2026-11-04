#include "ime/engine/engine.h"
#include "ime/engine/default_dict.h"
#include "common/binary_codec.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <span>

namespace qiqi {

namespace {

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
    uint32_t packed_code, std::wstring_view word, std::wstring_view new_word = {}) {
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
    out_word = reader.ReadWString(64);
    out_new_word.clear();
    if (change.operation == sync::Operation::Modify) {
        out_new_word = reader.ReadWString(64);
        if (out_new_word.empty()) {
            return false;
        }
    }
    return reader.Valid() && reader.Empty() && out_packed_code != 0 && !out_word.empty();
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

bool ReadDictEntriesFromFile(
    const std::filesystem::path& file_path,
    std::map<uint32_t, std::vector<std::wstring>>& out_entries,
    std::string* out_raw_utf8 = nullptr) {
    out_entries.clear();
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        if (out_raw_utf8) {
            out_raw_utf8->clear();
        }
        return true;
    }
    std::string raw_utf8;
    if (!ReadFileToUtf8String(file_path, raw_utf8)) {
        return false;
    }
    WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_utf8)), out_entries);
    if (out_raw_utf8) {
        *out_raw_utf8 = std::move(raw_utf8);
    }
    return true;
}

struct SourceMeta {
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint64_t hash = 0;
};

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
        if (dict_path == GetDefaultUserDictPath()) {
            std::string qdict_raw;
            if (ReadFileToUtf8String(user_qdict_path, qdict_raw)) {
                SaveUserDictBinaryToRegistry(codec::AsByteSpan(qdict_raw));
            }
        }
    }
    SourceMeta meta{};
    if (QueryFileSizeAndMtime(dict_path, meta.size, meta.mtime)) {
        meta.hash = codec::ComputeFnv1a64(bytes);
        std::ignore = WriteSourceMeta(channel_dir / L"source.meta", meta);
    }
    return S_OK;
}

struct DictDiffOp {
    sync::Operation op = sync::Operation::Add;
    uint32_t packed_code = 0;
    std::wstring word;
};

bool ComputeBoundedDictDiff(
    const std::map<uint32_t, std::vector<std::wstring>>& old_entries,
    const std::map<uint32_t, std::vector<std::wstring>>& new_entries,
    std::vector<DictDiffOp>& out_ops,
    size_t max_ops = 16) {
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

} // namespace

bool WubiEngine::IsEntryRemoved(uint32_t packed_code, std::wstring_view word) const {
    if (const auto it = removed_entries_.find(packed_code); it != removed_entries_.end()) {
        return std::find(it->second.begin(), it->second.end(), word) != it->second.end();
    }
    return false;
}

void WubiEngine::AddEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
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
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return false;
    }
    EraseWordFromCodeMap(dict_, packed, word);
    AppendUniqueWord(removed_entries_[packed], word);
    return true;
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
        AppendUniqueWord(removed_entries_[packed], old_word);
        EraseWordFromCodeMap(removed_entries_, packed, new_word);
        return true;
    }
    RemoveEntry(code, old_word);
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

std::wstring WubiEngine::EncodePhraseWubi86(std::wstring_view phrase) const {
    const auto chars = SplitUtf16Codepoints(phrase);
    if (chars.size() < 2) {
        return {};
    }

    auto resolve_char_code = [this](std::wstring_view ch) -> std::wstring {
        std::wstring c = LookupCharacterCode(ch);
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

template <typename FileMutator, typename MemoryMutator>
bool WubiEngine::MutateUserDictionaryEntry(
    std::wstring_view code,
    std::wstring_view word,
    std::wstring_view new_word,
    sync::Operation op,
    const std::filesystem::path& user_dict_path,
    FileMutator mutate_file,
    MemoryMutator mutate_memory) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty() || (op == sync::Operation::Modify && new_word.empty())) {
        return false;
    }
    const std::filesystem::path target_path = !user_dict_path.empty() ? user_dict_path : user_dict_path_;
    if (target_path.empty()) {
        mutate_memory();
        return true;
    }
    if (IsProcessAppContainer()) {
        QIQI_LOG_CAT_DEBUG(log::Keyword::Sync, "User dictionary mutation rejected inside read-only AppContainer");
        return false;
    }
    SetUserDictPath(target_path);
    std::ignore = CheckAndPublishExternalUserDictChanges(target_path);
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    const sync::Source source{sync::DataType::Dictionary, kGuidSyncSourceDictionary};
    if (FAILED(sync::Channel::Create(channel_dir, source))) {
        return false;
    }
    const sync::Channel channel(channel_dir, source);
    const std::vector<uint8_t> payload = EncodeDictEntryDelta(packed, word, new_word);
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::ignore = SynchronizeUserDictionary(sync::InputBoundary::NewInput);
        sync::Batch current_batch;
        if (FAILED(channel.Read(user_dict_sync_version_, current_batch))) {
            return false;
        }
        sync::Version committed{};
        const HRESULT hr = channel.Publish(
            current_batch.version,
            op,
            payload,
            [&]() -> HRESULT {
                std::map<uint32_t, std::vector<std::wstring>> file_entries;
                if (!ReadDictEntriesFromFile(target_path, file_entries)) {
                    return E_FAIL;
                }
                mutate_file(file_entries, packed);
                return WriteUpdatedUserDictFiles(target_path, channel_dir, file_entries);
            },
            committed);
        if (hr == S_OK) {
            mutate_memory();
            user_dict_sync_version_ = committed;
            return true;
        }
        if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
            hr != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            return false;
        }
        ::SwitchToThread();
    }
    return false;
}

bool WubiEngine::AddUserEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    return MutateUserDictionaryEntry(
        code,
        word,
        {},
        sync::Operation::Add,
        user_dict_path,
        [word](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            AppendUniqueWord(file_entries[packed], word);
        },
        [this, code, word]() { AddEntry(code, word); });
}

bool WubiEngine::RemoveUserEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    return MutateUserDictionaryEntry(
        code,
        word,
        {},
        sync::Operation::Remove,
        user_dict_path,
        [word](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            EraseWordFromCodeMap(file_entries, packed, word);
        },
        [this, code, word]() { std::ignore = RemoveEntry(code, word); });
}

bool WubiEngine::ModifyUserEntry(
    std::wstring_view code,
    std::wstring_view old_word,
    std::wstring_view new_word,
    const std::filesystem::path& user_dict_path) {
    return MutateUserDictionaryEntry(
        code,
        old_word,
        new_word,
        sync::Operation::Modify,
        user_dict_path,
        [old_word, new_word](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            if (!ReplaceWordInCodeMap(file_entries, packed, old_word, new_word)) {
                AppendUniqueWord(file_entries[packed], new_word);
            }
        },
        [this, code, old_word, new_word]() { std::ignore = ModifyEntry(code, old_word, new_word); });
}

bool WubiEngine::CheckAndPublishExternalUserDictChanges(const std::filesystem::path& dict_path) {
    if (dict_path.empty() || IsProcessAppContainer()) {
        return false;
    }
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(dict_path);
    std::error_code ec;
    if (!std::filesystem::exists(dict_path, ec)) {
        if (std::filesystem::exists(channel_dir / L"source.meta", ec)) {
            std::filesystem::remove_all(channel_dir, ec);
            user_dict_sync_version_ = {};
        }
        return false;
    }
    uint64_t cur_size = 0;
    uint64_t cur_mtime = 0;
    if (!QueryFileSizeAndMtime(dict_path, cur_size, cur_mtime)) {
        return false;
    }
    const sync::Source source{sync::DataType::Dictionary, kGuidSyncSourceDictionary};
    if (FAILED(sync::Channel::Create(channel_dir, source))) {
        return false;
    }
    const std::filesystem::path meta_path = channel_dir / L"source.meta";
    const std::filesystem::path snapshot_path = channel_dir / L"snapshot.txt";
    SourceMeta meta{};
    const bool had_meta = ReadSourceMeta(meta_path, meta) && std::filesystem::exists(snapshot_path, ec);
    if (had_meta && meta.size == cur_size && meta.mtime == cur_mtime) {
        return false;
    }

    std::map<uint32_t, std::vector<std::wstring>> new_entries;
    std::string raw_utf8;
    if (!ReadDictEntriesFromFile(dict_path, new_entries, &raw_utf8)) {
        return false;
    }
    const std::string normalized_utf8 = SerializeDictEntriesToUtf8(new_entries);
    const std::span<const uint8_t> norm_bytes = codec::AsByteSpan(normalized_utf8);
    const uint64_t cur_hash = codec::ComputeFnv1a64(norm_bytes);
    if (had_meta && meta.hash == cur_hash) {
        meta.size = cur_size;
        meta.mtime = cur_mtime;
        std::ignore = WriteSourceMeta(meta_path, meta);
        return false;
    }

    const sync::Channel channel(channel_dir, source);
    if (!had_meta) {
        std::ignore = WriteFileAtomicallyResult(snapshot_path, norm_bytes);
        std::filesystem::path user_qdict_path = dict_path;
        user_qdict_path.replace_extension(L".qdict");
        if (!std::filesystem::exists(user_qdict_path, ec)) {
            std::ignore = WubiMappedDict::CompileTextFileToQdict(dict_path, user_qdict_path);
        }
        const SourceMeta init_meta{cur_size, cur_mtime, cur_hash};
        std::ignore = WriteSourceMeta(meta_path, init_meta);
        return true;
    }

    const auto persist_external_snapshot = [&]() -> HRESULT {
        if (FAILED(WriteFileAtomicallyResult(snapshot_path, norm_bytes))) {
            return E_FAIL;
        }
        std::filesystem::path qdict_path = dict_path;
        qdict_path.replace_extension(L".qdict");
        if (WubiMappedDict::CompileTextFileToQdict(dict_path, qdict_path)) {
            WubiMappedDict::InvalidateProcessCacheForPath(dict_path);
        }
        const SourceMeta updated_meta{cur_size, cur_mtime, cur_hash};
        return WriteSourceMeta(meta_path, updated_meta);
    };

    std::map<uint32_t, std::vector<std::wstring>> old_entries;
    std::ignore = ReadDictEntriesFromFile(snapshot_path, old_entries);
    std::vector<DictDiffOp> diff_ops;
    if (ComputeBoundedDictDiff(old_entries, new_entries, diff_ops)) {
        sync::Batch batch;
        if (SUCCEEDED(channel.Read({}, batch))) {
            sync::Version ver = batch.version;
            bool all_ok = true;
            for (size_t i = 0; i < diff_ops.size(); ++i) {
                const auto& op = gsl::at(diff_ops, i);
                const bool is_last = (i + 1 == diff_ops.size());
                const std::vector<uint8_t> payload = EncodeDictEntryDelta(op.packed_code, op.word);
                sync::Version next_ver{};
                const HRESULT pub_hr = channel.Publish(
                    ver,
                    op.op,
                    payload,
                    [&]() -> HRESULT {
                        return is_last ? persist_external_snapshot() : S_OK;
                    },
                    next_ver);
                if (pub_hr != S_OK) {
                    all_ok = false;
                    break;
                }
                ver = next_ver;
            }
            if (all_ok) {
                QIQI_LOG_CAT_INFO(
                    log::Keyword::Sync,
                    "External dictionary modification synced incrementally ({} delta ops)",
                    diff_ops.size());
                return true;
            }
        }
    }

    // Large replacement or non-diffable external edit: update snapshot and force a new baseline for full reload.
    sync::Batch batch;
    if (SUCCEEDED(channel.Read({}, batch))) {
        sync::Version dummy{};
        std::ignore = channel.Publish(
            batch.version,
            sync::Operation::Add,
            {},
            [&]() -> HRESULT {
                if (FAILED(persist_external_snapshot())) {
                    return E_FAIL;
                }
                return S_FALSE; // Returning S_FALSE intentionally rotates the channel baseline GUID for full snapshot reload!
            },
            dummy);
        QIQI_LOG_CAT_INFO(
            log::Keyword::Sync,
            "External dictionary replacement detected; rotated baseline for full snapshot reload");
        return true;
    }
    return false;
}

HRESULT WubiEngine::SynchronizeUserDictionary(sync::InputBoundary boundary) {
    if (boundary == sync::InputBoundary::Composing) {
        return S_FALSE;
    }
    const std::filesystem::path dict_path = !user_dict_path_.empty() ? user_dict_path_ : GetDefaultUserDictPath();
    if (dict_path.empty()) {
        return S_OK;
    }
    std::ignore = CheckAndPublishExternalUserDictChanges(dict_path);
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(dict_path);
    std::error_code ec;
    if (!std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        return S_OK;
    }
    const sync::Source source{sync::DataType::Dictionary, kGuidSyncSourceDictionary};
    const sync::Channel channel(channel_dir, source);
    const bool is_startup_sync = (user_dict_sync_version_.baseline == GUID_NULL);
    return channel.Synchronize(
        user_dict_sync_version_,
        boundary,
        [&]() -> HRESULT {
            std::map<uint32_t, std::vector<std::wstring>> loaded;
            const std::filesystem::path snapshot_path = channel_dir / L"snapshot.txt";
            const std::filesystem::path read_path =
                std::filesystem::exists(snapshot_path, ec) ? snapshot_path : dict_path;
            if (!ReadDictEntriesFromFile(read_path, loaded)) {
                return E_FAIL;
            }
            if (!is_startup_sync) {
                // Refresh any existing mapped view of `dict_path` in place when reloading after a remote change.
                WubiMappedDict::InvalidateProcessCacheForPath(dict_path);
                const std::wstring target_canon = std::filesystem::weakly_canonical(dict_path, ec).wstring();
                for (auto& m : mapped_dicts_) {
                    if (!m || m->SourcePath().empty()) continue;
                    std::error_code m_ec;
                    if (std::filesystem::weakly_canonical(m->SourcePath(), m_ec).wstring() == target_canon) {
                        if (auto refreshed = WubiMappedDict::LoadFromTextOrCache(dict_path)) {
                            m = std::move(refreshed);
                        }
                    }
                }
            }
            dict_ = std::move(loaded);
            removed_entries_.clear();
            ++user_dict_snapshot_syncs_;
            return S_OK;
        },
        [&](std::span<const sync::Change> changes) -> HRESULT {
            for (const auto& change : changes) {
                uint32_t packed = 0;
                std::wstring word;
                std::wstring new_word;
                if (!DecodeDictEntryDelta(change, packed, word, new_word)) {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                const std::wstring code = UnpackWubiCode(packed);
                switch (change.operation) {
                    case sync::Operation::Add:
                        AddEntry(code, word);
                        break;
                    case sync::Operation::Remove:
                        RemoveEntry(code, word);
                        break;
                    case sync::Operation::Modify:
                        ModifyEntry(code, word, new_word);
                        break;
                    default:
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
            user_dict_incremental_syncs_ += changes.size();
            return S_OK;
        });
}

bool WubiEngine::LoadDictionaryFromBinaryBuffer(std::vector<uint8_t> qdict_bytes) {
    auto mapped = WubiMappedDict::LoadFromBinaryBuffer(std::move(qdict_bytes));
    if (!mapped) {
        return false;
    }
    mapped_dicts_.push_back(std::move(mapped));
    return true;
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    auto mapped = WubiMappedDict::LoadFromTextOrCache(file_path);
    if (!mapped) {
        QIQI_LOG_WARN("Failed to load or map external dictionary file: {}",
                      WideToUtf8(file_path.wstring()));
        return false;
    }
    if (std::find(mapped_dicts_.begin(), mapped_dicts_.end(), mapped) == mapped_dicts_.end()) {
        QIQI_LOG_INFO("Attached external dictionary '{}' ({} codes, {} words, mapped={})",
                      WideToUtf8(file_path.wstring()),
                      mapped->CodeCount(),
                      mapped->WordCount(),
                      mapped->IsMemoryMapped());
        mapped_dicts_.push_back(std::move(mapped));
    }
    return true;
}

bool WubiEngine::HasModifiedMappedDictionary() const noexcept {
    for (const auto& dict : mapped_dicts_) {
        if (dict && dict->IsSourceModified()) {
            return true;
        }
    }
    return false;
}

void WubiEngine::PromotePhraseForCode(std::wstring_view code, std::wstring_view word) {
    if (!dynamic_phrase_freq_) {
        return;
    }
    std::ignore = PromoteUserPhraseFrequency(code, word, phrase_freq_path_);
}

bool WubiEngine::PromoteUserPhraseFrequency(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& file_path) {
    if (SplitUtf16Codepoints(word).size() < 2) {
        return false;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return false;
    }
    const std::filesystem::path target_path = !file_path.empty() ? file_path : phrase_freq_path_;
    if (target_path.empty()) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        return true;
    }
    if (IsProcessAppContainer()) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        return false;
    }
    SetPhraseFreqPath(target_path);

    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    const sync::Source source{sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency};
    if (FAILED(sync::Channel::Create(channel_dir, source))) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        if (!SavePhraseFrequencies(target_path)) {
            QIQI_LOG_CAT_WARN(log::Keyword::Dictionary, "PromoteUserPhraseFrequency: in-memory order updated but persistence failed");
            return false;
        }
        return true;
    }
    const sync::Channel channel(channel_dir, source);
    const std::vector<uint8_t> payload = EncodeDictEntryDelta(packed, word);
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::ignore = SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
        PromotePhraseInMap(promoted_phrases_, packed, word);
        sync::Batch current_batch;
        if (FAILED(channel.Read(phrase_freq_sync_version_, current_batch))) {
            break;
        }
        sync::Version committed{};
        const HRESULT hr = channel.Publish(
            current_batch.version,
            sync::Operation::Reorder,
            payload,
            [&]() -> HRESULT {
                const std::vector<uint8_t> snapshot_bytes = EncodePhraseFreqSnapshot(promoted_phrases_);
                const HRESULT file_hr = WriteFileAtomicallyResult(target_path, snapshot_bytes);
                if (FAILED(file_hr)) {
                    return file_hr;
                }
                return WriteFileAtomicallyResult(channel_dir / L"snapshot.qfreq", snapshot_bytes);
            },
            committed);
        if (hr == S_OK) {
            phrase_freq_sync_version_ = committed;
            return true;
        }
        if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
            hr != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            break;
        }
        ::SwitchToThread();
    }
    QIQI_LOG_CAT_WARN(log::Keyword::Dictionary, "PromoteUserPhraseFrequency: in-memory order updated but coordinated persistence failed");
    return false;
}

bool WubiEngine::RemovePhraseFrequency(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& file_path) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty()) {
        return false;
    }
    const std::filesystem::path target_path = !file_path.empty() ? file_path : phrase_freq_path_;
    if (target_path.empty()) {
        return EraseWordFromCodeMap(promoted_phrases_, packed, word);
    }
    if (IsProcessAppContainer()) {
        return false;
    }
    SetPhraseFreqPath(target_path);

    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    const sync::Source source{sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency};
    if (FAILED(sync::Channel::Create(channel_dir, source))) {
        return false;
    }
    const sync::Channel channel(channel_dir, source);
    const std::vector<uint8_t> payload = EncodeDictEntryDelta(packed, word);
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::ignore = SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
        if (!EraseWordFromCodeMap(promoted_phrases_, packed, word)) {
            return false;
        }
        sync::Batch current_batch;
        if (FAILED(channel.Read(phrase_freq_sync_version_, current_batch))) {
            break;
        }
        sync::Version committed{};
        const HRESULT hr = channel.Publish(
            current_batch.version,
            sync::Operation::Remove,
            payload,
            [&]() -> HRESULT {
                const std::vector<uint8_t> snapshot_bytes = EncodePhraseFreqSnapshot(promoted_phrases_);
                const HRESULT file_hr = WriteFileAtomicallyResult(target_path, snapshot_bytes);
                if (FAILED(file_hr)) {
                    return file_hr;
                }
                return WriteFileAtomicallyResult(channel_dir / L"snapshot.qfreq", snapshot_bytes);
            },
            committed);
        if (hr == S_OK) {
            phrase_freq_sync_version_ = committed;
            return true;
        }
        if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
            hr != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            break;
        }
        ::SwitchToThread();
    }
    return false;
}

bool WubiEngine::ClearPhraseFrequencies(const std::filesystem::path& file_path) {
    const std::filesystem::path target_path = !file_path.empty() ? file_path : phrase_freq_path_;
    promoted_phrases_.clear();
    if (target_path.empty()) {
        return true;
    }
    if (IsProcessAppContainer()) {
        return false;
    }
    SetPhraseFreqPath(target_path);

    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    std::error_code ec;
    if (std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        std::filesystem::remove(channel_dir / L"journal.bin", ec);
    }
    if (!SavePhraseFrequencies(target_path)) {
        return false;
    }
    const sync::Channel channel(channel_dir, {sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency});
    sync::Batch batch;
    if (SUCCEEDED(channel.Read({}, batch))) {
        phrase_freq_sync_version_ = batch.version;
    }
    return true;
}

bool WubiEngine::LoadPhraseFrequencies(const std::filesystem::path& file_path) {
    if (file_path.empty()) {
        return false;
    }
    SetPhraseFreqPath(file_path);
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(file_path);
    const std::filesystem::path snapshot_path = channel_dir / L"snapshot.qfreq";

    std::string raw_bytes;
    if (!ReadFileToUtf8String(file_path, raw_bytes)) {
        if (!IsProcessAppContainer() || !ReadFileToUtf8String(snapshot_path, raw_bytes)) {
            std::error_code rm_ec;
            if (!IsProcessAppContainer() && std::filesystem::exists(channel_dir, rm_ec)) {
                std::filesystem::remove_all(channel_dir, rm_ec);
            }
            return false;
        }
    }

    const std::span<const uint8_t> byte_span = codec::AsByteSpan(raw_bytes);
    std::map<uint32_t, std::vector<std::wstring>> parsed;
    if (!DecodePhraseFreqSnapshot(byte_span, parsed)) {
        // Backward-compatible fallback for legacy UTF-8 text format (code word1 word2 ...)
        WubiMappedDict::ParseTextDictEntries(Utf8ToWide(StripUtf8Bom(raw_bytes)), parsed);
        FilterMultiCharacterPhrasesOnly(parsed);
    }
    promoted_phrases_ = std::move(parsed);

    std::error_code ec;
    if (std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        const sync::Channel channel(channel_dir, {sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency});
        sync::Batch batch;
        if (SUCCEEDED(channel.Read({}, batch))) {
            phrase_freq_sync_version_ = batch.version;
        }
    }
    return true;
}

bool WubiEngine::SavePhraseFrequencies(const std::filesystem::path& file_path) const {
    try {
        if (file_path.empty() || IsProcessAppContainer()) {
            return false;
        }
        const std::vector<uint8_t> bytes = EncodePhraseFreqSnapshot(promoted_phrases_);
        if (!WriteFileAtomically(file_path, bytes)) {
            return false;
        }
        const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(file_path);
        const sync::Source source{sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency};
        if (SUCCEEDED(sync::Channel::Create(channel_dir, source))) {
            std::ignore = WriteFileAtomicallyResult(channel_dir / L"snapshot.qfreq", bytes);
        }
        return true;
    } catch (...) {
        QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary, "SavePhraseFrequencies failed with a C++ exception");
        return false;
    }
}

HRESULT WubiEngine::SynchronizePhraseFrequencies(sync::InputBoundary boundary) {
    if (boundary == sync::InputBoundary::Composing) {
        return S_FALSE;
    }
    const std::filesystem::path freq_path = !phrase_freq_path_.empty() ? phrase_freq_path_ : GetDefaultPhraseFreqPath();
    if (freq_path.empty()) {
        return S_OK;
    }
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(freq_path);
    std::error_code ec;
    if (!IsProcessAppContainer() && !std::filesystem::exists(freq_path, ec)) {
        if (std::filesystem::exists(channel_dir / L"snapshot.qfreq", ec)) {
            std::filesystem::remove_all(channel_dir, ec);
            phrase_freq_sync_version_ = {};
        }
        return S_OK;
    }
    if (!std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        return S_OK;
    }
    const sync::Source source{sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency};
    const sync::Channel channel(channel_dir, source);
    return channel.Synchronize(
        phrase_freq_sync_version_,
        boundary,
        [&]() -> HRESULT {
            const std::filesystem::path snapshot_path = channel_dir / L"snapshot.qfreq";
            std::string raw;
            if (!ReadFileToUtf8String(snapshot_path, raw) && !ReadFileToUtf8String(freq_path, raw)) {
                return E_FAIL;
            }
            const std::span<const uint8_t> bytes = codec::AsByteSpan(raw);
            std::map<uint32_t, std::vector<std::wstring>> loaded;
            if (!DecodePhraseFreqSnapshot(bytes, loaded)) {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            promoted_phrases_ = std::move(loaded);
            ++phrase_freq_snapshot_syncs_;
            return S_OK;
        },
        [&](std::span<const sync::Change> changes) -> HRESULT {
            for (const auto& change : changes) {
                uint32_t packed = 0;
                std::wstring word;
                std::wstring unused_new;
                if (!DecodeDictEntryDelta(change, packed, word, unused_new)) {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                if (change.operation == sync::Operation::Reorder) {
                    if (SplitUtf16Codepoints(word).size() < 2) {
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    }
                    PromotePhraseInMap(promoted_phrases_, packed, word);
                } else if (change.operation == sync::Operation::Remove) {
                    std::ignore = EraseWordFromCodeMap(promoted_phrases_, packed, word);
                } else {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
            phrase_freq_incremental_syncs_ += changes.size();
            return S_OK;
        });
}

bool WubiEngine::SynchronizeCrossProcessChanges(sync::InputBoundary boundary) {
    if (boundary == sync::InputBoundary::Composing) {
        return false;
    }
    const HRESULT dict_hr = SynchronizeUserDictionary(boundary);
    const HRESULT freq_hr = dynamic_phrase_freq_ ? SynchronizePhraseFrequencies(boundary) : S_OK;
    for (auto& mapped : mapped_dicts_) {
        if (mapped && mapped->IsSourceModified()) {
            const std::filesystem::path src_path = mapped->SourcePath();
            WubiMappedDict::InvalidateProcessCacheForPath(src_path);
            if (auto reloaded = WubiMappedDict::LoadFromTextOrCache(src_path)) {
                mapped = std::move(reloaded);
            }
        }
    }
    return SUCCEEDED(dict_hr) && SUCCEEDED(freq_hr);
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return {};
    }

    std::vector<std::wstring> results;
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupExact(packed, results);
    }
    if (!removed_entries_.empty()) {
        std::erase_if(results, [&](const std::wstring& w) { return IsEntryRemoved(packed, w); });
    }
    if (const auto it = dict_.find(packed); it != dict_.end()) {
        for (const auto& w : it->second) {
            AppendUniqueWord(results, w);
        }
    }
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code == packed) {
            if (!IsEntryRemoved(packed, entry.word)) {
                AppendUniqueWord(results, entry.word);
            }
        } else if (entry.packed_code > packed) {
            break;
        }
    }

    if (dynamic_phrase_freq_ && results.size() > 1) {
        if (const auto pit = promoted_phrases_.find(packed); pit != promoted_phrases_.end()) {
            // Keep all single-character entries in their fixed positions;
            // reorder only multi-character phrase slots so promoted phrases appear first.
            std::vector<size_t> phrase_slots;
            std::vector<std::wstring> phrases;
            for (size_t i = 0; i < results.size(); ++i) {
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
        const auto append_wildcard_word = [&](uint32_t packed_code, std::wstring_view word) {
            const size_t prev_sz = results.size();
            AppendUniqueWord(results, word);
            if (out_display_labels && results.size() > prev_sz) {
                out_display_labels->push_back(std::wstring(word) + L"(" + UnpackWubiCode(packed_code) + L")");
            }
            return results.size() >= kMaxCandidateResults;
        };
        for (const auto& mapped : mapped_dicts_) {
            mapped->LookupWildcard(code, results, kMaxCandidateResults, out_display_labels);
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
    results = LookupExact(code);
    if (results.size() > kMaxCandidateResults) {
        results.resize(kMaxCandidateResults);
    }
    if (out_display_labels) {
        *out_display_labels = results;
    }
    if (results.size() >= kMaxCandidateResults) {
        return results;
    }

    // 2. Prefix matches in (exact_code, prefix_hi]
    const uint32_t prefix_hi = PackWubiPrefixUpperBound(code);
    const auto append_prefix_word = [&](uint32_t packed_code, std::wstring_view word) {
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
            exact_code, exact_code, prefix_hi, results, kMaxCandidateResults, code, out_display_labels);
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
