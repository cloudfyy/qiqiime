#include "ime/tsf/tip_service.h"
#include "tools/settings/window.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"

#include <oleauto.h>
#include <shellapi.h>
#include <filesystem>
#include <utility>
#include <vector>

namespace qiqi {

namespace {

constexpr const char* LangBarMenuIdToString(LangBarMenuId id) noexcept {
    switch (id) {
        case LangBarMenuId::SyncMenuState:     return "SyncMenuState";
        case LangBarMenuId::ToggleChinese:     return "ToggleChinese";
        case LangBarMenuId::ToggleFullWidth:   return "ToggleFullWidth";
        case LangBarMenuId::TogglePunctuation: return "TogglePunctuation";
        case LangBarMenuId::DictJidian:        return "DictJidian";
        case LangBarMenuId::DictHaifeng:       return "DictHaifeng";
        case LangBarMenuId::ToggleExtraDict:   return "ToggleExtraDict";
        case LangBarMenuId::ToggleStatusBar:   return "ToggleStatusBar";
        case LangBarMenuId::OpenSettings:      return "OpenSettings";
        case LangBarMenuId::ToggleUiLanguage:  return "ToggleUiLanguage";
        case LangBarMenuId::OpenHelp:          return "OpenHelp";
        case LangBarMenuId::OpenAbout:         return "OpenAbout";
        default:                               return "Unknown";
    }
}

} // namespace

QiQiTextService::ReloadResult QiQiTextService::LoadExternalDictionaryNextToDll() {
    ReloadResult result;
    const std::filesystem::path module_path = GetModuleFilePath(g_dll_instance);
    if (module_path.empty()) {
        engine_.ClearExternalDictionaries();
        QIQI_LOG_WARN("GetModuleFileNameW failed when locating external dictionaries");
        result.Fail();
        return result;
    }
    const std::filesystem::path dll_dir = module_path.parent_path();

    const PrimaryDictLoadSummary primary_summary =
        engine_.LoadConfiguredPrimaryDictionaries(config_, dll_dir, true);
    result.Observe(primary_summary.hr);
    bool loaded_any = (primary_summary.loaded_count > 0);
    const bool has_custom_user_path = !config_.user_dict_path.empty();
    const std::filesystem::path user_path = has_custom_user_path
        ? ResolveConfiguredDictionaryPath(dll_dir, config_.user_dict_path)
        : GetDefaultUserDictPath();
    engine_.SetUserDictPath(user_path);
    if (!IsProcessAppContainer()) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(user_path, ec);
        if (ec) {
            const HRESULT hr = HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(ec.value()));
            std::ignore = log::CheckHResult(hr, "LoadExternalDictionaryNextToDll::user dictionary");
            result.Fail(hr);
        } else if (exists) {
            const bool loaded = engine_.LoadDictionaryFile(user_path);
            loaded_any |= loaded;
            if (!loaded) result.Fail();
        } else if (has_custom_user_path) {
            result.Fail();
            QIQI_LOG_WARN("Configured user dictionary '{}' does not exist",
                          WideToUtf8(user_path.wstring()));
        }
    }
    result.Observe(engine_.InitializeUserDictionarySharing());

    if (!loaded_any && config_.primary_dict != PrimaryDict::BuiltinOnly) {
        QIQI_LOG_DEBUG("External dictionaries not found in '{}', using builtin dictionary only",
                       WideToUtf8(dll_dir.wstring()));
    } else {
        QIQI_LOG_INFO("Dictionary initialization complete: {} external dictionary file(s) active (primary={}, fallback={})",
                      engine_.MappedDictionaryCount(),
                      PrimaryDictToString(config_.primary_dict),
                      primary_summary.used_fallback);
    }
    return result;
}

