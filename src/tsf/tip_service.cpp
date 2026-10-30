#include "tsf/tip_service.h"
#include "tsf/display_attribute.h"
#include "tsf/edit_session.h"
#include "ui/settings_dialog.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/sound.h"
#include "common/version.h"

#include <oleauto.h>
#include <shellapi.h>
#include <array>
#include <filesystem>

namespace qiqi {

namespace {

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

bool IsKeyDown(int vk) noexcept {
    return (::GetKeyState(vk) & 0x8000) != 0;
}

constexpr const char* LangBarMenuIdToString(LangBarMenuId id) noexcept {
    switch (id) {
        case LangBarMenuId::ToggleChinese:     return "ToggleChinese";
        case LangBarMenuId::ToggleFullWidth:   return "ToggleFullWidth";
        case LangBarMenuId::TogglePunctuation: return "TogglePunctuation";
        case LangBarMenuId::DictJidian:        return "DictJidian";
        case LangBarMenuId::DictHaifeng:       return "DictHaifeng";
        case LangBarMenuId::ToggleExtraDict:   return "ToggleExtraDict";
        case LangBarMenuId::ToggleStatusBar:   return "ToggleStatusBar";
        case LangBarMenuId::OpenSettings:      return "OpenSettings";
        case LangBarMenuId::OpenConfigFile:    return "OpenConfigFile";
        case LangBarMenuId::ToggleUiLanguage:  return "ToggleUiLanguage";
        case LangBarMenuId::OpenAbout:         return "OpenAbout";
        default:                               return "Unknown";
    }
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

constexpr const char* PrimaryDictToDiagName(PrimaryDict dict) noexcept {
    switch (dict) {
        case PrimaryDict::Haifeng:     return "haifeng";
        case PrimaryDict::BuiltinOnly: return "builtin";
        case PrimaryDict::Jidian:
        default:                       return "jidian";
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
        case VK_OEM_1:      return "VK_OEM_1(;/:)";
        case VK_OEM_7:      return "VK_OEM_7('/\")";
        case VK_OEM_MINUS:  return "VK_OEM_MINUS(-/_)";
        case VK_OEM_PLUS:   return "VK_OEM_PLUS(=/+)";
        case VK_OEM_COMMA:  return "VK_OEM_COMMA(,/<)";
        case VK_OEM_PERIOD: return "VK_OEM_PERIOD(./>)";
        default:            return std::format("0x{:02X}", vk);
    }
}

} // namespace

QiQiTextService::QiQiTextService(HINSTANCE instance) {
    config_ = LoadConfig(instance);
    std::error_code ec;
    const auto user_cfg = GetDefaultUserConfigPath();
    if (std::filesystem::exists(user_cfg, ec)) {
        config_last_write_time_ = std::filesystem::last_write_time(user_cfg, ec);
    }
    log::SetLevel(config_.log_level);
    engine_.ApplyConfig(config_);
    candidate_ui_ = MakeComObject<CandidateUI>(instance);
    candidate_ui_->SetSelectCallback([this](size_t index_in_page) {
        if (index_in_page < 9 && active_context_ && engine_.IsComposing()) {
            const KeyProcessResult res = engine_.ProcessKey(
                gsl::narrow_cast<uint32_t>('1' + index_in_page), false, false, false);
            if (res.action != KeyActionType::PassThrough) {
                ApplyEngineResult(active_context_.Get(), res);
            }
        }
    });
    langbar_btn_ = MakeComObject<LangBarButton>([this](LangBarMenuId action_id) {
        HandleLangBarMenuAction(action_id);
    });
    status_bar_ = std::make_unique<StatusBarUI>(instance, [this](LangBarMenuId action_id) {
        HandleLangBarMenuAction(action_id);
    });
    LoadExternalDictionaryNextToDll();
    ++g_com_object_count;
}

QiQiTextService::~QiQiTextService() noexcept {
    try {
        if (status_bar_) {
            status_bar_->DestroyBarWindow();
            status_bar_.reset();
        }
        if (candidate_ui_) {
            candidate_ui_->DestroyPopupWindow();
            candidate_ui_.Reset();
        }
        langbar_btn_.Reset();
    } catch (...) {
    }
    --g_com_object_count;
}

void QiQiTextService::LoadExternalDictionaryNextToDll() {
    engine_.ClearExternalDictionaries();
    const std::filesystem::path module_path = log::GetModuleFilePath(g_dll_instance);
    if (module_path.empty()) {
        QIQI_LOG_WARN("GetModuleFileNameW failed when locating external dictionaries");
        return;
    }
    const std::filesystem::path dll_dir = module_path.parent_path();

    bool loaded_any = false;
    if (config_.primary_dict == PrimaryDict::Jidian) {
        const std::filesystem::path jidian_path = ResolveBundledDataFile(dll_dir, L"wubi86_jidian.txt");
        if (std::filesystem::exists(jidian_path)) {
            loaded_any |= engine_.LoadDictionaryFile(jidian_path);
        }
    } else if (config_.primary_dict == PrimaryDict::Haifeng) {
        const std::filesystem::path haifeng_path = ResolveBundledDataFile(dll_dir, L"wubi86_haifeng.txt");
        if (std::filesystem::exists(haifeng_path)) {
            loaded_any |= engine_.LoadDictionaryFile(haifeng_path);
        }
    }

    if (config_.primary_dict == PrimaryDict::Jidian && config_.enable_jidian_extra) {
        const std::filesystem::path extra_path = ResolveBundledDataFile(dll_dir, L"wubi86_jidian_extra.txt");
        if (std::filesystem::exists(extra_path)) {
            loaded_any |= engine_.LoadDictionaryFile(extra_path);
        }
    }

    if (!config_.user_dict_path.empty()) {
        std::filesystem::path user_path(config_.user_dict_path);
        if (user_path.is_relative()) {
            user_path = ResolveBundledDataFile(dll_dir, config_.user_dict_path);
        }
        if (std::filesystem::exists(user_path)) {
            loaded_any |= engine_.LoadDictionaryFile(user_path);
        } else {
            QIQI_LOG_WARN("Configured user dictionary '{}' does not exist",
                          log::WideToUtf8(user_path.wstring()));
        }
    } else {
        const std::filesystem::path default_user_dict = GetDefaultUserDictPath();
        if (std::filesystem::exists(default_user_dict)) {
            loaded_any |= engine_.LoadDictionaryFile(default_user_dict);
        }
    }

    if (!loaded_any && config_.primary_dict != PrimaryDict::BuiltinOnly) {
        QIQI_LOG_DEBUG("External dictionaries not found in '{}', using builtin dictionary only",
                       log::WideToUtf8(dll_dir.wstring()));
    } else {
        QIQI_LOG_INFO("Dictionary initialization complete: {} external dictionary file(s) active",
                      engine_.MappedDictionaryCount());
    }
}

STDMETHODIMP QiQiTextService::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfFunctionProvider)) {
        *ppvObj = static_cast<ITfFunctionProvider*>(this);
    } else if (IsEqualIID(riid, IID_ITfFunction) || IsEqualIID(riid, IID_ITfFnConfigure)) {
        *ppvObj = static_cast<ITfFnConfigure*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);
    } else {
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) QiQiTextService::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) QiQiTextService::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP QiQiTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP QiQiTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    if (!ptim) {
        return E_INVALIDARG;
    }
    thread_mgr_ = ptim;
    client_id_ = tid;
    activate_flags_ = dwFlags;

    const std::string host_exe = log::WideToUtf8(log::GetModuleFilePath(nullptr).filename().wstring());
    QIQI_LOG_INFO("QiQiTextService::ActivateEx(version={}, arch={}, host='{}', client_id={}, flags=0x{:x})",
                  version::kFullVersion, version::kArch, host_exe, tid, dwFlags);

    if (!InitThreadMgrEventSink() || !InitKeyEventSink()) {
        QIQI_LOG_ERROR("QiQiTextService::ActivateEx failed to initialize TSF event sinks");
        Deactivate();
        return E_FAIL;
    }
    InitDisplayAttributeGuidAtom();
    InitFunctionProvider();
    InitLangBarItem();
    if (config_.primary_dict != PrimaryDict::BuiltinOnly && engine_.MappedDictionaryCount() == 0) {
        LoadExternalDictionaryNextToDll();
    }
    SyncTsfCompartments();
    return S_OK;
}

