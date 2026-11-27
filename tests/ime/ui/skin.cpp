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

bool CandidateFrameContainsColor(const CandidateUI& ui, const RECT& requested, COLORREF expected) {
    const ScopedWindowDC screen(nullptr);
    const int width = std::max(1, ui.CurrentWidth());
    const int height = std::max(1, ui.CurrentHeight());
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), width, height));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
    RECT bounds{};
    const RECT client{0, 0, width, height};
    if (!::IntersectRect(&bounds, &requested, &client)) return false;
    ::SendMessageW(ui.WindowHandle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            if (::GetPixel(dc.Get(), x, y) == expected) return true;
        }
    }
    return false;
}

} // namespace

void TestCandidateSkinRepainting() {
    const TsfTestCaseTrace trace(__func__);
    const auto previous_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(),
        MonitorMetrics{RECT{0, 0, 1600, 1200}, 144});
    const auto restore = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetHighContrastOverrideForTest(false);
    for (const bool gdi : {false, true}) {
        auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        if (gdi) ui->DisableDirect2DForTest();
        ui->SetDisplayPreferences(UiTheme::Light, UiTextScale::Percent150, true);
        ui->SetLayout(CandidateLayout::Vertical);
        ui->SetCandidates(L"ab", {L"One", L"Two", L"Three", L"Four"}, 2, 1, 1);
        ui->MoveWindowTo(RECT{100, 100, 102, 120});
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        const RECT before = ui->CandidateRect(1);
        const auto* const window = ui->WindowHandle();
        const auto* const focus = ::GetFocus();
        for (const auto& skin : GetBuiltinImeSkins().first(2)) {
            unsigned int changes = 0;
            ui->SetAccessibilityStructureObserverForTest([&]() noexcept { ++changes; });
            ui->SetSkinSnapshot(ImeSkinSnapshot{skin.id, skin.palettes});
            QIQI_TEST_CHECK(ui->WindowHandle() == window && changes == 0);
            ui->SetAccessibilityStructureObserverForTest({});
            const RECT after = ui->CandidateRect(1);
            QIQI_TEST_CHECK(::EqualRect(&before, &after));
            QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) ==
                skin.palettes.light.Get(SkinColor::Background));
            QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{before.left + ScalePxByDpi(3, 144),
                before.bottom - ScalePxByDpi(3, 144)}) == skin.palettes.light.Get(SkinColor::SelectedBackground));
            UINT page = 0;
            UINT selection = 0;
            QIQI_TEST_CHECK(ui->GetCurrentPage(&page) == S_OK && page == 1);
            QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_OK && selection == 3);
            QIQI_TEST_CHECK(::GetFocus() == focus);
        }
    }
}

