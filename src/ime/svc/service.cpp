// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"
#include "ime/tsf/catmgr.h"
#include "ime/tsf/dispattr.h"
#include "ime/comp/target.h"
#include "ime/ui/candsearch.h"
#include "common/fileutil.h"
#include "common/configclient.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/metric.h"
#include "common/version.h"

#include <oleauto.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <string>

namespace qiqi {

namespace {

std::atomic<uint64_t> next_service_log_id{0};

struct PreservedKeyDef {
    const GUID* guid;
    UINT vkey;
    UINT modifiers;
    const wchar_t* desc;
};

constexpr std::array<PreservedKeyDef, 4> kPreservedKeys = {{
    {&kGuidPreservedKeyToggleMode,        VK_SPACE,      TF_MOD_CONTROL, L"切换中英文"},
    {&kGuidPreservedKeyToggleFullWidth,   VK_SPACE,      TF_MOD_SHIFT,   L"切换全半角"},
    {&kGuidPreservedKeyTogglePunctuation, VK_OEM_PERIOD, TF_MOD_CONTROL, L"切换中英文标点"},
    {&kGuidPreservedKeyAddUserWord,       VK_OEM_PLUS,   TF_MOD_CONTROL, L"剪贴板快捷造词"},
}};

} // namespace

QiQiTextService::QiQiTextService(HINSTANCE instance)
    : service_log_id_(++next_service_log_id), composition_lifecycle_(service_log_id_) {
    std::ignore = log::EnsureInitialized();
    uint64_t initial_revision = 0;
    if (HasConfigRegistrySubKeyOverride()) {
        config_ = LoadConfig(instance);
    } else {
        config::Snapshot initial;
        HRESULT read =
            config::CopyCachedSnapshot(
                initial);
        if (read != S_OK &&
            IsProcessAppContainer()) {
            const HRESULT registry_read =
                config::ReadSnapshotFromRegistry(
                    initial);
            QIQI_LOG_CAT_INFO(
                log::Keyword::Config,
                "IME AppContainer startup "
                "registry fallback "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    registry_read));
            read = registry_read;
        }
        if (read == S_OK) {
            config_ = std::move(initial.value);
            initial_revision =
                initial.revision;
        } else {
            config_ = {};
            QIQI_LOG_CAT_INFO(
                log::Keyword::Config,
                "IME started with safe default "
                "config; broker refresh is "
                "pending");
        }
    }
    engine_.ApplyConfig(config_);
    engine_.SetUserDictPath(GetDefaultUserDictPath());
    engine_.SetPhraseFreqPath(GetDefaultPhraseFreqPath());
    std::ignore = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath());
    ApplyDynamicEngineSettings();
    candidate_ui_ = MakeComObject<CandidateUI>(instance);
    candidate_ui_->SetFontPreferences(GetCandidateFontPreferences(config_));
    candidate_ui_->SetSelectCallback([this](size_t index_in_page) {
        if (index_in_page < 9 && composition_lifecycle_.Context() && engine_.IsComposing()) {
            const ComPtr<ITfContext> target = composition_lifecycle_.Context();
            const AccessPolicy policy = EvaluateContextAccess(target.Get(), true);
            if (!policy.can_input || !engine_.IsComposing()) {
                return;
            }
            const KeyProcessResult res = engine_.CommitCandidate(index_in_page, policy.can_learn, true);
            if (res.action != KeyActionType::PassThrough) {
                ApplyEngineResult(target.Get(), res);
            }
        }
    });
    candidate_ui_->SetPageTurnCallback([this](bool next_page) {
        if (composition_lifecycle_.Context() && engine_.IsComposing()) {
            const ComPtr<ITfContext> target = composition_lifecycle_.Context();
            const AccessPolicy policy = EvaluateContextAccess(target.Get(), true);
            if (!policy.can_input || !engine_.IsComposing()) {
                return;
            }
            const KeyProcessResult res = engine_.ProcessKey(
                next_page ? VK_NEXT : VK_PRIOR, false, false, false, false, policy.can_learn, true);
            if (res.action != KeyActionType::PassThrough) {
                ApplyEngineResult(target.Get(), res);
            }
        }
    });
    candidate_ui_->SetActionCallback(
        [this](CandidateUiAction action, size_t index) {
            return HandleCandidateUiAction(action, index);
        });
    candidate_ui_->SetIntegratedKeyDownCallback(
        [this](WPARAM wparam, LPARAM lparam, BOOL* eaten) {
            return HandleIntegratedCandidateKeyDown(
                wparam, lparam, eaten);
        });
    status_bar_ = std::make_unique<StatusBarUI>(instance, [this](LangBarMenuId action_id) {
        HandleLangBarMenuAction(action_id);
    });
    status_bar_->SetPositionChangedCallback([this](POINT new_pos) {
        SaveGlobalStatusBarPosition(new_pos);
    });
    SetUiPresentationEnabled(false);
    LoadExternalDictionaryNextToDll();
    config_registry_sequence_ =
        HasConfigRegistrySubKeyOverride()
            ? QueryRegistryConfigSequence()
            : (initial_revision != 0
                   ? initial_revision
                   : config::CachedRevision());
}

