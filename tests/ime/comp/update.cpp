#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/comp/update.h"
#include "ime/comp/tests.h"
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace qiqi::test {
namespace {

enum class Event { None, Range, Check, Text, Clone, Collapse, Selection, Property, Attribute };
constexpr std::array kTrace{
    Event::Range, Event::Check, Event::Text, Event::Check, Event::Clone, Event::Check,
    Event::Collapse, Event::Check, Event::Selection, Event::Check, Event::Property, Event::Check, Event::Attribute,
};
constexpr std::array kCloneFailureTrace{
    Event::Range, Event::Check, Event::Text, Event::Check, Event::Clone, Event::Check,
    Event::Property, Event::Check, Event::Attribute,
};

std::vector<Event> TracePrefix(std::span<const Event> trace, size_t count, bool extra_check = false) {
    const auto prefix = trace.first(count);
    std::vector<Event> result(prefix.begin(), prefix.end());
    if (extra_check) result.push_back(Event::Check);
    return result;
}

void TestCompositionUpdates() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Case {
        Event invalidation = Event::None;
        bool initially_current = true;
        bool fail_range = false;
        bool null_range = false;
        HRESULT text_hr = S_OK;
        HRESULT clone_hr = S_OK;
        bool null_clone = false;
        HRESULT collapse_hr = S_OK;
        HRESULT selection_hr = S_OK;
        HRESULT property_hr = S_OK;
        bool null_property = false;
        HRESULT attribute_hr = S_OK;
        TfGuidAtom atom = 23;
        std::vector<Event> events{kTrace.begin(), kTrace.end()};
    };
    const std::array cases{
        Case{},
        Case{.initially_current = false, .events = TracePrefix(kTrace, 2)},
        Case{.invalidation = Event::Range, .events = TracePrefix(kTrace, 2)},
        Case{.invalidation = Event::Text, .events = TracePrefix(kTrace, 4, true)},
        Case{.invalidation = Event::Clone, .events = TracePrefix(kTrace, 6, true)},
        Case{.invalidation = Event::Collapse, .events = TracePrefix(kTrace, 8, true)},
        Case{.invalidation = Event::Selection, .events = TracePrefix(kTrace, 10)},
        Case{.invalidation = Event::Property, .events = TracePrefix(kTrace, 12)},
        Case{.invalidation = Event::Attribute},
        Case{.fail_range = true, .events = TracePrefix(kTrace, 1)},
        Case{.null_range = true, .events = TracePrefix(kTrace, 1)},
        Case{.text_hr = E_ACCESSDENIED},
        Case{.clone_hr = E_ACCESSDENIED, .events = TracePrefix(kCloneFailureTrace, 9)},
        Case{.null_clone = true, .events = TracePrefix(kCloneFailureTrace, 9)},
        Case{.collapse_hr = E_ACCESSDENIED},
        Case{.selection_hr = E_ACCESSDENIED},
        Case{.property_hr = E_ACCESSDENIED, .events = TracePrefix(kTrace, 11)},
        Case{.null_property = true, .events = TracePrefix(kTrace, 11)},
        Case{.attribute_hr = E_ACCESSDENIED},
        Case{.atom = TF_INVALID_GUIDATOM, .events = TracePrefix(kTrace, 10)},
        Case{.initially_current = false, .fail_range = true, .events = TracePrefix(kTrace, 1)},
        Case{.initially_current = false, .null_range = true, .events = TracePrefix(kTrace, 1)},
        Case{.invalidation = Event::Text, .text_hr = E_ACCESSDENIED, .events = TracePrefix(kTrace, 4, true)},
        Case{.invalidation = Event::Clone, .clone_hr = E_ACCESSDENIED, .events = TracePrefix(kTrace, 6)},
        Case{.invalidation = Event::Collapse, .collapse_hr = E_ACCESSDENIED, .events = TracePrefix(kTrace, 8, true)},
        Case{.invalidation = Event::Property, .property_hr = E_ACCESSDENIED, .events = TracePrefix(kTrace, 11)},
        Case{.invalidation = Event::Attribute, .attribute_hr = E_ACCESSDENIED},
        Case{.invalidation = Event::Property, .null_property = true, .events = TracePrefix(kTrace, 11)},
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
            ComPtr<MockTfComposition> replacement;
            ComPtr<ITfRange> selected_range;
            std::vector<Event> events;
            const auto observe = [&](Event event) {
                events.push_back(event);
                if (test.invalidation == event) {
                    replacement = MakeComObject<MockTfComposition>(range);
                    current_composition = replacement;
                }
            };
            const auto count = [&](Event event) {
                return gsl::narrow_cast<unsigned int>(std::ranges::count(test.events, event));
            };
            composition->fail_get_range_ = test.fail_range;
            composition->return_null_range_ = test.null_range;
            range->text_ = L"before";
            range->set_text_result_ = test.text_hr;
            range->clone_result_ = test.clone_hr;
            range->return_null_clone_ = test.null_clone;
            range->collapse_result_ = test.collapse_hr;
            context->set_selection_result_ = test.selection_hr;
            context->get_property_result_ = test.property_hr;
            context->return_null_property_ = test.null_property;
            context->prop_->set_result_ = test.attribute_hr;
            const ULONG range_probe = range->AddRef();
            QIQI_TEST_CHECK(range->Release() == range_probe - 1);
            composition->get_range_observer_ = [&](ITfRange** output) {
                QIQI_TEST_CHECK(output && !*output);
                observe(Event::Range);
            };
            range->set_text_callback_ = [&] { observe(Event::Text); };
            range->clone_callback_ = [&] { observe(Event::Clone); };
            range->collapse_observer_ = [&](TfEditCookie ec, TfAnchor anchor) {
                QIQI_TEST_CHECK(ec == 179 && anchor == TF_ANCHOR_END);
                observe(Event::Collapse);
            };
            context->selection_observer_ = [&](TfEditCookie ec, ULONG amount, const TF_SELECTION* selection) {
                QIQI_TEST_CHECK(ec == 179 && amount == 1 && selection && selection->range &&
                                selection->range != range.Get() && selection->style.ase == TF_AE_NONE &&
                                selection->style.fInterimChar == FALSE);
                selected_range = selection->range;
                observe(Event::Selection);
            };
            context->get_property_observer_ = [&](REFGUID guid, ITfProperty** output) {
                QIQI_TEST_CHECK(IsEqualGUID(guid, GUID_PROP_ATTRIBUTE) && output && !*output);
                if (selected_range) QIQI_TEST_CHECK(selected_range->AddRef() == 2 && selected_range->Release() == 1);
                observe(Event::Property);
            };
            context->prop_->set_observer_ = [&](TfEditCookie ec, ITfRange* target, const VARIANT* value) {
                QIQI_TEST_CHECK(ec == 179 && target == range.Get() && value && value->vt == VT_I4 &&
                                value->lVal == gsl::narrow_cast<LONG>(test.atom) &&
                                value->wReserved1 == 0 && value->wReserved2 == 0 && value->wReserved3 == 0);
                QIQI_TEST_CHECK(context->prop_->AddRef() == 3 && context->prop_->Release() == 2);
                observe(Event::Attribute);
            };
            const auto updated = UpdateCompositionInSession(
                179, context.Get(), composition, text, test.atom,
                [&](const ITfComposition* captured) {
                    QIQI_TEST_CHECK(captured == composition.Get());
                    observe(Event::Check);
                    const ULONG expected_probe = range_probe + 1UL + (replacement ? 1UL : 0UL);
                    QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
                    return test.initially_current && current_composition.Get() == captured;
                });
            QIQI_TEST_CHECK(events == test.events && updated.Get() == composition.Get());
            QIQI_TEST_CHECK(composition->get_range_calls_ == 1 && range->set_text_calls_ == count(Event::Text) &&
                            range->clone_calls_ == count(Event::Clone) &&
                            context->set_selection_calls_ == count(Event::Selection) &&
                            context->get_property_calls_ == count(Event::Property) &&
                            context->prop_->set_calls_ == count(Event::Attribute));
            QIQI_TEST_CHECK(range->text_ == (count(Event::Text) && SUCCEEDED(test.text_hr) ? text : L"before"));
            if (count(Event::Text)) {
                QIQI_TEST_CHECK(range->last_set_text_cookie_ == 179 && range->last_set_text_flags_ == 0 &&
                                range->last_set_text_length_ == gsl::narrow_cast<LONG>(text.size()));
            }
            const ULONG expected_probe = range_probe + (replacement ? 1UL : 0UL);
            QIQI_TEST_CHECK(range->AddRef() == expected_probe && range->Release() == expected_probe - 1);
            QIQI_TEST_CHECK(context->prop_->AddRef() == 2 && context->prop_->Release() == 1);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 0 && context->request_edit_session_calls_ == 0 &&
                            context->start_composition_calls_ == 0 && context->insert_at_selection_calls_ == 0 &&
                            composition->end_composition_calls_ == 0);
            QIQI_TEST_CHECK(current_composition.Get() == (replacement ? replacement.Get() : composition.Get()));
            composition->get_range_observer_ = {};
            range->set_text_callback_ = {};
            range->clone_callback_ = {};
            range->collapse_observer_ = {};
            context->selection_observer_ = {};
            context->get_property_observer_ = {};
            context->prop_->set_observer_ = {};
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestLiveCompositionAttributeAtom() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Case {
        TfGuidAtom initial;
        Event boundary;
        TfGuidAtom next;
        bool writes;
    };
    constexpr std::array cases{
        Case{TF_INVALID_GUIDATOM, Event::Selection, 23, true},
        Case{23, Event::Selection, TF_INVALID_GUIDATOM, false},
        Case{23, Event::Property, 47, true},
        Case{23, Event::Property, TF_INVALID_GUIDATOM, true},
        Case{23, Event::Selection, 0x80000005UL, true},
    };
    for (const auto& test : cases) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        TfGuidAtom atom = test.initial;
        bool changed = false;
        context->set_selection_callback_ = [&] {
            if (test.boundary == Event::Selection) {
                atom = test.next;
                changed = true;
            }
        };
        context->get_property_callback_ = [&] {
            QIQI_TEST_CHECK(test.boundary == Event::Property || changed);
            if (test.boundary == Event::Property) {
                atom = test.next;
                changed = true;
            }
        };
        context->prop_->set_observer_ = [&](TfEditCookie ec, ITfRange* range, const VARIANT* value) {
            QIQI_TEST_CHECK(changed && ec == 181 && range == context->range_.Get() &&
                            value && value->vt == VT_I4 && value->lVal == gsl::narrow_cast<LONG>(test.next));
        };
        const auto updated = UpdateCompositionInSession(
            181, context.Get(), composition, L"abc", atom,
            [&](const ITfComposition* captured) noexcept { return captured == composition.Get(); });
        QIQI_TEST_CHECK(changed && updated.Get() == composition.Get() && atom == test.next &&
                        context->get_property_calls_ == (test.writes ? 1u : 0u) &&
                        context->prop_->set_calls_ == (test.writes ? 1u : 0u));
        context->set_selection_callback_ = {};
        context->get_property_callback_ = {};
        context->prop_->set_observer_ = {};
    }
}

