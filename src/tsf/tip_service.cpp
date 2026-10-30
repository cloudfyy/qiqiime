#include "tsf/tip_service.h"
#include "tsf/display_attribute.h"
#include "tsf/edit_session.h"
#include "ui/settings_dialog.h"
#include "common/file_util.h"
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

bool IsAnyMouseButtonDown() noexcept {
    return IsKeyDown(VK_LBUTTON) || IsKeyDown(VK_RBUTTON) || IsKeyDown(VK_MBUTTON);
}

constexpr ULONGLONG kMaxShiftTapDurationMs = 350;

bool QueryGuiThreadOrCursorFallbackRect(RECT& out_rc) noexcept {
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti)) {
        if (gti.hwndCaret && (gti.rcCaret.right > gti.rcCaret.left || gti.rcCaret.bottom > gti.rcCaret.top)) {
            POINT top_left{gti.rcCaret.left, gti.rcCaret.top};
            POINT bottom_right{gti.rcCaret.right, gti.rcCaret.bottom};
            if (::ClientToScreen(gti.hwndCaret, &top_left) && ::ClientToScreen(gti.hwndCaret, &bottom_right)) {
                out_rc = RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
                return true;
            }
        }
        if (gti.hwndFocus) {
            RECT focus_rc{};
            if (::GetWindowRect(gti.hwndFocus, &focus_rc)) {
                out_rc = RECT{focus_rc.left + 20, focus_rc.top + 24, focus_rc.left + 22, focus_rc.top + 44};
                return true;
            }
        }
    }
    POINT cursor_pt{};
    if (::GetCursorPos(&cursor_pt)) {
        out_rc = RECT{cursor_pt.x, cursor_pt.y, cursor_pt.x + 2, cursor_pt.y + 20};
        return true;
    }
    return false;
}

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

constexpr const char* LangBarMenuIdToString(LangBarMenuId id) noexcept {
    switch (id) {
        case LangBarMenuId::SyncMenuState:     return "SyncMenuState";
        case LangBarMenuId::ToggleChinese:     return "ToggleChinese";
        case LangBarMenuId::ToggleFullWidth:   return "ToggleFullWidth";
        case LangBarMenuId::TogglePunctuation: return "TogglePunctuation";
        case LangBarMenuId::DictJidian:        return "DictJidian";
        case LangBarMenuId::DictHaifeng:       return "DictHaifeng";
        case LangBarMenuId::ToggleExtraDict:   return "ToggleExtraDict";
        case LangBarMenuId::ToggleStatusBar:    return "ToggleStatusBar";
        case LangBarMenuId::OpenSettings:       return "OpenSettings";
        case LangBarMenuId::ToggleUiLanguage:   return "ToggleUiLanguage";
        case LangBarMenuId::OpenAbout:          return "OpenAbout";
        default:                                return "Unknown";
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

QiQiTextService::QiQiTextService(HINSTANCE instance) {
    config_ = LoadConfig(instance);
    config_registry_sequence_ = QueryRegistryConfigSequence();
    log::SetLevel(config_.log_level);
    engine_.ApplyConfig(config_);
    engine_.SetPhraseFreqPath(GetDefaultPhraseFreqPath());
    std::ignore = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath());
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
    status_bar_->SetPositionChangedCallback([this](POINT new_pos) {
        SaveGlobalStatusBarPosition(new_pos);
    });
    LoadExternalDictionaryNextToDll();
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
}

void QiQiTextService::LoadExternalDictionaryNextToDll() {
    engine_.ClearExternalDictionaries();
    const std::filesystem::path module_path = GetModuleFilePath(g_dll_instance);
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

    const std::string host_exe = WideToUtf8(GetModuleFilePath(nullptr).filename().wstring());
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
    InitCompartmentSinks();
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

    UninitCompartmentSinks();
    UninitLangBarItem();
    UninitFunctionProvider();
    UninitKeyEventSink();
    thread_mgr_sink_.Reset();

    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    activate_flags_ = 0;
    engine_.ClearExternalDictionaries();
    WubiMappedDict::ClearProcessCache();
    log::Shutdown();
    return S_OK;
}

bool QiQiTextService::InitThreadMgrEventSink() {
    if (!thread_mgr_sink_.Advise(
            thread_mgr_.Get(),
            IID_ITfThreadMgrEventSink,
            static_cast<ITfThreadMgrEventSink*>(this))) {
        QIQI_LOG_ERROR("InitThreadMgrEventSink: Advise(IID_ITfThreadMgrEventSink) failed");
        return false;
    }
    return true;
}

bool QiQiTextService::InitKeyEventSink() {
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (FAILED(thread_mgr_.As(&key_mgr)) || !key_mgr) {
        QIQI_LOG_ERROR("InitKeyEventSink: QueryInterface(ITfKeystrokeMgr) failed");
        return false;
    }
    const HRESULT hr = key_mgr->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
    if (FAILED(hr)) {
        QIQI_LOG_ERROR("InitKeyEventSink: AdviseKeyEventSink failed (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(hr));
        return false;
    }

    for (const auto& def : kPreservedKeys) {
        const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
        const HRESULT pk_hr = key_mgr->PreserveKey(
            client_id_, *def.guid, &pk, def.desc, gsl::narrow_cast<ULONG>(wcslen(def.desc)));
        if (FAILED(pk_hr)) {
            QIQI_LOG_WARN("InitKeyEventSink: PreserveKey(vkey=0x{:02X}, mod=0x{:X}) failed (hr=0x{:08x})",
                          def.vkey, def.modifiers, gsl::narrow_cast<unsigned int>(pk_hr));
        }
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
        if (FAILED(cat_mgr->RegisterGUID(kGuidDisplayAttributeInput, &display_attribute_atom_))) {
            QIQI_LOG_WARN("InitDisplayAttributeGuidAtom: RegisterGUID failed");
        }
    } else {
        QIQI_LOG_WARN("InitDisplayAttributeGuidAtom: ITfCategoryMgr unavailable");
    }
}

void QiQiTextService::InitFunctionProvider() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(&source_single)) && source_single) {
        ITfFunctionProvider* const provider = this;
        if (FAILED(source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, provider))) {
            QIQI_LOG_WARN("InitFunctionProvider: AdviseSingleSink(IID_ITfFunctionProvider) failed");
        }
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
        if (FAILED(lb_mgr->AddItem(langbar_btn_.Get()))) {
            QIQI_LOG_WARN("InitLangBarItem: ITfLangBarItemMgr::AddItem failed");
        }
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

