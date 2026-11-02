#include "common/test_support.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/version.h"
#include "common/d2d_controls.h"
#include "common/theme.h"
#include "common/i18n.h"
#include "common/win32_ui_util.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/window.h"

#include <commctrl.h>
#include <winver.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <span>
#include <system_error>
#include <cstring>

namespace {
using namespace qiqi;
using namespace qiqi::monitor;
using enum qiqi::monitor::ControlId;

HWND Child(const MonitorWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

void Click(const MonitorWindow& window, ControlId id) noexcept {
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(id), BN_CLICKED), 0);
}

std::wstring ControlText(HWND hwnd) {
    std::wstring text(gsl::narrow_cast<size_t>(::GetWindowTextLengthW(hwnd)) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, text.data(), gsl::narrow_cast<int>(text.size()));
    text.resize(gsl::narrow_cast<size_t>(copied));
    return text;
}

void SelectLanguage(const MonitorWindow& window, UiLanguage language) noexcept {
    ::SendMessageW(Child(window, Language), CB_SETCURSEL, std::to_underlying(language), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Language), CBN_SELCHANGE), 0);
}

std::wstring ReadClipboardText(HWND owner = nullptr) {
    const ScopedClipboard clip(owner);
    QIQI_TEST_CHECK(clip.IsOpen());
    const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    QIQI_TEST_CHECK(data != nullptr);
    const ScopedGlobalLock<wchar_t> lock(data);
    QIQI_TEST_CHECK(lock.IsValid());
    return std::wstring(lock.Get());
}

void TestDarkHeader(HWND list) {
    const HWND header = ListView_GetHeader(list);
    RECT rect{};
    QIQI_TEST_CHECK(::GetClientRect(header, &rect));
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    ::SendMessageW(header, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    const int width = ListView_GetColumnWidth(list, 0);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), width / 2, 2) == RGB(54, 58, 66));
    bool readable_text = false;
    for (int y = 3; y < rect.bottom - 3 && !readable_text; ++y) {
        for (int x = 6; x < width - 6; ++x) {
            const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
            if (pixel != CLR_INVALID && GetRValue(pixel) > 180 && GetGValue(pixel) > 180 && GetBValue(pixel) > 180) {
                readable_text = true;
                break;
            }
        }
    }
    QIQI_TEST_CHECK(readable_text);
}

int IconWidth(HICON icon) {
    QIQI_TEST_CHECK(icon != nullptr);
    ICONINFO info{};
    QIQI_TEST_CHECK(::GetIconInfo(icon, &info));
    const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
    const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
    BITMAP bitmap{};
    QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
    return bitmap.bmWidth;
}

Event MakeEvent(std::wstring text = L"message") {
    FILETIME time{};
    ::GetSystemTimeAsFileTime(&time);
    Event event;
    event.time = (gsl::narrow_cast<uint64_t>(time.dwHighDateTime) << 32) + gsl::narrow_cast<uint64_t>(time.dwLowDateTime);
    event.pid = 42;
    event.tid = 84;
    event.level = 4;
    event.process = L"Code.exe";
    event.name = L"QiQiLogInfo";
    event.message = std::move(text);
    return event;
}

