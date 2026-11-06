#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/presentation.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <optional>
#include <stdexcept>

namespace qiqi::test {
namespace {

void TestPresentationEntryGates() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool ui_present : {false, true}) {
        for (const bool manager_present : {false, true}) {
            for (const bool bound : {false, true}) {
                for (const bool active : {false, true}) {
                    for (const bool resetting : {false, true}) {
                        for (const bool composing : {false, true}) {
                            const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
                            const auto manager = MakeComObject<MockTfThreadMgr>();
                            const auto context = MakeComObject<MockTfContext>();
                            const auto composition = MakeComObject<MockTfComposition>(context->range_);
                            CompositionLifecycleState state(683);
                            state.SetActive(active);
                            if (bound) state.SetContext(context);
                            if (composing) state.SetComposition(composition);
                            std::optional<CompositionContextResetScope> scope;
                            if (resetting) scope.emplace(state);
                            const auto refs = context->AddRef();
                            std::ignore = context->Release();
                            unsigned int calls_seen = 0;
                            EnterCompositionCandidatePresentation(
                                ui_present ? ui.Get() : nullptr, manager_present ? manager.Get() : nullptr, state,
                                [&, calls = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                                    calls_seen = ++*calls;
                                    QIQI_TEST_CHECK(calls_seen == 1 && ticket.Matches({0, 0}, context.Get()) &&
                                                    state.IsCurrent(ticket) &&
                                                    context->AddRef() == refs + 1 && context->Release() == refs &&
                                                    ui->AddRef() == 2 && ui->Release() == 1 &&
                                                    manager->AddRef() == 2 && manager->Release() == 1);
                                });
                            const bool eligible = ui_present && manager_present && bound && active && !resetting;
                            QIQI_TEST_CHECK(calls_seen == (eligible ? 1u : 0u) &&
                                            state.IsActive() == active && state.IsResettingContext() == resetting &&
                                            state.Epoch().context == (resetting ? 1u : 0u) &&
                                            state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                            state.Composition().Get() == (composing ? composition.Get() : nullptr) &&
                                            context->AddRef() == refs && context->Release() == refs - 1 &&
                                            ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                            manager->begin_ui_calls_ == 0 && manager->end_ui_calls_ == 0 &&
                                            manager->updated_ui_ids_.empty() &&
                                            context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 64 && g_com_object_count.load() == objects_before);
}

void TestPresentationEntryOwners() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Exit { Normal, Exception, Nested, Replacement };
    const auto objects_before = g_com_object_count.load();
    for (const auto exit : {Exit::Normal, Exit::Exception, Exit::Nested, Exit::Replacement}) {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        auto context = MakeComObject<MockTfContext>();
        auto* const raw = context.Get();
        const auto replacement = MakeComObject<MockTfContext>();
        CompositionLifecycleState state(691);
        state.SetActive(true);
        state.SetContext(context);
        bool callback_finished = false;
        bool released = false;
        bool released_after_callback = false;
        raw->release_observer_ = [&]() noexcept {
            released = true;
            released_after_callback = callback_finished;
        };
        unsigned int calls = 0;
        unsigned int nested_calls = 0;
        bool caught = false;
        try {
            EnterCompositionCandidatePresentation(
                ui.Get(), manager.Get(), state,
                [&](EditSessionTicket ticket) {
                    ++calls;
                    const auto mark_finished = gsl::finally([&]() noexcept { callback_finished = true; });
                    QIQI_TEST_CHECK(ticket.Matches({0, 0}, raw) && raw->AddRef() == 4 && raw->Release() == 3);
                    if (exit == Exit::Nested) {
                        EnterCompositionCandidatePresentation(
                            ui.Get(), manager.Get(), state,
                            [&](EditSessionTicket nested) {
                                ++nested_calls;
                                QIQI_TEST_CHECK(nested.Matches({0, 0}, raw) && raw->AddRef() == 5 && raw->Release() == 4);
                                state.InvalidateContext();
                            });
                        QIQI_TEST_CHECK(ticket.Matches({0, 0}, raw) && !state.IsCurrent(ticket));
                    }
                    if (exit == Exit::Replacement) {
                        state.SetContext(replacement);
                    } else {
                        state.ClearContext();
                        state.InvalidateContext();
                    }
                    context.Reset();
                    QIQI_TEST_CHECK(!released && ticket.Matches({0, 0}, raw) &&
                                    raw->AddRef() == 2 && raw->Release() == 1);
                    if (exit == Exit::Exception) throw std::runtime_error("candidate presentation entry");
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught == (exit == Exit::Exception) && calls == 1 &&
                        nested_calls == (exit == Exit::Nested ? 1u : 0u) &&
                        callback_finished && released && released_after_callback && !context &&
                        state.Context().Get() == (exit == Exit::Replacement ? replacement.Get() : nullptr) &&
                        state.Epoch().context == (exit == Exit::Replacement ? 0u : exit == Exit::Nested ? 2u : 1u) &&
                        ui->AddRef() == 2 && ui->Release() == 1 && manager->AddRef() == 2 && manager->Release() == 1 &&
                        manager->begin_ui_calls_ == 0 && manager->end_ui_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionPresentationEntryTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestPresentationEntryGates();
    TestPresentationEntryOwners();
}

} // namespace qiqi::test
