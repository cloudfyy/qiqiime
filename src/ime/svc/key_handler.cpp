// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"
#include "ime/tsf/edit_session.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/sound.h"

#include <array>
#include <format>
#include <string>

namespace qiqi {

namespace {

bool IsKeyDown(int vk) noexcept {
    return (::GetKeyState(vk) & 0x8000) != 0;
}

bool IsAnyMouseButtonDown() noexcept {
    return IsKeyDown(VK_LBUTTON) || IsKeyDown(VK_RBUTTON) || IsKeyDown(VK_MBUTTON);
}

ShiftTapModifiers ReadShiftTapModifiers() noexcept {
    return {IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU), IsAnyMouseButtonDown()};
}

bool IsCapsLockOn() noexcept {
    return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

constexpr bool IsShiftVirtualKey(uint32_t vk) noexcept {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

} // namespace

QiQiTextService::KeyEvent QiQiTextService::ReadKeyEvent(WPARAM key, LPARAM flags) noexcept {
    return {
        .vk = gsl::narrow_cast<uint32_t>(key),
        .shift = IsKeyDown(VK_SHIFT),
        .ctrl = IsKeyDown(VK_CONTROL),
        .alt = IsKeyDown(VK_MENU),
        .caps = IsCapsLockOn(),
        .flags = flags,
    };
}

void QiQiTextService::LogShiftTap(std::string_view stage, uint32_t vk, const ITfContext* context,
                                 bool eaten, std::string_view reason) const noexcept {
    if (!log::ShouldLog(log::Level::debug, log::Keyword::Input)) return;
    const char* key = vk == VK_LSHIFT ? "left" : vk == VK_RSHIFT ? "right" :
                      vk == VK_SHIFT ? "generic" : "not-recorded";
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::Input,
        "ShiftTap(service={}, attempt={}, stage={}, key={}, reason={}, eaten={}, candidate={}, elapsed_ms={}, max_ms={}, enabled={}, active={}, resetting={}, preparing={}, has_context={}, current_context={}, chinese={}, composing={}, shift={}, ctrl={}, alt={}, mouse={})",
        service_log_id_, shift_tap_.Sequence(), stage, key, reason, eaten,
        shift_tap_.IsCandidate(), shift_tap_.ElapsedMs(::GetTickCount64()),
        ShiftTapTracker::kMaxDurationMs, config_.shift_toggle_mode, composition_lifecycle_.IsActive(), composition_lifecycle_.IsResettingContext(),
        preparing_input_context_ != nullptr, context != nullptr,
        context && context == composition_lifecycle_.Context().Get(), engine_.IsChineseMode(), engine_.IsComposing(),
        IsKeyDown(VK_SHIFT), IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU), IsAnyMouseButtonDown());
}

void QiQiTextService::TrackShiftTapKeyDown(std::string_view stage, uint32_t vk, ITfContext* context,
                                         LPARAM flags) noexcept {
    const bool is_auto_repeat = (flags & (1LL << 30)) != 0;
    const auto modifiers = ReadShiftTapModifiers();
    const char* reason = nullptr;
    ITfContext* const effective_ctx = context ? context : composition_lifecycle_.Context().Get();
    if (!composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() || preparing_input_context_) {
        reason = !composition_lifecycle_.IsActive() ? "inactive" : composition_lifecycle_.IsResettingContext() ? "resetting" : "preparing";
        shift_tap_.Reset();
    } else if (effective_ctx && IsContextKeyboardDisabled(effective_ctx)) {
        reason = "keyboard-disabled";
        shift_tap_.Reset();
    } else {
        reason = shift_tap_.OnKeyDown(::GetTickCount64(), is_auto_repeat, config_.shift_toggle_mode, modifiers);
    }
    LogShiftTap(stage, vk, context, false, reason);
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::Input,
        "ShiftTapContext(service={}, attempt={}, keyboard_disabled={})",
        service_log_id_, shift_tap_.Sequence(), effective_ctx && IsContextKeyboardDisabled(effective_ctx));
}

bool QiQiTextService::HandleShiftTapKeyUp(std::string_view stage, uint32_t vk, const ITfContext* context,
                                         bool test_only) noexcept {
    if (!IsShiftVirtualKey(vk)) return false;
    if (!shift_tap_.IsCandidate()) {
        LogShiftTap(stage, vk, context, false, "no-candidate");
        if (!test_only) shift_tap_.Reset();
        return false;
    }
    if (!composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() || preparing_input_context_) {
        LogShiftTap(stage, vk, context, false,
                    !composition_lifecycle_.IsActive() ? "inactive" :
                    composition_lifecycle_.IsResettingContext() ? "resetting" : "preparing");
        shift_tap_.Reset();
        return false;
    }
    const auto now = ::GetTickCount64();
    const auto modifiers = ReadShiftTapModifiers();
    const char* reason = shift_tap_.ReleaseReason(now, modifiers);
    LogShiftTap(stage, vk, context, reason == nullptr, reason ? reason : "accepted");
    if (!test_only) {
        shift_tap_.Reset();
    } else if (reason) {
        shift_tap_.Disarm();
    }
    return reason == nullptr;
}

