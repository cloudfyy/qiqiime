#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/comp/tests.h"
#include "ime/comp/attribute.h"
#include "ime/comp/cleanup.h"
#include "ime/tsf/edit_session.h"
#include <array>
#include <memory>

namespace qiqi::test {

namespace {

void TestCompositionCleanupRegistry() {
    static_assert(!std::is_copy_constructible_v<CompositionCleanup>);
    static_assert(!std::is_move_constructible_v<CompositionCleanup>);
    static_assert(!std::is_copy_constructible_v<CompositionCleanupRegistry>);
    static_assert(!std::is_move_constructible_v<CompositionCleanupRegistry>);
    const auto first = MakeComObject<MockTfContext>();
    const auto second = MakeComObject<MockTfContext>();
    const auto first_composition = MakeComObject<MockTfComposition>(first->range_);
    const auto second_composition = MakeComObject<MockTfComposition>(second->range_);
    const auto make_cleanup = [](gsl::not_null<ITfContext*> context, ITfComposition* composition, uint64_t id) {
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context.get();
        cleanup->composition = composition;
        cleanup->id = id;
        return cleanup;
    };

    CompositionCleanupRegistry registry;
    QIQI_TEST_CHECK(registry.TrackedCount() == 0 && !registry.FindComposition(nullptr));
    QIQI_TEST_CHECK(!registry.FindPending(nullptr) && !registry.FindPending(first.Get()));
    const auto starting = make_cleanup(first.Get(), nullptr, 0);
    registry.Track(starting);
    QIQI_TEST_CHECK(starting.use_count() == 1 && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindComposition(nullptr));
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && !registry.FindPending(second.Get()));

    const auto scheduled = make_cleanup(first.Get(), first_composition.Get(), 1);
    auto other = make_cleanup(second.Get(), second_composition.Get(), 2);
    registry.Track(scheduled);
    registry.Track(other);
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && registry.TrackedCount() == 3);
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == scheduled);
    QIQI_TEST_CHECK(registry.FindComposition(second_composition.Get()) == other);
    scheduled->finished = true;
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == scheduled && registry.TrackedCount() == 3);
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && registry.TrackedCount() == 2);
    QIQI_TEST_CHECK(!registry.FindComposition(first_composition.Get()));
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    QIQI_TEST_CHECK(starting->context_removed && starting->finished && !starting->terminated);
    QIQI_TEST_CHECK(!scheduled->context_removed && !other->context_removed && !other->finished);
    QIQI_TEST_CHECK(registry.TrackedCount() == 2);
    QIQI_TEST_CHECK(!registry.FindPending(first.Get()) && registry.TrackedCount() == 1);
    const std::weak_ptr<CompositionCleanup> expired = other;
    other.reset();
    QIQI_TEST_CHECK(expired.expired() && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindComposition(second_composition.Get()) && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindPending(nullptr) && registry.TrackedCount() == 0);

    // Removal reaches every matching record, including already-finished and unpublished results.
    const auto pending = make_cleanup(first.Get(), first_composition.Get(), 3);
    const auto finished = make_cleanup(first.Get(), first_composition.Get(), 4);
    const auto unpublished = make_cleanup(first.Get(), nullptr, 0);
    const auto foreign = make_cleanup(second.Get(), second_composition.Get(), 5);
    finished->finished = true;
    registry.Track(pending);
    registry.Track(finished);
    registry.Track(unpublished);
    registry.Track(foreign);
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    for (const auto& cleanup : {pending, finished, unpublished}) {
        QIQI_TEST_CHECK(cleanup->context_removed && cleanup->finished && !cleanup->terminated);
    }
    QIQI_TEST_CHECK(!foreign->context_removed && !foreign->finished);
    QIQI_TEST_CHECK(registry.TrackedCount() == 4);
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == pending);
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    QIQI_TEST_CHECK(!registry.FindPending(first.Get()) && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(registry.FindPending(second.Get()) == foreign);

    const auto survivor = make_cleanup(first.Get(), first_composition.Get(), 6);
    {
        CompositionCleanupRegistry transient;
        transient.Track(survivor);
        QIQI_TEST_CHECK(survivor.use_count() == 1);
    }
    QIQI_TEST_CHECK(survivor.use_count() == 1 && !survivor->finished && !survivor->context_removed);
    QIQI_TEST_CHECK(survivor->context.Get() == first.Get() &&
                    survivor->composition.Get() == first_composition.Get());
    QIQI_TEST_CHECK(first->request_edit_session_calls_ == 0 && second->request_edit_session_calls_ == 0);
    QIQI_TEST_CHECK(first->prop_->clear_calls_ == 0 && second->prop_->clear_calls_ == 0);
    QIQI_TEST_CHECK(first_composition->end_composition_calls_ == 0 &&
                    second_composition->end_composition_calls_ == 0);
    QIQI_TEST_CHECK(first->range_->set_text_calls_ == 0 && second->range_->set_text_calls_ == 0);
}

