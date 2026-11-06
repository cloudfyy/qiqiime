#include "common/test_support.h"
#include "ime/comp/layout.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <vector>

namespace qiqi::test {
namespace {

void TestLayoutQueryBoundaries() {
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

} // namespace

void RunTsfCompositionLayoutTests() {
    TestLayoutQueryBoundaries();
    TestLayoutQueryResourceLifetime();
}

} // namespace qiqi::test
