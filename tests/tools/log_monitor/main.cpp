#include "common/test_support.h"
#include "common/file_util.h"
#include "common/guids.h"
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

void SetProcessFilter(const MonitorWindow& window, const wchar_t* name) {
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, Process), name));
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_EDITCHANGE), 0);
}

void SelectPid(const MonitorWindow& window, DWORD pid) {
    const auto text = std::to_wstring(pid);
    const LRESULT index = ::SendMessageW(Child(window, Pid), CB_FINDSTRINGEXACT, gsl::narrow_cast<WPARAM>(-1),
                                        reinterpret_cast<LPARAM>(text.c_str()));
    QIQI_TEST_CHECK(index != CB_ERR);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(index), 0) != CB_ERR);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
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

struct AboutExpectation {
    HWND owner = nullptr;
    UiLanguage language = UiLanguage::Auto;
    RECT work_area{};
    bool seen = false;
};

thread_local AboutExpectation* current_about = nullptr;

std::vector<DWORD> IconPixels(HICON icon) {
    ICONINFO info{};
    QIQI_TEST_CHECK(icon && ::GetIconInfo(icon, &info));
    const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
    const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
    BITMAP bitmap{};
    QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = bitmap.bmWidth;
    dib.bmiHeader.biHeight = -bitmap.bmHeight;
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(gsl::narrow_cast<size_t>(bitmap.bmWidth) * gsl::narrow_cast<size_t>(bitmap.bmHeight));
    const ScopedWindowDC dc;
    QIQI_TEST_CHECK(dc);
    QIQI_TEST_CHECK(::GetDIBits(dc.Get(), color.Get(), 0, gsl::narrow_cast<UINT>(bitmap.bmHeight),
        pixels.data(), &dib, DIB_RGB_COLORS) == bitmap.bmHeight);
    return pixels;
}

