#include "common.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "ui/candidate_ui.h"
#include "ui/langbar_button.h"
#include "ui/settings_dialog.h"
#include "ui/status_bar.h"

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
    STDMETHODIMP AddMenuItem(UINT uId, DWORD dwFlags, HBITMAP, HBITMAP, const WCHAR* pch, ULONG cch, ITfMenu**) override {
        if (dwFlags & TF_LBMENUF_SEPARATOR) {
            ++separators_;
        } else {
            item_ids_.push_back(uId);
            if (pch && cch > 0) {
                item_texts_.emplace_back(pch, cch);
            }
        }
        return S_OK;
    }

    std::vector<UINT> item_ids_;
    std::vector<std::wstring> item_texts_;
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
    ScopedBstr tooltip;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(tooltip.Put())) && tooltip.IsValid());

    QIQI_TEST_CHECK(btn->GetText(nullptr) == E_INVALIDARG);
    ScopedBstr text;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(text.Put())) && std::wstring(text.Get()) == L"中");

    QIQI_TEST_CHECK(btn->GetIcon(nullptr) == E_INVALIDARG);
    ScopedIcon icon;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetIcon(icon.Put())) && icon.IsValid());

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
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(text.Put())) && std::wstring(text.Get()) == L"英");

    QIQI_TEST_CHECK(lb_src->UnadviseSink(9999) == CONNECT_E_NOCONNECTION);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->UnadviseSink(cookie)));

    // Menu & Click (Chinese UI)
    st.ui_language = UiLanguage::ZhCn;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(std::wstring(info.szDescription) == L"QiQi 五笔状态与菜单");
    QIQI_TEST_CHECK(btn->InitMenu(nullptr) == E_INVALIDARG);
    MockTfMenu menu_zh;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_zh)));
    QIQI_TEST_CHECK(menu_zh.item_ids_.size() == 10 && menu_zh.separators_ == 2);
    QIQI_TEST_CHECK(!menu_zh.item_texts_.empty() &&
                    menu_zh.item_texts_.front().find(L"中文输入模式") != std::wstring::npos);

    // Test English UI language on LangBarButton (GetInfo, GetTooltipString, InitMenu)
    st.ui_language = UiLanguage::EnUs;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(std::wstring(info.szDescription) == L"QiQi Wubi Status & Menu");
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(tooltip.Put())) &&
                    std::wstring(tooltip.Get()).find(L"Left-click") != std::wstring::npos);
    MockTfMenu menu_en;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_en)));
    QIQI_TEST_CHECK(menu_en.item_ids_.size() == 10 && menu_en.separators_ == 2);
    QIQI_TEST_CHECK(!menu_en.item_texts_.empty() &&
                    menu_en.item_texts_.front().find(L"Chinese Input Mode") != std::wstring::npos);

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
    ScopedBstr desc;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDescription(desc.Put())) && desc.IsValid());

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
    ScopedBstr str;
    QIQI_TEST_CHECK(ui->GetString(0, str.Put()) == E_INVALIDARG);

    // Show empty window in Chinese and English UI modes and trigger WM_PAINT
    ui->SetUiLanguage(UiLanguage::ZhCn);
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

    ui->SetUiLanguage(UiLanguage::EnUs);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
    }
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

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetString(6, str.Put())) && str.IsValid());
    QIQI_TEST_CHECK(std::wstring(str.Get()) == L"七");
    QIQI_TEST_CHECK(ui->GetString(7, str.Put()) == E_INVALIDARG);

    // Test off-screen work area clamping, long candidate auto-sizing, DPI change, & mouse click selection
    ui->SetUiLanguage(UiLanguage::EnUs);
    std::vector<std::wstring> long_cands = {
        L"恭恭敬敬一二三四", L"花花世界五六七八", L"劳苦功高九十", L"草草了事", L"芙蓉出水"};
    ui->SetCandidates(L"aaaa", long_cands, 5, 0, 0);
    QIQI_TEST_CHECK(ui->CurrentWidth() > CandidateUI::kPopupWidth);

    if (ui->WindowHandle()) {
        // Click before callback is set (exercises early return when !select_callback_)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
    }

    size_t clicked_idx = 99;
    ui->SetSelectCallback([&clicked_idx](size_t idx) { clicked_idx = idx; });
    if (ui->WindowHandle()) {
        ::SendMessageW(ui->WindowHandle(), WM_MOUSEACTIVATE, 0, 0);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(144, 144), 0);
        // MoveWindowTo while dpi_ is 144 and window DPI is 96 (exercises wnd_dpi != dpi_ rescaling)
        ui->MoveWindowTo(normal_rc);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(96, 96), 0);
        // Click outside candidate rects (miss) then inside candidate 0 (hit)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
        QIQI_TEST_CHECK(clicked_idx == 99);
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

