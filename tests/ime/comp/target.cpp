#include "common/test_support.h"
#include "ime/comp/target.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
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
    enum class Boundary { Reset, Document };
    const auto objects_before = g_com_object_count.load();
    for (const auto boundary : {Boundary::Reset, Boundary::Document}) {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        const auto document_link = context->document_link_;
        const auto sink = MakeComObject<TargetLayoutSink>();
        CompositionLifecycleState state(151);
        state.SetActive(true);
        Microsoft::WRL::ComPtr<ITfContext> preparing;
        ScopedTfSinkConnection layout;
        unsigned int releases = 0;
        context->release_observer_ = [&]() noexcept {
            QIQI_TEST_CHECK(!preparing && document_link.expired());
            ++releases;
        };
        context->get_document_callback_ = [&] {
            document.Reset();
            throw std::runtime_error("injected target preparation");
        };
        bool caught = false;
        try {
            std::ignore = PrepareCompositionTarget(
                gsl::not_null<ITfContext*>(context.Get()), state, preparing, layout,
                gsl::not_null<ITfTextLayoutSink*>(sink.Get()), 151, GUID_NULL,
                [&] {
                    const CompositionContextResetScope reset_scope(state);
                    QIQI_TEST_CHECK(reset_scope.Entered() && preparing.Get() == context.Get());
                    context.Reset();
                    QIQI_TEST_CHECK(releases == 0);
                    if (boundary == Boundary::Reset) {
                        document.Reset();
                        throw std::runtime_error("injected target preparation");
                    }
                },
                [&](EditSessionTicket ticket) noexcept { return state.IsCurrent(ticket); });
        } catch (const std::runtime_error& error) {
            QIQI_TEST_CHECK(std::string_view(error.what()) == "injected target preparation");
            caught = true;
        }
        QIQI_TEST_CHECK(caught && releases == 1 && !context && !document && !preparing &&
                        !state.Context() && !state.Document() && !state.IsResettingContext() &&
                        state.Epoch().context == 1 && !layout.IsConnected());
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionTargetTests() {
    TestTargetPreparationBoundaries();
    TestTargetPreparationUnwinding();
}

} // namespace qiqi::test
