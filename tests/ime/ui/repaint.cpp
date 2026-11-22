// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/controls.h"
#include "ime/comp/presentation.h"
#include "ime/tsf/mocks.h"
#include "ime/ui/candobj.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ime/ui/candidate.h"

namespace qiqi::test::candidate_cases {

using Microsoft::WRL::ComPtr;

namespace {

COLORREF PaintCandidatePixel(const CandidateUI& ui, POINT point) {
    const ScopedWindowDC screen(nullptr);
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), ui.CurrentWidth(), ui.CurrentHeight()));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
    ::SendMessageW(ui.WindowHandle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    return ::GetPixel(dc.Get(), point.x, point.y);
}

struct CandidateRefreshTrace {
    unsigned int frame_changes = 0;
    unsigned int visibility_changes = 0;
    unsigned int position_changes = 0;
};

LRESULT CALLBACK ObserveCandidateRefresh(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) noexcept {
    auto& trace = *reinterpret_cast<CandidateRefreshTrace*>(data);
    if (message == WM_WINDOWPOSCHANGING && lparam) {
        const auto& position = *reinterpret_cast<const WINDOWPOS*>(lparam);
        ++trace.position_changes;
        if (position.flags & SWP_FRAMECHANGED) ++trace.frame_changes;
        if (position.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) ++trace.visibility_changes;
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
}

} // namespace

void TestCandidateStableRepainting() {
    const TsfTestCaseTrace trace(__func__);
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, 144});
    for (const bool gdi : {false, true}) {
        const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        if (gdi) ui->DisableDirect2DForTest();
        ui->SetDisplayPreferences(UiTheme::Dark, UiTextScale::Percent100, false);
        ui->SetLayout(CandidateLayout::Vertical);
        ui->SetCandidates(L"ab", {L"One", L"Two", L"Three"}, 3, 0, 0);
        ui->MoveWindowTo(RECT{100, 100, 102, 120});
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        const HWND window = ui->WindowHandle();
        ::UpdateWindow(window);
        const auto* const focus = ::GetFocus();
#pragma warning(suppress : 26496) // The window subclass updates this through DWORD_PTR.
        CandidateRefreshTrace refresh;
        QIQI_TEST_CHECK(::SetWindowSubclass(window, ObserveCandidateRefresh, 1,
                                            reinterpret_cast<DWORD_PTR>(&refresh)));
        const auto remove_observer = gsl::finally([window]() noexcept {
            ::RemoveWindowSubclass(window, ObserveCandidateRefresh, 1);
        });
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        QIQI_TEST_CHECK(!::GetUpdateRect(window, nullptr, FALSE));
        QIQI_TEST_CHECK(refresh.position_changes == 0);
        for (int step = 0; step < 16; ++step) {
            ui->MoveWindowTo(RECT{100 + step, 100, 102 + step, 120});
            ui->SetCandidates(step % 2 ? L"ac" : L"ab", {L"One", L"Two", L"Three"},
                              3, 0, gsl::narrow_cast<size_t>(step % 3));
            QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
            ::UpdateWindow(window);
            QIQI_TEST_CHECK(ui->WindowHandle() == window && ::IsWindowVisible(window));
            const RECT selected = ui->CandidateRect(gsl::narrow_cast<size_t>(step % 3));
            QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{selected.left + 5, selected.bottom - 5}) ==
                            ui->Appearance().palette.selection_bg);
        }
        QIQI_TEST_CHECK(refresh.position_changes > 0);
        QIQI_TEST_CHECK(refresh.frame_changes == 0 && refresh.visibility_changes == 0);
        QIQI_TEST_CHECK(::GetFocus() == focus);
        const ScopedWindowDC screen;
        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
        const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), ui->CurrentWidth(), ui->CurrentHeight()));
        QIQI_TEST_CHECK(screen && dc && bitmap);
        const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
        constexpr COLORREF sentinel = RGB(255, 0, 255);
        QIQI_TEST_CHECK(::SetPixelV(dc.Get(), 2, 2, sentinel));
        QIQI_TEST_CHECK(::SendMessageW(window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc.Get()), 0) == 1);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 2, 2) == sentinel);
        ::SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
        QIQI_TEST_CHECK(::GetPixel(dc.Get(), 2, 2) == ui->Appearance().palette.window_bg);

        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 320, 180}, 144});
        ui->SetDisplayPreferences(UiTheme::Dark, UiTextScale::Percent200, true);
        ui->SetCandidates(L"abcd", std::vector<std::wstring>(9, L"Long candidate for a constrained work area"), 9, 0, 8);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) != 0);
        QIQI_TEST_CHECK(refresh.frame_changes > 0);
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, 144});
        ui->SetDisplayPreferences(UiTheme::Dark, UiTextScale::Percent100, false);
        ui->SetCandidates(L"ab", {L"One"}, 3, 0, 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) == 0);
        QIQI_TEST_CHECK(ui->WindowHandle() == window && ::IsWindowVisible(window));
        QIQI_TEST_CHECK(ui->Show(FALSE) == S_OK);
        ui->MoveWindowTo(RECT{200, 100, 202, 120});
        QIQI_TEST_CHECK(!::IsWindowVisible(window));
    }
}

