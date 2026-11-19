// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
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
#include <array>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

struct WinEventHookTraits {
    using HandleType = HWINEVENTHOOK;
    static HandleType Invalid() noexcept { return nullptr; }
    static bool IsValid(const HandleType& hook) noexcept { return hook != nullptr; }
    static void Close(HandleType hook) noexcept {
        if (!::UnhookWinEvent(hook)) {
            std::fprintf(stderr, "UnhookWinEvent failed (error=%lu)\n", ::GetLastError());
            QIQI_TEST_CHECK_CLEANUP(false);
        }
    }
};

class CandidateWinEventTrace final {
public:
    CandidateWinEventTrace() noexcept {
        QIQI_TEST_CHECK_CLEANUP(current_ == nullptr);
        hook_.Reset(::SetWinEventHook(EVENT_OBJECT_IME_SHOW, EVENT_OBJECT_IME_CHANGE,
            nullptr, OnEvent, ::GetCurrentProcessId(), ::GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT));
        if (!hook_) std::fprintf(stderr, "SetWinEventHook failed (error=%lu)\n", ::GetLastError());
        QIQI_TEST_CHECK_CLEANUP(hook_);
        current_ = this;
    }
    ~CandidateWinEventTrace() noexcept {
        hook_.Reset();
        current_ = nullptr;
    }
    CandidateWinEventTrace(const CandidateWinEventTrace&) = delete;
    CandidateWinEventTrace& operator=(const CandidateWinEventTrace&) = delete;
    CandidateWinEventTrace(CandidateWinEventTrace&&) = delete;
    CandidateWinEventTrace& operator=(CandidateWinEventTrace&&) = delete;

    void Expect(std::initializer_list<std::pair<DWORD, HWND>> expected) {
        const auto deadline = ::GetTickCount64() + 60;
        for (;;) {
            MSG message{};
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    ::PostQuitMessage(gsl::narrow_cast<int>(message.wParam));
                    QIQI_TEST_CHECK(false);
                }
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
            const auto now = ::GetTickCount64();
            if (now >= deadline) break;
            const DWORD wait = ::MsgWaitForMultipleObjectsEx(
                0, nullptr, gsl::narrow_cast<DWORD>(deadline - now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            QIQI_TEST_CHECK(wait != WAIT_FAILED);
        }
        QIQI_TEST_CHECK(count_ == expected.size());
        size_t index = 0;
        for (const auto& [event, window] : expected) {
            const auto& actual = gsl::at(events_, index++);
            QIQI_TEST_CHECK(actual.event == event && actual.window == window);
            QIQI_TEST_CHECK(actual.object == OBJID_CLIENT && actual.child == CHILDID_SELF);
            QIQI_TEST_CHECK(actual.thread == ::GetCurrentThreadId());
        }
        count_ = 0;
    }

private:
    struct Event {
        DWORD event = 0;
        HWND window = nullptr;
        LONG object = 0;
        LONG child = 0;
        DWORD thread = 0;
    };
    static void CALLBACK OnEvent(
        HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD thread, DWORD) noexcept {
        QIQI_TEST_CHECK_CLEANUP(current_ && current_->count_ < current_->events_.size());
        gsl::at(current_->events_, current_->count_++) = Event{event, window, object, child, thread};
    }
    inline static thread_local CandidateWinEventTrace* current_ = nullptr;
    std::array<Event, 64> events_{};
    size_t count_ = 0;
    ScopedHandle<WinEventHookTraits> hook_;
};

void TestCandidateLightDismissEvents() {
    const TsfTestCaseTrace trace(__func__);
    // Keep an independent snapshot while the test changes the override.
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, 96});
    CandidateWinEventTrace events;
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->MoveWindowTo(RECT{100, 100, 102, 120});
    ui->SetCandidates(L"ab", {L"One", L"Two", L"Three"}, 3, 0, 0);
    events.Expect({});
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    const HWND first = ui->WindowHandle();
    QIQI_TEST_CHECK(first && ::IsWindowVisible(first));
    events.Expect({{EVENT_OBJECT_IME_SHOW, first}});
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    ui->MoveWindowTo(RECT{100, 100, 102, 120});
    ui->SetCandidates(L"ab", {L"One", L"Two", L"Three"}, 3, 0, 1);
    events.Expect({});
    ui->MoveWindowTo(RECT{200, 200, 202, 220});
    events.Expect({{EVENT_OBJECT_IME_CHANGE, first}});
    ui->SetLayout(CandidateLayout::Vertical);
    events.Expect({{EVENT_OBJECT_IME_CHANGE, first}});
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, 144});
    ::SendMessageW(first, WM_DPICHANGED, MAKEWPARAM(144, 144), 0);
    events.Expect({{EVENT_OBJECT_IME_CHANGE, first}});
    QIQI_TEST_CHECK(ui->Show(FALSE) == S_OK && ui->Show(FALSE) == S_OK);
    events.Expect({{EVENT_OBJECT_IME_HIDE, first}});
    ui->MoveWindowTo(RECT{300, 300, 302, 320});
    ui->SetLayout(CandidateLayout::Horizontal);
    events.Expect({});
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    events.Expect({{EVENT_OBJECT_IME_SHOW, first}});
    QIQI_TEST_CHECK(::DestroyWindow(first) && !ui->WindowHandle());
    events.Expect({{EVENT_OBJECT_IME_HIDE, first}});
    ui->SetPresentationEnabled(false);
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_FALSE && !ui->WindowHandle());
    events.Expect({});
    ui->SetPresentationEnabled(true);
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    const HWND second = ui->WindowHandle();
    events.Expect({{EVENT_OBJECT_IME_SHOW, second}});
    ui.Reset();
    QIQI_TEST_CHECK(!::IsWindow(second));
    events.Expect({{EVENT_OBJECT_IME_HIDE, second}});
    for (const DWORD flags : std::array<DWORD, 2>{0, TF_TMAE_UIELEMENTENABLEDONLY}) {
        const auto manager = MakeComObject<MockTfThreadMgr>();
        manager->show_own_ui_ = FALSE;
        const auto uiless = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        KeyProcessResult snapshot;
        snapshot.composition_text = L"ab";
        snapshot.all_display_candidates = {L"One", L"Two"};
        PresentCompositionCandidates(*uiless.Get(), flags, snapshot, EditSessionEpoch{1, 1},
            1, GUID_NULL, [&]() { return manager.Get(); }, []() { return true; });
        QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 && !uiless->WindowHandle());
        const ComPtr<ITfThreadMgr> thread_manager = manager;
        EndCompositionCandidatePresentation(*uiless.Get(), thread_manager, 1, GUID_NULL);
        events.Expect({});
    }
}