QiQiTextService::~QiQiTextService() noexcept {
    DisconnectPasswordContext();
    if (langbar_btn_) langbar_btn_->Disconnect();
    if (candidate_ui_) {
        candidate_ui_->SetSelectCallback({});
        candidate_ui_->SetPageTurnCallback({});
        candidate_ui_->SetActionCallback({});
        candidate_ui_->SetIntegratedKeyDownCallback({});
        candidate_ui_->ResetIntegrationState();
    }
    std::ignore = DestroyUIWindows();
}

void QiQiTextService::SetUiPresentationEnabled(bool enabled) noexcept {
    if (candidate_ui_) candidate_ui_->SetPresentationEnabled(enabled);
    if (status_bar_) status_bar_->SetPresentationEnabled(enabled);
}

bool QiQiTextService::DestroyUIWindows() noexcept {
    SetUiPresentationEnabled(false);
    const bool candidate_destroyed = !candidate_ui_ || candidate_ui_->DestroyPopupWindow();
    const bool status_destroyed = !status_bar_ || status_bar_->DestroyBarWindow();
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=ui-windows-destroyed, candidate_destroyed={}, status_destroyed={})",
        service_log_id_,
        candidate_destroyed,
        status_destroyed);
    return candidate_destroyed && status_destroyed;
}

STDMETHODIMP QiQiTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP QiQiTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    if (!ptim) {
        return E_INVALIDARG;
    }
    if (activating_ || deactivating_ || composition_lifecycle_.IsResettingContext() || preparing_input_context_) {
        QIQI_LOG_CAT_WARN(
            log::Keyword::Lifecycle,
            "ActivateEx: activation during lifecycle transition rejected (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_UNEXPECTED));
        return E_UNEXPECTED;
    }
    const ComPtr<QiQiTextService> self_holder(this);
    activating_ = true;
    activation_cancelled_ = false;
    profile_deactivated_ = false;
    const auto finish_activation = gsl::finally([this]() noexcept {
        activating_ = false;
        activation_cancelled_ = false;
    });
    composition_lifecycle_.SetActive(false);
    composition_lifecycle_.InvalidateActivation();
    std::ignore = log::EnsureInitialized();
    const GUID act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, act_id);
    const auto restore_act = gsl::finally([this, prev_act]() noexcept {
        current_lifecycle_activity_ = prev_act;
    });
    thread_mgr_ = ptim;
    client_id_ = tid;
    activate_flags_ = dwFlags;

    const std::string host_exe = WideToUtf8(GetModuleFilePath(nullptr).filename().wstring());
    QIQI_LOG_ACT_START(
        log::Level::info,
        log::Keyword::Lifecycle,
        &act_id,
        nullptr,
        "QiQiTextService::ActivateEx(version={}, arch={}, host='{}', client_id={}, flags=0x{:x})",
        version::kVersion,
        version::kArch,
        host_exe,
        tid,
        dwFlags);

    const auto cancel_activation = [this, &act_id]() -> HRESULT {
        QIQI_LOG_ACT_STOP(
            log::Level::warn,
            log::Keyword::Lifecycle,
            &act_id,
            "QiQiTextService::ActivateEx cancelled by Deactivate (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_ABORT));
        activating_ = false;
        std::ignore = log::CheckHResult(Deactivate(), "ActivateEx::Deactivate(cancelled)");
        return E_ABORT;
    };
    if (activation_cancelled_) return cancel_activation();
    const bool sinks_ready = InitThreadMgrEventSink() && !activation_cancelled_ && InitKeyEventSink();
    if (activation_cancelled_) return cancel_activation();
    if (!sinks_ready) {
        QIQI_LOG_ACT_STOP(
            log::Level::err,
            log::Keyword::Lifecycle,
            &act_id,
            "QiQiTextService::ActivateEx failed to initialize TSF event sinks");
        activating_ = false;
        std::ignore = log::CheckHResult(Deactivate(), "ActivateEx::Deactivate(failed)");
        return E_FAIL;
    }
    ITfInputProcessorProfileActivationSink* const profile_notifications = this;
    const bool profile_ready = profile_sink_.Advise(
        thread_mgr_.Get(), IID_ITfInputProcessorProfileActivationSink,
        profile_notifications);
    if (activation_cancelled_) return cancel_activation();
    if (!profile_ready) {
        QIQI_LOG_ERROR("ActivateEx: failed to initialize active language profile notifications");
        activating_ = false;
        std::ignore = log::CheckHResult(Deactivate(), "ActivateEx::Deactivate(profile-sink-failed)");
        return E_FAIL;
    }
    RegisterCompositionDisplayAttributeAtom();
    if (activation_cancelled_) return cancel_activation();
    InitFunctionProvider();
    if (activation_cancelled_) return cancel_activation();
    InitLangBarItem();
    if (activation_cancelled_) return cancel_activation();
    InitCompartmentSinks();
    if (activation_cancelled_) return cancel_activation();
    if (engine_.MappedDictionaryCount() == 0) {
        LoadExternalDictionaryNextToDll();
    }
    if (activation_cancelled_) return cancel_activation();
    composition_lifecycle_.SetActive(true);
    SetUiPresentationEnabled(true);
    if (activation_cancelled_) return cancel_activation();
    SyncTsfCompartments();
    if (activation_cancelled_) return cancel_activation();
    ObserveFocusedPasswordContext();
    if (activation_cancelled_) return cancel_activation();
    QIQI_LOG_ACT_STOP(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &act_id,
        "TsfLifecycle(service={}, stage=activated, activation={}, context={}, client_id={})",
        service_log_id_,
        composition_lifecycle_.Epoch().activation,
        composition_lifecycle_.Epoch().context,
        client_id_);
    return S_OK;
}

