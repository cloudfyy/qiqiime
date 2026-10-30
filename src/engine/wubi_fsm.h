#pragma once
#include <windows.h>
#include <gsl/gsl>
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
    CandidateUp = 8,      // VK_UP
    CandidateDown = 9,    // VK_DOWN
    PageUp = 10,          // VK_PRIOR or '-'
    PageDown = 11,        // VK_NEXT or '='
    ChinesePunct = 12,    // Mapped Chinese full-width punctuation
    TempEnglishGuide = 13,// 'z' key in Idle when enable_z_temp_english is active
    PrintableAscii = 14,  // Printable ASCII key in TempEnglish or FullWidth mode
    CapsLockToggle = 15,  // VK_CAPITAL pressed mid-composition: commit raw code without eating OS toggle
    Count = 16
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
    SelectPrevCandidate = 9,    // Up arrow: move selection up
    SelectNextCandidate = 10,   // Down arrow: move selection down
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
    Count = 21
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
           state == EngineState::Composing4 ||
           state == EngineState::TempEnglish;
}

constexpr std::optional<std::wstring_view> MapChinesePunctuation(uint32_t vk, bool shift_down) noexcept {
    switch (vk) {
        case VK_OEM_COMMA:  return shift_down ? std::optional<std::wstring_view>(L"《") : std::optional<std::wstring_view>(L"，");
        case VK_OEM_PERIOD: return shift_down ? std::optional<std::wstring_view>(L"》") : std::optional<std::wstring_view>(L"。");
        case VK_OEM_1:      return shift_down ? std::optional<std::wstring_view>(L"：") : std::optional<std::wstring_view>(L"；");
        case VK_OEM_2:      return shift_down ? std::optional<std::wstring_view>(L"？") : std::optional<std::wstring_view>(L"、");
        case VK_OEM_3:      return shift_down ? std::optional<std::wstring_view>(L"～") : std::optional<std::wstring_view>(L"·");
        case VK_OEM_4:      return shift_down ? std::optional<std::wstring_view>(L"｛") : std::optional<std::wstring_view>(L"【");
        case VK_OEM_5:      return shift_down ? std::nullopt : std::optional<std::wstring_view>(L"、");
        case VK_OEM_6:      return shift_down ? std::optional<std::wstring_view>(L"｝") : std::optional<std::wstring_view>(L"】");
        case VK_OEM_7:      return shift_down ? std::optional<std::wstring_view>(L"“") : std::optional<std::wstring_view>(L"‘");
        case VK_OEM_MINUS:  return shift_down ? std::optional<std::wstring_view>(L"——") : std::nullopt;
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
        return gsl::narrow_cast<wchar_t>(ch - 0x21 + 0xFF01);
    }
    return gsl::narrow_cast<wchar_t>(ch);
}

// Map a virtual key + Shift + CapsLock state to its printable ASCII character (0x20..0x7E).
constexpr std::optional<char> MapPrintableAsciiChar(
    uint32_t vk, bool shift_down, bool caps_lock_on = false) noexcept {
    if (vk == VK_SPACE) {
        return ' ';
    }
    if (vk >= 'A' && vk <= 'Z') {
        const bool is_upper = (shift_down != caps_lock_on);
        return gsl::narrow_cast<char>(is_upper ? vk : (vk - 'A' + 'a'));
    }
    if (vk >= '0' && vk <= '9') {
        if (!shift_down) {
            return gsl::narrow_cast<char>(vk);
        }
        constexpr std::array<char, 10> kShiftedDigits = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        return gsl::at(kShiftedDigits, vk - '0');
    }
    switch (vk) {
        case VK_OEM_1:      return shift_down ? ':' : ';';
        case VK_OEM_PLUS:   return shift_down ? '+' : '=';
        case VK_OEM_COMMA:  return shift_down ? '<' : ',';
        case VK_OEM_MINUS:  return shift_down ? '_' : '-';
        case VK_OEM_PERIOD: return shift_down ? '>' : '.';
        case VK_OEM_2:      return shift_down ? '?' : '/';
        case VK_OEM_3:      return shift_down ? '~' : '`';
        case VK_OEM_4:      return shift_down ? '{' : '[';
        case VK_OEM_5:      return shift_down ? '|' : '\\';
        case VK_OEM_6:      return shift_down ? '}' : ']';
        case VK_OEM_7:      return shift_down ? '"' : '\'';
        default:            return std::nullopt;
    }
}

// Map a virtual key + Shift + CapsLock state to its Unicode Full-Width (全角) character.
constexpr std::optional<wchar_t> MapFullWidthChar(
    uint32_t vk, bool shift_down, bool caps_lock_on = false) noexcept {
    if (const auto ascii = MapPrintableAsciiChar(vk, shift_down, caps_lock_on); ascii.has_value()) {
        return AsciiToFullWidth(*ascii);
    }
    return std::nullopt;
}

