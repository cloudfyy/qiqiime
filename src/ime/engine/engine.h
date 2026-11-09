#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <optional>
#include <cstdint>

#include "common/change_sync.h"
#include "common/config.h"
#include "ime/engine/binary_dict.h"
#include "ime/fsm/fsm.h"

namespace qiqi {

enum class KeyActionType {
    PassThrough,   // Not handled by IME, let target application handle it
    UpdateState,   // Composition / candidate state updated
    Commit,        // Commit text (and optionally start a new composition)
    Cancel         // Cancel current composition and clear state
};

struct KeyProcessResult {
    KeyActionType action = KeyActionType::PassThrough;
    std::wstring committed_text;
    std::wstring composition_text;
    std::wstring promoted_code;
    std::wstring promoted_word;
    std::vector<std::wstring> page_candidates;
    std::vector<std::wstring> all_display_candidates;
    size_t page_size = 5;
    size_t selected_index = 0; // Index within page_candidates
    size_t current_page = 0;
    size_t total_pages = 0;
    bool empty_4code_warning = false;
    bool dup_4code_warning = false;
    bool trigger_key_sound = false;
};

struct PrimaryDictLoadSummary {
    HRESULT hr = S_OK;
    size_t loaded_count = 0;
    bool used_fallback = false;
    bool retained_previous = false;
    std::shared_ptr<const WubiMappedDict> primary_dict;
    std::shared_ptr<const WubiMappedDict> extra_dict;
};

class WubiEngine {
public:
    static constexpr size_t kMaxCodeLength = 4;
    static constexpr size_t kDefaultPageSize = 5;

    WubiEngine() noexcept = default;

    // Built-in dictionary is pre-compiled into constexpr kSortedBuiltinWubi86Dict (.rdata); kept as no-op for API compatibility.
    void LoadBuiltinDictionary() noexcept {}

    // Load additional UTF-8 table file (format: code word1 word2 ...)
    bool LoadDictionaryFile(const std::filesystem::path& file_path);
    // Attach an already-validated memory-mapped dictionary instance.
    bool AttachMappedDictionary(std::shared_ptr<const WubiMappedDict> mapped);
    // Load configured primary (bundled or custom) and optional extra dictionaries, with safe fallback on custom load failure.
    PrimaryDictLoadSummary LoadConfiguredPrimaryDictionaries(
        const QiQiConfig& config,
        const std::filesystem::path& module_dir,
        bool retain_existing_on_custom_failure = false);
    // Load additional dictionary from an in-memory .qdict binary buffer.
    bool LoadDictionaryFromBinaryBuffer(std::vector<uint8_t> qdict_bytes);
    void ClearExternalDictionaries() noexcept {
        mapped_dicts_.clear();
        primary_mapped_dict_.reset();
        extra_mapped_dict_.reset();
    }

    // Add, remove, or modify an entry in the mutable user dictionary overlay.
    void AddEntry(std::wstring_view code, std::wstring_view word);
    bool RemoveEntry(std::wstring_view code, std::wstring_view word);
    bool ModifyEntry(std::wstring_view code, std::wstring_view old_word, std::wstring_view new_word);
    void PinEntry(std::wstring_view code, std::wstring_view word);
    bool UnpinEntry(std::wstring_view code, std::wstring_view word);
    void BlockEntry(std::wstring_view code, std::wstring_view word);
    bool UnblockEntry(std::wstring_view code, std::wstring_view word);

    // Find the longest Wubi 86 code registered for a single character/word across active dictionaries.
    std::wstring LookupCharacterCode(std::wstring_view ch) const;

    // Compute the 4-letter Wubi 86 phrase code (2+2 for 2 chars, 1+1+2 for 3 chars, 1+1+1+last for >=4 chars).
    std::wstring EncodePhraseWubi86(std::wstring_view phrase) const;

