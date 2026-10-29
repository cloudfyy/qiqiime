#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <filesystem>
#include <optional>
#include <cstdint>

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
};

class WubiEngine {
public:
    static constexpr size_t kMaxCodeLength = 4;
    static constexpr size_t kDefaultPageSize = 5;

    WubiEngine();

    // Load内置码表 (called automatically by constructor)
    void LoadBuiltinDictionary();

    // Load additional UTF-8 table file (format: code word1 word2 ...)
    bool LoadDictionaryFile(const std::filesystem::path& file_path);

    // Add a single entry
    void AddEntry(std::wstring_view code, std::wstring_view word);

    // Mode management
    bool IsChineseMode() const noexcept { return state_ != EngineState::English; }
    void SetChineseMode(bool enabled) noexcept;
    void ToggleChineseMode() noexcept;

    // State queries
    EngineState State() const noexcept { return state_; }
    bool IsComposing() const noexcept { return IsComposingState(state_); }
    const std::wstring& RawCode() const noexcept { return raw_code_; }
    const std::vector<std::wstring>& AllCandidates() const noexcept { return all_candidates_; }
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

private:
    std::wstring CurrentSelectedCandidateOrRaw() const;
    void RefreshCandidates();
    KeyProcessResult MakeCurrentStateResult(KeyActionType action, std::wstring committed = L"") const;

    std::map<std::wstring, std::vector<std::wstring>> dict_;
    std::wstring raw_code_;
    std::vector<std::wstring> all_candidates_;
    size_t page_size_ = kDefaultPageSize;
    size_t current_page_ = 0;
    size_t selected_in_page_ = 0;
    EngineState state_ = EngineState::Idle;
};

} // namespace qiqi
