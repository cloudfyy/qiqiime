#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <optional>
#include <cstdint>

#include "common/config.h"
#include "engine/wubi_binary_dict.h"
#include "engine/wubi_fsm.h"

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
    std::vector<std::wstring> page_candidates;
    size_t selected_index = 0; // Index within page_candidates
    size_t current_page = 0;
    size_t total_pages = 0;
    bool empty_4code_warning = false;
    bool dup_4code_warning = false;
    bool trigger_key_sound = false;
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
    void ClearExternalDictionaries() noexcept { mapped_dicts_.clear(); }

    // Add a single entry
    void AddEntry(std::wstring_view code, std::wstring_view word);

    // Find the longest Wubi 86 code registered for a single character/word across active dictionaries.
    std::wstring LookupCharacterCode(std::wstring_view ch) const;

    // Compute the 4-letter Wubi 86 phrase code (2+2 for 2 chars, 1+1+2 for 3 chars, 1+1+1+last for >=4 chars).
    std::wstring EncodePhraseWubi86(std::wstring_view phrase) const;

    // Auto-encode a multi-character phrase, add it to the active in-memory dictionary, and optionally append to user_dict_path.
    bool AddUserPhrase(std::wstring_view phrase, const std::filesystem::path& user_dict_path = {});

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
    bool ShouldEatKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) const noexcept;

    // Process a key press and return the state transition
    KeyProcessResult ProcessKey(uint32_t vk, bool shift_down = false, bool ctrl_down = false, bool alt_down = false);

    // Exact + prefix lookup for a Wubi code
    std::vector<std::wstring> LookupCandidates(std::wstring_view code) const;
    std::vector<std::wstring> LookupExact(std::wstring_view code) const;
    size_t MappedDictionaryCount() const noexcept { return mapped_dicts_.size(); }

private:
    TransitionRule EvaluateKeyTransition(
        uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) const noexcept;
    void PromotePhraseForCode(std::wstring_view code, std::wstring_view word);
    std::wstring CurrentSelectedCandidateOrRaw() const;
    std::wstring_view ResolveChinesePunctuation(uint32_t vk, bool shift_down) noexcept;
    void AppendCodeAndRefresh(uint32_t vk, EngineState next_state);
    KeyProcessResult CommitAndReset(std::wstring committed_text);
    void RefreshCandidates();
    KeyProcessResult MakeCurrentStateResult(KeyActionType action, std::wstring committed = L"") const;

    std::map<uint32_t, std::vector<std::wstring>> dict_;
    std::map<uint32_t, std::vector<std::wstring>> promoted_phrases_;
    std::vector<std::shared_ptr<const WubiMappedDict>> mapped_dicts_;
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
    bool full_width_mode_ = false;
    bool chinese_punctuation_ = true;
    bool double_quote_open_ = false;
    bool single_quote_open_ = false;
    EngineState state_ = EngineState::Idle;
};

} // namespace qiqi
