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
#include <gsl/gsl>
#include <array>
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
    QIQI_TEST_CHECK(menu_zh.item_ids_.size() == 11 && menu_zh.separators_ == 2);
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
    QIQI_TEST_CHECK(menu_en.item_ids_.size() == 11 && menu_en.separators_ == 2);
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

    // Test off-screen work area clamping, long candidate auto-sizing, Direct2D/DirectWrite CJK Ext-B & Emoji rendering, DPI change, & mouse click selection
    QIQI_TEST_CHECK(ui->IsDirectWriteActive());
    ui->SetUiLanguage(UiLanguage::EnUs);
    std::vector<std::wstring> long_cands = {
        L"恭恭敬敬一二三四", L"𠀀𠀁生僻字🚀", L"花花世界五六七八", L"劳苦功高九十", L"芙蓉出水"};
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
    cfg.semicolon_quote_quick_select = false;
    cfg.show_code_hint = false;
    cfg.dynamic_phrase_freq = true;
    cfg.shift_toggle_mode = false;
    cfg.show_status_bar = true;
    cfg.beep_on_empty_4code = false;
    cfg.beep_on_dup_4code = true;
    cfg.clear_on_empty_4code = true;
    cfg.enable_key_sound = true;
    cfg.page_size = 7;

    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd.IsValid());

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    QIQI_TEST_CHECK(!read_back.enable_jidian_extra);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.push_commit_5th);
    QIQI_TEST_CHECK(!read_back.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!read_back.show_code_hint);
    QIQI_TEST_CHECK(read_back.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!read_back.shift_toggle_mode);
    QIQI_TEST_CHECK(read_back.show_status_bar);
    QIQI_TEST_CHECK(!read_back.beep_on_empty_4code);
    QIQI_TEST_CHECK(read_back.beep_on_dup_4code);
    QIQI_TEST_CHECK(read_back.clear_on_empty_4code);
    QIQI_TEST_CHECK(read_back.enable_key_sound);
    QIQI_TEST_CHECK(read_back.page_size == 7);

    // Also test BuiltinOnly and Jidian radio states + UiLanguage combobox + Cancel / WM_CLOSE
    cfg.primary_dict = PrimaryDict::BuiltinOnly;
    cfg.ui_language = UiLanguage::EnUs;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::EnUs);

    cfg.primary_dict = PrimaryDict::Jidian;
    cfg.ui_language = UiLanguage::ZhCn;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    // Simulate clicking Haifeng radio (102) -> disables Extra checkbox; clicking Jidian radio (101) -> enables it
    ::CheckRadioButton(hwnd.Get(), 101, 103, 102);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ::CheckRadioButton(hwnd.Get(), 101, 103, 101);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn);

    cfg.ui_language = UiLanguage::Auto;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::Auto);

    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    ::SendMessageW(hwnd.Get(), WM_CLOSE, 0, 0);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    wchar_t title_buf[128]{};
    {
        // Verify English settings dialog title when created with UiLanguage::EnUs
        cfg.ui_language = UiLanguage::EnUs;
        const ScopedWindow hwnd_en(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_en.IsValid());
        ::GetWindowTextW(hwnd_en.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi Wubi IME - Settings");

        // Verify Chinese settings dialog title when created with UiLanguage::ZhCn
        cfg.ui_language = UiLanguage::ZhCn;
        const ScopedWindow hwnd_zh(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_zh.IsValid());
        ::GetWindowTextW(hwnd_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi 五笔输入法 - 属性设置");

        // Test clicking the "About..." button (id=115) inside SettingsDialog (both with bound_config and nullptr)
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = ::FindWindowW(kAboutWndClass, nullptr);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_CLOSE, 0, 0);
            }
        });
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);

        const ScopedWindow hwnd_null_cfg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, nullptr));
        QIQI_TEST_CHECK(hwnd_null_cfg.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(hwnd_null_cfg.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = ::FindWindowW(kAboutWndClass, nullptr);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd_null_cfg.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);
    }

    // Destroy `hwnd` before testing modal ShowSettingsDialog so FindWindowW finds the active modal window
    {
        const ScopedWindow destroy_first = std::move(const_cast<ScopedWindow&>(hwnd));
    }

    // Test modal ShowSettingsDialog with auto-closing timer (IDOK -> returns true, IDCANCEL -> returns false)
    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    // Verify About dialog creation, painting, title localization, child controls, icon, and modal ShowAboutDialog
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_app_name).find(L"奇奇五笔") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_app_name).find(L"奇奇五笔") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kGitHubRepoUrl) == L"https://github.com/cloudfyy/qiqiime");
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"Apache License 2.0") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"BSD 3-Clause License") != std::wstring::npos);

    {
        const ScopedWindow about_zh(CreateAboutWindow(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
        QIQI_TEST_CHECK(about_zh.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(about_zh.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        ::GetWindowTextW(about_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"关于 奇奇五笔");
        wchar_t btn_buf[64]{};
        ::GetDlgItemTextW(about_zh.Get(), 201, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"访问 GitHub");
        ::GetDlgItemTextW(about_zh.Get(), IDOK, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"确定");
        ::ShowWindow(about_zh.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_zh.Get(), nullptr, TRUE);
        ::UpdateWindow(about_zh.Get());
        ::SendMessageW(about_zh.Get(), WM_CLOSE, 0, 0);

        const ScopedWindow about_en(CreateAboutWindow(::GetModuleHandleW(nullptr), about_zh.Get(), UiLanguage::EnUs));
        QIQI_TEST_CHECK(about_en.IsValid());
        ::GetWindowTextW(about_en.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"About 奇奇五笔 (QiQi Wubi)");
        ::GetDlgItemTextW(about_en.Get(), 201, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"Visit GitHub");
        ::GetDlgItemTextW(about_en.Get(), IDOK, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"OK");
        ::ShowWindow(about_en.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_en.Get(), nullptr, TRUE);
        ::UpdateWindow(about_en.Get());
        ::SendMessageW(about_en.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    }

    ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(kAboutWndClass, nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
}

void TestStatusBarUI() {
    // Load uxtheme.dll, dwmapi.dll, and shcore.dll via ScopedModule RAII in the test process
    const ScopedModule uxtheme_mod(::LoadLibraryW(L"uxtheme.dll"));
    const ScopedModule dwmapi_mod(::LoadLibraryW(L"dwmapi.dll"));
    const ScopedModule shcore_mod(::LoadLibraryW(L"shcore.dll"));

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
    // Move to out-of-range X coordinate while hovered_zone_ >= 0 (exercises UpdateTooltipForZone(-1) branch)
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(300, 18));
    QIQI_TEST_CHECK(bar.HoveredZone() == -1 && bar.ActiveTooltipText().empty());
    ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(145, 18));

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

    // 5. Test themed right-click context menu & WM_RBUTTONUP / WM_NCRBUTTONUP / WM_COMMAND / WM_SETTINGCHANGE
    ScopedGdiHandle<HBRUSH> menu_brush;
    ScopedMenu popup_menu = bar.BuildContextMenu(menu_brush);
    QIQI_TEST_CHECK(popup_menu.IsValid() && menu_brush.IsValid());
    QIQI_TEST_CHECK(::GetMenuItemCount(popup_menu.Get()) == 13); // 11 items + 2 separators
    QIQI_TEST_CHECK((::GetMenuState(popup_menu.Get(),
        static_cast<UINT>(LangBarMenuId::ToggleStatusBar), MF_BYCOMMAND) & MF_CHECKED) != 0);

    ::SetTimer(nullptr, 3, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        ::EndMenu();
    });
    ::SendMessageW(bar.WindowHandle(), WM_RBUTTONUP, 0, MAKELPARAM(35, 18));

    ::SetTimer(nullptr, 4, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        ::EndMenu();
    });
    ::SendMessageW(bar.WindowHandle(), WM_NCRBUTTONUP, HTCAPTION, MAKELPARAM(grip_screen.x, grip_screen.y));

    ::SendMessageW(bar.WindowHandle(), WM_COMMAND, MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage), 0), 0);
    QIQI_TEST_CHECK(clicked_action == LangBarMenuId::ToggleUiLanguage);

    // Test DestroyBarWindow followed by re-Show(true) when pos_initialized_ is already true
    bar.DestroyBarWindow();
    bar.Show(true);
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);

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

