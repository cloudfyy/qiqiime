// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/svc/lifecycle.h"
#include <array>
#include <memory>
#include <tuple>
#include <utility>

namespace qiqi::test {

void RunTsfServiceCleanupTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    // Deactivation preserves preedit text and ends only the captured host composition.
    {
        enum class Failure { None, Synchronous, ReadOnly, Request, End, Range, Property, PropertyQuery };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const auto failure : {Failure::None, Failure::Synchronous, Failure::ReadOnly, Failure::Request,
                                   Failure::End, Failure::Range, Failure::Property, Failure::PropertyQuery}) {
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL cleanup_eaten = FALSE;
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto composition = context->last_composition_;
            QIQI_TEST_CHECK(composition && !composition->ended_);
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 1);
                if (failure != Failure::Synchronous) QIQI_TEST_CHECK(context->in_immediate_write_session_);
            };
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            context->allow_sync_edit_sessions_ = failure != Failure::Synchronous;
            context->read_only_ = failure == Failure::ReadOnly;
            if (failure == Failure::Request) context->edit_session_request_result_ = E_ACCESSDENIED;
            if (failure == Failure::End) composition->end_result_ = E_FAIL;
            if (failure == Failure::Range) composition->fail_get_range_ = true;
            if (failure == Failure::Property) context->prop_->clear_result_ = E_ACCESSDENIED;
            if (failure == Failure::PropertyQuery) context->get_property_result_ = E_ACCESSDENIED;
            const auto requests_before = context->request_edit_session_calls_;
            const auto writes_before = context->range_->set_text_calls_;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            const auto cleanup_requests = failure == Failure::Synchronous ? 2u : 1u;
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(context->last_edit_session_flags_ ==
                            gsl::narrow_cast<DWORD>(
                                (failure == Failure::Synchronous ? TF_ES_ASYNC : TF_ES_SYNC) | TF_ES_READWRITE));
            QIQI_TEST_CHECK(queue->PendingCount() == (failure == Failure::Synchronous ? 2u : 1u) && !context->layout_sink_);
            const bool can_edit = failure != Failure::Synchronous && failure != Failure::ReadOnly &&
                                  failure != Failure::Request;
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(composition->ended_ == (can_edit && failure != Failure::End));
            const bool can_clear = can_edit && failure != Failure::Range && failure != Failure::PropertyQuery;
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == (can_clear ? 1u : 0u));
            if (can_clear) QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 1);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(mock_tm->last_ui_element_->IsShown(&shown) == S_OK && shown == FALSE);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            if (failure == Failure::Synchronous) {
                const auto ui_before_cleanup = std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
                QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
                QIQI_TEST_CHECK((std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before_cleanup);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
        }

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto host_context = MakeComObject<MockTfContext>();
        BOOL cleanup_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(host_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto host_composition = host_context->last_composition_;
        const auto host_requests = host_context->request_edit_session_calls_;
        QIQI_TEST_CHECK(host_composition->NotifyHostTermination(17) == S_OK);
        QIQI_TEST_CHECK(host_composition->ended_ && host_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(host_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(host_context->prop_->clear_calls_ == 1 && host_context->prop_->last_clear_cookie_ == 17);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(host_context->request_edit_session_calls_ == host_requests);
        QIQI_TEST_CHECK(host_composition->end_composition_calls_ == 0);

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto reentrant_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(reentrant_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto reentrant_composition = reentrant_context->last_composition_;
        reentrant_composition->end_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(reentrant_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == E_UNEXPECTED);
            QIQI_TEST_CHECK(reentrant_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(reentrant_composition->ended_ && reentrant_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(reentrant_context->prop_->clear_calls_ == 1 && reentrant_context->range_->text_ == L"a");
        reentrant_composition->end_callback_ = {};

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto interrupted_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(interrupted_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto interrupted_composition = interrupted_context->last_composition_;
        interrupted_context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(interrupted_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(interrupted_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(interrupted_composition->ended_ && interrupted_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(interrupted_context->prop_->clear_calls_ == 1 && interrupted_context->range_->text_ == L"a");
        interrupted_context->prop_->clear_callback_ = {};

        // Late termination of A must not reset B's composition or candidate UI.
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto previous_context = MakeComObject<MockTfContext>();
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(previous_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto previous_composition = previous_context->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto current_composition = current_context->last_composition_;
        const auto current_text = current_context->range_->text_;
        const auto ui_ends = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(previous_composition->NotifyHostTermination(19) == S_OK);
        QIQI_TEST_CHECK(current_context->range_->text_ == current_text && mock_tm->end_ui_calls_ == ui_ends);
        QIQI_TEST_CHECK(!current_composition->ended_);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(current_composition->ended_ && current_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }

    // Reentrant dispatch behind unfinished cleanup must retain the requeued task's snapshot.
    {
        const auto objects_before = g_com_object_count.load();
        {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto old_composition = context->last_composition_;
            const auto writes_before = context->range_->set_text_calls_;
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(peer->OnSetFocus(FALSE) == S_OK && queue->PendingCount() == 1);
            bool reentered = false;
            old_composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 53 && !std::exchange(reentered, true));
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 1 &&
                                queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                const auto ui_before = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
                QIQI_TEST_CHECK(queue->RunNext(59) == S_OK && queue->PendingCount() == 1);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before &&
                                context->start_composition_calls_ == 1);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'C', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 2);
            };
            QIQI_TEST_CHECK(queue->RunNext(53) == S_OK && reentered && queue->PendingCount() == 2);
            old_composition->end_callback_ = {};
            QIQI_TEST_CHECK(old_composition->ended_ && old_composition->end_composition_calls_ == 1 &&
                            old_composition->last_end_cookie_ == 53);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 53);
            QIQI_TEST_CHECK(queue->RunNext(61) == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(context->range_->text_ == L"b" && context->start_composition_calls_ == 2);
            QIQI_TEST_CHECK(queue->RunNext(67) == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"bc" && context->start_composition_calls_ == 2);
            context->allow_sync_edit_sessions_ = true;
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK && context->last_composition_->ended_);
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
    }

    // Stale edits cancel, but detached cleanup must still precede the next activation's writes.
    {
        const auto initial_objects = g_com_object_count.load();
        for (const UINT stale_key : {0u, UINT{VK_SPACE}, UINT{VK_ESCAPE}}) {
            for (const bool execute_cleanup : {false, true}) {
                auto queue = std::make_shared<MockEditSessionQueue>();
                auto context = MakeComObject<MockTfContext>(queue);
                auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
                auto manager = MakeComObject<MockTfThreadMgr>();
                BOOL eaten_deferred = FALSE;
                QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
                const auto old_composition = context->last_composition_;
                if (stale_key == 0) {
                    QIQI_TEST_CHECK(peer->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
                } else {
                    QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), stale_key, 0, &eaten_deferred) == S_OK &&
                                    eaten_deferred);
                }
                QIQI_TEST_CHECK(queue->PendingCount() == 1);
                context->allow_sync_edit_sessions_ = false;
                QIQI_TEST_CHECK(peer->Deactivate() == S_OK && queue->PendingCount() == 2);
                const auto writes_before = context->range_->set_text_calls_;
                const auto views_before = context->get_active_view_calls_;
                if (!execute_cleanup) {
                    peer.Reset();
                    manager.Reset();
                    QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && queue->PendingCount() == 0);
                    QIQI_TEST_CHECK(!old_composition->ended_ && old_composition->end_composition_calls_ == 0);
                    QIQI_TEST_CHECK(context->prop_->clear_calls_ == 0 && context->range_->text_ == L"a");
                    QIQI_TEST_CHECK(context->range_->set_text_calls_ == writes_before &&
                                    context->get_active_view_calls_ == views_before);
                    continue;
                }
                QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'B', 0, &eaten_deferred) == S_OK && eaten_deferred);
                QIQI_TEST_CHECK(queue->PendingCount() == 3);
                QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 2).flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                const auto ui_before = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
                QIQI_TEST_CHECK(queue->RunNext(31) == S_OK && queue->PendingCount() == 2);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before);
                QIQI_TEST_CHECK(context->get_active_view_calls_ == views_before &&
                                context->start_composition_calls_ == 1);
                QIQI_TEST_CHECK(!old_composition->ended_ && context->prop_->clear_calls_ == 0);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->RunNext(37) == S_OK && queue->PendingCount() == 1);
                QIQI_TEST_CHECK(old_composition->ended_ && old_composition->end_composition_calls_ == 1);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 37);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->RunNext(41) == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(context->range_->text_ == L"b" && context->start_composition_calls_ == 2);
                QIQI_TEST_CHECK(context->last_composition_.Get() != old_composition.Get() &&
                                !context->last_composition_->ended_ && context->prop_->clear_calls_ == 1);
                context->allow_sync_edit_sessions_ = true;
                QIQI_TEST_CHECK(peer->Deactivate() == S_OK && context->last_composition_->ended_);
            }
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
    }

    // Deferred cleanup survives deactivation, but cannot clear a new composition's attributes.
    {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const bool host_terminates : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto old_composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(!old_composition->ended_);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            if (host_terminates) {
                QIQI_TEST_CHECK(old_composition->NotifyHostTermination(23) == S_OK);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 23);
            }
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->PendingCount() == 2 && context->range_->text_ == L"a");
            if (!host_terminates) {
                QIQI_TEST_CHECK(context->last_edit_session_flags_ == (TF_ES_ASYNC | TF_ES_READWRITE));
            }
            const auto ui_before = std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
            QIQI_TEST_CHECK(queue->RunNext(29) == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(old_composition->end_composition_calls_ == (host_terminates ? 0u : 1u));
            QIQI_TEST_CHECK(old_composition->ended_ && context->range_->text_ == L"a");
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            QIQI_TEST_CHECK((std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"b");
            const auto new_composition = context->last_composition_;
            QIQI_TEST_CHECK(new_composition.Get() != old_composition.Get() && !new_composition->ended_);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            context->allow_sync_edit_sessions_ = true;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && new_composition->ended_);
        }

        for (const HRESULT failure : {E_ACCESSDENIED, TF_E_DISCONNECTED}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            context->async_edit_session_result_ = failure;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(!composition->ended_ && context->prop_->clear_calls_ == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && !context->layout_sink_);
        }
        const auto initial_objects = g_com_object_count.load();
        for (const bool execute_cleanup : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto peer_manager = MakeComObject<MockTfThreadMgr>();
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(peer->Activate(peer_manager.Get(), 2) == S_OK);
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK && queue->PendingCount() == 1);
            peer.Reset();
            peer_manager.Reset();
            if (execute_cleanup) {
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(!context->last_composition_->ended_);
            }
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }
}

} // namespace qiqi::test
