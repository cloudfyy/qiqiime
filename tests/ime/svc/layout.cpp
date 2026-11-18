// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "common/dpi.h"
#include "ime/tsf/mocks.h"
#include "ime/svc/lifecycle.h"
#include "ime/svc/service.h"
#include "ime/comp/layout.h"
#include <array>
#include <memory>

namespace qiqi::test {
namespace {

void TestLayoutFallbackDpiRouting(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const TsfTestCaseTrace trace(__func__);
    const ScopedWindow host(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, -800, -400, 600, 400,
        nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(host);
    RECT host_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(host.Get(), &host_rect));
    const auto previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto previous_gui = detail::LayoutGuiThreadInfoOverrideStorage();
    const BOOL previous_show = mock_tm->show_own_ui_;
    const auto restore_state = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetLayoutGuiThreadInfoOverrideForTest(previous_gui);
        mock_tm->show_own_ui_ = previous_show;
    });
    mock_tm->show_own_ui_ = TRUE;
    struct DpiCase {
        UINT dpi;
        LONG focus_offset_x;
        LONG focus_caret_bottom;
    };
    constexpr std::array cases{
        DpiCase{96, 20, 44}, DpiCase{120, 25, 55},
        DpiCase{144, 30, 66}, DpiCase{192, 40, 88},
    };
    enum class PositionSource { FocusWindow, GuiCaret, TsfCaret };
    for (const auto& test : cases) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{{-4000, -3000, 4000, 3000}, test.dpi});
        for (const auto source : {PositionSource::FocusWindow, PositionSource::GuiCaret, PositionSource::TsfCaret}) {
            GUITHREADINFO gui{};
            gui.cbSize = sizeof(gui);
            gui.hwndFocus = host.Get();
            if (source != PositionSource::FocusWindow) {
                gui.hwndCaret = host.Get();
                gui.rcCaret = {11, 13, 14, 30};
            }
            // Shared input queues can retain another thread's caret even after DestroyCaret.
            SetLayoutGuiThreadInfoOverrideForTest(gui);
            const auto context = MakeComObject<MockTfContext>();
            context->view_->hwnd_ = host.Get();
            context->view_->fail_get_text_ext_ = source != PositionSource::TsfCaret;
            context->view_->text_ext_ = {-1200, -700, -1197, -683};
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            const auto* const ui = dynamic_cast<const CandidateUI*>(mock_tm->last_ui_element_.Get());
            QIQI_TEST_CHECK(ui && ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()));
            POINT expected{host_rect.left + test.focus_offset_x, host_rect.top + test.focus_caret_bottom + 4};
            if (source == PositionSource::GuiCaret) {
                expected = {11, 13};
                QIQI_TEST_CHECK(::ClientToScreen(host.Get(), &expected));
                expected.y += 17 + 4;
            } else if (source == PositionSource::TsfCaret) {
                expected = {-1200, -683 + 4};
            }
            RECT actual{};
            QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &actual));
            QIQI_TEST_CHECK(actual.left == expected.x && actual.top == expected.y);
            QIQI_TEST_CHECK(ui->CurrentHeight() == ScalePxByDpi(CandidateUI::kPopupHeight, test.dpi));
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
        }
    }
}