void TestCompositionCleanupExecution() {
    enum class Interrupt { None, RemovedBefore, TerminatedBefore, RemovedLookup, TerminatedLookup, RemovedClear, TerminatedClear };
    struct Case {
        bool has_range = true;
        Interrupt interrupt = Interrupt::None;
        HRESULT property_hr = S_OK;
        bool null_property = false;
        HRESULT clear_hr = S_OK;
        HRESULT end_hr = S_OK;
        unsigned int clear_calls = 1;
        unsigned int end_calls = 1;
    };
    const std::array cases{
        Case{},
        Case{.has_range = false, .clear_calls = 0},
        Case{.interrupt = Interrupt::RemovedBefore, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedBefore, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::RemovedLookup, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedLookup, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::RemovedClear, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedClear, .end_calls = 0},
        Case{.property_hr = E_ACCESSDENIED, .clear_calls = 0},
        Case{.null_property = true, .clear_calls = 0},
        Case{.clear_hr = E_ACCESSDENIED},
        Case{.end_hr = TF_E_DISCONNECTED},
        Case{.clear_hr = E_ACCESSDENIED, .end_hr = TF_E_DISCONNECTED},
    };
    for (const auto& test : cases) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context;
        cleanup->composition = composition;
        if (test.has_range) cleanup->range = context->range_;
        context->get_property_result_ = test.property_hr;
        context->return_null_property_ = test.null_property;
        context->prop_->clear_result_ = test.clear_hr;
        composition->end_result_ = test.end_hr;
        cleanup->context_removed = test.interrupt == Interrupt::RemovedBefore;
        cleanup->terminated = test.interrupt == Interrupt::TerminatedBefore;
        context->get_property_callback_ = [&]() noexcept {
            if (test.interrupt == Interrupt::RemovedLookup) cleanup->context_removed = true;
            if (test.interrupt == Interrupt::TerminatedLookup) cleanup->terminated = true;
        };
        context->prop_->clear_callback_ = [&](TfEditCookie ec) noexcept {
            QIQI_TEST_CHECK(ec == 71);
            if (test.interrupt == Interrupt::RemovedClear) cleanup->context_removed = true;
            if (test.interrupt == Interrupt::TerminatedClear) cleanup->terminated = true;
        };
        const HRESULT expected_hr = test.end_calls ? test.end_hr : S_OK;
        QIQI_TEST_CHECK(ExecuteCompositionCleanup(cleanup, 71) == expected_hr);
        QIQI_TEST_CHECK(cleanup->started && cleanup->finished);
        QIQI_TEST_CHECK(context->prop_->clear_calls_ == test.clear_calls &&
                        cleanup->attribute_clear_started == (test.clear_calls != 0));
        QIQI_TEST_CHECK(composition->end_composition_calls_ == test.end_calls &&
                        composition->ended_ == (test.end_calls != 0 && SUCCEEDED(test.end_hr)));
        if (test.clear_calls) QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 71);
        if (test.end_calls) QIQI_TEST_CHECK(composition->last_end_cookie_ == 71);
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
        context->get_property_callback_ = {};
        context->prop_->clear_callback_ = {};
    }

    for (const auto failure : {0, 1, 2}) {
        const auto context = MakeComObject<MockTfContext>();
        context->get_property_result_ = failure == 0 ? E_ACCESSDENIED : S_OK;
        context->return_null_property_ = failure == 1;
        context->prop_->clear_result_ = TF_E_DISCONNECTED;
        bool started = false;
        const HRESULT expected_hr = failure == 0 ? E_ACCESSDENIED : failure == 1 ? E_UNEXPECTED : TF_E_DISCONNECTED;
        QIQI_TEST_CHECK(ClearCompositionDisplayAttribute(
            73, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfRange*>(context->range_.Get()),
            []() noexcept { return true; }, &started) == expected_hr);
        QIQI_TEST_CHECK(started == (failure == 2) && context->prop_->clear_calls_ == (failure == 2 ? 1u : 0u));
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        bool looked_up = false;
        context->get_property_callback_ = [&]() noexcept { looked_up = true; };
        QIQI_TEST_CHECK(ClearCompositionDisplayAttribute(
            73, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfRange*>(context->range_.Get()),
            []() noexcept { return false; }) == S_FALSE);
        QIQI_TEST_CHECK(!looked_up && context->prop_->clear_calls_ == 0);
        context->get_property_callback_ = {};
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context;
        cleanup->composition = composition;
        cleanup->range = context->range_;
        const std::weak_ptr<CompositionCleanup> released = cleanup;
        context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(ec == 79);
            cleanup.reset();
            QIQI_TEST_CHECK(!released.expired());
        };
        QIQI_TEST_CHECK(ExecuteCompositionCleanup(cleanup, 79) == S_OK);
        QIQI_TEST_CHECK(!cleanup && released.expired() && composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(composition->last_end_cookie_ == 79 && context->prop_->last_clear_cookie_ == 79);
        context->prop_->clear_callback_ = {};
    }
}