// Resolve 0-based page candidate index for digit keys ('1'..'9' -> 0..8)
// and optional ';' (VK_OEM_1 -> 1) / ''' (VK_OEM_7 -> 2) quick-select keys.
constexpr std::optional<size_t> ResolveCandidateSelectIndex(
    uint32_t vk, bool shift_down, bool quick_select_enabled = false) noexcept {
    if (shift_down) {
        return std::nullopt;
    }
    if (vk >= '1' && vk <= '9') {
        return static_cast<size_t>(vk - '1');
    }
    if (quick_select_enabled) {
        if (vk == VK_OEM_1) {
            return 1u; // ';' selects 2nd candidate
        }
        if (vk == VK_OEM_7) {
            return 2u; // ''' selects 3rd candidate
        }
    }
    return std::nullopt;
}

struct KeyClassifyFlags {
    bool chinese_punct = true;
    bool quick_select = false;
    bool z_temp_english = false;
    bool full_width = false;
    EngineState state = EngineState::Idle;
};

constexpr KeyEventKind ClassifyKeyEvent(
    uint32_t vk,
    bool shift_down,
    bool ctrl_down,
    bool alt_down,
    bool chinese_punct = true,
    bool composing_quick_select = false,
    bool z_temp_english = false,
    bool full_width = false,
    EngineState state = EngineState::Idle,
    bool caps_lock_on = false) noexcept {
    if (ctrl_down || alt_down) {
        return KeyEventKind::None;
    }

    if (vk == VK_CAPITAL) {
        return IsComposingState(state) ? KeyEventKind::CapsLockToggle : KeyEventKind::None;
    }

    // Industry-standard CapsLock English pass-through:
    // When CapsLock is ON in Idle state, all keys (uppercase letters, digits, space, ASCII punctuation)
    // bypass Wubi composition and Chinese punctuation conversion, passing through directly as English
    // (or converting to Full-Width if Full-Width mode is active).
    if (caps_lock_on && state == EngineState::Idle) {
        if (full_width && MapFullWidthChar(vk, shift_down, true).has_value()) {
            return KeyEventKind::PrintableAscii;
        }
        return KeyEventKind::None;
    }

    // In TempEnglish state, all printable non-space ASCII keys (including digits, '-', '=', punctuation)
    // are appended as literal ASCII characters rather than interpreted as candidate navigation or Chinese punctuation.
    if (state == EngineState::TempEnglish) {
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
        if (MapPrintableAsciiChar(vk, shift_down, caps_lock_on).has_value()) {
            return KeyEventKind::PrintableAscii;
        }
        return KeyEventKind::None;
    }

    const bool letter_upper = (shift_down != caps_lock_on);
    if (state == EngineState::Idle && z_temp_english && vk == 'Z' && !letter_upper) {
        return KeyEventKind::TempEnglishGuide;
    }
    if (vk >= 'A' && vk <= 'Z') {
        if ((state == EngineState::English || (state == EngineState::Idle && letter_upper)) && full_width) {
            return KeyEventKind::PrintableAscii;
        }
        return letter_upper ? KeyEventKind::UpperChar : KeyEventKind::CodeChar;
    }
    if (vk == VK_SPACE) {
        if ((state == EngineState::Idle || state == EngineState::English) && full_width) {
            return KeyEventKind::PrintableAscii;
        }
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
    if (vk == VK_PRIOR || (!shift_down && vk == VK_OEM_MINUS && state != EngineState::Idle && state != EngineState::English)) {
        return KeyEventKind::PageUp;
    }
    if (vk == VK_NEXT || (!shift_down && vk == VK_OEM_PLUS && state != EngineState::Idle && state != EngineState::English)) {
        return KeyEventKind::PageDown;
    }
    if (state != EngineState::Idle && state != EngineState::English &&
        ResolveCandidateSelectIndex(vk, shift_down, composing_quick_select).has_value()) {
        return KeyEventKind::DigitSelect;
    }
    if (state != EngineState::English && chinese_punct && MapChinesePunctuation(vk, shift_down).has_value()) {
        return KeyEventKind::ChinesePunct;
    }
    if (full_width && MapFullWidthChar(vk, shift_down, caps_lock_on).has_value()) {
        return KeyEventKind::PrintableAscii;
    }
    // Keep stateless backward-compatible classification when called with default state == Idle (e.g. in static_asserts)
    if (!full_width && state == EngineState::Idle) {
        if (!shift_down && vk == VK_OEM_MINUS) {
            return KeyEventKind::PageUp;
        }
        if (!shift_down && vk == VK_OEM_PLUS) {
            return KeyEventKind::PageDown;
        }
        if (ResolveCandidateSelectIndex(vk, shift_down, composing_quick_select).has_value()) {
            return KeyEventKind::DigitSelect;
        }
    }
    return KeyEventKind::None;
}

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