STDMETHODIMP QiQiTextService::Deactivate() {
    if (activating_) {
        if (!activation_cancelled_) {
            activation_cancelled_ = true;
            composition_lifecycle_.SetActive(false);
            SetUiPresentationEnabled(false);
            composition_lifecycle_.InvalidateActivation();
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::Lifecycle,
                &current_lifecycle_activity_,
                "TsfLifecycle(service={}, stage=activation-cancel-requested, activation={})",
                service_log_id_, composition_lifecycle_.Epoch().activation);
        }
        // Let the in-flight registration publish its ownership before teardown.
        return S_OK;
    }
    if (deactivating_) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=deactivate-reentrant, activation={}, context={})",
            service_log_id_,
            composition_lifecycle_.Epoch().activation,
            composition_lifecycle_.Epoch().context);
        return S_OK;
    }
    deactivating_ = true;
    const GUID act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, act_id);
    const auto finish_deactivation = gsl::finally([this, prev_act]() noexcept {
        current_lifecycle_activity_ = prev_act;
        deactivating_ = false;
    });
    composition_lifecycle_.SetActive(false);
    SetUiPresentationEnabled(false);
    composition_lifecycle_.InvalidateActivation();
    QIQI_LOG_ACT_START(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &act_id,
        &prev_act,
        "TsfLifecycle(service={}, stage=deactivate-begin, activation={}, context={}, has_composition={})",
        service_log_id_,
        composition_lifecycle_.Epoch().activation,
        composition_lifecycle_.Epoch().context,
        composition_lifecycle_.Composition() != nullptr);
    QIQI_LOG_ACT_INFO(
        log::Level::info,
        log::Keyword::Lifecycle,
        &act_id,
        "QiQiTextService::Deactivate(client_id={})",
        client_id_);
    ResetCompositionForInputMethodSwitch();
    if (status_bar_) {
        status_bar_->Show(false);
    }
    const bool windows_destroyed = DestroyUIWindows();

    UninitCompartmentSinks();
    UninitLangBarItem();
    UninitFunctionProvider();
    UninitKeyEventSink();
    profile_sink_.Reset();
    thread_mgr_sink_.Reset();

    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    activate_flags_ = 0;
    engine_.ClearExternalDictionaries();
    QIQI_LOG_ACT_STOP(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &act_id,
        "TsfLifecycle(service={}, stage=deactivate-local-complete, activation={}, context={}, cleanup_registry_slots={})",
        service_log_id_,
        composition_lifecycle_.Epoch().activation,
        composition_lifecycle_.Epoch().context,
        composition_lifecycle_.Cleanups().TrackedCount());
    return windows_destroyed ? S_OK : E_FAIL;
}

bool QiQiTextService::InitThreadMgrEventSink() noexcept {
    ITfThreadMgrEventSink* sink = this;
    if (!thread_mgr_sink_.Advise(
            thread_mgr_.Get(),
            IID_ITfThreadMgrEventSink,
            sink)) {
        QIQI_LOG_ERROR("InitThreadMgrEventSink: Advise(IID_ITfThreadMgrEventSink) failed");
        return false;
    }
    return true;
}

bool QiQiTextService::InitKeyEventSink() {
    key_event_sink_registered_ = false;
    registered_preserved_keys_.clear();
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return false;
    }
    ITfKeyEventSink* const sink = this;
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (!log::CheckHResult(thread_mgr_.As(&key_mgr),
                          "InitKeyEventSink::QueryInterface(ITfKeystrokeMgr)", log::Level::err) || !key_mgr) {
        return false;
    }
    if (activation_cancelled_) return false;
    const HRESULT hr = key_mgr->AdviseKeyEventSink(client_id_, sink, TRUE);
    if (FAILED(hr)) {
        QIQI_LOG_ERROR("InitKeyEventSink: AdviseKeyEventSink failed (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(hr));
        return false;
    }
    key_event_sink_registered_ = true;
    if (activation_cancelled_) return false;

    for (const auto& def : kPreservedKeys) {
        const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
        const HRESULT pk_hr = key_mgr->PreserveKey(
            client_id_, *def.guid, &pk, def.desc, gsl::narrow_cast<ULONG>(std::wcslen(def.desc)));
        if (SUCCEEDED(pk_hr)) {
            registered_preserved_keys_.push_back({*def.guid, pk});
        } else if (pk_hr == TF_E_ALREADY_EXISTS) {
            QIQI_LOG_WARN("InitKeyEventSink: PreserveKey(vkey=0x{:02X}, mod=0x{:X}) already registered; "
                          "skipping ownership (hr=0x{:08x})",
                          def.vkey, def.modifiers, gsl::narrow_cast<unsigned int>(pk_hr));
        } else {
            QIQI_LOG_WARN("InitKeyEventSink: PreserveKey(vkey=0x{:02X}, mod=0x{:X}) failed (hr=0x{:08x})",
                          def.vkey, def.modifiers, gsl::narrow_cast<unsigned int>(pk_hr));
        }
        if (activation_cancelled_) return false;
    }
    return true;
}

