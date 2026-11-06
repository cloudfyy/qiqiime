#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/layout.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace qiqi::test {
namespace {

void TestLayoutQueryBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Scenario {
        Normal, ViewFailure, ViewNull, ViewSFalse, ViewFailedOutput,
        RangeFailure, RangeNull, RangeSFalse, RangeFailedOutput,
        ExtNoLayout, ExtFailureOutput, ExtSFalse, Clipped, EmptyRect, InvertedRect,
        StaleView, StaleRange, StaleExtent, ReplacedRange, ReplacedExtent,
    };
    enum class Step { View, Check, Range, Extent };
    constexpr std::array scenarios{
        Scenario::Normal, Scenario::ViewFailure, Scenario::ViewNull, Scenario::ViewSFalse,
        Scenario::ViewFailedOutput, Scenario::RangeFailure, Scenario::RangeNull,
        Scenario::RangeSFalse, Scenario::RangeFailedOutput, Scenario::ExtNoLayout,
        Scenario::ExtFailureOutput, Scenario::ExtSFalse, Scenario::Clipped, Scenario::EmptyRect,
        Scenario::InvertedRect, Scenario::StaleView, Scenario::StaleRange, Scenario::StaleExtent,
        Scenario::ReplacedRange, Scenario::ReplacedExtent,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto scenario : scenarios) {
            const bool view_scenario = scenario == Scenario::ViewFailure || scenario == Scenario::ViewNull ||
                scenario == Scenario::ViewSFalse || scenario == Scenario::ViewFailedOutput || scenario == Scenario::StaleView;
            if (notified && view_scenario) continue;
            const auto context = MakeComObject<MockTfContext>();
            const auto composition = MakeComObject<MockTfComposition>(context->range_);
            const auto selected_view = notified ? MakeComObject<MockTfContextView>() : context->view_;
            selected_view->text_ext_ = notified ? RECT{300, 310, 340, 334} : RECT{100, 110, 140, 134};
            ComPtr<ITfContextView> view;
            if (notified) view = selected_view;
            CompositionLifecycleState state(191);
            state.SetActive(true);
            state.SetContext(context);
            state.SetComposition(composition);
            const EditSessionTicket ticket{state.Epoch(), context.Get()};
            context->fail_get_view_ = scenario == Scenario::ViewFailure;
            context->return_null_active_view_ = scenario == Scenario::ViewNull;
            context->active_view_result_ = scenario == Scenario::ViewSFalse ? S_FALSE :
                scenario == Scenario::ViewFailedOutput ? E_ACCESSDENIED : S_OK;
            composition->fail_get_range_ = scenario == Scenario::RangeFailure;
            composition->return_null_range_ = scenario == Scenario::RangeNull;
            composition->get_range_result_ = scenario == Scenario::RangeSFalse ? S_FALSE :
                scenario == Scenario::RangeFailedOutput ? E_ACCESSDENIED : S_OK;
            selected_view->fail_get_text_ext_ = scenario == Scenario::ExtNoLayout;
            selected_view->text_ext_result_ = scenario == Scenario::ExtSFalse ? S_FALSE :
                scenario == Scenario::ExtFailureOutput ? E_ACCESSDENIED : S_OK;
            selected_view->text_clipped_ = scenario == Scenario::Clipped ? TRUE : FALSE;
            if (scenario == Scenario::EmptyRect) selected_view->text_ext_ = {};
            if (scenario == Scenario::InvertedRect) selected_view->text_ext_ = {300, 310, 280, 290};
            std::vector<Step> trace;
            const auto replace_composition = [&] {
                const auto replacement_range = MakeComObject<MockTfRange>();
                state.SetComposition(MakeComObject<MockTfComposition>(replacement_range));
            };
            context->get_active_view_observer_ = [&](ITfContextView** output) {
                QIQI_TEST_CHECK(output);
                trace.push_back(Step::View);
                if (scenario == Scenario::StaleView) state.InvalidateContext();
            };
            composition->get_range_observer_ = [&](ITfRange** output) {
                QIQI_TEST_CHECK(output);
                trace.push_back(Step::Range);
                if (scenario == Scenario::StaleRange) state.InvalidateContext();
                if (scenario == Scenario::ReplacedRange) replace_composition();
            };
            selected_view->get_text_ext_observer_ = [&](TfEditCookie ec, ITfRange* range, RECT* rect, BOOL* clipped) {
                QIQI_TEST_CHECK(ec == 181 && range == context->range_.Get() && rect && clipped);
                trace.push_back(Step::Extent);
                if (scenario == Scenario::StaleExtent) state.InvalidateContext();
                if (scenario == Scenario::ReplacedExtent) replace_composition();
            };
            unsigned int checks_seen = 0;
            const auto query = QueryCompositionLayoutInSession(
                181, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfComposition*>(composition.Get()),
                view, false, ticket.epoch, 191, GUID_NULL,
                [&, checks = std::make_unique<unsigned int>(0)] {
                    checks_seen = ++*checks;
                    trace.push_back(Step::Check);
                    return state.IsCurrent(ticket) && state.Composition().Get() == composition.Get();
                });
            const bool view_output = notified || (scenario != Scenario::ViewFailure && scenario != Scenario::ViewNull);
            const bool view_ok = notified || (view_output && scenario != Scenario::ViewFailedOutput);
            const bool range_called = view_ok && scenario != Scenario::StaleView;
            const bool range_output = range_called && scenario != Scenario::RangeFailure && scenario != Scenario::RangeNull;
            const bool range_ok = range_output && scenario != Scenario::RangeFailedOutput;
            const bool extent_called = range_ok && scenario != Scenario::StaleRange && scenario != Scenario::ReplacedRange;
            const bool positioned = extent_called && scenario != Scenario::ExtNoLayout && scenario != Scenario::ExtFailureOutput;
            const bool replaced = scenario == Scenario::ReplacedRange || scenario == Scenario::ReplacedExtent;
            const bool current = scenario != Scenario::StaleView && scenario != Scenario::StaleRange &&
                scenario != Scenario::StaleExtent && !replaced;
            std::vector<Step> expected;
            if (!notified) expected.push_back(Step::View);
            if (view_ok) expected.push_back(Step::Check);
            if (range_called) expected.push_back(Step::Range);
            if (range_ok) expected.push_back(Step::Check);
            if (extent_called) expected.push_back(Step::Extent);
            expected.push_back(Step::Check);
            QIQI_TEST_CHECK(trace == expected && query.current == current && query.got_position == positioned);
            QIQI_TEST_CHECK(checks_seen == (1u + (view_ok ? 1u : 0u) + (range_ok ? 1u : 0u)) &&
                            query.range.Get() == (range_output ? context->range_.Get() : nullptr) &&
                            view.Get() == (view_output ? selected_view.Get() : nullptr));
            const bool extent_output = extent_called && scenario != Scenario::ExtNoLayout;
            const RECT expected_rect = extent_output ? selected_view->text_ext_ : RECT{};
            QIQI_TEST_CHECK(query.rect.left == expected_rect.left && query.rect.top == expected_rect.top &&
                            query.rect.right == expected_rect.right && query.rect.bottom == expected_rect.bottom &&
                            query.clipped == (extent_output ? selected_view->text_clipped_ : FALSE));
            QIQI_TEST_CHECK(context->get_active_view_calls_ == (notified ? 0u : 1u) &&
                            composition->get_range_calls_ == (range_called ? 1u : 0u) &&
                            selected_view->get_text_ext_calls_ == (extent_called ? 1u : 0u));
            const ULONG range_refs = range_output ? 3UL : 2UL;
            const ULONG composition_refs = replaced ? 1UL : 2UL;
            QIQI_TEST_CHECK(context->range_->AddRef() == range_refs + 1 &&
                            context->range_->Release() == range_refs &&
                            composition->AddRef() == composition_refs + 1 &&
                            composition->Release() == composition_refs);
            if (replaced) {
                QIQI_TEST_CHECK(state.Epoch().activation == ticket.epoch.activation &&
                                state.Epoch().context == ticket.epoch.context &&
                                state.Composition().Get() != composition.Get());
            }
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->set_selection_calls_ == 0 &&
                            context->range_->set_text_calls_ == 0 && !composition->ended_);
            if (notified) QIQI_TEST_CHECK(context->view_->get_text_ext_calls_ == 0);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 35 && g_com_object_count.load() == objects_before);
}