    // Configure or query the active user dictionary path used for cross-process synchronization.
    void SetUserDictPath(std::filesystem::path path) noexcept {
        if (user_dict_path_ != path) {
            user_dict_path_ = std::move(path);
            user_dict_sync_version_ = {};
            if (user_dict_sharing_) {
                ClearUserDictionaryEntries();
            }
        }
    }
    const std::filesystem::path& UserDictPath() const noexcept { return user_dict_path_; }
    HRESULT InitializeUserDictionarySharing();

    // Controlled user dictionary mutations (publish entry-level deltas and coordinate cross-process writes).
    bool AddUserPhrase(std::wstring_view phrase, const std::filesystem::path& user_dict_path = {});
    bool AddUserEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    HRESULT AddUserEntryResult(
        std::wstring_view code, std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool RemoveUserEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool ModifyUserEntry(
        std::wstring_view code,
        std::wstring_view old_word,
        std::wstring_view new_word,
        const std::filesystem::path& user_dict_path = {});
    bool ReplaceUserEntry(
        std::wstring_view old_code,
        std::wstring_view old_word,
        std::wstring_view new_code,
        std::wstring_view new_word,
        const std::filesystem::path& user_dict_path = {});
    HRESULT ReplaceUserEntryResult(
        std::wstring_view old_code, std::wstring_view old_word,
        std::wstring_view new_code, std::wstring_view new_word,
        const std::filesystem::path& user_dict_path = {});
    bool PinUserEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool UnpinUserEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool BlockSystemEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool UnblockSystemEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});
    bool DeleteOrBlockUserOrSystemEntry(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& user_dict_path = {});

    // Persist and restore dynamic phrase frequency promotions (binary QPF1 or legacy UTF-8 text).
    void SetPhraseFreqPath(std::filesystem::path path) noexcept {
        if (phrase_freq_path_ != path) {
            phrase_freq_path_ = std::move(path);
            phrase_freq_sync_version_ = {};
        }
    }
    const std::filesystem::path& PhraseFreqPath() const noexcept { return phrase_freq_path_; }
    bool LoadPhraseFrequencies(const std::filesystem::path& file_path);
    bool SavePhraseFrequencies(const std::filesystem::path& file_path) const;
    bool PromoteUserPhraseFrequency(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& file_path = {});
    bool RemovePhraseFrequency(
        std::wstring_view code,
        std::wstring_view word,
        const std::filesystem::path& file_path = {});
    bool ClearPhraseFrequencies(const std::filesystem::path& file_path = {});
    const std::map<uint32_t, std::vector<std::wstring>>& UserDictionaryEntries() const noexcept { return dict_; }
    const std::map<uint32_t, std::vector<std::wstring>>& PinnedEntries() const noexcept { return pinned_entries_; }
    const std::map<uint32_t, std::vector<std::wstring>>& BlockedEntries() const noexcept { return removed_entries_; }
    const std::map<uint32_t, std::vector<std::wstring>>& PromotedPhrases() const noexcept { return promoted_phrases_; }

    // Synchronize cross-process dictionary and phrase-frequency changes at a safe input boundary.
    HRESULT SynchronizeUserDictionary(sync::InputBoundary boundary);
    HRESULT SynchronizeUserDictionary() {
        return SynchronizeUserDictionary(IsComposing() ? sync::InputBoundary::Composing : sync::InputBoundary::NewInput);
    }
    HRESULT SynchronizePhraseFrequencies(sync::InputBoundary boundary);
    HRESULT SynchronizePhraseFrequencies() {
        return SynchronizePhraseFrequencies(
            IsComposing() ? sync::InputBoundary::Composing : sync::InputBoundary::NewInput);
    }
    bool SynchronizeCrossProcessChanges(sync::InputBoundary boundary);
    bool SynchronizeCrossProcessChanges() {
        return SynchronizeCrossProcessChanges(
            IsComposing() ? sync::InputBoundary::Composing : sync::InputBoundary::NewInput);
    }
    uint64_t UserDictIncrementalSyncCount() const noexcept { return user_dict_incremental_syncs_; }
    uint64_t UserDictSnapshotSyncCount() const noexcept { return user_dict_snapshot_syncs_; }
    uint64_t PhraseFreqIncrementalSyncCount() const noexcept { return phrase_freq_incremental_syncs_; }
    uint64_t PhraseFreqSnapshotSyncCount() const noexcept { return phrase_freq_snapshot_syncs_; }
    sync::Version UserDictSyncVersion() const noexcept { return user_dict_sync_version_; }
    sync::Version PhraseFreqSyncVersion() const noexcept { return phrase_freq_sync_version_; }

