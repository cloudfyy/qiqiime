// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/finish.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>

namespace qiqi::test {

void RunTsfCompositionTerminationRouteTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Route {
        Current, NullCurrent, NullEmpty, MissingContext, Unrelated,
        StartingCurrent, StartingOther, ScheduledOther, RemovedScheduled, ExpiredStarting,
    };
    constexpr std::array routes{
        Route::Current, Route::NullCurrent, Route::NullEmpty, Route::MissingContext, Route::Unrelated,
        Route::StartingCurrent, Route::StartingOther, Route::ScheduledOther, Route::RemovedScheduled, Route::ExpiredStarting,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const auto route : routes) {
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            context->range_->text_ = L"current";
            const auto composition = MakeComObject<MockTfComposition>(context->range_);
            const auto old_context = MakeComObject<MockTfContext>(queue);
            old_context->allow_sync_edit_sessions_ = false;
            old_context->range_->text_ = L"old";
            const auto old_composition = MakeComObject<MockTfComposition>(old_context->range_);
            CompositionLifecycleState state(593);
            state.SetActive(active);
            if (route != Route::MissingContext) state.SetContext(context);
            if (route != Route::NullEmpty) state.SetComposition(composition);
            const bool starting = route == Route::StartingCurrent || route == Route::StartingOther;
            const bool scheduled = route == Route::ScheduledOther || route == Route::RemovedScheduled;
            const bool tracked = starting || scheduled;
            std::shared_ptr<CompositionCleanup> record;
            ITfComposition* notification = composition.Get();
            if (route == Route::NullCurrent || route == Route::NullEmpty) notification = nullptr;
            if (route == Route::Unrelated || route == Route::StartingOther || scheduled) notification = old_composition.Get();
            if (starting || route == Route::ExpiredStarting) {
                record = state.Cleanups().TrackStarting(gsl::not_null<ITfContext*>(context.Get()));
                record->composition = notification;
                if (route == Route::ExpiredStarting) record.reset();
            }
            if (scheduled) {
                state.Cleanups().Schedule(old_context, old_composition, 23, state.Epoch(), GUID_NULL);
                QIQI_TEST_CHECK(queue->PendingCount() == 1);
                if (route == Route::RemovedScheduled) state.Cleanups().MarkContextRemoved(old_context.Get(), GUID_NULL);
            }
            unsigned int checks_seen = 0;
            unsigned int resets_seen = 0;
            unsigned int clear_callbacks = 0;
            context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 599);
                ++clear_callbacks;
            };
            const auto result = HandleCompositionTermination(
                599, notification, state, 593, GUID_NULL,
                [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                    checks_seen = ++*checks;
                    return state.IsCurrent(ticket) && !state.Composition();
                },
                [&, resets = std::make_unique<unsigned int>(0)] {
                    resets_seen = ++*resets;
                    state.InvalidateContext();
                    state.ClearContext();
                    state.ClearComposition();
                });
            const bool current_query = route == Route::Current || route == Route::ExpiredStarting;
            const bool direct_reset = route == Route::NullCurrent || route == Route::NullEmpty || route == Route::MissingContext;
            const bool reset = direct_reset || (current_query && active);
            QIQI_TEST_CHECK(result == S_OK && checks_seen == (current_query ? (active ? 4u : 2u) : 0u) &&
                            resets_seen == (reset ? 1u : 0u) && clear_callbacks == (current_query && active ? 1u : 0u) &&
                            state.IsActive() == active && state.Epoch().context == (reset ? 1u : 0u) &&
                            context->range_->text_ == L"current" && context->range_->set_text_calls_ == 0 &&
                            composition->end_composition_calls_ == 0 && context->request_edit_session_calls_ == 0);
            if (tracked || route == Route::Unrelated) {
                QIQI_TEST_CHECK(state.Composition().Get() == composition.Get() && state.Context().Get() == context.Get());
            } else {
                QIQI_TEST_CHECK(!state.Composition());
            }
            const auto unexpected_check = [tracked](EditSessionTicket) {
                QIQI_TEST_CHECK(!tracked);
                return false;
            };
            const auto unexpected_reset = [tracked] { QIQI_TEST_CHECK(!tracked); };
            if (starting) {
                QIQI_TEST_CHECK(record && record->terminated && record->finished &&
                                HandleCompositionTermination(
                                    601, notification, state, 593, GUID_NULL,
                                    unexpected_check, unexpected_reset) == S_OK);
            }
            if (scheduled) {
                const unsigned int expected_clears = route == Route::RemovedScheduled ? 0u : 1u;
                QIQI_TEST_CHECK(old_context->prop_->clear_calls_ == expected_clears &&
                                HandleCompositionTermination(
                                    601, notification, state, 593, GUID_NULL,
                                    unexpected_check, unexpected_reset) == S_OK &&
                                old_context->prop_->clear_calls_ == expected_clears);
                QIQI_TEST_CHECK(queue->RunNext(607) == S_OK && old_composition->end_composition_calls_ == 0 &&
                                old_context->range_->text_ == L"old" && old_context->range_->set_text_calls_ == 0 &&
                                state.Composition().Get() == composition.Get());
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0 && !state.Cleanups().HasPending(old_context.Get(), GUID_NULL));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 20 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