void TestBuffersAndFilters() {
    EventBuffer buffer(2, 4096);
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"first")));
    const uint64_t first = buffer.Events().front().id;
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"second")));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"third")));
    QIQI_TEST_CHECK(buffer.Events().size() == 2 && buffer.Evicted() == 1 && !buffer.Find(first));
    QIQI_TEST_CHECK(buffer.Find(first + 1)->message == L"second");
    const uint64_t last = buffer.Events().back().id;
    buffer.Clear();
    QIQI_TEST_CHECK(buffer.Bytes() == 0 && !buffer.Find(last));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent()));
    QIQI_TEST_CHECK(buffer.Events().front().id > last);
    QIQI_TEST_CHECK(!buffer.Push(MakeEvent(std::wstring(5000, L'x'))));
    QIQI_TEST_CHECK(buffer.Events().size() == 1 && buffer.Evicted() == 2);

    EventBuffer bytes(100, 1024);
    for (int i = 0; i < 100; ++i) bytes.Push(MakeEvent(std::wstring(150, L'x')));
    QIQI_TEST_CHECK(bytes.Bytes() <= 1024 && bytes.Events().size() < 100 && bytes.Evicted() > 0);
    auto exact_event = MakeEvent(std::wstring(150, L'x'));
    const size_t exact_bytes = exact_event.Bytes();
    EventBuffer exact(100, exact_bytes);
    QIQI_TEST_CHECK(exact.Push(std::move(exact_event)) && exact.Bytes() == exact_bytes);
    QIQI_TEST_CHECK(exact.Push(MakeEvent(std::wstring(150, L'x'))));
    QIQI_TEST_CHECK(exact.Events().size() == 1 && exact.Evicted() == 1 && exact.Bytes() == exact_bytes);
    EventBuffer too_small(100, exact_bytes - 1);
    QIQI_TEST_CHECK(!too_small.Push(MakeEvent(std::wstring(150, L'x'))) && too_small.Events().empty());
    EventBuffer capacities(10, 1024);
    auto reserved = MakeEvent();
    reserved.message.reserve(4096);
    QIQI_TEST_CHECK(!capacities.Push(std::move(reserved)));
    QIQI_TEST_CHECK(capacities.Events().empty());

    Filter filter;
    auto event = MakeEvent(L"Case sensitive? no.\tline\nnext");
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.process = L"CODE";
    filter.pid = 42;
    filter.level = 4;
    filter.text = L"NEXT";
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.pid = 43;
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.pid.reset();
    filter.level = 2;
    QIQI_TEST_CHECK(!filter.Matches(event));
    QIQI_TEST_CHECK(!ParsePid(L"") && !ParsePid(L"-1") && !ParsePid(L"4294967296") && !ParsePid(L"1x"));
    QIQI_TEST_CHECK(ParsePid(L"0") == 0 && ParsePid(L"4294967295") == MAXDWORD);
    const auto exported = ExportEvents({event}, L"test");
    QIQI_TEST_CHECK(exported.contains(version::kFullVersion) && exported.contains(version::kArch));
    QIQI_TEST_CHECK(exported.contains("\\tline\\nnext") && exported.contains("PID\tTID"));
    QIQI_TEST_CHECK(FormatTime(event.time).ends_with(L"Z"));
    auto second_event = MakeEvent(std::wstring(L"multi\0nul", 9));
    second_event.level = 2;
    second_event.pid = 99;
    const auto formatted_clip = FormatEventsForClipboard(std::array{event, second_event});
    QIQI_TEST_CHECK(formatted_clip.contains(L"PID=42\tTID=84\tInfo\tQiQiLogInfo\tCase sensitive? no.\tline\nnext\r\n"));
    QIQI_TEST_CHECK(formatted_clip.contains(L"PID=99\tTID=84\tError\tQiQiLogInfo\tmulti\x2400nul"));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, std::wstring_view(L"clip\0test", 9)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == L"clip\x2400test");
    event.message = std::wstring(1, L'\xD800');
    bool invalid = false;
    try { ExportEvents({event}, L"test"); } catch (const std::system_error&) { invalid = true; }
    QIQI_TEST_CHECK(invalid);
    EventQueue queue(5, 4096);
    std::thread producer([&] { for (int i = 0; i < 100; ++i) queue.Push(MakeEvent()); });
    producer.join();
    QIQI_TEST_CHECK(queue.Evicted() == 95 && queue.Take().size() == 5 && queue.Take().empty());
}

void TestExport() {
    const auto path = std::filesystem::temp_directory_path() /
        (L"qiqi-monitor-export-" + std::to_wstring(::GetCurrentProcessId()) + L".log");
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove(path, error);
    });
    const auto bytes = ExportEvents({MakeEvent(L"中文\tline\nend\\")}, L"test");
    QIQI_TEST_CHECK(WriteExport(path.c_str(), bytes) == ERROR_SUCCESS);
    std::string content;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    {
        const ScopedFileHandle lock(::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        QIQI_TEST_CHECK(lock && WriteExport(path.c_str(), "replacement") == ERROR_SHARING_VIOLATION);
    }
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    const auto missing = path / L"invalid.log";
    QIQI_TEST_CHECK(WriteExport(missing.c_str(), bytes) != ERROR_SUCCESS);
}