void QiQiTextService::HandleTestKeyDown(
    ITfContext* pic, const KeyEvent& key, gsl::not_null<BOOL*> pfEaten) noexcept {
    const auto [vk, shift, ctrl, alt, caps, flags] = key;
    if (IsShiftVirtualKey(vk)) {
        TrackShiftTapKeyDown("OnTestKeyDown", vk, pic, flags);
        *pfEaten = FALSE;
        return;
    }
    const bool keyboard_disabled = IsContextKeyboardDisabled(pic);
    if (!pic || !composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() || preparing_input_context_ || keyboard_disabled) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("OnTestKeyDown", vk, pic, false,
                        !pic ? "no-context" : !composition_lifecycle_.IsActive() ? "inactive" : composition_lifecycle_.IsResettingContext() ? "resetting" :
                        preparing_input_context_ ? "preparing" : "keyboard-disabled");
        }
        shift_tap_.Reset();
        *pfEaten = FALSE;
        return;
    }
    if (shift_tap_.IsCandidate()) LogShiftTap("OnTestKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();

    const bool eat_key = pic == composition_lifecycle_.Context().Get()
        ? engine_.ShouldEatKey(vk, shift, ctrl, alt, caps)
        : engine_.ShouldEatKeyAfterReset(vk, shift, ctrl, alt, caps);
    *pfEaten = eat_key ? TRUE : FALSE;
}

void QiQiTextService::HandleKeyDown(
    ITfContext* pic, const KeyEvent& key, gsl::not_null<BOOL*> pfEaten) {
    const auto [vk, shift, ctrl, alt, caps, flags] = key;
    if (IsShiftVirtualKey(vk)) {
        ITfContext* const shift_ctx = pic ? pic : composition_lifecycle_.Context().Get();
        if (shift_ctx) {
            const AccessPolicy shift_policy = EvaluateContextAccess(shift_ctx, true);
            if (!shift_policy.can_input) {
                LogShiftTap("OnKeyDown", vk, pic, false, "keyboard-disabled");
                shift_tap_.Reset();
                *pfEaten = FALSE;
                return;
            }
        }
        // Hosts may dispatch directly, without the test callback. Do not bind a context for Shift.
        TrackShiftTapKeyDown("OnKeyDown", vk, pic, flags);
        *pfEaten = FALSE;
        return;
    }
    const AccessPolicy initial_policy = EvaluateContextAccess(pic, true);
    if (shift_tap_.IsCandidate())
        LogShiftTap("OnKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();
    if (!initial_policy.can_input) {
        *pfEaten = FALSE;
        return;
    }
    if (!PrepareInputContext(pic)) {
        *pfEaten = FALSE;
        return;
    }
    const AccessPolicy active_policy = EvaluateContextAccess(pic, true);
    if (!active_policy.can_input) {
        *pfEaten = FALSE;
        return;
    }

    if (!engine_.IsComposing()) {
        std::ignore = engine_.SynchronizeCrossProcessChanges(sync::InputBoundary::NewInput);
    }

    if (vk == VK_CAPITAL && engine_.IsComposing()) {
        // Industry standard: pressing CapsLock mid-composition commits the raw English code
        // while letting VK_CAPITAL pass through (*pfEaten = FALSE) so Windows toggles CapsLock state.
        *pfEaten = FALSE;
        const KeyProcessResult result = engine_.ProcessKey(
            vk, shift, ctrl, alt, caps, active_policy.can_learn, true);
        if (pic && result.action != KeyActionType::PassThrough) {
            ApplyEngineResult(pic, result);
        }
        return;
    }

    if (!engine_.ShouldEatKey(vk, shift, ctrl, alt, caps)) {
        *pfEaten = FALSE;
        return;
    }

    *pfEaten = TRUE;
    const KeyProcessResult result = engine_.ProcessKey(
        vk, shift, ctrl, alt, caps, active_policy.can_learn, true);
    if (result.empty_4code_warning) {
        PlayImeSound(SoundEffectKind::EmptyCodeBeep);
    } else if (result.dup_4code_warning) {
        PlayImeSound(SoundEffectKind::DupCodeBeep);
    } else if (result.trigger_key_sound) {
        PlayImeSound(SoundEffectKind::KeyClick);
    }
    if (pic && result.action != KeyActionType::PassThrough) {
        ApplyEngineResult(pic, result);
    }
}

void QiQiTextService::HandleTestKeyUp(
    ITfContext* pic, uint32_t vk, gsl::not_null<BOOL*> pfEaten) noexcept {
    ITfContext* const effective_ctx = pic ? pic : composition_lifecycle_.Context().Get();
    if (IsShiftVirtualKey(vk) && effective_ctx && IsContextKeyboardDisabled(effective_ctx)) {
        LogShiftTap("OnTestKeyUp", vk, pic, false, "keyboard-disabled");
        shift_tap_.Reset();
        *pfEaten = FALSE;
        return;
    }
    *pfEaten = HandleShiftTapKeyUp("OnTestKeyUp", vk, pic, true) ? TRUE : FALSE;
}

void QiQiTextService::HandleKeyUp(
    ITfContext* pic, uint32_t vk, gsl::not_null<BOOL*> pfEaten) {
    if (IsShiftVirtualKey(vk)) {
        if (ITfContext* const effective_ctx = pic ? pic : composition_lifecycle_.Context().Get()) {
            const AccessPolicy policy = EvaluateContextAccess(effective_ctx, true);
            if (!policy.can_input) {
                LogShiftTap("OnKeyUp", vk, pic, false, "keyboard-disabled");
                shift_tap_.Reset();
                *pfEaten = FALSE;
                return;
            }
        }
    }
    if (HandleShiftTapKeyUp("OnKeyUp", vk, pic, false)) {
        *pfEaten = TRUE;
        ToggleModeAndFinishComposition(pic);
        LogShiftTap("OnKeyUp", vk, pic, true, "toggle-complete");
        return;
    }
    *pfEaten = FALSE;
}

void QiQiTextService::HandlePreservedKey(
    ITfContext* pic, REFGUID rguid, gsl::not_null<BOOL*> pfEaten) {
    if (!composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() ||
        preparing_input_context_) {
        *pfEaten = FALSE;
        return;
    }
    ITfContext* const target = pic ? pic : composition_lifecycle_.Context().Get();
    AccessPolicy policy{.can_input = true, .can_learn = true, .should_reset_active = false};
    if (target) {
        policy = EvaluateContextAccess(target, true);
        if (!policy.can_input) {
            *pfEaten = FALSE;
            return;
        }
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) {
        *pfEaten = TRUE;
        ToggleModeAndFinishComposition(target);
        return;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyToggleFullWidth)) {
        *pfEaten = TRUE;
        engine_.ToggleFullWidthMode();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Full-width mode toggled: {}", engine_.IsFullWidthMode() ? "FullWidth" : "HalfWidth");
        return;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyTogglePunctuation)) {
        *pfEaten = TRUE;
        engine_.ToggleChinesePunctuation();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Punctuation mode toggled: {}", engine_.IsChinesePunctuation() ? "Chinese" : "English");
        return;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyAddUserWord)) {
        if (!policy.can_learn) {
            *pfEaten = FALSE;
            return;
        }
        *pfEaten = TRUE;
        if (const ScopedClipboard clip{}; clip.IsOpen()) {
            if (const HANDLE hdata = ::GetClipboardData(CF_UNICODETEXT)) {
                if (const ScopedGlobalLock<wchar_t> locked(hdata); locked) {
                    const std::wstring phrase(locked.Get());
                    if (phrase.size() >= 2 && phrase.size() <= 16 &&
                        engine_.AddUserPhrase(phrase, GetDefaultUserDictPath())) {
                        config_registry_sequence_ = QueryRegistryConfigSequence();
                        PlayImeSound(SoundEffectKind::KeyClick);
                        NotifyGlobalConfigChanged();
                    } else {
                        QIQI_LOG_DEBUG("Clipboard phrase skipped (length={} outside [2..16] or encoding failed)",
                                       phrase.size());
                    }
                } else {
                    QIQI_LOG_WARN("OnPreservedKey(AddUserWord): GlobalLock failed (error={})", locked.Error());
                }
            } else {
                QIQI_LOG_DEBUG("OnPreservedKey(AddUserWord): clipboard does not contain CF_UNICODETEXT");
            }
        } else {
            QIQI_LOG_WARN("OnPreservedKey(AddUserWord): OpenClipboard failed (error={})", clip.Error());
        }
        return;
    }
    *pfEaten = FALSE;
}