void TestAboutDialogs() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<RECT> work_areas;
    QIQI_TEST_CHECK(::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            QIQI_TEST_CHECK(::GetMonitorInfoW(monitor, &info));
            reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&work_areas)));
    QIQI_TEST_CHECK(!work_areas.empty());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    for (const RECT& work : work_areas) {
        const int width = std::min<LONG>(640, work.right - work.left);
        const int height = std::min<LONG>(480, work.bottom - work.top);
        QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr,
            work.left + (work.right - work.left - width) * 3 / 4,
            work.top + (work.bottom - work.top - height) / 3,
            width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW));
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            SelectLanguage(window, language);
            AboutExpectation expected{window.Handle(), language, work};
            auto* previous = std::exchange(current_about, &expected);
            const auto restore = gsl::finally([previous]() noexcept { current_about = previous; });
            const UINT_PTR timer = ::SetTimer(window.Handle(), 2, 15, [](HWND timer_owner, UINT, UINT_PTR id, DWORD) noexcept {
                const HWND dialog = test::FindThreadWindowByClass(kAboutWndClass);
                if (!dialog) return;
                QIQI_TEST_CHECK(current_about != nullptr);
                QIQI_TEST_CHECK(::GetWindow(dialog, GW_OWNER) == current_about->owner);
                const auto& strings = GetUiStrings(current_about->language);
                QIQI_TEST_CHECK(ControlText(dialog) == strings.about_title);
                QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, 201)) == strings.about_btn_github);
                QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, IDOK)) == strings.dlg_btn_ok);
                const UINT dpi = ::GetDpiForWindow(dialog);
                const ScopedIcon expected_icon(static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(1), IMAGE_ICON, ScalePxByDpi(56, dpi), ScalePxByDpi(56, dpi), LR_DEFAULTCOLOR)));
                const auto actual_icon = reinterpret_cast<HICON>(::SendMessageW(dialog, WM_GETICON, ICON_BIG, 0));
                QIQI_TEST_CHECK(IconPixels(actual_icon) == IconPixels(expected_icon.Get()));
                RECT owner_rect{}, dialog_rect{};
                QIQI_TEST_CHECK(::GetWindowRect(current_about->owner, &owner_rect));
                QIQI_TEST_CHECK(::GetWindowRect(dialog, &dialog_rect));
                const int dialog_width = dialog_rect.right - dialog_rect.left;
                const int dialog_height = dialog_rect.bottom - dialog_rect.top;
                const POINT centered = ClampPointToWorkArea({
                    owner_rect.left + (owner_rect.right - owner_rect.left - dialog_width) / 2,
                    owner_rect.top + (owner_rect.bottom - owner_rect.top - dialog_height) / 2},
                    dialog_width, dialog_height, current_about->work_area);
                QIQI_TEST_CHECK(std::abs(dialog_rect.left - centered.x) <= 2);
                QIQI_TEST_CHECK(std::abs(dialog_rect.top - centered.y) <= 2);
                QIQI_TEST_CHECK(::MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST) ==
                    ::MonitorFromWindow(current_about->owner, MONITOR_DEFAULTTONEAREST));
                current_about->seen = true;
                ::KillTimer(timer_owner, id);
                ::SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            });
            QIQI_TEST_CHECK(timer != 0);
            const auto cancel_timer = gsl::finally([&window, timer]() noexcept { ::KillTimer(window.Handle(), timer); });
            Click(window, About);
            QIQI_TEST_CHECK(expected.seen);
        }
    }
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

    EventBuffer processes(3, 4096);
    auto first_process = MakeEvent();
    first_process.pid = 10;
    QIQI_TEST_CHECK(processes.Push(first_process));
    QIQI_TEST_CHECK(processes.Push(first_process));
    auto second_process = first_process;
    second_process.pid = 20;
    second_process.process = L"code.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    auto choices = CollectProcessChoices(processes, L"CODE");
    QIQI_TEST_CHECK(choices.names.size() == 1 && choices.pids == std::vector<DWORD>({10, 20}));
    second_process.pid = 30;
    second_process.process = L"notepad.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").pids == std::vector<DWORD>({10, 20, 30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"notepad").pids == std::vector<DWORD>({30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"missing").pids.empty());
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"code").pids == std::vector<DWORD>({20}));
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names == std::vector<std::wstring>({L"notepad.exe"}));
    processes.Clear();
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names.empty() &&
                    CollectProcessChoices(processes, L"").pids.empty());

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
    std::vector<Event> full_buffer;
    full_buffer.reserve(10'000);
    for (int i = 0; i < 10'000; ++i) {
        full_buffer.push_back(MakeEvent(std::format(L"bulk-record-{} {}", i, std::wstring(256, L'x'))));
    }
    const auto full_copy = FormatEventsForClipboard(full_buffer);
    QIQI_TEST_CHECK(full_copy.size() > 1'000'000);
    QIQI_TEST_CHECK(std::count(full_copy.begin(), full_copy.end(), L'\n') == 9'999);
    QIQI_TEST_CHECK(full_copy.contains(L"bulk-record-0 ") && full_copy.contains(L"bulk-record-9999 "));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, full_copy) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == full_copy);
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
    const LRESULT process_edit_height = ::SendMessageW(Child(window, Process), CB_GETITEMHEIGHT,
                                                      gsl::narrow_cast<WPARAM>(-1), 0);
    const WORD doubled_dpi = gsl::narrow<WORD>(gsl::narrow_cast<uint64_t>(dpi) * 2);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(doubled_dpi, doubled_dpi), reinterpret_cast<LPARAM>(&rect));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == column_width * 2);
    for (const auto id : {Process, Pid, Level, Language}) {
        QIQI_TEST_CHECK(::SendMessageW(Child(window, id), CB_GETITEMHEIGHT, 0, 0) ==
                        ScalePxByDpi(22, doubled_dpi));
        const LRESULT edit_height = ::SendMessageW(Child(window, id), CB_GETITEMHEIGHT,
                                                   gsl::narrow_cast<WPARAM>(-1), 0);
        if (id == Process) {
            QIQI_TEST_CHECK(std::abs(edit_height - process_edit_height * 2) <= 3);
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
}

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
    SelectPid(window, ::GetCurrentProcessId());
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, Copy);
    const auto single_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(single_copied.contains(L"Monitor initialized") && single_copied.contains(L"PID="));
    for (int i = 0; i < 450; ++i) Click(window, Copy);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 2);
    ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 2);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"Selected entries: "));
    Click(window, Copy);
    const auto multi_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(multi_copied.contains(L"Monitor initialized") && multi_copied.contains(L"Copied 1 log record(s) to clipboard") && multi_copied.contains(L"\r\n"));
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == 0);
    {
        std::array<BYTE, 256> keys{};
        QIQI_TEST_CHECK(::GetKeyboardState(keys.data()));
        const auto restore_keys = gsl::finally([&keys]() noexcept {
            ::SetKeyboardState(keys.data());
        });
        std::array<BYTE, 256> ctrl_down = keys;
        gsl::at(ctrl_down, VK_CONTROL) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(ctrl_down.data()));
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
    }
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Language), CBN_SELCHANGE), 0);
    ::SendMessageW(Child(window, Language), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Language), CBN_SELCHANGE), 0);
    SelectLanguage(window, UiLanguage::EnUs);
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
    QIQI_TEST_CHECK(ControlText(Child(window, DetailsSplitter)) == L"调整详情区高度");
    TestDetailsSplitter(window);
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