void TestCandidateSkinStates() {
    const TsfTestCaseTrace trace(__func__);
    const auto previous_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(),
        MonitorMetrics{RECT{0, 0, 1600, 1200}, 144});
    const auto restore = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetHighContrastOverrideForTest(false);
    const auto& skin = GetBuiltinImeSkins().front();
    const auto& palette = skin.palettes.light;

    for (const bool gdi : {false, true}) {
        const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        if (gdi) ui->DisableDirect2DForTest();
        ui->SetUiLanguage(UiLanguage::EnUs);
        ui->SetDisplayPreferences(UiTheme::Light, UiTextScale::Percent150, true);
        ui->SetLayout(CandidateLayout::Vertical);
        ui->SetSkinSnapshot(ImeSkinSnapshot{skin.id, skin.palettes});

        ui->SetCandidates(L"ab", {L"One", L"Two", L"Three", L"Four"}, 2, 0, 1);
        ui->MoveWindowTo(RECT{100, 100, 102, 120});
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        auto items = ui->PaintItemsForTest();
        QIQI_TEST_CHECK(items.size() == 5);
        QIQI_TEST_CHECK(gsl::at(items, 0).color_role == SkinColor::CodeText);
        QIQI_TEST_CHECK(CandidatePaintTextColor(
            palette, gsl::at(items, 0)) == palette.Get(SkinColor::CodeText));
        QIQI_TEST_CHECK(gsl::at(items, 1).color_role == SkinColor::CandidateText &&
                        !gsl::at(items, 1).selected);
        QIQI_TEST_CHECK(gsl::at(items, 2).selected &&
            CandidatePaintTextColor(palette, gsl::at(items, 2)) ==
                palette.Get(SkinColor::SelectedText));
        QIQI_TEST_CHECK(!gsl::at(items, 3).enabled && gsl::at(items, 4).enabled);
        QIQI_TEST_CHECK(CandidatePaintTextColor(
            palette, gsl::at(items, 3)) == palette.Get(SkinColor::PageDisabledText));
        QIQI_TEST_CHECK(CandidatePaintTextColor(
            palette, gsl::at(items, 4)) == palette.Get(SkinColor::PageEnabledText));
        QIQI_TEST_CHECK(PaintCandidatePixel(
            *ui.Get(), POINT{2, 2}) == palette.Get(SkinColor::Background));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 0).rect, palette.Get(SkinColor::CodeText)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 1).rect, palette.Get(SkinColor::CandidateText)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 2).rect, palette.Get(SkinColor::SelectedBackground)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 2).rect, palette.Get(SkinColor::SelectedText)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 2).rect, palette.Get(SkinColor::SelectedBorder)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 3).rect, palette.Get(SkinColor::PageDisabledText)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 4).rect, palette.Get(SkinColor::PageEnabledText)));

        ui->SetCandidates(L"ab", {L"One", L"Two", L"Three", L"Four"}, 2, 1, 0);
        items = ui->PaintItemsForTest();
        QIQI_TEST_CHECK(items.size() == 5 &&
            gsl::at(items, 3).enabled && !gsl::at(items, 4).enabled);
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 3).rect, palette.Get(SkinColor::PageEnabledText)));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 4).rect, palette.Get(SkinColor::PageDisabledText)));

        ui->SetCandidates(L"ab", {}, 2, 0, 0);
        items = ui->PaintItemsForTest();
        QIQI_TEST_CHECK(items.size() == 2);
        QIQI_TEST_CHECK(gsl::at(items, 1).color_role == SkinColor::EmptyText &&
            !gsl::at(items, 1).selected);
        QIQI_TEST_CHECK(CandidatePaintTextColor(
            palette, gsl::at(items, 1)) == palette.Get(SkinColor::EmptyText));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), gsl::at(items, 1).rect, palette.Get(SkinColor::EmptyText)));

        ui->SetCandidates(L"ab", {L"FIRST\nSECOND"}, 2, 0, 0);
        const RECT row = ui->CandidateRect(0);
        const LONG middle = row.top + (row.bottom - row.top) / 2;
        const LONG padding = ScalePxByDpi(8, 144);
        const COLORREF text_color = palette.Get(SkinColor::SelectedText);
        QIQI_TEST_CHECK(!CandidateFrameContainsColor(
            *ui.Get(), RECT{row.left, row.top, row.right, row.top + padding}, text_color));
        QIQI_TEST_CHECK(!CandidateFrameContainsColor(
            *ui.Get(), RECT{row.left, row.top, row.left + padding, row.bottom}, text_color));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), RECT{row.left + padding, row.top + padding, row.right - padding, middle}, text_color));
        QIQI_TEST_CHECK(CandidateFrameContainsColor(
            *ui.Get(), RECT{row.left + padding, middle, row.right - padding, row.bottom - padding}, text_color));
        QIQI_TEST_CHECK(ui->IsDirectWriteActive() == !gdi);
    }
}

