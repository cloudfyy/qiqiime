#include "Ime/tsf/tip_service.h"
#include "Ime/tsf/display_attribute.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/version.h"

#include <oleauto.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <string>

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

} // namespace

QiQiTextService::QiQiTextService(HINSTANCE instance) {
    std::ignore = log::EnsureInitialized();
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
    candidate_ui_->SetPageTurnCallback([this](bool next_page) {
        if (active_context_ && engine_.IsComposing()) {
            const KeyProcessResult res = engine_.ProcessKey(
                next_page ? VK_NEXT : VK_PRIOR, false, false, false);
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
        QIQI_LOG_ERROR("QiQiTextService destructor UI cleanup failed with a C++ exception");
    }
}

STDMETHODIMP QiQiTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP QiQiTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    if (!ptim) {
        return E_INVALIDARG;
    }
    is_active_ = false;
    ++edit_session_epoch_.activation;
    std::ignore = log::EnsureInitialized();
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
    RegisterCompositionDisplayAttributeAtom();
    InitFunctionProvider();
    InitLangBarItem();
    InitCompartmentSinks();
    if (config_.primary_dict != PrimaryDict::BuiltinOnly && engine_.MappedDictionaryCount() == 0) {
        LoadExternalDictionaryNextToDll();
    }
    is_active_ = true;
    SyncTsfCompartments();
    return S_OK;
}

STDMETHODIMP QiQiTextService::Deactivate() {
    is_active_ = false;
    ++edit_session_epoch_.activation;
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
    return S_OK;
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
    ITfKeyEventSink* sink = this;
    ComPtr<ITfKeystrokeMgr> key_mgr;
    if (!log::CheckHResult(thread_mgr_.As(&key_mgr),
                          "InitKeyEventSink::QueryInterface(ITfKeystrokeMgr)", log::Level::err) || !key_mgr) {
        return false;
    }
    const HRESULT hr = key_mgr->AdviseKeyEventSink(client_id_, sink, TRUE);
    if (FAILED(hr)) {
        QIQI_LOG_ERROR("InitKeyEventSink: AdviseKeyEventSink failed (hr=0x{:08x})",
                       gsl::narrow_cast<unsigned int>(hr));
        return false;
    }

    for (const auto& def : kPreservedKeys) {
        const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
        const HRESULT pk_hr = key_mgr->PreserveKey(
            client_id_, *def.guid, &pk, def.desc, gsl::narrow_cast<ULONG>(std::wcslen(def.desc)));
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
    if (log::CheckHResult(thread_mgr_.As(&key_mgr), "UninitKeyEventSink::QueryInterface(ITfKeystrokeMgr)") && key_mgr) {
        for (const auto& def : kPreservedKeys) {
            const TF_PRESERVEDKEY pk{def.vkey, def.modifiers};
            std::ignore = log::CheckHResult(key_mgr->UnpreserveKey(*def.guid, &pk),
                                           "ITfKeystrokeMgr::UnpreserveKey");
        }
        std::ignore = log::CheckHResult(key_mgr->UnadviseKeyEventSink(client_id_),
                                       "ITfKeystrokeMgr::UnadviseKeyEventSink");
    }
}

void QiQiTextService::RegisterCompositionDisplayAttributeAtom() {
    display_attribute_atom_ = TF_INVALID_GUIDATOM;
    ComPtr<ITfCategoryMgr> cat_mgr;
    if (const HMODULE msctf = ::GetModuleHandleW(L"msctf.dll")) {
        using TfCreateCategoryMgrFn = HRESULT(WINAPI*)(ITfCategoryMgr**);
        const auto fn = reinterpret_cast<TfCreateCategoryMgrFn>(
            ::GetProcAddress(msctf, "TF_CreateCategoryMgr"));
        if (fn) {
            std::ignore = log::CheckHResult(fn(&cat_mgr), "TF_CreateCategoryMgr; trying COM fallback",
                                           log::Level::debug);
        }
    }
    if (!cat_mgr) {
        std::ignore = log::CheckHResult(::CoCreateInstance(
            CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cat_mgr)),
            "RegisterCompositionDisplayAttributeAtom::CoCreateInstance(ITfCategoryMgr)");
    }
    if (cat_mgr) {
        std::ignore = log::CheckHResult(cat_mgr->RegisterGUID(kGuidDisplayAttributeInput, &display_attribute_atom_),
                                       "RegisterCompositionDisplayAttributeAtom::RegisterGUID");
    } else {
        QIQI_LOG_WARN("RegisterCompositionDisplayAttributeAtom: ITfCategoryMgr unavailable");
    }
}

void QiQiTextService::InitFunctionProvider() {
    if (!thread_mgr_ || client_id_ == TF_CLIENTID_NULL) {
        return;
    }
    ComPtr<ITfSourceSingle> source_single;
    if (log::CheckHResult(thread_mgr_.As(&source_single),
                         "InitFunctionProvider::QueryInterface(ITfSourceSingle)") && source_single) {
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
    if (log::CheckHResult(thread_mgr_.As(&lb_mgr), "InitLangBarItem::QueryInterface(ITfLangBarItemMgr)") && lb_mgr) {
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

STDMETHODIMP QiQiTextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pdim*/) noexcept {
    return S_OK;
}

void QiQiTextService::HandleFocusChanged(bool has_focus, FocusNotificationSource source) {
    using Clock = std::chrono::steady_clock;
    static std::atomic<uint64_t> focus_log_sequence{0};
    const bool measure = log::GetLevel() <= log::Level::debug && log::IsLogSinkActive();
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
        if (measure) {
            const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
            QIQI_LOG_DEBUG("FocusPerformance(id={}, source={}, has_focus={}, same_state={}, reload_checked={}, "
                           "reload_ns={}, position_ns={}, total_ns={}, position_changed={}, completed={})",
                           event_id, source == FocusNotificationSource::ThreadManager ? "thread-manager" : "key-event",
                           has_focus, same_state, has_focus, reload_ns, position_ns, total_ns, position_changed, completed);
        }
    });
    if (!has_focus) {
        ResetCompositionAndUI();
        if (status_bar_) {
            status_bar_->Show(false);
        }
    } else {
        const auto reload_started = measure ? Clock::now() : Clock::time_point{};
        ReloadConfigIfModified(event_id);
        const auto reload_finished = measure ? Clock::now() : Clock::time_point{};
        position_changed = SyncStatusBarPositionFromGlobalCompartment();
        if (measure) {
            reload_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(reload_finished - reload_started).count();
            position_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - reload_finished).count();
        }
        if (status_bar_) {
            status_bar_->Show(config_.show_status_bar);
        }
    }
    completed = true;
}

STDMETHODIMP QiQiTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* /*pdimPrevFocus*/) {
    HandleFocusChanged(pdimFocus != nullptr, FocusNotificationSource::ThreadManager);
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPushContext(ITfContext* /*pic*/) noexcept {
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPopContext(ITfContext* /*pic*/) noexcept {
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
