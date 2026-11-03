#pragma once
#include <gsl/gsl>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "ime/fsm/types.h"

namespace qiqi {

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
    if (!shift_down && vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return gsl::narrow_cast<char>('0' + (vk - VK_NUMPAD0));
    }
    switch (vk) {
        case VK_DECIMAL:    return shift_down ? std::nullopt : std::optional<char>('.');
        case VK_ADD:        return shift_down ? std::nullopt : std::optional<char>('+');
        case VK_SUBTRACT:   return shift_down ? std::nullopt : std::optional<char>('-');
        case VK_MULTIPLY:   return shift_down ? std::nullopt : std::optional<char>('*');
        case VK_DIVIDE:     return shift_down ? std::nullopt : std::optional<char>('/');
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

// Resolve 0-based page candidate index for digit keys ('1'..'9' / VK_NUMPAD1..9 -> 0..8)
// and optional ';' (VK_OEM_1 -> 1) / ''' (VK_OEM_7 -> 2) quick-select keys.
constexpr std::optional<size_t> ResolveCandidateSelectIndex(
    uint32_t vk, bool shift_down, bool quick_select_enabled = false) noexcept {
    if (shift_down) {
        return std::nullopt;
    }
    if (vk >= '1' && vk <= '9') {
        return static_cast<size_t>(vk - '1');
    }
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) {
        return static_cast<size_t>(vk - VK_NUMPAD1);
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

constexpr bool IsNumpadNonSelectComposingKey(uint32_t vk) noexcept {
    return vk == VK_NUMPAD0 || vk == VK_DECIMAL || vk == VK_ADD ||
           vk == VK_SUBTRACT || vk == VK_MULTIPLY || vk == VK_DIVIDE;
}

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
    bool caps_lock_on = false,
    bool up_down_page_turn = false) noexcept {
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
        switch (vk) {
            case VK_SPACE:  return KeyEventKind::Space;
            case VK_RETURN: return KeyEventKind::Enter;
            case VK_BACK:   return KeyEventKind::Backspace;
            case VK_ESCAPE: return KeyEventKind::Escape;
            default:
                return MapPrintableAsciiChar(vk, shift_down, caps_lock_on).has_value()
                    ? KeyEventKind::PrintableAscii
                    : KeyEventKind::None;
        }
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

    switch (vk) {
        case VK_SPACE:
            return ((state == EngineState::Idle || state == EngineState::English) && full_width)
                ? KeyEventKind::PrintableAscii
                : KeyEventKind::Space;
        case VK_RETURN:
            return KeyEventKind::Enter;
        case VK_BACK:
            return KeyEventKind::Backspace;
        case VK_ESCAPE:
            return KeyEventKind::Escape;
        case VK_LEFT:
            return KeyEventKind::CandidatePrev;
        case VK_RIGHT:
            return KeyEventKind::CandidateNext;
        case VK_UP:
            return up_down_page_turn ? KeyEventKind::PageUp : KeyEventKind::CandidatePrev;
        case VK_DOWN:
            return up_down_page_turn ? KeyEventKind::PageDown : KeyEventKind::CandidateNext;
        case VK_PRIOR:
            return KeyEventKind::PageUp;
        case VK_NEXT:
            return KeyEventKind::PageDown;
        default:
            break;
    }

    const bool is_wubi_composing = IsWubiComposingState(state);
    if (is_wubi_composing && !shift_down) {
        if (vk == VK_OEM_MINUS) {
            return KeyEventKind::PageUp;
        }
        if (vk == VK_OEM_PLUS) {
            return KeyEventKind::PageDown;
        }
        if (ResolveCandidateSelectIndex(vk, false, composing_quick_select).has_value()) {
            return KeyEventKind::DigitSelect;
        }
    }
    if (state != EngineState::English && chinese_punct && MapChinesePunctuation(vk, shift_down).has_value()) {
        return KeyEventKind::ChinesePunct;
    }
    if (full_width && MapFullWidthChar(vk, shift_down, caps_lock_on).has_value()) {
        return KeyEventKind::PrintableAscii;
    }
    // While composing in Half-Width mode, swallow VK_NUMPAD0 and Numpad operators (+ - * / .) so they do not
    // leak into the active TSF composition range as literal characters; they become no-op state updates.
    if (is_wubi_composing && !shift_down && IsNumpadNonSelectComposingKey(vk)) {
        return KeyEventKind::DigitSelect;
    }
    return KeyEventKind::None;
}

} // namespace qiqi
