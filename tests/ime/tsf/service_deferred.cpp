#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include <array>
#include <cstddef>
#include <memory>
#include <tuple>

namespace qiqi::test {

void RunTsfDeferredSessionTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink, gsl::not_null<ITfCompositionSink*> comp_sink) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    // Deferred sessions must not cross activation or input-context epochs.
    {
        const auto ui_calls = [&] {
            return std::tuple{mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
        };
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto old_context = MakeComObject<MockTfContext>(queue);
        BOOL deferred_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(old_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        auto new_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2 && new_context->range_->text_.empty());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).context.Get() == old_context.Get());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).context.Get() == new_context.Get());
        const auto before_old_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(old_context->range_->text_.empty() && old_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(old_context->start_composition_calls_ == 0 && ui_calls() == before_old_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->range_->text_ == L"b" && new_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).flags == (TF_ES_ASYNCDONTCARE | TF_ES_READ));
        QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK && queue->PendingCount() == 1);
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        const auto before_old_layout = ui_calls();
        const auto old_view_calls = new_context->get_active_view_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->get_active_view_calls_ == old_view_calls && ui_calls() == before_old_layout);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        const auto current_view_calls = current_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(current_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(current_context->get_active_view_calls_ == current_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // No callback may write or create UI while the service is stopped.
        auto stopped_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        const auto while_stopped = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && stopped_context->range_->text_.empty());
        QIQI_TEST_CHECK(stopped_context->start_composition_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = true;
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == E_FAIL);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = false;
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);

        // An old commit/cancel must not affect a fresh composition on the same COM context.
        for (const UINT action_key : {VK_SPACE, VK_ESCAPE}) {
            auto same_context = MakeComObject<MockTfContext>(queue);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), action_key, 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
            const auto before_stale_action = ui_calls();
            const auto writes_before_stale_action = same_context->range_->set_text_calls_;
            const auto clears_before_stale_action = same_context->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(same_context->range_->set_text_calls_ == writes_before_stale_action);
            QIQI_TEST_CHECK(same_context->prop_->clear_calls_ == clears_before_stale_action &&
                            ui_calls() == before_stale_action);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"b" && same_context->start_composition_calls_ == 2);
            QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        }

        // A delayed cancel must not terminate or hide another context's fresh composition.
        auto cancelled_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), VK_ESCAPE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        auto fresh_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fresh_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->start_composition_calls_ == 1);
        const auto before_old_cancel = ui_calls();
        const auto fresh_writes = fresh_context->range_->set_text_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->range_->set_text_calls_ == fresh_writes);
        QIQI_TEST_CHECK(ui_calls() == before_old_cancel);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Resetting focus invalidates old work even when activation and context pointers are unchanged.
        auto focused_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_focus_reset = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_focus_reset);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->text_ == L"b");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Multiple current snapshots remain FIFO, including commit and the next composition.
        auto fifo_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 3);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"ab");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fifo_context->range_->set_text_calls_ == 3 && fifo_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(fifo_context->start_composition_calls_ == 2);
        const auto fifo_view_calls = fifo_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(fifo_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(fifo_context->get_active_view_calls_ == fifo_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // A -> B -> A requires an epoch, not merely pointer equality.
        auto context_a = MakeComObject<MockTfContext>(queue);
        auto context_b = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_b.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_context_switch = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_a->start_composition_calls_ == 0 && context_b->start_composition_calls_ == 0);
        QIQI_TEST_CHECK(context_a->range_->set_text_calls_ == 0 && context_b->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_context_switch);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_a->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(context_a->range_->text_ == L"c" && context_b->range_->text_.empty());

        // Never reuse A's composition range when B becomes the current input target.
        QIQI_TEST_CHECK(service->OnLayoutChange(context_a.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        const auto text_in_a = context_a->range_->text_;
        const auto reads_in_a = context_a->get_active_view_calls_;
        auto context_c = MakeComObject<MockTfContext>();
        const auto old_composition = context_a->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), VK_BACK, 0, &deferred_eaten) == S_OK && !deferred_eaten);
        QIQI_TEST_CHECK(context_c->start_composition_calls_ == 0 && context_c->range_->text_.empty());
        QIQI_TEST_CHECK(old_composition->ended_ && context_a->prop_->clear_calls_ > 0);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(context_c->range_->text_ == L"d");
        const auto before_context_layout = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(context_a->range_->text_ == text_in_a && context_a->get_active_view_calls_ == reads_in_a);
        QIQI_TEST_CHECK(ui_calls() == before_context_layout);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Host termination is another context-epoch reset, independent of activation.
        auto terminated_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(comp_sink->OnCompositionTerminated(1, nullptr) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_terminated_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(ui_calls() == before_terminated_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
    }
}

void RunTsfWriteRequestTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Mode { Immediate, Deferred, PendingCleanup };
    enum class Outcome { Success, RequestSFalse, RequestFailure, ReadOnly, AsyncFailure };
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
    for (const auto mode : {Mode::Immediate, Mode::Deferred, Mode::PendingCleanup}) {
        for (const auto outcome : {
                 Outcome::Success, Outcome::RequestSFalse, Outcome::RequestFailure,
                 Outcome::ReadOnly, Outcome::AsyncFailure}) {
            if (mode == Mode::Immediate && outcome == Outcome::AsyncFailure) continue;
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            context->SetDeferredEditSessions(false);
            send_key(context.Get(), 'A');
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && queue->PendingCount() == 0);
            const auto old_composition = context->last_composition_;
            const bool pending_cleanup = mode == Mode::PendingCleanup;
            if (pending_cleanup) {
                context->allow_sync_edit_sessions_ = false;
                QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK && queue->PendingCount() == 1);
                QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
                context->allow_sync_edit_sessions_ = true;
            }
            context->SetDeferredEditSessions(mode == Mode::Deferred);
            context->edit_session_request_result_ = outcome == Outcome::RequestSFalse ? S_FALSE :
                outcome == Outcome::RequestFailure ? E_ACCESSDENIED : S_OK;
            context->async_edit_session_result_ = outcome == Outcome::AsyncFailure ? E_ACCESSDENIED : S_OK;
            context->read_only_ = outcome == Outcome::ReadOnly;
            const auto requests_before = context->request_edit_session_calls_;
            const auto writes_before = context->range_->set_text_calls_;
            send_key(context.Get(), 'B');
            const bool rejected = outcome == Outcome::RequestFailure || outcome == Outcome::ReadOnly ||
                outcome == Outcome::AsyncFailure;
            const bool queued = mode != Mode::Immediate && !rejected;
            const DWORD flags = (pending_cleanup ? TF_ES_ASYNC : TF_ES_ASYNCDONTCARE) | TF_ES_READWRITE;
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + 1 &&
                            context->last_edit_session_flags_ == flags &&
                            queue->PendingCount() ==
                                (pending_cleanup ? std::size_t{1} : std::size_t{0}) +
                                (queued ? std::size_t{1} : std::size_t{0}));
            if (queued) {
                const auto& request = queue->PendingRequests().back();
                QIQI_TEST_CHECK(request.context.Get() == context.Get() && request.client_id == 1 &&
                                request.flags == flags);
            }
            QIQI_TEST_CHECK(context->range_->set_text_calls_ ==
                            writes_before + (!rejected && mode == Mode::Immediate ? 1u : 0u));
            if (pending_cleanup) {
                QIQI_TEST_CHECK(queue->RunNext(307) == S_OK && old_composition->ended_ &&
                                old_composition->last_end_cookie_ == 307 &&
                                context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
            }
            if (queued) QIQI_TEST_CHECK(queue->RunNext(311) == S_OK);
            QIQI_TEST_CHECK(queue->PendingCount() == 0 && context->range_->text_ ==
                            (rejected ? L"a" : pending_cleanup ? L"b" : L"ab"));
            if (!rejected && pending_cleanup) {
                QIQI_TEST_CHECK(context->last_composition_.Get() != old_composition.Get() &&
                                !context->last_composition_->ended_);
            }
            context->edit_session_request_result_ = S_OK;
            context->async_edit_session_result_ = S_OK;
            context->read_only_ = false;
            send_key(context.Get(), 'C');
            QIQI_TEST_CHECK(context->last_edit_session_flags_ == (TF_ES_ASYNCDONTCARE | TF_ES_READWRITE));
            if (mode == Mode::Deferred) {
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext(313) == S_OK);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0 && context->range_->text_ ==
                            (pending_cleanup ? L"bc" : L"abc") && !context->last_composition_->ended_);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && service->Activate(mock_tm.get(), 1) == S_OK);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 14 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