void TestCompositionUpdateOwnership() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    for (const auto boundary : {Event::Range, Event::Property, Event::Attribute}) {
        const auto context = MakeComObject<MockTfContext>();
        auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto captured = composition.Get();
        bool released = false;
        const auto release = [&] {
            composition.Reset();
            released = true;
        };
        if (boundary == Event::Range) {
            composition->get_range_callback_ = release;
        } else if (boundary == Event::Property) {
            context->get_property_callback_ = release;
        } else {
            context->prop_->set_callback_ = release;
        }
        constexpr TfGuidAtom atom = 23;
        auto updated = UpdateCompositionInSession(
            191, context.Get(), composition, L"abc", atom,
            [&](const ITfComposition* expected) noexcept {
                QIQI_TEST_CHECK(expected == captured);
                return expected == composition.Get();
            });
        QIQI_TEST_CHECK(released && !composition && updated.Get() == captured);
        QIQI_TEST_CHECK(updated->AddRef() == 2 && updated->Release() == 1);
        QIQI_TEST_CHECK(context->prop_->AddRef() == 2 && context->prop_->Release() == 1);
        captured->get_range_callback_ = {};
        context->get_property_callback_ = {};
        context->prop_->set_callback_ = {};
        updated.Reset();
        QIQI_TEST_CHECK(context->range_->AddRef() == 2 && context->range_->Release() == 1);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionUpdateTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCompositionUpdates();
    TestLiveCompositionAttributeAtom();
    TestCompositionUpdateOwnership();
}

} // namespace qiqi::test
