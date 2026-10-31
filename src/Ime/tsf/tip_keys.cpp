#include "Ime/tsf/tip_service.h"
#include "Ime/tsf/edit_session.h"
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

constexpr ULONGLONG kMaxShiftTapDurationMs = 350;

bool QueryCompartmentNonZero(IUnknown* unk, REFGUID guid) noexcept {
    if (!unk) {
        return false;
    }
    ComPtr<ITfCompartmentMgr> comp_mgr;
    if (FAILED(unk->QueryInterface(IID_PPV_ARGS(&comp_mgr))) || !comp_mgr) {
        return false;
    }
    ComPtr<ITfCompartment> comp;
    if (FAILED(comp_mgr->GetCompartment(guid, &comp)) || !comp) {
        return false;
    }
    return ReadCompartmentInt4(comp.Get(), 0) != 0;
}

bool IsValidShiftTapRelease(ULONGLONG press_tick) noexcept {
    return (::GetTickCount64() - press_tick <= kMaxShiftTapDurationMs) &&
           !IsAnyMouseButtonDown() &&
           !IsKeyDown(VK_CONTROL) &&
           !IsKeyDown(VK_MENU);
}

bool IsCapsLockOn() noexcept {
    return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

constexpr const char* KeyActionTypeToString(KeyActionType action) noexcept {
    switch (action) {
        case KeyActionType::PassThrough: return "PassThrough";
        case KeyActionType::UpdateState: return "UpdateState";
        case KeyActionType::Commit:      return "Commit";
        case KeyActionType::Cancel:      return "Cancel";
        default:                         return "Unknown";
    }
}

std::string FormatVirtualKey(uint32_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::format("'{}'(0x{:02X})", static_cast<char>(vk), vk);
    }
    switch (vk) {
        case VK_SPACE:      return "VK_SPACE(0x20)";
        case VK_RETURN:     return "VK_RETURN(0x0D)";
        case VK_BACK:       return "VK_BACK(0x08)";
        case VK_ESCAPE:     return "VK_ESCAPE(0x1B)";
        case VK_SHIFT:      return "VK_SHIFT(0x10)";
        case VK_CAPITAL:    return "VK_CAPITAL(0x14)";
        case VK_LEFT:       return "VK_LEFT(0x25)";
        case VK_UP:         return "VK_UP(0x26)";
        case VK_RIGHT:      return "VK_RIGHT(0x27)";
        case VK_DOWN:       return "VK_DOWN(0x28)";
        case VK_OEM_1:      return "VK_OEM_1(;/:)";
        case VK_OEM_7:      return "VK_OEM_7('/\")";
        case VK_OEM_MINUS:  return "VK_OEM_MINUS(-/_)";
        case VK_OEM_PLUS:   return "VK_OEM_PLUS(=/+)";
        case VK_OEM_COMMA:  return "VK_OEM_COMMA(,/<)";
        case VK_OEM_PERIOD: return "VK_OEM_PERIOD(./>)";
        default:            return std::format("0x{:02X}", vk);
    }
}

void LogKeyTraceIfEnabled(
    bool enabled,
    uint32_t vk,
    bool shift,
    bool caps,
    bool ctrl,
    bool alt,
    const KeyProcessResult& result,
    size_t total_candidates,
    PrimaryDict primary_dict,
    size_t mapped_dicts) {
    if (!enabled) {
        return;
    }
    std::string cand_preview;
    for (size_t i = 0; i < result.page_candidates.size(); ++i) {
        if (i > 0) {
            cand_preview += ", ";
        }
        cand_preview += std::format("{}:{}", i + 1, WideToUtf8(gsl::at(result.page_candidates, i)));
    }
    QIQI_LOG_INFO(
        "[KeyTrace] vk={} shift={} caps={} ctrl={} alt={} -> action={} raw='{}' commit='{}' "
        "total_cands={} page={}/{} page_cands=[{}] dict={} mapped_dicts={}",
        FormatVirtualKey(vk),
        shift,
        caps,
        ctrl,
        alt,
        KeyActionTypeToString(result.action),
        WideToUtf8(result.composition_text),
        WideToUtf8(result.committed_text),
        total_candidates,
        result.current_page + (result.total_pages > 0 ? 1 : 0),
        result.total_pages,
        cand_preview,
        PrimaryDictToString(primary_dict),
        mapped_dicts);
}

} // namespace

