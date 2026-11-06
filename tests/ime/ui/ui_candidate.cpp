#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "ime/ui/candidate_ui.h"

#include <windows.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <string>
#include <thread>
#include <vector>

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

void TestCandidateUI() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));

    // QueryInterface tests
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void *dummy = nullptr;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IClassFactory, &dummy) == E_NOINTERFACE && dummy == nullptr);

    ComPtr<ITfUIElement> ui_elem;
    ComPtr<ITfCandidateListUIElement> cand_elem;
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfUIElement, &ui_elem)) && ui_elem);
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfCandidateListUIElement, &cand_elem)) && cand_elem);
    ui_elem.Reset();

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
    const RECT normal_rc{100, 100, 150, 125};
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

    std::array<UINT, 4> page_indices{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(page_indices.data(), 4, &page_cnt)) && page_cnt == 2);
    QIQI_TEST_CHECK(gsl::at(page_indices, 0) == 0 && gsl::at(page_indices, 1) == 5);

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

        // Single-page candidate list (5 items, page_size 5) does not reserve page-turn chevron buttons
        QIQI_TEST_CHECK(ui->PrevPageButtonRect().right == 0 && ui->NextPageButtonRect().right == 0);

        // Multi-page D2D chevron indicators & mouse page-turn click testing
        ui->SetCandidates(L"abcd", cands, 5, 0, 0);
        const RECT prev_rc = ui->PrevPageButtonRect();
        const RECT next_rc = ui->NextPageButtonRect();
        QIQI_TEST_CHECK(prev_rc.right > prev_rc.left && next_rc.right > next_rc.left);
        const LPARAM prev_pt = MAKELPARAM((prev_rc.left + prev_rc.right) / 2, (prev_rc.top + prev_rc.bottom) / 2);
        const LPARAM next_pt = MAKELPARAM((next_rc.left + next_rc.right) / 2, (next_rc.top + next_rc.bottom) / 2);
        // Click before page_turn_callback_ is set (exercises !page_turn_callback_ guard)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, next_pt);
        int page_turn_dir = 0; // -1 = prev, +1 = next
        ui->SetPageTurnCallback([&page_turn_dir](bool next) { page_turn_dir = next ? 1 : -1; });
        // On page 0, clicking prev chevron is disabled (no-op), clicking next chevron turns to next page (+1)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, prev_pt);
        QIQI_TEST_CHECK(page_turn_dir == 0);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, next_pt);
        QIQI_TEST_CHECK(page_turn_dir == 1);

        // On last page (page 1 of 2), clicking next chevron is disabled, clicking prev chevron turns to prev page (-1)
        page_turn_dir = 0;
        ui->SetCandidates(L"abcd", cands, 5, 1, 0);
        const RECT p1_prev_rc = ui->PrevPageButtonRect();
        const RECT p1_next_rc = ui->NextPageButtonRect();
        const LPARAM p1_prev_pt = MAKELPARAM((p1_prev_rc.left + p1_prev_rc.right) / 2, (p1_prev_rc.top + p1_prev_rc.bottom) / 2);
        const LPARAM p1_next_pt = MAKELPARAM((p1_next_rc.left + p1_next_rc.right) / 2, (p1_next_rc.top + p1_next_rc.bottom) / 2);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, p1_next_pt);
        QIQI_TEST_CHECK(page_turn_dir == 0);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, p1_prev_pt);
        QIQI_TEST_CHECK(page_turn_dir == -1);
    }

    const RECT far_rc{99999, 99999, 100050, 100025};
    ui->MoveWindowTo(far_rc);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui->SetUiElementId(42);
    QIQI_TEST_CHECK(ui->UiElementId() == 42);

    // Verify GDI fallback PaintPopup path when Direct2D target is unavailable (multi-page, single-page, and empty)
    ui->DisableDirect2DForTest();
    QIQI_TEST_CHECK(!ui->IsDirectWriteActive());
    ui->SetCandidates(L"abcd", cands, 5, 0, 0);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
        ::UpdateWindow(ui->WindowHandle());
    }
    ui->SetCandidates(L"xy", {}, 5, 0, 0);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
        ::UpdateWindow(ui->WindowHandle());
    }

    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == FALSE);
    const HWND old_window = ui->WindowHandle();
    QIQI_TEST_CHECK(old_window && ui->DestroyPopupWindow());
    QIQI_TEST_CHECK(!::IsWindow(old_window) && !ui->WindowHandle() && ui->DestroyPopupWindow());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
    const HWND recreated = ui->WindowHandle();
    bool wrong_thread_destroyed = true;
    {
        const std::jthread worker([&]() { wrong_thread_destroyed = ui->DestroyPopupWindow(); });
    }
    QIQI_TEST_CHECK(!wrong_thread_destroyed && ui->WindowHandle() == recreated && ::IsWindow(recreated));
    QIQI_TEST_CHECK(::DestroyWindow(recreated) && !ui->WindowHandle());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
    QIQI_TEST_CHECK(ui->DestroyPopupWindow());

    cand_elem.Reset();
    QIQI_TEST_CHECK(ui.Detach()->Release() == 0);
}

} // namespace

void RunCandidateUITests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCandidateUI();
}

} // namespace qiqi::test
