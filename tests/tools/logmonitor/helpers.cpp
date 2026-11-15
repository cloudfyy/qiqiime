// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

HWND Child(const MonitorWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

void Click(const MonitorWindow& window, ControlId id) {
    const HWND toolbar = Child(window, Toolbar);
    if (::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) >= 0) {
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
        RECT rect{};
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETRECT, std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
        const LPARAM point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
        ::SendMessageW(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, point);
        ::SendMessageW(toolbar, WM_LBUTTONUP, 0, point);
    } else {
        ::SendMessageW(window.Handle(), WM_COMMAND, std::to_underlying(id), 0);
    }
}

std::wstring CommandName(const MonitorWindow& window, ControlId id) {
    std::array<wchar_t, 256> text{};
    QIQI_TEST_CHECK(::GetMenuStringW(::GetMenu(window.Handle()), std::to_underlying(id), text.data(),
                                     gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND) > 0);
    return text.data();
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

LRESULT CALLBACK SimulateOpenDropdown(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                      DWORD_PTR) noexcept {
    if (message == CB_GETDROPPEDSTATE)
        return TRUE;
    return ::DefSubclassProc(control, message, wparam, lparam);
}

void SelectLanguage(const MonitorWindow& window, UiLanguage language) noexcept {
    const auto id = std::to_underlying(LanguageAuto) + std::to_underlying(language);
    ::SendMessageW(window.Handle(), WM_COMMAND, id, 0);
}

void CheckComboEditFont(HWND combo, UINT dpi) {
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    QIQI_TEST_CHECK(::GetComboBoxInfo(combo, &info) && info.hwndItem);
    const auto font = reinterpret_cast<HFONT>(::SendMessageW(info.hwndItem, WM_GETFONT, 0, 0));
    LOGFONTW logical{};
    QIQI_TEST_CHECK(::GetObjectW(font, sizeof(logical), &logical) == sizeof(logical));
    QIQI_TEST_CHECK(-logical.lfHeight == ScalePxByDpi(14, dpi));
    const ScopedWindowDC dc(info.hwndItem);
    const ScopedSelectObject selected(dc.Get(), font);
    QIQI_TEST_CHECK(dc && selected);
    TEXTMETRICW metrics{};
    RECT rect{};
    QIQI_TEST_CHECK(::GetTextMetricsW(dc.Get(), &metrics) && ::GetClientRect(info.hwndItem, &rect));
    QIQI_TEST_CHECK(metrics.tmHeight <= rect.bottom - rect.top);
}

std::wstring ReadClipboardText(HWND owner) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        const ScopedClipboard clip(owner);
        if (!clip.IsOpen()) {
            const HWND clip_owner = ::GetOpenClipboardWindow();
            const DWORD holder_tid = clip_owner ? ::GetWindowThreadProcessId(clip_owner, nullptr) : 0;
            if (clip.Error() == ERROR_ACCESS_DENIED && holder_tid != ::GetCurrentThreadId()) {
                ::Sleep(2);
                continue;
            }
        }
        QIQI_TEST_CHECK(clip.IsOpen());
        const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        QIQI_TEST_CHECK(data != nullptr);
        const ScopedGlobalLock<wchar_t> lock(data);
        QIQI_TEST_CHECK(lock.IsValid());
        return std::wstring(lock.Get());
    }
    QIQI_TEST_CHECK(false);
    return {};
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

Event MakeEvent(std::wstring text) {
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

} // namespace qiqi::monitor::tests
