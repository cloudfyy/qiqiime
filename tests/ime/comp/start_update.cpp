#include "common/test_support.h"
#include "ime/comp/start.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace qiqi::test {
namespace {

class StartUpdateSink final : public ComObjectBase<StartUpdateSink, ITfCompositionSink> {
public:
    explicit StartUpdateSink(CompositionLifecycleState& state) noexcept : state_(state) {}
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override {
        return state_.Cleanups().HandleTermination(ec, composition, GUID_NULL) ? S_OK : E_UNEXPECTED;
    }
private:
    CompositionLifecycleState& state_;
};

void TestStartPublicationAndUpdate() {
    enum class Kind {
        Existing, ExistingStale, ExistingReplace, ExistingRangeFailure,
        New, NewSFalse, NewRangeFailure, NewFailedOutput, NewNull, NewFailedNull, PreparationFailure,
        StaleStart, TerminatedCurrent, TerminatedStale, TerminatedReplace, NewReplace, LiveAtom,
    };
    constexpr std::array kinds{
        Kind::Existing, Kind::ExistingStale, Kind::ExistingReplace, Kind::ExistingRangeFailure,
        Kind::New, Kind::NewSFalse, Kind::NewRangeFailure, Kind::NewFailedOutput, Kind::NewNull, Kind::NewFailedNull,
        Kind::PreparationFailure, Kind::StaleStart, Kind::TerminatedCurrent,
        Kind::TerminatedStale, Kind::TerminatedReplace, Kind::NewReplace, Kind::LiveAtom,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto& text : {std::wstring{}, std::wstring{L"ab"}}) {
        for (const auto kind : kinds) {
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto context = MakeComObject<MockTfContext>(queue);
            context->range_->text_ = L"before";
            CompositionLifecycleState state(397);
            state.SetActive(kind != Kind::ExistingStale);
            state.SetContext(context);
            const auto sink = MakeComObject<StartUpdateSink>(state);
            const bool existing = kind == Kind::Existing || kind == Kind::ExistingStale ||
                kind == Kind::ExistingReplace || kind == Kind::ExistingRangeFailure;
            const bool returns_composition = existing || kind == Kind::New || kind == Kind::NewSFalse ||
                kind == Kind::NewRangeFailure || kind == Kind::NewReplace || kind == Kind::LiveAtom;
            const bool replaced = kind == Kind::ExistingReplace || kind == Kind::NewReplace;
            const bool range_failure = kind == Kind::ExistingRangeFailure || kind == Kind::NewRangeFailure;
            const bool writes = returns_composition && !replaced && kind != Kind::ExistingStale &&
                !range_failure;
            if (existing) {
                context->last_composition_ = MakeComObject<MockTfComposition>(context->range_, sink.Get());
                state.SetComposition(context->last_composition_);
                context->last_composition_->fail_get_range_ = kind == Kind::ExistingRangeFailure;
            }
            const EditSessionTicket ticket{state.Epoch(), context.Get()};
            TfGuidAtom atom = kind == Kind::LiveAtom ? TF_INVALID_GUIDATOM : TfGuidAtom{401};
            context->fail_qi_insert_ = kind == Kind::PreparationFailure;
            context->start_composition_result_ = kind == Kind::NewSFalse ? S_FALSE :
                kind == Kind::NewFailedOutput || kind == Kind::NewFailedNull ? E_ACCESSDENIED : S_OK;
            context->return_null_started_composition_ = kind == Kind::NewNull || kind == Kind::NewFailedNull;
            context->allow_sync_edit_sessions_ = kind != Kind::StaleStart;
            unsigned int range_queries = 0;
            const auto install_range_observer = [&] {
                context->last_composition_->get_range_observer_ = [&](ITfRange** output) {
                    QIQI_TEST_CHECK(output && state.Composition().Get() == context->last_composition_.Get());
                    ++range_queries;
                    // Start preparation and its record must have released before updating the published composition.
                    QIQI_TEST_CHECK(context->AddRef() == 3 && context->Release() == 2 &&
                                    context->range_->AddRef() == 3 && context->range_->Release() == 2);
                    if (replaced) {
                        state.SetComposition(MakeComObject<MockTfComposition>(MakeComObject<MockTfRange>()));
                    }
                };
            };
            if (existing) install_range_observer();
            context->start_composition_observer_ = [&](TfEditCookie ec, ITfRange* range, ITfCompositionSink* owner,
                                                       ITfComposition** output) {
                QIQI_TEST_CHECK(ec == 409 && range == context->range_.Get() && owner == sink.Get() && output &&
                                !state.Composition());
            };
            context->start_composition_callback_ = [&] {
                if (kind == Kind::NewRangeFailure) context->last_composition_->fail_get_range_ = true;
                if (kind == Kind::StaleStart) state.InvalidateContext();
                if (kind == Kind::LiveAtom) atom = 401;
                if (kind == Kind::TerminatedCurrent || kind == Kind::TerminatedStale || kind == Kind::TerminatedReplace) {
                    QIQI_TEST_CHECK(context->last_composition_->NotifyHostTermination(419) == S_OK);
                    if (kind == Kind::TerminatedStale) state.InvalidateContext();
                    if (kind == Kind::TerminatedReplace) {
                        state.SetComposition(MakeComObject<MockTfComposition>(MakeComObject<MockTfRange>()));
                    }
                }
                if (returns_composition) install_range_observer();
            };
            context->prop_->set_observer_ = [&](TfEditCookie ec, ITfRange* range, const VARIANT* value) {
                QIQI_TEST_CHECK(ec == 409 && range == context->range_.Get() && value &&
                                value->vt == VT_I4 && value->lVal == 401 &&
                                state.Composition().Get() == context->last_composition_.Get());
            };
            unsigned int checks_seen = 0;
            unsigned int resets_seen = 0;
            const auto result = EnsureAndUpdateCompositionInSession(
                409, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfCompositionSink*>(sink.Get()),
                state, 23, ticket.epoch, GUID_NULL, text, atom,
                [&, checks = std::make_unique<unsigned int>(0)](const ITfComposition* composition) {
                    checks_seen = ++*checks;
                    return state.IsCurrent(ticket) && state.Composition().Get() == composition;
                },
                [&, resets = std::make_unique<unsigned int>(0)] {
                    resets_seen = ++*resets;
                    QIQI_TEST_CHECK(kind == Kind::TerminatedCurrent && !state.Composition() &&
                                    context->AddRef() == 6 && context->Release() == 5);
                    state.InvalidateContext();
                    state.ClearContext();
                    state.ClearComposition();
                });
            const unsigned int update_checks = writes ? 6u :
                returns_composition && !range_failure ? 1u : 0u;
            const bool terminated = kind == Kind::TerminatedCurrent || kind == Kind::TerminatedStale ||
                kind == Kind::TerminatedReplace;
            const unsigned int expected_checks = (existing || kind == Kind::PreparationFailure ? 0u : 4u) +
                (terminated ? 1u : 0u) + update_checks;
            QIQI_TEST_CHECK(checks_seen == expected_checks && resets_seen == (kind == Kind::TerminatedCurrent ? 1u : 0u) &&
                            result.Get() == (returns_composition ? context->last_composition_.Get() : nullptr) &&
                            context->start_composition_calls_ == (existing || kind == Kind::PreparationFailure ? 0u : 1u) &&
                            range_queries == (returns_composition ? 1u : 0u) &&
                            context->range_->set_text_calls_ == (writes ? 1u : 0u) &&
                            context->range_->text_ == (writes ? text : L"before") &&
                            context->prop_->set_calls_ == (writes ? 1u : 0u));
            if (replaced || kind == Kind::TerminatedReplace) {
                QIQI_TEST_CHECK(state.Composition() && state.Composition().Get() != context->last_composition_.Get() &&
                                state.Epoch().activation == ticket.epoch.activation &&
                                state.Epoch().context == ticket.epoch.context);
            } else {
                QIQI_TEST_CHECK(state.Composition().Get() == result.Get());
            }
            if (returns_composition) {
                const ULONG references = replaced ? 2UL : 3UL;
                QIQI_TEST_CHECK(result->AddRef() == references + 1 && result->Release() == references);
            }
            if (kind == Kind::StaleStart) {
                QIQI_TEST_CHECK(context->request_edit_session_calls_ == 2 && queue->PendingCount() == 1);
                const auto& request = queue->PendingRequests().front();
                QIQI_TEST_CHECK(request.client_id == 23 && request.flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                QIQI_TEST_CHECK(queue->RunNext(421) == S_OK && context->last_composition_->ended_ &&
                                context->last_composition_->last_end_cookie_ == 421);
            } else {
                QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0);
            }
            QIQI_TEST_CHECK(!state.Cleanups().HasPending(context.Get(), GUID_NULL) && queue->PendingCount() == 0);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 34 && g_com_object_count.load() == objects_before);
}

void TestStartUpdateExceptionOwnership() {
    enum class Boundary { StartCheck, UpdateRange, UpdateCheck, Reset };
    const auto objects_before = g_com_object_count.load();
    for (const auto boundary : {Boundary::StartCheck, Boundary::UpdateRange, Boundary::UpdateCheck, Boundary::Reset}) {
        const auto context = MakeComObject<MockTfContext>();
        CompositionLifecycleState state(431);
        state.SetActive(true);
        state.SetContext(context);
        const auto sink = MakeComObject<StartUpdateSink>(state);
        const EditSessionTicket ticket{state.Epoch(), context.Get()};
        context->start_composition_callback_ = [&] {
            if (boundary == Boundary::Reset) {
                QIQI_TEST_CHECK(context->last_composition_->NotifyHostTermination(433) == S_OK);
            }
            context->last_composition_->get_range_observer_ = [&](ITfRange**) {
                if (boundary == Boundary::UpdateRange) {
                    QIQI_TEST_CHECK(state.Composition().Get() == context->last_composition_.Get() &&
                                    context->AddRef() == 3 && context->Release() == 2);
                    throw std::runtime_error("injected start/update failure");
                }
            };
        };
        unsigned int checks = 0;
        bool caught = false;
        try {
            static_cast<void>(EnsureAndUpdateCompositionInSession(
                439, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfCompositionSink*>(sink.Get()),
                state, 23, ticket.epoch, GUID_NULL, std::wstring{L"ab"}, TF_INVALID_GUIDATOM,
                [&](const ITfComposition* composition) {
                    ++checks;
                    if ((boundary == Boundary::StartCheck && checks == 4) ||
                        (boundary == Boundary::UpdateCheck && composition)) {
                        throw std::runtime_error("injected start/update failure");
                    }
                    return state.IsCurrent(ticket) && state.Composition().Get() == composition;
                },
                [&] {
                    QIQI_TEST_CHECK(boundary == Boundary::Reset && !state.Composition() &&
                                    context->AddRef() == 6 && context->Release() == 5);
                    throw std::runtime_error("injected start/update failure");
                }));
        } catch (const std::runtime_error&) {
            caught = true;
        }
        const bool published = boundary == Boundary::UpdateRange || boundary == Boundary::UpdateCheck;
        const ULONG composition_references = published ? 2UL : 1UL;
        QIQI_TEST_CHECK(caught && state.IsCurrent(ticket) &&
                        state.Composition().Get() == (published ? context->last_composition_.Get() : nullptr) &&
                        context->range_->set_text_calls_ == 0 && context->prop_->set_calls_ == 0 &&
                        context->AddRef() == 3 && context->Release() == 2 &&
                        context->range_->AddRef() == 3 && context->range_->Release() == 2 &&
                        context->last_composition_->AddRef() == composition_references + 1 &&
                        context->last_composition_->Release() == composition_references &&
                        !state.Cleanups().HasPending(context.Get(), GUID_NULL) &&
                        context->request_edit_session_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionStartUpdateTests() {
    TestStartPublicationAndUpdate();
    TestStartUpdateExceptionOwnership();
}

} // namespace qiqi::test