void TestSettingsDialogControls() {
    QiQiConfig cfg{};
    cfg.primary_dict = PrimaryDict::Haifeng;
    cfg.enable_jidian_extra = false;
    cfg.auto_commit_4code = false;
    cfg.push_commit_5th = false;
    cfg.shift_toggle_mode = false;
    cfg.show_status_bar = true;
    cfg.page_size = 7;

    HWND hwnd = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg);
    QIQI_TEST_CHECK(hwnd != nullptr);

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(hwnd, read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!read_back.enable_jidian_extra);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.push_commit_5th);
    QIQI_TEST_CHECK(!read_back.shift_toggle_mode);
    QIQI_TEST_CHECK(read_back.show_status_bar);
    QIQI_TEST_CHECK(read_back.page_size == 7);

    // Also test BuiltinOnly and Jidian radio states + UiLanguage combobox + Cancel / WM_CLOSE
    cfg.primary_dict = PrimaryDict::BuiltinOnly;
    cfg.ui_language = UiLanguage::EnUs;
    WriteSettingsToWindowControls(hwnd, cfg);
    ReadSettingsFromWindowControls(hwnd, read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::EnUs);

    cfg.primary_dict = PrimaryDict::Jidian;
    cfg.ui_language = UiLanguage::ZhCn;
    WriteSettingsToWindowControls(hwnd, cfg);
    ReadSettingsFromWindowControls(hwnd, read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn);

    cfg.ui_language = UiLanguage::Auto;
    WriteSettingsToWindowControls(hwnd, cfg);
    ReadSettingsFromWindowControls(hwnd, read_back);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::Auto);

    ::SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
    ::SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    ::DestroyWindow(hwnd);

    // Verify English settings dialog title when created with UiLanguage::EnUs
    cfg.ui_language = UiLanguage::EnUs;
    HWND hwnd_en = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg);
    QIQI_TEST_CHECK(hwnd_en != nullptr);
    wchar_t title_buf[128]{};
    ::GetWindowTextW(hwnd_en, title_buf, 128);
    QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi Wubi IME - Settings");
    ::DestroyWindow(hwnd_en);

    // Verify Chinese settings dialog title when created with UiLanguage::ZhCn
    cfg.ui_language = UiLanguage::ZhCn;
    HWND hwnd_zh = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg);
    QIQI_TEST_CHECK(hwnd_zh != nullptr);
    ::GetWindowTextW(hwnd_zh, title_buf, 128);
    QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi 五笔输入法 - 属性设置");
    ::DestroyWindow(hwnd_zh);

    // Test modal ShowSettingsDialog with auto-closing timer (IDOK -> returns true)
    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
}