void TestWindowAndVersion() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(::GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA);
    std::array<wchar_t, 256> title{};
    ::GetWindowTextW(window.Handle(), title.data(), gsl::narrow_cast<int>(title.size()));
    QIQI_TEST_CHECK(std::wstring_view(title.data()).contains(version::kVersionW));
    Click(window, Pause);
    Click(window, Source);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    QIQI_TEST_CHECK(::GetWindowTextLengthW(Child(window, Details)) > 0);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    ::SetWindowTextW(Child(window, Pid), L"invalid");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    Click(window, Clear);
    ::SetWindowTextW(Child(window, Pid), L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    RECT before{};
    ::GetWindowRect(Child(window, Details), &before);
    ::SendMessageW(Child(window, DetailsSize), TBM_SETPOS, TRUE, 220);
    ::SendMessageW(window.Handle(), WM_HSCROLL, 0, reinterpret_cast<LPARAM>(Child(window, DetailsSize)));
    RECT after{};
    ::GetWindowRect(Child(window, Details), &after);
    QIQI_TEST_CHECK(after.bottom - after.top > before.bottom - before.top);
    const int column_width = ListView_GetColumnWidth(list, 0);
    RECT rect{};
    ::GetWindowRect(window.Handle(), &rect);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    const WORD doubled_dpi = gsl::narrow<WORD>(gsl::narrow_cast<uint64_t>(dpi) * 2);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(doubled_dpi, doubled_dpi), reinterpret_cast<LPARAM>(&rect));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == column_width * 2);
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
    QIQI_TEST_CHECK(Child(window, Copy) != nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 122) == nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 123) == nullptr);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(ControlText(Child(window, Clear)) == L"Clear");
    QIQI_TEST_CHECK(ControlText(Child(window, Copy)) == L"Copy");
    QIQI_TEST_CHECK(ControlText(Child(window, Language)) == GetUiStrings(UiLanguage::EnUs).dlg_lang_en);
    Click(window, Source);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    ::SetWindowTextW(Child(window, Pid), std::to_wstring(::GetCurrentProcessId()).c_str());
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, Copy);
    const auto single_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(single_copied.contains(L"Monitor initialized") && single_copied.contains(L"PID="));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 2);
    ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 2);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"Selected entries: "));
    Click(window, Copy);
    const auto multi_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(multi_copied.contains(L"Monitor initialized") && multi_copied.contains(L"Copied 1 log record(s) to clipboard") && multi_copied.contains(L"\r\n"));
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetSelectedCount(list) == 1);
    Click(window, Pause);
    SelectLanguage(window, UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ControlText(Child(window, Clear)) == L"清空");
    QIQI_TEST_CHECK(ControlText(Child(window, Copy)) == L"复制");
    QIQI_TEST_CHECK(ControlText(Child(window, Language)) == GetUiStrings(UiLanguage::ZhCn).dlg_lang_zh);
    QIQI_TEST_CHECK(ControlText(Child(window, Pause)) == L"恢复滚动");
    QIQI_TEST_CHECK(ControlText(Child(window, Source)) == L"输入法日志");
    QIQI_TEST_CHECK(ControlText(window.Handle()).contains(L"奇奇日志监控"));
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == L"initialized");
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0) == 4);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"信息") &&
        ControlText(Child(window, Details)).contains(L"Monitor initialized"));
    QIQI_TEST_CHECK(ControlText(Child(window, Status)).contains(L"解码失败"));
    SetThemeDarkOverrideForTest(true);
    for (const UINT message : {WM_THEMECHANGED, WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED}) {
        ::SendMessageW(window.Handle(), message, 0, 0);
        QIQI_TEST_CHECK(ListView_GetBkColor(list) == RGB(43, 46, 52));
        QIQI_TEST_CHECK(ListView_GetTextColor(list) == RGB(238, 240, 245));
        QIQI_TEST_CHECK(ControlText(Child(window, Clear)) == L"清空");
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
    QIQI_TEST_CHECK(ControlText(Child(window, Language)) == GetUiStrings(UiLanguage::Auto).dlg_lang_auto);
    QIQI_TEST_CHECK(ControlText(Child(window, Clear)) ==
        (ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs ? L"Clear" : L"清空"));
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, 0, 0);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Language), CB_GETCURSEL, 0, 0) == 0);
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
    for (size_t i = 0; i < count; ++i) {
        const auto entry = bytes.subspan(6 + i * 14, 14);
        if (entry.front() == 0 && gsl::at(entry, 1) == 0) high_resolution = true;
    }
    QIQI_TEST_CHECK(high_resolution);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
}