void TestStatusBarBoundariesAtDifferentDpis() {
    const auto original_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(original_monitor);
    });
    const std::array<LangBarMenuId, 4> actions{
        LangBarMenuId::ToggleChinese, LangBarMenuId::ToggleFullWidth,
        LangBarMenuId::TogglePunctuation, LangBarMenuId::OpenSettings
    };

    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1920, 1080}, dpi});
        std::vector<LangBarMenuId> clicked;
        StatusBarUI bar(::GetModuleHandleW(nullptr), [&clicked](LangBarMenuId action) {
            clicked.push_back(action);
        });
        LangBarState state{};
        state.show_status_bar = true;
        for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
            state.ui_language = language;
            bar.UpdateState(state);
            QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);
            QIQI_TEST_CHECK(bar.CurrentDpi() == dpi);
            QIQI_TEST_CHECK(bar.CurrentWidth() == ScalePxByDpi(StatusBarUI::kBarWidth, dpi));
            QIQI_TEST_CHECK(bar.CurrentHeight() == ScalePxByDpi(StatusBarUI::kBarHeight, dpi));

            const auto& strings = GetUiStrings(language);
            const std::array<const wchar_t*, 5> tips{
                strings.tip_grip, strings.tip_mode, strings.tip_width,
                strings.tip_punct, strings.tip_settings
            };
            const int offset = ScalePxByDpi(StatusBarUI::kButtonsOffset, dpi);
            const int cell_width = ScalePxByDpi(StatusBarUI::kCellWidth, dpi);
            for (size_t zone = 0; zone < tips.size(); ++zone) {
                const int left = zone == 0 ? 0 : offset + (static_cast<int>(zone) - 1) * cell_width;
                const int right = zone == 0 ? offset : left + cell_width;
                for (const int x : {left, right - 1}) {
                    QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(x)) == tips[zone]);
                    const size_t previous_clicks = clicked.size();
                    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
                    QIQI_TEST_CHECK(clicked.size() == previous_clicks + (zone == 0 ? 0 : 1));
                    if (zone != 0) {
                        QIQI_TEST_CHECK(clicked.back() == actions[zone - 1]);
                    }
                }
            }
            for (const int x : {-1, bar.CurrentWidth(), bar.CurrentWidth() + cell_width}) {
                QIQI_TEST_CHECK(std::wstring(bar.GetTooltipTextForX(x)).empty());
                const size_t previous_clicks = clicked.size();
                ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
                QIQI_TEST_CHECK(clicked.size() == previous_clicks);
            }
        }
    }
}

