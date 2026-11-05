#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/comp/finish.h"
#include "ime/comp/tests.h"
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace qiqi::test {
namespace {

enum class Event { None, Range, Check, Property, Clear, Text, Clone, Collapse, Selection, Detach, End };

constexpr std::array kCommitTrace{
    Event::Range, Event::Check, Event::Check, Event::Property, Event::Check, Event::Clear, Event::Check,
    Event::Text, Event::Check, Event::Clone, Event::Check, Event::Collapse, Event::Check,
    Event::Selection, Event::Check, Event::Detach, Event::End,
};
constexpr std::array kCancelTrace{
    Event::Range, Event::Check, Event::Text, Event::Check, Event::Detach, Event::End,
};
constexpr std::array kPropertyFailureTrace{
    Event::Range, Event::Check, Event::Check, Event::Property, Event::Check, Event::Text, Event::Check,
    Event::Clone, Event::Check, Event::Collapse, Event::Check, Event::Selection, Event::Check, Event::Detach, Event::End,
};
constexpr std::array kHostTerminationTrace{
    Event::Range, Event::Check, Event::Property, Event::Check, Event::Clear, Event::Check,
};

std::vector<Event> TracePrefix(std::span<const Event> trace, size_t count, bool extra_check = false, bool end = false) {
    const auto prefix = trace.first(count);
    std::vector<Event> result(prefix.begin(), prefix.end());
    if (extra_check) result.push_back(Event::Check);
    if (end) {
        result.push_back(Event::Detach);
        result.push_back(Event::End);
    }
    return result;
}

void TestCompositionFinishing() {
    using Kind = CompositionFinishKind;
    struct Case {
        Kind kind = Kind::Commit;
        Event invalidation = Event::None;
        bool initially_current = true;
        bool fail_range = false;
        bool null_range = false;
        HRESULT property_hr = S_OK;
        bool null_property = false;
        HRESULT clear_hr = S_OK;
        HRESULT text_hr = S_OK;
        HRESULT clone_hr = S_OK;
        bool null_clone = false;
        HRESULT collapse_hr = S_OK;
        HRESULT selection_hr = S_OK;
        HRESULT end_hr = S_OK;
        std::vector<Event> events{kCommitTrace.begin(), kCommitTrace.end()};
    };
    const std::array cases{
        Case{},
        Case{.initially_current = false, .events = TracePrefix(kCommitTrace, 2)},
        Case{.invalidation = Event::Range, .events = TracePrefix(kCommitTrace, 2)},
        Case{.invalidation = Event::Property, .events = TracePrefix(kCommitTrace, 5, true)},
        Case{.invalidation = Event::Clear, .events = TracePrefix(kCommitTrace, 7)},
        Case{.invalidation = Event::Text, .events = TracePrefix(kCommitTrace, 9, true)},
        Case{.invalidation = Event::Clone, .events = TracePrefix(kCommitTrace, 11, true)},
        Case{.invalidation = Event::Collapse, .events = TracePrefix(kCommitTrace, 13, true)},
        Case{.invalidation = Event::Selection, .events = TracePrefix(kCommitTrace, 15)},
        Case{.fail_range = true, .events = TracePrefix(kCommitTrace, 2, false, true)},
        Case{.null_range = true, .events = TracePrefix(kCommitTrace, 2, false, true)},
        Case{.property_hr = E_ACCESSDENIED, .events = TracePrefix(kPropertyFailureTrace, 15)},
        Case{.null_property = true, .events = TracePrefix(kPropertyFailureTrace, 15)},
        Case{.clear_hr = E_ACCESSDENIED},
        Case{.text_hr = E_ACCESSDENIED},
        Case{.clone_hr = E_ACCESSDENIED, .events = TracePrefix(kCommitTrace, 11, false, true)},
        Case{.null_clone = true, .events = TracePrefix(kCommitTrace, 11, false, true)},
        Case{.collapse_hr = E_ACCESSDENIED},
        Case{.selection_hr = E_ACCESSDENIED},
        Case{.end_hr = E_ACCESSDENIED},
        Case{.invalidation = Event::Property, .property_hr = E_ACCESSDENIED,
             .events = TracePrefix(kPropertyFailureTrace, 5)},
        Case{.invalidation = Event::Text, .text_hr = E_ACCESSDENIED, .events = TracePrefix(kCommitTrace, 9, true)},
        Case{.invalidation = Event::Clear, .clear_hr = E_ACCESSDENIED, .events = TracePrefix(kCommitTrace, 7)},
        Case{.initially_current = false, .fail_range = true, .events = TracePrefix(kCommitTrace, 2)},
        Case{.initially_current = false, .null_range = true, .events = TracePrefix(kCommitTrace, 2)},
        Case{.kind = Kind::Cancel, .events = TracePrefix(kCancelTrace, 6)},
        Case{.kind = Kind::Cancel, .initially_current = false, .events = TracePrefix(kCancelTrace, 2)},
        Case{.kind = Kind::Cancel, .invalidation = Event::Range, .events = TracePrefix(kCancelTrace, 2)},
        Case{.kind = Kind::Cancel, .invalidation = Event::Text, .events = TracePrefix(kCancelTrace, 4)},
        Case{.kind = Kind::Cancel, .fail_range = true, .events = TracePrefix(kCancelTrace, 2, false, true)},
        Case{.kind = Kind::Cancel, .null_range = true, .events = TracePrefix(kCancelTrace, 2, false, true)},
        Case{.kind = Kind::Cancel, .text_hr = E_ACCESSDENIED, .events = TracePrefix(kCancelTrace, 6)},
        Case{.kind = Kind::Cancel, .end_hr = E_ACCESSDENIED, .events = TracePrefix(kCancelTrace, 6)},
        Case{.kind = Kind::Cancel, .initially_current = false, .fail_range = true, .events = TracePrefix(kCancelTrace, 2)},
        Case{.kind = Kind::Cancel, .invalidation = Event::Text, .text_hr = E_ACCESSDENIED,
             .events = TracePrefix(kCancelTrace, 4)},
    };
    const std::array inputs{
        std::wstring{L"abc"},
        std::wstring{},
        std::wstring{L'a', L'\0', L'\u4e2d', L'\xd840', L'\xdc00'},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& text : inputs) {
        for (const auto& test : cases) {
            const auto context = MakeComObject<MockTfContext>();
            const auto range = context->range_;
            const auto composition = MakeComObject<MockTfComposition>(range);
            ComPtr<ITfComposition> current_composition = composition;
            const auto captured_composition = current_composition;
            ComPtr<MockTfComposition> replacement;
            ComPtr<ITfRange> selected_range;
            const ULONG range_probe = range->AddRef();
            QIQI_TEST_CHECK(range->Release() == range_probe - 1);
            bool current = test.initially_current;
            bool has_caret = true;
            std::vector<Event> events;
            const auto observe = [&](Event event) {
                events.push_back(event);
                if (test.invalidation == event) current = false;
            };
            const auto count = [&](Event event) {
                return gsl::narrow_cast<unsigned int>(std::ranges::count(test.events, event));
            };
            range->text_ = L"before";
            composition->fail_get_range_ = test.fail_range;
            composition->return_null_range_ = test.null_range;
            composition->end_result_ = test.end_hr;
            context->get_property_result_ = test.property_hr;
            context->return_null_property_ = test.null_property;
            context->prop_->clear_result_ = test.clear_hr;
            range->set_text_result_ = test.text_hr;
            range->clone_result_ = test.clone_hr;
            range->return_null_clone_ = test.null_clone;
            range->collapse_result_ = test.collapse_hr;
            context->set_selection_result_ = test.selection_hr;
            composition->get_range_observer_ = [&](ITfRange** output) {
                QIQI_TEST_CHECK(output && !*output);
                observe(Event::Range);
            };
            context->get_property_observer_ = [&](REFGUID guid, ITfProperty** output) {
                QIQI_TEST_CHECK(IsEqualGUID(guid, GUID_PROP_ATTRIBUTE) && output && !*output);
                observe(Event::Property);
            };
            context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 163);
                observe(Event::Clear);
            };
            range->set_text_callback_ = [&] { observe(Event::Text); };
            range->clone_callback_ = [&] { observe(Event::Clone); };
            range->collapse_observer_ = [&](TfEditCookie ec, TfAnchor anchor) {
                QIQI_TEST_CHECK(ec == 163 && anchor == TF_ANCHOR_END);
                observe(Event::Collapse);
            };
            context->selection_observer_ = [&](TfEditCookie ec, ULONG amount, const TF_SELECTION* selection) {
                QIQI_TEST_CHECK(ec == 163 && amount == 1 && selection && selection->range &&
                                selection->range != range.Get() && selection->style.ase == TF_AE_NONE &&
                                selection->style.fInterimChar == FALSE);
                selected_range = selection->range;
                observe(Event::Selection);
            };
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 163 && !current_composition && !has_caret);
                const ULONG expected_probe = range_probe + (test.fail_range || test.null_range ? 0UL : 1UL);
                QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
                if (selected_range) {
                    QIQI_TEST_CHECK(selected_range->AddRef() == 2 && selected_range->Release() == 1);
                }
                observe(Event::End);
                replacement = MakeComObject<MockTfComposition>(range);
                current_composition = replacement;
                has_caret = true;
            };
            FinishCompositionInSession(
                163, context.Get(), captured_composition.Get(), test.kind, text,
                [&] {
                    observe(Event::Check);
                    return current && current_composition.Get() == captured_composition.Get();
                },
                [&] {
                    observe(Event::Detach);
                    current_composition.Reset();
                    has_caret = false;
                });
            QIQI_TEST_CHECK(events == test.events && composition->get_range_calls_ == 1);
            QIQI_TEST_CHECK(context->get_property_calls_ == count(Event::Property) &&
                            context->prop_->clear_calls_ == count(Event::Clear) &&
                            range->set_text_calls_ == count(Event::Text) && range->clone_calls_ == count(Event::Clone) &&
                            context->set_selection_calls_ == count(Event::Selection) &&
                            composition->end_composition_calls_ == count(Event::End));
            const auto expected_text = test.kind == Kind::Commit ? text : std::wstring{};
            QIQI_TEST_CHECK(range->text_ == (count(Event::Text) && SUCCEEDED(test.text_hr) ? expected_text : L"before"));
            if (count(Event::Text)) {
                QIQI_TEST_CHECK(range->last_set_text_cookie_ == 163 && range->last_set_text_flags_ == 0 &&
                                range->last_set_text_length_ == gsl::narrow_cast<LONG>(expected_text.size()));
            }
            QIQI_TEST_CHECK(has_caret && current_composition.Get() ==
                            (count(Event::End) ? replacement.Get() : captured_composition.Get()));
            QIQI_TEST_CHECK(composition->ended_ == (count(Event::End) && SUCCEEDED(test.end_hr)));
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                            context->insert_at_selection_calls_ == 0 && context->prop_->set_calls_ == 0);
            composition->get_range_observer_ = {};
            composition->end_callback_ = {};
            context->get_property_observer_ = {};
            context->prop_->clear_callback_ = {};
            range->set_text_callback_ = {};
            range->clone_callback_ = {};
            range->collapse_observer_ = {};
            context->selection_observer_ = {};
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestDirectCompositionCommit() {
    enum class Boundary { None, Interface, Insert };
    enum class Step { Interface, Check, Insert };
    struct Case {
        Boundary invalidation = Boundary::None;
        bool initially_current = true;
        bool fail_query = false;
        bool null_interface = false;
        bool fail_insert = false;
        bool null_range = false;
        unsigned int checks = 1;
        unsigned int inserts = 1;
    };
    const std::array cases{
        Case{},
        Case{.invalidation = Boundary::Interface, .inserts = 0},
        Case{.invalidation = Boundary::Insert},
        Case{.initially_current = false, .inserts = 0},
        Case{.fail_query = true, .checks = 0, .inserts = 0},
        Case{.null_interface = true, .checks = 0, .inserts = 0},
        Case{.fail_insert = true},
        Case{.null_range = true},
        Case{.invalidation = Boundary::Insert, .fail_insert = true},
    };
    const std::array inputs{
        std::wstring{L"abc"},
        std::wstring{},
        std::wstring{L'a', L'\0', L'\u4e2d', L'\xd840', L'\xdc00'},
    };
    for (const auto& text : inputs) {
        for (const auto& test : cases) {
            const auto context = MakeComObject<MockTfContext>();
            context->range_->text_ = L"before";
            context->fail_qi_insert_ = test.fail_query;
            context->return_null_insert_ = test.null_interface;
            context->fail_insert_at_sel_ = test.fail_insert;
            context->return_null_insert_range_ = test.null_range;
            bool current = test.initially_current;
            unsigned int checks = 0;
            std::vector<Step> events;
            context->query_interface_observer_ = [&](REFIID iid) {
                QIQI_TEST_CHECK(IsEqualIID(iid, IID_ITfInsertAtSelection));
                events.push_back(Step::Interface);
                if (test.invalidation == Boundary::Interface) current = false;
            };
            context->insert_observer_ = [&](TfEditCookie ec, DWORD flags, const WCHAR* data, LONG length, ITfRange** output) {
                QIQI_TEST_CHECK(ec == 167 && flags == 0 && data == text.data() &&
                                length == gsl::narrow_cast<LONG>(text.size()) && output && !*output);
                events.push_back(Step::Insert);
                QIQI_TEST_CHECK(context->AddRef() == 3 && context->Release() == 2);
                if (test.invalidation == Boundary::Insert) current = false;
            };
            InsertCommittedTextInSession(167, context.Get(), text, [&] {
                ++checks;
                events.push_back(Step::Check);
                return current;
            });
            const bool nonempty = !text.empty();
            const auto inserts = nonempty ? test.inserts : 0u;
            std::vector<Step> expected;
            if (nonempty) expected.push_back(Step::Interface);
            if (nonempty && test.checks) expected.push_back(Step::Check);
            if (inserts) expected.push_back(Step::Insert);
            QIQI_TEST_CHECK(events == expected && checks == (nonempty ? test.checks : 0u) &&
                            context->query_insert_calls_ == (nonempty ? 1u : 0u) &&
                            context->insert_at_selection_calls_ == inserts);
            QIQI_TEST_CHECK(context->range_->text_ == (inserts && !test.fail_insert ? text : L"before"));
            QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1);
            QIQI_TEST_CHECK(context->range_->AddRef() == 2 && context->range_->Release() == 1);
            QIQI_TEST_CHECK(context->get_property_calls_ == 0 && context->set_selection_calls_ == 0 &&
                            context->start_composition_calls_ == 0 && context->range_->clone_calls_ == 0 &&
                            context->range_->set_text_calls_ == 0 && context->request_edit_session_calls_ == 0);
            context->query_interface_observer_ = {};
            context->insert_observer_ = {};
        }
    }
}

