#include "Ime/tsf/tip_service.h"
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

bool IsCurrentProcessAppContainer() noexcept {
    ScopedKernelHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put())) {
        return false;
    }
    DWORD is_app_container = 0;
    DWORD ret_len = 0;
    if (::GetTokenInformation(
            token.Get(),
            TokenIsAppContainer,
            &is_app_container,
            sizeof(is_app_container),
            &ret_len)) {
        return is_app_container != 0;
    }
    return false;
}

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
        case LangBarMenuId::OpenAbout:         return "OpenAbout";
        default:                               return "Unknown";
    }
}

} // namespace

void QiQiTextService::LoadExternalDictionaryNextToDll() {
    engine_.ClearExternalDictionaries();
    const std::filesystem::path module_path = GetModuleFilePath(g_dll_instance);
    if (module_path.empty()) {
        QIQI_LOG_WARN("GetModuleFileNameW failed when locating external dictionaries");
        return;
    }
    const std::filesystem::path dll_dir = module_path.parent_path();

    size_t primary_count = 0;
    if (config_.primary_dict == PrimaryDict::Jidian) {
        const std::filesystem::path jidian_path = ResolveBundledDataFile(dll_dir, L"wubi86_jidian.txt");
        if (std::filesystem::exists(jidian_path) && engine_.LoadDictionaryFile(jidian_path)) {
            ++primary_count;
        }
    } else if (config_.primary_dict == PrimaryDict::Haifeng) {
        const std::filesystem::path haifeng_path = ResolveBundledDataFile(dll_dir, L"wubi86_haifeng.txt");
        if (std::filesystem::exists(haifeng_path) && engine_.LoadDictionaryFile(haifeng_path)) {
            ++primary_count;
        }
    }

    if (config_.primary_dict == PrimaryDict::Jidian && config_.enable_jidian_extra) {
        const std::filesystem::path extra_path = ResolveBundledDataFile(dll_dir, L"wubi86_jidian_extra.txt");
        if (std::filesystem::exists(extra_path) && engine_.LoadDictionaryFile(extra_path)) {
            ++primary_count;
        }
    }

    bool loaded_any = (primary_count > 0);
    if (!config_.user_dict_path.empty()) {
        std::filesystem::path user_path(config_.user_dict_path);
        if (user_path.is_relative()) {
            user_path = ResolveBundledDataFile(dll_dir, config_.user_dict_path);
        }
        if (std::filesystem::exists(user_path)) {
            loaded_any |= engine_.LoadDictionaryFile(user_path);
        } else {
            QIQI_LOG_WARN("Configured user dictionary '{}' does not exist",
                          WideToUtf8(user_path.wstring()));
        }
    } else {
        const std::filesystem::path default_user_dict = GetDefaultUserDictPath();
        if (std::filesystem::exists(default_user_dict)) {
            loaded_any |= engine_.LoadDictionaryFile(default_user_dict);
        } else {
            std::vector<uint8_t> reg_user_dict;
            if (LoadUserDictBinaryFromRegistry(reg_user_dict)) {
                loaded_any |= engine_.LoadDictionaryFromBinaryBuffer(std::move(reg_user_dict));
            }
        }
    }

    if (!loaded_any && config_.primary_dict != PrimaryDict::BuiltinOnly) {
        QIQI_LOG_DEBUG("External dictionaries not found in '{}', using builtin dictionary only",
                       WideToUtf8(dll_dir.wstring()));
    } else {
        QIQI_LOG_INFO("Dictionary initialization complete: {} external dictionary file(s) active",
                      engine_.MappedDictionaryCount());
    }
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
        if (!openclose_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_OPENCLOSE");
        }
        if (!convmode_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION");
        }
    }

    // 2. Bind process-wide TSF Global Compartments for config/dict broadcast & status bar position
    ComPtr<ITfCompartmentMgr> global_mgr;
    if (!log::CheckHResult(thread_mgr_->GetGlobalCompartment(&global_mgr),
                          "InitCompartmentSinks::GetGlobalCompartment", log::Level::debug) || !global_mgr) {
        return;
    }
    if (!global_config_comp_.Bind(global_mgr.Get(), kGuidCompartmentConfigVersion, sink)) {
        QIQI_LOG_WARN("InitCompartmentSinks: failed to bind kGuidCompartmentConfigVersion");
    }
    if (global_pos_comp_.Bind(global_mgr.Get(), kGuidCompartmentStatusBarPos, sink)) {
        SyncStatusBarPositionFromGlobalCompartment();
    } else {
        QIQI_LOG_WARN("InitCompartmentSinks: failed to bind kGuidCompartmentStatusBarPos");
    }
}

