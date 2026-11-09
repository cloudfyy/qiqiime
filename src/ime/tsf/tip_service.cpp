#include "ime/tsf/tip_service.h"
#include "ime/tsf/category_manager.h"
#include "ime/tsf/display_attribute.h"
#include "ime/comp/target.h"
#include "common/file_util.h"
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
    config_ = LoadConfig(instance);
    log::SetLevel(config_.log_level);
    engine_.ApplyConfig(config_);
    engine_.SetUserDictPath(GetDefaultUserDictPath());
    engine_.SetPhraseFreqPath(GetDefaultPhraseFreqPath());
    std::ignore = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath());
    candidate_ui_ = MakeComObject<CandidateUI>(instance);
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
    langbar_btn_ = MakeComObject<LangBarButton>([this](LangBarMenuId action_id) {
        HandleLangBarMenuAction(action_id);
    });
    status_bar_ = std::make_unique<StatusBarUI>(instance, [this](LangBarMenuId action_id) {
        HandleLangBarMenuAction(action_id);
    });
    status_bar_->SetPositionChangedCallback([this](POINT new_pos) {
        SaveGlobalStatusBarPosition(new_pos);
    });
    SetUiPresentationEnabled(false);
    LoadExternalDictionaryNextToDll();
    config_registry_sequence_ = QueryRegistryConfigSequence();
}

QiQiTextService::~QiQiTextService() noexcept {
    if (candidate_ui_) {
        candidate_ui_->SetSelectCallback({});
        candidate_ui_->SetPageTurnCallback({});
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
        version::kFullVersion,
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
    ResetCompositionAndUI();
    if (status_bar_) {
        status_bar_->Show(false);
    }
    const bool windows_destroyed = DestroyUIWindows();

    UninitCompartmentSinks();
    UninitLangBarItem();
    UninitFunctionProvider();
    UninitKeyEventSink();
    thread_mgr_sink_.Reset();

    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    activate_flags_ = 0;
    engine_.ClearExternalDictionaries();
    QIQI_LOG_ACT_STOP(
        log::Level::debug,
        log::Keyword::Lifecycle,
        &act_id,
        "TsfLifecycle(service={}, stage=deactivate-local-complete, activation={}, context={}, tracked_cleanups={})",
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
        function_provider_registered_ = log::CheckHResult(
            source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, provider),
            "InitFunctionProvider::AdviseSingleSink(IID_ITfFunctionProvider)");
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

void QiQiTextService::InitLangBarItem() {
    if (!thread_mgr_ || !langbar_btn_) {
        return;
    }
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    if (log::CheckHResult(thread_mgr_.As(&lb_mgr), "InitLangBarItem::QueryInterface(ITfLangBarItemMgr)") &&
        lb_mgr && !activation_cancelled_) {
        langbar_item_added_ = log::CheckHResult(lb_mgr->AddItem(langbar_btn_.Get()), "InitLangBarItem::AddItem");
    }
}

void QiQiTextService::UninitLangBarItem() {
    if (!langbar_item_added_ || !thread_mgr_ || !langbar_btn_) {
        return;
    }
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    if (log::CheckHResult(thread_mgr_.As(&lb_mgr), "UninitLangBarItem::QueryInterface(ITfLangBarItemMgr)") && lb_mgr) {
        std::ignore = log::CheckHResult(lb_mgr->RemoveItem(langbar_btn_.Get()), "UninitLangBarItem::RemoveItem");
    }
    langbar_item_added_ = false;
}

// ITfThreadMgrEventSink
STDMETHODIMP QiQiTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnUninitDocumentMgr(ITfDocumentMgr* pdim) {
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
        [this] { ResetCompositionAndUI(); },
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
        ResetCompositionAndUI();
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
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPushContext(ITfContext* pic) {
    if (!composition_lifecycle_.IsActive() || !pic || !composition_lifecycle_.Context() || pic == composition_lifecycle_.Context().Get()) return S_OK;
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), composition_lifecycle_.Context().Get()};
    const auto context = composition_lifecycle_.Context();
    ComPtr<ITfDocumentMgr> document;
    if (!log::CheckHResult(pic->GetDocumentMgr(&document), "OnPushContext::GetDocumentMgr") || !document) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Lifecycle,
            &current_lifecycle_activity_,
            "TsfLifecycle(service={}, stage=context-push-unresolved)",
            service_log_id_);
        return S_OK;
    }
    if (!IsEditSessionCurrent(ticket)) return S_OK;
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
    if (!ppunk) {
        return E_INVALIDARG;
    }
    *ppunk = nullptr;
    if (!IsEqualGUID(rguid, GUID_NULL) && !IsEqualGUID(rguid, kClsidQiQiTextService)) {
        return E_NOINTERFACE;
    }
    return QueryInterface(riid, reinterpret_cast<void**>(ppunk));
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
