#include "common/test_support.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <vector>

namespace qiqi::test {
namespace {

void TestCleanupHandoffOutcomes() {
    enum class Outcome { Immediate, Deferred, Rejected, ReadOnly, MissingClient, MissingContext };
    enum class Event { Range, Clear, End };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool has_composition : {false, true}) {
            for (const auto outcome : {Outcome::Immediate, Outcome::Deferred, Outcome::Rejected,
                                      Outcome::ReadOnly, Outcome::MissingClient, Outcome::MissingContext}) {
                const auto queue = std::make_shared<MockEditSessionQueue>();
                const auto context = MakeComObject<MockTfContext>(queue);
                context->range_->text_ = L"preedit";
                const auto composition = has_composition ? MakeComObject<MockTfComposition>(context->range_) :
                    ComPtr<MockTfComposition>{};
                CompositionLifecycleState state(503);
                state.SetActive(active);
                state.InvalidateActivation();
                state.InvalidateContext();
                if (outcome != Outcome::MissingContext) state.SetContext(context);
                state.SetComposition(composition);
                context->allow_sync_edit_sessions_ = outcome != Outcome::Deferred;
                context->read_only_ = outcome == Outcome::ReadOnly;
                context->edit_session_request_result_ = outcome == Outcome::Rejected ? E_ACCESSDENIED : S_OK;
                TfClientId client_id = outcome == Outcome::MissingClient ? TF_CLIENTID_NULL : 23;
                std::vector<Event> events;
                if (composition) {
                    composition->get_range_observer_ = [&](ITfRange**) {
                        events.push_back(Event::Range);
                        QIQI_TEST_CHECK(!state.Composition() &&
                                        composition->AddRef() == 3 && composition->Release() == 2);
                    };
                    context->prop_->clear_callback_ = [&](TfEditCookie) { events.push_back(Event::Clear); };
                    composition->end_callback_ = [&](TfEditCookie ec) {
                        events.push_back(Event::End);
                        QIQI_TEST_CHECK(!state.Composition() &&
                                        ec == (outcome == Outcome::Deferred ? TfEditCookie{509} : TfEditCookie{1}));
                    };
                }
                ScheduleCurrentCompositionCleanup(state, client_id, 503, GUID_NULL);
                const bool deferred = has_composition && outcome == Outcome::Deferred;
                const bool immediate = has_composition && outcome == Outcome::Immediate;
                const bool requested = has_composition && outcome != Outcome::MissingClient && outcome != Outcome::MissingContext;
                QIQI_TEST_CHECK(!state.Composition() && state.IsActive() == active &&
                                state.Epoch().activation == 1 && state.Epoch().context == 1 &&
                                state.Context().Get() == (outcome == Outcome::MissingContext ? nullptr : context.Get()) &&
                                context->request_edit_session_calls_ == (requested ? (deferred ? 2u : 1u) : 0u) &&
                                queue->PendingCount() == (deferred ? 1u : 0u) &&
                                state.Cleanups().HasPending(context.Get(), GUID_NULL) == deferred);
                std::vector<Event> expected;
                if (requested) expected.push_back(Event::Range);
                if (immediate) {
                    expected.push_back(Event::Clear);
                    expected.push_back(Event::End);
                }
                QIQI_TEST_CHECK(events == expected);
                if (requested) {
                    const DWORD expected_flags = (deferred ? TF_ES_ASYNC : TF_ES_SYNC) | TF_ES_READWRITE;
                    QIQI_TEST_CHECK(context->last_edit_session_flags_ == expected_flags);
                }
                if (deferred) {
                    QIQI_TEST_CHECK(queue->PendingRequests().front().client_id == 23 &&
                                    queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                    client_id = 29;
                    QIQI_TEST_CHECK(client_id == 29 && queue->PendingRequests().front().client_id == 23 &&
                                    queue->RunNext(509) == S_OK);
                    expected.push_back(Event::Clear);
                    expected.push_back(Event::End);
                }
                const bool executed = immediate || deferred;
                if (composition) {
                    QIQI_TEST_CHECK(composition->ended_ == executed && composition->end_composition_calls_ == (executed ? 1u : 0u) &&
                                    composition->AddRef() == 2 && composition->Release() == 1);
                }
                QIQI_TEST_CHECK(events == expected && context->prop_->clear_calls_ == (executed ? 1u : 0u) &&
                                context->range_->text_ == L"preedit" && context->range_->set_text_calls_ == 0 &&
                                context->start_composition_calls_ == 0 && !state.Composition() &&
                                !state.Cleanups().HasPending(context.Get(), GUID_NULL) && queue->PendingCount() == 0);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 24 && g_com_object_count.load() == objects_before);
}

void TestCleanupHandoffReentrancy() {
    enum class Boundary { Range, Clear, End };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool deferred : {false, true}) {
        for (const bool invalidate : {false, true}) {
            for (const auto boundary : {Boundary::Range, Boundary::Clear, Boundary::End}) {
                const auto queue = std::make_shared<MockEditSessionQueue>();
                const auto context = MakeComObject<MockTfContext>(queue);
                context->range_->text_ = L"old";
                context->allow_sync_edit_sessions_ = !deferred;
                const auto composition = MakeComObject<MockTfComposition>(context->range_);
                const auto replacement = MakeComObject<MockTfContext>();
                replacement->range_->text_ = L"new";
                const auto replacement_composition = MakeComObject<MockTfComposition>(replacement->range_);
                CompositionLifecycleState state(521);
                state.SetActive(true);
                state.SetContext(context);
                state.SetComposition(composition);
                unsigned int callbacks = 0;
                const auto callback = [&](Boundary current) {
                    if (current != boundary) return;
                    ++callbacks;
                    QIQI_TEST_CHECK(!state.Composition());
                    if (invalidate) state.InvalidateContext();
                    state.SetContext(replacement);
                    state.SetComposition(replacement_composition);
                };
                composition->get_range_observer_ = [&](ITfRange**) { callback(Boundary::Range); };
                context->prop_->clear_callback_ = [&](TfEditCookie) { callback(Boundary::Clear); };
                composition->end_callback_ = [&](TfEditCookie ec) {
                    QIQI_TEST_CHECK(ec == (deferred ? TfEditCookie{523} : TfEditCookie{1}));
                    callback(Boundary::End);
                };
                ScheduleCurrentCompositionCleanup(state, 23, 521, GUID_NULL);
                if (deferred) {
                    const bool reentered = boundary == Boundary::Range;
                    QIQI_TEST_CHECK(state.Composition().Get() == (reentered ? replacement_composition.Get() : nullptr) &&
                                    callbacks == (reentered ? 1u : 0u) && queue->PendingCount() == 1);
                    QIQI_TEST_CHECK(queue->RunNext(523) == S_OK);
                }
                QIQI_TEST_CHECK(callbacks == 1 && state.IsActive() && state.Epoch().activation == 0 &&
                                state.Epoch().context == (invalidate ? 1u : 0u) &&
                                state.Context().Get() == replacement.Get() && state.Composition().Get() == replacement_composition.Get() &&
                                composition->ended_ && composition->end_composition_calls_ == 1 &&
                                context->prop_->clear_calls_ == 1 && context->range_->text_ == L"old" &&
                                context->range_->set_text_calls_ == 0 && replacement->range_->text_ == L"new" &&
                                replacement->range_->set_text_calls_ == 0 && replacement->prop_->clear_calls_ == 0 &&
                                !replacement_composition->ended_ && queue->PendingCount() == 0 &&
                                !state.Cleanups().HasPending(context.Get(), GUID_NULL));
                QIQI_TEST_CHECK(composition->AddRef() == 2 && composition->Release() == 1 &&
                                replacement_composition->AddRef() == 3 && replacement_composition->Release() == 2);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 12 && g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionCleanupHandoffTests() {
    TestCleanupHandoffOutcomes();
    TestCleanupHandoffReentrancy();
}

} // namespace qiqi::test
