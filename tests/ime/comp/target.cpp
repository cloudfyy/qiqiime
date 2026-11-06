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

} // namespace

void RunTsfCompositionTargetTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestTargetPreparationBoundaries();
    TestTargetPreparationUnwinding();
    TestTargetPreparationIntegration();
    RunTsfCompositionTargetEntryTests();
}

} // namespace qiqi::test