void QiQiTextService::UninitCompartmentSinks() noexcept {
    convmode_comp_.Reset();
    openclose_comp_.Reset();
    global_pos_comp_.Reset();
    global_config_comp_.Reset();
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

bool QiQiTextService::SyncStatusBarPositionFromGlobalCompartment() noexcept {
    if (!global_pos_comp_ || !status_bar_) {
        return false;
    }
    constexpr LONG kUnsetSentinel = INT32_MIN;
    const LONG packed = global_pos_comp_.ReadInt4(kUnsetSentinel);
    if (packed == kUnsetSentinel || packed == 0) {
        return false;
    }
    const POINT shared_pos = UnpackLongToScreenPoint(packed);
    if (shared_pos.x != status_bar_->Position().x || shared_pos.y != status_bar_->Position().y) {
        status_bar_->SetPosition(shared_pos);
        QIQI_LOG_DEBUG("SyncStatusBarPositionFromGlobalCompartment: synced pos=({}, {})",
                       shared_pos.x, shared_pos.y);
        return true;
    }
    return false;
}

void QiQiTextService::SyncEngineFromSystemCompartments(REFGUID changed_guid) {
    if (syncing_compartments_) {
        return;
    }
    bool changed = false;
    if (IsEqualGUID(changed_guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) && openclose_comp_) {
        const bool want_chinese = (openclose_comp_.ReadInt4(1) != 0);
        if (want_chinese != engine_.IsChineseMode()) {
            if (!want_chinese && engine_.IsComposing() && active_context_) {
                KeyProcessResult cancel_res{};
                cancel_res.action = KeyActionType::Cancel;
                ApplyEngineResult(active_context_.Get(), cancel_res);
            }
            engine_.SetChineseMode(want_chinese);
            changed = true;
        }
    } else if (IsEqualGUID(changed_guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) && convmode_comp_) {
        const LONG conv = convmode_comp_.ReadInt4(0);
        const bool want_chinese = (conv & TF_CONVERSIONMODE_NATIVE) != 0;
        const bool want_full_width = (conv & TF_CONVERSIONMODE_FULLSHAPE) != 0;
        const bool want_cn_punct = (conv & TF_CONVERSIONMODE_SYMBOL) != 0;

        if (want_chinese != engine_.IsChineseMode()) {
            if (!want_chinese && engine_.IsComposing() && active_context_) {
                KeyProcessResult cancel_res{};
                cancel_res.action = KeyActionType::Cancel;
                ApplyEngineResult(active_context_.Get(), cancel_res);
            }
            engine_.SetChineseMode(want_chinese);
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
            ToggleModeAndCancelComposition(active_context_.Get());
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
    config_registry_sequence_ = QueryRegistryConfigSequence();
    if (reload_dicts) {
        LoadExternalDictionaryNextToDll();
    }
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
        if (IsCurrentProcessAppContainer()) {
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
    QIQI_LOG_DEBUG("SyncTsfCompartments(service={}, attempt={}, chinese={}, thread_mgr={}, client_valid={}, syncing={}, langbar={}, openclose={}, conversion={})",
                   service_log_id_, shift_log_sequence_, engine_.IsChineseMode(), thread_mgr_ != nullptr,
                   client_id_ != TF_CLIENTID_NULL, syncing_compartments_, langbar_btn_ != nullptr,
                   static_cast<bool>(openclose_comp_), static_cast<bool>(convmode_comp_));
    UpdateUiIndicators();

    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL || syncing_compartments_) {
        return;
    }
    syncing_compartments_ = true;
    const auto reset_guard = gsl::finally([this]() noexcept {
        syncing_compartments_ = false;
    });

    if (openclose_comp_) {
        const LONG value = engine_.IsChineseMode() ? 1 : 0;
        const HRESULT hr = openclose_comp_.WriteInt4(client_id_, value);
        QIQI_LOG_DEBUG("SyncTsfCompartments(service={}, attempt={}, compartment=openclose, value={}, hr=0x{:08x})",
                       service_log_id_, shift_log_sequence_, value, gsl::narrow_cast<unsigned int>(hr));
    }

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
        QIQI_LOG_DEBUG("SyncTsfCompartments(service={}, attempt={}, compartment=conversion, value={}, hr=0x{:08x})",
                       service_log_id_, shift_log_sequence_, conv_mode, gsl::narrow_cast<unsigned int>(hr));
    }
}

void QiQiTextService::ReloadConfigIfModified(uint64_t focus_log_id) {
    const bool trace = log::ShouldLog(log::Level::debug);
    const bool dictionary_modified = engine_.HasModifiedMappedDictionary();
    unsigned int dictionary_reload_calls = 0;
    std::string_view config_status = "not-checked";
    std::string_view phrase_frequency_status = "not-requested";
    bool completed = false;
    const auto trace_reload = gsl::finally([&]() noexcept {
        if (trace) {
            QIQI_LOG_DEBUG("ReloadConfigIfModified(focus_id={}, config_status={}, dictionary_modified={}, "
                           "dictionary_reload_calls={}, mapped_dicts={}, phrase_frequency_status={}, completed={})",
                           focus_log_id, config_status, dictionary_modified, dictionary_reload_calls,
                           engine_.MappedDictionaryCount(), phrase_frequency_status, completed);
        }
    });
    if (dictionary_modified) {
        QIQI_LOG_INFO("Detected modified dictionary file on disk; hot-reloading external dictionaries");
        ++dictionary_reload_calls;
        LoadExternalDictionaryNextToDll();
    }

    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == config_registry_sequence_) {
        config_status = reg_seq == 0 ? "sequence-unavailable" : "unchanged";
        completed = true;
        return;
    }

    QiQiConfig updated{};
    config_status = "load-failed";
    if (LoadConfigFromRegistry(updated, &config_registry_sequence_)) {
        config_status = "loaded";
        config_ = std::move(updated);
        UpdateSettingsToastLanguage(config_.ui_language);
        ApplyDynamicEngineSettings();
        if (config_.dynamic_phrase_freq && !engine_.PhraseFreqPath().empty()) {
            phrase_frequency_status = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath()) ? "loaded" : "load-failed";
        }
        ++dictionary_reload_calls;
        LoadExternalDictionaryNextToDll();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Hot-reloaded modified user configuration from HKCU\\Software\\QiQiIME (dict={}, mapped_dicts={})",
                      PrimaryDictToString(config_.primary_dict),
                      engine_.MappedDictionaryCount());
    }
    completed = true;
}

} // namespace qiqi
