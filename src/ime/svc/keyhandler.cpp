// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"
#include "common/configclient.h"
#include "ime/tsf/editsession.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/sound.h"
#include "common/uiutil.h"

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

ShiftTapModifiers ReadShiftTapModifiers(bool caps_lock_switch_enabled) noexcept {
    return {IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU), IsAnyMouseButtonDown(),
        caps_lock_switch_enabled && IsKeyDown(VK_CAPITAL)};
}

bool IsCapsLockOn() noexcept {
    return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

constexpr bool IsShiftVirtualKey(uint32_t vk) noexcept {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

constexpr bool IsKeyRepeat(LPARAM flags) noexcept {
    return (flags & (1LL << 30)) != 0;
}

} // namespace

QiQiTextService::KeyEvent QiQiTextService::ReadKeyEvent(WPARAM key, LPARAM flags) noexcept {
    return {
        .vk = gsl::narrow_cast<uint32_t>(key),
        .shift = IsKeyDown(VK_SHIFT),
        .ctrl = IsKeyDown(VK_CONTROL),
        .alt = IsKeyDown(VK_MENU),
        .win = key == VK_CAPITAL && (IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN)),
        .caps = IsCapsLockOn(),
        .flags = flags,
    };
}

bool QiQiTextService::ShouldEatCapsLockDown(const KeyEvent& key) const noexcept {
    if (IsKeyRepeat(key.flags)) return caps_lock_press_handled_;
    return config_.caps_lock_toggle_mode && !key.shift && !key.ctrl && !key.alt && !key.win;
}

bool QiQiTextService::ShouldEatCapsLockUp(ITfContext* context) const noexcept {
    if (!caps_lock_press_handled_ || IsContextKeyboardDisabled(context)) return false;
    return caps_lock_press_handled_;
}

bool QiQiTextService::EffectiveCapsLock(bool system_locked, bool use_current_composition) const noexcept {
    return system_locked && (!config_.caps_lock_toggle_mode || !engine_.IsChineseMode() ||
        (use_current_composition && engine_.IsTempEnglishMode()));
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
    const auto modifiers = ReadShiftTapModifiers(config_.caps_lock_toggle_mode || caps_lock_press_handled_);
    const char* reason = nullptr;
    ITfContext* const effective_ctx = context ? context : composition_lifecycle_.Context().Get();
    if (const auto blocked = GetInputBlockReason(); blocked != InputBlockReason::None) {
        reason = InputBlockReasonToString(blocked).data();
        shift_tap_.Reset();
    } else if (effective_ctx && IsContextKeyboardDisabled(effective_ctx)) {
        reason = "keyboard-disabled";
        shift_tap_.Reset();
    } else {
        reason = shift_tap_.OnKeyDown(::GetTickCount64(), IsKeyRepeat(flags), config_.shift_toggle_mode, modifiers);
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
    if (const auto blocked = GetInputBlockReason(); blocked != InputBlockReason::None) {
        LogShiftTap(stage, vk, context, false, InputBlockReasonToString(blocked));
        shift_tap_.Reset();
        return false;
    }
    const auto now = ::GetTickCount64();
    const auto modifiers = ReadShiftTapModifiers(config_.caps_lock_toggle_mode || caps_lock_press_handled_);
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
    const uint32_t vk = key.vk;
    if (IsShiftVirtualKey(vk)) {
        TrackShiftTapKeyDown("OnTestKeyDown", vk, pic, key.flags);
        *pfEaten = FALSE;
        return;
    }
    const bool keyboard_disabled = IsContextKeyboardDisabled(pic);
    // Context queries can reenter; read local state after the host returns.
    auto blocked = pic ? GetInputBlockReason() : InputBlockReason::NoContext;
    if (blocked == InputBlockReason::None && keyboard_disabled) {
        blocked = InputBlockReason::KeyboardDisabled;
    }
    if (blocked != InputBlockReason::None) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("OnTestKeyDown", vk, pic, false, InputBlockReasonToString(blocked));
        }
        shift_tap_.Reset();
        *pfEaten = FALSE;
        return;
    }
    if (shift_tap_.IsCandidate()) LogShiftTap("OnTestKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();

    if (vk == VK_CAPITAL && (config_.caps_lock_toggle_mode || caps_lock_press_handled_)) {
        *pfEaten = ShouldEatCapsLockDown(key) ? TRUE : FALSE;
        return;
    }

    const bool same_context = pic == composition_lifecycle_.Context().Get();
    const bool caps = EffectiveCapsLock(key.caps, same_context);
    const bool eat_key = same_context
        ? engine_.ShouldEatKey(vk, key.shift, key.ctrl, key.alt, caps)
        : engine_.ShouldEatKeyAfterReset(vk, key.shift, key.ctrl, key.alt, caps);
    *pfEaten = eat_key ? TRUE : FALSE;
}

bool QiQiTextService::CheckShiftContextAccess(
    ITfContext* context, std::string_view stage, uint32_t vk) {
    ITfContext* const effective_ctx = context ? context : composition_lifecycle_.Context().Get();
    if (effective_ctx) {
        const AccessPolicy policy = EvaluateContextAccess(effective_ctx, true);
        if (!policy.can_input) {
            LogShiftTap(stage, vk, context, false, "keyboard-disabled");
            shift_tap_.Reset();
            return false;
        }
    }
    return true;
}

