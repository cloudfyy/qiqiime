#include "common/test_support.h"
#include "ime/comp/finish.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace qiqi::test {
namespace {

enum class Event { Range, Check, Property, Clear, Reset };

void TestTerminationCompletion() {
    enum class Outcome {
        Normal, Inactive, RangeFailure, NullRange, PropertyFailure, ClearFailure,
        ReplaceRange, ReplaceProperty, ReplaceClear, StaleRange, NullNotification, MissingContext,
    };
    constexpr std::array outcomes{
        Outcome::Normal, Outcome::Inactive, Outcome::RangeFailure, Outcome::NullRange,
        Outcome::PropertyFailure, Outcome::ClearFailure, Outcome::ReplaceRange,
        Outcome::ReplaceProperty, Outcome::ReplaceClear, Outcome::StaleRange,
        Outcome::NullNotification, Outcome::MissingContext,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool empty_text : {false, true}) {
        for (const auto outcome : outcomes) {
            const auto context = MakeComObject<MockTfContext>();
            context->range_->text_ = empty_text ? L"" : L"preedit";
            const auto composition = MakeComObject<MockTfComposition>(context->range_);
            const auto replacement = MakeComObject<MockTfComposition>(MakeComObject<MockTfRange>());
            CompositionLifecycleState state(541);
            state.SetActive(outcome != Outcome::Inactive);
            if (outcome != Outcome::MissingContext) state.SetContext(context);
            state.SetComposition(composition);
            const EditSessionTicket original{state.Epoch(), state.Context().Get()};
            composition->fail_get_range_ = outcome == Outcome::RangeFailure;
            composition->return_null_range_ = outcome == Outcome::NullRange;
            context->get_property_result_ = outcome == Outcome::PropertyFailure ? E_ACCESSDENIED : S_OK;
            context->prop_->clear_result_ = outcome == Outcome::ClearFailure ? E_ACCESSDENIED : S_OK;
            const bool queried = outcome != Outcome::NullNotification && outcome != Outcome::MissingContext;
            const bool got_range = queried && outcome != Outcome::RangeFailure && outcome != Outcome::NullRange;
            std::vector<Event> events;
            composition->get_range_observer_ = [&](ITfRange** output) {
                events.push_back(Event::Range);
                QIQI_TEST_CHECK(output && !*output && !state.Composition() &&
                                state.Context().Get() == context.Get() &&
                                composition->AddRef() == 3 && composition->Release() == 2 &&
                                context->AddRef() == 4 && context->Release() == 3);
                if (outcome == Outcome::ReplaceRange) state.SetComposition(replacement);
                if (outcome == Outcome::StaleRange) state.InvalidateContext();
            };
            context->get_property_observer_ = [&](REFGUID guid, ITfProperty** output) {
                events.push_back(Event::Property);
                QIQI_TEST_CHECK(IsEqualGUID(guid, GUID_PROP_ATTRIBUTE) && output && !*output);
                if (outcome == Outcome::ReplaceProperty) state.SetComposition(replacement);
            };
            context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                events.push_back(Event::Clear);
                QIQI_TEST_CHECK(ec == 547);
                if (outcome == Outcome::ReplaceClear) state.SetComposition(replacement);
            };
            unsigned int checks_seen = 0;
            unsigned int resets_seen = 0;
            QIQI_TEST_CHECK(CompleteCurrentCompositionTermination(
                547, outcome == Outcome::NullNotification ? nullptr : composition.Get(), state,
                [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                    checks_seen = ++*checks;
                    events.push_back(Event::Check);
                    const ULONG references = got_range ? 3UL : 2UL;
                    QIQI_TEST_CHECK(ticket.target == original.target && ticket.epoch.context == original.epoch.context &&
                                    ticket.epoch.activation == original.epoch.activation &&
                                    context->range_->AddRef() == references + 1 &&
                                    context->range_->Release() == references);
                    return state.IsCurrent(ticket) && !state.Composition();
                },
                [&, resets = std::make_unique<unsigned int>(0)] {
                    resets_seen = ++*resets;
                    events.push_back(Event::Reset);
                    // Branch-local cleanup owners must be gone before the actual reset.
                    QIQI_TEST_CHECK(context->AddRef() == (outcome == Outcome::MissingContext ? 2UL : 3UL) &&
                                    context->Release() == (outcome == Outcome::MissingContext ? 1UL : 2UL) &&
                                    composition->AddRef() == (queried ? 2UL : 3UL) &&
                                    composition->Release() == (queried ? 1UL : 2UL) &&
                                    context->range_->AddRef() == 3 && context->range_->Release() == 2);
                    state.InvalidateContext();
                    state.ClearContext();
                    state.ClearComposition();
                }) == S_OK);
            std::vector<Event> expected;
            if (!queried) {
                expected = {Event::Reset};
            } else if (!got_range) {
                expected = {Event::Range, Event::Check, Event::Reset};
            } else if (outcome == Outcome::Inactive || outcome == Outcome::ReplaceRange || outcome == Outcome::StaleRange) {
                expected = {Event::Range, Event::Check, Event::Check};
            } else if (outcome == Outcome::PropertyFailure) {
                expected = {Event::Range, Event::Check, Event::Property, Event::Check, Event::Reset};
            } else if (outcome == Outcome::ReplaceProperty) {
                expected = {Event::Range, Event::Check, Event::Property, Event::Check, Event::Check};
            } else {
                expected = {Event::Range, Event::Check, Event::Property, Event::Check, Event::Clear, Event::Check};
                if (outcome != Outcome::ReplaceClear) expected.push_back(Event::Reset);
            }
            unsigned int expected_checks = 0;
            unsigned int expected_resets = 0;
            unsigned int expected_clears = 0;
            for (const auto event : expected) {
                if (event == Event::Check) ++expected_checks;
                if (event == Event::Reset) ++expected_resets;
                if (event == Event::Clear) ++expected_clears;
            }
            const bool replaced = outcome == Outcome::ReplaceRange || outcome == Outcome::ReplaceProperty || outcome == Outcome::ReplaceClear;
            QIQI_TEST_CHECK(events == expected && checks_seen == expected_checks && resets_seen == expected_resets &&
                            state.Composition().Get() == (replaced ? replacement.Get() : nullptr) &&
                            context->prop_->clear_calls_ == expected_clears &&
                            context->range_->text_ == (empty_text ? L"" : L"preedit") &&
                            context->range_->set_text_calls_ == 0 && composition->end_composition_calls_ == 0 &&
                            context->request_edit_session_calls_ == 0 && !replacement->ended_ &&
                            !state.Cleanups().HasPending(context.Get(), GUID_NULL));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 24 && g_com_object_count.load() == objects_before);
}

void TestTerminationCompletionExceptions() {
    enum class Boundary { Range, Check, Reset };
    const auto objects_before = g_com_object_count.load();
    for (const auto boundary : {Boundary::Range, Boundary::Check, Boundary::Reset}) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionLifecycleState state(557);
        state.SetActive(true);
        state.SetContext(context);
        state.SetComposition(composition);
        composition->get_range_observer_ = [&](ITfRange**) {
            if (boundary == Boundary::Range) throw std::runtime_error("injected termination failure");
        };
        bool caught = false;
        try {
            static_cast<void>(CompleteCurrentCompositionTermination(
                563, composition.Get(), state,
                [&](EditSessionTicket ticket) {
                    if (boundary == Boundary::Check) throw std::runtime_error("injected termination failure");
                    return state.IsCurrent(ticket) && !state.Composition();
                },
                [&] {
                    QIQI_TEST_CHECK(boundary == Boundary::Reset && composition->AddRef() == 2 &&
                                    composition->Release() == 1 && context->AddRef() == 3 && context->Release() == 2);
                    if (boundary == Boundary::Reset) throw std::runtime_error("injected termination failure");
                }));
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught && !state.Composition() && state.Context().Get() == context.Get() &&
                        state.Epoch().context == 0 && composition->AddRef() == 2 && composition->Release() == 1 &&
                        context->AddRef() == 3 && context->Release() == 2 &&
                        context->range_->AddRef() == 3 && context->range_->Release() == 2 &&
                        composition->end_composition_calls_ == 0 && context->request_edit_session_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionTerminationTests() {
    TestTerminationCompletion();
    TestTerminationCompletionExceptions();
}

} // namespace qiqi::test
