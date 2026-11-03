#include "ime/tsf/tip_service.h"
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

bool QueryCompartmentNonZero(IUnknown* unk, REFGUID guid) noexcept {
    if (!unk) {
        return false;
    }
    ComPtr<ITfCompartmentMgr> comp_mgr;
#pragma warning(suppress : 26447) // TSF COM calls report failures through HRESULT.
    if (!log::CheckHResult(unk->QueryInterface(IID_PPV_ARGS(&comp_mgr)),
                          "QueryCompartmentNonZero::QueryInterface(ITfCompartmentMgr)", log::Level::debug) || !comp_mgr) {
        return false;
    }
    ComPtr<ITfCompartment> comp;
#pragma warning(suppress : 26447) // TSF COM calls report failures through HRESULT.
    if (!log::CheckHResult(comp_mgr->GetCompartment(guid, &comp),
                          "QueryCompartmentNonZero::GetCompartment", log::Level::debug) || !comp) {
        return false;
    }
    return ReadCompartmentInt4(comp.Get(), 0) != 0;
}

bool IsCapsLockOn() noexcept {
    return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

constexpr bool IsShiftVirtualKey(uint32_t vk) noexcept {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

} // namespace

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
        ShiftTapTracker::kMaxDurationMs, config_.shift_toggle_mode, is_active_, resetting_context_,
        preparing_input_context_ != nullptr, context != nullptr,
        context && context == active_context_.Get(), engine_.IsChineseMode(), engine_.IsComposing(),
        IsKeyDown(VK_SHIFT), IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU), IsAnyMouseButtonDown());
}

// ITfKeyEventSink
void QiQiTextService::TrackShiftTapKeyDown(std::string_view stage, uint32_t vk, ITfContext* context,
                                         LPARAM flags) noexcept {
    const bool is_auto_repeat = (flags & (1LL << 30)) != 0;
    const auto modifiers = ReadShiftTapModifiers();
    const char* reason = nullptr;
    if (!is_active_ || resetting_context_ || preparing_input_context_) {
        reason = !is_active_ ? "inactive" : resetting_context_ ? "resetting" : "preparing";
        shift_tap_.Reset();
    } else {
        reason = shift_tap_.OnKeyDown(::GetTickCount64(), is_auto_repeat, config_.shift_toggle_mode, modifiers);
    }
    LogShiftTap(stage, vk, context, false, reason);
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::Input,
        "ShiftTapContext(service={}, attempt={}, keyboard_disabled={})",
        service_log_id_, shift_tap_.Sequence(), IsContextKeyboardDisabled(context));
}