    // Mode & configuration management
    void ApplyConfig(const QiQiConfig& cfg) noexcept;
    bool IsChineseMode() const noexcept { return state_ != EngineState::English; }
    void SetChineseMode(bool enabled) noexcept;
    void ToggleChineseMode() noexcept;
    bool IsFullWidthMode() const noexcept { return full_width_mode_; }
    void SetFullWidthMode(bool enabled) noexcept { full_width_mode_ = enabled; }
    void ToggleFullWidthMode() noexcept { full_width_mode_ = !full_width_mode_; }
    bool IsChinesePunctuation() const noexcept { return chinese_punctuation_; }
    void SetChinesePunctuation(bool enabled) noexcept { chinese_punctuation_ = enabled; }
    void ToggleChinesePunctuation() noexcept { chinese_punctuation_ = !chinese_punctuation_; }
    void SetPageSize(size_t page_size) noexcept;
    bool AutoCommit4Code() const noexcept { return auto_commit_4code_; }
    void SetAutoCommit4Code(bool enabled) noexcept { auto_commit_4code_ = enabled; }
    bool PushCommit5th() const noexcept { return push_commit_5th_; }
    void SetPushCommit5th(bool enabled) noexcept { push_commit_5th_ = enabled; }
    bool SemicolonQuoteQuickSelect() const noexcept { return semicolon_quote_quick_select_; }
    void SetSemicolonQuoteQuickSelect(bool enabled) noexcept { semicolon_quote_quick_select_ = enabled; }
    bool ShowCodeHint() const noexcept { return show_code_hint_; }
    void SetShowCodeHint(bool enabled) noexcept { show_code_hint_ = enabled; }
    bool EnableZTempEnglish() const noexcept { return enable_z_temp_english_; }
    void SetEnableZTempEnglish(bool enabled) noexcept { enable_z_temp_english_ = enabled; }
    bool DynamicPhraseFreq() const noexcept { return dynamic_phrase_freq_; }
    void SetDynamicPhraseFreq(bool enabled) noexcept { dynamic_phrase_freq_ = enabled; }
    bool IsTempEnglishMode() const noexcept { return state_ == EngineState::TempEnglish; }
    bool BeepOnEmpty4Code() const noexcept { return beep_on_empty_4code_; }
    void SetBeepOnEmpty4Code(bool enabled) noexcept { beep_on_empty_4code_ = enabled; }
    bool BeepOnDup4Code() const noexcept { return beep_on_dup_4code_; }
    void SetBeepOnDup4Code(bool enabled) noexcept { beep_on_dup_4code_ = enabled; }
    bool ClearOnEmpty4Code() const noexcept { return clear_on_empty_4code_; }
    void SetClearOnEmpty4Code(bool enabled) noexcept { clear_on_empty_4code_ = enabled; }
    bool EnableKeySound() const noexcept { return enable_key_sound_; }
    void SetEnableKeySound(bool enabled) noexcept { enable_key_sound_ = enabled; }
    bool UpDownPageTurn() const noexcept { return up_down_page_turn_; }
    void SetUpDownPageTurn(bool enabled) noexcept { up_down_page_turn_ = enabled; }