void TestLayoutQueryResourceLifetime() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Kind { Current, StaleRange, NoLayout };
    enum class Released { Composition, Range, View };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto kind : {Kind::Current, Kind::StaleRange, Kind::NoLayout}) {
            std::array<Released, 3> released{};
            size_t count = 0;
            auto context = MakeComObject<MockTfContext>();
            auto original = MakeComObject<MockTfComposition>(context->range_);
            const gsl::not_null<MockTfRange*> range(context->range_.Get());
            bool current = true;
            original->release_observer_ = [&]() noexcept {
                QIQI_TEST_CHECK(count == 0 && range->AddRef() == 2 && range->Release() == 1);
                gsl::at(released, count++) = Released::Composition;
            };
            range->release_observer_ = [&]() noexcept {
                QIQI_TEST_CHECK(count == 1);
                gsl::at(released, count++) = Released::Range;
            };
            context->view_->release_observer_ = [&]() noexcept {
                QIQI_TEST_CHECK(count == 2);
                gsl::at(released, count++) = Released::View;
            };
            original->get_range_callback_ = [&] {
                if (kind == Kind::StaleRange) current = false;
            };
            context->view_->fail_get_text_ext_ = kind == Kind::NoLayout;
            ComPtr<ITfContextView> view;
            if (notified) view = context->view_;
            {
                const auto composition = original;
                const auto query = QueryCompositionLayoutInSession(
                    193, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfComposition*>(composition.Get()),
                    view, false, {}, 197, GUID_NULL, [&]() noexcept { return current; });
                original.Reset();
                context.Reset();
                QIQI_TEST_CHECK(query.range.Get() == range.get() && query.current == current &&
                                query.got_position == (kind == Kind::Current) && count == 0);
                QIQI_TEST_CHECK(range->AddRef() == 3 && range->Release() == 2 &&
                                view->AddRef() == 2 && view->Release() == 1);
            }
            QIQI_TEST_CHECK(count == 2);
            view.Reset();
            QIQI_TEST_CHECK(count == 3 && (released == std::array{
                Released::Composition, Released::Range, Released::View}));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 6 && g_com_object_count.load() == objects_before);
}