void QiQiTextService::InitCompartmentSinks() {
    if (!thread_mgr_) {
        return;
    }
    ITfCompartmentEventSink* const sink = this;

    // 1. Bind thread-scoped system compartments for bidirectional mode sync
    ComPtr<ITfCompartmentMgr> thread_comp_mgr;
    if (log::CheckHResult(thread_mgr_.As(&thread_comp_mgr),
                         "InitCompartmentSinks::QueryInterface(ITfCompartmentMgr)") && thread_comp_mgr) {
        if (activation_cancelled_) return;
        if (!openclose_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_OPENCLOSE");
        }
        if (activation_cancelled_) return;
        if (!convmode_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION");
        }
    }
    if (activation_cancelled_) return;

    // 2. Bind process-wide TSF Global Compartments for config/dict broadcast & status bar position
    ComPtr<ITfCompartmentMgr> global_mgr;
    if (!log::CheckHResult(thread_mgr_->GetGlobalCompartment(&global_mgr),
                          "InitCompartmentSinks::GetGlobalCompartment", log::Level::debug) || !global_mgr) {
        return;
    }
    if (activation_cancelled_) return;
    if (!global_config_comp_.Bind(global_mgr.Get(), kGuidCompartmentConfigVersion, sink)) {
        QIQI_LOG_WARN("InitCompartmentSinks: failed to bind kGuidCompartmentConfigVersion");
    }
    if (activation_cancelled_) return;
    if (global_pos_comp_.Bind(global_mgr.Get(), kGuidCompartmentStatusBarPos, sink)) {
        if (activation_cancelled_) return;
        SyncStatusBarPositionFromGlobalCompartment();
    } else {
        QIQI_LOG_WARN("InitCompartmentSinks: failed to bind kGuidCompartmentStatusBarPos");
    }
}

void QiQiTextService::UninitCompartmentSinks() noexcept {
    DisconnectActiveContextAccessSinks();
    convmode_comp_.Reset();
    openclose_comp_.Reset();
    global_pos_comp_.Reset();
    global_config_comp_.Reset();
}

void QiQiTextService::ConnectActiveContextAccessSinks(ITfContext* context) {
    DisconnectActiveContextAccessSinks();
    context_access_.Reset();
    if (!context || !composition_lifecycle_.IsActive()) {
        return;
    }
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context};
    ITfStatusSink* const status_sink = this;
    std::ignore = context_status_sink_.Advise(context, IID_ITfStatusSink, status_sink);
    if (!IsEditSessionCurrent(ticket)) {
        DisconnectActiveContextAccessSinks();
        return;
    }
    ComPtr<ITfCompartmentMgr> comp_mgr;
    if (!log::CheckHResult(context->QueryInterface(IID_PPV_ARGS(&comp_mgr)),
                          "ConnectActiveContextAccessSinks::QueryInterface(ITfCompartmentMgr)", log::Level::debug) ||
        !comp_mgr) {
        return;
    }
    if (!IsEditSessionCurrent(ticket)) {
        DisconnectActiveContextAccessSinks();
        return;
    }
    ITfCompartmentEventSink* const sink = this;
    std::ignore = context_disabled_comp_.Bind(comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED, sink);
    if (!IsEditSessionCurrent(ticket)) {
        DisconnectActiveContextAccessSinks();
        return;
    }
    std::ignore = context_empty_comp_.Bind(comp_mgr.Get(), GUID_COMPARTMENT_EMPTYCONTEXT, sink);
    if (!IsEditSessionCurrent(ticket)) {
        DisconnectActiveContextAccessSinks();
    }
}

void QiQiTextService::DisconnectActiveContextAccessSinks() noexcept {
    context_empty_comp_.Reset();
    context_disabled_comp_.Reset();
    context_status_sink_.Reset();
}

STDMETHODIMP QiQiTextService::OnStatusChange(ITfContext* pic, DWORD /*dwFlags*/) {
    if (!pic || !composition_lifecycle_.IsActive() || pic != composition_lifecycle_.Context().Get()) {
        return S_OK;
    }
    HandleActiveContextAccessChanged();
    return S_OK;
}

