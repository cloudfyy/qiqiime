// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::monitor::tests {

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
    QIQI_TEST_CHECK(::GetDIBits(dc.Get(), color.Get(), 0, gsl::narrow_cast<UINT>(bitmap.bmHeight), pixels.data(), &dib,
                                DIB_RGB_COLORS) == bitmap.bmHeight);
    return pixels;
}

void TestAboutDialogs() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<RECT> work_areas;
    QIQI_TEST_CHECK(::EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            QIQI_TEST_CHECK(::GetMonitorInfoW(monitor, &info));
            reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&work_areas)));
    QIQI_TEST_CHECK(!work_areas.empty());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    for (const RECT& work : work_areas) {
        const int width = std::min<LONG>(640, work.right - work.left);
        const int height = std::min<LONG>(480, work.bottom - work.top);
        QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr, work.left + (work.right - work.left - width) * 3 / 4,
                                       work.top + (work.bottom - work.top - height) / 3, width, height,
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW));
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            SelectLanguage(window, language);
            AboutExpectation expected{window.Handle(), language, work};
            auto* previous = std::exchange(current_about, &expected);
            const auto restore = gsl::finally([previous]() noexcept { current_about = previous; });
            const UINT_PTR timer =
                ::SetTimer(window.Handle(), 2, 15, [](HWND timer_owner, UINT, UINT_PTR id, DWORD) noexcept {
                    const HWND dialog = test::FindThreadWindowByClass(kInfoWndClass);
                    if (!dialog)
                        return;
                    QIQI_TEST_CHECK(current_about != nullptr);
                    QIQI_TEST_CHECK(::GetWindow(dialog, GW_OWNER) == current_about->owner);
                    const auto& strings = GetUiStrings(current_about->language);
                    QIQI_TEST_CHECK(ControlText(dialog) == strings.about_title);
                    QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, 201)) == strings.about_btn_github);
                    QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, IDOK)) == strings.dlg_btn_ok);
                    const UINT dpi = ::GetDpiForWindow(dialog);
                    const ScopedIcon expected_icon(static_cast<HICON>(
                        ::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     ScalePxByDpi(56, dpi), ScalePxByDpi(56, dpi), LR_DEFAULTCOLOR)));
                    const auto actual_icon = reinterpret_cast<HICON>(::SendMessageW(dialog, WM_GETICON, ICON_BIG, 0));
                    QIQI_TEST_CHECK(IconPixels(actual_icon) == IconPixels(expected_icon.Get()));
                    RECT owner_rect{}, dialog_rect{};
                    QIQI_TEST_CHECK(::GetWindowRect(current_about->owner, &owner_rect));
                    QIQI_TEST_CHECK(::GetWindowRect(dialog, &dialog_rect));
                    const int dialog_width = dialog_rect.right - dialog_rect.left;
                    const int dialog_height = dialog_rect.bottom - dialog_rect.top;
                    const POINT centered = ClampPointToWorkArea(
                        {owner_rect.left + (owner_rect.right - owner_rect.left - dialog_width) / 2,
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
    ::DestroyWindow(window.Handle());
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

} // namespace qiqi::monitor::tests