bool QiQiTextService::HandleShiftTapKeyUp(std::string_view stage, uint32_t vk, const ITfContext* context,
                                         bool test_only) noexcept {
    if (!IsShiftVirtualKey(vk)) return false;
    if (!shift_tap_.IsCandidate()) {
        LogShiftTap(stage, vk, context, false, "no-candidate");
        if (!test_only) shift_tap_.Reset();
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

STDMETHODIMP QiQiTextService::OnSetFocus(BOOL fForeground) {
    HandleFocusChanged(fForeground != FALSE, FocusNotificationSource::KeyEvent);
    return S_OK;
}

bool QiQiTextService::IsContextKeyboardDisabled(ITfContext* context) const noexcept {
    if (QueryCompartmentNonZero(context, GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
        QueryCompartmentNonZero(context, GUID_COMPARTMENT_EMPTYCONTEXT)) {
        return true;
    }
    return QueryCompartmentNonZero(thread_mgr_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
           QueryCompartmentNonZero(thread_mgr_.Get(), GUID_COMPARTMENT_EMPTYCONTEXT);
}

STDMETHODIMP QiQiTextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);
    const bool caps = IsCapsLockOn();
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    if (IsShiftVirtualKey(vk)) {
        TrackShiftTapKeyDown("OnTestKeyDown", vk, pic, lParam);
        *pfEaten = FALSE;
        return S_OK;
    }
    const bool keyboard_disabled = IsContextKeyboardDisabled(pic);
    if (!pic || !is_active_ || resetting_context_ || preparing_input_context_ || keyboard_disabled) {
        if (shift_tap_.IsCandidate()) {
            LogShiftTap("OnTestKeyDown", vk, pic, false,
                        !pic ? "no-context" : !is_active_ ? "inactive" : resetting_context_ ? "resetting" :
                        preparing_input_context_ ? "preparing" : "keyboard-disabled");
        }
        shift_tap_.Reset();
        *pfEaten = FALSE;
        return S_OK;
    }
    if (shift_tap_.IsCandidate()) LogShiftTap("OnTestKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();

    const bool eat_key = pic == active_context_.Get()
        ? engine_.ShouldEatKey(vk, shift, ctrl, alt, caps)
        : engine_.ShouldEatKeyAfterReset(vk, shift, ctrl, alt, caps);
    *pfEaten = eat_key ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    // Host calls during target preparation can pump a newer keyboard message.
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool caps = IsCapsLockOn();
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    if (IsShiftVirtualKey(vk)) {
        // Hosts may dispatch directly, without the test callback. Do not bind a context for Shift.
        TrackShiftTapKeyDown("OnKeyDown", vk, pic, lParam);
        *pfEaten = FALSE;
        return S_OK;
    }
    if (shift_tap_.IsCandidate()) LogShiftTap("OnKeyDown", 0, pic, false, "other-key-cancelled");
    shift_tap_.Reset();
    if (IsContextKeyboardDisabled(pic)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    if (!PrepareInputContext(pic)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    if (vk == VK_CAPITAL && engine_.IsComposing()) {
        // Industry standard: pressing CapsLock mid-composition commits the raw English code
        // while letting VK_CAPITAL pass through (*pfEaten = FALSE) so Windows toggles CapsLock state.
        *pfEaten = FALSE;
        const KeyProcessResult result = engine_.ProcessKey(vk, shift, ctrl, alt, caps);
        if (pic && result.action != KeyActionType::PassThrough) {
            ApplyEngineResult(pic, result);
        }
        return S_OK;
    }

    if (!engine_.ShouldEatKey(vk, shift, ctrl, alt, caps)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = TRUE;
    const KeyProcessResult result = engine_.ProcessKey(vk, shift, ctrl, alt, caps);
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
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    *pfEaten = HandleShiftTapKeyUp("OnTestKeyUp", gsl::narrow_cast<uint32_t>(wParam), pic, true) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    if (HandleShiftTapKeyUp("OnKeyUp", vk, pic, false)) {
        *pfEaten = TRUE;
        ToggleModeAndCancelComposition(pic);
        LogShiftTap("OnKeyUp", vk, pic, true, "toggle-complete");
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) {
        *pfEaten = TRUE;
        ToggleModeAndCancelComposition(pic);
        return S_OK;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyToggleFullWidth)) {
        *pfEaten = TRUE;
        engine_.ToggleFullWidthMode();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Full-width mode toggled: {}", engine_.IsFullWidthMode() ? "FullWidth" : "HalfWidth");
        return S_OK;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyTogglePunctuation)) {
        *pfEaten = TRUE;
        engine_.ToggleChinesePunctuation();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Punctuation mode toggled: {}", engine_.IsChinesePunctuation() ? "Chinese" : "English");
        return S_OK;
    }
    if (IsEqualGUID(rguid, kGuidPreservedKeyAddUserWord)) {
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
                }
            } else {
                QIQI_LOG_DEBUG("OnPreservedKey(AddUserWord): clipboard does not contain CF_UNICODETEXT");
            }
        } else {
            QIQI_LOG_WARN("OnPreservedKey(AddUserWord): OpenClipboard failed (error={})", ::GetLastError());
        }
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

void QiQiTextService::ToggleModeAndCancelComposition(ITfContext* context) {
    ITfContext* const target = context ? context : active_context_.Get();
    if (engine_.IsComposing()) {
        if (target && PrepareInputContext(target) && engine_.IsComposing()) {
            KeyProcessResult cancel_res{};
            cancel_res.action = KeyActionType::Cancel;
            ApplyEngineResult(target, cancel_res);
        } else {
            ResetCompositionAndUI();
        }
    } else if (target && active_context_ && target != active_context_.Get()) {
        std::ignore = PrepareInputContext(target);
    }
    engine_.ToggleChineseMode();
    SyncTsfCompartments();
    QIQI_LOG_INFO("Input mode toggled: {}", engine_.IsChineseMode() ? "Chinese" : "English");
}

} // namespace qiqi