struct CandidateTeardownReentry {
    ComPtr<CandidateUI>* owner = nullptr;
    CandidateUI* ui = nullptr;
    bool release_owner = false;
    bool called = false;
};

LRESULT CALLBACK ObserveCandidateTeardown(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) noexcept {
    try {
        if (message == WM_NCDESTROY) {
            auto& state = *reinterpret_cast<CandidateTeardownReentry*>(data);
            QIQI_TEST_CHECK_CLEANUP(!state.called);
            state.called = true;
            const auto live_objects = g_com_object_count.load();
            if (state.release_owner) {
                state.owner->Reset();
            } else {
                state.ui->SetDisplayPreferences(DisplayPreferences{});
                QIQI_TEST_CHECK_CLEANUP(g_com_object_count.load() == live_objects);
                QIQI_TEST_CHECK_CLEANUP(state.ui->Show(TRUE) == S_FALSE);
            }
            QIQI_TEST_CHECK_CLEANUP(g_com_object_count.load() == live_objects);
            QIQI_TEST_CHECK_CLEANUP(::RemoveWindowSubclass(window, ObserveCandidateTeardown, id));
        }
        return ::DefSubclassProc(window, message, wparam, lparam);
    } catch (const std::bad_alloc&) {
        std::fputs("Candidate teardown reentry allocation failed (std::bad_alloc).\n", stderr);
        QIQI_TEST_CHECK_CLEANUP(false);
    }
}