// ITfKeyEventSink
STDMETHODIMP QiQiTextService::OnSetFocus(BOOL fForeground) {
    HandleFocusChanged(fForeground != FALSE);
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
    if (IsContextKeyboardDisabled(pic)) {
        shift_tap_candidate_ = false;
        shift_press_tick_ = 0;
        *pfEaten = FALSE;
        return S_OK;
    }
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    if (vk == VK_SHIFT) {
        const bool is_auto_repeat = (lParam & (1LL << 30)) != 0;
        if (!is_auto_repeat) {
            shift_tap_candidate_ = config_.shift_toggle_mode &&
                                   !IsKeyDown(VK_CONTROL) &&
                                   !IsKeyDown(VK_MENU) &&
                                   !IsAnyMouseButtonDown();
            shift_press_tick_ = shift_tap_candidate_ ? ::GetTickCount64() : 0;
        } else if (::GetTickCount64() - shift_press_tick_ > kMaxShiftTapDurationMs || IsAnyMouseButtonDown()) {
            shift_tap_candidate_ = false;
        }
        *pfEaten = FALSE;
        return S_OK;
    }
    shift_tap_candidate_ = false;
    shift_press_tick_ = 0;

    *pfEaten = engine_.ShouldEatKey(
                   vk,
                   IsKeyDown(VK_SHIFT),
                   IsKeyDown(VK_CONTROL),
                   IsKeyDown(VK_MENU),
                   IsCapsLockOn())
                   ? TRUE
                   : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (IsContextKeyboardDisabled(pic)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool caps = IsCapsLockOn();
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);

    if (vk == VK_CAPITAL && engine_.IsComposing()) {
        // Industry standard: pressing CapsLock mid-composition commits the raw English code
        // while letting VK_CAPITAL pass through (*pfEaten = FALSE) so Windows toggles CapsLock state.
        *pfEaten = FALSE;
        const KeyProcessResult result = engine_.ProcessKey(vk, shift, ctrl, alt, caps);
        LogKeyTraceIfEnabled(
            config_.log_keystrokes,
            vk,
            shift,
            caps,
            ctrl,
            alt,
            result,
            engine_.AllCandidates().size(),
            config_.primary_dict,
            engine_.MappedDictionaryCount());
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
    LogKeyTraceIfEnabled(
        config_.log_keystrokes,
        vk,
        shift,
        caps,
        ctrl,
        alt,
        result,
        engine_.AllCandidates().size(),
        config_.primary_dict,
        engine_.MappedDictionaryCount());
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

STDMETHODIMP QiQiTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (wParam == VK_SHIFT && shift_tap_candidate_) {
        if (IsValidShiftTapRelease(shift_press_tick_)) {
            *pfEaten = TRUE;
            return S_OK;
        }
        shift_tap_candidate_ = false;
    }
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (wParam == VK_SHIFT && shift_tap_candidate_) {
        shift_tap_candidate_ = false;
        const bool valid_tap = IsValidShiftTapRelease(shift_press_tick_);
        shift_press_tick_ = 0;
        if (valid_tap) {
            *pfEaten = TRUE;
            ToggleModeAndCancelComposition(pic);
            return S_OK;
        }
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
    if (engine_.IsComposing() && context) {
        KeyProcessResult cancel_res{};
        cancel_res.action = KeyActionType::Cancel;
        ApplyEngineResult(context, cancel_res);
    }
    engine_.ToggleChineseMode();
    SyncTsfCompartments();
}

} // namespace qiqi