void QiQiTextService::SetChineseModeAndFinishComposition(ITfContext* context, bool enabled) {
    if (enabled == engine_.IsChineseMode()) return;
    const ComPtr<ITfContext> target(context ? context : composition_lifecycle_.Context().Get());
    const AccessPolicy policy = target ? EvaluateContextAccess(target.Get(), false) : AccessPolicy{};
    KeyProcessResult result{};
    if (engine_.IsComposing()) {
        if (target && policy.can_input && PrepareInputContext(target.Get()) && engine_.IsComposing()) {
            result.action = config_.commit_on_english_switch ? KeyActionType::Commit : KeyActionType::Cancel;
            if (config_.commit_on_english_switch) {
                result.committed_text = engine_.RawCode();
            }
        } else {
            ResetCompositionAndUI();
        }
    } else if (target && policy.can_input && composition_lifecycle_.Context() &&
               target.Get() != composition_lifecycle_.Context().Get()) {
        std::ignore = PrepareInputContext(target.Get());
    }
    // Host edits can reenter mode synchronization and key prediction.
    engine_.SetChineseMode(enabled);
    if (result.action != KeyActionType::PassThrough && policy.can_input) {
        ApplyEngineResult(target.Get(), result);
    }
}

void QiQiTextService::ToggleModeAndFinishComposition(ITfContext* context) {
    SetChineseModeAndFinishComposition(context, !engine_.IsChineseMode());
    SyncTsfCompartments();
    QIQI_LOG_INFO("Input mode toggled: {}", engine_.IsChineseMode() ? "Chinese" : "English");
}

} // namespace qiqi