STDMETHODIMP QiQiTextService::Deactivate() {
    QIQI_LOG_INFO("QiQiTextService::Deactivate(client_id={})", client_id_);
    ResetCompositionAndUI();
    if (status_bar_) {
        status_bar_->Show(false);
    }

    UninitLangBarItem();
    UninitFunctionProvider();
    UninitKeyEventSink();
    UninitThreadMgrEventSink();

    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    activate_flags_ = 0;
    engine_.ClearExternalDictionaries();
    WubiMappedDict::ClearProcessCache();
    log::Shutdown();
    return S_OK;
}

bool QiQiTextService::InitThreadMgrEventSink() {
    ComPtr<ITfSource> source;
    if (FAILED(thread_mgr_.As(&source)) || !source) {
        return false;
    }
    return SUCCEEDED(source->AdviseSink(
        IID_ITfThreadMgrEventSink,
        static_cast<ITfThreadMgrEventSink*>(this),
        &thread_mgr_event_cookie_));
}

void QiQiTextService::UninitThreadMgrEventSink() {
    if (thread_mgr_event_cookie_ == TF_INVALID_COOKIE || !thread_mgr_) {
        return;
    }
    ComPtr<ITfSource> source;
    if (SUCCEEDED(thread_mgr_.As(&source)) && source) {
        source->UnadviseSink(thread_mgr_event_cookie_);
    }
    thread_mgr_event_cookie_ = TF_INVALID_COOKIE;
}

