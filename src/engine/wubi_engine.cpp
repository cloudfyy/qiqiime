#include "engine/wubi_engine.h"
#include "engine/wubi_default_dict.h"
#include "common/logger.h"

#include <windows.h>
#include <algorithm>

namespace qiqi {

WubiEngine::WubiEngine() {
    LoadBuiltinDictionary();
}

void WubiEngine::LoadBuiltinDictionary() {
    for (const auto& entry : kBuiltinWubi86Dict) {
        AddEntry(entry.code, entry.word);
    }
    QIQI_LOG_DEBUG("Loaded builtin Wubi86 dictionary: {} code keys", dict_.size());
}

void WubiEngine::AddEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }

    AppendUniqueWord(dict_[packed], word);
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    auto mapped = WubiMappedDict::LoadFromTextOrCache(file_path);
    if (!mapped) {
        QIQI_LOG_WARN("Failed to load or map external dictionary file: {}",
                      log::WideToUtf8(file_path.wstring()));
        return false;
    }
    if (std::find(mapped_dicts_.begin(), mapped_dicts_.end(), mapped) == mapped_dicts_.end()) {
        QIQI_LOG_INFO("Attached external dictionary '{}' ({} codes, {} words, mapped={})",
                      log::WideToUtf8(file_path.wstring()),
                      mapped->CodeCount(),
                      mapped->WordCount(),
                      mapped->IsMemoryMapped());
        mapped_dicts_.push_back(std::move(mapped));
    }
    return true;
}

void WubiEngine::ApplyConfig(const QiQiConfig& cfg) noexcept {
    SetPageSize(cfg.page_size);
    auto_commit_4code_ = cfg.auto_commit_4code;
    push_commit_5th_ = cfg.push_commit_5th;
    beep_on_empty_4code_ = cfg.beep_on_empty_4code;
    clear_on_empty_4code_ = cfg.clear_on_empty_4code;
    enable_key_sound_ = cfg.enable_key_sound;
    full_width_mode_ = cfg.default_full_width;
    chinese_punctuation_ = cfg.default_chinese_punct;
    SetChineseMode(cfg.default_chinese_mode);
    QIQI_LOG_DEBUG(
        "WubiEngine::ApplyConfig: page_size={}, auto4={}, push5={}, beep_empty4={}, "
        "clear_empty4={}, key_sound={}, full_width={}, cn_punct={}, cn_mode={}",
        page_size_,
        auto_commit_4code_,
        push_commit_5th_,
        beep_on_empty_4code_,
        clear_on_empty_4code_,
        enable_key_sound_,
        full_width_mode_,
        chinese_punctuation_,
        IsChineseMode());
}

void WubiEngine::SetPageSize(size_t page_size) noexcept {
    page_size_ = std::clamp<size_t>(page_size, 1, 9);
    current_page_ = 0;
    selected_in_page_ = 0;
}

void WubiEngine::SetChineseMode(bool enabled) noexcept {
    if (enabled) {
        if (state_ == EngineState::English) {
            state_ = EngineState::Idle;
        }
    } else {
        Reset();
        state_ = EngineState::English;
    }
    try {
        QIQI_LOG_DEBUG("WubiEngine mode switched to {}", enabled ? "Chinese" : "English");
    } catch (...) {
    }
}

void WubiEngine::ToggleChineseMode() noexcept {
    SetChineseMode(!IsChineseMode());
}

void WubiEngine::Reset() noexcept {
    raw_code_.clear();
    all_candidates_.clear();
    all_display_candidates_.clear();
    current_page_ = 0;
    selected_in_page_ = 0;
    if (state_ != EngineState::English) {
        state_ = EngineState::Idle;
    }
}

size_t WubiEngine::TotalPages() const noexcept {
    return ComputeTotalPages(all_candidates_.size(), page_size_);
}

std::vector<std::wstring> WubiEngine::CurrentPageCandidates() const {
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    if (start >= end) {
        return {};
    }
    const std::span<const std::wstring> page_span(all_candidates_);
    const auto sub = page_span.subspan(start, end - start);
    return std::vector<std::wstring>(sub.begin(), sub.end());
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return {};
    }

    std::vector<std::wstring> results;
    if (const auto it = dict_.find(packed); it != dict_.end()) {
        for (const auto& w : it->second) {
            AppendUniqueWord(results, w);
        }
    }
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupExact(packed, results);
    }
    return results;
}