void QiQiTextService::UninitKeyEventSink() {
    if (!key_event_sink_registered_ && registered_preserved_keys_.empty()) {
        return;
    }
    const auto registered_keys = std::move(registered_preserved_keys_);
    registered_preserved_keys_.clear();
    const bool had_key_event_sink = std::exchange(key_event_sink_registered_, false);
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        QIQI_LOG_WARN("UninitKeyEventSink: missing thread manager or client ID (preserved_keys={}, key_sink={})",
                      registered_keys.size(), had_key_event_sink);
        return;
    }
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (!log::CheckHResult(thread_mgr_.As(&key_mgr), "UninitKeyEventSink::QueryInterface(ITfKeystrokeMgr)") ||
        !key_mgr) {
        return;
    }
    for (const auto& entry : registered_keys) {
        const HRESULT unpreserve_hr = key_mgr->UnpreserveKey(entry.guid, &entry.key);
        if (FAILED(unpreserve_hr)) {
            QIQI_LOG_WARN("UninitKeyEventSink: UnpreserveKey(vkey=0x{:02X}, mod=0x{:X}) failed (hr=0x{:08x})",
                          entry.key.uVKey, entry.key.uModifiers, gsl::narrow_cast<unsigned int>(unpreserve_hr));
        }
    }
    if (had_key_event_sink) {
        std::ignore = log::CheckHResult(key_mgr->UnadviseKeyEventSink(client_id_),
                                       "UninitKeyEventSink::UnadviseKeyEventSink");
    }
}

void QiQiTextService::RegisterCompositionDisplayAttributeAtom() {
    display_attribute_atom_ = detail::RegisterCompositionDisplayAttributeAtom();
}

void QiQiTextService::InitFunctionProvider() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (log::CheckHResult(thread_mgr_.As(&source_single),
                         "InitFunctionProvider::QueryInterface(ITfSourceSingle)") && source_single && !activation_cancelled_) {
        ITfFunctionProvider* const provider = this;
        const HRESULT hr =
            source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, provider);
        function_provider_registered_ = log::CheckHResult(
            hr,
            "InitFunctionProvider::AdviseSingleSink(IID_ITfFunctionProvider)");
        QIQI_LOG_CAT_DEBUG(
            log::Keyword::Lifecycle,
            "SearchIntegration(service={}, stage=provider-register, client_id={}, flags=0x{:x}, registered={}, hr=0x{:08x})",
            service_log_id_, client_id_, activate_flags_,
            function_provider_registered_, gsl::narrow_cast<unsigned int>(hr));
    }
}

void QiQiTextService::UninitFunctionProvider() {
    if (!function_provider_registered_ || !thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (log::CheckHResult(thread_mgr_.As(&source_single),
                         "UninitFunctionProvider::QueryInterface(ITfSourceSingle)") && source_single) {
        std::ignore = log::CheckHResult(
            source_single->UnadviseSingleSink(client_id_, IID_ITfFunctionProvider),
            "UninitFunctionProvider::UnadviseSingleSink(IID_ITfFunctionProvider)");
    }
    function_provider_registered_ = false;
}

HWND QiQiTextService::GetLangBarMenuOwner() const noexcept {
    const auto current_thread_window =
        [](HWND window) noexcept {
            if (!window ||
                !::IsWindow(window)) {
                return HWND{};
            }
            DWORD process_id = 0;
            const DWORD thread_id =
                ::GetWindowThreadProcessId(
                    window,
                    &process_id);
            return thread_id ==
                        ::GetCurrentThreadId() &&
                    process_id ==
                        ::GetCurrentProcessId()
                ? window
                : HWND{};
        };
    if (thread_mgr_) {
        ComPtr<ITfDocumentMgr> document;
        if (SUCCEEDED(
                thread_mgr_->GetFocus(
                    &document)) &&
            document) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(
                    document->GetTop(
                        &context)) &&
                context) {
                ComPtr<ITfContextView> view;
                if (SUCCEEDED(
                        context->GetActiveView(
                            &view)) &&
                    view) {
                    HWND owner = nullptr;
                    if (SUCCEEDED(
                            view->GetWnd(
                                &owner))) {
                        const HWND valid =
                            current_thread_window(
                                owner);
                        if (valid) {
                            return valid;
                        }
                    }
                }
            }
        }
    }
    return current_thread_window(
        ::GetForegroundWindow());
}

