#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace qiqi {

// Shared pagination helpers used by both WubiEngine and CandidateUI
constexpr size_t ComputeTotalPages(size_t total_items, size_t page_size) noexcept {
    if (total_items == 0 || page_size == 0) {
        return 0;
    }
    return (total_items + page_size - 1) / page_size;
}

constexpr std::pair<size_t, size_t> ComputePageRange(
    size_t total_items, size_t page_size, size_t current_page) noexcept {
    const size_t start = current_page * page_size;
    if (start >= total_items || page_size == 0) {
        return {0, 0};
    }
    return {start, std::min(start + page_size, total_items)};
}

// Explicit finite states of the Wubi 86 input engine
enum class EngineState : uint8_t {
    English = 0,    // English pass-through mode
    Idle = 1,       // Chinese mode, 0 codes (not composing)
    Composing1 = 2, // Chinese mode, 1 code entered
    Composing2 = 3, // Chinese mode, 2 codes entered
    Composing3 = 4, // Chinese mode, 3 codes entered
    Composing4 = 5, // Chinese mode, 4 codes entered (max buffer length)
    Count = 6
};

// Discrete input event categories classified at compile time from (vk, modifiers)
enum class KeyEventKind : uint8_t {
    None = 0,
    CodeChar = 1,      // Lowercase 'a'-'z'
    Space = 2,         // VK_SPACE
    Enter = 3,         // VK_RETURN
    Backspace = 4,     // VK_BACK
    Escape = 5,        // VK_ESCAPE
    DigitSelect = 6,   // '1'-'9' without Shift
    CandidateUp = 7,   // VK_UP
    CandidateDown = 8, // VK_DOWN
    PageUp = 9,        // VK_PRIOR or '-'
    PageDown = 10,     // VK_NEXT or '='
    ChinesePunct = 11, // Mapped Chinese full-width punctuation
    Count = 12
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
    CommitDigitIndex = 8,       // '1'-'9': commit candidate at index if in page range
    SelectPrevCandidate = 9,    // Up arrow: move selection up
    SelectNextCandidate = 10,   // Down arrow: move selection down
    TurnPagePrev = 11,          // PageUp / '-': previous page
    TurnPageNext = 12,          // PageDown / '=': next page
    CommitPunctOnly = 13,       // Idle punctuation: directly commit Chinese punctuation
    CommitSelectedAndPunct = 14 // Composing punctuation: commit current candidate + punctuation -> Idle
};

struct TransitionRule {
    TransitionOp op = TransitionOp::PassThrough;
    EngineState next_state = EngineState::Idle;
    bool eat_key = false;
};

constexpr bool IsComposingState(EngineState state) noexcept {
    return state == EngineState::Composing1 ||
           state == EngineState::Composing2 ||
           state == EngineState::Composing3 ||
           state == EngineState::Composing4;
}

constexpr std::optional<std::wstring_view> MapChinesePunctuation(uint32_t vk, bool shift_down) noexcept {
    switch (vk) {
        case VK_OEM_COMMA:  return shift_down ? std::optional<std::wstring_view>(L"《") : std::optional<std::wstring_view>(L"，");
        case VK_OEM_PERIOD: return shift_down ? std::optional<std::wstring_view>(L"》") : std::optional<std::wstring_view>(L"。");
        case VK_OEM_1:      return shift_down ? std::optional<std::wstring_view>(L"：") : std::optional<std::wstring_view>(L"；");
        case VK_OEM_2:      return shift_down ? std::optional<std::wstring_view>(L"？") : std::optional<std::wstring_view>(L"、");
        case '1':           return shift_down ? std::optional<std::wstring_view>(L"！") : std::nullopt;
        case '4':           return shift_down ? std::optional<std::wstring_view>(L"￥") : std::nullopt;
        case '6':           return shift_down ? std::optional<std::wstring_view>(L"……") : std::nullopt;
        case '9':           return shift_down ? std::optional<std::wstring_view>(L"（") : std::nullopt;
        case '0':           return shift_down ? std::optional<std::wstring_view>(L"）") : std::nullopt;
        default:            return std::nullopt;
    }
}

constexpr wchar_t AsciiToFullWidth(char ch) noexcept {
    if (ch == ' ') {
        return L'\u3000';
    }
    if (ch >= 0x21 && ch <= 0x7E) {
        return static_cast<wchar_t>(ch - 0x21 + 0xFF01);
    }
    return static_cast<wchar_t>(ch);
}

