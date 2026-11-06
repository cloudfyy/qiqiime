#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/finish.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace qiqi::test {
namespace {

enum class Event { Range, Check, Property, Clear, Text, Clone, Collapse, Selection, End, Interface, Insert };

constexpr std::array kCommitEvents{
    Event::Range, Event::Check, Event::Check, Event::Property, Event::Check, Event::Clear,
    Event::Check, Event::Text, Event::Check, Event::Clone, Event::Check, Event::Collapse,
    Event::Check, Event::Selection, Event::Check, Event::End,
};
constexpr std::array kCancelEvents{Event::Range, Event::Check, Event::Text, Event::Check, Event::End};

void TestFinishStateProtocol() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class State { Empty, EmptyStale, Current, Stale, RangeFailure, NullRange, ReplaceRange, ReplaceText, ReplaceEnd, EndFailure };
    constexpr std::array states{
        State::Empty, State::EmptyStale, State::Current, State::Stale, State::RangeFailure,
        State::NullRange, State::ReplaceRange, State::ReplaceText, State::ReplaceEnd, State::EndFailure,
    };
    const std::array inputs{
        std::wstring{}, std::wstring{L"ab"}, std::wstring{L'a', L'\0', L'\u4e2d', L'\xd840', L'\xdc00'},
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto kind : {CompositionFinishKind::Commit, CompositionFinishKind::Cancel}) {
        for (const auto& text : inputs) {
            for (const auto scenario : states) {
                const bool commit = kind == CompositionFinishKind::Commit;
                const bool empty = scenario == State::Empty || scenario == State::EmptyStale;
                const bool stale = scenario == State::Stale || scenario == State::EmptyStale;
                const bool got_range = scenario != State::RangeFailure && scenario != State::NullRange;
                const auto context = MakeComObject<MockTfContext>();
                context->range_->text_ = L"before";
                const auto composition = empty ? ComPtr<MockTfComposition>{} :
                    MakeComObject<MockTfComposition>(context->range_);
                CompositionLifecycleState state(443);
                state.SetActive(!stale);
                state.SetContext(context);
                state.SetComposition(composition);
                const EditSessionTicket ticket{state.Epoch(), context.Get()};
                bool has_caret = true;
                std::vector<Event> events;
                const auto replace = [&] {
                    const auto range = MakeComObject<MockTfRange>();
                    range->text_ = L"replacement";
                    state.SetComposition(MakeComObject<MockTfComposition>(range));
                    has_caret = true;
                };
                if (composition) {
                    composition->fail_get_range_ = scenario == State::RangeFailure;
                    composition->return_null_range_ = scenario == State::NullRange;
                    composition->end_result_ = scenario == State::EndFailure ? E_ACCESSDENIED : S_OK;
                    composition->get_range_observer_ = [&](ITfRange** output) {
                        events.push_back(Event::Range);
                        QIQI_TEST_CHECK(output && !*output && state.Composition().Get() == composition.Get() &&
                                        composition->AddRef() == 4 && composition->Release() == 3);
                        if (scenario == State::ReplaceRange) replace();
                    };
                    composition->end_callback_ = [&](TfEditCookie ec) {
                        events.push_back(Event::End);
                        QIQI_TEST_CHECK(ec == 449 && !state.Composition() && !has_caret &&
                                        composition->AddRef() == 3 && composition->Release() == 2);
                        const ULONG range_references = got_range ? 3UL : 2UL;
                        QIQI_TEST_CHECK(context->range_->AddRef() == range_references + 1 &&
                                        context->range_->Release() == range_references);
                        if (scenario == State::ReplaceEnd) replace();
                    };
                }
                context->query_interface_observer_ = [&](REFIID iid) {
                    QIQI_TEST_CHECK(IsEqualIID(iid, IID_ITfInsertAtSelection));
                    events.push_back(Event::Interface);
                };
                context->insert_observer_ = [&](TfEditCookie ec, DWORD flags, const WCHAR* input, LONG length, ITfRange** output) {
                    events.push_back(Event::Insert);
                    QIQI_TEST_CHECK(ec == 449 && flags == 0 && input == text.data() &&
                                    length == gsl::narrow<LONG>(text.size()) && output && !*output &&
                                    !state.Composition() && has_caret);
                };
                context->get_property_observer_ = [&](REFGUID guid, ITfProperty** output) {
                    events.push_back(Event::Property);
                    QIQI_TEST_CHECK(IsEqualGUID(guid, GUID_PROP_ATTRIBUTE) && output && !*output);
                };
                context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                    QIQI_TEST_CHECK(ec == 449);
                    events.push_back(Event::Clear);
                };
                context->range_->set_text_callback_ = [&] {
                    events.push_back(Event::Text);
                    if (scenario == State::ReplaceText) replace();
                };
                context->range_->clone_callback_ = [&] { events.push_back(Event::Clone); };
                context->range_->collapse_observer_ = [&](TfEditCookie ec, TfAnchor anchor) {
                    QIQI_TEST_CHECK(ec == 449 && anchor == TF_ANCHOR_END);
                    events.push_back(Event::Collapse);
                };
                context->selection_observer_ = [&](TfEditCookie ec, ULONG count, const TF_SELECTION* selection) {
                    QIQI_TEST_CHECK(ec == 449 && count == 1 && selection && selection->range);
                    events.push_back(Event::Selection);
                };
                unsigned int checks_seen = 0;
                FinishOrInsertCompositionInSession(
                    449, gsl::not_null<ITfContext*>(context.Get()), state, has_caret, kind, text,
                    [&, checks = std::make_unique<unsigned int>(0)](const ITfComposition* captured) {
                        checks_seen = ++*checks;
                        events.push_back(Event::Check);
                        QIQI_TEST_CHECK(captured == composition.Get());
                        return state.IsCurrent(ticket) && state.Composition().Get() == captured;
                    });
                std::vector<Event> expected;
                if (empty) {
                    if (commit && !text.empty()) {
                        expected = {Event::Interface, Event::Check};
                        if (!stale) expected.push_back(Event::Insert);
                    }
                } else if (stale || scenario == State::ReplaceRange) {
                    expected = {Event::Range, Event::Check};
                } else if (!got_range) {
                    expected = {Event::Range, Event::Check, Event::End};
                } else if (scenario == State::ReplaceText) {
                    if (commit) {
                        expected.assign(kCommitEvents.begin(), kCommitEvents.begin() + 9);
                        expected.push_back(Event::Check);
                    } else {
                        expected.assign(kCancelEvents.begin(), kCancelEvents.begin() + 4);
                    }
                } else if (commit) {
                    expected.assign(kCommitEvents.begin(), kCommitEvents.end());
                } else {
                    expected.assign(kCancelEvents.begin(), kCancelEvents.end());
                }
                const auto count = [&](Event event) {
                    return gsl::narrow<unsigned int>(std::ranges::count(expected, event));
                };
                const bool ended = count(Event::End) != 0;
                const bool replaced = scenario == State::ReplaceRange || scenario == State::ReplaceText || scenario == State::ReplaceEnd;
                QIQI_TEST_CHECK(events == expected && checks_seen == count(Event::Check) &&
                                context->range_->set_text_calls_ == count(Event::Text) &&
                                context->insert_at_selection_calls_ == count(Event::Insert) &&
                                context->get_property_calls_ == count(Event::Property) &&
                                context->prop_->clear_calls_ == count(Event::Clear) &&
                                context->set_selection_calls_ == count(Event::Selection) &&
                                has_caret == (replaced || (!ended && !(empty && !commit))) &&
                                state.Epoch().activation == ticket.epoch.activation && state.Epoch().context == ticket.epoch.context);
                const auto expected_text = count(Event::Insert) ? text :
                    count(Event::Text) ? (commit ? text : std::wstring{}) : std::wstring{L"before"};
                QIQI_TEST_CHECK(context->range_->text_ == expected_text);
                if (replaced) {
                    QIQI_TEST_CHECK(state.Composition() && state.Composition().Get() != composition.Get());
                } else {
                    QIQI_TEST_CHECK(state.Composition().Get() == (ended ? nullptr : composition.Get()));
                }
                if (composition) {
                    const ULONG references = ended || replaced ? 1UL : 2UL;
                    QIQI_TEST_CHECK(composition->get_range_calls_ == 1 &&
                                    composition->end_composition_calls_ == count(Event::End) &&
                                    composition->ended_ == (ended && scenario != State::EndFailure) &&
                                    composition->AddRef() == references + 1 && composition->Release() == references);
                }
                QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                                context->prop_->set_calls_ == 0 && !state.Cleanups().HasPending(context.Get(), GUID_NULL));
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 60 && g_com_object_count.load() == objects_before);
}