void QiQiTextService::InitLangBarItem() {
    if (!thread_mgr_) {
        return;
    }
    if (langbar_btn_) langbar_btn_->Disconnect();
    langbar_btn_ = MakeComObject<LangBarButton>(
        [this](LangBarMenuId action_id) {
            const ComPtr<QiQiTextService> self_holder(this);
            HandleLangBarMenuAction(action_id);
        },
        [this]() -> HWND {
            const ComPtr<QiQiTextService> self_holder(this);
            return GetLangBarMenuOwner();
        });
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    HRESULT hr = thread_mgr_.As(&lb_mgr);
    if (SUCCEEDED(hr) && !lb_mgr) hr = E_UNEXPECTED;
    QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
        "LangBarRegistration(service={}, item={}, stage=query-add, hr=0x{:08x})",
        service_log_id_, langbar_btn_->LogId(), gsl::narrow_cast<unsigned int>(hr));
    if (log::CheckHResult(hr, "InitLangBarItem::QueryInterface(ITfLangBarItemMgr)") &&
        lb_mgr && !activation_cancelled_) {
        hr = lb_mgr->AddItem(langbar_btn_.Get());
        langbar_item_added_ = log::CheckHResult(hr, "InitLangBarItem::AddItem");
        QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
            "LangBarRegistration(service={}, item={}, stage=add, hr=0x{:08x}, added={}, cancelled={})",
            service_log_id_, langbar_btn_->LogId(), gsl::narrow_cast<unsigned int>(hr),
            langbar_item_added_, activation_cancelled_);
        if (langbar_item_added_ && !activation_cancelled_) {
            std::ignore = log::CheckHResult(langbar_btn_->Show(TRUE), "InitLangBarItem::Show");
        }
    }
}

void QiQiTextService::UninitLangBarItem() {
    if (langbar_btn_) langbar_btn_->Disconnect();
    if (!langbar_item_added_ || !thread_mgr_ || !langbar_btn_) {
        return;
    }
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    HRESULT hr = thread_mgr_.As(&lb_mgr);
    if (SUCCEEDED(hr) && !lb_mgr) hr = E_UNEXPECTED;
    QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
        "LangBarRegistration(service={}, item={}, stage=query-remove, hr=0x{:08x})",
        service_log_id_, langbar_btn_->LogId(), gsl::narrow_cast<unsigned int>(hr));
    if (log::CheckHResult(hr, "UninitLangBarItem::QueryInterface(ITfLangBarItemMgr)") && lb_mgr) {
        hr = lb_mgr->RemoveItem(langbar_btn_.Get());
        std::ignore = log::CheckHResult(hr, "UninitLangBarItem::RemoveItem");
        QIQI_LOG_ACT_INFO(log::Level::debug, log::Keyword::UI, &current_lifecycle_activity_,
            "LangBarRegistration(service={}, item={}, stage=remove, hr=0x{:08x}, remove_succeeded={})",
            service_log_id_, langbar_btn_->LogId(), gsl::narrow_cast<unsigned int>(hr), SUCCEEDED(hr));
    }
    langbar_item_added_ = false;
}

STDMETHODIMP QiQiTextService::OnActivated(
    DWORD type, LANGID /*language*/, REFCLSID clsid, REFGUID category,
    REFGUID profile, HKL /*layout*/, DWORD flags) {
    if (type != TF_PROFILETYPE_INPUTPROCESSOR && type != TF_PROFILETYPE_KEYBOARDLAYOUT) return S_OK;
    if (type == TF_PROFILETYPE_INPUTPROCESSOR && !IsEqualGUID(category, GUID_TFCAT_TIP_KEYBOARD)) return S_OK;
    const bool own_profile = type == TF_PROFILETYPE_INPUTPROCESSOR &&
        IsEqualCLSID(clsid, kClsidQiQiTextService) && IsEqualGUID(profile, kGuidProfileWubi86);
    const bool activated = (flags & TF_IPSINK_FLAG_ACTIVE) != 0;
    if (!own_profile && !activated) return S_OK;
    const bool selected = own_profile && activated;
    const ComPtr<QiQiTextService> self_holder(this);
    if (activating_) return selected ? S_OK : Deactivate();
    if (deactivating_ || !thread_mgr_ || client_id_ == TF_CLIENTID_NULL) return S_OK;
    if (selected) {
        if (!profile_deactivated_) return S_OK;
        profile_deactivated_ = false;
        composition_lifecycle_.SetActive(true);
        SetUiPresentationEnabled(true);
        SyncTsfCompartments();
        if (composition_lifecycle_.IsActive()) ObserveFocusedPasswordContext();
    } else {
        if (profile_deactivated_ || !composition_lifecycle_.IsActive()) return S_OK;
        profile_deactivated_ = true;
        composition_lifecycle_.SetActive(false);
        composition_lifecycle_.InvalidateActivation();
        SetUiPresentationEnabled(false);
        ResetCompositionForInputMethodSwitch();
        if (profile_deactivated_ && status_bar_) status_bar_->Show(false);
    }
    return S_OK;
}

