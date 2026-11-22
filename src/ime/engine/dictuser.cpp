// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dictdetail.h"
#include "ime/engine/builtindict.h"
#include "common/guids.h"
#include "common/logger.h"

#include <algorithm>
#include <ranges>
#include <tuple>
#include <vector>

namespace qiqi {

using dictdetail::EncodeDictEntryDelta;
using dictdetail::EraseWordFromCodeMap;
using dictdetail::kMaxSyncPublishAttempts;
using dictdetail::ReadDictEntriesFromFileResult;
using dictdetail::ReplaceWordInCodeMap;
using dictdetail::WriteUpdatedUserDictFiles;

template <typename FileMutator, typename MemoryMutator>
HRESULT WubiEngine::MutateUserDictionaryEntry(
    std::wstring_view code,
    std::wstring_view word,
    std::wstring_view new_word,
    sync::Operation op,
    const std::filesystem::path& user_dict_path,
    FileMutator mutate_file,
    MemoryMutator mutate_memory,
    bool reset_baseline,
    bool require_original) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty() || (op == sync::Operation::Modify && new_word.empty())) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "MutateUserDictionaryEntry");
        return E_INVALIDARG;
    }
    const std::filesystem::path target_path = !user_dict_path.empty() ? user_dict_path : user_dict_path_;
    const auto contains_original = [packed, word](const auto& entries) {
        const auto found = entries.find(packed);
        return found != entries.end() && std::ranges::contains(found->second, word);
    };
    if (target_path.empty()) {
        if (require_original && !contains_original(dict_)) {
            std::ignore = log::CheckHResult(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "ReplaceUserEntry original missing");
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        mutate_memory();
        return S_OK;
    }
    if (IsProcessAppContainer()) {
        QIQI_LOG_CAT_DEBUG(log::Keyword::Sync, "User dictionary mutation rejected inside read-only AppContainer");
        return E_ACCESSDENIED;
    }
    SetUserDictPath(target_path);
    std::ignore = CheckAndPublishExternalUserDictChanges(target_path);
    const std::filesystem::path channel_dir = GetSyncChannelDirectoryForFile(target_path);
    const sync::Source source{sync::DataType::Dictionary, kGuidSyncSourceDictionary};
    const HRESULT create_hr = sync::Channel::Create(channel_dir, source);
    if (FAILED(create_hr)) return create_hr;
    const sync::Channel channel(channel_dir, source);
    const std::vector<uint8_t> payload = EncodeDictEntryDelta(packed, word, new_word);
    for (int attempt = 0; attempt < kMaxSyncPublishAttempts; ++attempt) {
        std::ignore = SynchronizeUserDictionary(sync::InputBoundary::NewInput);
        sync::Batch current_batch;
        const HRESULT read_hr = channel.Read(user_dict_sync_version_, current_batch);
        if (FAILED(read_hr)) return read_hr;
        sync::Version committed{};
        const HRESULT hr = channel.Publish(
            current_batch.version,
            op,
            payload,
            [&]() -> HRESULT {
                std::map<uint32_t, std::vector<std::wstring>> file_entries;
                const HRESULT source_hr = ReadDictEntriesFromFileResult(target_path, file_entries);
                if (FAILED(source_hr)) return source_hr;
                if (require_original && !contains_original(file_entries)) {
                    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                }
                mutate_file(file_entries, packed);
                const HRESULT write_hr = WriteUpdatedUserDictFiles(target_path, channel_dir, file_entries);
                if (FAILED(write_hr)) return write_hr;
                // A multi-code edit needs a single source write and a snapshot reload in older clients.
                return reset_baseline ? S_FALSE : S_OK;
            },
            committed);
        if (hr == S_OK || (reset_baseline && hr == S_FALSE)) {
            mutate_memory();
            user_dict_sync_version_ = reset_baseline ? sync::Version{} : committed;
            if (user_dict_sharing_) {
                const HRESULT publish_hr = SaveUserDictSyncDirectory(channel_dir);
                if (FAILED(publish_hr)) return publish_hr;
            }
            return S_OK;
        }
        if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
            hr != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)) {
            return FAILED(hr) ? hr : E_UNEXPECTED;
        }
        ::SwitchToThread();
    }
    return HRESULT_FROM_WIN32(ERROR_RETRY);
}

bool WubiEngine::AddUserEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    return SUCCEEDED(AddUserEntryResult(code, word, user_dict_path));
}

HRESULT WubiEngine::AddUserEntryResult(
    std::wstring_view code, std::wstring_view word,
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
    return SUCCEEDED(MutateUserDictionaryEntry(
        code,
        word,
        {},
        sync::Operation::Remove,
        user_dict_path,
        [word](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            EraseWordFromCodeMap(file_entries, packed, word);
        },
        [this, code, word]() { std::ignore = RemoveEntry(code, word); }));
}

bool WubiEngine::ModifyUserEntry(
    std::wstring_view code,
    std::wstring_view old_word,
    std::wstring_view new_word,
    const std::filesystem::path& user_dict_path) {
    return SUCCEEDED(MutateUserDictionaryEntry(
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
        [this, code, old_word, new_word]() { std::ignore = ModifyEntry(code, old_word, new_word); }));
}

bool WubiEngine::ReplaceUserEntry(
    std::wstring_view old_code, std::wstring_view old_word,
    std::wstring_view new_code, std::wstring_view new_word,
    const std::filesystem::path& user_dict_path) {
    return SUCCEEDED(ReplaceUserEntryResult(old_code, old_word, new_code, new_word, user_dict_path));
}