bool QiQiTextService::InitKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (FAILED(thread_mgr_.As(&key_mgr)) || !key_mgr) {
        return false;
    }
    if (FAILED(key_mgr->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE))) {
        return false;
    }

    for (const auto& def : kPreservedKeys) {
        const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
        key_mgr->PreserveKey(
            client_id_, *def.guid, &pk, def.desc, gsl::narrow_cast<ULONG>(wcslen(def.desc)));
    }
    return true;
}

void QiQiTextService::UninitKeyEventSink() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (SUCCEEDED(thread_mgr_.As(&key_mgr)) && key_mgr) {
        for (const auto& def : kPreservedKeys) {
            const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
            key_mgr->UnpreserveKey(*def.guid, &pk);
        }
        key_mgr->UnadviseKeyEventSink(client_id_);
    }
}

void QiQiTextService::InitDisplayAttributeGuidAtom() {
    display_attribute_atom_ = TF_INVALID_GUIDATOM;
    ComPtr<ITfCategoryMgr> cat_mgr;
    if (const HMODULE msctf = ::GetModuleHandleW(L"msctf.dll")) {
        using TfCreateCategoryMgrFn = HRESULT(WINAPI*)(ITfCategoryMgr**);
        const auto fn = reinterpret_cast<TfCreateCategoryMgrFn>(
            ::GetProcAddress(msctf, "TF_CreateCategoryMgr"));
        if (fn) {
            std::ignore = fn(&cat_mgr);
        }
    }
    if (!cat_mgr) {
        std::ignore = ::CoCreateInstance(
            CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cat_mgr));
    }
    if (cat_mgr) {
        cat_mgr->RegisterGUID(kGuidDisplayAttributeInput, &display_attribute_atom_);
    }
}

void QiQiTextService::InitFunctionProvider() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(&source_single)) && source_single) {
        ITfFunctionProvider* const provider = this;
        source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, provider);
    }
}

void QiQiTextService::UninitFunctionProvider() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(&source_single)) && source_single) {
        source_single->UnadviseSingleSink(client_id_, IID_ITfFunctionProvider);
    }
}

void QiQiTextService::InitLangBarItem() {
    if (!thread_mgr_ || !langbar_btn_) {
        return;
    }
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    if (SUCCEEDED(thread_mgr_.As(&lb_mgr)) && lb_mgr) {
        lb_mgr->AddItem(langbar_btn_.Get());
    }
}