// ITfThreadMgrEventSink
STDMETHODIMP QiQiTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnUninitDocumentMgr(ITfDocumentMgr* pdim) {
    if (pdim && pdim == password_document_) DisconnectPasswordContext();
    const bool related = composition_lifecycle_.IsActive() && MatchesCompositionDocument(pdim, composition_lifecycle_);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=document-uninit, related={})",
        service_log_id_,
        related);
    if (related) {
        composition_lifecycle_.Cleanups().MarkContextRemoved(composition_lifecycle_.Context().Get(), current_lifecycle_activity_);
        ResetCompositionAndUI(false);
    }
    return S_OK;
}

bool QiQiTextService::ReconcileDocumentTarget(ITfDocumentMgr* document, std::string_view source) {
    return ReconcileCompositionDocumentTarget(
        document, source, composition_lifecycle_, service_log_id_, current_lifecycle_activity_,
        [this] { ResetCompositionAndUI(true, UnfinishedInputCleanupMode()); },
        [this](EditSessionTicket ticket) noexcept { return IsEditSessionCurrent(ticket); });
}

void QiQiTextService::HandleFocusChanged(bool has_focus, FocusNotificationSource source) {
    if (!composition_lifecycle_.IsActive()) return;
    const uint64_t focus_revision = ++focus_revision_;
    using Clock = std::chrono::steady_clock;
    static std::atomic<uint64_t> focus_log_sequence{0};
    const GUID focus_act_id = log::CreateActivityId();
    const GUID prev_act = std::exchange(current_lifecycle_activity_, focus_act_id);
    const auto metric_mode = has_focus ? metrics::Mode::FocusGained : metrics::Mode::FocusLost;
    const auto metric_source = source == FocusNotificationSource::ThreadManager
        ? metrics::Source::ThreadManager : metrics::Source::KeyEvent;
    metrics::Scope focus_metric(metrics::Metric::FocusTotal, service_log_id_, metric_mode, metric_source);
    focus_metric.SetActivity(focus_act_id, prev_act);
    const auto current_outcome = [&]() noexcept {
        if (!composition_lifecycle_.IsActive()) return metrics::Outcome::Cancelled;
        return focus_revision == focus_revision_ ? metrics::Outcome::Success : metrics::Outcome::Stale;
    };
    const auto set_result = [&](metrics::Scope& scope, const ReloadResult& result) noexcept {
        auto outcome = current_outcome();
        if (outcome == metrics::Outcome::Success && !result.succeeded) outcome = metrics::Outcome::Failure;
        scope.SetOutcome(outcome, FAILED(result.error) ? metrics::ErrorDomain::HResult : metrics::ErrorDomain::None,
            FAILED(result.error) ? gsl::narrow_cast<uint32_t>(result.error) : 0);
    };
    ReloadResult result;
    const bool measure = log::ShouldLog(log::Level::debug, log::Keyword::Lifecycle);
    const auto started = measure ? Clock::now() : Clock::time_point{};
    const uint64_t event_id = measure ? focus_log_sequence.fetch_add(1, std::memory_order_relaxed) + 1 : 0;
    const bool same_state = has_logged_focus_state_ && last_logged_focus_state_ == has_focus;
    if (measure) {
        has_logged_focus_state_ = true;
        last_logged_focus_state_ = has_focus;
    }
    int64_t reload_ns = 0;
    int64_t position_ns = 0;
    bool position_changed = false;
    bool completed = false;
    const auto trace_focus = gsl::finally([&]() noexcept {
        current_lifecycle_activity_ = prev_act;
        if (measure) {
            const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
            QIQI_LOG_ACT_STOP(
                log::Level::debug,
                log::Keyword::Lifecycle,
                &focus_act_id,
                "FocusPerformance(id={}, source={}, has_focus={}, same_state={}, reload_checked={}, "
                "reload_ns={}, position_ns={}, total_ns={}, position_changed={}, completed={})",
                event_id,
                source == FocusNotificationSource::ThreadManager ? "thread-manager" : "key-event",
                has_focus,
                same_state,
                has_focus,
                reload_ns,
                position_ns,
                total_ns,
                position_changed,
                completed);
        }
    });
    if (!has_focus) {
        DisconnectPasswordContext();
        ResetCompositionAndUI(true, UnfinishedInputCleanupMode());
        if (current_outcome() != metrics::Outcome::Success) {
            set_result(focus_metric, result);
            return;
        }
        if (status_bar_) {
            status_bar_->Show(false);
        }
    } else {
        const auto reload_started = measure ? Clock::now() : Clock::time_point{};
        {
            metrics::Scope reload_metric(metrics::Metric::FocusReload, service_log_id_,
                metric_mode, metric_source, focus_metric.OperationId());
            reload_metric.SetActivity(focus_act_id, prev_act);
            result = ReloadConfigIfModified(event_id);
            set_result(reload_metric, result);
        }
        if (current_outcome() != metrics::Outcome::Success) {
            set_result(focus_metric, result);
            return;
        }
        const auto reload_finished = measure ? Clock::now() : Clock::time_point{};
        {
            metrics::Scope position_metric(metrics::Metric::FocusPosition, service_log_id_,
                metric_mode, metric_source, focus_metric.OperationId());
            position_metric.SetActivity(focus_act_id, prev_act);
            ReloadResult position_result;
            position_result.Observe(SyncStatusBarPositionFromGlobalCompartment(&position_changed));
            set_result(position_metric, position_result);
            if (!position_result.succeeded) result.Fail(position_result.error);
        }
        if (current_outcome() != metrics::Outcome::Success) {
            set_result(focus_metric, result);
            return;
        }
        if (measure) {
            reload_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(reload_finished - reload_started).count();
            position_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - reload_finished).count();
        }
        if (status_bar_) {
            status_bar_->Show(config_.show_status_bar);
        }
    }
    completed = true;
    set_result(focus_metric, result);
}