void TestStatusBarContextMenuState() {
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const PrimaryDict dict : {PrimaryDict::Jidian, PrimaryDict::Haifeng, PrimaryDict::BuiltinOnly}) {
            for (const bool enabled : {false, true}) {
                LangBarState state{};
                state.ui_language = language;
                state.primary_dict = dict;
                state.chinese_mode = enabled;
                state.full_width = enabled;
                state.chinese_punct = enabled;
                state.enable_jidian_extra = enabled;
                // Menu construction must also work without creating a status bar window.
                bar.UpdateState(state);
                const auto& strings = GetUiStrings(language);
                struct ExpectedItem {
                    LangBarMenuId id;
                    const wchar_t* text;
                    bool checked;
                };
                const std::array<ExpectedItem, 11> expected{{
                    {LangBarMenuId::ToggleChinese, strings.menu_toggle_chinese, enabled},
                    {LangBarMenuId::ToggleFullWidth, strings.menu_toggle_full_width, enabled},
                    {LangBarMenuId::TogglePunctuation, strings.menu_toggle_punct, enabled},
                    {LangBarMenuId::DictJidian, strings.menu_dict_jidian, dict == PrimaryDict::Jidian},
                    {LangBarMenuId::DictHaifeng, strings.menu_dict_haifeng, dict == PrimaryDict::Haifeng},
                    {LangBarMenuId::ToggleExtraDict, strings.menu_toggle_extra_dict, dict == PrimaryDict::Jidian && enabled},
                    {LangBarMenuId::ToggleStatusBar, strings.menu_toggle_status_bar, false},
                    {LangBarMenuId::ToggleUiLanguage, strings.menu_toggle_ui_lang, false},
                    {LangBarMenuId::OpenSettings, strings.menu_open_settings, false},
                    {LangBarMenuId::OpenConfigFile, strings.menu_open_config_file, false},
                    {LangBarMenuId::OpenAbout, strings.menu_open_about, false}
                }};
                ScopedGdiHandle<HBRUSH> brush;
                const ScopedMenu menu = bar.BuildContextMenu(brush);
                QIQI_TEST_CHECK(menu.IsValid() && brush.IsValid());
                QIQI_TEST_CHECK(::GetMenuItemCount(menu.Get()) == 13);
                for (const UINT position : {3u, 7u}) {
                    QIQI_TEST_CHECK((::GetMenuState(menu.Get(), position, MF_BYPOSITION) & MF_SEPARATOR) != 0);
                }
                for (size_t i = 0; i < expected.size(); ++i) {
                    const auto& item = expected[i];
                    const int position = static_cast<int>(i + (i >= 3 ? 1 : 0) + (i >= 6 ? 1 : 0));
                    QIQI_TEST_CHECK(::GetMenuItemID(menu.Get(), position) == static_cast<UINT>(item.id));
                    const UINT flags = ::GetMenuState(menu.Get(), static_cast<UINT>(item.id), MF_BYCOMMAND);
                    QIQI_TEST_CHECK(flags != static_cast<UINT>(-1));
                    QIQI_TEST_CHECK(((flags & MF_CHECKED) != 0) == item.checked);
                    if (item.id == LangBarMenuId::ToggleExtraDict) {
                        QIQI_TEST_CHECK(((flags & MF_GRAYED) != 0) == (dict != PrimaryDict::Jidian));
                    }
                    wchar_t text[256]{};
                    QIQI_TEST_CHECK(::GetMenuStringW(menu.Get(), static_cast<UINT>(item.id), text, 256, MF_BYCOMMAND) > 0);
                    QIQI_TEST_CHECK(std::wstring(text) == item.text);
                }
                MENUINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = MIM_BACKGROUND;
                QIQI_TEST_CHECK(::GetMenuInfo(menu.Get(), &info));
                QIQI_TEST_CHECK(info.hbrBack == brush.Get());
                QIQI_TEST_CHECK(bar.WindowHandle() == nullptr);
            }
        }
    }
}