void QiQiTextService::PrefetchActiveContextInputScope(ITfContext* context) {
    if (!context || !composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() ||
        preparing_input_context_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context};
    if (!IsEditSessionCurrent(ticket) || !context_access_.NeedsScopeQuery(ticket)) {
        return;
    }
    const ContextFacts fast_facts = ReadFastContextFacts(
        context, InputScopeClassification::Unknown,
        [this, ticket]() noexcept { return IsEditSessionCurrent(ticket); });
    if (!IsEditSessionCurrent(ticket) || fast_facts.HasError() || fast_facts.IsBlocked()) {
        return;
    }
    ComPtr<ITfContext> ctx_holder(context);
    ComPtr<QiQiTextService> self_holder(this);
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, ticket](TfEditCookie ec) -> HRESULT {
            if (!self_holder->IsEditSessionCurrent(ticket)) {
                return S_OK;
            }
            std::ignore = self_holder->EvaluateContextAccessInSession(
                ec, gsl::not_null<ITfContext*>(ctx_holder.Get()), ticket, true);
            return S_OK;
        });
    HRESULT session_hr = S_OK;
    const HRESULT req_hr = context->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
    const log::LogContext act_ctx{log::Keyword::Input, log::Opcode::Info, &current_lifecycle_activity_, nullptr};
    std::ignore = log::CheckHResult(req_hr, "PrefetchInputScope::RequestEditSession", log::Level::debug, act_ctx);
    if (SUCCEEDED(req_hr)) {
        std::ignore = log::CheckHResult(session_hr, "PrefetchInputScope::DoEditSession", log::Level::debug, act_ctx);
    }
}

void QiQiTextService::HandleActiveContextAccessChanged() {
    if (!composition_lifecycle_.IsActive() || composition_lifecycle_.IsResettingContext() ||
        preparing_input_context_) {
        return;
    }
    context_access_.OnDynamicStateChanged();
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Input,
        &current_lifecycle_activity_,
        "ContextAccess(service={}, rev={}, stage=dynamic-state-changed, has_active_context={})",
        service_log_id_, context_access_.Revision(), composition_lifecycle_.Context() != nullptr);
    if (ITfContext* const active_ctx = composition_lifecycle_.Context().Get()) {
        const AccessPolicy policy = EvaluateContextAccess(active_ctx, true);
        if (policy.can_input && composition_lifecycle_.Context().Get() == active_ctx) {
            PrefetchActiveContextInputScope(active_ctx);
        }
    }
}

bool QiQiTextService::NotifyGlobalConfigChanged() noexcept {
    if (!global_config_comp_ || client_id_ == TF_CLIENTID_NULL) {
        QIQI_LOG_WARN("NotifyGlobalConfigChanged: global compartment or client ID unavailable");
        return false;
    }
    if (FAILED(global_config_comp_.IncrementInt4(client_id_))) {
        QIQI_LOG_WARN("NotifyGlobalConfigChanged: IncrementInt4 failed");
        return false;
    }
    QIQI_LOG_DEBUG("NotifyGlobalConfigChanged: broadcast Global Compartment config version={}",
                   global_config_comp_.ReadInt4(0));
    return true;
}