int Integration(const wchar_t* peer) {
    Diagnostics diagnostics;
    EventQueue queue{32, 128 * 1024};
    Collector collector(queue, diagnostics);
    // Initialize with default Level::info to verify that Collector's TRACE_LEVEL_VERBOSE session
    // receives Debug/Trace events without requiring SetLevel(Level::trace/debug).
    log::InitLogger(log::Level::info);
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
    QIQI_LOG_DEBUG("monitor-verbose-with-default-info-level");
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
        const bool own_verbose = std::any_of(received.begin(), received.end(), [](const Event& event) {
            return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5;
        });
        const bool remote = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-emitter";
        });
        const bool remote_verbose = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" && event.level == 5;
        });
        if (own && own_verbose && remote && remote_verbose) break;
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
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [](const Event& event) {
        return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5 &&
               event.name == L"QiQiLogVerbose";
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-emitter";
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" &&
               event.level == 5 && event.name == L"QiQiLogVerbose";
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

    {
        MonitorWindow window;
        QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
        SelectLanguage(window, UiLanguage::EnUs);
        Click(window, Start);
        log::WriteLogMessage(log::Level::info, "filter-parent");
        SetProcessFilter(window, own->process.c_str());
        const auto parent_pid = std::to_wstring(::GetCurrentProcessId());
        for (int i = 0; i < 50 && ControlText(Child(window, Pid)) != parent_pid; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
        const LRESULT name_index = ::SendMessageW(Child(window, Process), CB_FINDSTRINGEXACT,
            gsl::narrow_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(own->process.c_str()));
        QIQI_TEST_CHECK(name_index != CB_ERR);
        ::SendMessageW(Child(window, Process), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(name_index), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)) == own->process);

        PROCESS_INFORMATION second_child{};
        QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &second_child));
        const ScopedKernelHandle second_process(second_child.hProcess);
        const ScopedKernelHandle second_thread(second_child.hThread);
        QIQI_TEST_CHECK(::WaitForSingleObject(second_process.Get(), 10'000) == WAIT_OBJECT_0);
        DWORD second_exit = 1;
        QIQI_TEST_CHECK(::GetExitCodeProcess(second_process.Get(), &second_exit) && second_exit == 0);
        for (int i = 0; i < 50 && ::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) != 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        ::SetWindowTextW(Child(window, Search), L"child-debug");
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        SelectLanguage(window, UiLanguage::ZhCn);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        SelectLanguage(window, UiLanguage::EnUs);
        ::SetWindowTextW(Child(window, Search), L"");
        SetProcessFilter(window, L"");
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        Click(window, Start);
        Click(window, Clear);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Process), CB_GETCOUNT, 0, 0) == 0);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        Click(window, Source);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)).empty());
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SetProcessFilter(window, L"QiQiIMELogMonitor.exe");
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    }

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
        qiqi::log::InitLogger(qiqi::log::Level::info);
        QIQI_LOG_DEBUG("child-debug-default-info");
        qiqi::log::WriteLogMessage(qiqi::log::Level::info, "child-emitter");
        qiqi::log::Shutdown();
        return 0;
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--integration") {
        return Integration(args.size() == 3 ? gsl::at(args, 2) : nullptr);
    }
    TestBuffersAndFilters();
    TestExport();
    TestAboutDialogs();
    TestWindowAndVersion();
    TestDesktopIntegration();
    std::cout << "[PASS] Log monitor model, GUI and version tests\n";
    return 0;
}
