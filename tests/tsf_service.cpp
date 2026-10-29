#include "common.h"
#include "tsf_mocks.h"
#include "common/guids.h"
#include "tsf/tip_service.h"

namespace qiqi::test {

namespace {

void TestQiQiTextServiceLifecycleAndKeyFlow() {
    BYTE clean_kb_state[256]{};
    ::SetKeyboardState(clean_kb_state);

    auto* service = new QiQiTextService();

    // QueryInterface coverage for all supported interfaces
    QIQI_TEST_CHECK(service->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(service->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE && out == nullptr);

    ComPtr<ITfTextInputProcessor> tip_base;
    ComPtr<ITfTextInputProcessorEx> tip_ex;
    ComPtr<ITfThreadMgrEventSink> tm_sink;
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    ComPtr<ITfDisplayAttributeProvider> disp_attr_prov;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_base))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_ex))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&disp_attr_prov))));

    // Test ITfDisplayAttributeProvider & IEnumTfDisplayAttributeInfo & ITfDisplayAttributeInfo
    ComPtr<ITfDisplayAttributeInfo> attr_info;
    QIQI_TEST_CHECK(disp_attr_prov->GetDisplayAttributeInfo(kGuidProfileWubi86, &attr_info) == E_INVALIDARG);
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->GetDisplayAttributeInfo(kGuidDisplayAttributeInput, &attr_info)) && attr_info);
    GUID attr_guid{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetGUID(&attr_guid)) && IsEqualGUID(attr_guid, kGuidDisplayAttributeInput));
    BSTR attr_desc = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetDescription(&attr_desc)) && attr_desc != nullptr);
    ::SysFreeString(attr_desc);
    TF_DISPLAYATTRIBUTE da{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetAttributeInfo(&da)) && da.lsStyle == TF_LS_DOT);
    da.fBoldLine = TRUE;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->SetAttributeInfo(&da)));
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->Reset()));

    ComPtr<IEnumTfDisplayAttributeInfo> enum_attr;
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->EnumDisplayAttributeInfo(&enum_attr)) && enum_attr);
    ComPtr<ITfDisplayAttributeInfo> fetched_info;
    ULONG fetched = 0;
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_OK && fetched == 1 && fetched_info);
    fetched_info.Reset();
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_FALSE && fetched == 0);
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Reset()));
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Skip(1)));
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

    // 5th key push auto-commit flow (triggers Commit with non-empty composition_text)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
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

    // Test FullWidth toggle (Shift+Space) and Chinese Punctuation toggle (Ctrl+.) PreservedKeys
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), '1', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"１");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);

    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);

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
    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    std::error_code ec;
    const auto temp_appdata = std::filesystem::temp_directory_path() / L"QiQiIME_TsfTestAppData";
    std::filesystem::remove_all(temp_appdata, ec);
    _wputenv_s(L"LOCALAPPDATA", temp_appdata.wstring().c_str());

    QiQiConfig haifeng_cfg{};
    haifeng_cfg.primary_dict = PrimaryDict::Haifeng;
    haifeng_cfg.enable_jidian_extra = true;
    haifeng_cfg.user_dict_path = L"wubi86_jidian_extra.txt";
    SaveConfigFile(GetDefaultUserConfigPath(), haifeng_cfg);

    auto* service_haifeng = new QiQiTextService();
    ComPtr<ITfThreadMgrEventSink> hf_tm_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&hf_tm_sink))));

    // Modify config file timestamp and trigger hot-reload via OnSetFocus
    haifeng_cfg.page_size = 6;
    SaveConfigFile(GetDefaultUserConfigPath(), haifeng_cfg);
    std::filesystem::last_write_time(
        GetDefaultUserConfigPath(),
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10),
        ec);
    QIQI_TEST_CHECK(SUCCEEDED(hf_tm_sink->OnSetFocus(reinterpret_cast<ITfDocumentMgr*>(1), nullptr)));
    hf_tm_sink.Reset();
    QIQI_TEST_CHECK(service_haifeng->Release() == 0);

    if (orig_localappdata) {
        _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
        free(orig_localappdata);
    }
    std::filesystem::remove_all(temp_appdata, ec);

    // Also test QiQiTextService construction when:
    // 1) g_dll_instance points to kernel32.dll (where dictionaries do not exist next to DLL)
    // 2) g_dll_instance is invalid (GetModuleFileNameW failure branch)
    HINSTANCE saved_instance = g_dll_instance;
    g_dll_instance = ::GetModuleHandleW(L"kernel32.dll");
    auto* service_no_dict = new QiQiTextService();
    QIQI_TEST_CHECK(service_no_dict->Release() == 0);

    g_dll_instance = reinterpret_cast<HINSTANCE>(0x12345678);
    auto* service_bad_inst = new QiQiTextService();
    QIQI_TEST_CHECK(service_bad_inst->Release() == 0);
    g_dll_instance = saved_instance;
}

} // namespace

void RunTsfServiceTests() {
    TestQiQiTextServiceLifecycleAndKeyFlow();
}

} // namespace qiqi::test
