#pragma once
#include <gsl/gsl>
#include <array>
#include <cstddef>

#include "ime/fsm/types.h"

namespace qiqi {

constexpr TransitionRule ComputeTransitionRule(EngineState state, KeyEventKind event) noexcept {
    if (event == KeyEventKind::None) {
        return {TransitionOp::PassThrough, false};
    }

    if (state == EngineState::English) {
        if (event == KeyEventKind::PrintableAscii) {
            return {TransitionOp::CommitFullWidth, true};
        }
        return {TransitionOp::PassThrough, false};
    }

    if (state == EngineState::Idle) {
        switch (event) {
            case KeyEventKind::TempEnglishGuide:
                return {TransitionOp::StartTempEnglish, true};
            case KeyEventKind::CodeChar:
                return {TransitionOp::AppendCode, true};
            case KeyEventKind::ChinesePunct:
                return {TransitionOp::CommitPunctOnly, true};
            case KeyEventKind::PrintableAscii:
                return {TransitionOp::CommitFullWidth, true};
            default:
                return {TransitionOp::PassThrough, false};
        }
    }

    if (state == EngineState::TempEnglish) {
        switch (event) {
            case KeyEventKind::Escape:
                return {TransitionOp::CancelComposition, true};
            case KeyEventKind::Backspace:
                return {TransitionOp::PopTempEnglish, true};
            case KeyEventKind::Space:
            case KeyEventKind::Enter:
                return {TransitionOp::CommitTempEnglish, true};
            case KeyEventKind::CapsLockToggle:
                return {TransitionOp::CommitTempEnglish, false};
            case KeyEventKind::PrintableAscii:
            case KeyEventKind::CodeChar:
            case KeyEventKind::UpperChar:
                return {TransitionOp::AppendTempEnglish, true};
            default:
                return {TransitionOp::PassThrough, false};
        }
    }

    // Composing states (Composing1..Composing4)
    switch (event) {
        case KeyEventKind::CodeChar:
            switch (state) {
                case EngineState::Composing1:
                case EngineState::Composing2:
                    return {TransitionOp::AppendCode, true};
                case EngineState::Composing3:
                    return {TransitionOp::Append4thCode, true};
                case EngineState::Composing4:
                default:
                    return {TransitionOp::PushCommitAndRestart, true};
            }
        case KeyEventKind::Backspace:
            switch (state) {
                case EngineState::Composing1:
                    return {TransitionOp::CancelComposition, true};
                case EngineState::Composing2:
                case EngineState::Composing3:
                case EngineState::Composing4:
                default:
                    return {TransitionOp::PopCode, true};
            }
        case KeyEventKind::Escape:
            return {TransitionOp::CancelComposition, true};
        case KeyEventKind::Space:
            return {TransitionOp::CommitSelected, true};
        case KeyEventKind::Enter:
            return {TransitionOp::CommitRawCode, true};
        case KeyEventKind::CapsLockToggle:
            return {TransitionOp::CommitRawCode, false};
        case KeyEventKind::DigitSelect:
            return {TransitionOp::CommitDigitIndex, true};
        case KeyEventKind::CandidatePrev:
            return {TransitionOp::SelectPrevCandidate, true};
        case KeyEventKind::CandidateNext:
            return {TransitionOp::SelectNextCandidate, true};
        case KeyEventKind::PageUp:
            return {TransitionOp::TurnPagePrev, true};
        case KeyEventKind::PageDown:
            return {TransitionOp::TurnPageNext, true};
        case KeyEventKind::ChinesePunct:
            return {TransitionOp::CommitSelectedAndPunct, true};
        case KeyEventKind::UpperChar:
            return {TransitionOp::CommitSelectedAndUpper, true};
        case KeyEventKind::PrintableAscii:
            return {TransitionOp::CommitFullWidth, true};
        default:
            return {TransitionOp::PassThrough, false};
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

// Compile-time dispatch table; final states are resolved by runtime handlers.
inline constexpr FsmTable kWubiFsmTable = BuildWubiFsmTable();

constexpr TransitionRule LookupTransition(EngineState state, KeyEventKind event) noexcept {
    const auto s = static_cast<size_t>(state);
    const auto e = static_cast<size_t>(event);
    if (s >= static_cast<size_t>(EngineState::Count) ||
        e >= static_cast<size_t>(KeyEventKind::Count)) {
        return {TransitionOp::PassThrough, false};
    }
    return gsl::at(gsl::at(kWubiFsmTable, s), e);
}

// Compile-time invariants verification on kWubiFsmTable
static_assert(!LookupTransition(EngineState::English, KeyEventKind::CodeChar).eat_key);
static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::Space).eat_key);
static_assert(LookupTransition(EngineState::Idle, KeyEventKind::CodeChar).op == TransitionOp::AppendCode);
static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::CodeChar).op == TransitionOp::AppendCode);
static_assert(LookupTransition(EngineState::Composing2, KeyEventKind::CodeChar).op == TransitionOp::AppendCode);
static_assert(LookupTransition(EngineState::Composing3, KeyEventKind::CodeChar).op == TransitionOp::Append4thCode);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::CodeChar).op == TransitionOp::PushCommitAndRestart);
static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::Backspace).op == TransitionOp::CancelComposition);
static_assert(LookupTransition(EngineState::Composing2, KeyEventKind::Backspace).op == TransitionOp::PopCode);
static_assert(LookupTransition(EngineState::Composing3, KeyEventKind::Backspace).op == TransitionOp::PopCode);
static_assert(LookupTransition(EngineState::Composing4, KeyEventKind::Backspace).op == TransitionOp::PopCode);
static_assert(LookupTransition(EngineState::Idle, KeyEventKind::TempEnglishGuide).op == TransitionOp::StartTempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::PrintableAscii).op == TransitionOp::AppendTempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::Space).op == TransitionOp::CommitTempEnglish);
static_assert(LookupTransition(EngineState::TempEnglish, KeyEventKind::Backspace).op == TransitionOp::PopTempEnglish);
static_assert(LookupTransition(EngineState::English, KeyEventKind::PrintableAscii).op == TransitionOp::CommitFullWidth);

} // namespace qiqi