void TestLayoutTaskSnapshot() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    static_assert(std::is_copy_constructible_v<CompositionLayoutTask>);
    static_assert(std::is_nothrow_move_constructible_v<CompositionLayoutTask>);
    static_assert(!std::is_copy_assignable_v<CompositionLayoutTask>);
    static_assert(!std::is_move_assignable_v<CompositionLayoutTask>);
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const bool move : {false, true}) {
            auto context = MakeComObject<MockTfContext>();
            ComPtr<ITfContextView> view;
            if (notified) view = MakeComObject<MockTfContextView>();
            const auto* const expected_context = context.Get();
            const auto* const expected_view = view.Get();
            EditSessionTicket ticket{{211, 223}, context.Get()};
            GUID activity = log::CreateActivityId();
            const GUID expected_activity = activity;
            unsigned int releases = 0;
            context->release_observer_ = [&]() noexcept {
                QIQI_TEST_CHECK(releases == (notified ? 1u : 0u));
                ++releases;
            };
            if (notified) {
                auto* const notification_view = dynamic_cast<MockTfContextView*>(view.Get());
                QIQI_TEST_CHECK(notification_view);
                notification_view->release_observer_ = [&]() noexcept {
                    QIQI_TEST_CHECK(releases == 0);
                    ++releases;
                };
            }
            {
                CompositionLayoutTask original(context, view, ticket, activity);
                std::optional<CompositionLayoutTask> retained;
                if (move) retained.emplace(std::move(original));
                else retained.emplace(original);
                context.Reset();
                view.Reset();
                ticket = {};
                activity = GUID_NULL;
                QIQI_TEST_CHECK(retained->Context().Get() == expected_context &&
                                retained->View().Get() == expected_view &&
                                retained->Ticket().Matches({211, 223}, expected_context) &&
                                ::IsEqualGUID(retained->Activity(), expected_activity) && releases == 0);
                const ULONG references = move ? 1UL : 2UL;
                QIQI_TEST_CHECK(retained->Context()->AddRef() == references + 1 &&
                                retained->Context()->Release() == references);
                if (notified) {
                    QIQI_TEST_CHECK(retained->View()->AddRef() == references + 1 &&
                                    retained->View()->Release() == references);
                }
            }
            QIQI_TEST_CHECK(releases == (notified ? 2u : 1u));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 4 && g_com_object_count.load() == objects_before);
}

