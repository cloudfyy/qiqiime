// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace qiqi::test {
namespace {

class ResetLayoutSink final : public ComObjectBase<ResetLayoutSink, ITfTextLayoutSink> {
public:
    STDMETHODIMP OnLayoutChange(ITfContext*, TfLayoutCode, ITfContextView*) noexcept override { return S_OK; }
};

void TestContextResetProtocol() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Event { Schedule, Range, Clear, End, UI };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool cleanup : {false, true}) {
            for (const bool has_composition : {false, true}) {
                for (const bool connected : {false, true}) {
                    for (const bool nested : {false, true}) {
                        const auto context = MakeComObject<MockTfContext>();
                        const auto document = MakeComObject<MockTfDocumentMgr>();
                        const auto composition = has_composition ? MakeComObject<MockTfComposition>(context->range_) :
                            ComPtr<MockTfComposition>{};
                        const auto sink = MakeComObject<ResetLayoutSink>();
                        CompositionLifecycleState state(487);
                        state.SetActive(active);
                        state.SetContext(context);
                        state.SetDocument(document);
                        state.SetComposition(composition);
                        ScopedTfSinkConnection layout_sink;
                        if (connected) QIQI_TEST_CHECK(layout_sink.Advise(state.Context().Get(), IID_ITfTextLayoutSink, sink.Get()));
                        const EditSessionTicket ticket{state.Epoch(), context.Get()};
                        bool has_caret = true;
                        std::vector<Event> events;
                        if (composition) {
                            composition->get_range_observer_ = [&](ITfRange**) { events.push_back(Event::Range); };
                            context->prop_->clear_callback_ = [&](TfEditCookie ec) {
                                QIQI_TEST_CHECK(ec == 1);
                                events.push_back(Event::Clear);
                            };
                            composition->end_callback_ = [&](TfEditCookie ec) {
                                events.push_back(Event::End);
                                QIQI_TEST_CHECK(ec == 1 && state.IsResettingContext() && !state.Composition() &&
                                                state.Context().Get() == context.Get() &&
                                                state.Document().Get() == document.Get() && has_caret &&
                                                layout_sink.IsConnected() == connected);
                            };
                        }
                        std::optional<CompositionContextResetScope> outer;
                        if (nested) outer.emplace(state);
                        unsigned int scheduled = 0;
                        unsigned int ended_ui = 0;
                        ResetCompositionContextAndUI(
                            state, cleanup, has_caret, layout_sink, 487, GUID_NULL,
                            [&, calls = std::make_unique<unsigned int>(0)] {
                                scheduled = ++*calls;
                                events.push_back(Event::Schedule);
                                QIQI_TEST_CHECK(state.IsResettingContext() && !state.IsCurrent(ticket) &&
                                                state.Context().Get() == context.Get() && has_caret);
                                ScheduleCurrentCompositionCleanup(state, 23, 487, GUID_NULL);
                            },
                            [&, calls = std::make_unique<unsigned int>(0)] {
                                ended_ui = ++*calls;
                                events.push_back(Event::UI);
                                QIQI_TEST_CHECK(state.IsResettingContext() && !state.Context() && !state.Document() &&
                                                !state.Composition() && !has_caret && !layout_sink.IsConnected() &&
                                                context->layout_unadvise_calls_ == (connected ? 1u : 0u));
                            });
                        const bool host_cleanup = !nested && cleanup && has_composition;
                        std::vector<Event> expected;
                        if (!nested) {
                            if (cleanup) expected.push_back(Event::Schedule);
                            if (host_cleanup) {
                                expected.push_back(Event::Range);
                                expected.push_back(Event::Clear);
                                expected.push_back(Event::End);
                            }
                            expected.push_back(Event::UI);
                        }
                        QIQI_TEST_CHECK(events == expected && scheduled == (!nested && cleanup ? 1u : 0u) &&
                                        ended_ui == (nested ? 0u : 1u) && state.IsActive() == active &&
                                        state.IsResettingContext() == nested && state.Epoch().activation == 0 &&
                                        state.Epoch().context == (nested ? 2u : 1u) && !state.IsCurrent(ticket) &&
                                        has_caret == nested && layout_sink.IsConnected() == (nested && connected) &&
                                        context->request_edit_session_calls_ == (host_cleanup ? 1u : 0u) &&
                                        context->prop_->clear_calls_ == (host_cleanup ? 1u : 0u) &&
                                        context->range_->set_text_calls_ == 0 && context->start_composition_calls_ == 0 &&
                                        !state.Cleanups().HasPending(context.Get(), GUID_NULL));
                        QIQI_TEST_CHECK(state.Context().Get() == (nested ? context.Get() : nullptr) &&
                                        state.Document().Get() == (nested ? document.Get() : nullptr) &&
                                        state.Composition().Get() == (nested ? composition.Get() : nullptr));
                        if (composition) {
                            QIQI_TEST_CHECK(composition->ended_ == host_cleanup &&
                                            composition->end_composition_calls_ == (host_cleanup ? 1u : 0u));
                            const ULONG references = nested ? 2UL : 1UL;
                            QIQI_TEST_CHECK(composition->AddRef() == references + 1 && composition->Release() == references);
                        }
                        const ULONG context_references = nested ? (connected ? 3UL : 2UL) : 1UL;
                        const ULONG document_references = nested ? 2UL : 1UL;
                        QIQI_TEST_CHECK(context->AddRef() == context_references + 1 && context->Release() == context_references &&
                                        document->AddRef() == document_references + 1 && document->Release() == document_references);
                        outer.reset();
                        QIQI_TEST_CHECK(!state.IsResettingContext());
                        ++cases;
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 32 && g_com_object_count.load() == objects_before);
}

void TestContextResetCallbackBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { Schedule, UI };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto boundary : {Boundary::Schedule, Boundary::UI}) {
        for (const unsigned int attempts : {0u, 1u, 3u}) {
            const auto context = MakeComObject<MockTfContext>();
            const auto document = MakeComObject<MockTfDocumentMgr>();
            const auto composition = MakeComObject<MockTfComposition>(context->range_);
            const auto sink = MakeComObject<ResetLayoutSink>();
            CompositionLifecycleState state(491);
            state.SetActive(true);
            state.SetContext(context);
            state.SetDocument(document);
            state.SetComposition(composition);
            ScopedTfSinkConnection layout_sink;
            QIQI_TEST_CHECK(layout_sink.Advise(state.Context().Get(), IID_ITfTextLayoutSink, sink.Get()));
            bool has_caret = true;
            unsigned int scheduled = 0;
            unsigned int ended_ui = 0;
            const auto callback = [&](Boundary current) {
                QIQI_TEST_CHECK(state.IsResettingContext());
                if (current != boundary) return;
                if (attempts == 0) throw std::runtime_error("injected reset failure");
                for (unsigned int index = 0; index < attempts; ++index) {
                    const auto context_epoch = state.Epoch().context;
                    ResetCompositionContextAndUI(
                        state, false, has_caret, layout_sink, 491, GUID_NULL,
                        [] { QIQI_TEST_CHECK(false); }, [] { QIQI_TEST_CHECK(false); });
                    QIQI_TEST_CHECK(state.IsResettingContext() && state.Epoch().context == context_epoch + 1 &&
                                    has_caret == (current == Boundary::Schedule) &&
                                    layout_sink.IsConnected() == (current == Boundary::Schedule));
                }
            };
            bool caught = false;
            try {
                ResetCompositionContextAndUI(
                    state, true, has_caret, layout_sink, 491, GUID_NULL,
                    [&] { ++scheduled; callback(Boundary::Schedule); },
                    [&] { ++ended_ui; callback(Boundary::UI); });
            } catch (const std::runtime_error&) {
                caught = true;
            }
            const bool untouched = attempts == 0 && boundary == Boundary::Schedule;
            QIQI_TEST_CHECK(caught == (attempts == 0) && scheduled == 1 && ended_ui == (untouched ? 0u : 1u) &&
                            !state.IsResettingContext() && state.IsActive() && state.Epoch().context == uint64_t{1} + attempts &&
                            state.Context().Get() == (untouched ? context.Get() : nullptr) &&
                            state.Document().Get() == (untouched ? document.Get() : nullptr) &&
                            state.Composition().Get() == (untouched ? composition.Get() : nullptr) &&
                            has_caret == untouched && layout_sink.IsConnected() == untouched &&
                            context->layout_unadvise_calls_ == (untouched ? 0u : 1u) &&
                            context->request_edit_session_calls_ == 0 && !composition->ended_);
            const ULONG context_references = untouched ? 3UL : 1UL;
            const ULONG other_references = untouched ? 2UL : 1UL;
            QIQI_TEST_CHECK(context->AddRef() == context_references + 1 && context->Release() == context_references &&
                            document->AddRef() == other_references + 1 && document->Release() == other_references &&
                            composition->AddRef() == other_references + 1 && composition->Release() == other_references);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 6 && g_com_object_count.load() == objects_before);
}

void TestContextResetFinalReleaseOrder() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Event { Context, Composition, Range, UI };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool connected : {false, true}) {
        for (const bool has_composition : {false, true}) {
            std::vector<Event> events;
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext();
            auto composition = has_composition ? MakeComObject<MockTfComposition>(context->range_) : ComPtr<MockTfComposition>{};
            const auto sink = MakeComObject<ResetLayoutSink>();
            CompositionLifecycleState state(499);
            state.SetActive(true);
            state.SetContext(context);
            state.SetDocument(document);
            state.SetComposition(composition);
            ScopedTfSinkConnection layout_sink;
            if (connected) QIQI_TEST_CHECK(layout_sink.Advise(state.Context().Get(), IID_ITfTextLayoutSink, sink.Get()));
            bool has_caret = true;
            const auto* const raw_context = context.Get();
            const auto document_link = context->document_link_;
            context->release_observer_ = [&]() noexcept {
                events.push_back(Event::Context);
                QIQI_TEST_CHECK(state.IsResettingContext() && !state.Context() && has_caret == !connected &&
                                document_link.expired() == connected &&
                                raw_context->layout_unadvise_calls_ == (connected ? 1u : 0u));
            };
            context->range_->release_observer_ = [&]() noexcept {
                events.push_back(Event::Range);
                QIQI_TEST_CHECK(state.IsResettingContext() && document_link.expired() == (connected || has_composition));
            };
            if (composition) {
                composition->release_observer_ = [&]() noexcept {
                    events.push_back(Event::Composition);
                    QIQI_TEST_CHECK(state.IsResettingContext() && !state.Context() && !state.Document() &&
                                    !state.Composition() && !has_caret && document_link.expired());
                };
            }
            context.Reset();
            document.Reset();
            composition.Reset();
            ResetCompositionContextAndUI(
                state, false, has_caret, layout_sink, 499, GUID_NULL,
                [] { QIQI_TEST_CHECK(false); },
                [&] {
                    events.push_back(Event::UI);
                    QIQI_TEST_CHECK(state.IsResettingContext() && !state.Context() && !state.Document() &&
                                    !state.Composition() && !has_caret && !layout_sink.IsConnected() && document_link.expired());
                });
            std::vector<Event> expected;
            if (!connected) {
                expected.push_back(Event::Context);
                if (!has_composition) expected.push_back(Event::Range);
            }
            if (has_composition) expected.push_back(Event::Composition);
            if (connected) expected.push_back(Event::Context);
            if (connected || has_composition) expected.push_back(Event::Range);
            expected.push_back(Event::UI);
            QIQI_TEST_CHECK(events == expected && !state.IsResettingContext() && state.Epoch().context == 1);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 4 && g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionContextResetTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestContextResetProtocol();
    TestContextResetCallbackBoundaries();
    TestContextResetFinalReleaseOrder();
}

} // namespace qiqi::test