void TestFirstKeyLayoutRefresh(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const TsfTestCaseTrace trace(__func__);
    const auto previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto previous_gui = detail::LayoutGuiThreadInfoOverrideStorage();
    const BOOL previous_show = mock_tm->show_own_ui_;
    const auto restore = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetLayoutGuiThreadInfoOverrideForTest(previous_gui);
        mock_tm->show_own_ui_ = previous_show;
    });
    const ScopedWindow host(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 0, 0, 860, 940, nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(host);
    struct Case {
        MonitorMetrics monitor;
        RECT stale;
        RECT settled;
        POINT expected;
    };
    constexpr std::array cases{
        Case{{{1920, 0, 3840, 1032}, 96}, {2452, 177, 2452, 196},
             {2412, 1046, 2412, 1065}, {2412, 968}},
        Case{{{1920, 48, 3840, 1080}, 96}, {2412, 904, 2413, 923},
             {2412, 14, 2413, 33}, {2412, 48}},
        Case{{{72, 0, 1920, 1200}, 144}, {-1012, 90, -1011, 118},
             {206, 90, 207, 118}, {206, 122}},
        Case{{{-1920, 0, -72, 1200}, 144}, {14, 90, 15, 118},
             {-1600, 90, -1599, 118}, {-1600, 122}},
    };
    enum class Mode { Refresh, GuiCaret, Uiless, CompositionStart, KeyRelease, DirectKeyRelease };
    for (const auto& test : cases) {
        SetMonitorMetricsOverrideForTest(test.monitor);
        for (const auto mode : {Mode::Refresh, Mode::GuiCaret, Mode::Uiless, Mode::CompositionStart,
                 Mode::KeyRelease, Mode::DirectKeyRelease}) {
            GUITHREADINFO gui{};
            gui.cbSize = sizeof(gui);
            gui.hwndFocus = host.Get();
            if (mode == Mode::GuiCaret) {
                gui.hwndCaret = host.Get();
                gui.rcCaret = {11, 13, 14, 30};
            }
            SetLayoutGuiThreadInfoOverrideForTest(gui);
            mock_tm->show_own_ui_ = mode != Mode::Uiless;
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            context->SetDeferredEditSessions(false);
            context->view_->hwnd_ = host.Get();
            context->view_->text_ext_ = test.stale;
            if (mode == Mode::CompositionStart) {
                context->view_->get_text_ext_observer_ = [&](TfEditCookie, ITfRange* range, RECT*, BOOL*) {
                    const auto* const queried_range = dynamic_cast<const MockTfRange*>(range);
                    QIQI_TEST_CHECK(queried_range);
                    context->view_->text_ext_ = queried_range->collapsed_anchor_ == TF_ANCHOR_START
                        ? test.settled : test.stale;
                };
            }
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            const auto* const ui = dynamic_cast<const CandidateUI*>(mock_tm->last_ui_element_.Get());
            QIQI_TEST_CHECK(ui && context->range_->text_ == L"a");
            const bool key_release = mode == Mode::KeyRelease || mode == Mode::DirectKeyRelease;
            if (mode == Mode::Refresh || mode == Mode::CompositionStart || key_release) {
                if (mode == Mode::CompositionStart) {
                    RECT first{};
                    QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &first));
                    QIQI_TEST_CHECK(first.left == test.expected.x && first.top == test.expected.y);
                    QIQI_TEST_CHECK(!context->range_->collapsed_anchor_);
                }
                QIQI_TEST_CHECK(queue->PendingCount() == 1);
                QIQI_TEST_CHECK(queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READ));
                const auto writes = context->range_->set_text_calls_;
                const auto selections = context->set_selection_calls_;
                const auto queries = context->view_->get_text_ext_calls_;
                if (!key_release) context->view_->text_ext_ = test.settled;
                QIQI_TEST_CHECK(queue->RunNext(271) == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(context->view_->get_text_ext_calls_ == queries + 1);
                if (key_release) {
                    context->view_->text_ext_ = test.settled;
                    if (mode == Mode::KeyRelease) {
                        QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                    } else {
                        QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                    }
                    QIQI_TEST_CHECK(queue->PendingCount() == 1);
                    QIQI_TEST_CHECK(queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READ));
                    QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                    QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                    QIQI_TEST_CHECK(queue->PendingCount() == 1);
                    QIQI_TEST_CHECK(queue->RunNext(277) == S_OK && queue->PendingCount() == 0);
                }
                QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes);
                QIQI_TEST_CHECK(context->set_selection_calls_ == selections && !context->range_->collapsed_anchor_);
                RECT actual{};
                QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &actual));
                QIQI_TEST_CHECK(actual.left == test.expected.x && actual.top == test.expected.y);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 0 && context->range_->text_ == L"ab");
            } else {
                QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 0);
            }
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
        }
    }
}