void QiQiTextService::InitCompartmentSinks() {
    if (!thread_mgr_) {
        return;
    }
    ITfCompartmentEventSink* const sink = this;

    // 1. Bind thread-scoped system compartments for bidirectional mode sync
    ComPtr<ITfCompartmentMgr> thread_comp_mgr;
    if (SUCCEEDED(thread_mgr_.As(&thread_comp_mgr)) && thread_comp_mgr) {
        if (!openclose_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_OPENCLOSE");
        }
        if (!convmode_comp_.Bind(thread_comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, sink)) {
            QIQI_LOG_WARN("InitCompartmentSinks: failed to bind GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION");
        }
    }

    // 2. Bind process-wide TSF Global Compartments for config/dict broadcast & status bar position
    ComPtr<ITfCompartmentMgr> global_mgr;
    if (FAILED(thread_mgr_->GetGlobalCompartment(&global_mgr)) || !global_mgr) {
        QIQI_LOG_DEBUG("InitCompartmentSinks: GetGlobalCompartment unavailable");
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

void QiQiTextService::UninitCompartmentSinks() {
    convmode_comp_.Reset();
    openclose_comp_.Reset();
    global_pos_comp_.Reset();
    global_config_comp_.Reset();
}

void QiQiTextService::NotifyGlobalConfigChanged() {
    if (!global_config_comp_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    const LONG next_version = global_config_comp_.ReadInt4(0) + 1;
    if (FAILED(global_config_comp_.WriteInt4(client_id_, next_version))) {
        QIQI_LOG_WARN("NotifyGlobalConfigChanged: SetValue(version={}) failed", next_version);
    } else {
        QIQI_LOG_DEBUG("NotifyGlobalConfigChanged: broadcast Global Compartment config version={}", next_version);
    }
}

void QiQiTextService::SaveGlobalStatusBarPosition(POINT pos) {
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

void QiQiTextService::SyncStatusBarPositionFromGlobalCompartment() {
    if (!global_pos_comp_ || !status_bar_) {
        return;
    }
    constexpr LONG kUnsetSentinel = INT32_MIN;
    const LONG packed = global_pos_comp_.ReadInt4(kUnsetSentinel);
    if (packed == kUnsetSentinel || packed == 0) {
        return;
    }
    const POINT shared_pos = UnpackLongToScreenPoint(packed);
    if (shared_pos.x != status_bar_->Position().x || shared_pos.y != status_bar_->Position().y) {
        status_bar_->SetPosition(shared_pos);
        QIQI_LOG_DEBUG("SyncStatusBarPositionFromGlobalCompartment: synced pos=({}, {})",
                       shared_pos.x, shared_pos.y);
    }
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
                config_.primary_dict = PrimaryDict::Jidian;
                PersistAndSyncQuickConfigChange(true);
            }
            break;
        case LangBarMenuId::DictHaifeng:
            if (config_.primary_dict != PrimaryDict::Haifeng) {
                config_.primary_dict = PrimaryDict::Haifeng;
                PersistAndSyncQuickConfigChange(true);
            }
            break;
        case LangBarMenuId::ToggleExtraDict:
            if (config_.primary_dict == PrimaryDict::Jidian) {
                config_.enable_jidian_extra = !config_.enable_jidian_extra;
                PersistAndSyncQuickConfigChange(true);
            }
            break;
        case LangBarMenuId::ToggleStatusBar:
            config_.show_status_bar = !config_.show_status_bar;
            PersistAndSyncQuickConfigChange(false);
            break;
        case LangBarMenuId::ToggleUiLanguage:
            config_.ui_language = (ResolveUiLanguage(config_.ui_language) == UiLanguage::EnUs)
                ? UiLanguage::ZhCn
                : UiLanguage::EnUs;
            PersistAndSyncQuickConfigChange(false);
            break;
        case LangBarMenuId::OpenSettings:
            Show(nullptr, kChineseSimplifiedLangId, kGuidProfileWubi86);
            break;
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

void QiQiTextService::HandleFocusChanged(bool has_focus) {
    if (!has_focus) {
        ResetCompositionAndUI();
        if (status_bar_) {
            status_bar_->Show(false);
        }
    } else {
        ReloadConfigIfModified();
        SyncStatusBarPositionFromGlobalCompartment();
        if (status_bar_) {
            status_bar_->Show(config_.show_status_bar);
        }
    }
}

STDMETHODIMP QiQiTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* /*pdimPrevFocus*/) {
    HandleFocusChanged(pdimFocus != nullptr);
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

// ITfCompositionSink: called when the host application forcefully terminates composition
// (e.g. user clicked elsewhere, switched Excel cell, or DOM re-rendered)
STDMETHODIMP QiQiTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) {
    QIQI_LOG_DEBUG("Host terminated active TSF composition");
    ResetCompositionAndUI();
    return S_OK;
}

// ITfTextLayoutSink: called when an async host (Chromium/VS Code/WPF/Word) updates its text layout
STDMETHODIMP QiQiTextService::OnLayoutChange(
    ITfContext* pic, TfLayoutCode lcode, ITfContextView* /*pView*/) {
    if (lcode != TF_LC_CHANGE || !pic || pic != active_context_.Get() || !composition_) {
        return S_OK;
    }
    ComPtr<ITfContext> ctx_holder(pic);
    ComPtr<QiQiTextService> self_holder(this);
    const ComPtr<LambdaEditSession> session = MakeComObject<LambdaEditSession>(
        [self_holder, ctx_holder](TfEditCookie ec) -> HRESULT {
            self_holder->UpdateCandidateWindowPositionInSession(
                ec, gsl::not_null<ITfContext*>(ctx_holder.Get()));
            return S_OK;
        });
    HRESULT session_hr = S_OK;
    std::ignore = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READ, &session_hr);
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
}

void QiQiTextService::PersistAndSyncQuickConfigChange(bool reload_dicts) {
    SaveConfig(config_);
    config_registry_sequence_ = QueryRegistryConfigSequence();
    if (reload_dicts) {
        LoadExternalDictionaryNextToDll();
    }
    SyncTsfCompartments();
    NotifyGlobalConfigChanged();
}

STDMETHODIMP QiQiTextService::Show(HWND hwndParent, LANGID /*langid*/, REFGUID /*rguidProfile*/) {
    QIQI_LOG_INFO("Opening Settings dialog");
    ReloadConfigIfModified();
    if (ShowSettingsDialog(g_dll_instance, hwndParent, config_)) {
        ApplyDynamicEngineSettings();
        PersistAndSyncQuickConfigChange(true);
        QIQI_LOG_INFO("Settings dialog saved and applied");
    } else {
        QIQI_LOG_DEBUG("Settings dialog cancelled");
    }
    return S_OK;
}

// ITfDisplayAttributeProvider
STDMETHODIMP QiQiTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) noexcept {
    return CreateComObjectToOut<QiQiEnumDisplayAttributeInfo>(ppEnum);
}

STDMETHODIMP QiQiTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) noexcept {
    if (!ppInfo) {
        return E_INVALIDARG;
    }
    if (!IsEqualGUID(guid, kGuidDisplayAttributeInput)) {
        *ppInfo = nullptr;
        return E_INVALIDARG;
    }
    return CreateComObjectToOut<QiQiDisplayAttributeInfo>(ppInfo);
}