// Map a virtual key + Shift state to its Unicode Full-Width (全角) character.
constexpr std::optional<wchar_t> MapFullWidthChar(uint32_t vk, bool shift_down) noexcept {
    if (vk == VK_SPACE) {
        return L'\u3000';
    }
    if (vk >= 'A' && vk <= 'Z') {
        const char ascii = static_cast<char>(shift_down ? vk : (vk - 'A' + 'a'));
        return AsciiToFullWidth(ascii);
    }
    if (vk >= '0' && vk <= '9') {
        if (!shift_down) {
            return AsciiToFullWidth(static_cast<char>(vk));
        }
        constexpr std::array<char, 10> kShiftedDigits = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        return AsciiToFullWidth(kShiftedDigits[vk - '0']);
    }
    switch (vk) {
        case VK_OEM_1:      return AsciiToFullWidth(shift_down ? ':' : ';');
        case VK_OEM_PLUS:   return AsciiToFullWidth(shift_down ? '+' : '=');
        case VK_OEM_COMMA:  return AsciiToFullWidth(shift_down ? '<' : ',');
        case VK_OEM_MINUS:  return AsciiToFullWidth(shift_down ? '_' : '-');
        case VK_OEM_PERIOD: return AsciiToFullWidth(shift_down ? '>' : '.');
        case VK_OEM_2:      return AsciiToFullWidth(shift_down ? '?' : '/');
        case VK_OEM_3:      return AsciiToFullWidth(shift_down ? '~' : '`');
        case VK_OEM_4:      return AsciiToFullWidth(shift_down ? '{' : '[');
        case VK_OEM_5:      return AsciiToFullWidth(shift_down ? '|' : '\\');
        case VK_OEM_6:      return AsciiToFullWidth(shift_down ? '}' : ']');
        case VK_OEM_7:      return AsciiToFullWidth(shift_down ? '"' : '\'');
        default:            return std::nullopt;
    }
}

constexpr KeyEventKind ClassifyKeyEvent(
    uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down, bool chinese_punct = true) noexcept {
    if (ctrl_down || alt_down) {
        return KeyEventKind::None;
    }
    if (!shift_down && vk >= 'A' && vk <= 'Z') {
        return KeyEventKind::CodeChar;
    }
    if (vk == VK_SPACE) {
        return KeyEventKind::Space;
    }
    if (vk == VK_RETURN) {
        return KeyEventKind::Enter;
    }
    if (vk == VK_BACK) {
        return KeyEventKind::Backspace;
    }
    if (vk == VK_ESCAPE) {
        return KeyEventKind::Escape;
    }
    if (vk == VK_UP) {
        return KeyEventKind::CandidateUp;
    }
    if (vk == VK_DOWN) {
        return KeyEventKind::CandidateDown;
    }
    if (vk == VK_PRIOR || (!shift_down && vk == VK_OEM_MINUS)) {
        return KeyEventKind::PageUp;
    }
    if (vk == VK_NEXT || (!shift_down && vk == VK_OEM_PLUS)) {
        return KeyEventKind::PageDown;
    }
    if (!shift_down && vk >= '1' && vk <= '9') {
        return KeyEventKind::DigitSelect;
    }
    if (chinese_punct && MapChinesePunctuation(vk, shift_down).has_value()) {
        return KeyEventKind::ChinesePunct;
    }
    return KeyEventKind::None;
}

constexpr TransitionRule ComputeTransitionRule(EngineState state, KeyEventKind event) noexcept {
    if (state == EngineState::English || event == KeyEventKind::None) {
        return {TransitionOp::PassThrough, state, false};
    }

    if (state == EngineState::Idle) {
        switch (event) {
            case KeyEventKind::CodeChar:
                return {TransitionOp::AppendCode, EngineState::Composing1, true};
            case KeyEventKind::ChinesePunct:
                return {TransitionOp::CommitPunctOnly, EngineState::Idle, true};
            default:
                return {TransitionOp::PassThrough, EngineState::Idle, false};
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
        case KeyEventKind::DigitSelect:
            return {TransitionOp::CommitDigitIndex, EngineState::Idle, true};
        case KeyEventKind::CandidateUp:
            return {TransitionOp::SelectPrevCandidate, state, true};
        case KeyEventKind::CandidateDown:
            return {TransitionOp::SelectNextCandidate, state, true};
        case KeyEventKind::PageUp:
            return {TransitionOp::TurnPagePrev, state, true};
        case KeyEventKind::PageDown:
            return {TransitionOp::TurnPageNext, state, true};
        case KeyEventKind::ChinesePunct:
            return {TransitionOp::CommitSelectedAndPunct, EngineState::Idle, true};
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
            table[s][e] = ComputeTransitionRule(
                static_cast<EngineState>(s), static_cast<KeyEventKind>(e));
        }
    }
    return table;
}

// Compile-time generated state transition lookup table (6 states x 12 events)
inline constexpr FsmTable kWubiFsmTable = BuildWubiFsmTable();

constexpr TransitionRule LookupTransition(EngineState state, KeyEventKind event) noexcept {
    const auto s = static_cast<size_t>(state);
    const auto e = static_cast<size_t>(event);
    if (s >= static_cast<size_t>(EngineState::Count) ||
        e >= static_cast<size_t>(KeyEventKind::Count)) {
        return {TransitionOp::PassThrough, state, false};
    }
    return kWubiFsmTable[s][e];
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

} // namespace qiqi
