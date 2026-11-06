#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/layout.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include "ime/ui/candidate_ui.h"
#include <memory>
#include <optional>
#include <stdexcept>

namespace qiqi::test {
namespace {

void TestLayoutUpdateBranches() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Outcome {
        Normal, NoLayout, FallbackFailure, RangeFailure, NullRange,
        Inactive, Resetting, ReplaceRange, MoveReplacement, NoView,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool ui_present : {false, true}) {
        for (const bool composing : {false, true}) {
            for (const bool had_position : {false, true}) {
                for (const auto outcome : {
                         Outcome::Normal, Outcome::NoLayout, Outcome::FallbackFailure, Outcome::RangeFailure, Outcome::NullRange,
                         Outcome::Inactive, Outcome::Resetting, Outcome::ReplaceRange, Outcome::MoveReplacement, Outcome::NoView}) {
                    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
                    const auto context = MakeComObject<MockTfContext>();
                    const auto composition = MakeComObject<MockTfComposition>(context->range_);
                    const auto replacement = MakeComObject<MockTfComposition>(context->range_);
                    CompositionLifecycleState state(701);
                    state.SetActive(outcome != Outcome::Inactive);
                    state.SetContext(context);
                    if (composing) state.SetComposition(composition);
                    std::optional<CompositionContextResetScope> reset;
                    if (outcome == Outcome::Resetting) reset.emplace(state);
                    ComPtr<ITfContextView> view;
                    if (outcome != Outcome::NoView) view = context->view_;
                    composition->fail_get_range_ = outcome == Outcome::RangeFailure;
                    composition->return_null_range_ = outcome == Outcome::NullRange;
                    composition->get_range_callback_ = [&] {
                        if (outcome == Outcome::ReplaceRange) state.SetComposition(replacement);
                    };
                    context->view_->fail_get_text_ext_ = outcome == Outcome::NoLayout || outcome == Outcome::FallbackFailure;
                    bool has_position = had_position;
                    unsigned int checks_seen = 0;
                    unsigned int fallbacks_seen = 0;
                    unsigned int moves_seen = 0;
                    UpdateCompositionLayoutPositionInSession(
                        709, gsl::not_null<ITfContext*>(context.Get()), state, ui_present ? ui.Get() : nullptr,
                        view, has_position, 701, GUID_NULL,
                        [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket, const ITfComposition* expected) {
                            checks_seen = ++*checks;
                            QIQI_TEST_CHECK(ticket.Matches(state.Epoch(), context.Get()) && expected == composition.Get());
                            return state.IsCurrent(ticket) && state.Composition().Get() == expected;
                        },
                        [&, calls = std::make_unique<unsigned int>(0)](RECT& output) {
                            fallbacks_seen = ++*calls;
                            QIQI_TEST_CHECK(!has_position);
                            output = RECT{500, 510, 502, 530};
                            return outcome != Outcome::FallbackFailure;
                        },
                        [&, calls = std::make_unique<unsigned int>(0)](const RECT& output) {
                            moves_seen = ++*calls;
                            const bool fallback = outcome == Outcome::NoLayout || outcome == Outcome::RangeFailure ||
                                outcome == Outcome::NullRange;
                            QIQI_TEST_CHECK(output.left == (fallback ? 500 : context->view_->text_ext_.left) &&
                                            has_position == !fallback);
                            if (outcome == Outcome::MoveReplacement) {
                                state.SetComposition(replacement);
                                has_position = false;
                            }
                        });
                    const bool entered = ui_present && composing;
                    const bool stale_initial = outcome == Outcome::Inactive || outcome == Outcome::Resetting;
                    const bool stale = stale_initial || outcome == Outcome::ReplaceRange;
                    const bool range_failed = outcome == Outcome::RangeFailure || outcome == Outcome::NullRange;
                    const bool query_failed = range_failed || outcome == Outcome::NoLayout || outcome == Outcome::FallbackFailure;
                    const bool fallback = entered && !stale && query_failed && !had_position;
                    const bool moved = entered && !stale && (!query_failed || (fallback && outcome != Outcome::FallbackFailure));
                    const bool replaced = entered && (outcome == Outcome::MoveReplacement || outcome == Outcome::ReplaceRange);
                    QIQI_TEST_CHECK(checks_seen == (entered ? (stale_initial || range_failed ? 2u : 3u) : 0u) &&
                                    fallbacks_seen == (fallback ? 1u : 0u) && moves_seen == (moved ? 1u : 0u) &&
                                    has_position == (entered && outcome == Outcome::MoveReplacement ? false :
                                                     had_position || (entered && !stale && !query_failed)) &&
                                    state.Composition().Get() == (replaced ? replacement.Get() : composing ? composition.Get() : nullptr) &&
                                    composition->get_range_calls_ == (entered && !stale_initial ? 1u : 0u) &&
                                    context->get_active_view_calls_ == (entered && outcome == Outcome::NoView ? 1u : 0u) &&
                                    (view != nullptr) == (outcome != Outcome::NoView || entered) &&
                                    context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0 &&
                                    composition->end_composition_calls_ == 0 && replacement->end_composition_calls_ == 0);
                    ++cases;
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 80 && g_com_object_count.load() == objects_before);
}

void TestLayoutUpdateOwners() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    for (const int boundary : {0, 1, 2}) {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        const auto context = MakeComObject<MockTfContext>();
        auto composition = MakeComObject<MockTfComposition>(context->range_);
        bool released = false;
        bool range_released = false;
        composition->release_observer_ = [&]() noexcept { released = true; };
        context->range_->release_observer_ = [&]() noexcept { range_released = true; };
        CompositionLifecycleState state(719);
        state.SetActive(true);
        state.SetContext(context);
        state.SetComposition(composition);
        ComPtr<ITfContextView> view = context->view_;
        context->view_->fail_get_text_ext_ = boundary == 1;
        bool has_position = false;
        bool caught = false;
        unsigned int checks = 0;
        unsigned int callbacks = 0;
        try {
            UpdateCompositionLayoutPositionInSession(
                727, gsl::not_null<ITfContext*>(context.Get()), state, ui.Get(), view, has_position, 719, GUID_NULL,
                [&](EditSessionTicket ticket, const ITfComposition* expected) {
                    ++checks;
                    QIQI_TEST_CHECK(ticket.Matches({0, 0}, context.Get()) && expected == composition.Get() && !released);
                    if (boundary == 0) {
                        state.ClearComposition();
                        composition.Reset();
                        throw std::runtime_error("layout update identity");
                    }
                    return true;
                },
                [&](RECT&) {
                    ++callbacks;
                    QIQI_TEST_CHECK(boundary == 1 && checks == 3 && !released && !range_released && !has_position);
                    state.ClearComposition();
                    composition.Reset();
                    context->range_.Reset();
                    QIQI_TEST_CHECK(!released && !range_released);
                    if (boundary == 1) throw std::runtime_error("layout update fallback");
                    return false;
                },
                [&](const RECT&) {
                    ++callbacks;
                    QIQI_TEST_CHECK(boundary == 2 && checks == 3 && !released && !range_released && has_position);
                    state.ClearComposition();
                    composition.Reset();
                    context->range_.Reset();
                    QIQI_TEST_CHECK(!released && !range_released);
                    if (boundary == 2) throw std::runtime_error("layout update move");
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught && released && callbacks == (boundary == 0 ? 0u : 1u) &&
                        checks == (boundary == 0 ? 1u : 3u) && range_released == (boundary != 0) &&
                        !state.Composition() && !composition && has_position == (boundary == 2) &&
                        view.Get() == context->view_.Get() && context->request_edit_session_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionLayoutUpdateTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestLayoutUpdateBranches();
    TestLayoutUpdateOwners();
}

} // namespace qiqi::test
