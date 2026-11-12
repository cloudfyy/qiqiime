// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void ExerciseGuiCommands(const DictManagerWindow& window) {
    // Type a phrase into EditWordInput -> verify AutoCode fills EditCodeInput with "kwwl"
    InvokeCommand(window, NewWord, true);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(EditWordInput), EN_CHANGE), 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");

    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) >= 1);

    // Select first row, promote phrase frequency, and filter by PhraseFreq source (combo index 4)
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Promote, true);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);

    // Pin entry and verify Pinned source filter (combo index 2).
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Pin, true);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);

    // Query LVN_GETDISPINFOW for all 4 columns
    const HWND list = Child(window, List);
    for (int col = std::to_underlying(ColumnId::Code); col <= std::to_underlying(ColumnId::Rank); ++col) {
        std::array<wchar_t, 256> cell_buf{};
        NMLVDISPINFOW disp{};
        disp.hdr.hwndFrom = list;
        disp.hdr.idFrom = gsl::narrow_cast<UINT_PTR>(std::to_underlying(List));
        disp.hdr.code = LVN_GETDISPINFOW;
        disp.item.mask = LVIF_TEXT;
        disp.item.iItem = 0;
        disp.item.iSubItem = col;
        disp.item.pszText = cell_buf.data();
        disp.item.cchTextMax = gsl::narrow_cast<int>(cell_buf.size());
        ::SendMessageW(window.Handle(), WM_NOTIFY, disp.hdr.idFrom, reinterpret_cast<LPARAM>(&disp));
        QIQI_TEST_CHECK(gsl::at(cell_buf, 0) != L'\0');
    }

    // Exercise filters, menu/toolbar commands, keyboard shortcuts, and display changes.
    const auto search = [&](const wchar_t* query, int count) {
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, SearchEdit), query));
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == count);
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"中华人民共和国");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");
    };
    search(L" KW ", 1);
    search(L"人民", 1);
    for (const auto language : {LanguageEn, LanguageZh}) {
        InvokeCommand(window, language);
        QIQI_TEST_CHECK(ControlText(Child(window, SearchEdit)) == L"人民");
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
        QIQI_TEST_CHECK(ControlText(Child(window, SearchLabel)) == (language == LanguageEn ? L"Search" : L"搜索"));
        std::array<wchar_t, 128> cue{};
        QIQI_TEST_CHECK(::SendMessageW(Child(window, SearchEdit), EM_GETCUEBANNER,
            reinterpret_cast<WPARAM>(cue.data()), gsl::narrow_cast<LPARAM>(cue.size())));
        QIQI_TEST_CHECK(std::wstring_view(cue.data()) == (language == LanguageEn
            ? L"Wubi code or word (letters search codes)" : L"五笔编码或词条（纯字母按编码搜索）"));
    }
    search(L"不存在", 0);
    search(L"KW人民", 0);
    search(L"k w", 0);
    search(L"KWWL", 1);
    search(L" \t ", 1);
    search(L"", 1);

    InvokeCommand(window, AutoCode);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, SelectAll);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
    InvokeCommand(window, Copy, true);
    InvokeCommand(window, Reload, true);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);

    // Keyboard shortcuts on ListView (Ctrl+A, Ctrl+C, VK_DELETE)
    {
        std::array<BYTE, 256> keys{};
        QIQI_TEST_CHECK(::GetKeyboardState(keys.data()));
        const auto restore_keys = gsl::finally([&keys]() noexcept { ::SetKeyboardState(keys.data()); });
        std::array<BYTE, 256> ctrl_down = keys;
        gsl::at(ctrl_down, VK_CONTROL) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(ctrl_down.data()));
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
    }

    // Context menu open & cancel
    {
        const UINT_PTR cancel_timer = ::SetTimer(window.Handle(), 9, 15, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(owner, id);
            ::SendMessageW(owner, WM_CANCELMODE, 0, 0);
        });
        QIQI_TEST_CHECK(cancel_timer != 0);
        const auto kill_timer = gsl::finally([&window, cancel_timer]() noexcept {
            ::KillTimer(window.Handle(), cancel_timer);
        });
        ::SendMessageW(window.Handle(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list), MAKELPARAM(-1, -1));
    }

    // DPI, theme, and display recovery messages
    RECT wnd_rect{};
    ::GetWindowRect(window.Handle(), &wnd_rect);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&wnd_rect));
    ::SendMessageW(window.Handle(), WM_DISPLAYCHANGE, 0, 0);
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);

    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Pin); // Unpin when already pinned.
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    // Switch language between EnUs and ZhCn
    InvokeCommand(window, LanguageEn);
    QIQI_TEST_CHECK(MenuText(::GetMenu(window.Handle()), Reload) == L"Reload");
    InvokeCommand(window, LanguageZh);
    QIQI_TEST_CHECK(MenuText(::GetMenu(window.Handle()), Reload) == L"刷新");

    // Clear frequencies, switch back to UserDict (combo index 1), delete entry
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    InvokeCommand(window, ClearFreq);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Delete, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    InvokeCommand(window, Exit);
    DrainQuitMessages();
}

} // namespace qiqi::dict_manager::tests