void TestCandidateTeardownReentry() {
    const TsfTestCaseTrace trace(__func__);
    for (const bool release_owner : {true, false}) {
        const auto objects_before = g_com_object_count.load();
        auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        ui->SetCandidates(L"ab", {L"One"}, 3, 0, 0);
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
        const HWND window = ui->WindowHandle();
        CandidateTeardownReentry state{std::addressof(ui), ui.Get(), release_owner};
        const BOOL installed = ::SetWindowSubclass(
            window, ObserveCandidateTeardown, 1, reinterpret_cast<DWORD_PTR>(&state));
        if (!installed) {
            const DWORD error = ::GetLastError();
            std::fprintf(stderr,
                "Candidate teardown observer failed (error=%lu, has_window=%d, valid_window=%d, owner_thread=%lu, current_thread=%lu)\n",
                error, window != nullptr, ::IsWindow(window), ::GetWindowThreadProcessId(window, nullptr),
                ::GetCurrentThreadId());
        }
        QIQI_TEST_CHECK(installed);
        const auto remove_observer = gsl::finally([window]() noexcept {
            if (::IsWindow(window)) {
                QIQI_TEST_CHECK_CLEANUP(::RemoveWindowSubclass(window, ObserveCandidateTeardown, 1));
            }
        });
        if (release_owner) {
            const gsl::not_null<CandidateUI*> current(ui.Get());
            QIQI_TEST_CHECK(current->DestroyPopupWindow());
            QIQI_TEST_CHECK(!ui);
        } else {
            ui.Reset();
        }
        QIQI_TEST_CHECK(state.called && !::IsWindow(window));
        QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
    }
}

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

void TestCandidateDisplayPreferences() {
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
    for (const bool gdi : {false, true}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1600, 1200}, dpi});
            const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
            if (gdi) ui->DisableDirect2DForTest();
            ui->SetLayout(CandidateLayout::Vertical);
            ui->SetUiLanguage(UiLanguage::EnUs);
            ui->SetCandidates(L"abc", {L"One", L"Two", L"Three"}, 2, 0, 1);
            ui->MoveWindowTo(RECT{10, 10, 12, 30});
            QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
            QIQI_TEST_CHECK(!ui->Appearance().palette.is_dark);
            QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) == kLightThemePalette.window_bg);
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
                QIQI_TEST_CHECK(appearance.high_contrast == (theme == UiTheme::HighContrast));
                QIQI_TEST_CHECK(appearance.palette.is_dark ==
                                (theme == UiTheme::Dark || theme == UiTheme::HighContrast));
                QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) == appearance.palette.window_bg);
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
                    for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                        ::SendMessageW(ui->WindowHandle(), message, 0, 0);
                        QIQI_TEST_CHECK(ui->Appearance().palette.is_dark == app_dark);
                        QIQI_TEST_CHECK(PaintCandidatePixel(*ui.Get(), POINT{2, 2}) ==
                                        GetThemePalette(app_dark).window_bg);
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

