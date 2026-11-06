#include "common/test_support.h"
#include "ime/comp/state.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
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
            for (const bool active : {false, true}) {
                state.SetActive(active);
                for (const bool resetting : {false, true}) {
                    std::optional<CompositionContextResetScope> reset_scope;
                    if (resetting) reset_scope.emplace(state);
                    const auto& epoch = state.Epoch();
                    const std::array identities{
                        Case{{epoch, state.Context().Get()}, true},
                        Case{{{epoch.activation - 1, epoch.context}, state.Context().Get()}, false},
                        Case{{{epoch.activation, epoch.context - 1}, state.Context().Get()}, false},
                        Case{{{epoch.activation - 1, epoch.context - 1}, state.Context().Get()}, false},
                        Case{{epoch, second.Get()}, false},
                        Case{{epoch, nullptr}, !bound},
                        Case{{{epoch.activation, epoch.context + 1}, state.Context().Get()}, false},
                    };
                    QIQI_TEST_CHECK(state.IsResettingContext() == resetting);
                    for (const auto& test : identities) {
                        QIQI_TEST_CHECK(state.IsCurrent(test.ticket) ==
                                        (active && !resetting && test.matches));
                        ++cases;
                    }
                }
                QIQI_TEST_CHECK(!state.IsResettingContext() && state.Epoch().activation == 1);
            }
            QIQI_TEST_CHECK(live_epoch.activation == 1 && live_epoch.context == 3 &&
                            &live_epoch == &state.Epoch() && cleanups == &state.Cleanups());
            const EditSessionTicket old{live_epoch, state.Context().Get()};
            state.InvalidateActivation();
            QIQI_TEST_CHECK(live_epoch.activation == 2 && !state.IsCurrent(old));
            state.InvalidateContext();
            QIQI_TEST_CHECK(live_epoch.context == 4 && !state.IsCurrent(old));
            state.SetActive(false);
            state.SetActive(true);
            QIQI_TEST_CHECK(!state.IsCurrent(old) && cleanups == &state.Cleanups());
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
            QIQI_TEST_CHECK(!state.IsCurrent(ticket) && !state.Context() &&
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

void TestLifecycleContextResetScopes() {
    static_assert(!std::is_copy_constructible_v<CompositionContextResetScope>);
    static_assert(!std::is_move_constructible_v<CompositionContextResetScope>);
    static_assert(std::is_nothrow_destructible_v<CompositionContextResetScope>);
    enum class Exit { Normal, Early, Exception };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool bound : {false, true}) {
            for (const unsigned int depth : {0u, 1u, 3u}) {
                for (const auto exit : {Exit::Normal, Exit::Early, Exit::Exception}) {
                    const auto context = MakeComObject<MockTfContext>();
                    const auto document = MakeComObject<MockTfDocumentMgr>();
                    const auto composition = MakeComObject<MockTfComposition>(context->range_);
                    CompositionLifecycleState state(131);
                    state.SetActive(active);
                    if (bound) state.SetContext(context);
                    state.SetDocument(document);
                    state.SetComposition(composition);
                    const auto starting = state.Cleanups().TrackStarting(context.Get());
                    const auto* const cleanups = &state.Cleanups();
                    const auto& epoch = state.Epoch();
                    const EditSessionTicket before{epoch, state.Context().Get()};
                    unsigned int entries = 0;
                    const auto nest = [&](auto&& self, unsigned int remaining) noexcept -> void {
                        if (remaining == 0) return;
                        const auto previous = epoch.context;
                        {
                            const CompositionContextResetScope nested(state);
                            QIQI_TEST_CHECK(!nested.Entered() && state.IsResettingContext() &&
                                            epoch.context == previous + 1);
                            QIQI_TEST_CHECK(!state.IsCurrent({epoch, state.Context().Get()}));
                            self(self, remaining - 1);
                        }
                        QIQI_TEST_CHECK(state.IsResettingContext());
                    };
                    const auto reset = [&] {
                        const CompositionContextResetScope outer(state);
                        QIQI_TEST_CHECK(outer.Entered() && state.IsResettingContext() && epoch.context == 1);
                        ++entries;
                        QIQI_TEST_CHECK(!state.IsCurrent({epoch, state.Context().Get()}));
                        nest(nest, depth);
                        QIQI_TEST_CHECK(epoch.context == uint64_t{1} + depth && state.IsResettingContext());
                        if (exit == Exit::Early) return;
                        if (exit == Exit::Exception) throw std::runtime_error("injected context reset");
                    };
                    bool caught = false;
                    try {
                        reset();
                    } catch (const std::runtime_error& error) {
                        QIQI_TEST_CHECK(exit == Exit::Exception &&
                                        std::string_view(error.what()) == "injected context reset");
                        caught = true;
                    }
                    QIQI_TEST_CHECK(caught == (exit == Exit::Exception) && entries == 1 &&
                                    !state.IsResettingContext() && state.IsActive() == active);
                    QIQI_TEST_CHECK(epoch.activation == 0 && epoch.context == uint64_t{1} + depth &&
                                    !state.IsCurrent(before) &&
                                    state.IsCurrent({epoch, state.Context().Get()}) == active);
                    QIQI_TEST_CHECK(state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                    state.Document().Get() == document.Get() &&
                                    state.Composition().Get() == composition.Get());
                    QIQI_TEST_CHECK(cleanups == &state.Cleanups() && state.Cleanups().TrackedCount() == 1 &&
                                    !starting->finished && !starting->context_removed && !starting->terminated);
                    const ULONG context_refs = 3UL + (bound ? 1UL : 0UL);
                    QIQI_TEST_CHECK(context->AddRef() == context_refs && context->Release() == context_refs - 1 &&
                                    document->AddRef() == 3 && document->Release() == 2 &&
                                    composition->AddRef() == 3 && composition->Release() == 2);
                    QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 &&
                                    context->range_->set_text_calls_ == 0 && context->prop_->clear_calls_ == 0 &&
                                    composition->get_range_calls_ == 0 && composition->end_composition_calls_ == 0);
                    ++cases;
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 36 && g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionStateTests() {
    TestLifecycleStateIdentity();
    TestLifecycleStateResources();
    TestLifecycleStateReleaseOrder();
    TestLifecycleStateDetachedCleanup();
    TestLifecycleContextResetScopes();
}

} // namespace qiqi::test