    // State queries
    EngineState State() const noexcept { return state_; }
    bool IsComposing() const noexcept { return IsComposingState(state_); }
    const std::wstring& RawCode() const noexcept { return raw_code_; }
    const std::vector<std::wstring>& AllCandidates() const noexcept { return all_candidates_; }
    const std::vector<std::wstring>& AllDisplayCandidates() const noexcept { return all_display_candidates_; }
    std::vector<std::wstring> CurrentPageCandidates() const;
    size_t SelectedInPage() const noexcept { return selected_in_page_; }
    size_t CurrentPage() const noexcept { return current_page_; }
    size_t TotalPages() const noexcept;
    size_t PageSize() const noexcept { return page_size_; }

    // Reset composition state
    void Reset() noexcept;

    // Test whether a virtual key would be eaten by the engine in its current state
    bool ShouldEatKey(
        uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on = false) const noexcept;

    // Predict a target switch without mutating the current composition.
    bool ShouldEatKeyAfterReset(
        uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on = false) const noexcept;

    // Process a key press and return the state transition
    KeyProcessResult ProcessKey(
        uint32_t vk, bool shift_down = false, bool ctrl_down = false, bool alt_down = false,
        bool caps_lock_on = false, bool allow_learn = true, bool defer_phrase_promotion = false);

    // Commit a candidate on the current page without interpreting a keyboard event.
    KeyProcessResult CommitCandidate(
        size_t index_in_page, bool allow_learn = true, bool defer_phrase_promotion = false);

    // Apply a deferred phrase promotion after host edit session access is confirmed.
    void ApplyDeferredPhrasePromotion(const KeyProcessResult& result, bool allow_learn = true);

    // Exact + prefix lookup for a Wubi code (optionally populating progressive/wildcard display labels)
    std::vector<std::wstring> LookupCandidates(
        std::wstring_view code, std::vector<std::wstring>* out_display_labels = nullptr) const;
    std::vector<std::wstring> LookupExact(std::wstring_view code) const;
    const auto& MappedDictionaries() const noexcept { return mapped_dicts_; }
    size_t MappedDictionaryCount() const noexcept { return mapped_dicts_.size(); }
    bool HasModifiedMappedDictionary() const noexcept;

private:
    struct KeyInputContext {
        uint32_t vk = 0;
        bool shift_down = false;
        bool caps_lock_on = false;
        bool allow_learn = true;
        bool defer_phrase_promotion = false;
    };
    struct TransitionOutcome {
        EngineState next_state;
        KeyProcessResult result;
    };
    using ActionHandler = TransitionOutcome (WubiEngine::*)(const KeyInputContext&);

    template <typename Operation>
    KeyProcessResult ExecuteTransition(Operation operation);
    TransitionRule EvaluateKeyTransition(
        EngineState state, uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on) const noexcept;
    void PromotePhraseForCode(std::wstring_view code, std::wstring_view word);
    std::wstring CurrentSelectedCandidateOrRaw() const;
    std::wstring_view ResolveChinesePunctuation(uint32_t vk, bool shift_down) noexcept;
    void ClearCompositionData() noexcept;
    void AppendCodeAndRefresh(uint32_t vk);
    void UpdateTempEnglishCandidates();
    TransitionOutcome CommitAndClear(
        std::wstring committed_text, std::wstring promoted_code = {}, std::wstring promoted_word = {});
    TransitionOutcome ResolveCandidateCommit(
        size_t index_in_page, bool allow_learn = true, bool defer_phrase_promotion = false);
    void RefreshCandidates();
    KeyProcessResult MakeCurrentStateResult(KeyActionType action, std::wstring committed = L"") const;