HRESULT WubiEngine::ReplaceUserEntryResult(
    std::wstring_view old_code, std::wstring_view old_word,
    std::wstring_view new_code, std::wstring_view new_word,
    const std::filesystem::path& user_dict_path) {
    const uint32_t new_packed = PackWubiCode(new_code);
    if (new_packed == 0 || new_word.empty()) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "ReplaceUserEntry");
        return E_INVALIDARG;
    }
    return MutateUserDictionaryEntry(
        old_code, old_word, new_word, sync::Operation::Modify, user_dict_path,
        [old_word, new_packed, new_word](std::map<uint32_t, std::vector<std::wstring>>& entries, uint32_t old_packed) {
            if (old_packed == new_packed) {
                if (old_word != new_word) {
                    std::ignore = ReplaceWordInCodeMap(entries, old_packed, old_word, new_word);
                }
            } else {
                EraseWordFromCodeMap(entries, old_packed, old_word);
                AppendUniqueWord(entries[new_packed], new_word);
            }
        },
        [this, old_code, old_word, new_code, new_word]() {
            if (old_code == new_code) {
                std::ignore = ModifyEntry(old_code, old_word, new_word);
            } else {
                std::ignore = RemoveEntry(old_code, old_word);
                AddEntry(new_code, new_word);
            }
        },
        old_code != new_code, true);
}

bool WubiEngine::PinUserEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    return SUCCEEDED(PinUserEntryResult(code, word, user_dict_path));
}

HRESULT WubiEngine::PinUserEntryResult(
    std::wstring_view code, std::wstring_view word, const std::filesystem::path& user_dict_path) {
    if (word.empty()) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "PinUserEntry word");
        return E_INVALIDARG;
    }
    const std::wstring pinned_token = L"^" + std::wstring(word);
    return MutateUserDictionaryEntry(
        code,
        pinned_token,
        {},
        sync::Operation::Add,
        user_dict_path,
        [word, pinned_token](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            EraseWordFromCodeMap(file_entries, packed, L"!" + std::wstring(word));
            EraseWordFromCodeMap(file_entries, packed, pinned_token);
            auto& list = file_entries[packed];
            list.insert(list.begin(), pinned_token);
        },
        [this, code, word]() { PinEntry(code, word); });
}

bool WubiEngine::UnpinUserEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    if (word.empty()) {
        return false;
    }
    const std::wstring pinned_token = L"^" + std::wstring(word);
    return SUCCEEDED(MutateUserDictionaryEntry(
        code,
        pinned_token,
        {},
        sync::Operation::Remove,
        user_dict_path,
        [word, pinned_token](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            if (EraseWordFromCodeMap(file_entries, packed, pinned_token) &&
                std::find(file_entries[packed].begin(), file_entries[packed].end(), word) == file_entries[packed].end()) {
                file_entries[packed].push_back(std::wstring(word));
            }
        },
        [this, code, word]() { std::ignore = UnpinEntry(code, word); }));
}

bool WubiEngine::BlockSystemEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    return SUCCEEDED(BlockSystemEntryResult(code, word, user_dict_path));
}

HRESULT WubiEngine::BlockSystemEntryResult(
    std::wstring_view code, std::wstring_view word, const std::filesystem::path& user_dict_path) {
    if (word.empty()) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "BlockSystemEntry word");
        return E_INVALIDARG;
    }
    const std::wstring block_token = L"!" + std::wstring(word);
    return MutateUserDictionaryEntry(
        code,
        block_token,
        {},
        sync::Operation::Add,
        user_dict_path,
        [word, block_token](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            EraseWordFromCodeMap(file_entries, packed, word);
            EraseWordFromCodeMap(file_entries, packed, L"^" + std::wstring(word));
            AppendUniqueWord(file_entries[packed], block_token);
        },
        [this, code, word]() { BlockEntry(code, word); });
}

bool WubiEngine::UnblockSystemEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    if (word.empty()) {
        return false;
    }
    const std::wstring block_token = L"!" + std::wstring(word);
    return SUCCEEDED(MutateUserDictionaryEntry(
        code,
        block_token,
        {},
        sync::Operation::Remove,
        user_dict_path,
        [block_token](std::map<uint32_t, std::vector<std::wstring>>& file_entries, uint32_t packed) {
            EraseWordFromCodeMap(file_entries, packed, block_token);
        },
        [this, code, word]() { std::ignore = UnblockEntry(code, word); }));
}

bool WubiEngine::DeleteOrBlockUserOrSystemEntry(
    std::wstring_view code,
    std::wstring_view word,
    const std::filesystem::path& user_dict_path) {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0 || word.empty()) {
        return false;
    }
    // Check if the word is also present in any system/builtin dictionary for this code.
    bool in_system = false;
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code == packed && entry.word == word) {
            in_system = true;
            break;
        }
    }
    if (!in_system) {
        const std::filesystem::path target_path = !user_dict_path.empty() ? user_dict_path : user_dict_path_;
        std::error_code ec;
        const std::wstring user_canon =
            target_path.empty() ? std::wstring{} : std::filesystem::weakly_canonical(target_path, ec).wstring();
        for (const auto& mapped : mapped_dicts_) {
            if (!mapped) continue;
            if (!user_canon.empty() && !mapped->SourcePath().empty() &&
                std::filesystem::weakly_canonical(mapped->SourcePath(), ec).wstring() == user_canon) {
                continue;
            }
            std::vector<std::wstring> sys_words;
            mapped->LookupExact(packed, sys_words);
            if (std::find(sys_words.begin(), sys_words.end(), word) != sys_words.end()) {
                in_system = true;
                break;
            }
        }
    }
    if (in_system) {
        return BlockSystemEntry(code, word, user_dict_path);
    }
    return RemoveUserEntry(code, word, user_dict_path);
}

} // namespace qiqi