void QiQiTextService::ResetCompositionAndUI() {
    shift_tap_candidate_ = false;
    shift_press_tick_ = 0;
    has_valid_caret_pos_ = false;
    text_layout_sink_.Reset();
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
    UpdateUiIndicators();

    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL || syncing_compartments_) {
        return;
    }
    syncing_compartments_ = true;
    const auto reset_guard = gsl::finally([this]() noexcept {
        syncing_compartments_ = false;
    });

    if (openclose_comp_) {
        openclose_comp_.WriteInt4(client_id_, engine_.IsChineseMode() ? 1 : 0);
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
        convmode_comp_.WriteInt4(client_id_, conv_mode);
    }
}

void QiQiTextService::ReloadConfigIfModified() {
    if (engine_.HasModifiedMappedDictionary()) {
        QIQI_LOG_INFO("Detected modified dictionary file on disk; hot-reloading external dictionaries");
        LoadExternalDictionaryNextToDll();
    }

    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == config_registry_sequence_) {
        return;
    }

    QiQiConfig updated{};
    if (LoadConfigFromRegistry(updated, &config_registry_sequence_)) {
        config_ = std::move(updated);
        ApplyDynamicEngineSettings();
        if (config_.dynamic_phrase_freq && !engine_.PhraseFreqPath().empty()) {
            std::ignore = engine_.LoadPhraseFrequencies(engine_.PhraseFreqPath());
        }
        LoadExternalDictionaryNextToDll();
        SyncTsfCompartments();
        QIQI_LOG_INFO("Hot-reloaded modified user configuration from HKCU\\Software\\QiQiIME (dict={}, mapped_dicts={}, log_keystrokes={})",
                      PrimaryDictToString(config_.primary_dict),
                      engine_.MappedDictionaryCount(),
                      config_.log_keystrokes);
    }
}

