// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/logger.h"

#include <tuple>
#include <utility>
#include <vector>

namespace qiqi {

using dictdetail::DecodeDictEntryDelta;
using dictdetail::DecodePhraseFreqSnapshot;
using dictdetail::EncodeDictEntryDelta;
using dictdetail::EncodePhraseFreqSnapshot;
using dictdetail::EraseWordFromCodeMap;
using dictdetail::FilterMultiCharacterPhrasesOnly;
using dictdetail::kMaxSyncPublishAttempts;
using dictdetail::PromotePhraseInMap;
using dictdetail::SplitUtf16Codepoints;

namespace {

HRESULT WritePhraseFreqSnapshot(const std::filesystem::path& path,
    const std::map<uint32_t, std::vector<std::wstring>>& entries) {
    try {
        const auto bytes = EncodePhraseFreqSnapshot(entries);
        const HRESULT written = WriteFileAtomicallyResult(path, bytes);
        if (FAILED(written)) return written;
        const auto directory = GetSyncChannelDirectoryForFile(path);
        const HRESULT created = sync::Channel::Create(directory,
            {sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency});
        if (FAILED(created)) return created;
        return WriteFileAtomicallyResult(directory / L"snapshot.qfreq", bytes);
    } catch (const std::bad_alloc&) {
        std::ignore = log::CheckHResult(E_OUTOFMEMORY, "WritePhraseFreqSnapshot");
        return E_OUTOFMEMORY;
    }
}

} // namespace

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
    return SUCCEEDED(PromoteUserPhraseFrequencyResult(code, word, file_path));
}

HRESULT WubiEngine::PromoteUserPhraseFrequencyResult(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& file_path) {
    if (SplitUtf16Codepoints(word).size() < 2) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "PromoteUserPhraseFrequency phrase length");
        return E_INVALIDARG;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "PromoteUserPhraseFrequency code");
        return E_INVALIDARG;
    }
    const std::filesystem::path target_path = !file_path.empty() ? file_path : phrase_freq_path_;
    if (target_path.empty()) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        return S_OK;
    }
    if (IsProcessAppContainer()) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        return E_ACCESSDENIED;
    }
    SetPhraseFreqPath(target_path);

    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    const sync::Source source{sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency};
    const HRESULT create_hr = sync::Channel::Create(channel_dir, source);
    if (FAILED(create_hr)) {
        PromotePhraseInMap(promoted_phrases_, packed, word);
        if (!SavePhraseFrequencies(target_path)) {
            QIQI_LOG_CAT_WARN(log::Keyword::Dictionary, "PromoteUserPhraseFrequency: in-memory order updated but persistence failed");
            return create_hr;
        }
        return S_OK;
    }
    const sync::Channel channel(channel_dir, source);
    const std::vector<uint8_t> payload = EncodeDictEntryDelta(packed, word);
    HRESULT last_error = HRESULT_FROM_WIN32(ERROR_RETRY);
    for (int attempt = 0; attempt < kMaxSyncPublishAttempts; ++attempt) {
        std::ignore = SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
        PromotePhraseInMap(promoted_phrases_, packed, word);
        sync::Batch current_batch;
        const HRESULT read_hr = channel.Read(phrase_freq_sync_version_, current_batch);
        if (FAILED(read_hr)) {
            last_error = read_hr;
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
            return S_OK;
        }
        last_error = FAILED(hr) ? hr : E_UNEXPECTED;
        if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
            hr != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            break;
        }
        ::SwitchToThread();
    }
    QIQI_LOG_CAT_WARN(log::Keyword::Dictionary,
        "PromoteUserPhraseFrequency: in-memory order updated but coordinated persistence failed (hr=0x{:08x})",
        gsl::narrow_cast<unsigned int>(last_error));
    return last_error;
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
    for (int attempt = 0; attempt < kMaxSyncPublishAttempts; ++attempt) {
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

HRESULT WubiEngine::ClearPhraseFrequenciesResult(const std::filesystem::path& file_path) {
    const std::filesystem::path target_path = !file_path.empty() ? file_path : phrase_freq_path_;
    if (target_path.empty()) {
        promoted_phrases_.clear();
        return S_OK;
    }
    if (IsProcessAppContainer()) {
        std::ignore = log::CheckHResult(E_ACCESSDENIED, "ClearPhraseFrequencies");
        return E_ACCESSDENIED;
    }

    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    ScopedFileHandle folder;
    const HRESULT opened = OpenDataItemNoReparse(channel_dir, true, 0, folder);
    if (!log::CheckHResult(opened, "ClearPhraseFrequencies::OpenChannel")) return opened;
    if (opened == S_OK) {
        const HRESULT removed = RemoveFileNoReparse(channel_dir / L"journal.bin");
        if (!log::CheckHResult(removed, "ClearPhraseFrequencies::RemoveJournal")) return removed;
    }

    const HRESULT saved = WritePhraseFreqSnapshot(target_path, {});
    if (FAILED(saved)) return saved;
    SetPhraseFreqPath(target_path);
    promoted_phrases_.clear();
    phrase_freq_sync_version_ = {};
    const sync::Channel channel(channel_dir, {sync::DataType::PhraseFrequency, kGuidSyncSourcePhraseFrequency});
    sync::Batch batch;
    if (SUCCEEDED(channel.Read({}, batch))) {
        phrase_freq_sync_version_ = batch.version;
    }
    return S_OK;
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
    if (file_path.empty() || IsProcessAppContainer()) {
        std::ignore = log::CheckHResult(file_path.empty() ? E_INVALIDARG : E_ACCESSDENIED, "SavePhraseFrequencies");
        return false;
    }
    return SUCCEEDED(WritePhraseFreqSnapshot(file_path, promoted_phrases_));
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
    if (!IsProcessAppContainer()) {
        const bool present = std::filesystem::exists(freq_path, ec);
        if (ec) {
            const HRESULT error = HRESULT_FROM_WIN32(ec.value());
            std::ignore = log::CheckHResult(error, "Phrase sync::CheckSource");
            return error;
        }
        if (!present) {
            const bool has_snapshot = std::filesystem::exists(channel_dir / L"snapshot.qfreq", ec);
            if (ec) {
                const HRESULT error = HRESULT_FROM_WIN32(ec.value());
                std::ignore = log::CheckHResult(error, "Phrase sync::CheckSnapshot");
                return error;
            }
            if (!has_snapshot) return S_OK;
            const HRESULT cleared = sync::Channel::ClearOrphanedFiles(freq_path);
            if (cleared == S_OK) {
                phrase_freq_sync_version_ = {};
            }
            if (cleared != S_FALSE) return cleared;
        }
    }
    if (!std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        if (ec) {
            const HRESULT error = HRESULT_FROM_WIN32(ec.value());
            std::ignore = log::CheckHResult(error, "Phrase sync::CheckJournal");
            return error;
        }
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
            promoted_phrases_.swap(loaded);
            ++phrase_freq_snapshot_syncs_;
            return S_OK;
        },
        [&](std::span<const sync::Change> changes) -> HRESULT {
            auto staged = promoted_phrases_;
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
                    PromotePhraseInMap(staged, packed, word);
                } else if (change.operation == sync::Operation::Remove) {
                    std::ignore = EraseWordFromCodeMap(staged, packed, word);
                } else {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
            promoted_phrases_.swap(staged);
            phrase_freq_incremental_syncs_ += changes.size();
            return S_OK;
        });
}

} // namespace qiqi