void TestLayoutCallbackScope() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Kind {
        Current, StaleActivation, StaleContext, WrongTarget, Inactive, Resetting,
        Replacement, ChangedActivity, Nested, ThrowCheck, ThrowUpdate,
    };
    constexpr std::array kinds{
        Kind::Current, Kind::StaleActivation, Kind::StaleContext, Kind::WrongTarget,
        Kind::Inactive, Kind::Resetting, Kind::Replacement, Kind::ChangedActivity,
        Kind::Nested, Kind::ThrowCheck, Kind::ThrowUpdate,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto kind : kinds) {
            const auto context = MakeComObject<MockTfContext>();
            const auto other_context = MakeComObject<MockTfContext>();
            ComPtr<ITfContextView> view;
            if (notified) view = MakeComObject<MockTfContextView>();
            CompositionLifecycleState state(227);
            state.SetActive(true);
            state.SetContext(context);
            state.SetComposition(MakeComObject<MockTfComposition>(context->range_));
            const auto old_composition = state.Composition();
            const EditSessionTicket ticket{state.Epoch(), context.Get()};
            const GUID layout_activity = log::CreateActivityId();
            const GUID previous_activity = log::CreateActivityId();
            const GUID changed_activity = log::CreateActivityId();
            GUID activity = previous_activity;
            const CompositionLayoutTask task(context, view, ticket, layout_activity);
            std::optional<CompositionContextResetScope> reset;
            if (kind == Kind::StaleActivation) state.InvalidateActivation();
            if (kind == Kind::StaleContext) state.InvalidateContext();
            if (kind == Kind::WrongTarget) state.SetContext(other_context);
            if (kind == Kind::Inactive) state.SetActive(false);
            if (kind == Kind::Resetting) reset.emplace(state);
            if (kind == Kind::Replacement) {
                state.SetComposition(MakeComObject<MockTfComposition>(context->range_));
            }
            unsigned int checks_seen = 0;
            unsigned int updates_seen = 0;
            bool caught = false;
            try {
                const HRESULT hr = ExecuteCompositionLayoutInSession(
                    229, task, activity, 227,
                    [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket expected) {
                        checks_seen = ++*checks;
                        QIQI_TEST_CHECK(::IsEqualGUID(activity, layout_activity) &&
                                        expected.Matches(ticket.epoch, context.Get()));
                        if (kind == Kind::ThrowCheck) throw std::runtime_error("layout check");
                        if (kind == Kind::ChangedActivity) activity = changed_activity;
                        return state.IsCurrent(expected);
                    },
                    [&, updates = std::make_unique<unsigned int>(0)](
                        TfEditCookie ec, gsl::not_null<ITfContext*> target, const ComPtr<ITfContextView>& supplied_view) {
                        updates_seen = ++*updates;
                        QIQI_TEST_CHECK(ec == 229 && target.get() == context.Get() && supplied_view.Get() == view.Get() &&
                                        ::IsEqualGUID(activity, kind == Kind::ChangedActivity ? changed_activity : layout_activity));
                        if (kind == Kind::Replacement) {
                            QIQI_TEST_CHECK(state.Composition().Get() != old_composition.Get() &&
                                            state.Epoch().activation == ticket.epoch.activation &&
                                            state.Epoch().context == ticket.epoch.context);
                        }
                        if (kind == Kind::Nested) {
                            const GUID nested_activity = log::CreateActivityId();
                            const CompositionLayoutTask nested(context, view, ticket, nested_activity);
                            unsigned int nested_updates = 0;
                            QIQI_TEST_CHECK(ExecuteCompositionLayoutInSession(
                                233, nested, activity, 227,
                                [&](EditSessionTicket expected) noexcept {
                                    QIQI_TEST_CHECK(::IsEqualGUID(activity, nested_activity));
                                    return state.IsCurrent(expected);
                                },
                                [&](TfEditCookie cookie, gsl::not_null<ITfContext*> nested_target,
                                    const ComPtr<ITfContextView>& nested_view) {
                                    QIQI_TEST_CHECK(cookie == 233 && nested_target.get() == context.Get() &&
                                                    nested_view.Get() == view.Get() &&
                                                    ::IsEqualGUID(activity, nested_activity));
                                    ++nested_updates;
                                }) == S_OK);
                            QIQI_TEST_CHECK(nested_updates == 1 && ::IsEqualGUID(activity, layout_activity));
                        }
                        activity = changed_activity;
                        if (kind == Kind::ThrowUpdate) throw std::runtime_error("layout update");
                    });
                QIQI_TEST_CHECK(hr == S_OK);
            } catch (const std::runtime_error& error) {
                caught = true;
                QIQI_TEST_CHECK(std::string_view(error.what()) ==
                                (kind == Kind::ThrowCheck ? "layout check" : "layout update"));
            }
            const bool skipped = kind == Kind::StaleActivation || kind == Kind::StaleContext ||
                kind == Kind::WrongTarget || kind == Kind::Inactive || kind == Kind::Resetting || kind == Kind::ThrowCheck;
            QIQI_TEST_CHECK(checks_seen == 1 && updates_seen == (skipped ? 0u : 1u) &&
                            caught == (kind == Kind::ThrowCheck || kind == Kind::ThrowUpdate) &&
                            ::IsEqualGUID(activity, previous_activity) &&
                            context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 22 && g_com_object_count.load() == objects_before);
}