int Integration(const wchar_t* peer) {
    Diagnostics diagnostics;
    EventQueue queue{32, 128 * 1024};
    Collector collector(queue, diagnostics);
    log::InitLogger(log::Level::trace);
    const auto shutdown = gsl::finally([]() noexcept { log::Shutdown(); });
    if (!collector.Start()) {
        const auto errors = diagnostics.queue.Take();
        for (const auto& error : errors) std::wcerr << error.message << L"\n";
        const bool denied = std::any_of(errors.begin(), errors.end(), [](const Event& event) {
            return event.message.contains(L"error=5,");
        });
        if (denied) return 77;
        QIQI_TEST_CHECK(false);
    }
    const std::wstring session_name(collector.SessionName());
    const std::string marker = std::string("QiQi monitor exact UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd\tline\nend") + std::string("\0tail\0", 6);
    log::WriteLogMessage(log::Level::info, marker);
    diagnostics.Record(2, L"MONITOR_ONLY_SENTINEL", ERROR_INVALID_DATA);
    log::Shutdown();
    QIQI_TEST_CHECK(log::EnsureInitialized());
    log::WriteLogMessage(log::Level::warn, "after provider re-registration");

    const auto executable = peer ? std::filesystem::path(peer) : GetModuleFilePath(nullptr);
    std::wstring command = L"\"" + executable.wstring() + L"\" --emit";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const ScopedKernelHandle child(process.hProcess);
    const ScopedKernelHandle thread(process.hThread);
    QIQI_TEST_CHECK(::WaitForSingleObject(child.Get(), 10'000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    QIQI_TEST_CHECK(::GetExitCodeProcess(child.Get(), &exit) && exit == 0);
    std::vector<Event> received;
    for (int i = 0; i < 60; ++i) {
        auto batch = queue.Take();
        for (auto& event : batch) received.push_back(std::move(event));
        const bool own = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.message == Utf8ToWide(marker);
        });
        const bool remote = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-emitter";
        });
        if (own && remote) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    collector.PollLoss();
    QIQI_TEST_CHECK(collector.Stop());
    QIQI_TEST_CHECK(!collector.Running() && collector.decode_errors == 0 && collector.events_lost == 0);
    const auto own = std::find_if(received.begin(), received.end(), [&](const Event& event) {
        return event.message == Utf8ToWide(marker);
    });
    QIQI_TEST_CHECK(own != received.end());
    std::wcout << L"Decoded event: " << own->name << L", UTC=" << FormatTime(own->time)
               << L", now=" << FormatTime(MakeEvent().time) << std::endl;
    QIQI_TEST_CHECK(own->pid == ::GetCurrentProcessId() && own->tid == ::GetCurrentThreadId());
    QIQI_TEST_CHECK(own->level == 4 && !own->process.empty());
    QIQI_TEST_CHECK(own->name == L"QiQiLogInfo");
    QIQI_TEST_CHECK(own->time <= MakeEvent().time && MakeEvent().time - own->time < 60ULL * 10'000'000);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-emitter";
    }));
    QIQI_TEST_CHECK(std::none_of(received.begin(), received.end(), [](const Event& event) {
        return event.message.contains(L"MONITOR_ONLY_SENTINEL");
    }));
    struct Properties {
        EVENT_TRACE_PROPERTIES trace{};
        std::array<wchar_t, 256> name{};
    } query;
    query.trace.Wnode.BufferSize = sizeof(query);
    query.trace.LoggerNameOffset = offsetof(Properties, name);
    QIQI_TEST_CHECK(::ControlTraceW(0, session_name.c_str(), &query.trace, EVENT_TRACE_CONTROL_QUERY) == ERROR_WMI_INSTANCE_NOT_FOUND);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [](const Event& event) {
        return event.message == L"after provider re-registration" && event.level == 3;
    }));
    QIQI_TEST_CHECK(collector.Start());
    for (int i = 0; i < 1024; ++i) log::WriteLogMessage(log::Level::info, "burst-monitor");
    for (int i = 0; i < 30 && queue.Evicted() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    QIQI_TEST_CHECK(queue.Evicted() > 0 && queue.Take().size() == 32);
    QIQI_TEST_CHECK(collector.Stop());
    QIQI_TEST_CHECK(collector.decode_errors == 0);
    QIQI_TEST_CHECK(collector.Stop());
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    QIQI_TEST_CHECK(argv != nullptr);
    const std::span<wchar_t*> args(argv, gsl::narrow_cast<size_t>(argc));
    if (args.size() == 2 && (std::wstring_view(gsl::at(args, 1)) == L"--preview-dark" ||
        std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast")) {
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetThemeDarkOverrideForTest(true);
        SetHighContrastOverrideForTest(std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast");
        return RunMonitor(::GetModuleHandleW(nullptr), SW_SHOWNORMAL);
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--emit") {
        qiqi::log::InitLogger(qiqi::log::Level::trace);
        qiqi::log::WriteLogMessage(qiqi::log::Level::info, "child-emitter");
        qiqi::log::Shutdown();
        return 0;
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--integration") {
        return Integration(args.size() == 3 ? gsl::at(args, 2) : nullptr);
    }
    TestBuffersAndFilters();
    TestExport();
    TestWindowAndVersion();
    TestDesktopIntegration();
    std::cout << "[PASS] Log monitor model, GUI and version tests\n";
    return 0;
}
