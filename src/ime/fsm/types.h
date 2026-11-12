// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <cstdint>

namespace qiqi {

// Explicit finite states of the Wubi 86 input engine
enum class EngineState : uint8_t {
    English = 0,     // English pass-through mode
    Idle = 1,        // Chinese mode, 0 codes (not composing)
    Composing1 = 2,  // Chinese mode, 1 code entered
    Composing2 = 3,  // Chinese mode, 2 codes entered
    Composing3 = 4,  // Chinese mode, 3 codes entered
    Composing4 = 5,  // Chinese mode, 4 codes entered (max buffer length)
    TempEnglish = 6, // 'z'-guided temporary English string input mode
    Count = 7
};

// Discrete input event categories classified at compile time from (state, vk, modifiers, config flags)
enum class KeyEventKind : uint8_t {
    None = 0,
    CodeChar = 1,         // Lowercase 'a'-'z'
    UpperChar = 2,        // Uppercase 'A'-'Z' (Shift + letter or CapsLock + letter)
    Space = 3,            // VK_SPACE
    Enter = 4,            // VK_RETURN
    Backspace = 5,        // VK_BACK
    Escape = 6,           // VK_ESCAPE
    DigitSelect = 7,      // '1'-'9' (or ';' / ''' when quick-select is enabled while composing)
    CandidatePrev = 8,    // VK_LEFT (or VK_UP when up_down_page_turn is false)
    CandidateNext = 9,    // VK_RIGHT (or VK_DOWN when up_down_page_turn is false)
    PageUp = 10,          // VK_PRIOR, '-' (or VK_UP when up_down_page_turn is true)
    PageDown = 11,        // VK_NEXT, '=' (or VK_DOWN when up_down_page_turn is true)
    ChinesePunct = 12,    // Mapped Chinese full-width punctuation
    TempEnglishGuide = 13,// 'z' key in Idle when enable_z_temp_english is active
    PrintableAscii = 14,  // Printable ASCII key in TempEnglish or FullWidth mode
    CapsLockToggle = 15,  // VK_CAPITAL pressed mid-composition: commit raw code without eating OS toggle
    PinCandidate = 16,    // Ctrl+'1'..'9' while composing: pin candidate to top of code
    DeleteCandidate = 17, // Ctrl+Delete or Ctrl+Shift+'1'..'9' while composing: delete user word or block system word
    Count = 18
};

// State machine transition actions executed by the engine
enum class TransitionOp : uint8_t {
    PassThrough = 0,
    AppendCode = 1,             // Append 1st..3rd code char and refresh candidates
    Append4thCode = 2,          // Append 4th code char; auto-commit if unique exact match without 'z'
    PushCommitAndRestart = 3,   // 5th key: commit current selection and start new 1-code composition
    PopCode = 4,                // Backspace with >=2 codes: pop last char and refresh candidates
    CancelComposition = 5,      // Escape or Backspace on 1st code: clear composition -> Idle
    CommitSelected = 6,         // Space: commit current selected candidate -> Idle
    CommitRawCode = 7,          // Enter: commit raw ASCII code -> Idle
    CommitDigitIndex = 8,       // '1'-'9' / ';' / ''': commit candidate at index if in page range
    SelectPrevCandidate = 9,    // Up/Left arrow: move selection to previous candidate (cross-page)
    SelectNextCandidate = 10,   // Down/Right arrow: move selection to next candidate (cross-page)
    TurnPagePrev = 11,          // PageUp / '-': previous page
    TurnPageNext = 12,          // PageDown / '=': next page
    CommitPunctOnly = 13,       // Idle punctuation: directly commit Chinese punctuation
    CommitSelectedAndPunct = 14,// Composing punctuation: commit current candidate + punctuation -> Idle
    CommitSelectedAndUpper = 15,// Composing Shift+Letter: commit current candidate + uppercase letter -> Idle
    StartTempEnglish = 16,      // Idle 'z': enter TempEnglish state with leading 'z' guide
    AppendTempEnglish = 17,     // TempEnglish printable ASCII: append char to temporary English buffer
    PopTempEnglish = 18,        // TempEnglish Backspace: pop last char or cancel if only 'z' remains
    CommitTempEnglish = 19,     // TempEnglish Space/Enter: commit temporary English string -> Idle
    CommitFullWidth = 20,       // FullWidth mode fallback: commit (optional candidate +) full-width char -> Idle
    PinDigitCandidate = 21,     // Ctrl+'1'..'9': pin candidate at digit index to top
    DeleteOrBlockCandidate = 22,// Ctrl+Delete or Ctrl+Shift+'1'..'9': delete user entry or block system entry
    Count = 23
};

struct TransitionRule {
    TransitionOp op = TransitionOp::PassThrough;
    bool eat_key = false;
};

constexpr EngineState StateAfterCompositionReset(EngineState state) noexcept {
    return state == EngineState::English ? EngineState::English : EngineState::Idle;
}

constexpr bool IsWubiComposingState(EngineState state) noexcept {
    return state == EngineState::Composing1 ||
           state == EngineState::Composing2 ||
           state == EngineState::Composing3 ||
           state == EngineState::Composing4;
}

constexpr bool IsComposingState(EngineState state) noexcept {
    return IsWubiComposingState(state) || state == EngineState::TempEnglish;
}

} // namespace qiqi