void TestCompositionCleanupRequests() {
    enum class Invalidation { None, Removed, Terminated };
    struct Case {
        bool asynchronous = false;
        bool read_only = false;
        HRESULT request_hr = S_OK;
        HRESULT async_hr = S_OK;
        HRESULT end_hr = S_OK;
        bool range_failure = false;
        bool null_range = false;
        Invalidation invalidation = Invalidation::None;
        bool discard = false;
    };
    const std::array cases{
        Case{},
        Case{.asynchronous = true},
        Case{.read_only = true},
        Case{.request_hr = E_ACCESSDENIED},
        Case{.request_hr = TF_E_DISCONNECTED},
        Case{.request_hr = TF_E_SYNCHRONOUS},
        Case{.end_hr = E_ACCESSDENIED},
        Case{.end_hr = TF_E_SYNCHRONOUS},
        Case{.asynchronous = true, .async_hr = E_ACCESSDENIED},
        Case{.asynchronous = true, .async_hr = TF_E_DISCONNECTED},
        Case{.range_failure = true},
        Case{.null_range = true},
        Case{.invalidation = Invalidation::Removed},
        Case{.invalidation = Invalidation::Terminated},
        Case{.asynchronous = true, .discard = true},
        Case{.asynchronous = true, .range_failure = true},
        Case{.asynchronous = true, .end_hr = E_ACCESSDENIED},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->id = 1;
        cleanup->context = context;
        cleanup->composition = composition;
        CompositionCleanupRegistry registry;
        registry.Track(cleanup);
        bool registered_during_range = false;
        composition->get_range_callback_ = [&]() noexcept {
            registered_during_range = registry.FindComposition(composition.Get()) == cleanup &&
                                      registry.FindPending(context.Get()) == cleanup;
            QIQI_TEST_CHECK(registered_during_range);
            if (test.invalidation == Invalidation::Removed) {
                registry.MarkContextRemoved(context.Get(), 0, GUID_NULL);
            } else if (test.invalidation == Invalidation::Terminated) {
                QIQI_TEST_CHECK(HandleCompositionCleanupTermination(
                    registry, 89, composition.Get(), 0, GUID_NULL));
            }
        };
        composition->end_callback_ = [&](TfEditCookie ec) noexcept {
            QIQI_TEST_CHECK(cleanup->started && !cleanup->finished);
            QIQI_TEST_CHECK(ec == (test.asynchronous ? TfEditCookie{83} : TfEditCookie{1}));
        };
        composition->fail_get_range_ = test.range_failure;
        composition->return_null_range_ = test.null_range;
        composition->end_result_ = test.end_hr;
        context->allow_sync_edit_sessions_ = !test.asynchronous;
        context->read_only_ = test.read_only;
        context->edit_session_request_result_ = test.request_hr;
        context->async_edit_session_result_ = test.async_hr;
        RequestCompositionCleanup(cleanup, 7);
        composition->get_range_callback_ = {};
        const bool invalidated = test.invalidation != Invalidation::None;
        const bool rejected = FAILED(test.request_hr) || test.read_only;
        const bool fallback = !invalidated && !rejected && test.asynchronous;
        const bool queued = fallback && SUCCEEDED(test.async_hr);
        const bool immediate = !invalidated && !rejected && !test.asynchronous;
        const unsigned int requests = invalidated ? 0u : fallback ? 2u : 1u;
        QIQI_TEST_CHECK(registered_during_range == !test.range_failure);
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests &&
                        cleanup->async_requested == fallback);
        QIQI_TEST_CHECK(cleanup->started == immediate && cleanup->finished == !queued);
        QIQI_TEST_CHECK(queue->PendingCount() == (queued ? 1u : 0u));
        if (requests) {
            QIQI_TEST_CHECK(context->last_edit_session_flags_ ==
                            ((fallback ? DWORD{TF_ES_ASYNC} : DWORD{TF_ES_SYNC}) | DWORD{TF_ES_READWRITE}));
        }
        if (queued) {
            const auto& request = queue->PendingRequests().front();
            QIQI_TEST_CHECK(request.client_id == 7 && request.context.Get() == context.Get() &&
                            request.flags == (TF_ES_ASYNC | TF_ES_READWRITE));
            QIQI_TEST_CHECK(!composition->ended_ && context->prop_->clear_calls_ == 0);
            if (test.discard) {
                const std::weak_ptr<CompositionCleanup> released = cleanup;
                cleanup.reset();
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && released.expired());
                QIQI_TEST_CHECK(!registry.FindPending(context.Get()) && registry.TrackedCount() == 0);
                QIQI_TEST_CHECK(composition->end_composition_calls_ == 0 && context->prop_->clear_calls_ == 0);
                composition->end_callback_ = {};
                continue;
            }
            QIQI_TEST_CHECK(queue->RunNext(83) == test.end_hr && queue->PendingCount() == 0);
        }
        const bool executed = immediate || queued;
        const unsigned int clear_calls = executed && !test.range_failure && !test.null_range ? 1u : 0u;
        QIQI_TEST_CHECK(cleanup->finished && cleanup->started == executed);
        QIQI_TEST_CHECK(composition->end_composition_calls_ == (executed ? 1u : 0u) &&
                        context->prop_->clear_calls_ == clear_calls);
        QIQI_TEST_CHECK(!registry.FindPending(context.Get()) && registry.TrackedCount() == 0);
        QIQI_TEST_CHECK(context->range_->set_text_calls_ == 0);
        composition->end_callback_ = {};
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionCleanupTermination() {
    enum class Reentry { None, LookupRemoval, LookupDuplicate, ClearRemoval, ClearDuplicate };
    struct Case {
        bool unpublished = false;
        bool started = false;
        bool finished = false;
        bool terminated = false;
        bool removed = false;
        bool has_range = true;
        bool attribute_started = false;
        bool null_property = false;
        HRESULT property_hr = S_OK;
        HRESULT clear_hr = S_OK;
        Reentry reentry = Reentry::None;
        unsigned int clear_calls = 1;
        unsigned int property_calls = 1;
        bool expect_finished = true;
    };
    const std::array cases{
        Case{},
        Case{.started = true, .expect_finished = false},
        Case{.finished = true},
        Case{.finished = true, .removed = true, .clear_calls = 0, .property_calls = 0},
        Case{.finished = true, .terminated = true, .clear_calls = 0, .property_calls = 0},
        Case{.started = true, .terminated = true, .clear_calls = 0, .property_calls = 0, .expect_finished = false},
        Case{.has_range = false, .clear_calls = 0, .property_calls = 0},
        Case{.attribute_started = true, .clear_calls = 0, .property_calls = 0},
        Case{.unpublished = true, .clear_calls = 0, .property_calls = 0},
        Case{.unpublished = true, .started = true, .clear_calls = 0, .property_calls = 0},
        Case{.null_property = true, .clear_calls = 0},
        Case{.property_hr = E_ACCESSDENIED, .clear_calls = 0},
        Case{.clear_hr = E_ACCESSDENIED},
        Case{.reentry = Reentry::LookupRemoval, .clear_calls = 0},
        Case{.reentry = Reentry::LookupDuplicate},
        Case{.reentry = Reentry::ClearRemoval},
        Case{.reentry = Reentry::ClearDuplicate},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto unrelated = MakeComObject<MockTfComposition>(context->range_);
        const auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->id = test.unpublished ? 0u : 1u;
        cleanup->context = context;
        cleanup->composition = composition;
        if (test.has_range) cleanup->range = context->range_;
        cleanup->started = test.started;
        cleanup->finished = test.finished;
        cleanup->terminated = test.terminated;
        cleanup->context_removed = test.removed;
        cleanup->attribute_clear_started = test.attribute_started;
        context->return_null_property_ = test.null_property;
        context->get_property_result_ = test.property_hr;
        context->prop_->clear_result_ = test.clear_hr;
        CompositionCleanupRegistry registry;
        registry.Track(cleanup);
        QIQI_TEST_CHECK(!HandleCompositionCleanupTermination(registry, 97, nullptr, 0, GUID_NULL));
        QIQI_TEST_CHECK(!HandleCompositionCleanupTermination(registry, 97, unrelated.Get(), 0, GUID_NULL));
        QIQI_TEST_CHECK(cleanup->terminated == test.terminated && cleanup->finished == test.finished);
        unsigned int reentries = 0;
        const auto reenter = [&] {
            ++reentries;
            QIQI_TEST_CHECK(cleanup->terminated);
            if (test.reentry == Reentry::LookupRemoval || test.reentry == Reentry::ClearRemoval) {
                registry.MarkContextRemoved(context.Get(), 0, GUID_NULL);
            } else {
                QIQI_TEST_CHECK(HandleCompositionCleanupTermination(registry, 97, composition.Get(), 0, GUID_NULL));
            }
        };
        context->get_property_callback_ = [&] {
            QIQI_TEST_CHECK(cleanup->terminated);
            if (test.reentry == Reentry::LookupRemoval || test.reentry == Reentry::LookupDuplicate) reenter();
        };
        context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(ec == 97 && cleanup->terminated && cleanup->attribute_clear_started);
            if (test.reentry == Reentry::ClearRemoval || test.reentry == Reentry::ClearDuplicate) reenter();
        };
        QIQI_TEST_CHECK(HandleCompositionCleanupTermination(registry, 97, composition.Get(), 0, GUID_NULL));
        QIQI_TEST_CHECK(cleanup->terminated && cleanup->finished == test.expect_finished &&
                        cleanup->started == test.started && !cleanup->async_requested);
        QIQI_TEST_CHECK(cleanup->context_removed ==
                        (test.removed || test.reentry == Reentry::LookupRemoval || test.reentry == Reentry::ClearRemoval));
        QIQI_TEST_CHECK(cleanup->attribute_clear_started == (test.attribute_started || test.clear_calls != 0));
        QIQI_TEST_CHECK(context->get_property_calls_ == test.property_calls &&
                        context->prop_->clear_calls_ == test.clear_calls &&
                        reentries == (test.reentry == Reentry::None ? 0u : 1u));
        QIQI_TEST_CHECK(HandleCompositionCleanupTermination(registry, 101, composition.Get(), 0, GUID_NULL));
        QIQI_TEST_CHECK(context->get_property_calls_ == test.property_calls &&
                        context->prop_->clear_calls_ == test.clear_calls &&
                        cleanup->finished == test.expect_finished);
        QIQI_TEST_CHECK(composition->end_composition_calls_ == 0 && !composition->ended_ &&
                        context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(registry.TrackedCount() == 1 && registry.FindComposition(composition.Get()) == cleanup);
        context->get_property_callback_ = {};
        context->prop_->clear_callback_ = {};
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->id = 1;
        cleanup->context = context;
        cleanup->composition = composition;
        cleanup->range = context->range_;
        const std::weak_ptr<CompositionCleanup> released = cleanup;
        CompositionCleanupRegistry registry;
        registry.Track(cleanup);
        context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(ec == 103);
            registry.MarkContextRemoved(context.Get(), 0, GUID_NULL);
            QIQI_TEST_CHECK(!registry.FindPending(context.Get()) && registry.TrackedCount() == 0);
            cleanup.reset();
            QIQI_TEST_CHECK(!released.expired());
        };
        QIQI_TEST_CHECK(HandleCompositionCleanupTermination(registry, 103, composition.Get(), 0, GUID_NULL));
        QIQI_TEST_CHECK(released.expired() && context->prop_->clear_calls_ == 1 &&
                        composition->end_composition_calls_ == 0 && context->request_edit_session_calls_ == 0);
        context->prop_->clear_callback_ = {};
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionCleanupRegistry registry;
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context;
        registry.Track(cleanup);
        QIQI_TEST_CHECK(!HandleCompositionCleanupTermination(registry, 107, nullptr, 0, GUID_NULL));
        QIQI_TEST_CHECK(!cleanup->terminated && !cleanup->finished);
        cleanup->composition = composition;
        cleanup.reset();
        QIQI_TEST_CHECK(!HandleCompositionCleanupTermination(registry, 107, composition.Get(), 0, GUID_NULL));
        QIQI_TEST_CHECK(registry.TrackedCount() == 1 && context->get_property_calls_ == 0 &&
                        context->prop_->clear_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionCleanupCoordinator() {
    static_assert(!std::is_copy_constructible_v<CompositionCleanupCoordinator>);
    static_assert(!std::is_move_constructible_v<CompositionCleanupCoordinator>);
    enum class RangeEvent { None, Removed, Terminated };
    enum class QueuedEvent { Run, Discard, Removed, Terminated };
    struct Case {
        bool asynchronous = false;
        RangeEvent range_event = RangeEvent::None;
        QueuedEvent queued_event = QueuedEvent::Run;
        HRESULT request_hr = S_OK;
        HRESULT async_hr = S_OK;
        unsigned int requests = 1;
        bool queued = false;
        unsigned int clears = 1;
        unsigned int ends = 1;
    };
    const std::array cases{
        Case{},
        Case{.asynchronous = true, .requests = 2, .queued = true},
        Case{.asynchronous = true, .queued_event = QueuedEvent::Discard,
             .requests = 2, .queued = true, .clears = 0, .ends = 0},
        Case{.asynchronous = true, .queued_event = QueuedEvent::Removed,
             .requests = 2, .queued = true, .clears = 0, .ends = 0},
        Case{.asynchronous = true, .queued_event = QueuedEvent::Terminated,
             .requests = 2, .queued = true, .ends = 0},
        Case{.asynchronous = true, .range_event = RangeEvent::Removed, .requests = 0, .clears = 0, .ends = 0},
        Case{.asynchronous = true, .range_event = RangeEvent::Terminated, .requests = 0, .clears = 0, .ends = 0},
        Case{.request_hr = E_ACCESSDENIED, .clears = 0, .ends = 0},
        Case{.asynchronous = true, .async_hr = E_ACCESSDENIED, .requests = 2, .clears = 0, .ends = 0},
    };
    constexpr GUID activity{0x23456789, 0x2345, 0x6789, {0xab, 0xcd, 0xef, 1, 2, 3, 4, 5}};
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        const auto unrelated = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionCleanupCoordinator coordinator(41);
        context->allow_sync_edit_sessions_ = !test.asynchronous;
        context->edit_session_request_result_ = test.request_hr;
        context->async_edit_session_result_ = test.async_hr;
        bool registered_during_range = false;
        composition->get_range_callback_ = [&] {
            registered_during_range = coordinator.TrackedCount() == 1 && coordinator.HasPending(context.Get(), activity);
            QIQI_TEST_CHECK(registered_during_range && !coordinator.HasPending(unrelated.Get(), activity));
            if (test.range_event == RangeEvent::Removed) {
                coordinator.MarkContextRemoved(context.Get(), activity);
            } else if (test.range_event == RangeEvent::Terminated) {
                QIQI_TEST_CHECK(coordinator.HandleTermination(105, composition.Get(), activity));
            }
        };
        coordinator.Schedule(context, composition, 7, {9, 17}, activity);
        composition->get_range_callback_ = {};
        QIQI_TEST_CHECK(registered_during_range && context->request_edit_session_calls_ == test.requests);
        QIQI_TEST_CHECK(queue->PendingCount() == (test.queued ? 1u : 0u));
        QIQI_TEST_CHECK(coordinator.HasPending(context.Get(), activity) == test.queued);
        if (test.queued) {
            const auto& request = queue->PendingRequests().front();
            QIQI_TEST_CHECK(request.client_id == 7 && request.context.Get() == context.Get() &&
                            request.flags == (TF_ES_ASYNC | TF_ES_READWRITE));
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 0 && composition->end_composition_calls_ == 0);
            if (test.queued_event == QueuedEvent::Removed || test.queued_event == QueuedEvent::Terminated) {
                if (test.queued_event == QueuedEvent::Removed) coordinator.MarkContextRemoved(context.Get(), activity);
                QIQI_TEST_CHECK(coordinator.HandleTermination(107, composition.Get(), activity));
                QIQI_TEST_CHECK(coordinator.HandleTermination(107, composition.Get(), activity));
                QIQI_TEST_CHECK(coordinator.TrackedCount() == 1);
                QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), activity) && coordinator.TrackedCount() == 0);
                QIQI_TEST_CHECK(!coordinator.HandleTermination(107, composition.Get(), activity));
            }
            if (test.queued_event == QueuedEvent::Discard) {
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
            } else {
                QIQI_TEST_CHECK(queue->RunNext(109) == S_OK);
            }
        }
        QIQI_TEST_CHECK(queue->PendingCount() == 0 && context->prop_->clear_calls_ == test.clears &&
                        composition->end_composition_calls_ == test.ends);
        if (test.clears) {
            const TfEditCookie cookie = test.queued_event == QueuedEvent::Terminated ?
                TfEditCookie{107} : test.asynchronous ? TfEditCookie{109} : TfEditCookie{1};
            QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == cookie);
        }
        if (test.ends) {
            QIQI_TEST_CHECK(composition->last_end_cookie_ == (test.asynchronous ? TfEditCookie{109} : TfEditCookie{1}));
        }
        QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), activity) && coordinator.TrackedCount() == 0 &&
                        context->range_->set_text_calls_ == 0);
    }
    for (const bool missing_context : {false, true}) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionCleanupCoordinator coordinator(43);
        const auto starting = coordinator.TrackStarting(context.Get());
        starting->finished = true;
        bool queried_range = false;
        composition->get_range_callback_ = [&] { queried_range = true; };
        ComPtr<ITfContext> target = context;
        if (missing_context) target.Reset();
        coordinator.Schedule(target, composition, missing_context ? TfClientId{7} : TF_CLIENTID_NULL, {9, 17}, activity);
        QIQI_TEST_CHECK(!queried_range && context->request_edit_session_calls_ == 0 &&
                        context->get_property_calls_ == 0 && composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(coordinator.TrackedCount() == 1 && starting->finished && !starting->terminated);
        QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), activity) && coordinator.TrackedCount() == 0);
        composition->get_range_callback_ = {};
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto unrelated = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionCleanupCoordinator coordinator(47);
        const auto starting = coordinator.TrackStarting(context.Get());
        QIQI_TEST_CHECK(starting.use_count() == 1 && starting->id == 0 && starting->context.Get() == context.Get() &&
                        !starting->composition && !starting->finished && !starting->terminated);
        QIQI_TEST_CHECK(coordinator.HasPending(context.Get(), activity) && !coordinator.HasPending(unrelated.Get(), activity));
        QIQI_TEST_CHECK(!coordinator.HandleTermination(111, nullptr, activity));
        starting->composition = composition;
        QIQI_TEST_CHECK(coordinator.HandleTermination(111, composition.Get(), activity));
        QIQI_TEST_CHECK(starting->finished && starting->terminated && !starting->attribute_clear_started &&
                        context->get_property_calls_ == 0 && composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), activity) && coordinator.TrackedCount() == 0);
        const auto finished = coordinator.TrackStarting(context.Get());
        const auto pending = coordinator.TrackStarting(context.Get());
        auto other = coordinator.TrackStarting(unrelated.Get());
        finished->finished = true;
        coordinator.MarkContextRemoved(context.Get(), activity);
        QIQI_TEST_CHECK(finished->context_removed && finished->finished && pending->context_removed && pending->finished);
        QIQI_TEST_CHECK(!other->context_removed && !other->finished && other.use_count() == 1);
        QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), activity) && coordinator.HasPending(unrelated.Get(), activity));
        const std::weak_ptr<CompositionCleanup> released = other;
        other.reset();
        QIQI_TEST_CHECK(released.expired() && !coordinator.HasPending(unrelated.Get(), activity) &&
                        coordinator.TrackedCount() == 0);
    }
    {
        auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        context->allow_sync_edit_sessions_ = false;
        std::shared_ptr<CompositionCleanup> starting;
        {
            CompositionCleanupCoordinator coordinator(53);
            starting = coordinator.TrackStarting(context.Get());
            starting->finished = true;
            coordinator.Schedule(context, composition, 7, {9, 17}, activity);
            QIQI_TEST_CHECK(coordinator.TrackedCount() == 1 && coordinator.HasPending(context.Get(), activity));
        }
        QIQI_TEST_CHECK(starting->finished && !starting->context_removed && !starting->terminated);
        starting.reset();
        QIQI_TEST_CHECK(queue->RunNext(113) == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 113 &&
                        composition->end_composition_calls_ == 1 && composition->last_end_cookie_ == 113);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionCleanupTests() {
    TestCompositionCleanupRegistry();
    TestCompositionCleanupExecution();
    TestCompositionCleanupRequests();
    TestCompositionCleanupTermination();
    TestCompositionCleanupCoordinator();
}

} // namespace qiqi::test
