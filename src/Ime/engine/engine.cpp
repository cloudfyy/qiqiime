#include "Ime/engine/engine.h"
#include "Ime/engine/default_dict.h"
#include "common/logger.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <span>

namespace qiqi {

void WubiEngine::ApplyConfig(const QiQiConfig& cfg) noexcept {
    SetPageSize(cfg.page_size);
    auto_commit_4code_ = cfg.auto_commit_4code;
    push_commit_5th_ = cfg.push_commit_5th;
    semicolon_quote_quick_select_ = cfg.semicolon_quote_quick_select;
    show_code_hint_ = cfg.show_code_hint;
    enable_z_temp_english_ = cfg.enable_z_temp_english;
    dynamic_phrase_freq_ = cfg.dynamic_phrase_freq;
    beep_on_empty_4code_ = cfg.beep_on_empty_4code;
    beep_on_dup_4code_ = cfg.beep_on_dup_4code;
    clear_on_empty_4code_ = cfg.clear_on_empty_4code;
    enable_key_sound_ = cfg.enable_key_sound;
    up_down_page_turn_ = cfg.up_down_page_turn;
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
    QIQI_LOG_DEBUG("WubiEngine mode switched to {}", enabled ? "Chinese" : "English");
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
    state_ = StateAfterCompositionReset(state_);
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
    current_page_ = 0;
    selected_in_page_ = 0;
    all_display_candidates_.clear();

    const bool has_wildcard = (raw_code_.find(L'z') != std::wstring::npos) ||
                              (raw_code_.find(L'Z') != std::wstring::npos);
    const bool want_display_hints = has_wildcard || (show_code_hint_ && raw_code_.size() < kMaxCodeLength);
    all_candidates_ = LookupCandidates(raw_code_, want_display_hints ? &all_display_candidates_ : nullptr);
    if (!want_display_hints) {
        all_display_candidates_ = all_candidates_;
    }
}

KeyProcessResult WubiEngine::MakeCurrentStateResult(KeyActionType action, std::wstring committed) const {
    KeyProcessResult res;
    res.action = action;
    res.committed_text = std::move(committed);
    res.composition_text = raw_code_;
    res.page_candidates = CurrentPageCandidates();
    res.all_display_candidates = all_display_candidates_;
    res.page_size = page_size_;
    res.selected_index = selected_in_page_;
    res.current_page = current_page_;
    res.total_pages = TotalPages();
    res.trigger_key_sound = enable_key_sound_ && (action != KeyActionType::PassThrough);
    return res;
}

TransitionRule WubiEngine::EvaluateKeyTransition(
    EngineState state, uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on) const noexcept {
    const KeyEventKind event = ClassifyKeyEvent(
        vk,
        shift_down,
        ctrl_down,
        alt_down,
        chinese_punctuation_,
        semicolon_quote_quick_select_ && IsComposingState(state),
        enable_z_temp_english_,
        full_width_mode_,
        state,
        caps_lock_on,
        up_down_page_turn_);
    return LookupTransition(state, event);
}

bool WubiEngine::ShouldEatKey(
    uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on) const noexcept {
    return EvaluateKeyTransition(state_, vk, shift_down, ctrl_down, alt_down, caps_lock_on).eat_key;
}

bool WubiEngine::ShouldEatKeyAfterReset(
    uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on) const noexcept {
    return EvaluateKeyTransition(StateAfterCompositionReset(state_), vk, shift_down, ctrl_down, alt_down, caps_lock_on).eat_key;
}

void WubiEngine::UpdateTempEnglishCandidates() {
    const bool has_payload = raw_code_.size() > 1;
    const std::wstring payload = has_payload ? raw_code_.substr(1) : L"z";
    all_candidates_ = {payload};
    all_display_candidates_ = {has_payload ? payload : L"(英文模式)"};
    current_page_ = 0;
    selected_in_page_ = 0;
}

KeyProcessResult WubiEngine::HandlePassThrough(const KeyInputContext& /*ctx*/) noexcept {
    return {};
}

KeyProcessResult WubiEngine::HandleAppendCode(const KeyInputContext& ctx) {
    AppendCodeAndRefresh(ctx.vk, ctx.next_state);
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleAppend4thCode(const KeyInputContext& ctx) {
    AppendCodeAndRefresh(ctx.vk, ctx.next_state);
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

KeyProcessResult WubiEngine::HandlePushCommitAndRestart(const KeyInputContext& ctx) {
    if (!push_commit_5th_) {
        return MakeCurrentStateResult(KeyActionType::UpdateState);
    }
    std::wstring auto_committed = CurrentSelectedCandidateOrRaw();
    Reset();
    AppendCodeAndRefresh(ctx.vk, ctx.next_state);
    return MakeCurrentStateResult(KeyActionType::Commit, std::move(auto_committed));
}

KeyProcessResult WubiEngine::HandlePopCode(const KeyInputContext& ctx) {
    raw_code_.pop_back();
    state_ = ctx.next_state;
    RefreshCandidates();
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleCancelComposition(const KeyInputContext& /*ctx*/) noexcept {
    Reset();
    KeyProcessResult res{};
    res.action = KeyActionType::Cancel;
    return res;
}

KeyProcessResult WubiEngine::HandleCommitSelected(const KeyInputContext& /*ctx*/) {
    if (clear_on_empty_4code_ && all_candidates_.empty()) {
        Reset();
        return MakeCurrentStateResult(KeyActionType::Cancel);
    }
    std::wstring selected = CurrentSelectedCandidateOrRaw();
    PromotePhraseForCode(raw_code_, selected);
    return CommitAndReset(std::move(selected));
}

KeyProcessResult WubiEngine::HandleCommitRawCode(const KeyInputContext& /*ctx*/) {
    return CommitAndReset(raw_code_);
}

KeyProcessResult WubiEngine::HandleCommitDigitIndex(const KeyInputContext& ctx) {
    const auto idx_opt = ResolveCandidateSelectIndex(ctx.vk, ctx.shift_down, semicolon_quote_quick_select_);
    const auto page = CurrentPageCandidates();
    if (idx_opt.has_value() && *idx_opt < page.size()) {
        std::wstring selected = gsl::at(page, *idx_opt);
        PromotePhraseForCode(raw_code_, selected);
        return CommitAndReset(std::move(selected));
    }
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleSelectPrevCandidate(const KeyInputContext& /*ctx*/) {
    if (selected_in_page_ > 0) {
        --selected_in_page_;
    } else if (current_page_ > 0) {
        --current_page_;
        const auto prev_page = CurrentPageCandidates();
        selected_in_page_ = prev_page.empty() ? 0 : (prev_page.size() - 1);
    }
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleSelectNextCandidate(const KeyInputContext& /*ctx*/) {
    const auto page = CurrentPageCandidates();
    if (!page.empty() && selected_in_page_ + 1 < page.size()) {
        ++selected_in_page_;
    } else if (current_page_ + 1 < TotalPages()) {
        ++current_page_;
        selected_in_page_ = 0;
    }
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleTurnPagePrev(const KeyInputContext& /*ctx*/) {
    if (current_page_ > 0) {
        --current_page_;
        selected_in_page_ = 0;
    }
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleTurnPageNext(const KeyInputContext& /*ctx*/) {
    if (current_page_ + 1 < TotalPages()) {
        ++current_page_;
        selected_in_page_ = 0;
    }
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleCommitPunctOnly(const KeyInputContext& ctx) {
    return MakeCurrentStateResult(
        KeyActionType::Commit,
        std::wstring(ResolveChinesePunctuation(ctx.vk, ctx.shift_down)));
}

KeyProcessResult WubiEngine::HandleCommitSelectedAndPunct(const KeyInputContext& ctx) {
    std::wstring committed = CurrentSelectedCandidateOrRaw();
    committed += ResolveChinesePunctuation(ctx.vk, ctx.shift_down);
    return CommitAndReset(std::move(committed));
}

KeyProcessResult WubiEngine::HandleCommitSelectedAndUpper(const KeyInputContext& ctx) {
    std::wstring committed = CurrentSelectedCandidateOrRaw();
    const wchar_t upper_ch = full_width_mode_
        ? MapFullWidthChar(ctx.vk, true).value_or(gsl::narrow_cast<wchar_t>(ctx.vk))
        : gsl::narrow_cast<wchar_t>(ctx.vk);
    committed.push_back(upper_ch);
    return CommitAndReset(std::move(committed));
}

KeyProcessResult WubiEngine::HandleStartTempEnglish(const KeyInputContext& ctx) {
    raw_code_ = L"z";
    state_ = ctx.next_state;
    UpdateTempEnglishCandidates();
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleAppendTempEnglish(const KeyInputContext& ctx) {
    if (const auto ascii = MapPrintableAsciiChar(ctx.vk, ctx.shift_down, ctx.caps_lock_on); ascii.has_value()) {
        raw_code_.push_back(gsl::narrow_cast<wchar_t>(*ascii));
        UpdateTempEnglishCandidates();
        return MakeCurrentStateResult(KeyActionType::UpdateState);
    }
    return {};
}

KeyProcessResult WubiEngine::HandlePopTempEnglish(const KeyInputContext& /*ctx*/) {
    if (raw_code_.size() <= 1) {
        Reset();
        return MakeCurrentStateResult(KeyActionType::Cancel);
    }
    raw_code_.pop_back();
    UpdateTempEnglishCandidates();
    return MakeCurrentStateResult(KeyActionType::UpdateState);
}

KeyProcessResult WubiEngine::HandleCommitTempEnglish(const KeyInputContext& /*ctx*/) {
    std::wstring committed = (raw_code_.size() > 1) ? raw_code_.substr(1) : L"z";
    return CommitAndReset(std::move(committed));
}

KeyProcessResult WubiEngine::HandleCommitFullWidth(const KeyInputContext& ctx) {
    if (const auto fw = MapFullWidthChar(ctx.vk, ctx.shift_down, ctx.caps_lock_on); fw.has_value()) {
        std::wstring committed;
        if (IsComposing()) {
            committed = CurrentSelectedCandidateOrRaw();
        }
        committed.push_back(*fw);
        return CommitAndReset(std::move(committed));
    }
    return {};
}

template <typename Fn>
consteval bool AllActionHandlersBound(std::span<const Fn> handlers) noexcept {
    for (const auto fn : handlers) {
#pragma warning(suppress : 26477)
        if (!fn) {
            return false;
        }
    }
    return true;
}

KeyProcessResult WubiEngine::ProcessKey(
    uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool caps_lock_on) {
    // Compile-time Command dispatch table indexed directly by TransitionOp
    static constexpr std::array<ActionHandler, static_cast<size_t>(TransitionOp::Count)> kActionHandlers = {
        &WubiEngine::HandlePassThrough,           // 0: PassThrough
        &WubiEngine::HandleAppendCode,            // 1: AppendCode
        &WubiEngine::HandleAppend4thCode,         // 2: Append4thCode
        &WubiEngine::HandlePushCommitAndRestart,  // 3: PushCommitAndRestart
        &WubiEngine::HandlePopCode,               // 4: PopCode
        &WubiEngine::HandleCancelComposition,     // 5: CancelComposition
        &WubiEngine::HandleCommitSelected,        // 6: CommitSelected
        &WubiEngine::HandleCommitRawCode,         // 7: CommitRawCode
        &WubiEngine::HandleCommitDigitIndex,      // 8: CommitDigitIndex
        &WubiEngine::HandleSelectPrevCandidate,   // 9: SelectPrevCandidate
        &WubiEngine::HandleSelectNextCandidate,   // 10: SelectNextCandidate
        &WubiEngine::HandleTurnPagePrev,          // 11: TurnPagePrev
        &WubiEngine::HandleTurnPageNext,          // 12: TurnPageNext
        &WubiEngine::HandleCommitPunctOnly,       // 13: CommitPunctOnly
        &WubiEngine::HandleCommitSelectedAndPunct,// 14: CommitSelectedAndPunct
        &WubiEngine::HandleCommitSelectedAndUpper,// 15: CommitSelectedAndUpper
        &WubiEngine::HandleStartTempEnglish,      // 16: StartTempEnglish
        &WubiEngine::HandleAppendTempEnglish,     // 17: AppendTempEnglish
        &WubiEngine::HandlePopTempEnglish,        // 18: PopTempEnglish
        &WubiEngine::HandleCommitTempEnglish,     // 19: CommitTempEnglish
        &WubiEngine::HandleCommitFullWidth        // 20: CommitFullWidth
    };
    static_assert(AllActionHandlersBound(std::span<const ActionHandler>(kActionHandlers)));

    const TransitionRule rule = EvaluateKeyTransition(state_, vk, shift_down, ctrl_down, alt_down, caps_lock_on);
    if (rule.op != TransitionOp::PassThrough) {
        QIQI_LOG_TRACE("FSM transition: state={} -> op={} next_state={}",
                       static_cast<unsigned int>(state_),
                       static_cast<unsigned int>(rule.op),
                       static_cast<unsigned int>(rule.next_state));
    }
    const KeyInputContext ctx{vk, shift_down, caps_lock_on, rule.next_state};
    return (this->*gsl::at(kActionHandlers, static_cast<size_t>(rule.op)))(ctx);
}

} // namespace qiqi
