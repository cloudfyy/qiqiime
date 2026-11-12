// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

void TestWindowAndVersion() {
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(::GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA);
    std::array<wchar_t, 256> title{};
    ::GetWindowTextW(window.Handle(), title.data(), gsl::narrow_cast<int>(title.size()));
    QIQI_TEST_CHECK(std::wstring_view(title.data()).contains(version::kVersionW));
    Click(window, Pause);
    Click(window, DiagnosticsView);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    QIQI_TEST_CHECK(::GetWindowTextLengthW(Child(window, Details)) > 0);
    for (int col = 0; col < 7; ++col) {
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
    ::SendMessageW(Child(window, Process), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_SELCHANGE), 0);
    ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_CLOSEUP), 0);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Category), CB_GETCOUNT, 0, 0) == 10);
    for (int col = 1; col <= 9; ++col) {
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, col, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
    }
    ::SendMessageW(Child(window, Category), CB_SETCURSEL, 0, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    {
        const UINT_PTR cancel_menu_timer = ::SetTimer(window.Handle(), 3, 15, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(owner, id);
            ::SendMessageW(owner, WM_CANCELMODE, 0, 0);
        });
        QIQI_TEST_CHECK(cancel_menu_timer != 0);
        const auto kill_timer = gsl::finally([&window, cancel_menu_timer]() noexcept {
            ::KillTimer(window.Handle(), cancel_menu_timer);
        });
        ::SendMessageW(window.Handle(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list), MAKELPARAM(-1, -1));
    }
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    SetProcessFilter(window, L"missing-process");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
    SetProcessFilter(window, L"QiQiIMELogMonitor.exe");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
    Click(window, Clear);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Process), CB_GETCOUNT, 0, 0) == 0);
    SetProcessFilter(window, L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    const int column_width = ListView_GetColumnWidth(list, 0);
    RECT rect{};
    ::GetWindowRect(window.Handle(), &rect);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    CheckComboEditFont(Child(window, Process), dpi);
    const WORD doubled_dpi = gsl::narrow<WORD>(gsl::narrow_cast<uint64_t>(dpi) * 2);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(doubled_dpi, doubled_dpi), reinterpret_cast<LPARAM>(&rect));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == column_width * 2);
    for (const auto id : {Process, Pid, Level, Category}) {
        QIQI_TEST_CHECK(::SendMessageW(Child(window, id), CB_GETITEMHEIGHT, 0, 0) ==
                        ScalePxByDpi(22, doubled_dpi));
        const LRESULT edit_height = ::SendMessageW(Child(window, id), CB_GETITEMHEIGHT,
                                                   gsl::narrow_cast<WPARAM>(-1), 0);
        if (id == Process) {
            CheckComboEditFont(Child(window, Process), doubled_dpi);
        } else {
            QIQI_TEST_CHECK(edit_height == ScalePxByDpi(22, doubled_dpi));
        }
    }
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    const auto executable = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMELogMonitor.exe";
    DWORD unused = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(executable.c_str(), &unused);
    QIQI_TEST_CHECK(size > 0);
    std::vector<BYTE> resource(size);
    QIQI_TEST_CHECK(::GetFileVersionInfoW(executable.c_str(), 0, size, resource.data()));
    VS_FIXEDFILEINFO* info = nullptr;
    UINT bytes = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(resource.data(), L"\\", reinterpret_cast<void**>(&info), &bytes));
    QIQI_TEST_CHECK(info && info->dwFileType == VFT_APP);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionMS) == version::kMajor &&
                   LOWORD(info->dwFileVersionMS) == version::kMinor &&
                   HIWORD(info->dwFileVersionLS) == version::kPatch);
    QIQI_TEST_CHECK(::DestroyWindow(window.Handle()));
    QIQI_TEST_CHECK(window.Handle() == nullptr);
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

} // namespace qiqi::monitor::tests