void TestFirstKeyLayoutRefreshLifecycle(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const TsfTestCaseTrace trace(__func__);
    const auto previous_monitor = detail::MonitorMetricsOverrideStorage();
    const auto previous_gui = detail::LayoutGuiThreadInfoOverrideStorage();
    const BOOL previous_show = mock_tm->show_own_ui_;
    const auto restore = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(previous_monitor);
        SetLayoutGuiThreadInfoOverrideForTest(previous_gui);
        mock_tm->show_own_ui_ = previous_show;
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 48, 1920, 1080}, 96});
    const ScopedWindow host(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 0, 0, 860, 940, nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(host);
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    gui.hwndFocus = host.Get();
    SetLayoutGuiThreadInfoOverrideForTest(gui);
    mock_tm->show_own_ui_ = TRUE;
    enum class Transition { FocusLost, Reactivate, SwitchContext, ReplaceComposition, Abandon, Reject, NoLayout };
    const auto exercise = [&](Transition transition, bool key_release) {
        const TsfTestCaseTrace trigger_trace(key_release ? "layout.initial-key-release" : "layout.initial-post-edit");
        const auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        context->SetDeferredEditSessions(false);
        context->view_->hwnd_ = host.Get();
        context->view_->text_ext_ = {200, 904, 201, 923};
        if (transition == Transition::Reject) context->async_edit_session_result_ = E_ACCESSDENIED;
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        const auto original = context->last_composition_;
        QIQI_TEST_CHECK(queue->PendingCount() == (transition == Transition::Reject ? 0u : 1u));
        if (key_release) {
            if (transition != Transition::Reject) {
                QIQI_TEST_CHECK(queue->RunNext(281) == S_OK && queue->PendingCount() == 0);
            }
            QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == (transition == Transition::Reject ? 0u : 1u));
        }
        if (transition == Transition::FocusLost) {
            QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        } else if (transition == Transition::Reactivate) {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && service->Activate(mock_tm.get(), 1) == S_OK);
        } else if (transition == Transition::SwitchContext) {
            const auto replacement = MakeComObject<MockTfContext>();
            QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'B', 0, &eaten) == S_OK && eaten);
        } else if (transition == Transition::ReplaceComposition) {
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
            context->view_->text_ext_ = {400, 200, 401, 219};
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(context->last_composition_ != original && queue->PendingCount() == 2);
            QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 2);
        } else if (transition == Transition::NoLayout) {
            context->view_->fail_get_text_ext_ = true;
        }
        const auto* const ui = dynamic_cast<const CandidateUI*>(mock_tm->last_ui_element_.Get());
        QIQI_TEST_CHECK(ui);
        const HWND window = ui->WindowHandle();
        RECT before{};
        if (window) QIQI_TEST_CHECK(::GetWindowRect(window, &before));
        const auto queries = context->view_->get_text_ext_calls_;
        const auto writes = context->range_->set_text_calls_;
        if (transition == Transition::Abandon) {
            QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
        } else if (transition != Transition::Reject) {
            QIQI_TEST_CHECK(queue->RunNext(283) == S_OK);
        }
        QIQI_TEST_CHECK(context->view_->get_text_ext_calls_ ==
            queries + (transition == Transition::NoLayout ? 1u : 0u));
        QIQI_TEST_CHECK(context->range_->set_text_calls_ == writes);
        QIQI_TEST_CHECK(ui->WindowHandle() == window);
        if (window) {
            RECT after{};
            QIQI_TEST_CHECK(::GetWindowRect(window, &after) && ::EqualRect(&before, &after));
        }
        if (transition == Transition::ReplaceComposition) {
            QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext(293) == S_OK);
        }
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
    };
    for (const auto transition : {Transition::FocusLost, Transition::Reactivate, Transition::SwitchContext,
             Transition::ReplaceComposition, Transition::Abandon, Transition::Reject, Transition::NoLayout}) {
        exercise(transition, false);
        exercise(transition, true);
    }
}

} // namespace

void RunTsfLayoutQueryTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { View, Range, Extent, Owner };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    TestFirstKeyLayoutRefresh(service, mock_tm);
    TestFirstKeyLayoutRefreshLifecycle(service, mock_tm);
    TestLayoutFallbackDpiRouting(service, mock_tm);
    {
        const ScopedWindow first(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 100, 100, 600, 400, nullptr, nullptr, g_dll_instance, nullptr));
        const ScopedWindow second(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 200, 200, 600, 400, nullptr, nullptr, g_dll_instance, nullptr));
        const ScopedWindow edit(::CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 24, first.Get(), nullptr, g_dll_instance, nullptr));
        QIQI_TEST_CHECK(first && second && edit);
        const BOOL original_show = mock_tm->show_own_ui_;
        const auto restore_show = gsl::finally([&]() noexcept { mock_tm->show_own_ui_ = original_show; });
        for (const BOOL show : {TRUE, FALSE}) {
            mock_tm->show_own_ui_ = show;
            const auto context = MakeComObject<MockTfContext>();
            context->view_->hwnd_ = edit.Get();
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            const auto* const ui = dynamic_cast<const CandidateUI*>(mock_tm->last_ui_element_.Get());
            QIQI_TEST_CHECK(ui);
            if (show) {
                QIQI_TEST_CHECK(ui->WindowHandle() && ::GetWindow(ui->WindowHandle(), GW_OWNER) == first.Get());
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() || !::IsWindowVisible(ui->WindowHandle()));
            }
            const auto notification = MakeComObject<MockTfContextView>();
            notification->hwnd_ = second.Get();
            QIQI_TEST_CHECK(service->OnLayoutChange(context.Get(), TF_LC_CHANGE, notification.Get()) == S_OK);
            if (show) {
                QIQI_TEST_CHECK(ui->WindowHandle() && ::GetWindow(ui->WindowHandle(), GW_OWNER) == second.Get());
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() || !::IsWindowVisible(ui->WindowHandle()));
            }
            notification->get_wnd_result_ = E_FAIL;
            notification->hwnd_ = nullptr;
            QIQI_TEST_CHECK(service->OnLayoutChange(context.Get(), TF_LC_CHANGE, notification.Get()) == S_OK);
            if (show) {
                const HWND focus_window = ::GetFocus();
                QIQI_TEST_CHECK(ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()) &&
                                ::GetWindow(ui->WindowHandle(), GW_OWNER) ==
                                    (focus_window ? ::GetAncestor(focus_window, GA_ROOT) : nullptr));
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() || !::IsWindowVisible(ui->WindowHandle()));
            }
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
        }
    }
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto boundary : {Boundary::View, Boundary::Range, Boundary::Extent, Boundary::Owner}) {
            if (notified && boundary == Boundary::View) continue;
            for (const bool reactivate : {false, true}) {
                const auto context = MakeComObject<MockTfContext>();
                const auto replacement = MakeComObject<MockTfContext>();
                replacement->view_->text_ext_ = {300, 200, 340, 224};
                BOOL eaten = FALSE;
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
                auto* const ui = dynamic_cast<CandidateUI*>(mock_tm->last_ui_element_.Get());
                QIQI_TEST_CHECK(ui);
                const auto composition = context->last_composition_;
                const auto queried_view = notified ? MakeComObject<MockTfContextView>() : context->view_;
                queried_view->text_ext_ = {700, 500, 740, 524};
                const auto view_calls_before = context->get_active_view_calls_;
                const auto extent_calls_before = queried_view->get_text_ext_calls_;
                unsigned int callbacks = 0;
                RECT replacement_rect{};
                const auto callback = [&] {
                    if (callbacks != 0) return;
                    ++callbacks;
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (reactivate) {
                        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
                        QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && eaten);
                        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &replacement_rect));
                    }
                };
                const auto clear_callbacks = gsl::finally([&]() noexcept {
                    context->get_active_view_observer_ = {};
                    composition->get_range_callback_ = {};
                    queried_view->get_text_ext_callback_ = {};
                    queried_view->get_wnd_callback_ = {};
                });
                if (boundary == Boundary::View) {
                    context->get_active_view_observer_ = [&](ITfContextView**) { callback(); };
                } else if (boundary == Boundary::Range) {
                    composition->get_range_callback_ = callback;
                } else if (boundary == Boundary::Extent) {
                    queried_view->get_text_ext_callback_ = callback;
                } else {
                    queried_view->get_wnd_callback_ = callback;
                }
                QIQI_TEST_CHECK(service->OnLayoutChange(
                    context.Get(), TF_LC_CHANGE, notified ? queried_view.Get() : nullptr) == S_OK);
                context->get_active_view_observer_ = {};
                composition->get_range_callback_ = {};
                queried_view->get_text_ext_callback_ = {};
                queried_view->get_wnd_callback_ = {};
                QIQI_TEST_CHECK(callbacks == 1 && composition->ended_ && context->range_->text_ == L"a" &&
                                context->get_active_view_calls_ == view_calls_before + (notified ? 0u : 1u) &&
                                queried_view->get_text_ext_calls_ ==
                                    extent_calls_before +
                                        (boundary == Boundary::Extent || boundary == Boundary::Owner ? 1u : 0u));
                if (reactivate) {
                    RECT after{};
                    QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &after) &&
                                    ::EqualRect(&after, &replacement_rect) && ::IsWindowVisible(ui->WindowHandle()));
                    QIQI_TEST_CHECK(replacement->range_->text_ == L"c" && !replacement->last_composition_->ended_);
                    QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'D', 0, &eaten) == S_OK && eaten &&
                                    replacement->range_->text_ == L"cd");
                } else {
                    QIQI_TEST_CHECK(!ui->WindowHandle() && ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                    service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && !eaten &&
                                    replacement->range_->text_.empty());
                }
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 14 && g_com_object_count.load() == objects_before);
}

void RunTsfLayoutSessionTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Transition { Current, FocusLost, Reactivate, SwitchContext, ReplaceComposition, Abandon };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    const auto send_key = [&](ITfContext* context, UINT key) {
        QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context, key, 0, &eaten) == S_OK && eaten);
    };
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto transition : {
                 Transition::Current, Transition::FocusLost, Transition::Reactivate,
                 Transition::SwitchContext, Transition::ReplaceComposition, Transition::Abandon}) {
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            const auto replacement = MakeComObject<MockTfContext>();
            auto view = MakeComObject<MockTfContextView>();
            unsigned int notification_queries = 0;
            bool notification_released = false;
            view->get_text_ext_observer_ = [&](TfEditCookie ec, ITfRange* range, RECT*, BOOL*) {
                const auto* const caret_range = dynamic_cast<const MockTfRange*>(range);
                QIQI_TEST_CHECK(ec == 263 && caret_range && range != context->range_.Get());
                QIQI_TEST_CHECK(caret_range->collapsed_anchor_ == TF_ANCHOR_START);
                ++notification_queries;
            };
            view->release_observer_ = [&]() noexcept { notification_released = true; };
            send_key(context.Get(), 'A');
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto original_composition = context->last_composition_;
            QIQI_TEST_CHECK(service->OnLayoutChange(
                context.Get(), TF_LC_CHANGE, notified ? view.Get() : nullptr) == S_OK);
            QIQI_TEST_CHECK(queue->PendingCount() == 1 &&
                            queue->PendingRequests().front().flags == (TF_ES_ASYNCDONTCARE | TF_ES_READ));
            view.Reset();
            QIQI_TEST_CHECK(notification_released == !notified);
            context->SetDeferredEditSessions(false);
            if (transition == Transition::FocusLost) {
                QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
            } else if (transition == Transition::Reactivate) {
                QIQI_TEST_CHECK(service->Deactivate() == S_OK && service->Activate(mock_tm.get(), 1) == S_OK);
                send_key(context.Get(), 'B');
            } else if (transition == Transition::SwitchContext) {
                send_key(replacement.Get(), 'C');
            } else if (transition == Transition::ReplaceComposition) {
                send_key(context.Get(), VK_SPACE);
                send_key(context.Get(), 'B');
                QIQI_TEST_CHECK(context->last_composition_.Get() != original_composition.Get());
            }
            const bool current = transition == Transition::Current || transition == Transition::ReplaceComposition;
            const auto active_view_calls = context->get_active_view_calls_;
            const auto extent_calls = context->view_->get_text_ext_calls_;
            const auto old_range_calls = original_composition->get_range_calls_;
            const auto current_composition = context->last_composition_;
            const auto current_range_calls = current_composition->get_range_calls_;
            const auto writes = context->range_->set_text_calls_;
            const auto replacement_writes = replacement->range_->set_text_calls_;
            if (transition == Transition::Abandon) {
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
            } else {
                QIQI_TEST_CHECK(queue->RunNext(263) == S_OK);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0 && notification_released &&
                            notification_queries == (notified && current ? 1u : 0u) &&
                            context->get_active_view_calls_ == active_view_calls + (!notified && current ? 1u : 0u) &&
                            context->view_->get_text_ext_calls_ == extent_calls + (!notified && current ? 1u : 0u) &&
                            context->range_->set_text_calls_ == writes &&
                            replacement->range_->set_text_calls_ == replacement_writes);
            QIQI_TEST_CHECK(current_composition->get_range_calls_ == current_range_calls + (current ? 1u : 0u));
            if (transition == Transition::ReplaceComposition) {
                QIQI_TEST_CHECK(original_composition->get_range_calls_ == old_range_calls &&
                                original_composition->ended_ && context->range_->text_ == L"b");
            }
            if (transition == Transition::Reactivate) {
                QIQI_TEST_CHECK(context->range_->text_ == L"b" && !current_composition->ended_);
            }
            if (transition == Transition::SwitchContext) {
                send_key(replacement.Get(), 'D');
                QIQI_TEST_CHECK(replacement->range_->text_ == L"cd");
            } else {
                send_key(context.Get(), 'D');
                QIQI_TEST_CHECK(!context->range_->text_.empty());
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && service->Activate(mock_tm.get(), 1) == S_OK);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 12 && g_com_object_count.load() == objects_before);
    for (const bool execute : {false, true}) {
        const auto initial_objects = g_com_object_count.load();
        {
            const auto queue = std::make_shared<MockEditSessionQueue>();
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto view = MakeComObject<MockTfContextView>();
            bool context_released = false;
            bool view_released = false;
            context->release_observer_ = [&]() noexcept { context_released = true; };
            view->release_observer_ = [&]() noexcept { view_released = true; };
            QIQI_TEST_CHECK(peer->Activate(manager.Get(), 3) == S_OK);
            QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            QIQI_TEST_CHECK(peer->OnLayoutChange(context.Get(), TF_LC_CHANGE, view.Get()) == S_OK &&
                            queue->PendingCount() == 1);
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK);
            peer.Reset();
            manager.Reset();
            context.Reset();
            view.Reset();
            QIQI_TEST_CHECK(!context_released && !view_released && g_com_object_count.load() > initial_objects);
            if (execute) QIQI_TEST_CHECK(queue->RunNext(269) == S_OK);
            else QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
            QIQI_TEST_CHECK(context_released && view_released && queue->PendingCount() == 0 &&
                            g_com_object_count.load() == initial_objects);
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
    }
}

} // namespace qiqi::test
