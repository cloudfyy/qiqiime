#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include <memory>
#include <tuple>

namespace qiqi::test {

void RunTsfDeferredSessionTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink, gsl::not_null<ITfCompositionSink*> comp_sink) {
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

} // namespace qiqi::test