void QiQiTextService::SaveGlobalStatusBarPosition(POINT pos) noexcept {
    if (!global_pos_comp_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    const LONG packed = PackScreenPointToLong(pos);
    if (FAILED(global_pos_comp_.WriteInt4(client_id_, packed))) {
        QIQI_LOG_WARN("SaveGlobalStatusBarPosition: SetValue(pos=({}, {})) failed", pos.x, pos.y);
    } else {
        QIQI_LOG_DEBUG("SaveGlobalStatusBarPosition: broadcast Global Compartment pos=({}, {})", pos.x, pos.y);
    }
}

HRESULT QiQiTextService::SyncStatusBarPositionFromGlobalCompartment(bool* changed) noexcept {
    if (changed) *changed = false;
    if (!global_pos_comp_ || !status_bar_) {
        return S_FALSE;
    }
    const uint64_t revision = focus_revision_;
    constexpr LONG kUnsetSentinel = INT32_MIN;
    LONG packed = kUnsetSentinel;
    const HRESULT hr = global_pos_comp_.TryReadInt4(packed);
    if (hr != S_OK) return hr;
    if (revision != focus_revision_ || !status_bar_) return S_FALSE;
    if (packed == kUnsetSentinel || packed == 0) {
        return S_FALSE;
    }
    const POINT shared_pos = UnpackLongToScreenPoint(packed);
    if (shared_pos.x != status_bar_->Position().x || shared_pos.y != status_bar_->Position().y) {
        status_bar_->SetPosition(shared_pos);
        QIQI_LOG_DEBUG("SyncStatusBarPositionFromGlobalCompartment: synced pos=({}, {})",
                       shared_pos.x, shared_pos.y);
        if (changed) *changed = true;
    }
    return S_OK;
}

void QiQiTextService::SyncEngineFromSystemCompartments(REFGUID changed_guid) {
    if (syncing_compartments_) {
        return;
    }
    bool changed = false;
    if (IsEqualGUID(changed_guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) && openclose_comp_) {
        const bool want_chinese = (openclose_comp_.ReadInt4(1) != 0);
        if (want_chinese != engine_.IsChineseMode()) {
            SetChineseModeAndFinishComposition(composition_lifecycle_.Context().Get(), want_chinese);
            changed = true;
        }
    } else if (IsEqualGUID(changed_guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) && convmode_comp_) {
        const LONG conv = convmode_comp_.ReadInt4(0);
        const bool want_chinese = (conv & TF_CONVERSIONMODE_NATIVE) != 0;
        const bool want_full_width = (conv & TF_CONVERSIONMODE_FULLSHAPE) != 0;
        const bool want_cn_punct = (conv & TF_CONVERSIONMODE_SYMBOL) != 0;

        if (want_chinese != engine_.IsChineseMode()) {
            SetChineseModeAndFinishComposition(composition_lifecycle_.Context().Get(), want_chinese);
            changed = true;
        }
        if (want_full_width != engine_.IsFullWidthMode()) {
            engine_.SetFullWidthMode(want_full_width);
            changed = true;
        }
        if (want_cn_punct != engine_.IsChinesePunctuation()) {
            engine_.SetChinesePunctuation(want_cn_punct);
            changed = true;
        }
    }

    if (changed) {
        QIQI_LOG_INFO("Synchronized external TSF compartment change (cn={}, full_width={}, cn_punct={})",
                      engine_.IsChineseMode(), engine_.IsFullWidthMode(), engine_.IsChinesePunctuation());
        SyncTsfCompartments();
    }
}

STDMETHODIMP QiQiTextService::OnChange(REFGUID rguid) {
    if (IsEqualGUID(rguid, kGuidCompartmentConfigVersion)) {
        QIQI_LOG_DEBUG("ITfCompartmentEventSink::OnChange(kGuidCompartmentConfigVersion) received");
        ReloadConfigIfModified();
    } else if (IsEqualGUID(rguid, kGuidCompartmentStatusBarPos)) {
        QIQI_LOG_DEBUG("ITfCompartmentEventSink::OnChange(kGuidCompartmentStatusBarPos) received");
        SyncStatusBarPositionFromGlobalCompartment();
    } else if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) ||
               IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
        SyncEngineFromSystemCompartments(rguid);
    } else if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
               IsEqualGUID(rguid, GUID_COMPARTMENT_EMPTYCONTEXT)) {
        HandleActiveContextAccessChanged();
    }
    return S_OK;
}

void QiQiTextService::HandleLangBarMenuAction(LangBarMenuId action_id) {
    if (action_id == LangBarMenuId::SyncMenuState) {
        ReloadConfigIfModified();
        SyncTsfCompartments();
        return;
    }
    QIQI_LOG_INFO("HandleLangBarMenuAction invoked (action={} [{}])",
                  LangBarMenuIdToString(action_id), static_cast<unsigned int>(action_id));
    switch (action_id) {
        case LangBarMenuId::ToggleChinese:
            ToggleModeAndFinishComposition(composition_lifecycle_.Context().Get());
            break;
        case LangBarMenuId::ToggleFullWidth:
            engine_.ToggleFullWidthMode();
            SyncTsfCompartments();
            break;
        case LangBarMenuId::TogglePunctuation:
            engine_.ToggleChinesePunctuation();
            SyncTsfCompartments();
            break;
        case LangBarMenuId::DictJidian:
            if (config_.primary_dict != PrimaryDict::Jidian) {
                QiQiConfig updated_config = config_;
                updated_config.primary_dict = PrimaryDict::Jidian;
                PersistAndSyncQuickConfigChange(std::move(updated_config), true);
            }
            break;
        case LangBarMenuId::DictHaifeng:
            if (config_.primary_dict != PrimaryDict::Haifeng) {
                QiQiConfig updated_config = config_;
                updated_config.primary_dict = PrimaryDict::Haifeng;
                PersistAndSyncQuickConfigChange(std::move(updated_config), true);
            }
            break;
        case LangBarMenuId::ToggleExtraDict:
            if (config_.primary_dict == PrimaryDict::Jidian) {
                QiQiConfig updated_config = config_;
                updated_config.enable_jidian_extra = !updated_config.enable_jidian_extra;
                PersistAndSyncQuickConfigChange(std::move(updated_config), true);
            }
            break;
        case LangBarMenuId::ToggleStatusBar: {
            QiQiConfig updated_config = config_;
            updated_config.show_status_bar = !updated_config.show_status_bar;
            PersistAndSyncQuickConfigChange(std::move(updated_config), false);
            break;
        }
        case LangBarMenuId::ToggleUiLanguage: {
            QiQiConfig updated_config = config_;
            updated_config.ui_language = (ResolveUiLanguage(updated_config.ui_language) == UiLanguage::EnUs)
                ? UiLanguage::ZhCn
                : UiLanguage::EnUs;
            PersistAndSyncQuickConfigChange(std::move(updated_config), false);
            break;
        }
        case LangBarMenuId::OpenSettings:
            Show(nullptr, kChineseSimplifiedLangId, kGuidProfileWubi86);
            break;
        case LangBarMenuId::OpenHelp:
            ShowHelpDialog(g_dll_instance, nullptr, config_.ui_language);
            break;
        case LangBarMenuId::OpenAbout:
            ShowAboutDialog(g_dll_instance, nullptr, config_.ui_language);
            break;
    }
}