void TestCandidateConstrainedViewport() {
    const TsfTestCaseTrace trace(__func__);
    // Keep an independent snapshot: the test changes the override's backing storage.
    const std::optional<MonitorMetrics> previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore = gsl::finally([&]() noexcept { SetMonitorMetricsOverrideForTest(previous_monitor); });
    const RECT work{-320, -180, 0, 0};
    SetMonitorMetricsOverrideForTest(MonitorMetrics{work, 96});
    for (const bool gdi : {false, true}) {
        const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
        if (gdi) ui->DisableDirect2DForTest();
        ui->SetDisplayPreferences(UiTheme::HighContrast, UiTextScale::Percent200, true);
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

void TestCandidateHostOwnership() {
    const TsfTestCaseTrace trace(__func__);
    ScopedWindow first(::CreateWindowExW(
        WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 100, 100, 600, 400,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    const ScopedWindow second(::CreateWindowExW(
        WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 200, 200, 600, 400,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    const ScopedWindow edit(::CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 24, first.Get(), nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(first && second && edit);
    const auto* const focus = ::GetFocus();
    const auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetCandidates(L"ab", {L"one", L"two"}, 5, 0, 1);
    ui->SetUiElementId(77);
    const RECT caret{100, 100, 102, 124};
    ui->MoveWindowTo(caret, edit.Get());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    QIQI_TEST_CHECK(::GetWindow(ui->WindowHandle(), GW_OWNER) == first.Get());
    ui->MoveWindowTo(caret, second.Get());
    QIQI_TEST_CHECK(ui->WindowHandle() && ::GetWindow(ui->WindowHandle(), GW_OWNER) == second.Get());
    QIQI_TEST_CHECK(::IsWindowVisible(ui->WindowHandle()) && ::GetFocus() == focus);
    UINT selection = 0;
    QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_OK && selection == 1 && ui->UiElementId() == 77);
    QIQI_TEST_CHECK(::SetWindowPos(second.Get(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    bool above_owner = false;
    for (HWND window = ::GetWindow(second.Get(), GW_HWNDPREV); window; window = ::GetWindow(window, GW_HWNDPREV)) {
        if (window == ui->WindowHandle()) {
            above_owner = true;
            break;
        }
    }
    QIQI_TEST_CHECK(above_owner);
    ui->MoveWindowTo(caret);
    QIQI_TEST_CHECK(ui->WindowHandle() && !::GetWindow(ui->WindowHandle(), GW_OWNER));
    QIQI_TEST_CHECK(ui->Show(FALSE) == S_OK);
    ui->MoveWindowTo(caret, first.Get());
    BOOL shown = TRUE;
    QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && !shown && !ui->WindowHandle());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    QIQI_TEST_CHECK(::GetWindow(ui->WindowHandle(), GW_OWNER) == first.Get());
    first.Reset();
    QIQI_TEST_CHECK(!ui->WindowHandle() && ui->IsShown(&shown) == S_OK && !shown);
    ui->MoveWindowTo(caret, second.Get());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    QIQI_TEST_CHECK(::GetWindow(ui->WindowHandle(), GW_OWNER) == second.Get());
    QIQI_TEST_CHECK(ui->DestroyPopupWindow() && ::IsWindow(second.Get()) && ::GetFocus() == focus);

    const auto owned = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    owned->SetCandidates(L"ab", {L"one", L"two"}, 5, 0, 0);
    owned->MoveWindowTo(caret, nullptr, true);
    BOOL owned_shown = TRUE;
    QIQI_TEST_CHECK(owned->Show(TRUE) == S_FALSE && !owned->WindowHandle());
    QIQI_TEST_CHECK(owned->IsShown(&owned_shown) == S_OK && !owned_shown);

    const ScopedWindow retry_owner(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 300, 300, 300, 200,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(retry_owner);
    owned->MoveWindowTo(caret, retry_owner.Get(), true);
    QIQI_TEST_CHECK(owned->WindowHandle() &&
                    ::GetWindow(owned->WindowHandle(), GW_OWNER) == retry_owner.Get() &&
                    ::IsWindowVisible(owned->WindowHandle()));

    const HWND stale_owner = retry_owner.Get();
    ::DestroyWindow(stale_owner);
    QIQI_TEST_CHECK(!::IsWindow(stale_owner) && !owned->WindowHandle());
    owned->MoveWindowTo(caret, stale_owner, true);
    QIQI_TEST_CHECK(!owned->WindowHandle() && owned->IsShown(&owned_shown) == S_OK && !owned_shown);
    owned->MoveWindowTo(caret, second.Get(), true);
    QIQI_TEST_CHECK(owned->WindowHandle() &&
                    ::GetWindow(owned->WindowHandle(), GW_OWNER) == second.Get() &&
                    ::IsWindowVisible(owned->WindowHandle()));
}

void TestCandidateLayouts() {
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

void TestCandidateUI() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));

    // QueryInterface tests
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void *dummy = nullptr;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_IClassFactory, &dummy) == E_NOINTERFACE && dummy == nullptr);

    ComPtr<ITfUIElement> ui_elem;
    ComPtr<ITfCandidateListUIElement> cand_elem;
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfUIElement, &ui_elem)) && ui_elem);
    QIQI_TEST_CHECK(SUCCEEDED(ui->QueryInterface(IID_ITfCandidateListUIElement, &cand_elem)) && cand_elem);
    ui_elem.Reset();

    // Metadata & invalid arg checks
    QIQI_TEST_CHECK(ui->GetDescription(nullptr) == E_INVALIDARG);
    ScopedBstr desc;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDescription(desc.Put())) && desc.IsValid());

    QIQI_TEST_CHECK(ui->GetGUID(nullptr) == E_INVALIDARG);
    GUID guid{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetGUID(&guid)) && IsEqualGUID(guid, kGuidCandidateUIElement));

    QIQI_TEST_CHECK(ui->GetUpdatedFlags(nullptr) == E_INVALIDARG);
    DWORD flags = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetUpdatedFlags(&flags)) && flags != 0);

    QIQI_TEST_CHECK(ui->GetDocumentMgr(nullptr) == E_INVALIDARG);
    ITfDocumentMgr* doc_mgr = nullptr;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetDocumentMgr(&doc_mgr)) && doc_mgr == nullptr);

    QIQI_TEST_CHECK(ui->IsShown(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCount(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetSelection(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetString(0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetPageIndex(nullptr, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(ui->GetCurrentPage(nullptr) == E_INVALIDARG);

    // Empty candidate state
    UINT count = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 0);
    UINT sel = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 0);
    UINT page_cnt = 99;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(nullptr, 0, &page_cnt)) && page_cnt == 0);
    ScopedBstr str;
    QIQI_TEST_CHECK(ui->GetString(0, str.Put()) == E_INVALIDARG);

    // Show empty window in Chinese and English UI modes and trigger WM_PAINT
    ui->SetUiLanguage(UiLanguage::ZhCn);
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(TRUE)));
    BOOL shown = FALSE;
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == TRUE);
    const RECT normal_rc{100, 100, 150, 125};
    ui->MoveWindowTo(normal_rc);
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui->SetUiLanguage(UiLanguage::EnUs);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
    }
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Multi-page candidate state (7 candidates, page_size 5, page 1, selected 1 -> index 6)
    std::vector<std::wstring> cands = {L"一", L"二", L"三", L"四", L"五", L"六", L"七"};
    ui->SetCandidates(L"abcd", cands, 5, 1, 1);

    // Also test page_size=0 fallback while hidden
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    ui->MoveWindowTo(normal_rc);
    ui->SetCandidates(L"abcd", cands, 0, 1, 1);
    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(TRUE)));

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCount(&count)) && count == 7);
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetSelection(&sel)) && sel == 6);
    UINT cur_page = 0;
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetCurrentPage(&cur_page)) && cur_page == 1);
    QIQI_TEST_CHECK(SUCCEEDED(ui->SetPageIndex(nullptr, 0)));

    std::array<UINT, 4> page_indices{};
    QIQI_TEST_CHECK(SUCCEEDED(ui->GetPageIndex(page_indices.data(), 4, &page_cnt)) && page_cnt == 2);
    QIQI_TEST_CHECK(gsl::at(page_indices, 0) == 0 && gsl::at(page_indices, 1) == 5);

    QIQI_TEST_CHECK(SUCCEEDED(ui->GetString(6, str.Put())) && str.IsValid());
    QIQI_TEST_CHECK(std::wstring(str.Get()) == L"七");
    QIQI_TEST_CHECK(ui->GetString(7, str.Put()) == E_INVALIDARG);

    // Test off-screen work area clamping, long candidate auto-sizing, Direct2D/DirectWrite CJK Ext-B & Emoji rendering, DPI change, & mouse click selection
    QIQI_TEST_CHECK(ui->IsDirectWriteActive());
    ui->SetUiLanguage(UiLanguage::EnUs);
    std::vector<std::wstring> long_cands = {
        L"恭恭敬敬一二三四", L"𠀀𠀁生僻字🚀", L"花花世界五六七八", L"劳苦功高九十", L"芙蓉出水"};
    ui->SetCandidates(L"aaaa", long_cands, 5, 0, 0);
    QIQI_TEST_CHECK(ui->CurrentWidth() > CandidateUI::kPopupWidth);

    if (ui->WindowHandle()) {
        // Click before callback is set (exercises early return when !select_callback_)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
    }

    size_t clicked_idx = 99;
    ui->SetSelectCallback([&clicked_idx](size_t idx) { clicked_idx = idx; });
    if (ui->WindowHandle()) {
        ::SendMessageW(ui->WindowHandle(), WM_MOUSEACTIVATE, 0, 0);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(144, 144), 0);
        // MoveWindowTo while dpi_ is 144 and window DPI is 96 (exercises wnd_dpi != dpi_ rescaling)
        ui->MoveWindowTo(normal_rc);
        ::SendMessageW(ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(96, 96), 0);
        // Click outside candidate rects (miss) then inside candidate 0 (hit)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
        QIQI_TEST_CHECK(clicked_idx == 99);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
        QIQI_TEST_CHECK(clicked_idx == 0);

        // Single-page candidate list (5 items, page_size 5) does not reserve page-turn chevron buttons
        QIQI_TEST_CHECK(ui->PrevPageButtonRect().right == 0 && ui->NextPageButtonRect().right == 0);

        // Multi-page D2D chevron indicators & mouse page-turn click testing
        ui->SetCandidates(L"abcd", cands, 5, 0, 0);
        const RECT prev_rc = ui->PrevPageButtonRect();
        const RECT next_rc = ui->NextPageButtonRect();
        QIQI_TEST_CHECK(prev_rc.right > prev_rc.left && next_rc.right > next_rc.left);
        const LPARAM prev_pt = MAKELPARAM((prev_rc.left + prev_rc.right) / 2, (prev_rc.top + prev_rc.bottom) / 2);
        const LPARAM next_pt = MAKELPARAM((next_rc.left + next_rc.right) / 2, (next_rc.top + next_rc.bottom) / 2);
        // Click before page_turn_callback_ is set (exercises !page_turn_callback_ guard)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, next_pt);
        int page_turn_dir = 0; // -1 = prev, +1 = next
        ui->SetPageTurnCallback([&page_turn_dir](bool next) { page_turn_dir = next ? 1 : -1; });
        // On page 0, clicking prev chevron is disabled (no-op), clicking next chevron turns to next page (+1)
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, prev_pt);
        QIQI_TEST_CHECK(page_turn_dir == 0);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, next_pt);
        QIQI_TEST_CHECK(page_turn_dir == 1);

        // On last page (page 1 of 2), clicking next chevron is disabled, clicking prev chevron turns to prev page (-1)
        page_turn_dir = 0;
        ui->SetCandidates(L"abcd", cands, 5, 1, 0);
        const RECT p1_prev_rc = ui->PrevPageButtonRect();
        const RECT p1_next_rc = ui->NextPageButtonRect();
        const LPARAM p1_prev_pt = MAKELPARAM((p1_prev_rc.left + p1_prev_rc.right) / 2, (p1_prev_rc.top + p1_prev_rc.bottom) / 2);
        const LPARAM p1_next_pt = MAKELPARAM((p1_next_rc.left + p1_next_rc.right) / 2, (p1_next_rc.top + p1_next_rc.bottom) / 2);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, p1_next_pt);
        QIQI_TEST_CHECK(page_turn_dir == 0);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, p1_prev_pt);
        QIQI_TEST_CHECK(page_turn_dir == -1);
    }

    const RECT far_rc{99999, 99999, 100050, 100025};
    ui->MoveWindowTo(far_rc);
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui->SetUiElementId(42);
    QIQI_TEST_CHECK(ui->UiElementId() == 42);

    // Verify GDI fallback PaintPopup path when Direct2D target is unavailable (multi-page, single-page, and empty)
    ui->DisableDirect2DForTest();
    QIQI_TEST_CHECK(!ui->IsDirectWriteActive());
    ui->SetCandidates(L"abcd", cands, 5, 0, 0);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
        ::UpdateWindow(ui->WindowHandle());
    }
    ui->SetCandidates(L"xy", {}, 5, 0, 0);
    if (ui->WindowHandle()) {
        ::InvalidateRect(ui->WindowHandle(), nullptr, TRUE);
        ::UpdateWindow(ui->WindowHandle());
    }

    QIQI_TEST_CHECK(SUCCEEDED(ui->Show(FALSE)));
    QIQI_TEST_CHECK(SUCCEEDED(ui->IsShown(&shown)) && shown == FALSE);
    const HWND old_window = ui->WindowHandle();
    QIQI_TEST_CHECK(old_window && ui->DestroyPopupWindow());
    QIQI_TEST_CHECK(!::IsWindow(old_window) && !ui->WindowHandle() && ui->DestroyPopupWindow());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
    const HWND recreated = ui->WindowHandle();
    bool wrong_thread_destroyed = true;
    {
        const std::jthread worker([&]() { wrong_thread_destroyed = ui->DestroyPopupWindow(); });
    }
    QIQI_TEST_CHECK(!wrong_thread_destroyed && ui->WindowHandle() == recreated && ::IsWindow(recreated));
    QIQI_TEST_CHECK(::DestroyWindow(recreated) && !ui->WindowHandle());
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
    QIQI_TEST_CHECK(ui->DestroyPopupWindow());

    cand_elem.Reset();
    QIQI_TEST_CHECK(ui.Detach()->Release() == 0);
}

} // namespace

void RunCandidateUITests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCandidateLightDismissEvents();
    TestCandidateTeardownReentry();
    TestCandidateStableRepainting();
    TestCandidateUI();
    TestCandidateLayouts();
    TestCandidateHostOwnership();
    TestCandidateDisplayPreferences();
    TestCandidateConstrainedViewport();
}

} // namespace qiqi::test
