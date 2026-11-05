#include "common/test_support.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <type_traits>
#include <utility>

namespace qiqi::test {

namespace {

void TestLifecycleStateIdentity() {
    static_assert(!std::is_copy_constructible_v<CompositionLifecycleState>);
    static_assert(!std::is_move_constructible_v<CompositionLifecycleState>);
    static_assert(!std::is_copy_assignable_v<CompositionLifecycleState>);
    static_assert(!std::is_move_assignable_v<CompositionLifecycleState>);
    static_assert(std::is_nothrow_destructible_v<CompositionLifecycleState>);
    const auto objects_before = g_com_object_count.load();
    {
        const auto first = MakeComObject<MockTfContext>();
        const auto second = MakeComObject<MockTfContext>();
        size_t cases = 0;
        for (const bool bound : {false, true}) {
            CompositionLifecycleState state(107);
            QIQI_TEST_CHECK(!state.IsActive() && !state.Context() && !state.Document() && !state.Composition() &&
                            state.Epoch().activation == 0 && state.Epoch().context == 0);
            const auto& live_epoch = state.Epoch();
            const auto* const cleanups = &state.Cleanups();
            state.InvalidateActivation();
            state.InvalidateContext();
            if (bound) state.SetContext(first);
            struct Case {
                EditSessionTicket ticket;
                bool matches;
            };
            const std::array identities{
                Case{{{1, 1}, state.Context().Get()}, true},
                Case{{{0, 1}, state.Context().Get()}, false},
                Case{{{1, 0}, state.Context().Get()}, false},
                Case{{{0, 0}, state.Context().Get()}, false},
                Case{{{1, 1}, second.Get()}, false},
                Case{{{1, 1}, nullptr}, !bound},
                Case{{{1, 2}, state.Context().Get()}, false},
            };
            for (const bool active : {false, true}) {
                state.SetActive(active);
                for (const bool resetting : {false, true}) {
                    for (const auto& test : identities) {
                        QIQI_TEST_CHECK(state.IsCurrent(test.ticket, resetting) ==
                                        (active && !resetting && test.matches));
                        ++cases;
                    }
                }
                QIQI_TEST_CHECK(state.Epoch().activation == 1 && state.Epoch().context == 1);
            }
            QIQI_TEST_CHECK(live_epoch.activation == 1 && live_epoch.context == 1 &&
                            &live_epoch == &state.Epoch() && cleanups == &state.Cleanups());
            const EditSessionTicket old{live_epoch, state.Context().Get()};
            state.InvalidateActivation();
            QIQI_TEST_CHECK(live_epoch.activation == 2 && !state.IsCurrent(old, false));
            state.InvalidateContext();
            QIQI_TEST_CHECK(live_epoch.context == 2 && !state.IsCurrent(old, false));
            state.SetActive(false);
            state.SetActive(true);
            QIQI_TEST_CHECK(!state.IsCurrent(old, false) && cleanups == &state.Cleanups());
        }
        QIQI_TEST_CHECK(cases == 56);
        for (const auto& context : {first, second}) {
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                            context->insert_at_selection_calls_ == 0 && context->get_property_calls_ == 0);
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestLifecycleStateResources() {
    const auto objects_before = g_com_object_count.load();
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto document = MakeComObject<MockTfDocumentMgr>();
        const auto next_document = MakeComObject<MockTfDocumentMgr>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto next_composition = MakeComObject<MockTfComposition>(context->range_);
        {
            CompositionLifecycleState state(109);
            state.SetActive(true);
            state.SetContext(context);
            state.SetDocument(document);
            state.SetComposition(composition);
            QIQI_TEST_CHECK(state.Context().Get() == context.Get() && state.Document().Get() == document.Get() &&
                            state.Composition().Get() == composition.Get());
            QIQI_TEST_CHECK(context->AddRef() == 3 && context->Release() == 2);
            QIQI_TEST_CHECK(document->AddRef() == 3 && document->Release() == 2);
            QIQI_TEST_CHECK(composition->AddRef() == 3 && composition->Release() == 2);
            const CompositionLifecycleState& observed = state;
            QIQI_TEST_CHECK(&observed.Cleanups() == &state.Cleanups());
            const auto& published = state.Composition();
            auto detached = state.TakeComposition();
            QIQI_TEST_CHECK(!published && detached.Get() == composition.Get());
            state.SetComposition(next_composition);
            QIQI_TEST_CHECK(published.Get() == next_composition.Get() && detached.Get() == composition.Get());
            state.SetDocument(next_document);
            QIQI_TEST_CHECK(document->AddRef() == 2 && document->Release() == 1 &&
                            state.Document().Get() == next_document.Get());
            const auto starting = state.Cleanups().TrackStarting(context.Get());
            const EditSessionTicket ticket{state.Epoch(), context.Get()};
            state.InvalidateContext();
            state.ClearContext();
            QIQI_TEST_CHECK(!state.IsCurrent(ticket, false) && !state.Context() &&
                            state.Composition().Get() == next_composition.Get() &&
                            state.Document().Get() == next_document.Get());
            QIQI_TEST_CHECK(state.Cleanups().HasPending(context.Get(), GUID_NULL) &&
                            context->AddRef() == 3 && context->Release() == 2);
            state.ClearDocument();
            state.ClearComposition();
            QIQI_TEST_CHECK(!state.Document() && !published && detached.Get() == composition.Get());
            QIQI_TEST_CHECK(next_document->AddRef() == 2 && next_document->Release() == 1 &&
                            next_composition->AddRef() == 2 && next_composition->Release() == 1);
            state.Cleanups().MarkContextRemoved(context.Get(), GUID_NULL);
            QIQI_TEST_CHECK(starting->context_removed && starting->finished &&
                            !state.Cleanups().HasPending(context.Get(), GUID_NULL));
        }
        QIQI_TEST_CHECK(context->AddRef() == 2 && context->Release() == 1 &&
                        composition->AddRef() == 2 && composition->Release() == 1);
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                        composition->get_range_calls_ == 0 && composition->end_composition_calls_ == 0 &&
                        next_composition->get_range_calls_ == 0 && next_composition->end_composition_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestLifecycleStateReleaseOrder() {
    enum class Released { Context, Composition };
    const auto objects_before = g_com_object_count.load();
    {
        std::array<Released, 2> released{};
        size_t count = 0;
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto document_link = context->document_link_;
        context->release_observer_ = [&]() noexcept {
            QIQI_TEST_CHECK(document_link.expired() && count == 0);
            gsl::at(released, count++) = Released::Context;
        };
        composition->release_observer_ = [&]() noexcept {
            QIQI_TEST_CHECK(document_link.expired() && count == 1);
            gsl::at(released, count++) = Released::Composition;
        };
        {
            CompositionLifecycleState state(113);
            state.SetComposition(std::move(composition));
            state.SetContext(std::move(context));
            state.SetDocument(std::move(document));
            QIQI_TEST_CHECK(!composition && !context && !document && !document_link.expired() && count == 0);
        }
        QIQI_TEST_CHECK(document_link.expired() && count == 2 &&
                        (released == std::array{Released::Context, Released::Composition}));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestLifecycleStateDetachedCleanup() {
    const auto objects_before = g_com_object_count.load();
    for (const bool asynchronous : {false, true}) {
        const auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = MakeComObject<MockTfContext>(queue);
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        context->range_->text_ = L"preedit";
        context->allow_sync_edit_sessions_ = !asynchronous;
        {
            CompositionLifecycleState state(127);
            state.SetActive(true);
            state.InvalidateActivation();
            state.InvalidateContext();
            state.SetContext(context);
            state.SetComposition(composition);
            const auto* const cleanups = &state.Cleanups();
            state.Cleanups().Schedule(state.Context(), state.TakeComposition(), 41, state.Epoch(), GUID_NULL);
            QIQI_TEST_CHECK(!state.Composition() && queue->PendingCount() == (asynchronous ? 1u : 0u));
            state.SetActive(false);
            state.InvalidateActivation();
            state.InvalidateContext();
            state.ClearContext();
            QIQI_TEST_CHECK(cleanups == &state.Cleanups() &&
                            state.Cleanups().HasPending(context.Get(), GUID_NULL) == asynchronous);
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == (asynchronous ? 2u : 1u));
        }
        if (asynchronous) {
            QIQI_TEST_CHECK(!composition->ended_ && queue->PendingCount() == 1 &&
                            queue->PendingRequests().front().client_id == 41);
            QIQI_TEST_CHECK(queue->RunNext(137) == S_OK && queue->PendingCount() == 0);
        }
        QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1 &&
                        composition->last_end_cookie_ == (asynchronous ? TfEditCookie{137} : TfEditCookie{1}));
        QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->range_->text_ == L"preedit" &&
                        context->range_->set_text_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionStateTests() {
    TestLifecycleStateIdentity();
    TestLifecycleStateResources();
    TestLifecycleStateReleaseOrder();
    TestLifecycleStateDetachedCleanup();
}

} // namespace qiqi::test