void TestCandidateFontPreferences() {
    const TsfTestCaseTrace trace(__func__);
    auto previous_monitor = std::exchange(
        detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, 96});
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetCandidates(L"ab", {L"One", L"Two"}, 2, 0, 0);
    ui->MoveWindowTo(RECT{100, 100, 102, 120});
    const RECT automatic_row = ui->CandidateRect(0);
    ui->SetFontPreferences({
        L"Segoe UI", 24, CandidateFontWeight::Bold, true,
    });
    ui->SetCandidates(L"ab", {L"One", L"Two"}, 2, 0, 0);
    const LOGFONTW description = ui->FontDescriptionForTest();
    QIQI_TEST_CHECK(LogFontFaceNameView(description) == L"Segoe UI");
    QIQI_TEST_CHECK(description.lfWeight == FW_BOLD && description.lfItalic);
    QIQI_TEST_CHECK(std::abs(gsl::narrow_cast<int>(description.lfHeight)) ==
        CandidateFontHeightPx({L"Segoe UI", 24, CandidateFontWeight::Bold, true},
            kCandidateFontDefaultHeightPx, 96, UiTextScale::Percent100));
    const RECT custom_row = ui->CandidateRect(0);
    QIQI_TEST_CHECK(custom_row.bottom - custom_row.top >
        automatic_row.bottom - automatic_row.top);
    ui->SetFontPreferences({
        L"QiQi Missing Candidate Font", 30, CandidateFontWeight::Black, true,
    });
    const LOGFONTW missing_fallback = ui->FontDescriptionForTest();
    QIQI_TEST_CHECK(LogFontFaceNameView(missing_fallback) !=
        L"QiQi Missing Candidate Font");
    QIQI_TEST_CHECK(missing_fallback.lfWeight == FW_NORMAL &&
        !missing_fallback.lfItalic);
    ui->SetFontPreferences({});
    const LOGFONTW restored = ui->FontDescriptionForTest();
    QIQI_TEST_CHECK(std::abs(gsl::narrow_cast<int>(restored.lfHeight)) ==
        CandidateFontHeightPx({}, kCandidateFontDefaultHeightPx,
            96, UiTextScale::Percent100));
}

