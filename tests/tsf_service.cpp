#include "common.h"
#include "tsf_mocks.h"
#include "common/guids.h"
#include "common/sound.h"
#include "tsf/tip_service.h"

namespace qiqi::test {

namespace {

void TestQiQiTextServiceLifecycleAndKeyFlow() {
    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    std::error_code appdata_ec;
    const auto temp_appdata = std::filesystem::temp_directory_path() / L"QiQiIME_TsfTestAppData";
    std::filesystem::remove_all(temp_appdata, appdata_ec);
    _wputenv_s(L"LOCALAPPDATA", temp_appdata.wstring().c_str());
    const auto restore_appdata = gsl::finally([&]() noexcept {
        if (orig_localappdata) {
            _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
            free(orig_localappdata);
        }
        std::error_code clean_ec;
        std::filesystem::remove_all(temp_appdata, clean_ec);
    });

    BYTE clean_kb_state[256]{};
    ::SetKeyboardState(clean_kb_state);
    SetMuteAudioOutputForTest(true);
    ResetLastPlayedSoundForTest();

    auto* service = new QiQiTextService(g_dll_instance);

    // QueryInterface coverage for all supported interfaces
    QIQI_TEST_CHECK(service->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(service->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE && out == nullptr);

    ComPtr<ITfTextInputProcessor> tip_base;
    ComPtr<ITfTextInputProcessorEx> tip_ex;
    ComPtr<ITfThreadMgrEventSink> tm_sink;
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    ComPtr<ITfFunctionProvider> func_prov;
    ComPtr<ITfFnConfigure> fn_cfg;
    ComPtr<ITfDisplayAttributeProvider> disp_attr_prov;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_base))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_ex))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&func_prov))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&fn_cfg))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&disp_attr_prov))));

    // Test ITfFunctionProvider & ITfFnConfigure
    QIQI_TEST_CHECK(func_prov->GetType(nullptr) == E_INVALIDARG);
    GUID fp_guid{};
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetType(&fp_guid)) && IsEqualGUID(fp_guid, kClsidQiQiTextService));
    QIQI_TEST_CHECK(func_prov->GetDescription(nullptr) == E_INVALIDARG);
    ScopedBstr fp_desc;
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetDescription(fp_desc.Put())) && fp_desc.IsValid());
    QIQI_TEST_CHECK(func_prov->GetFunction(GUID_NULL, IID_ITfFnConfigure, nullptr) == E_INVALIDARG);
    ComPtr<IUnknown> fn_unk;
    QIQI_TEST_CHECK(func_prov->GetFunction(kGuidProfileWubi86, IID_ITfFnConfigure, &fn_unk) == E_NOINTERFACE);
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetFunction(GUID_NULL, IID_ITfFnConfigure, &fn_unk)) && fn_unk);
    fn_unk.Reset();

    QIQI_TEST_CHECK(fn_cfg->GetDisplayName(nullptr) == E_INVALIDARG);
    ScopedBstr fn_name;
    QIQI_TEST_CHECK(SUCCEEDED(fn_cfg->GetDisplayName(fn_name.Put())) && fn_name.IsValid());
    func_prov.Reset();
    fn_cfg.Reset();

    // Test ITfDisplayAttributeProvider & IEnumTfDisplayAttributeInfo & ITfDisplayAttributeInfo
    QIQI_TEST_CHECK(disp_attr_prov->EnumDisplayAttributeInfo(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(disp_attr_prov->GetDisplayAttributeInfo(kGuidDisplayAttributeInput, nullptr) == E_INVALIDARG);
    ComPtr<ITfDisplayAttributeInfo> attr_info;
    QIQI_TEST_CHECK(disp_attr_prov->GetDisplayAttributeInfo(kGuidProfileWubi86, &attr_info) == E_INVALIDARG);
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->GetDisplayAttributeInfo(kGuidDisplayAttributeInput, &attr_info)) && attr_info);
    QIQI_TEST_CHECK(attr_info->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(attr_info->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);
    ComPtr<ITfDisplayAttributeInfo> attr_qi;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->QueryInterface(IID_PPV_ARGS(&attr_qi))) && attr_qi);
    attr_qi.Reset();
    QIQI_TEST_CHECK(attr_info->GetGUID(nullptr) == E_INVALIDARG);
    GUID attr_guid{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetGUID(&attr_guid)) && IsEqualGUID(attr_guid, kGuidDisplayAttributeInput));
    QIQI_TEST_CHECK(attr_info->GetDescription(nullptr) == E_INVALIDARG);
    ScopedBstr attr_desc;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetDescription(attr_desc.Put())) && attr_desc.IsValid());
    QIQI_TEST_CHECK(attr_info->GetAttributeInfo(nullptr) == E_INVALIDARG);
    TF_DISPLAYATTRIBUTE da{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetAttributeInfo(&da)) && da.lsStyle == TF_LS_DOT);
    QIQI_TEST_CHECK(attr_info->SetAttributeInfo(nullptr) == E_INVALIDARG);
    da.fBoldLine = TRUE;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->SetAttributeInfo(&da)));
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->Reset()));

    ComPtr<IEnumTfDisplayAttributeInfo> enum_attr;
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->EnumDisplayAttributeInfo(&enum_attr)) && enum_attr);
    QIQI_TEST_CHECK(enum_attr->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(enum_attr->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);
    ComPtr<IEnumTfDisplayAttributeInfo> enum_qi;
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->QueryInterface(IID_PPV_ARGS(&enum_qi))) && enum_qi);
    enum_qi.Reset();
    QIQI_TEST_CHECK(enum_attr->Clone(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(enum_attr->Next(1, nullptr, nullptr) == E_INVALIDARG);
    ComPtr<ITfDisplayAttributeInfo> fetched_info;
    ULONG fetched = 99;
    QIQI_TEST_CHECK(enum_attr->Next(0, &fetched_info, &fetched) == S_OK && fetched == 0);
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_OK && fetched == 1 && fetched_info);
    fetched_info.Reset();
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_FALSE && fetched == 0);
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Reset()));
    QIQI_TEST_CHECK(enum_attr->Next(2, &fetched_info, &fetched) == S_FALSE && fetched == 1 && fetched_info);
    fetched_info.Reset();
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Reset()));
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Skip(1)));
    QIQI_TEST_CHECK(enum_attr->Skip(1) == S_FALSE);
    ComPtr<IEnumTfDisplayAttributeInfo> cloned_enum;
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Clone(&cloned_enum)) && cloned_enum);
    cloned_enum.Reset();
    enum_attr.Reset();
    attr_info.Reset();
    disp_attr_prov.Reset();

    // Deactivate before Activate (exercises UninitKeyEventSink / UninitThreadMgrEventSink early-return)
    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    // Activate failure paths
    QIQI_TEST_CHECK(service->ActivateEx(nullptr, 1, 0) == E_INVALIDARG);
    ComPtr<MockTfThreadMgr> mock_tm(new MockTfThreadMgr());
    mock_tm->fail_query_source_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_query_source_ = false;
    mock_tm->fail_advise_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_advise_ = false;
    mock_tm->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_query_key_mgr_ = false;
    mock_tm->fail_key_sink_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_key_sink_ = false;

    // Activate success
    QIQI_TEST_CHECK(SUCCEEDED(service->Activate(mock_tm.Get(), 1)));

    // ThreadMgrEventSink notifications (including non-null OnSetFocus to test config hot-reload check)
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnInitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnUninitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPushContext(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPopContext(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnSetFocus(reinterpret_cast<ITfDocumentMgr*>(1), nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnSetFocus(nullptr, nullptr)));

    // KeyEventSink error checks
    QIQI_TEST_CHECK(key_sink->OnTestKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnTestKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnPreservedKey(nullptr, kGuidPreservedKeyToggleMode, nullptr) == E_INVALIDARG);

    ComPtr<MockTfContext> mock_ctx(new MockTfContext());
    BOOL eaten = FALSE;

    // Type 'A' then 'B' -> starts composition (BeginUIElement) then updates composition (UpdateUIElement)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");

    // Backspace back to 'a', then Space commits "工"
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_BACK, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SPACE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工");

    // Direct Chinese punctuation commit when not composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"。");

    // Start composing 'W', then cancel via Escape
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_.empty());

    // 5th key push auto-commit flow (triggers Commit with non-empty composition_text and EmptyCodeBeep on 4th code "yyyx")
    ResetLastPlayedSoundForTest();
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");

    // Test Shift tap mode toggle while composing (cancels composition and switches to English)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);

    // In English mode, 'A' is not eaten
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Toggle back to Chinese mode via PreservedKey (Ctrl+Space)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidProfileWubi86, &eaten)) && eaten == FALSE);

    // Test Caps Lock (VK_CAPITAL low-order bit = 1):
    // 1) In Idle Chinese mode, letter and punctuation keys pass through as English (not eaten by Wubi composition)
    // 2) Pressing VK_CAPITAL mid-composition commits the raw English code ("ab") while letting VK_CAPITAL pass through (*pfEaten = FALSE)
    // 3) While composing, pressing a letter with Caps Lock ON commits the current candidate + uppercase letter
    {
        BYTE caps_kb_state[256]{};
        caps_kb_state[VK_CAPITAL] = 0x01;
        ::SetKeyboardState(caps_kb_state);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);

        ::SetKeyboardState(clean_kb_state);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");

        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");

        ::SetKeyboardState(caps_kb_state);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工B");
        ::SetKeyboardState(clean_kb_state);
    }

    // Test FullWidth toggle (Shift+Space) and Chinese Punctuation toggle (Ctrl+.) PreservedKeys
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), '1', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"１");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);

    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);

    // Populate clipboard with a 2-character phrase ("中国") and test Ctrl+= (kGuidPreservedKeyAddUserWord)
    if (const ScopedClipboard clip{}; clip.IsOpen()) {
        ::EmptyClipboard();
        if (const HGLOBAL hclip = ::GlobalAlloc(GMEM_MOVEABLE, 4 * sizeof(wchar_t))) {
            if (auto* p = static_cast<wchar_t*>(::GlobalLock(hclip))) {
                wcscpy_s(p, 4, L"中国");
                ::GlobalUnlock(hclip);
                ::SetClipboardData(CF_UNICODETEXT, hclip);
            } else {
                ::GlobalFree(hclip);
            }
        }
    }
    ResetLastPlayedSoundForTest();
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyAddUserWord, &eaten)) && eaten == TRUE);

    // Start composing again, then test mouse click selection on CandidateUI and LangBar menu actions
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    if (mock_tm->last_ui_element_) {
        auto* cand_ui = static_cast<CandidateUI*>(mock_tm->last_ui_element_.Get());
        if (cand_ui->WindowHandle()) {
            ::SendMessageW(cand_ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
            QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工");
        }
    }

    if (mock_tm->langbar_item_) {
        ComPtr<ITfLangBarItemButton> lb_btn;
        if (SUCCEEDED(mock_tm->langbar_item_.As(&lb_btn)) && lb_btn) {
            POINT pt{0, 0};
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnClick(TF_LBI_CLK_LEFT, pt, nullptr)));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleChinese))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleFullWidth))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleFullWidth))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::TogglePunctuation))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::TogglePunctuation))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleStatusBar))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleStatusBar))));
            ComPtr<ITfFnConfigure> cfg_check;
            QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&cfg_check))) && cfg_check);
            ScopedBstr disp_name1;
            ScopedBstr disp_name2;
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage))));
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(disp_name1.Put())) && disp_name1.IsValid());
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage))));
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(disp_name2.Put())) && disp_name2.IsValid());
            QIQI_TEST_CHECK(std::wstring(disp_name1.Get()) != std::wstring(disp_name2.Get()));
        }
    }

    // Start composing again, then test PreservedKey while composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);

    // Non-shift TestKeyUp and KeyUp
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Host composition termination & focus loss
    QIQI_TEST_CHECK(SUCCEEDED(comp_sink->OnCompositionTerminated(1, nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnSetFocus(FALSE)));

    // Test defensive failure branches in StartOrUpdateCompositionInSession, UpdateCandidateWindowPositionInSession, and BeginOrUpdateCandidateUI
    mock_ctx->fail_qi_insert_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_qi_insert_ = false;

    mock_ctx->fail_insert_at_sel_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_insert_at_sel_ = false;

    mock_ctx->fail_qi_comp_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_qi_comp_ = false;

    mock_ctx->fail_start_comp_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_start_comp_ = false;

    mock_ctx->fail_comp_get_range_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_comp_get_range_ = false;

    mock_ctx->fail_get_view_ = true;
    mock_tm->fail_query_ui_mgr_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_get_view_ = false;
    mock_tm->fail_query_ui_mgr_ = false;

    mock_tm->fail_begin_ui_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_tm->fail_begin_ui_ = false;

    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    // Test UILess game mode activation (TF_TMAE_UIELEMENTENABLEDONLY with show_own_ui_ = FALSE)
    mock_tm->show_own_ui_ = FALSE;
    QIQI_TEST_CHECK(SUCCEEDED(service->ActivateEx(mock_tm.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SPACE, 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    comp_sink.Reset();
    key_sink.Reset();
    tm_sink.Reset();
    tip_ex.Reset();
    tip_base.Reset();
    QIQI_TEST_CHECK(service->Release() == 0);

    // Also test QiQiTextService with custom Haifeng + user_dict config and hot-reload on OnSetFocus
    std::error_code ec;
    std::filesystem::remove_all(temp_appdata, ec);

    QiQiConfig haifeng_cfg{};
    haifeng_cfg.primary_dict = PrimaryDict::Haifeng;
    haifeng_cfg.enable_jidian_extra = true;
    haifeng_cfg.log_keystrokes = true;
    haifeng_cfg.user_dict_path = L"wubi86_jidian_extra.txt";
    SaveConfigFile(GetDefaultUserConfigPath(), haifeng_cfg);

    auto* service_haifeng = new QiQiTextService(g_dll_instance);
    ComPtr<ITfThreadMgrEventSink> hf_tm_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&hf_tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->Activate(mock_tm.Get(), 1)));
    if (mock_tm->langbar_item_) {
        ComPtr<ITfLangBarItemButton> hf_lb;
        if (SUCCEEDED(mock_tm->langbar_item_.As(&hf_lb)) && hf_lb) {
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictJidian))));
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictHaifeng))));
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleExtraDict))));
            ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                ::KillTimer(nullptr, id);
                const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
                if (dlg) {
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                }
            });
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenSettings))));

            ::SetTimer(nullptr, 5, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                ::KillTimer(nullptr, id);
                const HWND dlg = ::FindWindowW(kAboutWndClass, nullptr);
                if (dlg) {
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                }
            });
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenAbout))));
        }
    }

    // Modify config file timestamp and trigger hot-reload via OnSetFocus (also enabling key sound & clear_on_empty_4code)
    haifeng_cfg.page_size = 6;
    haifeng_cfg.enable_key_sound = true;
    haifeng_cfg.beep_on_dup_4code = true;
    haifeng_cfg.clear_on_empty_4code = true;
    SaveConfigFile(GetDefaultUserConfigPath(), haifeng_cfg);
    std::filesystem::last_write_time(
        GetDefaultUserConfigPath(),
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10),
        ec);
    QIQI_TEST_CHECK(SUCCEEDED(hf_tm_sink->OnSetFocus(reinterpret_cast<ITfDocumentMgr*>(1), nullptr)));
    ComPtr<ITfKeyEventSink> hf_key_sink;
    if (SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&hf_key_sink))) && hf_key_sink) {
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));

        // Type 4 duplicate-match keys ("aaaa" -> "工", "恭恭敬敬"): verifies beep_on_dup_4code plays DupCodeBeep
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));

        // Type 4 unmatched keys ("xvwq"): verifies clear_on_empty_4code cancels TSF composition & plays EmptyCodeBeep
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'V', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'Q', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
        QIQI_TEST_CHECK(mock_ctx->range_->text_.empty());

        // Exercise FormatVirtualKey branches and VK_CAPITAL trace logging when log_keystrokes is enabled
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_MINUS, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PLUS, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_DOWN, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_BACK, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_1, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_7, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_RETURN, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_COMMA, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        hf_key_sink.Reset();
    }
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->Deactivate()));
    hf_tm_sink.Reset();
    QIQI_TEST_CHECK(service_haifeng->Release() == 0);
    SetMuteAudioOutputForTest(false);

    // Also test QiQiTextService construction when:
    // 1) g_dll_instance points to kernel32.dll (where dictionaries do not exist next to DLL)
    // 2) g_dll_instance is invalid (GetModuleFileNameW failure branch)
    HINSTANCE saved_instance = g_dll_instance;
    g_dll_instance = ::GetModuleHandleW(L"kernel32.dll");
    auto* service_no_dict = new QiQiTextService(g_dll_instance);
    QIQI_TEST_CHECK(service_no_dict->Release() == 0);

    g_dll_instance = reinterpret_cast<HINSTANCE>(0x12345678);
    auto* service_bad_inst = new QiQiTextService(g_dll_instance);
    QIQI_TEST_CHECK(service_bad_inst->Release() == 0);
    g_dll_instance = saved_instance;
}

} // namespace

void RunTsfServiceTests() {
    TestQiQiTextServiceLifecycleAndKeyFlow();
}

} // namespace qiqi::test
