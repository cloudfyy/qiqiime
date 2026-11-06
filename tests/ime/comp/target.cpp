#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/target.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace qiqi::test {

namespace {

class TargetLayoutSink final : public ComObjectBase<TargetLayoutSink, ITfTextLayoutSink> {
public:
    STDMETHODIMP OnLayoutChange(ITfContext*, TfLayoutCode, ITfContextView*) noexcept override { return S_OK; }
};

void TestTargetPreparationBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { None, Reset, Document, Advise };
    enum class Step { Reset, Check, Document, Advise };
    struct DocumentResult {
        HRESULT result;
        bool null_output;
    };
    const std::array outcomes{
        DocumentResult{S_OK, false}, DocumentResult{S_FALSE, false},
        DocumentResult{E_ACCESSDENIED, false}, DocumentResult{S_OK, true},
        DocumentResult{E_ACCESSDENIED, true},
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto& outcome : outcomes) {
        for (const HRESULT advise_result : {S_OK, E_ACCESSDENIED}) {
            for (const auto boundary : {Boundary::None, Boundary::Reset, Boundary::Document, Boundary::Advise}) {
                if (boundary == Boundary::Advise && FAILED(advise_result)) continue;
                auto document = MakeComObject<MockTfDocumentMgr>();
                auto context = document->CreateTestContext();
                const auto old_context = MakeComObject<MockTfContext>();
                const auto old_document = MakeComObject<MockTfDocumentMgr>();
                const auto sink = MakeComObject<TargetLayoutSink>();
                CompositionLifecycleState state(149);
                state.SetActive(true);
                state.InvalidateActivation();
                state.SetContext(old_context);
                state.SetDocument(old_document);
                Microsoft::WRL::ComPtr<ITfContext> preparing;
                ScopedTfSinkConnection layout;
                std::vector<Step> trace;
                context->get_document_result_ = outcome.result;
                context->return_null_document_ = outcome.null_output;
                context->layout_advise_result_ = advise_result;
                context->get_document_callback_ = [&] {
                    trace.push_back(Step::Document);
                    QIQI_TEST_CHECK(preparing.Get() == context.Get() && !state.Context() && !state.Document());
                    if (boundary == Boundary::Document) state.InvalidateContext();
                };
                context->query_interface_observer_ = [&](REFIID iid) {
                    if (!IsEqualIID(iid, IID_ITfSource)) return;
                    trace.push_back(Step::Advise);
                    QIQI_TEST_CHECK(preparing.Get() == context.Get() && state.Context().Get() == context.Get());
                    QIQI_TEST_CHECK(state.Document().Get() == (outcome.null_output ? nullptr : document.Get()));
                };
                context->layout_advise_callback_ = [&] {
                    QIQI_TEST_CHECK(!layout.IsConnected() && preparing.Get() == context.Get());
                    if (boundary == Boundary::Advise) state.InvalidateContext();
                };
                auto reset = [&, once = std::make_unique<bool>(false)] {
                    QIQI_TEST_CHECK(!*once && preparing.Get() == context.Get());
                    *once = true;
                    trace.push_back(Step::Reset);
                    QIQI_TEST_CHECK(context->AddRef() == 4 && context->Release() == 3);
                    const CompositionContextResetScope reset_scope(state);
                    QIQI_TEST_CHECK(reset_scope.Entered());
                    state.ClearContext();
                    state.ClearDocument();
                    layout.Reset();
                    if (boundary == Boundary::Reset) state.InvalidateContext();
                };
                auto current = [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                    trace.push_back(Step::Check);
                    ++*checks;
                    QIQI_TEST_CHECK(preparing.Get() == context.Get() && ticket.epoch.activation == 1 &&
                                    ticket.epoch.context == 1);
                    QIQI_TEST_CHECK(ticket.target == (*checks == 3 ? context.Get() : nullptr));
                    return state.IsCurrent(ticket);
                };
                const bool ready = PrepareCompositionTarget(
                    gsl::not_null<ITfContext*>(context.Get()), state, preparing, layout,
                    gsl::not_null<ITfTextLayoutSink*>(sink.Get()), 149, GUID_NULL,
                    std::move(reset), std::move(current));
                const bool bound = boundary == Boundary::None || boundary == Boundary::Advise;
                const bool connected = boundary == Boundary::None && SUCCEEDED(advise_result);
                std::vector<Step> expected{Step::Reset, Step::Check};
                if (boundary != Boundary::Reset) {
                    expected.push_back(Step::Document);
                    expected.push_back(Step::Check);
                    if (boundary != Boundary::Document) {
                        expected.push_back(Step::Advise);
                        expected.push_back(Step::Check);
                    }
                }
                QIQI_TEST_CHECK(ready == (boundary == Boundary::None) && trace == expected);
                QIQI_TEST_CHECK(!preparing && !state.IsResettingContext() && state.IsActive());
                QIQI_TEST_CHECK(state.Epoch().activation == 1 &&
                                state.Epoch().context == (boundary == Boundary::None ? 1u : 2u));
                QIQI_TEST_CHECK(state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                state.Document().Get() == (bound && !outcome.null_output ? document.Get() : nullptr));
                QIQI_TEST_CHECK(layout.IsConnected() == connected && (context->layout_sink_ != nullptr) == connected);
                QIQI_TEST_CHECK(context->get_document_calls_ == (boundary == Boundary::Reset ? 0u : 1u) &&
                                context->layout_advise_calls_ == (bound ? 1u : 0u) &&
                                context->layout_unadvise_calls_ == (boundary == Boundary::Advise ? 1u : 0u));
                const ULONG context_refs = 1UL + (bound ? 1UL : 0UL) + (connected ? 1UL : 0UL);
                const ULONG document_refs = 1UL + (bound && !outcome.null_output ? 1UL : 0UL);
                QIQI_TEST_CHECK(context->AddRef() == context_refs + 1 && context->Release() == context_refs &&
                                document->AddRef() == document_refs + 1 && document->Release() == document_refs);
                QIQI_TEST_CHECK(old_context->AddRef() == 2 && old_context->Release() == 1 &&
                                old_document->AddRef() == 2 && old_document->Release() == 1);
                QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                                context->range_->set_text_calls_ == 0 && state.Cleanups().TrackedCount() == 0);
                context->get_document_callback_ = {};
                context->query_interface_observer_ = {};
                context->layout_advise_callback_ = {};
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 35 && g_com_object_count.load() == objects_before);
}

void TestTargetPreparationUnwinding() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { Reset, Document };
    struct Case { Boundary boundary; bool entry; };
    const std::array cases{
        Case{Boundary::Reset, false}, Case{Boundary::Document, false},
        Case{Boundary::Reset, true}, Case{Boundary::Document, true},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        const auto document_link = context->document_link_;
        const auto sink = MakeComObject<TargetLayoutSink>();
        CompositionLifecycleState state(151);
        state.SetActive(true);
        Microsoft::WRL::ComPtr<ITfContext> preparing;
        ScopedTfSinkConnection layout;
        const GUID original_activity{0x123, 0x234, 0x345, {1, 2, 3, 4, 5, 6, 7, 8}};
        GUID activity = original_activity;
        unsigned int releases = 0;
        context->release_observer_ = [&]() noexcept {
            QIQI_TEST_CHECK(!preparing && document_link.expired() &&
                            IsEqualGUID(activity, original_activity) == !test.entry);
            ++releases;
        };
        context->get_document_callback_ = [&] {
            document.Reset();
            throw std::runtime_error("injected target preparation");
        };
        bool caught = false;
        try {
            const auto reset = [&] {
                const CompositionContextResetScope reset_scope(state);
                QIQI_TEST_CHECK(reset_scope.Entered() && preparing.Get() == context.Get());
                context.Reset();
                QIQI_TEST_CHECK(releases == 0);
                if (test.boundary == Boundary::Reset) {
                    document.Reset();
                    throw std::runtime_error("injected target preparation");
                }
            };
            const auto current = [&](EditSessionTicket ticket) noexcept { return state.IsCurrent(ticket); };
            if (test.entry) {
                std::ignore = PrepareCompositionInputTarget(
                    context.Get(), state, preparing, layout, gsl::not_null<ITfTextLayoutSink*>(sink.Get()),
                    151, activity, std::ref(reset), std::ref(current));
            } else {
                std::ignore = PrepareCompositionTarget(
                    gsl::not_null<ITfContext*>(context.Get()), state, preparing, layout,
                    gsl::not_null<ITfTextLayoutSink*>(sink.Get()), 151, GUID_NULL,
                    std::ref(reset), std::ref(current));
            }
        } catch (const std::runtime_error& error) {
            QIQI_TEST_CHECK(std::string_view(error.what()) == "injected target preparation");
            caught = true;
        }
        QIQI_TEST_CHECK(caught && releases == 1 && !context && !document && !preparing &&
                        !state.Context() && !state.Document() && !state.IsResettingContext() &&
                        state.Epoch().context == 1 && !layout.IsConnected() && IsEqualGUID(activity, original_activity));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestTargetPreparationIntegration() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Target { Missing, Same, Other, Unbound };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool resetting : {false, true}) {
            for (const bool pending : {false, true}) {
                for (const auto target : {Target::Missing, Target::Same, Target::Other, Target::Unbound}) {
                    const auto document = MakeComObject<MockTfDocumentMgr>();
                    const auto context = document->CreateTestContext();
                    const auto old_context = MakeComObject<MockTfContext>();
                    const auto sink = MakeComObject<TargetLayoutSink>();
                    CompositionLifecycleState state(733);
                    state.SetActive(active);
                    if (target != Target::Unbound) state.SetContext(old_context);
                    ComPtr<ITfContext> preparing;
                    if (pending) preparing = old_context;
                    ScopedTfSinkConnection layout;
                    std::optional<CompositionContextResetScope> reset_scope;
                    if (resetting) reset_scope.emplace(state);
                    const GUID original_activity{0x123, 0x234, 0x345, {1, 2, 3, 4, 5, 6, 7, 8}};
                    GUID activity = original_activity;
                    unsigned int resets_seen = 0;
                    unsigned int checks_seen = 0;
                    unsigned int document_calls = 0;
                    context->get_document_callback_ = [&] {
                        ++document_calls;
                        QIQI_TEST_CHECK(resets_seen == 1 && checks_seen == 1 && preparing.Get() == context.Get() &&
                                        !state.Context() && !IsEqualGUID(activity, original_activity));
                    };
                    ITfContext* input = target == Target::Missing ? nullptr :
                        target == Target::Same ? old_context.Get() : context.Get();
                    const bool ready = PrepareCompositionInputTarget(
                        input, state, preparing, layout, gsl::not_null<ITfTextLayoutSink*>(sink.Get()), 733, activity,
                        [&, resets = std::make_unique<unsigned int>(0)] {
                            resets_seen = ++*resets;
                            QIQI_TEST_CHECK(resets_seen == 1 && preparing.Get() == context.Get() &&
                                            !IsEqualGUID(activity, original_activity));
                            const CompositionContextResetScope scope(state);
                            QIQI_TEST_CHECK(scope.Entered());
                            state.ClearContext();
                            state.ClearDocument();
                            layout.Reset();
                        },
                        [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                            checks_seen = ++*checks;
                            QIQI_TEST_CHECK(resets_seen == 1 && preparing.Get() == context.Get() &&
                                            ticket.Matches({0, 1}, checks_seen == 3 ? context.Get() : nullptr) &&
                                            !IsEqualGUID(activity, original_activity));
                            return state.IsCurrent(ticket);
                        });
                    const bool eligible = active && !resetting && !pending && target != Target::Missing;
                    const bool prepared = eligible && target != Target::Same;
                    QIQI_TEST_CHECK(ready == eligible && resets_seen == (prepared ? 1u : 0u) &&
                                    checks_seen == (prepared ? 3u : 0u) && document_calls == (prepared ? 1u : 0u) &&
                                    IsEqualGUID(activity, original_activity) && layout.IsConnected() == prepared &&
                                    preparing.Get() == (pending ? old_context.Get() : nullptr) &&
                                    state.IsActive() == active && state.IsResettingContext() == resetting &&
                                    state.Epoch().context == (prepared || resetting ? 1u : 0u) &&
                                    state.Context().Get() == (prepared ? context.Get() : target == Target::Unbound ? nullptr : old_context.Get()) &&
                                    state.Document().Get() == (prepared ? document.Get() : nullptr) &&
                                    context->layout_advise_calls_ == (prepared ? 1u : 0u) &&
                                    context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
                    context->get_document_callback_ = {};
                    ++cases;
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 32 && g_com_object_count.load() == objects_before);
}

void TestDocumentIdentityMatching() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Incoming { Missing, SamePointer, SameIdentity, Different };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool bound : {false, true}) {
            for (const auto incoming : {Incoming::Missing, Incoming::SamePointer, Incoming::SameIdentity, Incoming::Different}) {
                for (const bool current_fails : {false, true}) {
                    for (const bool incoming_fails : {false, true}) {
                        const auto document = MakeComObject<MockTfDocumentMgr>();
                        const auto other_document = MakeComObject<MockTfDocumentMgr>();
                        const auto current = MakeComObject<MockTfDocumentMgrAlias>(
                            gsl::not_null<ITfDocumentMgr*>(document.Get()));
                        const auto alias = MakeComObject<MockTfDocumentMgrAlias>(
                            gsl::not_null<ITfDocumentMgr*>(incoming == Incoming::Different ? other_document.Get() : document.Get()));
                        CompositionLifecycleState state(641);
                        state.SetActive(active);
                        if (bound) state.SetDocument(current);
                        current->identity_result = current_fails ? E_ACCESSDENIED : S_OK;
                        alias->identity_result = incoming_fails ? E_ACCESSDENIED : S_OK;
                        std::array<unsigned int, 2> queries{};
                        size_t query_count = 0;
                        current->identity_observer = [&]() noexcept {
                            QIQI_TEST_CHECK(query_count < queries.size());
                            gsl::at(queries, query_count++) = 1;
                        };
                        alias->identity_observer = [&]() noexcept {
                            QIQI_TEST_CHECK(query_count < queries.size());
                            gsl::at(queries, query_count++) = 2;
                        };
                        ITfDocumentMgr* target = incoming == Incoming::Missing ? nullptr :
                            incoming == Incoming::SamePointer ? current.Get() : alias.Get();
                        const bool matches = bound && (incoming == Incoming::SamePointer ||
                            (incoming == Incoming::SameIdentity && !current_fails && !incoming_fails));
                        QIQI_TEST_CHECK(MatchesCompositionDocument(target, state) == matches &&
                                        state.IsActive() == active && state.Document().Get() == (bound ? current.Get() : nullptr) &&
                                        state.Epoch().activation == 0 && state.Epoch().context == 0 &&
                                        document->get_top_calls == 0 && document->get_base_calls == 0);
                        QIQI_TEST_CHECK(current->AddRef() == (bound ? 3UL : 2UL) &&
                                        current->Release() == (bound ? 2UL : 1UL) &&
                                        alias->AddRef() == 2 && alias->Release() == 1);
                        const bool queried = bound && incoming != Incoming::Missing && incoming != Incoming::SamePointer;
                        QIQI_TEST_CHECK(query_count == (queried ? current_fails ? 1u : 2u : 0u));
                        if (queried) {
                            QIQI_TEST_CHECK(gsl::at(queries, 0) == 1 &&
                                            (current_fails || gsl::at(queries, 1) == 2));
                        }
                        current->identity_observer = {};
                        alias->identity_observer = {};
                        ++cases;
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 64 && g_com_object_count.load() == objects_before);
}

void TestDocumentTargetReconciliation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Shape { Empty, Unrelated, TargetTop, TargetBase, TargetBoth };
    enum class Boundary { None, Top, Base, Reset };
    enum class Step { Top, Base, Check, Reset };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto shape : {Shape::Empty, Shape::Unrelated, Shape::TargetTop, Shape::TargetBase, Shape::TargetBoth}) {
        for (const HRESULT top_hr : {S_OK, S_FALSE, E_ACCESSDENIED}) {
            for (const HRESULT base_hr : {S_OK, E_ACCESSDENIED}) {
                for (const auto boundary : {Boundary::None, Boundary::Top, Boundary::Base, Boundary::Reset}) {
                    const auto document = MakeComObject<MockTfDocumentMgr>();
                    const auto old_document = MakeComObject<MockTfDocumentMgr>();
                    const auto context = document->CreateTestContext();
                    const auto other_base = document->CreateTestContext();
                    const auto other_top = document->CreateTestContext();
                    if (shape != Shape::Empty) {
                        auto* base = shape == Shape::TargetBase || shape == Shape::TargetBoth ? context.Get() : other_base.Get();
                        auto* top = shape == Shape::TargetTop || shape == Shape::TargetBoth ? context.Get() : other_top.Get();
                        QIQI_TEST_CHECK(document->Push(base) == S_OK);
                        if (base != top) QIQI_TEST_CHECK(document->Push(top) == S_OK);
                    }
                    CompositionLifecycleState state(643);
                    state.SetActive(true);
                    state.InvalidateActivation();
                    state.SetContext(context);
                    state.SetDocument(old_document);
                    std::vector<Step> trace;
                    document->get_top_result = top_hr;
                    document->get_base_result = base_hr;
                    document->get_top_callback = [&] {
                        trace.push_back(Step::Top);
                        if (boundary == Boundary::Top) state.InvalidateContext();
                    };
                    document->get_base_callback = [&] {
                        trace.push_back(Step::Base);
                        if (boundary == Boundary::Base) state.InvalidateContext();
                    };
                    unsigned int resets = 0;
                    unsigned int checks = 0;
                    auto reset = [&, once = std::make_unique<bool>(false)] {
                        QIQI_TEST_CHECK(!*once && state.Context().Get() == context.Get() && state.Document() == old_document);
                        *once = true;
                        ++resets;
                        trace.push_back(Step::Reset);
                        const CompositionContextResetScope reset_scope(state);
                        state.ClearContext();
                        state.ClearDocument();
                        if (boundary == Boundary::Reset) state.InvalidateContext();
                    };
                    auto current = [&, calls = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                        trace.push_back(Step::Check);
                        checks = ++*calls;
                        QIQI_TEST_CHECK(document->get_top_calls == 1 && document->get_base_calls == 1 &&
                                        ticket.epoch.activation == 1 && ticket.epoch.context == (checks == 1 ? 0u : 1u) &&
                                        ticket.target == (checks == 1 ? context.Get() : nullptr));
                        return state.IsCurrent(ticket);
                    };
                    const bool ready = ReconcileCompositionDocumentTarget(
                        document.Get(), "component", state, 643, GUID_NULL, std::move(reset), std::move(current));
                    const bool stale = (boundary == Boundary::Top && SUCCEEDED(top_hr)) ||
                        (boundary == Boundary::Base && SUCCEEDED(base_hr));
                    const bool retained = SUCCEEDED(top_hr) && SUCCEEDED(base_hr) &&
                        (shape == Shape::TargetTop || shape == Shape::TargetBase || shape == Shape::TargetBoth);
                    const bool should_reset = !stale && !retained;
                    std::vector<Step> expected;
                    if (SUCCEEDED(top_hr)) expected.push_back(Step::Top);
                    if (SUCCEEDED(base_hr)) expected.push_back(Step::Base);
                    expected.push_back(Step::Check);
                    if (should_reset) {
                        expected.push_back(Step::Reset);
                        expected.push_back(Step::Check);
                    }
                    QIQI_TEST_CHECK(ready == (!stale && (!should_reset || boundary != Boundary::Reset)) &&
                                    trace == expected && resets == (should_reset ? 1u : 0u) &&
                                    checks == (should_reset ? 2u : 1u) && state.IsActive() && !state.IsResettingContext() &&
                                    state.Context().Get() == (should_reset ? nullptr : context.Get()) &&
                                    state.Document().Get() == (should_reset ? nullptr : stale ? old_document.Get() : document.Get()) &&
                                    context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
                    document->get_top_callback = {};
                    document->get_base_callback = {};
                    ++cases;
                }
            }
        }
    }
    for (const bool bound : {false, true}) {
        for (const bool has_document : {false, true}) {
            if (bound && has_document) continue;
            const auto context = MakeComObject<MockTfContext>();
            const auto document = MakeComObject<MockTfDocumentMgr>();
            CompositionLifecycleState state(645);
            if (bound) state.SetContext(context);
            unsigned int callbacks = 0;
            QIQI_TEST_CHECK(ReconcileCompositionDocumentTarget(
                has_document ? document.Get() : nullptr, "missing", state, 645, GUID_NULL,
                [&] { ++callbacks; }, [&](EditSessionTicket) { ++callbacks; return false; }));
            QIQI_TEST_CHECK(callbacks == 0 && document->get_top_calls == 0 && document->get_base_calls == 0 &&
                            state.Context().Get() == (bound ? context.Get() : nullptr) && !state.Document());
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 123 && g_com_object_count.load() == objects_before);
}

void TestDocumentTargetUnwinding() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { None, Check, Reset, Recheck };
    const auto objects_before = g_com_object_count.load();
    for (const auto boundary : {Boundary::None, Boundary::Check, Boundary::Reset, Boundary::Recheck}) {
        const auto document = MakeComObject<MockTfDocumentMgr>();
        auto old_document = MakeComObject<MockTfDocumentMgr>();
        auto context = old_document->CreateTestContext();
        const auto document_link = context->document_link_;
        const auto base = document->CreateTestContext();
        const auto top = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(base.Get()) == S_OK && document->Push(top.Get()) == S_OK);
        CompositionLifecycleState state(647);
        state.SetActive(true);
        state.SetContext(context);
        state.SetDocument(old_document);
        unsigned int releases = 0;
        unsigned int checks = 0;
        context->release_observer_ = [&]() noexcept {
            QIQI_TEST_CHECK(!state.Context() && !state.Document() && document_link.expired() &&
                            base->AddRef() == 3 && base->Release() == 2 &&
                            top->AddRef() == 3 && top->Release() == 2);
            ++releases;
        };
        const auto clear = [&] {
            state.ClearContext();
            state.ClearDocument();
            context.Reset();
            old_document.Reset();
            QIQI_TEST_CHECK(releases == 0 && base->AddRef() == 4 && base->Release() == 3 &&
                            top->AddRef() == 4 && top->Release() == 3);
        };
        bool caught = false;
        try {
            const bool ready = ReconcileCompositionDocumentTarget(
                document.Get(), "unwind", state, 647, GUID_NULL,
                [&] {
                    state.InvalidateContext();
                    clear();
                    if (boundary == Boundary::Reset) throw std::runtime_error("injected document reconciliation");
                },
                [&](EditSessionTicket ticket) {
                    ++checks;
                    if (boundary == Boundary::Check || (boundary == Boundary::Recheck && checks == 2)) {
                        if (checks == 1) clear();
                        throw std::runtime_error("injected document reconciliation");
                    }
                    return state.IsCurrent(ticket);
                });
            QIQI_TEST_CHECK(ready && boundary == Boundary::None);
        } catch (const std::runtime_error& error) {
            QIQI_TEST_CHECK(std::string_view(error.what()) == "injected document reconciliation");
            caught = true;
        }
        QIQI_TEST_CHECK(caught == (boundary != Boundary::None) && releases == 1 && !context && !old_document &&
                        checks == (boundary == Boundary::Check || boundary == Boundary::Reset ? 1u : 2u) &&
                        state.Epoch().context == (boundary == Boundary::Check ? 0u : 1u));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionTargetTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestTargetPreparationBoundaries();
    TestTargetPreparationUnwinding();
    TestTargetPreparationIntegration();
    RunTsfCompositionTargetEntryTests();
    TestDocumentIdentityMatching();
    TestDocumentTargetReconciliation();
    TestDocumentTargetUnwinding();
}

} // namespace qiqi::test
