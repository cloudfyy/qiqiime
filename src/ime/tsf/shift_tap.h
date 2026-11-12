// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdint>

namespace qiqi {

struct ShiftTapModifiers {
    bool ctrl = false;
    bool alt = false;
    bool mouse = false;
};

class ShiftTapTracker {
public:
    static constexpr uint64_t kMaxDurationMs = 350;

    constexpr bool IsCandidate() const noexcept { return candidate_; }
    constexpr uint64_t Sequence() const noexcept { return sequence_; }
    constexpr uint64_t ElapsedMs(uint64_t now) const noexcept {
        return press_tick_ ? now - press_tick_ : 0;
    }

    constexpr void Reset() noexcept {
        candidate_ = false;
        press_tick_ = 0;
    }

    // Retain the original press time for diagnostics until the actual keyup or reset.
    constexpr void Disarm() noexcept { candidate_ = false; }

    constexpr const char* OnKeyDown(
        uint64_t now, bool is_auto_repeat, bool enabled, ShiftTapModifiers modifiers) noexcept {
        if (!is_auto_repeat && !candidate_) {
            ++sequence_;
            candidate_ = enabled && !modifiers.ctrl && !modifiers.alt && !modifiers.mouse;
            press_tick_ = candidate_ ? now : 0;
            return !enabled ? "disabled" : modifiers.ctrl ? "ctrl" :
                   modifiers.alt ? "alt" : modifiers.mouse ? "mouse-button" : "armed";
        }
        if (now - press_tick_ > kMaxDurationMs || modifiers.mouse) {
            Disarm();
            return modifiers.mouse ? "repeat-mouse-button" : "repeat-timeout";
        }
        return is_auto_repeat ? "repeat-preserved" : "press-preserved";
    }

    constexpr const char* ReleaseReason(uint64_t now, ShiftTapModifiers modifiers) const noexcept {
        if (!candidate_) return "no-candidate";
        if (now - press_tick_ > kMaxDurationMs) return "timeout";
        if (modifiers.mouse) return "mouse-button";
        if (modifiers.ctrl) return "ctrl";
        if (modifiers.alt) return "alt";
        return nullptr;
    }

private:
    bool candidate_ = false;
    uint64_t press_tick_ = 0;
    uint64_t sequence_ = 0;
};

} // namespace qiqi