void TestStatusBarUI() {
    // 1. Test theme detection & palette switching (OS default, forced Light, forced Dark)
    SetThemeDarkOverrideForTest(std::nullopt);
    (void)IsSystemDarkTheme();
    SetThemeDarkOverrideForTest(false);
    QIQI_TEST_CHECK(!IsSystemDarkTheme());
    QIQI_TEST_CHECK(!GetCurrentThemePalette().is_dark);
    SetThemeDarkOverrideForTest(true);
    QIQI_TEST_CHECK(IsSystemDarkTheme());
    QIQI_TEST_CHECK(GetCurrentThemePalette().is_dark);

    LangBarMenuId clicked_action = static_cast<LangBarMenuId>(0);
    StatusBarUI bar(::GetModuleHandleW(nullptr), [&clicked_action](LangBarMenuId id) {
        clicked_action = id;
    });

    // Disabled by default
    QIQI_TEST_CHECK(!bar.IsVisible());

    LangBarState st{};
    st.show_status_bar = true;
    st.ui_language = UiLanguage::ZhCn;
    bar.UpdateState(st);
    // Call Show(true) again when window already exists
    bar.Show(true);
    QIQI_TEST_CHECK(bar.IsVisible());
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);

    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::SendMessageW(bar.WindowHandle(), WM_MOUSEACTIVATE, 0, 0);

    // 2. Test leftmost vertical pill gripper [0..22) hit-test (HTCAPTION), cursor, and window moving (WM_MOVE)
    POINT grip_screen{10, 18};
    ::ClientToScreen(bar.WindowHandle(), &grip_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(grip_screen.x, grip_screen.y)) == HTCAPTION);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_SETCURSOR, reinterpret_cast<WPARAM>(bar.WindowHandle()), MAKELPARAM(HTCAPTION, WM_MOUSEMOVE)) == TRUE);

    POINT btn_screen{40, 18};
    ::ClientToScreen(bar.WindowHandle(), &btn_screen);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_NCHITTEST, 0, MAKELPARAM(btn_screen.x, btn_screen.y)) == HTCLIENT);
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_SETCURSOR, reinterpret_cast<WPARAM>(bar.WindowHandle()), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == TRUE);

    ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(320, 480));
    QIQI_TEST_CHECK(bar.Position().x == 320 && bar.Position().y == 480);

    // 3. Test hover tooltips across Grip [0..22) and all 4 buttons [22..58, 58..94, 94..130, 130..166)
    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(-5)).empty());
    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(999)).empty());

    ::SendMessageW(bar.WindowHandle(), WM_NCMOUSEMOVE, HTCAPTION, MAKELPARAM(grip_screen.x, grip_screen.y));
    QIQI_TEST_CHECK(bar.HoveredZone() == 0);
    QIQI_TEST_CHECK(bar.ActiveTooltipText() == kZhCnStrings.tip_grip);
    QIQI_TEST_CHECK(bar.TooltipWindowHandle() != nullptr);
    ::SendMessageW(bar.TooltipWindowHandle(), WM_MOUSEACTIVATE, 0, 0);

    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(35, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 1 && bar.ActiveTooltipText() == kZhCnStrings.tip_mode);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(70, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 2 && bar.ActiveTooltipText() == kZhCnStrings.tip_width);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(105, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 3 && bar.ActiveTooltipText() == kZhCnStrings.tip_punct);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(145, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == 4 && bar.ActiveTooltipText() == kZhCnStrings.tip_settings);

    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::SendMessageW(bar.WindowHandle(), WM_MOUSELEAVE, 0, 0);
    QIQI_TEST_CHECK(bar.HoveredZone() == -1 && bar.ActiveTooltipText().empty());

    // 4. Test button clicks: [Grip:0..22] [Cell0:22..58] [Cell1:58..94] [Cell2:94..130] [Cell3:130..166]
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 18));
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 18));
    QIQI_TEST_CHECK(clicked_action == static_cast<LangBarMenuId>(0));

    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(35, 18));
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleChinese);
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(70, 18));
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleFullWidth);
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(105, 18));
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::TogglePunctuation);
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(145, 18));
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::OpenSettings);

    // 5. Test themed right-click context menu & WM_COMMAND / WM_SETTINGCHANGE in Light & Dark modes
    ScopedGdiHandle<HBRUSH> menu_brush;
    ScopedMenu popup_menu = bar.BuildContextMenu(menu_brush);
    QIQI_TEST_CHECK(popup_menu.IsValid() && menu_brush.IsValid());
    QIQI_TEST_CHECK(::GetMenuItemCount(popup_menu.Get()) == 12); // 10 items + 2 separators

    ::SendMessageW(bar.WindowHandle(), WM_COMMAND, MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage), 0), 0);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleUiLanguage);

    // Test Light theme + English UI rendering and tooltips
    SetThemeDarkOverrideForTest(false);
    ::SendMessageW(bar.WindowHandle(), WM_SETTINGCHANGE, 0, 0);
    st.ui_language = UiLanguage::EnUs;
    st.chinese_mode = false;
    st.full_width = true;
    st.chinese_punct = false;
    bar.UpdateState(st);
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(35, 18));
    QIQI_TEST_CHECK(bar.ActiveTooltipText() == kEnUsStrings.tip_mode);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    SetThemeDarkOverrideForTest(std::nullopt);
    st.show_status_bar = false;
    bar.UpdateState(st);
    QIQI_TEST_CHECK(!bar.IsVisible());
    bar.DestroyBarWindow();
}

