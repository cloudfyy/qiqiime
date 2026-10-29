#include "engine/wubi_engine.h"
#include "engine/wubi_default_dict.h"
#include "common/logger.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace qiqi {

namespace {

constexpr bool IsWubiCodeChar(wchar_t ch) noexcept {
    // Standard 86 Wubi uses 'a'-'y' for radicals and 'z' as wildcard/query key
    return ch >= L'a' && ch <= L'z';
}

} // namespace

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
    if (code.empty() || word.empty() || code.size() > kMaxCodeLength) {
        return;
    }
    std::wstring norm_code(code);
    for (auto& ch : norm_code) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
        if (!IsWubiCodeChar(ch)) {
            return;
        }
    }

    auto& list = dict_[norm_code];
    if (std::find(list.begin(), list.end(), word) == list.end()) {
        list.emplace_back(word);
    }
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        QIQI_LOG_WARN("Failed to open external dictionary file: {}",
                      log::WideToUtf8(file_path.wstring()));
        return false;
    }

    size_t loaded_words = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Strip UTF-8 BOM on first line if present
        if (line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }

        std::wstring wline = log::Utf8ToWide(line);
        if (wline.empty()) {
            continue;
        }
        std::wistringstream iss(wline);
        std::wstring code;
        if (!(iss >> code) || code.empty() || code.starts_with(L'#')) {
            continue;
        }
        std::wstring word;
        while (iss >> word) {
            AddEntry(code, word);
            ++loaded_words;
        }
    }
    QIQI_LOG_INFO("Loaded external dictionary file '{}' ({} words, total {} code keys)",
                  log::WideToUtf8(file_path.wstring()), loaded_words, dict_.size());
    return true;
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
    return std::vector<std::wstring>(all_candidates_.begin() + static_cast<ptrdiff_t>(start),
                                     all_candidates_.begin() + static_cast<ptrdiff_t>(end));
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    const auto it = dict_.find(std::wstring(code));
    if (it == dict_.end()) {
        return {};
    }
    return it->second;
}

std::vector<std::wstring> WubiEngine::LookupCandidates(std::wstring_view code) const {
    if (code.empty()) {
        return {};
    }

    std::vector<std::wstring> results;
    auto append_unique = [&results](const std::wstring& w) {
        if (std::find(results.begin(), results.end(), w) == results.end()) {
            results.push_back(w);
        }
    };

    // Wildcard query if 'z' is present
    const bool has_wildcard = code.find(L'z') != std::wstring_view::npos;
    if (has_wildcard) {
        for (const auto& [k, words] : dict_) {
            if (k.size() < code.size()) {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < code.size(); ++i) {
                if (code[i] != L'z' && code[i] != k[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                for (const auto& w : words) {
                    append_unique(w);
                    if (results.size() >= 50) {
                        return results;
                    }
                }
            }
        }
        return results;
    }

    // 1. Exact matches first
    if (const auto exact = dict_.find(std::wstring(code)); exact != dict_.end()) {
        for (const auto& w : exact->second) {
            append_unique(w);
        }
    }

    // 2. Prefix matches (sorted by code length then lexicographically)
    std::wstring prefix(code);
    for (auto it = dict_.lower_bound(prefix); it != dict_.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        if (it->first == prefix) {
            continue;
        }
        for (const auto& w : it->second) {
            append_unique(w);
            if (results.size() >= 50) {
                return results;
            }
        }
    }

    return results;
}

std::wstring WubiEngine::CurrentSelectedCandidateOrRaw() const {
    const auto page = CurrentPageCandidates();
    if (!page.empty()) {
        return page[std::min(selected_in_page_, page.size() - 1)];
    }
    return raw_code_;
}

void WubiEngine::AppendCodeAndRefresh(uint32_t vk, EngineState next_state) {
    const wchar_t ch = static_cast<wchar_t>(vk - 'A' + L'a');
    raw_code_.push_back(ch);
    state_ = next_state;
    RefreshCandidates();
}

KeyProcessResult WubiEngine::CommitAndReset(std::wstring committed_text) {
    Reset();
    return MakeCurrentStateResult(KeyActionType::Commit, std::move(committed_text));
}

void WubiEngine::RefreshCandidates() {
    all_candidates_ = LookupCandidates(raw_code_);
    current_page_ = 0;
    selected_in_page_ = 0;
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
    return res;
}

bool WubiEngine::ShouldEatKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) const noexcept {
    const KeyEventKind event = ClassifyKeyEvent(vk, shift_down, ctrl_down, alt_down);
    return LookupTransition(state_, event).eat_key;
}

KeyProcessResult WubiEngine::ProcessKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) {
    const KeyEventKind event = ClassifyKeyEvent(vk, shift_down, ctrl_down, alt_down);
    const TransitionRule rule = LookupTransition(state_, event);

    switch (rule.op) {
        case TransitionOp::PassThrough:
            return {};

        case TransitionOp::AppendCode:
            AppendCodeAndRefresh(vk, rule.next_state);
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::Append4thCode:
            AppendCodeAndRefresh(vk, rule.next_state);
            // Standard Wubi 86 4-code auto-commit:
            // When reaching 4 codes without wildcard 'z' and there is a unique exact match, commit immediately!
            if (raw_code_.find(L'z') == std::wstring::npos) {
                const auto exact = LookupExact(raw_code_);
                if (exact.size() == 1) {
                    return CommitAndReset(exact.front());
                }
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);

        case TransitionOp::PushCommitAndRestart: {
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
            return CommitAndReset(CurrentSelectedCandidateOrRaw());

        case TransitionOp::CommitRawCode:
            return CommitAndReset(raw_code_);

        case TransitionOp::CommitDigitIndex: {
            const size_t idx = static_cast<size_t>(vk - '1');
            const auto page = CurrentPageCandidates();
            if (idx < page.size()) {
                return CommitAndReset(page[idx]);
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
                std::wstring(MapChinesePunctuation(vk, shift_down).value_or(L"")));

        case TransitionOp::CommitSelectedAndPunct: {
            std::wstring committed = CurrentSelectedCandidateOrRaw();
            committed += MapChinesePunctuation(vk, shift_down).value_or(L"");
            return CommitAndReset(std::move(committed));
        }
    }

    return {};
}

} // namespace qiqi