std::vector<std::wstring> WubiEngine::LookupCandidates(std::wstring_view code) const {
    const uint32_t exact_code = PackWubiCode(code);
    if (exact_code == 0) {
        return {};
    }

    constexpr size_t kMaxCandidateResults = 50;
    std::vector<std::wstring> results;

    // Wildcard query if 'z' is present
    const bool has_wildcard = (code.find(L'z') != std::wstring_view::npos) ||
                              (code.find(L'Z') != std::wstring_view::npos);
    if (has_wildcard) {
        for (const auto& [k, words] : dict_) {
            if (MatchesWildcardPattern(k, code)) {
                for (const auto& w : words) {
                    AppendUniqueWord(results, w);
                    if (results.size() >= kMaxCandidateResults) {
                        return results;
                    }
                }
            }
        }
        for (const auto& mapped : mapped_dicts_) {
            mapped->LookupWildcard(code, results, kMaxCandidateResults);
            if (results.size() >= kMaxCandidateResults) {
                return results;
            }
        }
        return results;
    }

    // 1. Exact matches first
    results = LookupExact(code);
    if (results.size() >= kMaxCandidateResults) {
        results.resize(kMaxCandidateResults);
        return results;
    }

    // 2. Prefix matches in (exact_code, prefix_hi]
    const uint32_t prefix_hi = PackWubiPrefixUpperBound(code);
    for (auto it = dict_.upper_bound(exact_code); it != dict_.end() && it->first <= prefix_hi; ++it) {
        for (const auto& w : it->second) {
            AppendUniqueWord(results, w);
            if (results.size() >= kMaxCandidateResults) {
                return results;
            }
        }
    }
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupPrefix(exact_code, exact_code, prefix_hi, results, kMaxCandidateResults);
        if (results.size() >= kMaxCandidateResults) {
            return results;
        }
    }

    return results;
}

std::wstring WubiEngine::CurrentSelectedCandidateOrRaw() const {
    const auto page = CurrentPageCandidates();
    if (!page.empty()) {
        return gsl::at(page, std::min(selected_in_page_, page.size() - 1));
    }
    return raw_code_;
}

std::wstring_view WubiEngine::ResolveChinesePunctuation(uint32_t vk, bool shift_down) noexcept {
    if (vk == VK_OEM_7) {
        if (shift_down) {
            const std::wstring_view quote = double_quote_open_ ? L"”" : L"“";
            double_quote_open_ = !double_quote_open_;
            return quote;
        }
        const std::wstring_view quote = single_quote_open_ ? L"’" : L"‘";
        single_quote_open_ = !single_quote_open_;
        return quote;
    }
    return MapChinesePunctuation(vk, shift_down).value_or(L"");
}

void WubiEngine::AppendCodeAndRefresh(uint32_t vk, EngineState next_state) {
    const wchar_t ch = gsl::narrow_cast<wchar_t>(vk - 'A' + L'a');
    raw_code_.push_back(ch);
    state_ = next_state;
    RefreshCandidates();
}

KeyProcessResult WubiEngine::CommitAndReset(std::wstring committed_text) {
    Reset();
    return MakeCurrentStateResult(KeyActionType::Commit, std::move(committed_text));
}

void WubiEngine::RefreshCandidates() {
    all_candidates_.clear();
    all_display_candidates_.clear();
    current_page_ = 0;
    selected_in_page_ = 0;

    const bool has_wildcard = (raw_code_.find(L'z') != std::wstring::npos) ||
                              (raw_code_.find(L'Z') != std::wstring::npos);
    if (!has_wildcard) {
        all_candidates_ = LookupCandidates(raw_code_);
        all_display_candidates_ = all_candidates_;
        return;
    }

    constexpr size_t kMaxCandidateResults = 50;
    for (const auto& [k, words] : dict_) {
        if (MatchesWildcardPattern(k, raw_code_)) {
            const std::wstring code_str = UnpackWubiCode(k);
            for (const auto& w : words) {
                const size_t prev_sz = all_candidates_.size();
                AppendUniqueWord(all_candidates_, w);
                if (all_candidates_.size() > prev_sz) {
                    all_display_candidates_.push_back(w + L"(" + code_str + L")");
                }
                if (all_candidates_.size() >= kMaxCandidateResults) {
                    return;
                }
            }
        }
    }
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupWildcard(
            raw_code_, all_candidates_, kMaxCandidateResults, &all_display_candidates_);
        if (all_candidates_.size() >= kMaxCandidateResults) {
            return;
        }
    }
}

KeyProcessResult WubiEngine::MakeCurrentStateResult(KeyActionType action, std::wstring committed) const {
    KeyProcessResult res;
    res.action = action;
    res.committed_text = std::move(committed);
    res.composition_text = raw_code_;
    res.page_candidates = CurrentPageCandidates();
    res.selected_index = selected_in_page_;
    res.current_page = current_page_;
    res.total_pages = TotalPages();
    res.trigger_key_sound = enable_key_sound_ && (action != KeyActionType::PassThrough);
    return res;
}

bool WubiEngine::ShouldEatKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) const noexcept {
    const KeyEventKind event = ClassifyKeyEvent(vk, shift_down, ctrl_down, alt_down, chinese_punctuation_);
    if (LookupTransition(state_, event).eat_key) {
        return true;
    }
    if (full_width_mode_ && !ctrl_down && !alt_down) {
        return MapFullWidthChar(vk, shift_down).has_value();
    }
    return false;
}