STDMETHODIMP QiQiTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* /*pdimPrevFocus*/) {
    if (!composition_lifecycle_.IsActive()) return S_OK;
    const auto focus_revision = focus_revision_;
    if (!ReconcileDocumentTarget(pdimFocus, "focus")) return S_OK;
    if (!composition_lifecycle_.IsActive() || focus_revision != focus_revision_) return S_OK;
    HandleFocusChanged(pdimFocus != nullptr, FocusNotificationSource::ThreadManager);
    if (!composition_lifecycle_.IsActive() || focus_revision_ != focus_revision + 1) return S_OK;
    DisconnectPasswordContext();
    if (!composition_lifecycle_.IsActive() || focus_revision_ != focus_revision + 1) return S_OK;
    ObservePasswordDocument(pdimFocus);
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPushContext(ITfContext* pic) {
    if (!composition_lifecycle_.IsActive() || !pic) return S_OK;
    const bool observe_password = password_document_ != nullptr;
    const auto context = composition_lifecycle_.Context();
    const bool reconcile_composition = context && pic != context.Get();
    if (!observe_password && !reconcile_composition) return S_OK;
    const uint64_t revision = password_context_revision_;
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.Get()};
    ComPtr<ITfDocumentMgr> document;
    const HRESULT hr = pic->GetDocumentMgr(&document);
    if (observe_password && !IsPasswordObservationCurrent(revision)) return S_OK;
    if (!log::CheckHResult(hr, "OnPushContext::GetDocumentMgr") || !document) {
        if (reconcile_composition) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::Lifecycle,
                &current_lifecycle_activity_,
                "TsfLifecycle(service={}, stage=context-push-unresolved)",
                service_log_id_);
        }
        return S_OK;
    }
    if (observe_password && document.Get() == password_document_) {
        ObservePasswordDocument(document.Get());
    }
    if (!reconcile_composition || !IsEditSessionCurrent(ticket)) return S_OK;
    const bool related = MatchesCompositionDocument(document.Get(), composition_lifecycle_);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=context-push, related={})",
        service_log_id_,
        related);
    if (related) std::ignore = ReconcileDocumentTarget(document.Get(), "push");
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPopContext(ITfContext* pic) {
    const ComPtr<ITfDocumentMgr> password_document(
        pic && pic == password_context_.Get() ? password_document_ : nullptr);
    const uint64_t password_revision = password_context_revision_ + 1;
    if (password_document) DisconnectPasswordContext();
    if (pic) composition_lifecycle_.Cleanups().MarkContextRemoved(pic, current_lifecycle_activity_);
    const bool current = composition_lifecycle_.IsActive() && pic && pic == composition_lifecycle_.Context().Get();
    const bool preparing = composition_lifecycle_.IsActive() && pic && pic == preparing_input_context_.Get();
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &current_lifecycle_activity_,
        "TsfLifecycle(service={}, stage=context-pop, current={}, preparing={})",
        service_log_id_,
        current,
        preparing);
    if (current) {
        ResetCompositionAndUI(false);
    } else if (preparing) {
        composition_lifecycle_.InvalidateContext();
    }
    if (password_document && IsPasswordObservationCurrent(password_revision) && !password_context_) {
        ObservePasswordDocument(password_document.Get());
    }
    return S_OK;
}

// ITfFunctionProvider
STDMETHODIMP QiQiTextService::GetType(GUID* pguid) noexcept {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = kClsidQiQiTextService;
    return S_OK;
}

STDMETHODIMP QiQiTextService::GetDescription(BSTR* pbstrDesc) noexcept {
    return CopyToBstr(kTextServiceDesc, pbstrDesc);
}