// ITfFnConfigure
STDMETHODIMP QiQiTextService::GetDisplayName(BSTR* pbstrName) noexcept {
    const auto& str = GetUiStrings(config_.ui_language);
    return CopyToBstr(str.configure_display_name, pbstrName);
}

void QiQiTextService::ApplyDynamicEngineSettings() noexcept {
    log::SetLevel(config_.log_level);
    engine_.SetPageSize(config_.page_size);
    engine_.SetAutoCommit4Code(config_.auto_commit_4code);
    engine_.SetPushCommit5th(config_.push_commit_5th);
    engine_.SetSemicolonQuoteQuickSelect(config_.semicolon_quote_quick_select);
    engine_.SetShowCodeHint(config_.show_code_hint);
    engine_.SetEnableZTempEnglish(config_.enable_z_temp_english);
    engine_.SetDynamicPhraseFreq(config_.dynamic_phrase_freq);
    engine_.SetBeepOnEmpty4Code(config_.beep_on_empty_4code);
    engine_.SetBeepOnDup4Code(config_.beep_on_dup_4code);
    engine_.SetClearOnEmpty4Code(config_.clear_on_empty_4code);
    engine_.SetEnableKeySound(config_.enable_key_sound);
    engine_.SetUpDownPageTurn(config_.up_down_page_turn);
}

bool QiQiTextService::PersistAndSyncQuickConfigChange(QiQiConfig updated_config, bool reload_dicts) {
    if (!SaveConfig(updated_config)) {
        QIQI_LOG_ERROR("Quick settings save failed; keeping the current configuration");
        ShowSettingsToast(config_.ui_language, SettingsToastKind::SaveFailure);
        return false;
    }

    config_ = std::move(updated_config);
    UpdateSettingsToastLanguage(config_.ui_language);
    SyncSavedConfigChange(reload_dicts);
    if (!NotifyGlobalConfigChanged()) {
        ShowSettingsToast(config_.ui_language, SettingsToastKind::NotificationFailure);
    }
    return true;
}

void QiQiTextService::SyncSavedConfigChange(bool reload_dicts) {
    if (reload_dicts) {
        LoadExternalDictionaryNextToDll();
    }
    config_registry_sequence_ = QueryRegistryConfigSequence();
    SyncTsfCompartments();
}