void TestFinishStateExceptionOwnership() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { Range, Check, Text, End };
    const auto objects_before = g_com_object_count.load();
    for (const auto kind : {CompositionFinishKind::Commit, CompositionFinishKind::Cancel}) {
        for (const auto boundary : {Boundary::Range, Boundary::Check, Boundary::Text, Boundary::End}) {
            const auto context = MakeComObject<MockTfContext>();
            const auto composition = MakeComObject<MockTfComposition>(context->range_);
            CompositionLifecycleState state(457);
            state.SetActive(true);
            state.SetContext(context);
            state.SetComposition(composition);
            const EditSessionTicket ticket{state.Epoch(), context.Get()};
            bool has_caret = true;
            composition->get_range_observer_ = [&](ITfRange**) {
                if (boundary == Boundary::Range) throw std::runtime_error("injected finish failure");
            };
            context->range_->set_text_callback_ = [&] {
                if (boundary == Boundary::Text) throw std::runtime_error("injected finish failure");
            };
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 461 && !state.Composition() && !has_caret &&
                                composition->AddRef() == 3 && composition->Release() == 2);
                if (boundary == Boundary::End) throw std::runtime_error("injected finish failure");
            };
            bool caught = false;
            try {
                FinishOrInsertCompositionInSession(
                    461, gsl::not_null<ITfContext*>(context.Get()), state, has_caret, kind, L"ab",
                    [&](const ITfComposition* captured) {
                        QIQI_TEST_CHECK(captured == composition.Get());
                        if (boundary == Boundary::Check) throw std::runtime_error("injected finish failure");
                        return state.IsCurrent(ticket) && state.Composition().Get() == captured;
                    });
            } catch (const std::runtime_error&) {
                caught = true;
            }
            const bool detached = boundary == Boundary::End;
            const ULONG references = detached ? 1UL : 2UL;
            QIQI_TEST_CHECK(caught && state.IsCurrent(ticket) &&
                            state.Composition().Get() == (detached ? nullptr : composition.Get()) &&
                            has_caret == !detached && !composition->ended_ &&
                            composition->AddRef() == references + 1 && composition->Release() == references &&
                            context->range_->AddRef() == 3 && context->range_->Release() == 2 &&
                            context->AddRef() == 3 && context->Release() == 2 &&
                            context->request_edit_session_calls_ == 0);
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionFinishStateTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestFinishStateProtocol();
    TestFinishStateExceptionOwnership();
}

} // namespace qiqi::test