STDMETHODIMP QiQiTextService::GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk) noexcept {
    HRESULT hr = E_INVALIDARG;
    if (ppunk) {
        *ppunk = nullptr;
        hr = IsEqualGUID(rguid, GUID_NULL) || IsEqualGUID(rguid, kClsidQiQiTextService)
            ? QueryInterface(riid, reinterpret_cast<void**>(ppunk))
            : E_NOINTERFACE;
    }
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::Input,
        "SearchIntegration(service={}, stage=get-function, function_null={}, function_service={}, "
        "function_data1=0x{:08x}, iid_data1=0x{:08x}, search_provider={}, linguistic_alternates={}, hr=0x{:08x})",
        service_log_id_, IsEqualGUID(rguid, GUID_NULL) != FALSE,
        IsEqualGUID(rguid, kClsidQiQiTextService) != FALSE, rguid.Data1, riid.Data1,
        IsEqualGUID(riid, IID_ITfFnSearchCandidateProvider) != FALSE,
        IsEqualGUID(riid, IID_ITfFnGetLinguisticAlternates) != FALSE,
        gsl::narrow_cast<unsigned int>(hr));
    return hr;
}

STDMETHODIMP QiQiTextService::GetSearchCandidates(
    BSTR query, BSTR application_id, ITfCandidateList** candidates) noexcept {
    size_t query_length = 0;
    const auto finish = [this, query, application_id, &query_length](
                            HRESULT hr, size_t count = 0) noexcept {
        QIQI_LOG_CAT_DEBUG(
            log::Keyword::Input,
            "SearchIntegration(service={}, stage=get-search-candidates, query_present={}, "
            "query_length={}, application_id_present={}, candidate_count={}, hr=0x{:08x})",
            service_log_id_, query != nullptr, query_length,
            application_id != nullptr, count, gsl::narrow_cast<unsigned int>(hr));
        return hr;
    };
    if (!candidates) return finish(E_INVALIDARG);
    *candidates = nullptr;
    if (!query) return finish(S_FALSE);
    query_length = gsl::narrow_cast<size_t>(::SysStringLen(query));
    const std::wstring_view code(query, query_length);
    if (code.empty()) return finish(S_FALSE);
    try {
        auto results = engine_.LookupCandidates(code);
        const size_t count = results.size();
        const HRESULT hr = CreateSearchCandidateList(
            std::move(results), candidates);
        return finish(hr, SUCCEEDED(hr) ? count : 0);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "QiQiTextService::GetSearchCandidates allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return finish(E_OUTOFMEMORY);
    }
}

STDMETHODIMP QiQiTextService::SetResult(
    BSTR query, BSTR application_id, BSTR result) noexcept {
    const HRESULT hr = query && result ? E_NOTIMPL : E_INVALIDARG;
    QIQI_LOG_CAT_DEBUG(
        log::Keyword::Input,
        "SearchIntegration(service={}, stage=set-result, query_present={}, application_id_present={}, "
        "result_present={}, feedback_applied=false, hr=0x{:08x})",
        service_log_id_, query != nullptr, application_id != nullptr,
        result != nullptr, gsl::narrow_cast<unsigned int>(hr));
    return hr;
}

HRESULT QiQiTextService::HandleCandidateUiAction(
    CandidateUiAction action, size_t index) {
    const ComPtr<ITfContext> context = composition_lifecycle_.Context();
    if (!context || !composition_lifecycle_.IsActive() ||
        !engine_.IsComposing()) {
        return E_ILLEGAL_METHOD_CALL;
    }
    const AccessPolicy policy = EvaluateContextAccess(context.Get(), true);
    if (!policy.can_input) return E_ACCESSDENIED;

    KeyProcessResult result;
    switch (action) {
    case CandidateUiAction::SetSelection:
        result = engine_.SelectCandidate(index);
        break;
    case CandidateUiAction::Finalize:
        if (engine_.AllCandidates().empty()) return S_FALSE;
        result = engine_.CommitCandidate(
            engine_.SelectedInPage(), policy.can_learn, true);
        break;
    case CandidateUiAction::Abort:
        result = engine_.ProcessKey(
            VK_ESCAPE, false, false, false, false,
            policy.can_learn, true);
        break;
    case CandidateUiAction::FinalizeExact:
        result = engine_.ProcessKey(
            VK_RETURN, false, false, false, false,
            policy.can_learn, true);
        break;
    default:
        return E_INVALIDARG;
    }
    if (result.action == KeyActionType::PassThrough) return S_FALSE;
    ApplyEngineResult(context.Get(), result);
    return S_OK;
}

HRESULT QiQiTextService::HandleIntegratedCandidateKeyDown(
    WPARAM wparam, LPARAM lparam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    const ComPtr<ITfContext> context = composition_lifecycle_.Context();
    if (!context) return S_FALSE;
    return OnKeyDown(context.Get(), wparam, lparam, eaten);
}

// ITfDisplayAttributeProvider
STDMETHODIMP QiQiTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) noexcept {
    return CreateComObjectToOut<QiQiEnumDisplayAttributeInfo>(
        ppEnum, 0u, config_.underline_style, config_.bold_underline);
}

STDMETHODIMP QiQiTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) noexcept {
    if (!ppInfo) {
        return E_INVALIDARG;
    }
    if (!IsEqualGUID(guid, kGuidDisplayAttributeInput)) {
        *ppInfo = nullptr;
        return E_INVALIDARG;
    }
    return CreateComObjectToOut<QiQiDisplayAttributeInfo>(
        ppInfo, config_.underline_style, config_.bold_underline);
}

} // namespace qiqi