void TestMultiMonitorAndPerMonitorDpi() {
    // Simulate a secondary monitor located to the left/top of primary monitor (negative coordinates) at 144 DPI (150%)
    MonitorMetrics secondary_mon{};
    secondary_mon.work_area = RECT{-1920, -1080, 0, 0};
    secondary_mon.dpi = 144;
    SetMonitorMetricsOverrideForTest(secondary_mon);

    // 1. CandidateUI on negative-coordinate high-DPI secondary monitor
    auto* cand_ui = new CandidateUI(::GetModuleHandleW(nullptr));
    QIQI_TEST_CHECK(SUCCEEDED(cand_ui->Show(TRUE)));
    cand_ui->SetCandidates(L"abcd", {L"一", L"二", L"三"}, 5, 0, 0);

    // Move near bottom-right of secondary monitor -> should flip above and clamp within [-1920..0, -1080..0]
    RECT near_bottom_right{-50, -20, -10, -2};
    cand_ui->MoveWindowTo(near_bottom_right);
    QIQI_TEST_CHECK(cand_ui->CurrentHeight() == ScalePxByDpi(CandidateUI::kPopupHeight, 144));

    // Also test WM_DPICHANGED with suggested RECT* on CandidateUI
    RECT suggested_cand{-1500, -800, -1000, -700};
    if (cand_ui->WindowHandle()) {
        ::SendMessageW(
            cand_ui->WindowHandle(),
            WM_DPICHANGED,
            MAKEWPARAM(192, 192),
            reinterpret_cast<LPARAM>(&suggested_cand));
        QIQI_TEST_CHECK(cand_ui->CurrentHeight() == ScalePxByDpi(CandidateUI::kPopupHeight, 192));
    }
    cand_ui->DestroyPopupWindow();
    QIQI_TEST_CHECK(cand_ui->Release() == 0);

    // 2. StatusBarUI on negative-coordinate high-DPI secondary monitor
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    LangBarState st{};
    st.show_status_bar = true;
    bar.UpdateState(st);
    QIQI_TEST_CHECK(bar.CurrentDpi() == 144);
    QIQI_TEST_CHECK(bar.CurrentWidth() == ScalePxByDpi(StatusBarUI::kBarWidth, 144));
    QIQI_TEST_CHECK(bar.Position().x >= secondary_mon.work_area.left &&
                    bar.Position().x + bar.CurrentWidth() <= secondary_mon.work_area.right);
    QIQI_TEST_CHECK(bar.Position().y >= secondary_mon.work_area.top &&
                    bar.Position().y + bar.CurrentHeight() <= secondary_mon.work_area.bottom);

    // Drag status bar near top of secondary monitor and trigger tooltip (flips below bar and clamps to work_area)
    if (bar.WindowHandle()) {
        ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(-1910, -1075));
        ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(45, 18));
        QIQI_TEST_CHECK(!bar.ActiveTooltipText().empty());

        // Simulate cross-monitor WM_DPICHANGED with suggested rect and WM_DISPLAYCHANGE re-clamping
        RECT suggested_bar{-1200, -600, -900, -550};
        ::SendMessageW(
            bar.WindowHandle(),
            WM_DPICHANGED,
            MAKEWPARAM(192, 192),
            reinterpret_cast<LPARAM>(&suggested_bar));
        ::SendMessageW(bar.WindowHandle(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    }
    bar.DestroyBarWindow();

    // 3. SettingsDialog centered & DPI-scaled on secondary monitor
    QiQiConfig cfg{};
    HWND dlg = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg);
    QIQI_TEST_CHECK(dlg != nullptr);
    RECT dlg_rc{};
    ::GetWindowRect(dlg, &dlg_rc);
    QIQI_TEST_CHECK(dlg_rc.left >= secondary_mon.work_area.left &&
                    dlg_rc.right <= secondary_mon.work_area.right);
    ::DestroyWindow(dlg);

    SetMonitorMetricsOverrideForTest(std::nullopt);
}

} // namespace

void RunCandidateUITests() {
    TestCandidateUI();
    TestLangBarButton();
    TestSettingsDialogControls();
    TestStatusBarUI();
    TestMultiMonitorAndPerMonitorDpi();
}

} // namespace qiqi::test
