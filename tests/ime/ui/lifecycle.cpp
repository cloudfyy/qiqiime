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

} // namespace

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
        PresentCompositionCandidates(*uiless.Get(), flags, snapshot, nullptr, EditSessionEpoch{1, 1},
            1, GUID_NULL, [&]() { return manager.Get(); }, []() { return true; });
        QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 && !uiless->WindowHandle());
        const ComPtr<ITfThreadMgr> thread_manager = manager;
        EndCompositionCandidatePresentation(*uiless.Get(), thread_manager, 1, GUID_NULL);
        events.Expect({});
    }
}

namespace {

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

} // namespace

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

void TestCandidateAutomationTeardown() {
    const TsfTestCaseTrace trace(__func__);
    const auto objects_before = g_com_object_count.load();
    ComPtr<IRawElementProviderSimple> close_provider;
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetCandidates(L"ab", {L"One"}, 3, 0, 0);

    ComPtr<IUIAutomation> automation;
    QIQI_TEST_CHECK(::CoCreateInstance(CLSID_CUIAutomation, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)) == S_OK && automation);
    const auto request_provider = [&automation](HWND window) {
        ComPtr<IUIAutomationElement> element;
        QIQI_TEST_CHECK(automation->ElementFromHandle(window, element.GetAddressOf()) == S_OK &&
                        element);
        ScopedBstr automation_id;
        QIQI_TEST_CHECK(element->get_CurrentAutomationId(automation_id.Put()) == S_OK &&
                        automation_id && std::wstring_view(automation_id.Get()) == L"IME_Candidate_Window");
    };

    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    const HWND first_window = ui->WindowHandle();
    request_provider(first_window);
    QIQI_TEST_CHECK(::DestroyWindow(first_window));
    QIQI_TEST_CHECK(!::IsWindow(first_window) && !ui->WindowHandle());

    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    const HWND second_window = ui->WindowHandle();
    request_provider(second_window);

    // Simulate a UIA callback retaining the root during the last owner's close event.
    ui->SetAccessibilityDispatchObserverForTest(
        [&close_provider, candidate = ui.Get()](EVENTID event, bool is_root) {
            if (event != UIA_MenuClosedEventId) return;
            QIQI_TEST_CHECK(is_root && !close_provider);
            QIQI_TEST_CHECK(candidate->QueryInterface(IID_PPV_ARGS(&close_provider)) == S_OK &&
                            close_provider);
        });
    QIQI_TEST_CHECK(ui.Detach()->Release() == 1);
    QIQI_TEST_CHECK(!::IsWindow(second_window) && close_provider);
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before + 1);
    QIQI_TEST_CHECK(close_provider.Detach()->Release() == 0);
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test::candidate_cases