    // Table-driven Command handlers corresponding 1:1 to TransitionOp
    TransitionOutcome HandlePassThrough(const KeyInputContext& ctx) noexcept;
    TransitionOutcome HandleAppendCode(const KeyInputContext& ctx);
    TransitionOutcome HandleAppend4thCode(const KeyInputContext& ctx);
    TransitionOutcome HandlePushCommitAndRestart(const KeyInputContext& ctx);
    TransitionOutcome HandlePopCode(const KeyInputContext& ctx);
    TransitionOutcome HandleCancelComposition(const KeyInputContext& ctx) noexcept;
    TransitionOutcome HandleCommitSelected(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitRawCode(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitDigitIndex(const KeyInputContext& ctx);
    TransitionOutcome HandleSelectPrevCandidate(const KeyInputContext& ctx);
    TransitionOutcome HandleSelectNextCandidate(const KeyInputContext& ctx);
    TransitionOutcome HandleTurnPagePrev(const KeyInputContext& ctx);
    TransitionOutcome HandleTurnPageNext(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitPunctOnly(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitSelectedAndPunct(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitSelectedAndUpper(const KeyInputContext& ctx);
    TransitionOutcome HandleStartTempEnglish(const KeyInputContext& ctx);
    TransitionOutcome HandleAppendTempEnglish(const KeyInputContext& ctx);
    TransitionOutcome HandlePopTempEnglish(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitTempEnglish(const KeyInputContext& ctx);
    TransitionOutcome HandleCommitFullWidth(const KeyInputContext& ctx);
    TransitionOutcome HandlePinDigitCandidate(const KeyInputContext& ctx);
    TransitionOutcome HandleDeleteOrBlockCandidate(const KeyInputContext& ctx);

    bool IsEntryRemoved(uint32_t packed_code, std::wstring_view word) const;
    void ClearUserDictionaryEntries() noexcept;
    void SwapDictionaryState(WubiEngine& other) noexcept;
    bool CheckAndPublishExternalUserDictChanges(const std::filesystem::path& dict_path);
    template <typename FileMutator, typename MemoryMutator>
    HRESULT MutateUserDictionaryEntry(
        std::wstring_view code,
        std::wstring_view word,
        std::wstring_view new_word,
        sync::Operation op,
        const std::filesystem::path& user_dict_path,
        FileMutator mutate_file,
        MemoryMutator mutate_memory,
        bool reset_baseline = false,
        bool require_original = false);

    std::map<uint32_t, std::vector<std::wstring>> dict_;
    std::map<uint32_t, std::vector<std::wstring>> pinned_entries_;
    std::map<uint32_t, std::vector<std::wstring>> removed_entries_;
    std::map<uint32_t, std::vector<std::wstring>> promoted_phrases_;
    std::filesystem::path user_dict_path_;
    std::filesystem::path shared_user_dict_directory_;
    bool user_dict_sharing_ = false;
    HRESULT shared_user_dict_status_ = S_OK;
    std::filesystem::path phrase_freq_path_;
    sync::Version user_dict_sync_version_{};
    sync::Version phrase_freq_sync_version_{};
    uint64_t user_dict_incremental_syncs_ = 0;
    uint64_t user_dict_snapshot_syncs_ = 0;
    uint64_t phrase_freq_incremental_syncs_ = 0;
    uint64_t phrase_freq_snapshot_syncs_ = 0;
    std::vector<std::shared_ptr<const WubiMappedDict>> mapped_dicts_;
    std::shared_ptr<const WubiMappedDict> primary_mapped_dict_;
    std::shared_ptr<const WubiMappedDict> extra_mapped_dict_;
    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
    std::vector<std::wstring> all_display_candidates_;
    size_t page_size_ = kDefaultPageSize;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    bool auto_commit_4code_ = true;
    bool push_commit_5th_ = true;
    bool semicolon_quote_quick_select_ = true;
    bool show_code_hint_ = true;
    bool enable_z_temp_english_ = true;
    bool dynamic_phrase_freq_ = false;
    bool beep_on_empty_4code_ = true;
    bool beep_on_dup_4code_ = false;
    bool clear_on_empty_4code_ = false;
    bool enable_key_sound_ = false;
    bool up_down_page_turn_ = false;
    bool full_width_mode_ = false;
    bool chinese_punctuation_ = true;
    bool double_quote_open_ = false;
    bool single_quote_open_ = false;
    EngineState state_ = EngineState::Idle;
};

} // namespace qiqi