void TestHostCompositionTermination() {
    struct Case {
        Event invalidation = Event::None;
        bool initially_current = true;
        bool fail_range = false;
        bool null_range = false;
        HRESULT property_hr = S_OK;
        bool null_property = false;
        HRESULT clear_hr = S_OK;
        bool may_reset = true;
        std::vector<Event> events{kHostTerminationTrace.begin(), kHostTerminationTrace.end()};
    };
    const std::array cases{
        Case{},
        Case{.initially_current = false, .may_reset = false, .events = {Event::Range, Event::Check, Event::Check}},
        Case{.invalidation = Event::Range, .may_reset = false, .events = {Event::Range, Event::Check, Event::Check}},
        Case{.invalidation = Event::Property, .may_reset = false,
             .events = {Event::Range, Event::Check, Event::Property, Event::Check, Event::Check}},
        Case{.invalidation = Event::Clear, .may_reset = false},
        Case{.fail_range = true, .events = {Event::Range, Event::Check}},
        Case{.null_range = true, .events = {Event::Range, Event::Check}},
        Case{.property_hr = E_ACCESSDENIED, .events = {Event::Range, Event::Check, Event::Property, Event::Check}},
        Case{.null_property = true, .events = {Event::Range, Event::Check, Event::Property, Event::Check}},
        Case{.clear_hr = E_ACCESSDENIED},
        Case{.initially_current = false, .fail_range = true, .may_reset = false, .events = {Event::Range, Event::Check}},
        Case{.initially_current = false, .null_range = true, .may_reset = false, .events = {Event::Range, Event::Check}},
        Case{.invalidation = Event::Property, .property_hr = E_ACCESSDENIED, .may_reset = false,
             .events = {Event::Range, Event::Check, Event::Property, Event::Check}},
        Case{.invalidation = Event::Clear, .clear_hr = E_ACCESSDENIED, .may_reset = false},
        Case{.invalidation = Event::Range, .fail_range = true, .may_reset = false, .events = {Event::Range, Event::Check}},
        Case{.invalidation = Event::Range, .null_range = true, .may_reset = false, .events = {Event::Range, Event::Check}},
        Case{.invalidation = Event::Property, .null_property = true, .may_reset = false,
             .events = {Event::Range, Event::Check, Event::Property, Event::Check}},
    };
    const std::array inputs{
        std::wstring{L"abc"},
        std::wstring{},
        std::wstring{L'a', L'\0', L'\u4e2d', L'\xd840', L'\xdc00'},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& text : inputs) {
        for (const auto& test : cases) {
            const auto context = MakeComObject<MockTfContext>();
            const auto context_holder = context;
            const auto range = context->range_;
            const auto composition = MakeComObject<MockTfComposition>(range);
            ComPtr<ITfComposition> current_composition = composition;
            const auto composition_holder = std::move(current_composition);
            ComPtr<MockTfComposition> replacement;
            std::vector<Event> events;
            const auto observe = [&](Event event) {
                events.push_back(event);
                if (test.invalidation == event) {
                    replacement = MakeComObject<MockTfComposition>(range);
                    current_composition = replacement;
                }
            };
            const ULONG range_probe = range->AddRef();
            QIQI_TEST_CHECK(range->Release() == range_probe - 1);
            QIQI_TEST_CHECK(composition->NotifyHostTermination(173) == S_OK);
            range->text_ = text;
            composition->fail_get_range_ = test.fail_range;
            composition->return_null_range_ = test.null_range;
            context->get_property_result_ = test.property_hr;
            context->return_null_property_ = test.null_property;
            context->prop_->clear_result_ = test.clear_hr;
            composition->get_range_observer_ = [&](ITfRange** output) {
                QIQI_TEST_CHECK(!current_composition && output && !*output);
                observe(Event::Range);
            };
            context->get_property_observer_ = [&](REFGUID guid, ITfProperty** output) {
                QIQI_TEST_CHECK(IsEqualGUID(guid, GUID_PROP_ATTRIBUTE) && output && !*output);
                observe(Event::Property);
            };
            context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 173);
                observe(Event::Clear);
            };
            const bool has_range = !test.fail_range && !test.null_range;
            {
                const auto terminated = ClearTerminatedCompositionInSession(
                    173, context_holder.Get(), composition_holder.Get(),
                    [&] {
                        observe(Event::Check);
                        const ULONG expected_probe = range_probe + (has_range ? 1UL : 0UL) + (replacement ? 1UL : 0UL);
                        QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
                        return test.initially_current && !current_composition;
                    });
                QIQI_TEST_CHECK(terminated.may_reset == test.may_reset &&
                                terminated.range.Get() == (has_range ? range.Get() : nullptr));
                const ULONG expected_probe = range_probe + (has_range ? 1UL : 0UL) + (replacement ? 1UL : 0UL);
                QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
            }
            const ULONG expected_probe = range_probe + (replacement ? 1UL : 0UL);
            QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
            QIQI_TEST_CHECK(events == test.events && composition->get_range_calls_ == 1);
            QIQI_TEST_CHECK(context->get_property_calls_ ==
                            gsl::narrow_cast<unsigned int>(std::ranges::count(test.events, Event::Property)) &&
                            context->prop_->clear_calls_ ==
                            gsl::narrow_cast<unsigned int>(std::ranges::count(test.events, Event::Clear)));
            QIQI_TEST_CHECK(range->text_ == text && range->set_text_calls_ == 0 && range->clone_calls_ == 0 &&
                            context->set_selection_calls_ == 0 && context->prop_->set_calls_ == 0 &&
                            context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                            composition->end_composition_calls_ == 0 && composition->ended_);
            QIQI_TEST_CHECK(current_composition.Get() == replacement.Get());
            if (replacement) QIQI_TEST_CHECK(!replacement->ended_ && replacement->end_composition_calls_ == 0);
            composition->get_range_observer_ = {};
            context->get_property_observer_ = {};
            context->prop_->clear_callback_ = {};
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionFinishTests() {
    TestCompositionFinishing();
    TestDirectCompositionCommit();
    TestHostCompositionTermination();
}

} // namespace qiqi::test
