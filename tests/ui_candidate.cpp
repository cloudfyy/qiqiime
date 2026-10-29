#include "common.h"
#include "common/guids.h"
#include "ui/candidate_ui.h"
#include "ui/langbar_button.h"

#include <windows.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

class MockTfMenu final : public ITfMenu {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfMenu)) {
            *ppvObj = static_cast<ITfMenu*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP AddMenuItem(UINT uId, DWORD dwFlags, HBITMAP, HBITMAP, const WCHAR*, ULONG, ITfMenu**) override {
        if (dwFlags & TF_LBMENUF_SEPARATOR) {
            ++separators_;
        } else {
            item_ids_.push_back(uId);
        }
        return S_OK;
    }

    std::vector<UINT> item_ids_;
    size_t separators_ = 0;
};

class MockLangBarItemSink final : public ITfLangBarItemSink {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItemSink)) {
            *ppvObj = static_cast<ITfLangBarItemSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP OnUpdate(DWORD dwFlags) override {
        last_update_flags_ = dwFlags;
        return S_OK;
    }

    DWORD last_update_flags_ = 0;
};

void TestLangBarButton() {
    LangBarMenuId last_action = static_cast<LangBarMenuId>(0);
    auto* btn = new LangBarButton([&last_action](LangBarMenuId id) { last_action = id; });

    QIQI_TEST_CHECK(btn->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(btn->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);

    ComPtr<ITfLangBarItemButton> lb_btn;
    ComPtr<ITfSource> lb_src;
    QIQI_TEST_CHECK(SUCCEEDED(btn->QueryInterface(IID_PPV_ARGS(&lb_btn))));
    QIQI_TEST_CHECK(SUCCEEDED(btn->QueryInterface(IID_PPV_ARGS(&lb_src))));

    // Info, Status, Show, Tooltip, Text, Icon
    QIQI_TEST_CHECK(btn->GetInfo(nullptr) == E_INVALIDARG);
    TF_LANGBARITEMINFO info{};
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(IsEqualCLSID(info.clsidService, kClsidQiQiTextService));

    QIQI_TEST_CHECK(btn->GetStatus(nullptr) == E_INVALIDARG);
    DWORD status = 99;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetStatus(&status)) && status == 0);
    QIQI_TEST_CHECK(SUCCEEDED(btn->Show(TRUE)));

    QIQI_TEST_CHECK(btn->GetTooltipString(nullptr) == E_INVALIDARG);
    BSTR tooltip = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(&tooltip)) && tooltip != nullptr);
    ::SysFreeString(tooltip);

    QIQI_TEST_CHECK(btn->GetText(nullptr) == E_INVALIDARG);
    BSTR text = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(&text)) && std::wstring(text) == L"中");
    ::SysFreeString(text);

    QIQI_TEST_CHECK(btn->GetIcon(nullptr) == E_INVALIDARG);
    HICON icon = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetIcon(&icon)) && icon != nullptr);
    ::DestroyIcon(icon);

    // AdviseSink / UpdateState / UnadviseSink
    MockLangBarItemSink sink;
    DWORD cookie = 0;
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, nullptr, &cookie) == E_INVALIDARG);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_IUnknown, &sink, &cookie) == CONNECT_E_CANNOTCONNECT);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie)) && cookie != 0);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == CONNECT_E_ADVISELIMIT);

    LangBarState st{};
    st.chinese_mode = false;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(sink.last_update_flags_ != 0);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(&text)) && std::wstring(text) == L"英");
    ::SysFreeString(text);

    QIQI_TEST_CHECK(lb_src->UnadviseSink(9999) == CONNECT_E_NOCONNECTION);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->UnadviseSink(cookie)));

    // Menu & Click
    QIQI_TEST_CHECK(btn->InitMenu(nullptr) == E_INVALIDARG);
    MockTfMenu menu;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu)));
    QIQI_TEST_CHECK(menu.item_ids_.size() == 9 && menu.separators_ == 2);

    POINT pt{0, 0};
    QIQI_TEST_CHECK(SUCCEEDED(btn->OnClick(TF_LBI_CLK_LEFT, pt, nullptr)));
    QIQI_TEST_CHECK(last_action == LangBarMenuId::ToggleChinese);
    QIQI_TEST_CHECK(SUCCEEDED(btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictHaifeng))));
    QIQI_TEST_CHECK(last_action == LangBarMenuId::DictHaifeng);

    lb_src.Reset();
    lb_btn.Reset();
    QIQI_TEST_CHECK(btn->Release() == 0);
}

