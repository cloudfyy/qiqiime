// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/logger.h"

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

namespace qiqi {

using dictdetail::ComputeBoundedDictDiff;
using dictdetail::DecodeDictEntryDelta;
using dictdetail::DictDiffOp;
using dictdetail::EncodeDictEntryDelta;
using dictdetail::QueryFileSizeAndMtime;
using dictdetail::ReadDictEntriesFromFile;
using dictdetail::ReadDictEntriesFromFileResult;
using dictdetail::ReadSourceMeta;
using dictdetail::SerializeDictEntriesToUtf8;
using dictdetail::SourceMeta;
using dictdetail::WriteSourceMeta;

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

void WubiEngine::ClearUserDictionaryEntries() noexcept {
    dict_.clear();
    pinned_entries_.clear();
    removed_entries_.clear();
}

HRESULT WubiEngine::InitializeUserDictionarySharing() {
    user_dict_sharing_ = true;
    if (IsProcessAppContainer()) {
        std::filesystem::path directory;
        const HRESULT hr = LoadUserDictSyncDirectory(directory);
        shared_user_dict_status_ = hr;
        if (directory != shared_user_dict_directory_) {
            shared_user_dict_directory_ = std::move(directory);
            user_dict_sync_version_ = {};
            ClearUserDictionaryEntries();
        }
        if (FAILED(hr)) return hr;
        return SynchronizeUserDictionary(sync::InputBoundary::NewInput);
    }
    return SynchronizeUserDictionary(sync::InputBoundary::NewInput);
}

HRESULT WubiEngine::SynchronizeUserDictionary(sync::InputBoundary boundary) {
    if (boundary == sync::InputBoundary::Composing) {
        return S_FALSE;
    }
    const bool shared_reader = user_dict_sharing_ && IsProcessAppContainer();
    const std::filesystem::path dict_path = shared_reader ? std::filesystem::path{} :
        (!user_dict_path_.empty() ? user_dict_path_ : GetDefaultUserDictPath());
    if (shared_reader && shared_user_dict_directory_.empty()) return shared_user_dict_status_;
    if (!shared_reader && dict_path.empty()) {
        return S_OK;
    }
    if (!shared_reader) std::ignore = CheckAndPublishExternalUserDictChanges(dict_path);
    const std::filesystem::path channel_dir = shared_reader ? shared_user_dict_directory_ :
        GetSyncChannelDirectoryForFile(dict_path);
    std::error_code ec;
    if (user_dict_sharing_ && !shared_reader) {
        const bool source_exists = std::filesystem::exists(dict_path, ec);
        if (ec) {
            const HRESULT error = HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value()));
            std::ignore = log::CheckHResult(error, "SynchronizeUserDictionary::source");
            return error;
        }
        if (!source_exists) {
            ClearUserDictionaryEntries();
            std::erase_if(mapped_dicts_, [&dict_path](const auto& dictionary) {
                return dictionary && dictionary->SourcePath() == dict_path;
            });
        }
        const bool ready = source_exists && std::filesystem::exists(channel_dir / L"snapshot.txt", ec);
        if (ec || (source_exists && !ready)) {
            const HRESULT error = HRESULT_FROM_WIN32(ec ?
                gsl::narrow_cast<DWORD>(ec.value()) : ERROR_FILE_NOT_FOUND);
            std::ignore = log::CheckHResult(error, "SynchronizeUserDictionary::snapshot");
            return error;
        }
        const HRESULT publish_hr = SaveUserDictSyncDirectory(ready ? channel_dir : std::filesystem::path{});
        if (FAILED(publish_hr)) return publish_hr;
    }
    if (!std::filesystem::exists(channel_dir / L"journal.bin", ec)) {
        const HRESULT hr = ec ? HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value())) :
            (shared_reader ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : S_OK);
        std::ignore = log::CheckHResult(hr, "SynchronizeUserDictionary::journal");
        return hr;
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
                shared_reader || std::filesystem::exists(snapshot_path, ec) ? snapshot_path : dict_path;
            const HRESULT read_hr = ReadDictEntriesFromFileResult(read_path, loaded, nullptr, !shared_reader);
            if (FAILED(read_hr)) return read_hr;
            WubiEngine staged;
            staged.promoted_phrases_ = promoted_phrases_;
            for (const auto& [packed, words] : loaded) {
                const std::wstring code = UnpackWubiCode(packed);
                for (const auto& w : words) {
                    if (!w.starts_with(L'^') && !w.starts_with(L'!')) {
                        staged.AddEntry(code, w);
                    }
                }
                for (auto it = words.rbegin(); it != words.rend(); ++it) {
                    if (it->starts_with(L'^') || it->starts_with(L'!')) {
                        staged.AddEntry(code, *it);
                    }
                }
            }
            auto staged_mapped = mapped_dicts_;
            if (!is_startup_sync && !shared_reader) {
                // Refresh any existing mapped view of `dict_path` in place when reloading after a remote change.
                WubiMappedDict::InvalidateProcessCacheForPath(dict_path);
                const std::wstring target_canon = std::filesystem::weakly_canonical(dict_path, ec).wstring();
                if (ec) {
                    QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary,
                        "Dictionary sync: canonical path failed (error={})", ec.value());
                    return E_FAIL;
                }
                for (auto& m : staged_mapped) {
                    if (!m || m->SourcePath().empty()) continue;
                    std::error_code m_ec;
                    const auto canonical = std::filesystem::weakly_canonical(m->SourcePath(), m_ec);
                    if (m_ec) {
                        QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary,
                            "Dictionary sync: mapped canonical path failed (error={})", m_ec.value());
                        return E_FAIL;
                    }
                    if (canonical.wstring() == target_canon) {
                        auto refreshed = WubiMappedDict::LoadFromTextOrCache(dict_path);
                        if (!refreshed) return E_FAIL;
                        m = std::move(refreshed);
                    }
                }
            }
            SwapDictionaryState(staged);
            mapped_dicts_.swap(staged_mapped);
            ++user_dict_snapshot_syncs_;
            return S_OK;
        },
        [&](std::span<const sync::Change> changes) -> HRESULT {
            // Reuse entry semantics without exposing a partially applied batch.
            WubiEngine staged;
            staged.dict_ = dict_;
            staged.pinned_entries_ = pinned_entries_;
            staged.removed_entries_ = removed_entries_;
            staged.promoted_phrases_ = promoted_phrases_;
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
                        staged.AddEntry(code, word);
                        break;
                    case sync::Operation::Remove:
                        staged.RemoveEntry(code, word);
                        break;
                    case sync::Operation::Modify:
                        staged.ModifyEntry(code, word, new_word);
                        break;
                    default:
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
            SwapDictionaryState(staged);
            user_dict_incremental_syncs_ += changes.size();
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
            const bool primary = mapped == primary_mapped_dict_;
            auto reloaded = primary
                ? WubiMappedDict::ValidateAndLoadPrimaryDictionary(src_path).dictionary
                : WubiMappedDict::LoadFromTextOrCache(src_path);
            if (reloaded) {
                if (mapped == primary_mapped_dict_) {
                    primary_mapped_dict_ = reloaded;
                }
                if (mapped == extra_mapped_dict_) {
                    extra_mapped_dict_ = reloaded;
                }
                mapped = std::move(reloaded);
            }
        }
    }
    return SUCCEEDED(dict_hr) && SUCCEEDED(freq_hr);
}

} // namespace qiqi