void QiQiTextService::UninitLangBarItem() {
    if (!thread_mgr_ || !langbar_btn_) {
        return;
    }
    ComPtr<ITfLangBarItemMgr> lb_mgr;
    if (SUCCEEDED(thread_mgr_.As(&lb_mgr)) && lb_mgr) {
        lb_mgr->RemoveItem(langbar_btn_.Get());
    }
}

void QiQiTextService::HandleLangBarMenuAction(LangBarMenuId action_id) {
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
                config_.primary_dict = PrimaryDict::Jidian;
                SaveConfigFile(GetDefaultUserConfigPath(), config_);
                LoadExternalDictionaryNextToDll();
                SyncTsfCompartments();
            }
            break;
        case LangBarMenuId::DictHaifeng:
            if (config_.primary_dict != PrimaryDict::Haifeng) {
                config_.primary_dict = PrimaryDict::Haifeng;
                SaveConfigFile(GetDefaultUserConfigPath(), config_);
                LoadExternalDictionaryNextToDll();
                SyncTsfCompartments();
            }
            break;
        case LangBarMenuId::ToggleExtraDict:
            if (config_.primary_dict == PrimaryDict::Jidian) {
                config_.enable_jidian_extra = !config_.enable_jidian_extra;
                SaveConfigFile(GetDefaultUserConfigPath(), config_);
                LoadExternalDictionaryNextToDll();
                SyncTsfCompartments();
            }
            break;
        case LangBarMenuId::ToggleStatusBar:
            config_.show_status_bar = !config_.show_status_bar;
            SaveConfigFile(GetDefaultUserConfigPath(), config_);
            SyncTsfCompartments();
            break;
        case LangBarMenuId::ToggleUiLanguage:
            config_.ui_language = (ResolveUiLanguage(config_.ui_language) == UiLanguage::EnUs)
                ? UiLanguage::ZhCn
                : UiLanguage::EnUs;
            SaveConfigFile(GetDefaultUserConfigPath(), config_);
            SyncTsfCompartments();
            break;
        case LangBarMenuId::OpenSettings:
            Show(nullptr, kChineseSimplifiedLangId, kGuidProfileWubi86);
            break;
        case LangBarMenuId::OpenConfigFile: {
            const auto cfg_path = GetDefaultUserConfigPath();
            EnsureDefaultConfigFile(cfg_path);
            ::ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case LangBarMenuId::OpenAbout:
            ShowAboutDialog(g_dll_instance, nullptr, config_.ui_language);
            break;
    }
}

// ITfThreadMgrEventSink
STDMETHODIMP QiQiTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* /*pdimPrevFocus*/) {
    if (!pdimFocus) {
        ResetCompositionAndUI();
    } else {
        ReloadConfigIfModified();
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPushContext(ITfContext* /*pic*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPopContext(ITfContext* /*pic*/) noexcept {
    return S_OK;
}

// ITfKeyEventSink
STDMETHODIMP QiQiTextService::OnSetFocus(BOOL fForeground) {
    if (!fForeground) {
        ResetCompositionAndUI();
    }
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    if (vk == VK_SHIFT) {
        shift_tap_candidate_ = config_.shift_toggle_mode && !IsKeyDown(VK_CONTROL) && !IsKeyDown(VK_MENU);
        *pfEaten = FALSE;
        return S_OK;
    }
    shift_tap_candidate_ = false;

    *pfEaten = engine_.ShouldEatKey(vk, IsKeyDown(VK_SHIFT), IsKeyDown(VK_CONTROL), IsKeyDown(VK_MENU))
                   ? TRUE
                   : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    const uint32_t vk = gsl::narrow_cast<uint32_t>(wParam);
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);

    if (!engine_.ShouldEatKey(vk, shift, ctrl, alt)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = TRUE;
    const KeyProcessResult result = engine_.ProcessKey(vk, shift, ctrl, alt);
    if (config_.log_keystrokes) {
        std::string cand_preview;
        for (size_t i = 0; i < result.page_candidates.size(); ++i) {
            if (i > 0) {
                cand_preview += ", ";
            }
            cand_preview += std::format("{}:{}", i + 1, log::WideToUtf8(gsl::at(result.page_candidates, i)));
        }
        QIQI_LOG_INFO(
            "[KeyTrace] vk={} shift={} ctrl={} alt={} -> action={} raw='{}' commit='{}' "
            "total_cands={} page={}/{} page_cands=[{}] dict={} mapped_dicts={}",
            FormatVirtualKey(vk),
            shift,
            ctrl,
            alt,
            KeyActionTypeToString(result.action),
            log::WideToUtf8(result.composition_text),
            log::WideToUtf8(result.committed_text),
            engine_.AllCandidates().size(),
            result.current_page + (result.total_pages > 0 ? 1 : 0),
            result.total_pages,
            cand_preview,
            PrimaryDictToDiagName(config_.primary_dict),
            engine_.MappedDictionaryCount());
        log::Flush();
    }
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
    *pfEaten = (wParam == VK_SHIFT && shift_tap_candidate_) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) {
        return E_INVALIDARG;
    }
    if (wParam == VK_SHIFT && shift_tap_candidate_) {
        shift_tap_candidate_ = false;
        *pfEaten = TRUE;
        ToggleModeAndCancelComposition(pic);
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
                        PlayImeSound(SoundEffectKind::KeyClick);
                    }
                }
            }
        }
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