void QiQiTextService::ApplyEngineResult(ITfContext* context, const KeyProcessResult& result) {
    if (!context) {
        return;
    }
    if (active_context_.Get() != context) {
        text_layout_sink_.Reset();
        active_context_ = context;
        text_layout_sink_.Advise(
            active_context_.Get(), IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this));
    }

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
                self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result);
            } else {
                self_holder->EndCandidateUI();
            }
            return S_OK;
        }

        if (result.action == KeyActionType::UpdateState) {
            self_holder->SyncCompositionUIInSession(ec, non_null_ctx, result);
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
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot) {
    StartOrUpdateCompositionInSession(ec, context, snapshot.composition_text);
    UpdateCandidateWindowPositionInSession(ec, context);
    BeginOrUpdateCandidateUI(snapshot);
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
        has_valid_caret_pos_ = false;
    } else if (!committed_text.empty()) {
        // Direct commit without prior composition (e.g. Chinese punctuation)
        ComPtr<ITfInsertAtSelection> insert_at_sel;
        if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&insert_at_sel))) && insert_at_sel) {
            ComPtr<ITfRange> inserted_range;
            if (FAILED(insert_at_sel->InsertTextAtSelection(
                    ec, 0, committed_text.c_str(), gsl::narrow_cast<LONG>(committed_text.size()), &inserted_range))) {
                QIQI_LOG_WARN("CommitAndEndCompositionInSession: InsertTextAtSelection failed");
            }
        } else {
            QIQI_LOG_WARN("CommitAndEndCompositionInSession: QueryInterface(ITfInsertAtSelection) failed");
        }
    }
}

void QiQiTextService::CancelCompositionInSession(TfEditCookie ec, ITfContext* /*context*/) {
    has_valid_caret_pos_ = false;
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
    ComPtr<ITfRange> comp_range;
    RECT rc{};
    BOOL clipped = FALSE;
    if (SUCCEEDED(context->GetActiveView(&view)) && view &&
        SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range &&
        SUCCEEDED(view->GetTextExt(ec, comp_range.Get(), &rc, &clipped))) {
        has_valid_caret_pos_ = true;
        candidate_ui_->MoveWindowTo(rc);
        return;
    }
    if (!has_valid_caret_pos_ && QueryGuiThreadOrCursorFallbackRect(rc)) {
        QIQI_LOG_DEBUG("UpdateCandidateWindowPositionInSession: GetTextExt pending layout; using GUI/cursor fallback");
        candidate_ui_->MoveWindowTo(rc);
    }
}

void QiQiTextService::BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot) {
    if (!candidate_ui_ || !thread_mgr_) {
        return;
    }

    candidate_ui_->SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index);

    ComPtr<ITfUIElementMgr> ui_mgr;
    if (FAILED(thread_mgr_.As(&ui_mgr)) || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; showing popup directly");
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
            const bool show_popup = (show_own_ui && !force_uiless);
            QIQI_LOG_DEBUG("BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={})",
                           element_id, show_own_ui != FALSE, show_popup);
            candidate_ui_->Show(show_popup ? TRUE : FALSE);
            ui_mgr->UpdateUIElement(element_id);
        } else {
            QIQI_LOG_WARN("BeginOrUpdateCandidateUI: BeginUIElement failed; falling back to popup window");
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
