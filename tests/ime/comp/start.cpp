#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/comp/start.h"
#include "ime/comp/tests.h"
#include <array>
#include <vector>

namespace qiqi::test {

void RunTsfCompositionStartTests() {
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

} // namespace qiqi::test