void TestLayoutSessionRequests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Kind {
        Immediate, RequestSFalse, CallbackSFalse, CallbackFailure, RequestFailure,
        Deferred, DeferredFailure, ReadOnly, ExpiredQueue, Abandon,
    };
    constexpr std::array kinds{
        Kind::Immediate, Kind::RequestSFalse, Kind::CallbackSFalse, Kind::CallbackFailure,
        Kind::RequestFailure, Kind::Deferred, Kind::DeferredFailure, Kind::ReadOnly,
        Kind::ExpiredQueue, Kind::Abandon,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto kind : kinds) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            const bool queued = kind == Kind::Deferred || kind == Kind::DeferredFailure ||
                kind == Kind::ExpiredQueue || kind == Kind::Abandon;
            auto context = queued ? MakeComObject<MockTfContext>(queue) : MakeComObject<MockTfContext>();
            auto view = notified ? MakeComObject<MockTfContextView>() : ComPtr<MockTfContextView>{};
            const auto* const target = context.Get();
            const auto* const notification_view = view.Get();
            const EditSessionTicket ticket{{239, 241}, target};
            const GUID layout_activity = log::CreateActivityId();
            const GUID previous_activity = log::CreateActivityId();
            GUID activity = previous_activity;
            context->edit_session_request_result_ = kind == Kind::RequestSFalse ? S_FALSE :
                kind == Kind::RequestFailure ? E_ACCESSDENIED : S_OK;
            context->async_edit_session_result_ = kind == Kind::DeferredFailure ? E_ACCESSDENIED : S_OK;
            context->read_only_ = kind == Kind::ReadOnly;
            const HRESULT callback_hr = kind == Kind::CallbackSFalse ? S_FALSE :
                kind == Kind::CallbackFailure ? E_ACCESSDENIED : S_OK;
            bool request_returned = false;
            unsigned int callbacks = 0;
            unsigned int updates = 0;
            auto session = MakeComObject<LambdaEditSession>(
                [&, task = CompositionLayoutTask(context, view, ticket, layout_activity)](TfEditCookie ec) {
                    ++callbacks;
                    QIQI_TEST_CHECK(request_returned == queued && ec == (queued ? 251u : 1u));
                    QIQI_TEST_CHECK(ExecuteCompositionLayoutInSession(
                        ec, task, activity, 243,
                        [&](EditSessionTicket expected) noexcept {
                            return expected.Matches({239, 241}, target);
                        },
                        [&](TfEditCookie cookie, gsl::not_null<ITfContext*> captured_context,
                            const ComPtr<ITfContextView>& captured_view) {
                            QIQI_TEST_CHECK(cookie == ec && captured_context.get() == target &&
                                            captured_view.Get() == notification_view &&
                                            ::IsEqualGUID(activity, layout_activity));
                            ++updates;
                        }) == S_OK);
                    return callback_hr;
                });
            if (kind == Kind::ExpiredQueue) queue.reset();
            RequestCompositionLayoutSession(
                gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfEditSession*>(session.Get()),
                247, ticket, 243, layout_activity);
            request_returned = true;
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 1 &&
                            context->last_edit_session_flags_ == (TF_ES_ASYNCDONTCARE | TF_ES_READ) &&
                            context->range_->set_text_calls_ == 0 && ::IsEqualGUID(activity, previous_activity));
            const bool pending = kind == Kind::Deferred || kind == Kind::Abandon;
            if (queue) {
                QIQI_TEST_CHECK(queue->PendingCount() == (pending ? 1u : 0u));
                if (pending) {
                    const auto& request = queue->PendingRequests().front();
                    QIQI_TEST_CHECK(request.context.Get() == target && request.session.Get() == session.Get() &&
                                    request.client_id == 247 && request.flags == (TF_ES_ASYNCDONTCARE | TF_ES_READ));
                }
            }
            session.Reset();
            context.Reset();
            view.Reset();
            if (kind == Kind::Deferred) QIQI_TEST_CHECK(queue->RunNext(251) == callback_hr);
            if (kind == Kind::Abandon) QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
            const bool ran = kind != Kind::RequestFailure && kind != Kind::DeferredFailure &&
                kind != Kind::ExpiredQueue && kind != Kind::Abandon;
            QIQI_TEST_CHECK(callbacks == (ran ? 1u : 0u) && updates == callbacks &&
                            ::IsEqualGUID(activity, previous_activity));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 20 && g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionLayoutTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestLayoutQueryBoundaries();
    TestLayoutQueryResourceLifetime();
    TestLayoutTaskSnapshot();
    TestLayoutCallbackScope();
    TestLayoutSessionRequests();
    RunTsfCompositionLayoutEntryTests();
    RunTsfCompositionLayoutUpdateTests();
    RunTsfCompositionLayoutPositionTests();
}

} // namespace qiqi::test