// ITfCompositionSink: called when the host application forcefully terminates composition
// (e.g. user clicked elsewhere, switched Excel cell, or DOM re-rendered)
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) {
    QIQI_LOG_DEBUG("Host terminated active TSF composition");
    ResetCompositionAndUI();
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

// ITfFnConfigure
STDMETHODIMP QiQiTextService::GetDisplayName(BSTR* pbstrName) noexcept {
    const auto& str = GetUiStrings(config_.ui_language);
    return CopyToBstr(str.configure_display_name, pbstrName);
}

STDMETHODIMP QiQiTextService::Show(HWND hwndParent, LANGID /*langid*/, REFGUID /*rguidProfile*/) {
    QIQI_LOG_INFO("Opening Settings dialog");
    if (ShowSettingsDialog(g_dll_instance, hwndParent, config_)) {
        SaveConfigFile(GetDefaultUserConfigPath(), config_);
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
        LoadExternalDictionaryNextToDll();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Settings dialog saved and applied");
    } else {
        QIQI_LOG_DEBUG("Settings dialog cancelled");
    }
    return S_OK;
}

// ITfDisplayAttributeProvider
STDMETHODIMP QiQiTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) noexcept {
    if (!ppEnum) {
        return E_INVALIDARG;
    }
    *ppEnum = new (std::nothrow) QiQiEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP QiQiTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) noexcept {
    if (!ppInfo) {
        return E_INVALIDARG;
    }
    if (!IsEqualGUID(guid, kGuidDisplayAttributeInput)) {
        *ppInfo = nullptr;
        return E_INVALIDARG;
    }
    *ppInfo = new (std::nothrow) QiQiDisplayAttributeInfo();
    return *ppInfo ? S_OK : E_OUTOFMEMORY;
}