KeyProcessResult WubiEngine::ProcessKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) {
    const KeyEventKind event = ClassifyKeyEvent(vk, shift_down, ctrl_down, alt_down, chinese_punctuation_);
    const TransitionRule rule = LookupTransition(state_, event);

    switch (rule.op) {
        case TransitionOp::PassThrough:
            if (full_width_mode_ && !ctrl_down && !alt_down) {
                if (const auto fw = MapFullWidthChar(vk, shift_down); fw.has_value()) {
                    std::wstring committed;
                    if (IsComposing()) {
                        committed = CurrentSelectedCandidateOrRaw();
                    }
                    committed.push_back(*fw);
                    return CommitAndReset(std::move(committed));
                }
            }
            return {};

        case TransitionOp::AppendCode:
            AppendCodeAndRefresh(vk, rule.next_state);
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::Append4thCode: {
            AppendCodeAndRefresh(vk, rule.next_state);
            // Standard Wubi 86 4-code auto-commit:
            // When reaching 4 codes without wildcard 'z' and there is a unique exact match, commit immediately!
            if (auto_commit_4code_ && raw_code_.find(L'z') == std::wstring::npos) {
                const auto exact = LookupExact(raw_code_);
                if (exact.size() == 1) {
                    return CommitAndReset(exact.front());
                }
            }
            const bool is_empty_4code = all_candidates_.empty();
            if (is_empty_4code && clear_on_empty_4code_) {
                QIQI_LOG_DEBUG("4-code lookup empty: auto-clearing composition (beep={})", beep_on_empty_4code_);
                Reset();
                KeyProcessResult res = MakeCurrentStateResult(KeyActionType::Cancel);
                if (beep_on_empty_4code_) {
                    res.empty_4code_warning = true;
                }
                return res;
            }
            KeyProcessResult res = MakeCurrentStateResult(KeyActionType::UpdateState);
            if (beep_on_empty_4code_ && is_empty_4code) {
                QIQI_LOG_DEBUG("4-code lookup empty: triggering empty-code warning beep");
                res.empty_4code_warning = true;
            }
            return res;
        }

        case TransitionOp::PushCommitAndRestart: {
            if (!push_commit_5th_) {
                return MakeCurrentStateResult(KeyActionType::UpdateState);
            }
            std::wstring auto_committed = CurrentSelectedCandidateOrRaw();
            Reset();
            AppendCodeAndRefresh(vk, rule.next_state);
            return MakeCurrentStateResult(KeyActionType::Commit, std::move(auto_committed));
        }

        case TransitionOp::CancelComposition:
            Reset();
            return MakeCurrentStateResult(KeyActionType::Cancel);

        case TransitionOp::PopCode:
            raw_code_.pop_back();
            state_ = rule.next_state;
            RefreshCandidates();
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::CommitSelected:
            if (clear_on_empty_4code_ && all_candidates_.empty()) {
                Reset();
                return MakeCurrentStateResult(KeyActionType::Cancel);
            }
            return CommitAndReset(CurrentSelectedCandidateOrRaw());

        case TransitionOp::CommitRawCode:
            return CommitAndReset(raw_code_);

        case TransitionOp::CommitDigitIndex: {
            const size_t idx = static_cast<size_t>(vk - '1');
            const auto page = CurrentPageCandidates();
            if (idx < page.size()) {
                return CommitAndReset(gsl::at(page, idx));
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }

        case TransitionOp::SelectPrevCandidate:
            if (selected_in_page_ > 0) {
                --selected_in_page_;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::SelectNextCandidate: {
            const auto page = CurrentPageCandidates();
            if (!page.empty() && selected_in_page_ + 1 < page.size()) {
                ++selected_in_page_;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }

        case TransitionOp::TurnPagePrev:
            if (current_page_ > 0) {
                --current_page_;
                selected_in_page_ = 0;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::TurnPageNext:
            if (current_page_ + 1 < TotalPages()) {
                ++current_page_;
                selected_in_page_ = 0;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::CommitPunctOnly:
            return MakeCurrentStateResult(
                KeyActionType::Commit,
                std::wstring(ResolveChinesePunctuation(vk, shift_down)));

        case TransitionOp::CommitSelectedAndPunct: {
            std::wstring committed = CurrentSelectedCandidateOrRaw();
            committed += ResolveChinesePunctuation(vk, shift_down);
            return CommitAndReset(std::move(committed));
        }

        case TransitionOp::CommitSelectedAndUpper: {
            std::wstring committed = CurrentSelectedCandidateOrRaw();
            const wchar_t upper_ch = full_width_mode_
                ? MapFullWidthChar(vk, true).value_or(gsl::narrow_cast<wchar_t>(vk))
                : gsl::narrow_cast<wchar_t>(vk);
            committed.push_back(upper_ch);
            return CommitAndReset(std::move(committed));
        }
    }

    return {};
}

} // namespace qiqi