STDMETHODIMP QiQiTextService::Show(HWND hwndParent, LANGID /*langid*/, REFGUID /*rguidProfile*/) {
    QIQI_LOG_INFO("Opening Settings dialog");
    ReloadConfigIfModified();
    if (ActivateExistingSettingsWindow()) {
        QIQI_LOG_INFO("Activated existing Settings dialog window");
        return S_OK;
    }
    if (!hwndParent) {
        if (IsProcessAppContainer()) {
            ::AllowSetForegroundWindow(ASFW_ANY);
            const HINSTANCE sh_res = ::ShellExecuteW(
                nullptr, L"open", L"qiqiime-settings:", nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(sh_res) > 32) {
                QIQI_LOG_INFO("Launched QiQiIMESettings.exe out-of-sandbox via qiqiime-settings: URI protocol");
                return S_OK;
            }
        }
        const std::filesystem::path module_path = GetModuleFilePath(g_dll_instance);
        const std::wstring mod_filename = module_path.filename().wstring();
        const bool is_ime_dll = (_wcsicmp(mod_filename.c_str(), L"qiqiime.dll") == 0);
        if (is_ime_dll) {
            const std::filesystem::path exe_path =
                ResolveBundledDataFile(module_path.parent_path(), L"QiQiIMESettings.exe");
            std::error_code ec;
            if (std::filesystem::exists(exe_path, ec)) {
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                std::wstring cmd = L"\"" + exe_path.wstring() + L"\"";
                if (::CreateProcessW(
                        exe_path.c_str(),
                        cmd.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        0,
                        nullptr,
                        nullptr,
                        &si,
                        &pi) != FALSE) {
                    const ScopedKernelHandle proc(pi.hProcess);
                    const ScopedKernelHandle thread(pi.hThread);
                    ::AllowSetForegroundWindow(pi.dwProcessId);
                    QIQI_LOG_INFO("Launched standalone QiQiIMESettings.exe from '{}'",
                                  WideToUtf8(exe_path.wstring()));
                    return S_OK;
                }
            }
            ::AllowSetForegroundWindow(ASFW_ANY);
            const HINSTANCE sh_res = ::ShellExecuteW(
                nullptr, L"open", L"qiqiime-settings:", nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(sh_res) > 32) {
                QIQI_LOG_INFO("Launched QiQiIMESettings.exe via qiqiime-settings: URI fallback");
                return S_OK;
            }
        }
    }
    if (ShowSettingsDialog(g_dll_instance, hwndParent, config_, true)) {
        ApplyDynamicEngineSettings();
        SyncSavedConfigChange(true);
        if (!NotifyGlobalConfigChanged()) {
            ReportSettingsNotificationFailure(hwndParent, config_.ui_language);
        }
        QIQI_LOG_INFO("Settings dialog saved and applied");
    } else {
        QIQI_LOG_DEBUG("Settings dialog cancelled");
    }
    return S_OK;
}

void QiQiTextService::UpdateUiIndicators() {
    LangBarState lb_state{};
    lb_state.chinese_mode = engine_.IsChineseMode();
    lb_state.full_width = engine_.IsFullWidthMode();
    lb_state.chinese_punct = engine_.IsChinesePunctuation();
    lb_state.show_status_bar = config_.show_status_bar;
    lb_state.primary_dict = config_.primary_dict;
    lb_state.enable_jidian_extra = config_.enable_jidian_extra;
    lb_state.ui_language = config_.ui_language;

    if (candidate_ui_) {
        candidate_ui_->SetUiLanguage(config_.ui_language);
    }
    if (langbar_btn_) {
        langbar_btn_->UpdateState(lb_state);
    }
    if (status_bar_ && thread_mgr_) {
        status_bar_->UpdateState(lb_state);
    }
}

void QiQiTextService::SyncTsfCompartments() {
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::Config,
        &current_lifecycle_activity_,
        "SyncTsfCompartments(service={}, attempt={}, chinese={}, thread_mgr={}, client_valid={}, syncing={}, langbar={}, openclose={}, conversion={})",
        service_log_id_, shift_tap_.Sequence(), engine_.IsChineseMode(), thread_mgr_ != nullptr,
        client_id_ != TF_CLIENTID_NULL, syncing_compartments_, langbar_btn_ != nullptr,
        static_cast<bool>(openclose_comp_), static_cast<bool>(convmode_comp_));
    if (activation_cancelled_) return;
    UpdateUiIndicators();

    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL || syncing_compartments_ || activation_cancelled_) {
        return;
    }
    syncing_compartments_ = true;
    const auto reset_guard = gsl::finally([this]() noexcept {
        syncing_compartments_ = false;
    });

    if (openclose_comp_) {
        const LONG value = engine_.IsChineseMode() ? 1 : 0;
        const HRESULT hr = openclose_comp_.WriteInt4(client_id_, value);
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Config,
            &current_lifecycle_activity_,
            "SyncTsfCompartments(service={}, attempt={}, compartment=openclose, value={}, hr=0x{:08x})",
            service_log_id_, shift_tap_.Sequence(), value, gsl::narrow_cast<unsigned int>(hr));
    }
    if (activation_cancelled_) return;

    LONG conv_mode = 0;
    if (engine_.IsChineseMode()) {
        conv_mode |= TF_CONVERSIONMODE_NATIVE;
    }
    if (engine_.IsFullWidthMode()) {
        conv_mode |= TF_CONVERSIONMODE_FULLSHAPE;
    }
    if (engine_.IsChinesePunctuation()) {
        conv_mode |= TF_CONVERSIONMODE_SYMBOL;
    }
    if (convmode_comp_) {
        const HRESULT hr = convmode_comp_.WriteInt4(client_id_, conv_mode);
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::Config,
            &current_lifecycle_activity_,
            "SyncTsfCompartments(service={}, attempt={}, compartment=conversion, value={}, hr=0x{:08x})",
            service_log_id_, shift_tap_.Sequence(), conv_mode, gsl::narrow_cast<unsigned int>(hr));
    }
}