void TestStatusBarTooltipLifecycle() {
    StatusBarUI bar(::GetModuleHandleW(nullptr));
    bar.Show(true);
    QIQI_TEST_CHECK(bar.WindowHandle() != nullptr);
    QIQI_TEST_CHECK(::IsWindowVisible(bar.WindowHandle()));
    QIQI_TEST_CHECK(::SendMessageW(bar.WindowHandle(), WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    const int x = ScalePxByDpi(StatusBarUI::kButtonsOffset + 1, bar.CurrentDpi());
    const auto show_tooltip = [&bar, x]() {
        ::SendMessageW(bar.WindowHandle(), WM_MOUSELEAVE, 0, 0);
        ::SendMessageW(bar.WindowHandle(), WM_MOUSEMOVE, 0, MAKELPARAM(x, 1));
        QIQI_TEST_CHECK(bar.HoveredZone() == 1);
        QIQI_TEST_CHECK(!bar.ActiveTooltipText().empty());
        QIQI_TEST_CHECK(bar.TooltipWindowHandle() != nullptr);
        QIQI_TEST_CHECK(::IsWindowVisible(bar.TooltipWindowHandle()));
        QIQI_TEST_CHECK(::SendMessageW(bar.TooltipWindowHandle(), WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    };
    const auto check_tooltip_hidden = [&bar]() {
        QIQI_TEST_CHECK(bar.ActiveTooltipText().empty());
        QIQI_TEST_CHECK(!::IsWindowVisible(bar.TooltipWindowHandle()));
    };

    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 1));
    check_tooltip_hidden();
    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_MOVE, 0, MAKELPARAM(100, 100));
    check_tooltip_hidden();
    show_tooltip();
    ::SendMessageW(bar.WindowHandle(), WM_NCMOUSELEAVE, 0, 0);
    QIQI_TEST_CHECK(bar.HoveredZone() == -1);
    check_tooltip_hidden();
    show_tooltip();
    bar.Show(false);
    QIQI_TEST_CHECK(!bar.IsVisible() && !::IsWindowVisible(bar.WindowHandle()));
    check_tooltip_hidden();

    bar.Show(true);
    show_tooltip();
    const HWND old_window = bar.WindowHandle();
    const HWND old_tooltip = bar.TooltipWindowHandle();
    bar.DestroyBarWindow();
    QIQI_TEST_CHECK(!::IsWindow(old_window) && !::IsWindow(old_tooltip));
    QIQI_TEST_CHECK(bar.WindowHandle() == nullptr && bar.TooltipWindowHandle() == nullptr);
    QIQI_TEST_CHECK(!bar.IsVisible() && bar.HoveredZone() == -1);
    QIQI_TEST_CHECK(bar.ActiveTooltipText().empty());
    bar.DestroyBarWindow();
    bar.Show(true);
    show_tooltip();
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

    // 3. SettingsDialog & AboutDialog centered & DPI-scaled on secondary monitor
    QiQiConfig cfg{};
    {
        const ScopedWindow dlg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(dlg.IsValid());
        RECT dlg_rc{};
        ::GetWindowRect(dlg.Get(), &dlg_rc);
        QIQI_TEST_CHECK(dlg_rc.left >= secondary_mon.work_area.left &&
                        dlg_rc.right <= secondary_mon.work_area.right);

        const ScopedWindow about_dlg(CreateAboutWindow(::GetModuleHandleW(nullptr), dlg.Get(), UiLanguage::ZhCn));
        QIQI_TEST_CHECK(about_dlg.IsValid());
        RECT about_rc{};
        ::GetWindowRect(about_dlg.Get(), &about_rc);
        QIQI_TEST_CHECK(about_rc.left >= secondary_mon.work_area.left &&
                        about_rc.right <= secondary_mon.work_area.right);
    }

    SetMonitorMetricsOverrideForTest(std::nullopt);

    // Also test QueryActiveMonitor with a non-null parent window and fallback_hwnd DPI query
    {
        const ScopedWindow parent_dlg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(parent_dlg.IsValid());
        const MonitorMetrics parent_mon = QueryActiveMonitor(parent_dlg.Get());
        QIQI_TEST_CHECK(parent_mon.dpi > 0);
        QIQI_TEST_CHECK(GetDpiForMonitorHandle(nullptr, parent_dlg.Get()) > 0);
    }
}

} // namespace

void RunCandidateUITests() {
    TestCandidateUI();
    TestLangBarButton();
    TestSettingsDialogControls();
    TestStatusBarUI();
    TestStatusBarBoundariesAtDifferentDpis();
    TestStatusBarContextMenuState();
    TestStatusBarTooltipLifecycle();
    TestMultiMonitorAndPerMonitorDpi();
}

} // namespace qiqi::test
