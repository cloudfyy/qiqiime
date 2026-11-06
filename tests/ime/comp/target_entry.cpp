#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/target.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <optional>
#include <stdexcept>

namespace qiqi::test {
namespace {

void TestTargetEntryGates() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Target { Missing, Same, Other };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool resetting : {false, true}) {
            for (const bool preparing : {false, true}) {
                for (const auto target : {Target::Missing, Target::Same, Target::Other}) {
                    for (const bool prepared : {false, true}) {
                        const auto context = MakeComObject<MockTfContext>();
                        const auto other = MakeComObject<MockTfContext>();
                        CompositionLifecycleState state(577);
                        state.SetActive(active);
                        state.SetContext(context);
                        ComPtr<ITfContext> pending;
                        if (preparing) pending = other;
                        std::optional<CompositionContextResetScope> scope;
                        if (resetting) scope.emplace(state);
                        const GUID original_activity{0x101, 0x202, 0x303, {1, 2, 3, 4, 5, 6, 7, 8}};
                        GUID activity = original_activity;
                        ITfContext* input = target == Target::Missing ? nullptr :
                            target == Target::Same ? context.Get() : other.Get();
                        unsigned int calls_seen = 0;
                        const bool result = EnterCompositionTargetPreparation(
                            input, state, pending.Get(), activity, 577,
                            [&, calls = std::make_unique<unsigned int>(0)](gsl::not_null<ITfContext*> actual, const GUID& id) {
                                calls_seen = ++*calls;
                                QIQI_TEST_CHECK(actual.get() == other.Get() && IsEqualGUID(activity, id) &&
                                                !IsEqualGUID(activity, original_activity) && !pending);
                                return prepared;
                            });
                        const bool eligible = active && !resetting && !preparing && target != Target::Missing;
                        const bool invoked = eligible && target == Target::Other;
                        QIQI_TEST_CHECK(result == (eligible && (target == Target::Same || prepared)) &&
                                        calls_seen == (invoked ? 1u : 0u) && IsEqualGUID(activity, original_activity) &&
                                        state.IsActive() == active && state.IsResettingContext() == resetting &&
                                        state.Context().Get() == context.Get() && pending.Get() == (preparing ? other.Get() : nullptr) &&
                                        state.Epoch().context == (resetting ? 1u : 0u) &&
                                        context->get_document_calls_ == 0 && other->get_document_calls_ == 0 &&
                                        context->request_edit_session_calls_ == 0 && other->request_edit_session_calls_ == 0);
                        ++cases;
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 48 && g_com_object_count.load() == objects_before);
}

void TestTargetEntryActivityScopes() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Exit { Success, Failure, Exception, Nested };
    const auto objects_before = g_com_object_count.load();
    for (const auto exit : {Exit::Success, Exit::Failure, Exit::Exception, Exit::Nested}) {
        const auto context = MakeComObject<MockTfContext>();
        const auto nested_context = MakeComObject<MockTfContext>();
        CompositionLifecycleState state(587);
        state.SetActive(true);
        ComPtr<ITfContext> pending;
        const GUID original_activity{0x404, 0x505, 0x606, {8, 7, 6, 5, 4, 3, 2, 1}};
        GUID activity = original_activity;
        bool caught = false;
        bool result = false;
        try {
            result = EnterCompositionTargetPreparation(
                context.Get(), state, pending.Get(), activity, 587,
                [&](gsl::not_null<ITfContext*> actual, const GUID& id) {
                    QIQI_TEST_CHECK(actual.get() == context.Get() && IsEqualGUID(activity, id));
                    if (exit == Exit::Exception) throw std::runtime_error("injected target entry failure");
                    if (exit == Exit::Nested) {
                        const GUID outer = activity;
                        QIQI_TEST_CHECK(EnterCompositionTargetPreparation(
                            nested_context.Get(), state, pending.Get(), activity, 587,
                            [&](gsl::not_null<ITfContext*> nested, const GUID& nested_id) {
                                QIQI_TEST_CHECK(nested.get() == nested_context.Get() && IsEqualGUID(activity, nested_id) &&
                                                !IsEqualGUID(activity, outer));
                                return true;
                            }));
                        QIQI_TEST_CHECK(IsEqualGUID(activity, outer));
                        pending = context;
                        QIQI_TEST_CHECK(!EnterCompositionTargetPreparation(
                            nested_context.Get(), state, pending.Get(), activity, 587,
                            [](gsl::not_null<ITfContext*>, const GUID&) {
                                QIQI_TEST_CHECK(false);
                                return false;
                            }));
                        QIQI_TEST_CHECK(IsEqualGUID(activity, outer));
                        pending.Reset();
                    }
                    // Restoration must use the saved ID, not the callback's last value.
                    activity = GUID_NULL;
                    return exit != Exit::Failure;
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught == (exit == Exit::Exception) && result == (exit == Exit::Success || exit == Exit::Nested) &&
                        IsEqualGUID(activity, original_activity) && !pending && !state.Context() && state.Epoch().context == 0 &&
                        context->AddRef() == 2 && context->Release() == 1 &&
                        nested_context->AddRef() == 2 && nested_context->Release() == 1);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionTargetEntryTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestTargetEntryGates();
    TestTargetEntryActivityScopes();
}

} // namespace qiqi::test
