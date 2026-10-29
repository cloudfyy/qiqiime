#include "engine/wubi_engine.h"
#include "engine/wubi_default_dict.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <algorithm>

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
    if (word.empty()) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }

    AppendUniqueWord(dict_[packed], word);
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
    AddEntry(code, phrase);
    QIQI_LOG_INFO("Added user phrase (code='{}', length={})",
                  log::WideToUtf8(code), SplitUtf16Codepoints(phrase).size());

    if (user_dict_path.empty()) {
        return true;
    }

    try {
        std::error_code ec;
        if (user_dict_path.has_parent_path()) {
            std::filesystem::create_directories(user_dict_path.parent_path(), ec);
        }
        const ScopedFileHandle file(::CreateFileW(
            user_dict_path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
            nullptr));
        if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
            QIQI_LOG_WARN("AddUserPhrase: failed to open user dict '{}' for append (error={})",
                          log::WideToUtf8(user_dict_path.wstring()), ::GetLastError());
            return false;
        }
        const std::string line = log::WideToUtf8(code) + " " + log::WideToUtf8(phrase) + "\n";
        DWORD written = 0;
        const DWORD to_write = gsl::narrow_cast<DWORD>(line.size());
        return (::WriteFile(file.Get(), line.data(), to_write, &written, nullptr) != FALSE) &&
               (written == to_write);
    } catch (...) {
        return false;
    }
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
    semicolon_quote_quick_select_ = cfg.semicolon_quote_quick_select;
    show_code_hint_ = cfg.show_code_hint;
    enable_z_temp_english_ = cfg.enable_z_temp_english;
    beep_on_empty_4code_ = cfg.beep_on_empty_4code;
    beep_on_dup_4code_ = cfg.beep_on_dup_4code;
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
    temp_english_text_.clear();
    temp_english_mode_ = false;
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

    constexpr size_t kMaxCandidateResults = 50;
    const bool has_wildcard = (raw_code_.find(L'z') != std::wstring::npos) ||
                              (raw_code_.find(L'Z') != std::wstring::npos);
    if (!has_wildcard) {
        if (!show_code_hint_ || raw_code_.size() >= kMaxCodeLength) {
            all_candidates_ = LookupCandidates(raw_code_);
            all_display_candidates_ = all_candidates_;
            return;
        }

        const uint32_t exact_code = PackWubiCode(raw_code_);
        if (exact_code == 0) {
            return;
        }
        all_candidates_ = LookupExact(raw_code_);
        if (all_candidates_.size() > kMaxCandidateResults) {
            all_candidates_.resize(kMaxCandidateResults);
        }
        all_display_candidates_ = all_candidates_;
        if (all_candidates_.size() >= kMaxCandidateResults) {
            return;
        }

        const uint32_t prefix_hi = PackWubiPrefixUpperBound(raw_code_);
        for (auto it = dict_.upper_bound(exact_code); it != dict_.end() && it->first <= prefix_hi; ++it) {
            const std::wstring code_str = UnpackWubiCode(it->first);
            const std::wstring_view suffix = (code_str.size() > raw_code_.size())
                ? std::wstring_view(code_str).substr(raw_code_.size())
                : std::wstring_view(code_str);
            for (const auto& w : it->second) {
                const size_t prev_sz = all_candidates_.size();
                AppendUniqueWord(all_candidates_, w);
                if (all_candidates_.size() > prev_sz) {
                    all_display_candidates_.push_back(w + L"(~" + std::wstring(suffix) + L")");
                }
                if (all_candidates_.size() >= kMaxCandidateResults) {
                    return;
                }
            }
        }
        for (const auto& mapped : mapped_dicts_) {
            mapped->LookupPrefix(
                exact_code,
                exact_code,
                prefix_hi,
                all_candidates_,
                kMaxCandidateResults,
                raw_code_,
                &all_display_candidates_);
            if (all_candidates_.size() >= kMaxCandidateResults) {
                return;
            }
        }
        return;
    }

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
    if (temp_english_mode_) {
        if (ctrl_down || alt_down) {
            return false;
        }
        if (vk == VK_BACK || vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE) {
            return true;
        }
        return MapPrintableAsciiChar(vk, shift_down).has_value();
    }
    const bool quick_sel = semicolon_quote_quick_select_ && IsComposing();
    const KeyEventKind event = ClassifyKeyEvent(
        vk, shift_down, ctrl_down, alt_down, chinese_punctuation_, quick_sel);
    if (LookupTransition(state_, event).eat_key) {
        return true;
    }
    if (full_width_mode_ && !ctrl_down && !alt_down) {
        return MapFullWidthChar(vk, shift_down).has_value();
    }
    return false;
}

KeyProcessResult WubiEngine::ProcessKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) {
    if (temp_english_mode_) {
        if (ctrl_down || alt_down) {
            return {};
        }
        if (vk == VK_ESCAPE) {
            Reset();
            return MakeCurrentStateResult(KeyActionType::Cancel);
        }
        if (vk == VK_BACK) {
            if (temp_english_text_.empty()) {
                Reset();
                return MakeCurrentStateResult(KeyActionType::Cancel);
            }
            temp_english_text_.pop_back();
            raw_code_ = L"z" + temp_english_text_;
            all_candidates_ = {temp_english_text_.empty() ? L"z" : temp_english_text_};
            all_display_candidates_ = {temp_english_text_.empty() ? L"(英文模式)" : temp_english_text_};
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }
        if (vk == VK_SPACE || vk == VK_RETURN) {
            std::wstring committed = temp_english_text_.empty() ? L"z" : temp_english_text_;
            return CommitAndReset(std::move(committed));
        }
        if (const auto ascii = MapPrintableAsciiChar(vk, shift_down); ascii.has_value()) {
            temp_english_text_.push_back(gsl::narrow_cast<wchar_t>(*ascii));
            raw_code_ = L"z" + temp_english_text_;
            all_candidates_ = {temp_english_text_};
            all_display_candidates_ = {temp_english_text_};
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }
        return {};
    }

    if (state_ == EngineState::Idle && enable_z_temp_english_ &&
        vk == 'Z' && !shift_down && !ctrl_down && !alt_down) {
        temp_english_mode_ = true;
        temp_english_text_.clear();
        raw_code_ = L"z";
        state_ = EngineState::Composing1;
        all_candidates_ = {L"z"};
        all_display_candidates_ = {L"(英文模式)"};
        current_page_ = 0;
        selected_in_page_ = 0;
        return MakeCurrentStateResult(KeyActionType::UpdateState);
    }

    const bool quick_sel = semicolon_quote_quick_select_ && IsComposing();
    const KeyEventKind event = ClassifyKeyEvent(
        vk, shift_down, ctrl_down, alt_down, chinese_punctuation_, quick_sel);
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
            if (raw_code_.find(L'z') == std::wstring::npos) {
                const auto exact = LookupExact(raw_code_);
                if (auto_commit_4code_ && exact.size() == 1) {
                    return CommitAndReset(exact.front());
                }
                if (beep_on_dup_4code_ && exact.size() > 1) {
                    QIQI_LOG_DEBUG("4-code lookup has {} exact matches: triggering dup-code chime", exact.size());
                    KeyProcessResult res = MakeCurrentStateResult(KeyActionType::UpdateState);
                    res.dup_4code_warning = true;
                    return res;
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
            const auto idx_opt = ResolveCandidateSelectIndex(vk, shift_down, semicolon_quote_quick_select_);
            const auto page = CurrentPageCandidates();
            if (idx_opt.has_value() && *idx_opt < page.size()) {
                return CommitAndReset(gsl::at(page, *idx_opt));
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