void QiQiTextService::ResetCompositionAndUI() {
    composition_.Reset();
    active_context_.Reset();
    engine_.Reset();
    EndCandidateUI();
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

void QiQiTextService::SyncTsfCompartments() {
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

    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfCompartmentMgr> comp_mgr;
    if (FAILED(thread_mgr_.As(&comp_mgr)) || !comp_mgr) {
        return;
    }

    auto set_dword = [&](REFGUID guid, LONG val) {
        ComPtr<ITfCompartment> comp;
        if (SUCCEEDED(comp_mgr->GetCompartment(guid, &comp)) && comp) {
            const VARIANT var = MakeInt4Variant(val);
            comp->SetValue(client_id_, &var);
        }
    };

    set_dword(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, engine_.IsChineseMode() ? 1 : 0);

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
    set_dword(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, conv_mode);
}

void QiQiTextService::ReloadConfigIfModified() {
    std::error_code ec;
    const std::filesystem::path user_cfg = GetDefaultUserConfigPath();
    if (!std::filesystem::exists(user_cfg, ec)) {
        return;
    }
    const auto write_time = std::filesystem::last_write_time(user_cfg, ec);
    if (ec || write_time == config_last_write_time_) {
        return;
    }

    QiQiConfig updated{};
    if (LoadConfigFile(user_cfg, updated)) {
        const bool dict_changed =
            (updated.primary_dict != config_.primary_dict) ||
            (updated.enable_jidian_extra != config_.enable_jidian_extra) ||
            (updated.user_dict_path != config_.user_dict_path);
        config_last_write_time_ = write_time;
        config_ = std::move(updated);
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
        if (dict_changed || (config_.primary_dict != PrimaryDict::BuiltinOnly && engine_.MappedDictionaryCount() == 0)) {
            LoadExternalDictionaryNextToDll();
        }
        SyncTsfCompartments();
        QIQI_LOG_INFO("Hot-reloaded modified user configuration from '{}' (dict={}, mapped_dicts={}, log_keystrokes={})",
                      log::WideToUtf8(user_cfg.wstring()),
                      PrimaryDictToDiagName(config_.primary_dict),
                      engine_.MappedDictionaryCount(),
                      config_.log_keystrokes);
    }
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context) {
        return;
    }
    active_context_ = context;

    ComPtr<ITfContext> ctx_holder(context);
    ComPtr<QiQiTextService> self_holder(this);

    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder, result](TfEditCookie ec) -> HRESULT {
        const gsl::not_null<ITfContext*> non_null_ctx(ctx_holder.Get());
        if (result.action == KeyActionType::Cancel) {
            self_holder->CancelCompositionInSession(ec, non_null_ctx.get());
            self_holder->EndCandidateUI();
            return S_OK;
        }

        if (result.action == KeyActionType::Commit) {
            self_holder->CommitAndEndCompositionInSession(ec, non_null_ctx, result.committed_text);
            if (!result.composition_text.empty()) {
                // e.g. 5th key auto-committed previous 4-code word and started a new 1-code composition
                self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result.composition_text);
            } else {
                self_holder->EndCandidateUI();
            }
            return S_OK;
        }

        if (result.action == KeyActionType::UpdateState) {
            self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result.composition_text);
            return S_OK;
        }

        return S_OK;
    });

    HRESULT session_hr = S_OK;
    const HRESULT req_hr = context->RequestEditSession(
        client_id_,
        session.Get(),
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &session_hr);
    if (FAILED(req_hr) || FAILED(session_hr)) {
        QIQI_LOG_WARN("RequestEditSession failed (req_hr=0x{:08x}, session_hr=0x{:08x})",
                      gsl::narrow_cast<unsigned int>(req_hr),
                      gsl::narrow_cast<unsigned int>(session_hr));
    }
}

void QiQiTextService::SyncCompositionUIInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    StartOrUpdateCompositionInSession(ec, context, text);
    UpdateCandidateWindowPositionInSession(ec, context);
    BeginOrUpdateCandidateUI();
}

void QiQiTextService::SetRangeTextAndCollapseCaret(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text) {
    range->SetText(ec, 0, text.data(), gsl::narrow_cast<LONG>(text.size()));

    ComPtr<ITfRange> caret_range;
    if (SUCCEEDED(range->Clone(&caret_range)) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel{};
        sel.range = caret_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        context->SetSelection(ec, 1, &sel);
    }
}

void QiQiTextService::StartOrUpdateCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text) {
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) || !insert_at_sel) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: QueryInterface(ITfInsertAtSelection) failed");
            return;
        }
        ComPtr<ITfRange> insert_range;
        if (FAILED(insert_at_sel->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &insert_range)) || !insert_range) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: InsertTextAtSelection(TF_IAS_QUERYONLY) failed");
            return;
        }

        ComPtr<ITfContextComposition> ctx_comp;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&ctx_comp))) || !ctx_comp) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: QueryInterface(ITfContextComposition) failed");
            return;
        }
        if (FAILED(ctx_comp->StartComposition(
                ec, insert_range.Get(), static_cast<ITfCompositionSink*>(this), &composition_)) ||
            !composition_) {
            QIQI_LOG_WARN("StartOrUpdateCompositionInSession: StartComposition failed");
            return;
        }
    }

    ComPtr<ITfRange> comp_range;
    if (FAILED(composition_->GetRange(&comp_range)) || !comp_range) {
        QIQI_LOG_WARN("StartOrUpdateCompositionInSession: composition_->GetRange failed");
        return;
    }
    SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), text);

    // Apply dotted underline display attribute to the composition range
    if (display_attribute_atom_ != TF_INVALID_GUIDATOM) {
        ComPtr<ITfProperty> disp_prop;
        if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop)) && disp_prop) {
            const VARIANT var = MakeInt4Variant(gsl::narrow_cast<LONG>(display_attribute_atom_));
            disp_prop->SetValue(ec, comp_range.Get(), &var);
        }
    }
}