void TestCandidateDisplayPreferences(
    const std::optional<ImeSkinSnapshot>& skin_snapshot) {
    const TsfTestCaseTrace trace(__func__);
    // Keep an independent snapshot: the test changes the override's backing storage.
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto previous_theme = detail::ThemeDarkOverrideStorage();
    const auto previous_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetThemeDarkOverrideForTest(previous_theme);
        SetTaskbarDarkOverrideForTest(previous_taskbar);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetHighContrastOverrideForTest(false);
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    const ScopedWindow synthetic_host(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(synthetic_host);
    for (const bool gdi : {false, true}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, dpi});
            const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
            if (gdi) ui->DisableDirect2DForTest();
            ui->SetSkinSnapshot(skin_snapshot);
            ui->SetLayout(CandidateLayout::Vertical);
            ui->SetUiLanguage(UiLanguage::EnUs);
            ui->SetCandidates(L"abc", {L"One", L"Two", L"Three"}, 2, 0, 1);
            ui->MoveWindowTo(RECT{10, 10, 12, 30});
            QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
            const auto initial = ResolveImeSkinAppearance({}, skin_snapshot);
            QIQI_TEST_CHECK(ui->Appearance().palette.is_dark == initial.display.palette.is_dark);
            QIQI_TEST_CHECK(PaintCandidatePixel(
                *ui.Get(), POINT{2, 2}) == initial.colors.Get(SkinColor::Background));
            const auto* const focus = ::GetFocus();
            const int normal_height = ui->CandidateRect(0).bottom - ui->CandidateRect(0).top;
            const int normal_width = ui->CurrentWidth();
            const int normal_popup_height = ui->CurrentHeight();
            int previous_height = 0;
            for (const auto scale : {UiTextScale::Percent100, UiTextScale::Percent125,
                                     UiTextScale::Percent150, UiTextScale::Percent200}) {
                ui->SetDisplayPreferences(UiTheme::Light, scale, false);
                const RECT item = ui->CandidateRect(0);
                QIQI_TEST_CHECK(item.bottom - item.top > previous_height);
                if (scale == UiTextScale::Percent100) {
                    QIQI_TEST_CHECK(ui->CurrentWidth() == normal_width &&
                                    ui->CurrentHeight() == normal_popup_height &&
                                    item.bottom - item.top == normal_height);
                }
                previous_height = item.bottom - item.top;
            }
            for (const auto theme : {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
                ui->SetDisplayPreferences(theme, UiTextScale::Percent150, true);
                const auto& appearance = ui->Appearance();
                const auto expected = ResolveImeSkinAppearance(
                    {theme, UiTextScale::Percent150, true}, skin_snapshot);
                QIQI_TEST_CHECK(appearance.high_contrast == (theme == UiTheme::HighContrast));
                QIQI_TEST_CHECK(appearance.palette.is_dark == expected.display.palette.is_dark);
                QIQI_TEST_CHECK(PaintCandidatePixel(
                    *ui.Get(), POINT{2, 2}) == expected.colors.Get(SkinColor::Background));
                for (const RECT& rect : {ui->CandidateRect(0), ui->CandidateRect(1),
                                        ui->PrevPageButtonRect(), ui->NextPageButtonRect()}) {
                    QIQI_TEST_CHECK(rect.right - rect.left >= ScalePxByDpi(44, dpi));
                    QIQI_TEST_CHECK(rect.bottom - rect.top >= ScalePxByDpi(44, dpi));
                }
                const RECT selected = ui->CandidateRect(1);
                QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{selected.left + ScalePxByDpi(3, dpi),
                    selected.bottom - ScalePxByDpi(3, dpi)}) == appearance.palette.selection_bg);
                QIQI_TEST_CHECK(::GetFocus() == focus);
            }
            ui->SetDisplayPreferences(UiTheme::Dark, UiTextScale::Percent100, false);
            QIQI_TEST_CHECK(ui->CandidateRect(0).bottom - ui->CandidateRect(0).top == normal_height);
            SetHighContrastOverrideForTest(true);
            ::SendMessageW(ui->WindowHandle(), WM_SETTINGCHANGE, 0, 0);
            QIQI_TEST_CHECK(ui->Appearance().system_high_contrast);
            QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) == ::GetSysColor(COLOR_WINDOW));
            SetHighContrastOverrideForTest(false);
            ::SendMessageW(ui->WindowHandle(), WM_THEMECHANGED, 0, 0);
            QIQI_TEST_CHECK(!ui->Appearance().high_contrast && ui->Appearance().palette.is_dark);
            ui->SetDisplayPreferences(UiTheme::System, UiTextScale::Percent100, false);
            for (const bool app_dark : {false, true}) {
                for (const bool windows_dark : {false, true}) {
                    SetThemeDarkOverrideForTest(app_dark);
                    SetTaskbarDarkOverrideForTest(windows_dark);
                    if (skin_snapshot) {
                        ui->MoveWindowTo(RECT{10, 10, 12, 30},
                            app_dark != windows_dark ? synthetic_host.Get() : nullptr);
                        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
                    }
                    for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                        ::SendMessageW(ui->WindowHandle(), message, 0, 0);
                        const auto expected = ResolveImeSkinAppearance(
                            {UiTheme::System, UiTextScale::Percent100, false},
                            skin_snapshot, SystemThemeSource::App);
                        QIQI_TEST_CHECK(ui->Appearance().palette.is_dark ==
                                        expected.display.palette.is_dark);
                        QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) ==
                                        expected.colors.Get(SkinColor::Background));
                    }
                }
            }
            SetThemeDarkOverrideForTest(false);
            SetTaskbarDarkOverrideForTest(true);
            QIQI_TEST_CHECK(::GetFocus() == focus);
            UINT selection = 0;
            UINT page_count = 0;
            QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_OK && selection == 1);
            QIQI_TEST_CHECK(ui->GetPageIndex(nullptr, 0, &page_count) == S_OK && page_count == 2);
            if (gdi) QIQI_TEST_CHECK(!ui->IsDirectWriteActive());
        }
    }
}

} // namespace qiqi::test::candidate_cases
