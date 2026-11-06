#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/comp/range.h"
#include "ime/comp/tests.h"
#include <array>
#include <string>
#include <vector>

namespace qiqi::test {

void RunTsfCompositionRangeTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { None, Text, Clone, Collapse, Selection };
    enum class Event { Text, Check, Clone, Collapse, Selection };
    struct Case {
        Boundary invalidation = Boundary::None;
        bool initially_current = true;
        HRESULT text_hr = S_OK;
        HRESULT clone_hr = S_OK;
        HRESULT collapse_hr = S_OK;
        HRESULT selection_hr = S_OK;
        bool null_clone = false;
        unsigned int clones = 1;
        unsigned int collapses = 1;
        unsigned int selections = 1;
        unsigned int checks = 3;
    };
    const std::array cases{
        Case{},
        Case{.invalidation = Boundary::Text, .clones = 0, .collapses = 0, .selections = 0, .checks = 1},
        Case{.invalidation = Boundary::Clone, .collapses = 0, .selections = 0, .checks = 2},
        Case{.invalidation = Boundary::Collapse, .selections = 0},
        Case{.invalidation = Boundary::Selection},
        Case{.initially_current = false, .clones = 0, .collapses = 0, .selections = 0, .checks = 1},
        Case{.text_hr = E_ACCESSDENIED},
        Case{.clone_hr = E_ACCESSDENIED, .collapses = 0, .selections = 0, .checks = 1},
        Case{.collapse_hr = E_ACCESSDENIED},
        Case{.selection_hr = E_ACCESSDENIED},
        Case{.null_clone = true, .collapses = 0, .selections = 0, .checks = 1},
        Case{.invalidation = Boundary::Text, .text_hr = E_ACCESSDENIED,
             .clones = 0, .collapses = 0, .selections = 0, .checks = 1},
        Case{.invalidation = Boundary::Collapse, .collapse_hr = E_ACCESSDENIED, .selections = 0},
    };
    const std::array inputs{
        std::wstring{L"abc"},
        std::wstring{},
        std::wstring{L'a', L'\0', L'\u4e2d', L'\xd840', L'\xdc00'},
    };
    for (const auto& text : inputs) {
        for (const auto& test : cases) {
            const auto context = MakeComObject<MockTfContext>();
            const auto range = context->range_;
            range->text_ = L"before";
            range->set_text_result_ = test.text_hr;
            range->clone_result_ = test.clone_hr;
            range->collapse_result_ = test.collapse_hr;
            range->return_null_clone_ = test.null_clone;
            context->set_selection_result_ = test.selection_hr;
            bool current = test.initially_current;
            unsigned int checks = 0;
            unsigned int collapses = 0;
            std::vector<Event> events;
            ComPtr<ITfRange> selected_range;
            range->set_text_callback_ = [&] {
                events.push_back(Event::Text);
                if (test.invalidation == Boundary::Text) current = false;
            };
            range->clone_callback_ = [&] {
                events.push_back(Event::Clone);
                if (test.invalidation == Boundary::Clone) current = false;
            };
            range->collapse_observer_ = [&](TfEditCookie ec, TfAnchor anchor) {
                events.push_back(Event::Collapse);
                ++collapses;
                QIQI_TEST_CHECK(ec == 127 && anchor == TF_ANCHOR_END);
            };
            range->collapse_callback_ = [&] {
                if (test.invalidation == Boundary::Collapse) current = false;
            };
            context->selection_observer_ = [&](TfEditCookie ec, ULONG count, const TF_SELECTION* selection) {
                events.push_back(Event::Selection);
                QIQI_TEST_CHECK(ec == 127 && count == 1 && selection && selection->range &&
                                selection->range != range.Get());
                QIQI_TEST_CHECK(selection->style.ase == TF_AE_NONE && selection->style.fInterimChar == FALSE);
                selected_range = selection->range;
                if (test.invalidation == Boundary::Selection) current = false;
            };
            WriteCompositionTextAndCaret(127, context.Get(), range.Get(), text, [&] {
                events.push_back(Event::Check);
                ++checks;
                return current;
            });
            QIQI_TEST_CHECK(range->set_text_calls_ == 1 && range->last_set_text_cookie_ == 127 &&
                            range->last_set_text_flags_ == 0 &&
                            range->last_set_text_length_ == gsl::narrow_cast<LONG>(text.size()));
            QIQI_TEST_CHECK(range->text_ == (FAILED(test.text_hr) ? std::wstring{L"before"} : text));
            QIQI_TEST_CHECK(range->clone_calls_ == test.clones && collapses == test.collapses &&
                            context->set_selection_calls_ == test.selections && checks == test.checks);
            std::vector<Event> expected{Event::Text, Event::Check};
            if (test.clones) expected.push_back(Event::Clone);
            if (test.checks > 1) expected.push_back(Event::Check);
            if (test.collapses) {
                expected.push_back(Event::Collapse);
                expected.push_back(Event::Check);
            }
            if (test.selections) expected.push_back(Event::Selection);
            QIQI_TEST_CHECK(events == expected);
            if (test.selections) {
                QIQI_TEST_CHECK(selected_range && selected_range->AddRef() == 2);
                QIQI_TEST_CHECK(selected_range->Release() == 1);
            } else {
                QIQI_TEST_CHECK(!selected_range);
            }
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->get_property_calls_ == 0 &&
                            context->start_composition_calls_ == 0);
            range->set_text_callback_ = {};
            range->clone_callback_ = {};
            range->collapse_callback_ = {};
            range->collapse_observer_ = {};
            context->selection_observer_ = {};
            selected_range.Reset();
        }
    }
}

} // namespace qiqi::test