void QiQiTextService::HandleKeyDown(
    ITfContext* pic, const KeyEvent& key, gsl::not_null<BOOL*> pfEaten) {
    const uint32_t vk = key.vk;
    if (IsShiftVirtualKey(vk)) {
        if (CheckShiftContextAccess(pic, "OnKeyDown", vk)) {
            // Hosts may dispatch directly, without the test callback. Do not bind a context for Shift.
            TrackShiftTapKeyDown("OnKeyDown", vk, pic, key.flags);
        }
        *pfEaten = FALSE;
        return;
    }
    const AccessPolicy initial_policy = EvaluateContextAccess(pic, true);
    if (shift_tap_.IsCandidate())
        LogShiftTap("OnKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();
    if (vk == VK_CAPITAL) {
        const bool eat_key = initial_policy.can_input && ShouldEatCapsLockDown(key);
        caps_lock_press_handled_ = eat_key;
        if (eat_key || config_.caps_lock_toggle_mode) {
            *pfEaten = eat_key ? TRUE : FALSE;
            if (eat_key && !IsKeyRepeat(key.flags)) ToggleModeAndFinishComposition(pic);
            return;
        }
    }
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

    const bool caps = EffectiveCapsLock(key.caps, true);
    if (vk == VK_CAPITAL && engine_.IsComposing()) {
        // Industry standard: pressing CapsLock mid-composition commits the raw English code
        // while letting VK_CAPITAL pass through (*pfEaten = FALSE) so Windows toggles CapsLock state.
        *pfEaten = FALSE;
        const KeyProcessResult result = engine_.ProcessKey(
            vk, key.shift, key.ctrl, key.alt, caps, active_policy.can_learn, true);
        if (pic && result.action != KeyActionType::PassThrough) {
            ApplyEngineResult(pic, result);
        }
        return;
    }

    if (!engine_.ShouldEatKey(vk, key.shift, key.ctrl, key.alt, caps)) {
        *pfEaten = FALSE;
        return;
    }

    *pfEaten = TRUE;
    const KeyProcessResult result = engine_.ProcessKey(
        vk, key.shift, key.ctrl, key.alt, caps, active_policy.can_learn, true);
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
    if (vk == VK_CAPITAL) {
        *pfEaten = ShouldEatCapsLockUp(pic) ? TRUE : FALSE;
        return;
    }
    ITfContext* const effective_ctx = pic ? pic : composition_lifecycle_.Context().Get();
    if (IsShiftVirtualKey(vk) && effective_ctx && IsContextKeyboardDisabled(effective_ctx)) {
        LogShiftTap("OnTestKeyUp", vk, pic, false, "keyboard-disabled");
        shift_tap_.Reset();
        *pfEaten = FALSE;
        return;
    }
    *pfEaten = HandleShiftTapKeyUp("OnTestKeyUp", vk, pic, true) ? TRUE : FALSE;
    RefreshCandidateLayoutOnKeyRelease(pic, vk);
}

void QiQiTextService::HandleKeyUp(
    ITfContext* pic, uint32_t vk, gsl::not_null<BOOL*> pfEaten) {
    if (vk == VK_CAPITAL) {
        *pfEaten = ShouldEatCapsLockUp(pic) ? TRUE : FALSE;
        caps_lock_press_handled_ = false;
        return;
    }
    if (IsShiftVirtualKey(vk) && !CheckShiftContextAccess(pic, "OnKeyUp", vk)) {
        *pfEaten = FALSE;
        return;
    }
    if (HandleShiftTapKeyUp("OnKeyUp", vk, pic, false)) {
        *pfEaten = TRUE;
        ToggleModeAndFinishComposition(pic);
        LogShiftTap("OnKeyUp", vk, pic, true, "toggle-complete");
        return;
    }
    *pfEaten = FALSE;
    RefreshCandidateLayoutOnKeyRelease(pic, vk);
}

void QiQiTextService::HandlePreservedKey(
    ITfContext* pic, REFGUID rguid, gsl::not_null<BOOL*> pfEaten) {
    if (GetInputBlockReason() != InputBlockReason::None) {
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
        std::array<wchar_t, 16> phrase{};
        size_t length = 0;
        {
            const ScopedClipboard clip;
            if (!clip) {
                QIQI_LOG_WARN("OnPreservedKey(AddUserWord): OpenClipboard failed (error={})", clip.Error());
                return;
            }
            const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
            if (!data) {
                QIQI_LOG_DEBUG("OnPreservedKey(AddUserWord): clipboard does not contain CF_UNICODETEXT");
                return;
            }
            const HRESULT read = ReadClipboardUnicodeBuffer(data, phrase, length);
            if (!log::CheckHResult(read, "OnPreservedKey(AddUserWord)::ReadClipboardUnicodeBuffer",
                    read == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ? log::Level::debug : log::Level::warn))
                return;
        }
        if (length >= 2 && engine_.AddUserPhrase({phrase.data(), length}, GetDefaultUserDictPath())) {
            config_registry_sequence_ = HasConfigRegistrySubKeyOverride()
                ? QueryRegistryConfigSequence() : config::CachedRevision();
            PlayImeSound(SoundEffectKind::KeyClick);
            NotifyGlobalConfigChanged();
        } else {
            QIQI_LOG_DEBUG("Clipboard phrase skipped (length={} below 2 or encoding failed)", length);
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
