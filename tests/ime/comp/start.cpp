// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/comp/cleanup.h"
#include "ime/comp/start.h"
#include "ime/comp/tests.h"
#include "ime/tsf/edit_session.h"
#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace qiqi::test {

namespace {

class StartCompositionSink final : public ComObjectBase<StartCompositionSink, ITfCompositionSink> {
public:
    explicit StartCompositionSink(CompositionCleanupCoordinator& cleanups) noexcept : cleanups_(cleanups) {}
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override {
        ++notifications_;
        return cleanups_.HandleTermination(ec, composition, GUID_NULL) ? S_OK : E_UNEXPECTED;
    }
    unsigned int notifications_ = 0;
private:
    CompositionCleanupCoordinator& cleanups_;
};

enum class BeginStep { None, Insertion, Check, Range, Composition, Start, CleanupRange };
constexpr std::array kBeginTrace{
    BeginStep::Insertion, BeginStep::Check, BeginStep::Range, BeginStep::Check,
    BeginStep::Composition, BeginStep::Check, BeginStep::Start, BeginStep::Check,
};

std::vector<BeginStep> BeginTracePrefix(size_t count, bool cleanup = false) {
    const auto prefix = std::span(kBeginTrace).first(count);
    std::vector<BeginStep> result(prefix.begin(), prefix.end());
    if (cleanup) result.push_back(BeginStep::CleanupRange);
    return result;
}

void TestCompositionStartPreparation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { None, InsertionInterface, Range, CompositionInterface };
    enum class Event { InsertionInterface, Check, Range, CompositionInterface };
    struct Case {
        Boundary invalidation = Boundary::None;
        bool initially_current = true;
        bool fail_insertion = false;
        bool fail_range = false;
        bool fail_composition = false;
        bool null_insertion = false;
        bool null_range = false;
        bool null_composition = false;
        bool ready = true;
        bool insertion = true;
        bool range = true;
        bool composition = true;
        unsigned int inserts = 1;
        unsigned int composition_queries = 1;
        unsigned int checks = 3;
    };
    const std::array cases{
        Case{},
        Case{.invalidation = Boundary::InsertionInterface, .ready = false, .range = false, .composition = false,
             .inserts = 0, .composition_queries = 0, .checks = 1},
        Case{.invalidation = Boundary::Range, .ready = false, .composition = false, .composition_queries = 0, .checks = 2},
        Case{.invalidation = Boundary::CompositionInterface, .ready = false},
        Case{.initially_current = false, .ready = false, .range = false, .composition = false,
             .inserts = 0, .composition_queries = 0, .checks = 1},
        Case{.fail_insertion = true, .ready = false, .insertion = false, .range = false, .composition = false,
             .inserts = 0, .composition_queries = 0, .checks = 0},
        Case{.fail_range = true, .ready = false, .range = false, .composition = false,
             .composition_queries = 0, .checks = 1},
        Case{.fail_composition = true, .ready = false, .composition = false, .checks = 2},
        Case{.null_insertion = true, .ready = false, .insertion = false, .range = false, .composition = false,
             .inserts = 0, .composition_queries = 0, .checks = 0},
        Case{.null_range = true, .ready = false, .range = false, .composition = false,
             .composition_queries = 0, .checks = 1},
        Case{.null_composition = true, .ready = false, .composition = false, .checks = 2},
        Case{.invalidation = Boundary::CompositionInterface, .fail_composition = true,
             .ready = false, .composition = false, .checks = 2},
    };
    for (const auto& test : cases) {
        const auto context = MakeComObject<MockTfContext>();
        context->range_->text_ = L"before";
        context->fail_qi_insert_ = test.fail_insertion;
        context->fail_insert_at_sel_ = test.fail_range;
        context->fail_qi_comp_ = test.fail_composition;
        context->return_null_insert_ = test.null_insertion;
        context->return_null_insert_range_ = test.null_range;
        context->return_null_composition_context_ = test.null_composition;
        bool current = test.initially_current;
        unsigned int checks = 0;
        std::vector<Event> events;
        context->query_interface_observer_ = [&](REFIID iid) {
            if (IsEqualIID(iid, IID_ITfInsertAtSelection)) {
                events.push_back(Event::InsertionInterface);
                if (test.invalidation == Boundary::InsertionInterface) current = false;
            } else {
                QIQI_TEST_CHECK(IsEqualIID(iid, IID_ITfContextComposition));
                events.push_back(Event::CompositionInterface);
                if (test.invalidation == Boundary::CompositionInterface) current = false;
            }
        };
        context->insert_observer_ = [&](TfEditCookie ec, DWORD flags, const WCHAR* text, LONG length, ITfRange** output) {
            events.push_back(Event::Range);
            QIQI_TEST_CHECK(ec == 139 && flags == DWORD{TF_IAS_QUERYONLY} && !text && length == 0 && output);
            if (test.invalidation == Boundary::Range) current = false;
        };
        {
            const auto prepared = PrepareCompositionStart(139, context.Get(), [&] {
                events.push_back(Event::Check);
                ++checks;
                return current;
            });
            QIQI_TEST_CHECK(prepared.ready == test.ready && (prepared.insertion != nullptr) == test.insertion &&
                            (prepared.range != nullptr) == test.range &&
                            (prepared.composition_context != nullptr) == test.composition);
            if (test.insertion) QIQI_TEST_CHECK(prepared.insertion.Get() == static_cast<ITfInsertAtSelection*>(context.Get()));
            if (test.range) QIQI_TEST_CHECK(prepared.range.Get() == context->range_.Get());
            if (test.composition) {
                QIQI_TEST_CHECK(prepared.composition_context.Get() == static_cast<ITfContextComposition*>(context.Get()));
            }
            const ULONG held_interfaces = (test.insertion ? 1UL : 0UL) + (test.composition ? 1UL : 0UL);
            QIQI_TEST_CHECK(context->AddRef() == 2UL + held_interfaces);
            QIQI_TEST_CHECK(context->Release() == 1UL + held_interfaces);
            QIQI_TEST_CHECK(context->range_->AddRef() == (test.range ? 3UL : 2UL));
            QIQI_TEST_CHECK(context->range_->Release() == (test.range ? 2UL : 1UL));
        }
        QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1);
        QIQI_TEST_CHECK(context->range_->AddRef() == 2 && context->range_->Release() == 1);
        QIQI_TEST_CHECK(context->query_insert_calls_ == 1 && context->insert_at_selection_calls_ == test.inserts &&
                        context->query_composition_calls_ == test.composition_queries && checks == test.checks);
        std::vector<Event> expected{Event::InsertionInterface};
        if (test.checks > 0) expected.push_back(Event::Check);
        if (test.inserts) expected.push_back(Event::Range);
        if (test.checks > 1) expected.push_back(Event::Check);
        if (test.composition_queries) expected.push_back(Event::CompositionInterface);
        if (test.checks > 2) expected.push_back(Event::Check);
        QIQI_TEST_CHECK(events == expected);
        QIQI_TEST_CHECK(context->range_->text_ == L"before" && context->range_->set_text_calls_ == 0 &&
                        context->range_->clone_calls_ == 0 && context->set_selection_calls_ == 0 &&
                        context->start_composition_calls_ == 0 && context->request_edit_session_calls_ == 0 &&
                        context->get_property_calls_ == 0);
        context->query_interface_observer_ = {};
        context->insert_observer_ = {};
    }
}

