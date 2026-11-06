#include "common/test_support.h"
#include "ime/comp/layout.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <optional>
#include <stdexcept>

namespace qiqi::test {
namespace {

void TestLayoutEntryGates() {
    enum class Target { Missing, Same, Other };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto code : {TF_LC_CREATE, TF_LC_CHANGE, TF_LC_DESTROY}) {
        for (const auto target : {Target::Missing, Target::Same, Target::Other}) {
            for (const bool active : {false, true}) {
                for (const bool bound : {false, true}) {
                    for (const bool composing : {false, true}) {
                        for (const bool ui : {false, true}) {
                            for (const bool resetting : {false, true}) {
                                const auto context = MakeComObject<MockTfContext>();
                                const auto other = MakeComObject<MockTfContext>();
                                const auto composition = MakeComObject<MockTfComposition>(context->range_);
                                CompositionLifecycleState state(673);
                                state.SetActive(active);
                                if (bound) state.SetContext(context);
                                if (composing) state.SetComposition(composition);
                                std::optional<CompositionContextResetScope> scope;
                                if (resetting) scope.emplace(state);
                                const ITfContext* input = target == Target::Missing ? nullptr :
                                    target == Target::Same ? context.Get() : other.Get();
                                const GUID activity{0x123, 0x234, 0x345, {1, 2, 3, 4, 5, 6, 7, 8}};
                                const auto refs = context->AddRef();
                                std::ignore = context->Release();
                                unsigned int ui_reads_seen = 0;
                                unsigned int requests_seen = 0;
                                const bool reaches_ui_gate = code == TF_LC_CHANGE && target == Target::Same &&
                                    active && bound && composing;
                                const auto result = EnterCompositionLayoutNotification(
                                    input, code, state, 673, activity,
                                    [&, reads = std::make_unique<unsigned int>(0)] {
                                        ui_reads_seen = ++*reads;
                                        return ui;
                                    },
                                    [&, requests = std::make_unique<unsigned int>(0)](const GUID& id) {
                                        requests_seen = ++*requests;
                                        QIQI_TEST_CHECK(requests_seen == 1 && ui_reads_seen == 2 &&
                                                        reaches_ui_gate && ui && !IsEqualGUID(id, GUID_NULL) &&
                                                        !IsEqualGUID(id, activity) &&
                                                        context->AddRef() == refs && context->Release() == refs - 1);
                                    });
                                QIQI_TEST_CHECK(result == S_OK && requests_seen == (reaches_ui_gate && ui ? 1u : 0u) &&
                                                ui_reads_seen == (reaches_ui_gate ? 2u : 1u) &&
                                                state.IsActive() == active && state.IsResettingContext() == resetting &&
                                                state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                                state.Composition().Get() == (composing ? composition.Get() : nullptr) &&
                                                state.Epoch().context == (resetting ? 1u : 0u) &&
                                                context->AddRef() == refs && context->Release() == refs - 1 &&
                                                context->request_edit_session_calls_ == 0 && other->request_edit_session_calls_ == 0 &&
                                                context->get_document_calls_ == 0 && context->get_active_view_calls_ == 0 &&
                                                context->range_->set_text_calls_ == 0 && composition->end_composition_calls_ == 0);
                                ++cases;
                            }
                        }
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 288 && g_com_object_count.load() == objects_before);
}

void TestLayoutEntryCallbacks() {
    enum class Exit { Normal, Exception, Nested, ChangedUI };
    const auto objects_before = g_com_object_count.load();
    for (const auto exit : {Exit::Normal, Exit::Exception, Exit::Nested, Exit::ChangedUI}) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        CompositionLifecycleState state(677);
        state.SetActive(true);
        state.SetContext(context);
        state.SetComposition(composition);
        const GUID original_activity{0x123, 0x234, 0x345, {1, 2, 3, 4, 5, 6, 7, 8}};
        GUID activity = original_activity;
        unsigned int reads = 0;
        unsigned int calls = 0;
        unsigned int nested_calls = 0;
        bool caught = false;
        HRESULT result = E_FAIL;
        try {
            result = EnterCompositionLayoutNotification(
                context.Get(), TF_LC_CHANGE, state, 677, activity,
                [&] {
                    ++reads;
                    return exit != Exit::ChangedUI || reads == 1;
                },
                [&](const GUID& id) {
                    ++calls;
                    QIQI_TEST_CHECK(reads == 2 && !IsEqualGUID(id, original_activity) &&
                                    IsEqualGUID(activity, original_activity) &&
                                    context->AddRef() == 3 && context->Release() == 2);
                    if (exit == Exit::Nested) {
                        const GUID outer = id;
                        QIQI_TEST_CHECK(EnterCompositionLayoutNotification(
                            context.Get(), TF_LC_CHANGE, state, 677, id,
                            [] { return true; },
                            [&](const GUID& nested) {
                                ++nested_calls;
                                QIQI_TEST_CHECK(!IsEqualGUID(nested, outer) && IsEqualGUID(id, outer) &&
                                                IsEqualGUID(activity, original_activity));
                                state.InvalidateContext();
                            }) == S_OK && IsEqualGUID(id, outer));
                    }
                    // Notification entry does not own or restore the lifecycle activity slot.
                    activity = GUID_NULL;
                    if (exit == Exit::Exception) throw std::runtime_error("layout notification entry");
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught == (exit == Exit::Exception) && result == (caught ? E_FAIL : S_OK) &&
                        calls == (exit == Exit::ChangedUI ? 0u : 1u) && reads == 2 &&
                        nested_calls == (exit == Exit::Nested ? 1u : 0u) &&
                        IsEqualGUID(activity, exit == Exit::ChangedUI ? original_activity : GUID_NULL) &&
                        state.Epoch().context == (exit == Exit::Nested ? 1u : 0u) &&
                        state.Context().Get() == context.Get() && state.Composition().Get() == composition.Get() &&
                        context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionLayoutEntryTests() {
    TestLayoutEntryGates();
    TestLayoutEntryCallbacks();
}

} // namespace qiqi::test
