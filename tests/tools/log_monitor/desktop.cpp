// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

void TestDetailsSplitter(const MonitorWindow& window) {
    const HWND splitter = Child(window, DetailsSplitter);
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(splitter);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 118) == nullptr);
    const auto bounds = [](HWND control) {
        RECT rect{};
        QIQI_TEST_CHECK(::GetWindowRect(control, &rect));
        return rect;
    };
    const auto height = [&](HWND control) {
        const RECT rect = bounds(control);
        return rect.bottom - rect.top;
    };
    const int count = ListView_GetItemCount(list);
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto details = ControlText(Child(window, Details));
    const auto search = ControlText(Child(window, Search));
    const auto pid = ControlText(Child(window, Pid));
    const auto level = ::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    const int initial_height = height(Child(window, Details));
    const int initial_list_height = height(list);
    const auto drag = [&](int delta) {
        const int center = height(splitter) / 2;
        ::SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, center));
        QIQI_TEST_CHECK(::GetCapture() == splitter);
        ::SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, gsl::narrow_cast<int64_t>(center) + delta));
        ::SendMessageW(splitter, WM_LBUTTONUP, 0, MAKELPARAM(10, center));
        QIQI_TEST_CHECK(::GetCapture() != splitter);
    };
    ::ShowWindow(window.Handle(), SW_SHOWNOACTIVATE);
    QIQI_TEST_CHECK(::SendMessageW(splitter, WM_GETDLGCODE, 0, 0) == DLGC_WANTARROWS);
    ::SendMessageW(splitter, WM_SETCURSOR, reinterpret_cast<WPARAM>(splitter), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    QIQI_TEST_CHECK(::GetCursor() == ::LoadCursorW(nullptr, IDC_SIZENS));
    drag(-ScalePxByDpi(50, dpi));
    QIQI_TEST_CHECK(height(Child(window, Details)) > initial_height);
    QIQI_TEST_CHECK(height(list) < initial_list_height);
    const RECT list_rect = bounds(list);
    const RECT splitter_rect = bounds(splitter);
    QIQI_TEST_CHECK(list_rect.bottom == splitter_rect.top);
    QIQI_TEST_CHECK(splitter_rect.bottom == bounds(Child(window, Details)).top);
    drag(ScalePxByDpi(50, dpi));
    QIQI_TEST_CHECK(std::abs(height(Child(window, Details)) - initial_height) <= 1);
    drag(-10000);
    QIQI_TEST_CHECK(height(list) >= ScalePxByDpi(60, dpi));
    const int largest = height(Child(window, Details));
    drag(10000);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    ::SendMessageW(splitter, WM_KEYDOWN, VK_END, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == largest);
    ::SendMessageW(splitter, WM_KEYDOWN, VK_HOME, 0);
    ::SendMessageW(splitter, WM_KEYDOWN, VK_UP, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(70, dpi));
    ::SendMessageW(splitter, WM_KEYDOWN, VK_DOWN, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    ::SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 2));
    QIQI_TEST_CHECK(::GetCapture() == splitter && ::ReleaseCapture());
    ::SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, -100));
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    for (int i = 0; i < 7; ++i) ::SendMessageW(splitter, WM_KEYDOWN, VK_UP, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == count);
    QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == selected);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)) == details);
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == search);
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == pid);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0) == level);
}

