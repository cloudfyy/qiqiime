#include "common/test_support.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <optional>

namespace qiqi::test {

void RunTsfCompositionIdentityTests() {
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool resetting : {false, true}) {
            for (const bool bound : {false, true}) {
                for (const bool valid_ticket : {false, true}) {
                    for (const bool has_composition : {false, true}) {
                        const auto context = MakeComObject<MockTfContext>();
                        const auto composition = MakeComObject<MockTfComposition>(context->range_);
                        const auto replacement = MakeComObject<MockTfComposition>(context->range_);
                        CompositionLifecycleState state(569);
                        state.SetActive(active);
                        if (bound) state.SetContext(context);
                        if (has_composition) state.SetComposition(composition);
                        std::optional<CompositionContextResetScope> scope;
                        if (resetting) scope.emplace(state);
                        EditSessionTicket ticket{state.Epoch(), state.Context().Get()};
                        if (!valid_ticket) ++ticket.epoch.context;
                        const std::array<const ITfComposition*, 3> identities{nullptr, composition.Get(), replacement.Get()};
                        for (const auto* expected : identities) {
                            const bool current = active && !resetting && valid_ticket;
                            QIQI_TEST_CHECK(CheckCompositionEditSessionCurrent(state, ticket, 569, GUID_NULL) == current &&
                                            CheckCompositionEditCurrent(state, ticket, expected, 569, GUID_NULL) ==
                                                (current && state.Composition().Get() == expected) &&
                                            state.IsActive() == active && state.IsResettingContext() == resetting &&
                                            state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                            state.Composition().Get() == (has_composition ? composition.Get() : nullptr) &&
                                            state.Cleanups().TrackedCount() == 0 &&
                                            context->request_edit_session_calls_ == 0 && context->get_property_calls_ == 0 &&
                                            context->range_->set_text_calls_ == 0 && composition->get_range_calls_ == 0 &&
                                            composition->end_composition_calls_ == 0 && replacement->get_range_calls_ == 0);
                            const ULONG composition_refs = has_composition ? 2UL : 1UL;
                            const ULONG context_refs = bound ? 2UL : 1UL;
                            QIQI_TEST_CHECK(composition->AddRef() == composition_refs + 1 && composition->Release() == composition_refs &&
                                            context->AddRef() == context_refs + 1 && context->Release() == context_refs);
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 96 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