void TestCandidateConstrainedViewport(
    const std::optional<ImeSkinSnapshot>& skin_snapshot) {
    const TsfTestCaseTrace trace(__func__);
    // Keep an independent snapshot: the test changes the override's backing storage.
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore = gsl::finally([&]() noexcept { SetMonitorMetricsOverrideForTest(previous_monitor); });
    const RECT work{-320, -180, 0, 0};
    SetMonitorMetricsOverrideForTest(MonitorMetrics{work, 96});
    for (const bool gdi : {false, true}) {
        const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        if (gdi) ui->DisableDirect2DForTest();
        ui->SetSkinSnapshot(skin_snapshot);
        ui->SetDisplayPreferences(skin_snapshot ? UiTheme::Light : UiTheme::HighContrast,
            UiTextScale::Percent200, true);
        const std::vector<std::wstring> candidates{
            std::wstring(80, L'长') + L"𠀀🚀", L"第二项", L"第三项", L"第四项", L"第五项",
            L"第六项", L"第七项", L"第八项", L"末项"};
        ui->SetCandidates(L"abcd", candidates, 9, 0, 8);
        ui->MoveWindowTo(RECT{-10, -20, -8, -5});
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        RECT actual{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &actual));
        QIQI_TEST_CHECK(actual.left >= work.left && actual.top >= work.top &&
                        actual.right <= work.right && actual.bottom <= work.bottom);
        QIQI_TEST_CHECK(ui->ContentHeight() > ui->CurrentHeight() && ui->ScrollOffset() > 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(ui->WindowHandle(), GWL_STYLE) & WS_VSCROLL) != 0);
        const RECT selected = ui->CandidateRect(8);
        QIQI_TEST_CHECK(selected.top >= 0 && selected.bottom <= ui->CurrentHeight());
        QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{selected.left + 3, selected.bottom - 3}) ==
                        ui->Appearance().palette.selection_bg);
        if (!gdi) {
            const int content_height = ui->ContentHeight();
            ui->DisableDirect2DForTest();
            ui->SetCandidates(L"abcd", candidates, 9, 0, 8);
            const RECT fallback = ui->CandidateRect(8);
            QIQI_TEST_CHECK(::EqualRect(&selected, &fallback) && ui->ContentHeight() == content_height);
        }
        size_t clicked = 99;
        ui->SetSelectCallback([&](size_t index) { clicked = index; });
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM((selected.left + selected.right) / 2, (selected.top + selected.bottom) / 2));
        QIQI_TEST_CHECK(clicked == 8);
        ::SendMessageW(ui->WindowHandle(), WM_VSCROLL, SB_TOP, 0);
        QIQI_TEST_CHECK(ui->ScrollOffset() == 0);
        const RECT first = ui->CandidateRect(0);
        QIQI_TEST_CHECK(first.bottom - first.top > ui->CurrentHeight());
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM(gsl::narrow_cast<LONGLONG>(first.left) + 20, gsl::narrow_cast<LONGLONG>(first.top) + 20));
        QIQI_TEST_CHECK(clicked == 0);
        ::SendMessageW(ui->WindowHandle(), WM_VSCROLL, SB_BOTTOM, 0);
        const int bottom = ui->ScrollOffset();
        ::SendMessageW(ui->WindowHandle(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        QIQI_TEST_CHECK(ui->ScrollOffset() < bottom);
        ui->SetCandidates(L"abcd", candidates, 9, 0, 8);
        QIQI_TEST_CHECK(ui->CandidateRect(8).bottom <= ui->CurrentHeight());
        UINT count = 0;
        UINT pages = 0;
        QIQI_TEST_CHECK(ui->GetCount(&count) == S_OK && count == 9);
        QIQI_TEST_CHECK(ui->GetPageIndex(nullptr, 0, &pages) == S_OK && pages == 1);
        for (UINT i = 0; i < count; ++i) {
            ScopedBstr text;
            QIQI_TEST_CHECK(ui->GetString(i, text.Put()) == S_OK && std::wstring(text.Get()) == gsl::at(candidates, i));
        }
        ui->SetCandidates(L"a", {L"一", L"二", L"三", L"四", L"五"}, 9, 0, 0);
        QIQI_TEST_CHECK(ui->CandidateRect(4).top > ui->CandidateRect(0).top);
        ::UpdateWindow(ui->WindowHandle());
    }
}