QiQiTextService::ReloadResult QiQiTextService::ReloadConfigIfModified(uint64_t focus_log_id) {
    ReloadResult result;
    const bool trace = log::ShouldLog(log::Level::debug, log::Keyword::Config);
    const bool dictionary_modified = engine_.HasModifiedMappedDictionary();
    unsigned int dictionary_reload_calls = 0;
    std::string_view config_status = "not-checked";
    std::string_view phrase_frequency_status = "not-requested";
    bool completed = false;
    const auto trace_reload = gsl::finally([&]() noexcept {
        if (trace) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::Config,
                &current_lifecycle_activity_,
                "ReloadConfigIfModified(focus_id={}, config_status={}, dictionary_modified={}, "
                "dictionary_reload_calls={}, mapped_dicts={}, phrase_frequency_status={}, completed={})",
                focus_log_id, config_status, dictionary_modified, dictionary_reload_calls,
                engine_.MappedDictionaryCount(), phrase_frequency_status, completed);
        }
    });
    if (dictionary_modified) {
        QIQI_LOG_ACT_INFO(
            log::Level::info,
            log::Keyword::Dictionary,
            &current_lifecycle_activity_,
            "Detected modified dictionary file on disk; hot-reloading external dictionaries");
        ++dictionary_reload_calls;
        result = LoadExternalDictionaryNextToDll();
    } else {
        result.Observe(engine_.SynchronizeUserDictionary());
    }
    if (config_.dynamic_phrase_freq) {
        result.Observe(engine_.SynchronizePhraseFrequencies());
    }

    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == config_registry_sequence_) {
        config_status = reg_seq == 0 ? "sequence-unavailable" : "unchanged";
        if (reg_seq == 0) {
            result.Fail();
            QIQI_LOG_WARN("ReloadConfigIfModified: configuration sequence unavailable");
        }
        completed = true;
        return result;
    }

    QiQiConfig updated{};
    config_status = "load-failed";
    if (LoadConfigFromRegistry(updated, &config_registry_sequence_)) {
        config_status = "loaded";
        config_ = std::move(updated);
        UpdateSettingsToastLanguage(config_.ui_language);
        ApplyDynamicEngineSettings();
        if (config_.dynamic_phrase_freq && !engine_.PhraseFreqPath().empty()) {
            const bool loaded = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath());
            phrase_frequency_status = loaded ? "loaded" : "load-failed";
            if (!loaded) result.Fail();
        }
        ++dictionary_reload_calls;
        const auto dictionaries = LoadExternalDictionaryNextToDll();
        if (!dictionaries.succeeded) result.Fail(dictionaries.error);
        SyncTsfCompartments();
        QIQI_LOG_ACT_INFO(
            log::Level::info,
            log::Keyword::Config,
            &current_lifecycle_activity_,
            "Hot-reloaded modified user configuration from HKCU\\Software\\QiQiIME (dict={}, mapped_dicts={})",
            PrimaryDictToString(config_.primary_dict),
            engine_.MappedDictionaryCount());
    } else {
        result.Fail();
        QIQI_LOG_WARN("ReloadConfigIfModified: configuration load failed");
    }
    completed = true;
    return result;
}

} // namespace qiqi