void TestCompositionStartResolution() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Case {
        bool current = true;
        bool removed = false;
        bool terminated = false;
        bool composition = true;
    };
    const std::array cases{
        Case{},
        Case{.composition = false},
        Case{.terminated = true},
        Case{.terminated = true, .composition = false},
        Case{.removed = true},
        Case{.removed = true, .composition = false},
        Case{.removed = true, .terminated = true},
        Case{.removed = true, .terminated = true, .composition = false},
        Case{.current = false},
        Case{.current = false, .composition = false},
        Case{.current = false, .terminated = true},
        Case{.current = false, .terminated = true, .composition = false},
        Case{.current = false, .removed = true},
        Case{.current = false, .removed = true, .composition = false},
        Case{.current = false, .removed = true, .terminated = true},
        Case{.current = false, .removed = true, .terminated = true, .composition = false},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        for (const HRESULT start_hr : {S_OK, S_FALSE, E_ACCESSDENIED}) {
            for (const bool asynchronous : {false, true}) {
                const auto queue = std::make_shared<MockEditSessionQueue>();
                const auto context = MakeComObject<MockTfContext>(queue);
                const auto composition = MakeComObject<MockTfComposition>(context->range_);
                CompositionCleanupCoordinator coordinator(71);
                const auto starting = coordinator.TrackStarting(context.Get());
                if (test.composition) starting->composition = composition;
                starting->finished = true;
                starting->context_removed = test.removed;
                starting->terminated = test.terminated;
                context->allow_sync_edit_sessions_ = !asynchronous;
                context->range_->text_ = L"preedit";
                const bool scheduled = !test.current && !test.removed && !test.terminated && test.composition;
                const bool published = test.current && !test.removed && !test.terminated &&
                    test.composition && SUCCEEDED(start_hr);
                const auto expected = test.terminated ? CompositionStartDisposition::Terminated :
                    published ? CompositionStartDisposition::Publish : CompositionStartDisposition::Discard;
                bool queried_range = false;
                composition->get_range_callback_ = [&] {
                    queried_range = true;
                    QIQI_TEST_CHECK(!starting->composition && starting->finished && starting->id == 0 &&
                                    coordinator.HasPending(context.Get(), GUID_NULL));
                };
                const auto disposition = coordinator.ResolveStarting(
                    starting, start_hr, test.current, 17, {3, 5}, {7, 11}, GUID_NULL);
                composition->get_range_callback_ = {};
                QIQI_TEST_CHECK(disposition == expected && queried_range == scheduled);
                QIQI_TEST_CHECK(starting->finished && starting->context_removed == test.removed &&
                                starting->terminated == test.terminated && starting->id == 0);
                QIQI_TEST_CHECK(starting->composition.Get() ==
                                (test.composition && !scheduled ? composition.Get() : nullptr));
                QIQI_TEST_CHECK(context->request_edit_session_calls_ == (scheduled ? (asynchronous ? 2u : 1u) : 0u));
                QIQI_TEST_CHECK(coordinator.HasPending(context.Get(), GUID_NULL) == (scheduled && asynchronous));
                QIQI_TEST_CHECK(queue->PendingCount() == (scheduled && asynchronous ? 1u : 0u));
                if (scheduled && asynchronous) {
                    const auto& request = queue->PendingRequests().front();
                    QIQI_TEST_CHECK(request.client_id == 17 && request.context.Get() == context.Get() &&
                                    request.flags == DWORD{TF_ES_ASYNC | TF_ES_READWRITE});
                    QIQI_TEST_CHECK(context->prop_->clear_calls_ == 0 && composition->end_composition_calls_ == 0);
                    QIQI_TEST_CHECK(queue->RunNext(149) == S_OK);
                }
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == (scheduled ? 1u : 0u) &&
                                composition->end_composition_calls_ == (scheduled ? 1u : 0u));
                if (scheduled) {
                    QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == (asynchronous ? TfEditCookie{149} : TfEditCookie{1}) &&
                                    composition->last_end_cookie_ == context->prop_->last_clear_cookie_);
                }
                QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), GUID_NULL) && coordinator.TrackedCount() == 0);
                QIQI_TEST_CHECK(context->range_->text_ == L"preedit" && context->range_->set_text_calls_ == 0 &&
                                context->set_selection_calls_ == 0 && context->start_composition_calls_ == 0);
            }
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionStartResolutionReentry() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Event { Prune, TerminateStarting, TerminateCleanup, RemoveContext };
    const auto objects_before = g_com_object_count.load();
    for (const auto event : {Event::Prune, Event::TerminateStarting, Event::TerminateCleanup, Event::RemoveContext}) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionCleanupCoordinator coordinator(73);
        auto starting = coordinator.TrackStarting(context.Get());
        starting->composition = composition;
        starting->finished = true;
        const std::weak_ptr<CompositionCleanup> lifetime = starting;
        bool reentered = false;
        composition->get_range_callback_ = [&] {
            reentered = true;
            const auto held = lifetime.lock();
            QIQI_TEST_CHECK(held && held->finished && !held->composition &&
                            coordinator.HasPending(context.Get(), GUID_NULL));
            if (event == Event::TerminateStarting) {
                held->terminated = true;
            } else if (event == Event::TerminateCleanup) {
                QIQI_TEST_CHECK(coordinator.HandleTermination(151, composition.Get(), GUID_NULL));
            } else if (event == Event::RemoveContext) {
                coordinator.MarkContextRemoved(context.Get(), GUID_NULL);
            }
            starting.reset();
            QIQI_TEST_CHECK(!lifetime.expired());
        };
        const auto disposition = coordinator.ResolveStarting(starting, S_OK, false, 19, {3, 5}, {7, 11}, GUID_NULL);
        composition->get_range_callback_ = {};
        QIQI_TEST_CHECK(reentered && lifetime.expired() && !starting);
        QIQI_TEST_CHECK(disposition == (event == Event::TerminateStarting ?
                        CompositionStartDisposition::Terminated : CompositionStartDisposition::Discard));
        const bool executed = event == Event::Prune || event == Event::TerminateStarting;
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == (executed ? 1u : 0u) &&
                        context->prop_->clear_calls_ == (executed ? 1u : 0u) &&
                        composition->end_composition_calls_ == (executed ? 1u : 0u));
        QIQI_TEST_CHECK(!coordinator.HasPending(context.Get(), GUID_NULL) && coordinator.TrackedCount() == 0);
        QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1);
    }
    for (const bool execute : {false, true}) {
        const auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        context->allow_sync_edit_sessions_ = false;
        {
            CompositionCleanupCoordinator coordinator(79);
            const auto starting = coordinator.TrackStarting(context.Get());
            starting->composition = composition;
            starting->finished = true;
            QIQI_TEST_CHECK(coordinator.ResolveStarting(
                starting, S_OK, false, 23, {3, 5}, {7, 11}, GUID_NULL) == CompositionStartDisposition::Discard);
            QIQI_TEST_CHECK(!starting->composition && queue->PendingCount() == 1);
        }
        if (execute) {
            QIQI_TEST_CHECK(queue->RunNext(157) == S_OK);
        } else {
            QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
        }
        QIQI_TEST_CHECK(queue->PendingCount() == 0 && composition->end_composition_calls_ == (execute ? 1u : 0u) &&
                        context->prop_->clear_calls_ == (execute ? 1u : 0u));
        QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionBeginWorkflow() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Fault { None, Insertion, Range, Composition, NullInsertion, NullRange, NullComposition };
    enum class HostEvent { None, Stale, Removed, Terminated, RemovedAndTerminated };
    struct Case {
        BeginStep invalidation = BeginStep::None;
        bool initially_current = true;
        Fault fault = Fault::None;
        HRESULT start_hr = S_OK;
        bool fail_start = false;
        bool null_start = false;
        HostEvent host_event = HostEvent::None;
        unsigned int interfaces = 2;
        bool range = true;
        bool ready = true;
        bool scheduled = false;
        CompositionStartDisposition disposition = CompositionStartDisposition::Publish;
        std::vector<BeginStep> events{kBeginTrace.begin(), kBeginTrace.end()};
    };
    using Disposition = CompositionStartDisposition;
    const std::array cases{
        Case{},
        Case{.invalidation = BeginStep::Insertion, .interfaces = 1, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(2)},
        Case{.invalidation = BeginStep::Range, .interfaces = 1, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(4)},
        Case{.invalidation = BeginStep::Composition, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(6)},
        Case{.initially_current = false, .interfaces = 1, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(2)},
        Case{.fault = Fault::Insertion, .interfaces = 0, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(1)},
        Case{.fault = Fault::Range, .interfaces = 1, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(3)},
        Case{.fault = Fault::Composition, .interfaces = 1, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(5)},
        Case{.fault = Fault::NullInsertion, .interfaces = 0, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(1)},
        Case{.fault = Fault::NullRange, .interfaces = 1, .range = false, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(3)},
        Case{.fault = Fault::NullComposition, .interfaces = 1, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(5)},
        Case{.invalidation = BeginStep::Composition, .fault = Fault::Composition, .interfaces = 1, .ready = false,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(5)},
        Case{.fail_start = true, .disposition = Disposition::Discard},
        Case{.null_start = true, .disposition = Disposition::Discard},
        Case{.start_hr = S_FALSE},
        Case{.start_hr = E_ACCESSDENIED, .disposition = Disposition::Discard},
        Case{.start_hr = E_ACCESSDENIED, .null_start = true, .disposition = Disposition::Discard},
        Case{.host_event = HostEvent::Stale, .scheduled = true,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(8, true)},
        Case{.start_hr = E_ACCESSDENIED, .host_event = HostEvent::Stale, .scheduled = true,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(8, true)},
        Case{.null_start = true, .host_event = HostEvent::Stale, .disposition = Disposition::Discard},
        Case{.host_event = HostEvent::Removed, .disposition = Disposition::Discard},
        Case{.host_event = HostEvent::Terminated, .disposition = Disposition::Terminated},
        Case{.host_event = HostEvent::RemovedAndTerminated, .disposition = Disposition::Terminated},
        Case{.invalidation = BeginStep::Start, .scheduled = true,
             .disposition = Disposition::Discard, .events = BeginTracePrefix(8, true)},
        Case{.invalidation = BeginStep::Start, .fail_start = true, .disposition = Disposition::Discard},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        for (const bool asynchronous : {false, true}) {
            CompositionCleanupCoordinator cleanups(89);
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            const auto sink = MakeComObject<StartCompositionSink>(cleanups);
            context->range_->text_ = L"preedit";
            context->allow_sync_edit_sessions_ = !asynchronous;
            context->fail_qi_insert_ = test.fault == Fault::Insertion;
            context->fail_insert_at_sel_ = test.fault == Fault::Range;
            context->fail_qi_comp_ = test.fault == Fault::Composition;
            context->return_null_insert_ = test.fault == Fault::NullInsertion;
            context->return_null_insert_range_ = test.fault == Fault::NullRange;
            context->return_null_composition_context_ = test.fault == Fault::NullComposition;
            context->fail_start_comp_ = test.fail_start;
            context->return_null_started_composition_ = test.null_start;
            context->start_composition_result_ = test.start_hr;
            bool current = test.initially_current;
            unsigned int checks = 0;
            std::vector<BeginStep> events;
            EditSessionEpoch current_epoch{3, 5};
            const auto observe = [&](BeginStep step) {
                events.push_back(step);
                if (test.invalidation == step) current = false;
            };
            context->query_interface_observer_ = [&](REFIID iid) {
                QIQI_TEST_CHECK(IsEqualIID(iid, IID_ITfInsertAtSelection) || IsEqualIID(iid, IID_ITfContextComposition));
                observe(IsEqualIID(iid, IID_ITfInsertAtSelection) ? BeginStep::Insertion : BeginStep::Composition);
            };
            context->insert_observer_ = [&](TfEditCookie ec, DWORD flags, const WCHAR* text, LONG length, ITfRange** output) {
                QIQI_TEST_CHECK(ec == 193 && flags == DWORD{TF_IAS_QUERYONLY} && !text && length == 0 && output && !*output);
                observe(BeginStep::Range);
            };
            context->start_composition_observer_ = [&](TfEditCookie ec, ITfRange* range, ITfCompositionSink* target,
                                                       ITfComposition** output) {
                QIQI_TEST_CHECK(ec == 193 && range == context->range_.Get() && target == sink.Get() && output && !*output);
                QIQI_TEST_CHECK(cleanups.TrackedCount() == 1 && cleanups.HasPending(context.Get(), GUID_NULL));
                QIQI_TEST_CHECK(context->AddRef() == 5 && context->Release() == 4);
                QIQI_TEST_CHECK(context->range_->AddRef() == 3 && context->range_->Release() == 2);
                observe(BeginStep::Start);
            };
            context->start_composition_callback_ = [&] {
                if (context->last_composition_) {
                    context->last_composition_->get_range_callback_ = [&] { observe(BeginStep::CleanupRange); };
                }
                if (test.host_event == HostEvent::Stale) {
                    current = false;
                    ++current_epoch.activation;
                }
                if (test.host_event == HostEvent::Removed || test.host_event == HostEvent::RemovedAndTerminated) {
                    cleanups.MarkContextRemoved(context.Get(), GUID_NULL);
                }
                if (test.host_event == HostEvent::Terminated || test.host_event == HostEvent::RemovedAndTerminated) {
                    QIQI_TEST_CHECK(context->last_composition_ &&
                                    context->last_composition_->NotifyHostTermination(197) == S_OK);
                }
            };
            const bool created = test.ready && !test.fail_start && !test.null_start;
            ComPtr<ITfComposition> published;
            {
                const auto attempt = BeginCompositionInSession(
                    193, context.Get(), sink.Get(), cleanups, 29, {3, 5}, current_epoch, GUID_NULL,
                    [&] {
                        observe(BeginStep::Check);
                        ++checks;
                        if (checks == 4) {
                            QIQI_TEST_CHECK(cleanups.TrackedCount() == 1 && !cleanups.HasPending(context.Get(), GUID_NULL));
                        } else {
                            QIQI_TEST_CHECK(cleanups.TrackedCount() == 0);
                        }
                        return current;
                    });
                QIQI_TEST_CHECK(events == test.events && attempt.preparation.ready == test.ready &&
                                (attempt.starting != nullptr) == test.ready && attempt.disposition == test.disposition);
                QIQI_TEST_CHECK(context->start_composition_calls_ == (test.ready ? 1u : 0u));
                QIQI_TEST_CHECK(queue->PendingCount() == (test.scheduled && asynchronous ? 1u : 0u));
                if (test.scheduled && asynchronous) {
                    const auto& request = queue->PendingRequests().front();
                    QIQI_TEST_CHECK(request.client_id == 29 && request.context.Get() == context.Get() &&
                                    request.flags == DWORD{TF_ES_ASYNC | TF_ES_READWRITE});
                    QIQI_TEST_CHECK(queue->RunNext(199) == S_OK);
                }
                QIQI_TEST_CHECK(context->request_edit_session_calls_ ==
                                (test.scheduled ? (asynchronous ? 2u : 1u) : 0u));
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == (test.scheduled ? 1u : 0u));
                if (created) {
                    QIQI_TEST_CHECK(context->last_composition_->end_composition_calls_ == (test.scheduled ? 1u : 0u));
                }
                const ULONG context_probe = 2UL + test.interfaces + (test.ready ? 1UL : 0UL);
                QIQI_TEST_CHECK(context->AddRef() == context_probe && context->Release() == context_probe - 1);
                const ULONG range_probe = 2UL + (test.range ? 1UL : 0UL) + (created ? 1UL : 0UL);
                QIQI_TEST_CHECK(context->range_->AddRef() == range_probe && context->range_->Release() == range_probe - 1);
                if (attempt.starting) {
                    QIQI_TEST_CHECK(attempt.starting->finished && attempt.starting->id == 0 &&
                                    attempt.starting->context.Get() == context.Get());
                    QIQI_TEST_CHECK(attempt.starting->context_removed ==
                                    (test.host_event == HostEvent::Removed || test.host_event == HostEvent::RemovedAndTerminated));
                    QIQI_TEST_CHECK(attempt.starting->terminated ==
                                    (test.host_event == HostEvent::Terminated || test.host_event == HostEvent::RemovedAndTerminated));
                    QIQI_TEST_CHECK(attempt.starting->composition.Get() ==
                                    (created && !test.scheduled ? context->last_composition_.Get() : nullptr));
                }
                if (attempt.disposition == Disposition::Publish) {
                    published = std::move(attempt.starting->composition);
                    QIQI_TEST_CHECK(!attempt.starting->composition && published.Get() == context->last_composition_.Get());
                }
            }
            QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1);
            const ULONG range_probe = 2UL + (created ? 1UL : 0UL);
            QIQI_TEST_CHECK(context->range_->AddRef() == range_probe && context->range_->Release() == range_probe - 1);
            QIQI_TEST_CHECK(!cleanups.HasPending(context.Get(), GUID_NULL) && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(checks == gsl::narrow_cast<unsigned int>(std::ranges::count(test.events, BeginStep::Check)));
            QIQI_TEST_CHECK(context->range_->text_ == L"preedit" && context->range_->set_text_calls_ == 0 &&
                            context->range_->clone_calls_ == 0 && context->set_selection_calls_ == 0 &&
                            context->prop_->set_calls_ == 0);
            QIQI_TEST_CHECK(sink->notifications_ ==
                            (test.host_event == HostEvent::Terminated || test.host_event == HostEvent::RemovedAndTerminated ? 1u : 0u));
            context->query_interface_observer_ = {};
            context->insert_observer_ = {};
            context->start_composition_observer_ = {};
            context->start_composition_callback_ = {};
            if (context->last_composition_) context->last_composition_->get_range_callback_ = {};
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionBeginPredicate() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    {
        CompositionCleanupCoordinator cleanups(97);
        const auto context = MakeComObject<MockTfContext>();
        const auto sink = MakeComObject<StartCompositionSink>(cleanups);
        unsigned int observed = 0;
        const auto attempt = BeginCompositionInSession(
            211, context.Get(), sink.Get(), cleanups, 31, {3, 5}, {3, 5}, GUID_NULL,
            [calls = std::make_unique<unsigned int>(0), &observed]() mutable noexcept {
                observed = ++*calls;
                return true;
            });
        QIQI_TEST_CHECK(observed == 4 && attempt.disposition == CompositionStartDisposition::Publish &&
                        attempt.starting && attempt.starting->finished && attempt.starting->composition);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionStartTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCompositionStartPreparation();
    TestCompositionStartResolution();
    TestCompositionStartResolutionReentry();
    TestCompositionBeginWorkflow();
    TestCompositionBeginPredicate();
    RunTsfCompositionStartUpdateTests();
}

} // namespace qiqi::test
