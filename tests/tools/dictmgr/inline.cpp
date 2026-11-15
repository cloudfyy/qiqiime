// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestInlineEditing() {
    const auto restore_monitor = gsl::finally([]() noexcept { SetMonitorMetricsOverrideForTest(std::nullopt); });
    const auto temp_dir =
        std::filesystem::temp_directory_path() / (L"qiqi_dict_inline_" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(temp_dir);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        DrainQuitMessages();
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
    });
    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.qfreq";
    std::string source = "abcd 原始词\n";
    for (int i = 0; i < 100; ++i)
        source += "zzzz 滚动测试词" + std::to_string(i) + "\n";
    QIQI_TEST_CHECK(WriteFileAtomically(user_dict, codec::AsByteSpan(source)));

    DictManagerWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 101);
    const auto visible = [&](ControlId id) {
        return (::GetWindowLongPtrW(Child(window, id), GWL_STYLE) & WS_VISIBLE) != 0;
    };
    const auto search = [&](const wchar_t* text) {
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, SearchEdit), text));
    };
    const auto marked = [&](int row = 0) {
        std::array<wchar_t, 256> source_text{};
        ListView_GetItemText(list, row, std::to_underlying(ColumnId::Source),
            source_text.data(), gsl::narrow_cast<int>(source_text.size()));
        return std::wstring_view(source_text.data()).starts_with(L"* ");
    };
    const auto begin = [&](int row = 0, ColumnId column = ColumnId::Word) {
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, row, FALSE);
        RECT cell{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, row, std::to_underlying(column), LVIR_BOUNDS, &cell));
        NMITEMACTIVATE click{};
        click.hdr = {list, std::to_underlying(List), NM_DBLCLK};
        click.iItem = row;
        click.iSubItem = std::to_underlying(column);
        click.ptAction = {(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
        ::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(List), reinterpret_cast<LPARAM>(&click));
        QIQI_TEST_CHECK(visible(EditWordInput) && visible(EditCodeInput) && visible(InlineSave));
    };
    const auto check_position = [&](int row) {
        for (const auto& [id, column] :
             std::array{std::pair{EditCodeInput, ColumnId::Code}, std::pair{EditWordInput, ColumnId::Word},
                 std::pair{InlineSave, ColumnId::Action}}) {
            RECT cell{}, editor{}, intersection{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
            QIQI_TEST_CHECK(ListView_GetSubItemRect(list, row, std::to_underlying(column), LVIR_BOUNDS, &cell));
            ::MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&cell), 2);
            QIQI_TEST_CHECK(::GetWindowRect(Child(window, id), &editor));
            QIQI_TEST_CHECK(::IntersectRect(&intersection, &cell, &editor) && ::EqualRect(&intersection, &editor));
        }
        const auto button = ToolbarItemRect(window, SaveWord);
        RECT toolbar{};
        ::GetClientRect(Child(window, InlineSave), &toolbar);
        QIQI_TEST_CHECK(button.left >= 0 && button.top >= 0 && button.right <= toolbar.right &&
                        button.bottom <= toolbar.bottom);
        const ScopedWindowDC window_dc(window.Handle());
        QIQI_TEST_CHECK(window_dc);
        const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
        QIQI_TEST_CHECK(dc);
        const ScopedGdiHandle<HBITMAP> bitmap(
            ::CreateCompatibleBitmap(window_dc.Get(), toolbar.right, toolbar.bottom));
        QIQI_TEST_CHECK(bitmap);
        const ScopedSelectObject selected(dc.Get(), bitmap.Get());
        QIQI_TEST_CHECK(selected);
        ::SendMessageW(Child(window, InlineSave), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
        const auto foreground = IsHighContrastEnabled() ? ::GetSysColor(COLOR_BTNTEXT)
                                                       : GetCurrentThemePalette().text_primary;
        size_t ink = 0;
        for (int y = button.top + 2; y < button.bottom - 2; ++y) {
            for (int x = button.left + 2; x < button.right - 2; ++x) {
                if (::GetPixel(dc.Get(), x, y) == foreground) ++ink;
            }
        }
        QIQI_TEST_CHECK(ink > 10);
    };
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    search(L"");
    ::ShowWindow(window.Handle(), SW_SHOWNOACTIVATE);
    {
        RECT row{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS));
        const POINT icon_point{row.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) / 2,
            (row.top + row.bottom) / 2};
        const LPARAM icon = MAKELPARAM(icon_point.x, icon_point.y);
        ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, icon);
        QIQI_TEST_CHECK(::GetCapture() == list);
        ::SendMessageW(list, WM_CANCELMODE, 0, 0);
        QIQI_TEST_CHECK(::GetCapture() != list);
        ::SendMessageW(list, WM_LBUTTONUP, 0, icon);
        QIQI_TEST_CHECK(!visible(EditCodeInput));
        ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, icon);
        const POINT outside_point{row.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) + 10,
            (row.top + row.bottom) / 2};
        const LPARAM outside = MAKELPARAM(outside_point.x, outside_point.y);
        ::SendMessageW(list, WM_LBUTTONUP, 0, outside);
        QIQI_TEST_CHECK(::GetCapture() != list && !visible(EditCodeInput));
    }
    for (const auto column : {ColumnId::Code, ColumnId::Word}) {
        RECT cell{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, 0, std::to_underlying(column), LVIR_BOUNDS, &cell));
        const LPARAM point = MAKELPARAM((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2);
        // Seed the first-click selection without entering native drag tracking.
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ::SendMessageW(list, WM_LBUTTONDBLCLK, MK_LBUTTON, point);
        ::SendMessageW(list, WM_LBUTTONUP, 0, point);
        QIQI_TEST_CHECK(visible(EditCodeInput) && visible(EditWordInput));
        QIQI_TEST_CHECK(::GetFocus() == Child(window,
            column == ColumnId::Code ? EditCodeInput : EditWordInput));
        InvokeCommand(window, CancelEdit);
    }
    ClickRowEdit(window, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    ClickRowEdit(window, 1);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"zzzz" && !marked(1));
    const auto second_word = ControlText(Child(window, EditWordInput));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"放弃的行内草稿"));
    QIQI_TEST_CHECK(marked(1));
    begin(1, ColumnId::Word);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"放弃的行内草稿" && marked(1));
    ClickRowEdit(window, 1);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"放弃的行内草稿" && marked(1));
    ClickRowEdit(window, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"原始词");
    QIQI_TEST_CHECK(!marked(0) && !marked(1));
    check_position(0);
    ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ::SendMessageW(list, WM_KEYDOWN, VK_F2, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == second_word);
    InvokeCommand(window, CancelEdit);

    const int original_count = ListView_GetItemCount(list);
    InvokeCommand(window, NewWord);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == original_count + 1);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"放弃的新增草稿"));
    ClickRowEdit(window, 2);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == original_count);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"zzzz");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == second_word && !marked(1));
    check_position(1);
    InvokeCommand(window, CancelEdit);

    for (const auto target : {SourceCombo, SearchEdit}) {
        for (const bool new_word : {false, true}) {
            search(L"abcd");
            if (new_word) InvokeCommand(window, NewWord);
            else begin();
            QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, SearchEdit)) &&
                ::IsWindowEnabled(Child(window, SourceCombo)));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"筛选时放弃的草稿"));
            QIQI_TEST_CHECK(marked());
            ::SetFocus(Child(window, target));
            QIQI_TEST_CHECK(::GetFocus() == Child(window, target));
            QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(EditCodeInput) && !visible(InlineSave));
            QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
            QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && !marked());
            if (target == SearchEdit) {
                search(L"zzzz");
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 100);
            } else {
                ::SendMessageW(Child(window, SourceCombo), CB_SHOWDROPDOWN, TRUE, 0);
                QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETDROPPEDSTATE, 0, 0));
                ::SendMessageW(Child(window, SourceCombo), CB_SHOWDROPDOWN, FALSE, 0);
                ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
                ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
                ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
                ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
            }
            QIQI_TEST_CHECK(::GetFocus() == Child(window, target));
        }
    }
    search(L"abcd");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && !visible(EditWordInput));
    begin(0, ColumnId::Code);
    check_position(0);
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
    ::RedrawWindow(window.Handle(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    ::RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    {
        const ScopedWindowDC dc(Child(window, InlineSave));
        QIQI_TEST_CHECK(dc);
        const auto button = ToolbarItemRect(window, SaveWord);
        size_t ink = 0;
        for (int y = button.top + 2; y < button.bottom - 2; ++y) {
            for (int x = button.left + 2; x < button.right - 2; ++x) {
                if (::GetPixel(dc.Get(), x, y) == GetCurrentThemePalette().text_primary) ++ink;
            }
        }
        QIQI_TEST_CHECK(ink > 10);
    }
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"原始词");
    QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, SearchEdit)) && ::IsWindowEnabled(Child(window, SourceCombo)));
    QIQI_TEST_CHECK(
        (::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(Delete), MF_BYCOMMAND) & MF_GRAYED) != 0);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"暂时修改"));
    QIQI_TEST_CHECK(marked() && ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"原始词"));
    QIQI_TEST_CHECK(!marked() && !ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"temp"));
    QIQI_TEST_CHECK(marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"abcd"));
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"wxyz"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"行内新词"));
    QIQI_TEST_CHECK(marked());
    InvokeCommand(window, Reload);
    InvokeCommand(window, Delete);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"行内新词");

    WubiEngine peer;
    peer.SetUserDictPath(user_dict);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"drop").empty());
    ::SendMessageW(Child(window, EditWordInput), WM_KILLFOCUS, reinterpret_cast<WPARAM>(list), 0);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(peer.LookupExact(L"abcd").front() == L"原始词" && peer.LookupExact(L"wxyz").empty());
    ::SendMessageW(Child(window, EditWordInput), WM_IME_STARTCOMPOSITION, 0, 0);
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_ESCAPE, 0);
    QIQI_TEST_CHECK(visible(EditWordInput));
    QIQI_TEST_CHECK(marked() && ControlText(Child(window, EditWordInput)) == L"行内新词");
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz");
    ::SendMessageW(Child(window, EditWordInput), WM_IME_ENDCOMPOSITION, 0, 0);
    QIQI_TEST_CHECK((::SendMessageW(Child(window, EditWordInput), WM_GETDLGCODE, VK_RETURN, 0) & DLGC_WANTMESSAGE) !=
                    0);
    QIQI_TEST_CHECK((::SendMessageW(Child(window, EditWordInput), WM_GETDLGCODE, VK_ESCAPE, 0) & DLGC_WANTMESSAGE) !=
                    0);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 3840, 2160}, 96});
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        RECT rect{0, 0, ScalePxByDpi(1020, dpi), ScalePxByDpi(680, dpi)};
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        for (const auto language : {LanguageEn, LanguageZh}) {
            InvokeCommand(window, language);
            for (const bool dark : {false, true}) {
                SetThemeDarkOverrideForTest(dark);
                ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
                ClickRowEdit(window, 0);
                QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"行内新词");
                QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz");
                QIQI_TEST_CHECK(marked() && ControlText(Child(window, Status)).starts_with(L"* "));
                check_position(0);
                QIQI_TEST_CHECK(ControlText(Child(window, SearchEdit)) == L"abcd");
            }
        }
    }
    for (const UINT dpi : {120u, 144u, 192u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 1024, 728}, 96});
        RECT rect{0, 0, ScalePxByDpi(1020, dpi), ScalePxByDpi(680, dpi)};
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        RECT client{}, action{};
        QIQI_TEST_CHECK(::GetClientRect(list, &client));
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, 0, std::to_underlying(ColumnId::Action), LVIR_BOUNDS, &action));
        QIQI_TEST_CHECK(action.right > client.right);
        QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(EditCodeInput) && !visible(InlineSave));
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"行内新词");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz" && marked());
        QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(peer.LookupExact(L"abcd").front() == L"原始词" && peer.LookupExact(L"wxyz").empty());

        SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 3840, 2160}, 96});
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        QIQI_TEST_CHECK(visible(EditWordInput) && visible(EditCodeInput) && visible(InlineSave));
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"行内新词");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz" && marked());
        check_position(0);
    }
    SetThemeDarkOverrideForTest(std::nullopt);
    QIQI_TEST_CHECK(peer.AddUserEntry(L"aaaa", L"外部新增词"));
    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ::IsWindowEnabled(Child(window, SearchEdit)));
    QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"abcd").empty() && peer.LookupExact(L"wxyz").front() == L"行内新词");
    QIQI_TEST_CHECK(peer.LookupExact(L"aaaa").front() == L"外部新增词");

    search(L"wxyz");
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ::SendMessageW(list, WM_KEYDOWN, VK_F2, 0);
    QIQI_TEST_CHECK(visible(EditWordInput));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"a1"));
    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditCodeInput)) == L"a1");
    ::SendMessageW(Child(window, EditCodeInput), WM_KEYDOWN, VK_ESCAPE, 0);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(peer.RemoveUserEntry(L"wxyz", L"行内新词"));
    QIQI_TEST_CHECK(peer.AddUserEntry(L"wxyz", L"远端修改词"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"不能覆盖远端"));
    InvokeCommand(window, SaveWord);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"不能覆盖远端");
    QIQI_TEST_CHECK(peer.LookupExact(L"wxyz").front() == L"远端修改词");
    InvokeCommand(window, CancelEdit);

    search(L"");
    begin(0, ColumnId::Word);
    ListView_EnsureVisible(list, ListView_GetItemCount(list) - 1, FALSE);
    QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(InlineSave));
    ListView_EnsureVisible(list, 0, FALSE);
    QIQI_TEST_CHECK(visible(EditWordInput));
    check_position(0);
    const int width = ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word));
    const int wider_column = width + ScalePxByDpi(20, 192);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), wider_column);
    check_position(0);
    RECT client{};
    ::GetClientRect(list, &client);
    const int oversized_column = width + gsl::narrow_cast<int>(client.right);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), oversized_column);
    QIQI_TEST_CHECK(!visible(EditWordInput));
    ListView_Scroll(list, ScalePxByDpi(60, 192), 0);
    QIQI_TEST_CHECK(!visible(EditWordInput));
    ListView_Scroll(list, -::GetScrollPos(list, SB_HORZ), 0);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), width);
    QIQI_TEST_CHECK(visible(EditWordInput));
    check_position(0);
    InvokeCommand(window, CancelEdit);

    search(L"不存在");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    InvokeCommand(window, NewWord, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && visible(EditWordInput));
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    QIQI_TEST_CHECK(marked());
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"test"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中国"));
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"test");
    InvokeCommand(window, AutoCode);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"khlg");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"khlg");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"test"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中国"));
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"test").front() == L"中国");

    search(L"test");
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"删除后的草稿"));
    QIQI_TEST_CHECK(peer.RemoveUserEntry(L"test", L"中国"));
    for (int attempt = 0; attempt < 2; ++attempt) {
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && marked());
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"删除后的草稿");
        QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(peer.LookupExact(L"test").empty());
    }
    InvokeCommand(window, CancelEdit);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.AddUserEntry(L"test", L"中国"));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);

    search(L"test");
    begin();
    InvokeCommand(window, SaveWord);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"失败草稿"));
    {
        const ScopedFileHandle locked(::CreateFileW(user_dict.c_str(), GENERIC_READ, 0, nullptr,
                                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"失败草稿");
        QIQI_TEST_CHECK(marked());
        const auto status = ControlText(Child(window, Status));
        QIQI_TEST_CHECK(status.find(L"0x80070020") != std::wstring::npos);
        QIQI_TEST_CHECK(status.find(L"访问被拒绝") == std::wstring::npos);
    }
    {
        const DWORD attributes = ::GetFileAttributesW(user_dict.c_str());
        QIQI_TEST_CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        const auto restore_attributes = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes));
        });
        QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes | FILE_ATTRIBUTE_READONLY));
        for (const auto language : {LanguageEn, LanguageZh}) {
            InvokeCommand(window, language);
            InvokeCommand(window, SaveWord);
            QIQI_TEST_CHECK(visible(EditWordInput) && marked());
            QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"失败草稿");
            const auto status = ControlText(Child(window, Status));
            QIQI_TEST_CHECK(status.find(L"0x80070005") != std::wstring::npos);
            QIQI_TEST_CHECK(status.find(language == LanguageEn ? L"access denied" : L"访问被拒绝") != std::wstring::npos);
        }
        InvokeCommand(window, CancelEdit);
        InvokeCommand(window, NewWord);
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"deny"));
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"无权新增草稿"));
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && marked());
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"无权新增草稿");
        QIQI_TEST_CHECK(ControlText(Child(window, Status)).find(L"访问被拒绝") != std::wstring::npos);
    }
    InvokeCommand(window, CancelEdit);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"test").front() == L"中国");

    QIQI_TEST_CHECK(peer.PinUserEntry(L"test", L"置顶原词"));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"用户副本"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"copy"));
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"copy").front() == L"用户副本");
    QIQI_TEST_CHECK(peer.PinnedEntries().at(PackWubiCode(L"test")).front() == L"置顶原词");

    search(L"不存在");
    InvokeCommand(window, NewWord);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"退出确认草稿"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"exit"));
    const UINT_PTR timer = ::SetTimer(window.Handle(), 19, 20, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND popup = ::GetLastActivePopup(owner);
        if (popup != owner) {
            ::KillTimer(owner, id);
            ::PostMessageW(popup, WM_COMMAND, IDNO, 0);
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const HWND closing = window.Handle();
    InvokeCommand(window, Exit);
    if (::IsWindow(closing)) ::KillTimer(closing, timer);
    QIQI_TEST_CHECK(!::IsWindow(closing));
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"exit").empty());
    DrainQuitMessages();

    for (const int mode : {0, 1, 2, 3, 4}) {
        DictManagerWindow unchanged;
        QIQI_TEST_CHECK(unchanged.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
        if (mode < 2 || mode == 4) {
            ::SendMessageW(Child(unchanged, SourceCombo), CB_SETCURSEL, 1, 0);
            ::SendMessageW(unchanged.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, SearchEdit), L"test"));
            const HWND unchanged_list = Child(unchanged, List);
            ListView_SetItemState(unchanged_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ::SendMessageW(unchanged_list, WM_KEYDOWN, VK_F2, 0);
            QIQI_TEST_CHECK(ControlText(Child(unchanged, EditWordInput)) == L"中国");
        } else {
            InvokeCommand(unchanged, NewWord);
        }
        if (mode == 1 || mode == 3) {
            const auto original_word = ControlText(Child(unchanged, EditWordInput));
            const auto original_code = ControlText(Child(unchanged, EditCodeInput));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), L"临时更改"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), original_word.c_str()));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditCodeInput), original_code.c_str()));
        }
        if (mode == 4) {
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), L"关闭时放弃更改"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditCodeInput), L"exit"));
        }
        QIQI_TEST_CHECK(ControlText(Child(unchanged, Status)).starts_with(L"* ") == (mode == 4));
        const UINT_PTR dismiss = ::SetTimer(unchanged.Handle(), 19, 20, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND popup = ::GetLastActivePopup(owner);
            if (popup != owner) {
                ::KillTimer(owner, id);
                ::PostMessageW(popup, WM_COMMAND, IDNO, 0);
            }
        });
        QIQI_TEST_CHECK(dismiss != 0);
        const HWND handle = unchanged.Handle();
        ::SendMessageW(handle, WM_CLOSE, 0, 0);
        if (::IsWindow(handle)) ::KillTimer(handle, dismiss);
        QIQI_TEST_CHECK(!::IsWindow(handle));
        DrainQuitMessages();
    }
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"exit").empty());
    QIQI_TEST_CHECK(std::ranges::contains(peer.LookupExact(L"test"), L"中国"));
}

} // namespace qiqi::dict_manager::tests