void TestCandidateUI() {
    auto* ui = new CandidateUI(::GetModuleHandleW(nullptr));

    // QueryInterface tests
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* dummy = nullptr;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IClassFactory, &dummy) == E_NOINTERFACE && dummy == nullptr);

    ComPtr<ITfCandidateListUIElement> cand_elem;
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfCandidateListUIElement, &cand_elem)) && cand_elem);

    // Metadata & invalid arg checks
    QIQI_TEST_CHECK(ui->GetDescription(nullptr) == E_INVALIDARG);
    BSTR desc = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDescription(&desc)) && desc != nullptr);
    ::SysFreeString(desc);

    QIQI_TEST_CHECK(ui->GetGUID(nullptr) == E_INVALIDARG);
    GUID guid{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetGUID(&guid)) && IsEqualGUID(guid, kGuidCandidateUIElement));

    QIQI_TEST_CHECK(ui->GetUpdatedFlags(nullptr) == E_INVALIDARG);
    DWORD flags = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetUpdatedFlags(&flags)) && flags != 0);

    QIQI_TEST_CHECK(ui->GetDocumentMgr(nullptr) == E_INVALIDARG);
    ITfDocumentMgr* doc_mgr = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDocumentMgr(&doc_mgr)) && doc_mgr == nullptr);

    QIQI_TEST_CHECK(ui->IsShown(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCount(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetSelection(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetString(0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetPageIndex(nullptr, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCurrentPage(nullptr) == E_INVALIDARG);

    // Empty candidate state
    UINT count = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 0);
    UINT sel = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 0);
    UINT page_cnt = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(nullptr, 0, &page_cnt)) && page_cnt == 0);
    BSTR str = nullptr;
    QIQI_TEST_CHECK(ui->GetString(0, &str) == E_INVALIDARG);

    // Show empty window and trigger WM_PAINT
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(TRUE)));
    BOOL shown = FALSE;
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == TRUE);
    RECT normal_rc{100, 100, 150, 125};
    ui->MoveWindowTo(normal_rc);
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Multi-page candidate state (7 candidates, page_size 5, page 1, selected 1 -> index 6)
    std::vector<std::wstring> cands = {L"一", L"二", L"三", L"四", L"五", L"六", L"七"};
    ui->SetCandidates(L"abcd", cands, 5, 1, 1);

    // Also test page_size=0 fallback while hidden
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    ui->MoveWindowTo(normal_rc);
    ui->SetCandidates(L"abcd", cands, 0, 1, 1);
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(TRUE)));

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 7);
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 6);
    UINT cur_page = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCurrentPage(&cur_page)) && cur_page == 1);
    QIQI_TEST_CHECK(SUCCEEDED(ui->SetPageIndex(nullptr, 0)));

    UINT page_indices[4]{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(page_indices, 4, &page_cnt)) && page_cnt == 2);
    QIQI_TEST_CHECK(page_indices[0] == 0 && page_indices[1] == 5);

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetString(6, &str)) && str != nullptr);
    QIQI_TEST_CHECK(std::wstring(str) == L"七");
    ::SysFreeString(str);
    QIQI_TEST_CHECK(ui->GetString(7, &str) == E_INVALIDARG);

    // Test off-screen work area clamping, long candidate auto-sizing, DPI change, & mouse click selection
    std::vector<std::wstring> long_cands = {
        L"恭恭敬敬一二三四", L"花花世界五六七八", L"劳苦功高九十", L"草草了事", L"芙蓉出水"};
    ui->SetCandidates(L"aaaa", long_cands, 5, 0, 0);
    QIQI_TEST_CHECK(ui->CurrentWidth() > CandidateUI::kPopupWidth);

    size_t clicked_idx = 99;
    ui->SetSelectCallback([&clicked_idx](size_t idx) { clicked_idx = idx; });
    if (ui->WindowHandle()) {
        ::SendMessageW(ui->WindowHandle(), WM_MOUSEACTIVATE, 0, 0);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(144, 144), 0);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(96, 96), 0);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
        QIQI_TEST_CHECK(clicked_idx == 0);
    }

    RECT far_rc{99999, 99999, 100050, 100025};
    ui->MoveWindowTo(far_rc);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui->SetUiElementId(42);
    QIQI_TEST_CHECK(ui->UiElementId() == 42);

    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == FALSE);
    ui->DestroyPopupWindow();

    cand_elem.Reset();
    QIQI_TEST_CHECK(ui->Release() == 0);
}

} // namespace

void RunCandidateUITests() {
    TestCandidateUI();
    TestLangBarButton();
}

} // namespace qiqi::test