void TestCandidateLayouts(
    const std::optional<ImeSkinSnapshot>& skin_snapshot) {
    const TsfTestCaseTrace trace(__func__);
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
    });
    const std::vector<std::wstring> candidates = {L"第一", L"第二", L"第三", L"第四", L"第五", L"第六", L"第七"};
    for (const bool gdi : {false, true}) {
        for (const UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
            const RECT work{-2400, -1600, 0, 0};
            SetMonitorMetricsOverrideForTest(MonitorMetrics{work, dpi});
            auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
            if (gdi) ui->DisableDirect2DForTest();
            ui->SetSkinSnapshot(skin_snapshot);
            const RECT caret{-80, -80, -78, -60};
            ui->MoveWindowTo(caret);
            ui->SetCandidates(L"abcd", candidates, 5, 0, 2);
            QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
            const auto check_bounds = [&]() {
                RECT actual{};
                QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &actual));
                QIQI_TEST_CHECK(actual.left >= work.left && actual.top >= work.top &&
                                actual.right <= work.right && actual.bottom <= work.bottom);
                QIQI_TEST_CHECK(actual.bottom <= caret.top);
            };
            const int horizontal_height = ui->CurrentHeight();
            const int horizontal_width = ui->CurrentWidth();
            ui->SetLayout(CandidateLayout::Vertical);
            ui->SetLayout(CandidateLayout::Vertical);
            QIQI_TEST_CHECK(ui->CurrentHeight() > horizontal_height);
            const int compact_width = ui->CurrentWidth();
            QIQI_TEST_CHECK(compact_width < ScalePxByDpi(CandidateUI::kPopupWidth, dpi));
            RECT compact_rect{};
            QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &compact_rect));
            QIQI_TEST_CHECK(compact_rect.right - compact_rect.left == compact_width);
            std::vector<std::wstring> wide_candidates = candidates;
            gsl::at(wide_candidates, 0) = L"候选内容更新后窗口变宽仍需留在当前显示器";
            ui->SetCandidates(L"abcd", wide_candidates, 5, 0, 2);
            QIQI_TEST_CHECK(ui->CurrentWidth() > compact_width);
            check_bounds();
            ui->SetCandidates(L"abcd", candidates, 5, 0, 2);
            QIQI_TEST_CHECK(ui->CurrentWidth() == compact_width);
            check_bounds();
            UINT selection = 0;
            QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_OK && selection == 2);
            size_t clicked = 99;
            ui->SetSelectCallback([&](size_t index) { clicked = index; });
            for (size_t i = 0; i < 5; ++i) {
                const RECT rect = ui->CandidateRect(i);
                ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
                    MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2));
                QIQI_TEST_CHECK(clicked == i);
            }
            int direction = 0;
            ui->SetPageTurnCallback([&](bool next) { direction = next ? 1 : -1; });
            const auto click = [&](const RECT& rect) {
                ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
                    MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2));
            };
            click(ui->PrevPageButtonRect());
            QIQI_TEST_CHECK(direction == 0);
            click(ui->NextPageButtonRect());
            QIQI_TEST_CHECK(direction == 1);
            ::UpdateWindow(ui->WindowHandle());
            ui->SetCandidates(L"abcd", candidates, 5, 1, 1);
            QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_OK && selection == 6);
            direction = 0;
            click(ui->NextPageButtonRect());
            QIQI_TEST_CHECK(direction == 0);
            click(ui->PrevPageButtonRect());
            QIQI_TEST_CHECK(direction == -1);
            ::UpdateWindow(ui->WindowHandle());
            check_bounds();
            ui->SetCandidates(L"abcd", {L"第一"}, 5, 0, 0);
            QIQI_TEST_CHECK(ui->CurrentWidth() < compact_width);
            QIQI_TEST_CHECK(ui->PrevPageButtonRect().right == 0 &&
                            ui->NextPageButtonRect().right == 0);
            ui->SetCandidates(L"xy", {}, 5, 0, 0);
            ::UpdateWindow(ui->WindowHandle());
            QIQI_TEST_CHECK(ui->PrevPageButtonRect().right == 0 && ui->CurrentHeight() == horizontal_height);
            QIQI_TEST_CHECK(ui->CurrentWidth() < ScalePxByDpi(CandidateUI::kPopupWidth, dpi));
            ui->SetLayout(CandidateLayout::Horizontal);
            ui->SetCandidates(L"abcd", candidates, 5, 0, 2);
            QIQI_TEST_CHECK(ui->CurrentWidth() == horizontal_width);
            ui->SetCandidates(L"abcd", candidates, 5, 0, 0);
            std::vector<std::wstring> longer = candidates;
            gsl::at(longer, 0) = L"候选内容更新后窗口变宽仍需留在当前显示器";
            ui->SetCandidates(L"abcd", longer, 5, 0, 0);
            check_bounds();
            const RECT suggested{-20, -20, 300, 300};
            ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                reinterpret_cast<LPARAM>(&suggested));
            for (const UINT event : {WM_THEMECHANGED, WM_SETTINGCHANGE}) {
                ::SendMessageW(ui->WindowHandle(), event, 0, 0);
                check_bounds();
            }
            ui->SetLayout(CandidateLayout::Vertical);
            ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                reinterpret_cast<LPARAM>(&suggested));
            check_bounds();
            ::UpdateWindow(ui->WindowHandle());
        }
    }
}

} // namespace qiqi::test::candidate_cases