void TestDesktopIntegration() {
    SetThemeDarkOverrideForTest(false);
    SetHighContrastOverrideForTest(false);
    const auto restore = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        SetMonitorMetricsOverrideForTest(std::nullopt);
    });
    const RECT negative = ClampWindowRectToWorkArea({-2200, -1200, 100, 100}, {-1600, -900, 0, 0});
    QIQI_TEST_CHECK(negative.left == -1600 && negative.top == -900 && negative.right == 0 && negative.bottom == 0);
    const RECT inside = ClampWindowRectToWorkArea({-1400, -800, -500, -100}, {-1600, -900, 0, 0});
    QIQI_TEST_CHECK(inside.left == -1400 && inside.top == -800 && inside.right == -500);
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND list = Child(window, List);
    const LONG_PTR list_style = ::GetWindowLongPtrW(list, GWL_STYLE);
    QIQI_TEST_CHECK((list_style & LVS_OWNERDATA) != 0 && (list_style & LVS_SINGLESEL) == 0);
    const UINT initial_dpi = ::GetDpiForWindow(window.Handle());
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_BIG, 0))) == ScalePxByDpi(48, initial_dpi));
    QIQI_TEST_CHECK(Child(window, Copy) == nullptr && Child(window, Toolbar) != nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 122) == nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 123) == nullptr);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(CommandName(window, Clear) == L"Clear");
    QIQI_TEST_CHECK(CommandName(window, Copy) == L"Copy");
    QIQI_TEST_CHECK(CommandName(window, LanguageEn) == GetUiStrings(UiLanguage::EnUs).dlg_lang_en);
    Click(window, DiagnosticsView);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    SelectPid(window, ::GetCurrentProcessId());
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, Copy);
    const auto single_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(single_copied.contains(L"Monitor initialized") && single_copied.contains(L"PID="));
    // Populate diagnostics without repeatedly accessing the system clipboard.
    for (int i = 0; i < 450; ++i) {
        ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    }
    SelectLanguage(window, UiLanguage::EnUs);
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 0, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 405);
    ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 405);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"Selected entries: "));
    Click(window, Copy);
    const auto multi_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(multi_copied.contains(L"Monitor initialized") && multi_copied.contains(L"Invalid PID selection") && multi_copied.contains(L"\r\n"));
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == 0);
    {
        ::SendMessageW(Child(window, Level), CB_SETCURSEL, 0, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
        std::array<BYTE, 256> keys{};
        QIQI_TEST_CHECK(::GetKeyboardState(keys.data()));
        const auto restore_keys = gsl::finally([&keys]() noexcept {
            ::SetKeyboardState(keys.data());
        });
        std::array<BYTE, 256> ctrl_down = keys;
        gsl::at(ctrl_down, VK_CONTROL) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(ctrl_down.data()));
#pragma warning(suppress : 26496) // Mutated by the Win32 subclass callback through dwRefData.
        unsigned int detail_updates = 0;
        const HWND details = Child(window, Details);
        const SUBCLASSPROC count_updates =
            [](HWND control, UINT message, WPARAM wparam, LPARAM lparam,
               UINT_PTR, DWORD_PTR context) noexcept -> LRESULT {
                if (message == WM_SETTEXT) ++*reinterpret_cast<unsigned int*>(context);
                return ::DefSubclassProc(control, message, wparam, lparam);
            };
        QIQI_TEST_CHECK(::SetWindowSubclass(details, count_updates, 91,
                                           reinterpret_cast<DWORD_PTR>(&detail_updates)));
        const auto remove_counter = gsl::finally([=]() noexcept {
            ::RemoveWindowSubclass(details, count_updates, 91);
        });
        const auto select_begin = std::chrono::steady_clock::now();
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        QIQI_TEST_CHECK(std::chrono::steady_clock::now() - select_begin < std::chrono::seconds(1));
        QIQI_TEST_CHECK(detail_updates == 1);
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 405);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        const auto bulk_copied = ReadClipboardText(window.Handle());
        QIQI_TEST_CHECK(bulk_copied.contains(L"Monitor initialized"));
        const auto copied_count = std::count(bulk_copied.begin(), bulk_copied.end(), L'\n');
        QIQI_TEST_CHECK(gsl::narrow_cast<UINT>(copied_count + 1) == ListView_GetSelectedCount(list));
        ::SendMessageW(list, WM_KEYDOWN, VK_INSERT, 0);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        Click(window, Copy);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
        const auto refreshed_copy = ReadClipboardText(window.Handle());
        size_t record_count = 0;
        const std::wstring record_marker = std::format(L"\tPID={}\tTID=", ::GetCurrentProcessId());
        for (size_t position = 0; (position = refreshed_copy.find(record_marker, position)) != std::wstring::npos;
             position += record_marker.size()) {
            ++record_count;
        }
        QIQI_TEST_CHECK(record_count == gsl::narrow_cast<size_t>(ListView_GetItemCount(list)));
        QIQI_TEST_CHECK(refreshed_copy.contains(L"Monitor initialized"));
        QIQI_TEST_CHECK(refreshed_copy.contains(std::format(L"Copied {} log record(s) to clipboard",
                                                          copied_count + 1)));
        QIQI_TEST_CHECK(refreshed_copy.size() > bulk_copied.size());
        ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    }
    ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    SelectLanguage(window, UiLanguage::EnUs);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetSelectedCount(list) == 1);
    Click(window, Pause);
    SelectLanguage(window, UiLanguage::ZhCn);
    QIQI_TEST_CHECK(CommandName(window, Clear) == L"清空");
    QIQI_TEST_CHECK(CommandName(window, Copy) == L"复制");
    QIQI_TEST_CHECK(CommandName(window, LanguageZh) == GetUiStrings(UiLanguage::ZhCn).dlg_lang_zh);
    QIQI_TEST_CHECK(CommandName(window, Pause) == L"恢复自动滚动");
    QIQI_TEST_CHECK(CommandName(window, Source) == L"输入法日志");
    QIQI_TEST_CHECK(ControlText(window.Handle()).contains(L"奇奇日志监控"));
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == L"initialized");
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0) == 4);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"信息") &&
        ControlText(Child(window, Details)).contains(L"Monitor initialized"));
    QIQI_TEST_CHECK(ControlText(Child(window, Status)).contains(L"解码失败"));
    QIQI_TEST_CHECK(ControlText(Child(window, DetailsSplitter)) == L"调整详情区高度");
    TestDetailsSplitter(window);
    SetThemeDarkOverrideForTest(true);
    for (const UINT message : {WM_THEMECHANGED, WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED}) {
        ::SendMessageW(window.Handle(), message, 0, 0);
        QIQI_TEST_CHECK(ListView_GetBkColor(list) == RGB(43, 46, 52));
        QIQI_TEST_CHECK(ListView_GetTextColor(list) == kDarkTextColor);
        QIQI_TEST_CHECK(CommandName(window, Clear) == L"清空");
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    }
    TestDarkHeader(list);
    SetHighContrastOverrideForTest(true);
    ::SendMessageW(window.Handle(), WM_SYSCOLORCHANGE, 0, 0);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == ::GetSysColor(COLOR_WINDOW));
    QIQI_TEST_CHECK(ListView_GetTextColor(list) == ::GetSysColor(COLOR_WINDOWTEXT));
    SetHighContrastOverrideForTest(false);
    SetThemeDarkOverrideForTest(false);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == RGB(255, 255, 255));
    SelectLanguage(window, UiLanguage::Auto);
    QIQI_TEST_CHECK(CommandName(window, LanguageAuto) == GetUiStrings(UiLanguage::Auto).dlg_lang_auto);
    QIQI_TEST_CHECK(CommandName(window, Clear) ==
        (ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs ? L"Clear" : L"清空"));
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, 0, 0);
    QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(LanguageAuto), MF_BYCOMMAND) & MF_CHECKED) != 0);
    const int original_column = ListView_GetColumnWidth(list, 0);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{-2560, 0, 0, 1440}, 192});
    RECT proposed{-2700, -100, 100, 1600};
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&proposed));
    RECT actual{};
    ::GetWindowRect(window.Handle(), &actual);
    // The real desktop may impose a smaller maximum track size than the simulated monitor.
    QIQI_TEST_CHECK(actual.left >= -2560 && actual.top >= 0 && actual.right <= 0 && actual.bottom <= 1440);
    QIQI_TEST_CHECK(actual.right > actual.left && actual.bottom > actual.top);
    const int scaled_column = ::MulDiv(original_column, 192, gsl::narrow_cast<int>(initial_dpi));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == scaled_column);
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_BIG, 0))) == 96);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&proposed));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == scaled_column);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{80, 40, 1200, 820}, 96});
    ::SendMessageW(window.Handle(), WM_DISPLAYCHANGE, 0, 0);
    ::GetWindowRect(window.Handle(), &actual);
    QIQI_TEST_CHECK(actual.left >= 80 && actual.top >= 40 && actual.right <= 1200 && actual.bottom <= 820);
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_SMALL, 0))) == 16);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{100, 70, 900, 570}, 96});
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    ::GetWindowRect(window.Handle(), &actual);
    QIQI_TEST_CHECK(actual.left >= 100 && actual.top >= 70 && actual.right <= 900 && actual.bottom <= 570);
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == L"initialized");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    const auto executable = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMELogMonitor.exe";
    const ScopedModule resource(::LoadLibraryExW(executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    QIQI_TEST_CHECK(resource);
    const HRSRC group = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    QIQI_TEST_CHECK(group);
    const HGLOBAL loaded = ::LoadResource(resource.Get(), group);
    QIQI_TEST_CHECK(loaded != nullptr);
    const std::span<const BYTE> bytes(static_cast<const BYTE*>(::LockResource(loaded)), ::SizeofResource(resource.Get(), group));
    QIQI_TEST_CHECK(bytes.size() >= 6);
    WORD count = 0;
    std::memcpy(&count, bytes.subspan(4, 2).data(), 2);
    QIQI_TEST_CHECK(bytes.size() >= 6 + size_t{count} * 14);
    bool high_resolution = false;
    WORD monitor_icon_id = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto entry = bytes.subspan(6 + i * 14, 14);
        if (entry.front() == 0 && gsl::at(entry, 1) == 0) {
            high_resolution = true;
            std::memcpy(&monitor_icon_id, entry.subspan(12, 2).data(), 2);
        }
    }
    QIQI_TEST_CHECK(high_resolution && monitor_icon_id != 0);
    const auto settings_exe = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMESettings.exe";
    if (std::filesystem::exists(settings_exe)) {
        const ScopedModule settings_res(::LoadLibraryExW(settings_exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
        QIQI_TEST_CHECK(settings_res.IsValid());
        const HRSRC mon_frame = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(monitor_icon_id), RT_ICON);
        const HRSRC set_frame = ::FindResourceW(settings_res.Get(), MAKEINTRESOURCEW(monitor_icon_id), RT_ICON);
        QIQI_TEST_CHECK(mon_frame != nullptr && set_frame != nullptr);
        const DWORD mon_size = ::SizeofResource(resource.Get(), mon_frame);
        const DWORD set_size = ::SizeofResource(settings_res.Get(), set_frame);
        const auto* mon_ptr = static_cast<const BYTE*>(::LockResource(::LoadResource(resource.Get(), mon_frame)));
        const auto* set_ptr = static_cast<const BYTE*>(::LockResource(::LoadResource(settings_res.Get(), set_frame)));
        QIQI_TEST_CHECK(mon_ptr != nullptr && set_ptr != nullptr);
        const bool distinct_icons = (mon_size != set_size) || (std::memcmp(mon_ptr, set_ptr, mon_size) != 0);
        QIQI_TEST_CHECK(distinct_icons);
    }
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
}

} // namespace qiqi::monitor::tests