void QiQiTextService::CommitAndEndCompositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text) {
    if (composition_) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
            ComPtr<ITfProperty> disp_prop;
            if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &disp_prop)) && disp_prop) {
                disp_prop->Clear(ec, comp_range.Get());
            }
            SetRangeTextAndCollapseCaret(ec, context, gsl::not_null<ITfRange*>(comp_range.Get()), committed_text);
        }
        composition_->EndComposition(ec);
        composition_.Reset();
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) && insert_at_sel) {
            ComPtr<ITfRange> inserted_range;
            insert_at_sel->InsertTextAtSelection(
                ec, 0, committed_text.c_str(), gsl::narrow_cast<LONG>(committed_text.size()), &inserted_range);
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, ITfContext* /*context*/) {
    if (!composition_) {
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
        comp_range->SetText(ec, 0, L"", 0);
    }
    composition_->EndComposition(ec);
    composition_.Reset();
}

void QiQiTextService::UpdateCandidateWindowPositionInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context) {
    if (!composition_ || !candidate_ui_) {
        return;
    }
    ComPtr<ITfContextView> view;
    if (FAILED(context->GetActiveView(&view)) || !view) {
        return;
    }
    ComPtr<ITfRange> comp_range;
    if (FAILED(composition_->GetRange(&comp_range)) || !comp_range) {
        return;
    }
    RECT rc{};
    BOOL clipped = FALSE;
    if (SUCCEEDED(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped))) {
        candidate_ui_->MoveWindowTo(rc);
    }
}

void QiQiTextService::BeginOrUpdateCandidateUI() {
    if (!candidate_ui_ || !thread_mgr_) {
        return;
    }

    candidate_ui_->SetCandidates(
        engine_.RawCode(),
        engine_.AllDisplayCandidates(),
        engine_.PageSize(),
        engine_.CurrentPage(),
        engine_.SelectedInPage());

    ComPtr<ITfUIElementMgr> ui_mgr;
    if (FAILED(thread_mgr_.As(&ui_mgr)) || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        candidate_ui_->Show(TRUE);
        return;
    }

    if (candidate_ui_->UiElementId() == TF_INVALID_UIELEMENTID) {
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        if (SUCCEEDED(ui_mgr->BeginUIElement(candidate_ui_.Get(), &show_own_ui, &element_id))) {
            candidate_ui_->SetUiElementId(element_id);
            // If host game/app returned FALSE in show_own_ui (UILess mode), do not show popup window
            const bool force_uiless = (activate_flags_ & TF_TMAE_UIELEMENTENABLEDONLY) != 0 && !show_own_ui;
            candidate_ui_->Show((show_own_ui && !force_uiless) ? TRUE : FALSE);
            ui_mgr->UpdateUIElement(element_id);
        } else {
            candidate_ui_->Show(TRUE);
        }
    } else {
        ui_mgr->UpdateUIElement(candidate_ui_->UiElementId());
    }
}

void QiQiTextService::EndCandidateUI() {
    if (!candidate_ui_) {
        return;
    }
    if (candidate_ui_->UiElementId() != TF_INVALID_UIELEMENTID && thread_mgr_) {
        ComPtr<ITfUIElementMgr> ui_mgr;
        if (SUCCEEDED(thread_mgr_.As(&ui_mgr)) && ui_mgr) {
            ui_mgr->EndUIElement(candidate_ui_->UiElementId());
        }
        candidate_ui_->SetUiElementId(TF_INVALID_UIELEMENTID);
    }
    candidate_ui_->Show(FALSE);
}

} // namespace qiqi
