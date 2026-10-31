#pragma once
#include <gsl/gsl>
#include <array>
#include <cstddef>

#include "Ime/engine/fsm_types.h"

namespace qiqi {

constexpr TransitionRule ComputeTransitionRule(EngineState state, KeyEventKind event) noexcept {
    if (event == KeyEventKind::None) {
        return {TransitionOp::PassThrough, state, false};
    }

    if (state == EngineState::English) {
        if (event == KeyEventKind::PrintableAscii) {
            return {TransitionOp::CommitFullWidth, EngineState::English, true};
        }
        return {TransitionOp::PassThrough, EngineState::English, false};
    }

    if (state == EngineState::Idle) {
        switch (event) {
            case KeyEventKind::TempEnglishGuide:
                return {TransitionOp::StartTempEnglish, EngineState::TempEnglish, true};
            case KeyEventKind::CodeChar:
                return {TransitionOp::AppendCode, EngineState::Composing1, true};
            case KeyEventKind::ChinesePunct:
                return {TransitionOp::CommitPunctOnly, EngineState::Idle, true};
            case KeyEventKind::PrintableAscii:
                return {TransitionOp::CommitFullWidth, EngineState::Idle, true};
            default:
                return {TransitionOp::PassThrough, EngineState::Idle, false};
        }
    }

    if (state == EngineState::TempEnglish) {
        switch (event) {
            case KeyEventKind::Escape:
                return {TransitionOp::CancelComposition, EngineState::Idle, true};
            case KeyEventKind::Backspace:
                return {TransitionOp::PopTempEnglish, EngineState::TempEnglish, true};
            case KeyEventKind::Space:
            case KeyEventKind::Enter:
                return {TransitionOp::CommitTempEnglish, EngineState::Idle, true};
            case KeyEventKind::CapsLockToggle:
                return {TransitionOp::CommitTempEnglish, EngineState::Idle, false};
            case KeyEventKind::PrintableAscii:
            case KeyEventKind::CodeChar:
            case KeyEventKind::UpperChar:
                return {TransitionOp::AppendTempEnglish, EngineState::TempEnglish, true};
            default:
                return {TransitionOp::PassThrough, EngineState::TempEnglish, false};
        }
    }

    // Composing states (Composing1..Composing4)
    switch (event) {
        case KeyEventKind::CodeChar:
            switch (state) {
                case EngineState::Composing1:
                    return {TransitionOp::AppendCode, EngineState::Composing2, true};
                case EngineState::Composing2:
                    return {TransitionOp::AppendCode, EngineState::Composing3, true};
                case EngineState::Composing3:
                    return {TransitionOp::Append4thCode, EngineState::Composing4, true};
                case EngineState::Composing4:
                default:
                    return {TransitionOp::PushCommitAndRestart, EngineState::Composing1, true};
            }
        case KeyEventKind::Backspace:
            switch (state) {
                case EngineState::Composing1:
                    return {TransitionOp::CancelComposition, EngineState::Idle, true};
                case EngineState::Composing2:
                    return {TransitionOp::PopCode, EngineState::Composing1, true};
                case EngineState::Composing3:
                    return {TransitionOp::PopCode, EngineState::Composing2, true};
                case EngineState::Composing4:
                default:
                    return {TransitionOp::PopCode, EngineState::Composing3, true};
            }
        case KeyEventKind::Escape:
            return {TransitionOp::CancelComposition, EngineState::Idle, true};
        case KeyEventKind::Space:
            return {TransitionOp::CommitSelected, EngineState::Idle, true};
        case KeyEventKind::Enter:
            return {TransitionOp::CommitRawCode, EngineState::Idle, true};
        case KeyEventKind::CapsLockToggle:
            return {TransitionOp::CommitRawCode, EngineState::Idle, false};
        case KeyEventKind::DigitSelect:
            return {TransitionOp::CommitDigitIndex, EngineState::Idle, true};
        case KeyEventKind::CandidatePrev:
            return {TransitionOp::SelectPrevCandidate, state, true};
        case KeyEventKind::CandidateNext:
            return {TransitionOp::SelectNextCandidate, state, true};
        case KeyEventKind::PageUp:
            return {TransitionOp::TurnPagePrev, state, true};
        case KeyEventKind::PageDown:
            return {TransitionOp::TurnPageNext, state, true};
        case KeyEventKind::ChinesePunct:
            return {TransitionOp::CommitSelectedAndPunct, EngineState::Idle, true};
        case KeyEventKind::UpperChar:
            return {TransitionOp::CommitSelectedAndUpper, EngineState::Idle, true};
        case KeyEventKind::PrintableAscii:
            return {TransitionOp::CommitFullWidth, EngineState::Idle, true};
        default:
            return {TransitionOp::PassThrough, state, false};
    }
}

using FsmTable = std::array<
    std::array<TransitionRule, static_cast<size_t>(KeyEventKind::Count)>,
    static_cast<size_t>(EngineState::Count)>;

constexpr FsmTable BuildWubiFsmTable() noexcept {
    FsmTable table{};
    for (size_t s = 0; s < static_cast<size_t>(EngineState::Count); ++s) {
        for (size_t e = 0; e < static_cast<size_t>(KeyEventKind::Count); ++e) {
            gsl::at(gsl::at(table, s), e) = ComputeTransitionRule(
                static_cast<EngineState>(s), static_cast<KeyEventKind>(e));
        }
    }
    return table;
}

// Compile-time generated state transition lookup table (7 states x 16 events)
inline constexpr FsmTable kWubiFsmTable = BuildWubiFsmTable();

constexpr TransitionRule LookupTransition(EngineState state, KeyEventKind event) noexcept {
    const auto s = static_cast<size_t>(state);
    const auto e = static_cast<size_t>(event);
    if (s >= static_cast<size_t>(EngineState::Count) ||
        e >= static_cast<size_t>(KeyEventKind::Count)) {
        return {TransitionOp::PassThrough, state, false};
    }
    return gsl::at(gsl::at(kWubiFsmTable, s), e);
}

// Compile-time invariants verification on kWubiFsmTable
static_assert(!LookupTransition(EngineState::English, KeyEventKind::CodeChar).eat_key);
static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::Space).eat_key);
static_assert(LookupTransition(EngineState::Idle, KeyEventKind::CodeChar).next_state == EngineState::Composing1);
static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::CodeChar).next_state == EngineState::Composing2);
static_assert(LookupTransition(EngineState::Composing2, KeyEventKind::CodeChar).next_state == EngineState::Composing3);
static_assert(LookupTransition(EngineState::Composing3, KeyEventKind::CodeChar).op == TransitionOp::Append4thCode);
static_assert(LookupTransition(EngineState::Composing3, KeyEventKind::CodeChar).next_state == EngineState::Composing4);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::CodeChar).op == TransitionOp::PushCommitAndRestart);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::CodeChar).next_state == EngineState::Composing1);
static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::Backspace).op == TransitionOp::CancelComposition);
static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::Backspace).next_state == EngineState::Idle);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::Backspace).op == TransitionOp::PopCode);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::Backspace).next_state == EngineState::Composing3);
static_assert(LookupTransition(EngineState::Idle, KeyEventKind::TempEnglishGuide).op == TransitionOp::StartTempEnglish);
static_assert(LookupTransition(EngineState::Idle, KeyEventKind::TempEnglishGuide).next_state == EngineState::TempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::PrintableAscii).op == TransitionOp::AppendTempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::Space).op == TransitionOp::CommitTempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::Backspace).op == TransitionOp::PopTempEnglish);
static_assert(LookupTransition(EngineState::English, KeyEventKind::PrintableAscii).op == TransitionOp::CommitFullWidth);

} // namespace qiqi
