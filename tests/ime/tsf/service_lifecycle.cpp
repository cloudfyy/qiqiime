#include "common/test_support.h"
#include "mocks.h"
#include "common/guids.h"
#include "common/file_util.h"
#include "ime/tsf/composition_attribute.h"
#include "ime/tsf/composition_cleanup.h"
#include "ime/tsf/composition_edit.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/tip_service.h"

#include <commctrl.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace qiqi::test {

namespace {

void TestEditSessionTickets() {
    static_assert(std::is_trivially_copyable_v<EditSessionTicket>);
    static_assert(std::is_nothrow_copy_constructible_v<EditSessionTicket>);
    const auto first = MakeComObject<MockTfContext>();
    const auto second = MakeComObject<MockTfContext>();
    const EditSessionTicket ticket{{7, 11}, first.Get()};
    struct Case {
        EditSessionEpoch epoch;
        const ITfContext* target;
        bool matches;
    };
    const std::array cases{
        Case{{7, 11}, first.Get(), true},
        Case{{8, 11}, first.Get(), false},
        Case{{7, 12}, first.Get(), false},
        Case{{8, 12}, first.Get(), false},
        Case{{7, 11}, second.Get(), false},
        Case{{8, 11}, second.Get(), false},
        Case{{7, 12}, second.Get(), false},
        Case{{7, 13}, first.Get(), false},
        Case{{7, 11}, nullptr, false},
    };
    for (const auto& test : cases) {
        QIQI_TEST_CHECK(ticket.Matches(test.epoch, test.target) == test.matches);
    }
    const EditSessionTicket detached{{7, 11}, nullptr};
    QIQI_TEST_CHECK(detached.Matches({7, 11}, nullptr));
    QIQI_TEST_CHECK(!detached.Matches({7, 11}, first.Get()));
    QIQI_TEST_CHECK(!detached.Matches({7, 12}, nullptr));
}

void TestCompositionEditTasks() {
    static_assert(std::is_copy_constructible_v<CompositionEditTask>);
    static_assert(std::is_nothrow_move_constructible_v<KeyProcessResult>);
    static_assert(std::is_nothrow_move_constructible_v<CompositionEditTask>);
    static_assert(!std::is_copy_assignable_v<CompositionEditTask>);
    static_assert(!std::is_move_assignable_v<CompositionEditTask>);
    const auto objects_before = g_com_object_count.load();
    constexpr GUID activity{0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 1, 2, 3, 4}};
    for (const auto action : {
             KeyActionType::PassThrough, KeyActionType::UpdateState, KeyActionType::Commit, KeyActionType::Cancel}) {
        ComPtr<LambdaEditSession> session;
        {
            auto context = MakeComObject<MockTfContext>();
            auto* const raw_context = context.Get();
            const EditSessionTicket ticket{{19, 29}, raw_context};
            KeyProcessResult snapshot;
            snapshot.action = action;
            snapshot.committed_text = L"commit";
            snapshot.composition_text = L"ab";
            snapshot.page_candidates = {L"third", L"fourth"};
            snapshot.all_display_candidates = {L"first", L"second", L"third", L"fourth", L"fifth"};
            snapshot.page_size = 2;
            snapshot.selected_index = 1;
            snapshot.current_page = 1;
            snapshot.total_pages = 3;
            snapshot.empty_4code_warning = true;
            snapshot.dup_4code_warning = true;
            snapshot.trigger_key_sound = true;
            const auto expected = snapshot;
            const CompositionEditTask task(context, snapshot, ticket, activity);
            CompositionEditTask copy = task;
            CompositionEditTask moved = std::move(copy);
            snapshot = {};
            const auto verify = [expected, ticket, raw_context, activity](const CompositionEditTask& current) {
                const auto& actual = current.Snapshot();
                QIQI_TEST_CHECK(std::tie(
                    actual.action, actual.committed_text, actual.composition_text, actual.page_candidates,
                    actual.all_display_candidates, actual.page_size, actual.selected_index, actual.current_page,
                    actual.total_pages, actual.empty_4code_warning, actual.dup_4code_warning, actual.trigger_key_sound) ==
                    std::tie(
                    expected.action, expected.committed_text, expected.composition_text, expected.page_candidates,
                    expected.all_display_candidates, expected.page_size, expected.selected_index, expected.current_page,
                    expected.total_pages, expected.empty_4code_warning, expected.dup_4code_warning, expected.trigger_key_sound));
                QIQI_TEST_CHECK(current.Context().Get() == raw_context &&
                                current.Ticket().Matches(ticket.epoch, raw_context));
                QIQI_TEST_CHECK(!current.Ticket().Matches({20, 29}, raw_context) &&
                                !current.Ticket().Matches({19, 30}, raw_context));
                QIQI_TEST_CHECK(IsEqualGUID(current.Activity(), activity));
                QIQI_TEST_CHECK(raw_context->request_edit_session_calls_ == 0 &&
                                raw_context->range_->set_text_calls_ == 0 && raw_context->prop_->clear_calls_ == 0);
            };
            verify(task);
            verify(moved);
            session = MakeComObject<LambdaEditSession>(
                [task = std::move(moved), verify](TfEditCookie ec) -> HRESULT {
                    QIQI_TEST_CHECK(ec == 47);
                    verify(task);
                    return S_OK;
                });
            context.Reset();
        }
        QIQI_TEST_CHECK(session->DoEditSession(47) == S_OK);
        session.Reset();
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionCleanupRegistry() {
    static_assert(!std::is_copy_constructible_v<CompositionCleanup>);
    static_assert(!std::is_move_constructible_v<CompositionCleanup>);
    static_assert(!std::is_copy_constructible_v<CompositionCleanupRegistry>);
    static_assert(!std::is_move_constructible_v<CompositionCleanupRegistry>);
    const auto first = MakeComObject<MockTfContext>();
    const auto second = MakeComObject<MockTfContext>();
    const auto first_composition = MakeComObject<MockTfComposition>(first->range_);
    const auto second_composition = MakeComObject<MockTfComposition>(second->range_);
    const auto make_cleanup = [](gsl::not_null<ITfContext*> context, ITfComposition* composition, uint64_t id) {
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context.get();
        cleanup->composition = composition;
        cleanup->id = id;
        return cleanup;
    };

    CompositionCleanupRegistry registry;
    QIQI_TEST_CHECK(registry.TrackedCount() == 0 && !registry.FindComposition(nullptr));
    QIQI_TEST_CHECK(!registry.FindPending(nullptr) && !registry.FindPending(first.Get()));
    const auto starting = make_cleanup(first.Get(), nullptr, 0);
    registry.Track(starting);
    QIQI_TEST_CHECK(starting.use_count() == 1 && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindComposition(nullptr));
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && !registry.FindPending(second.Get()));

    const auto scheduled = make_cleanup(first.Get(), first_composition.Get(), 1);
    auto other = make_cleanup(second.Get(), second_composition.Get(), 2);
    registry.Track(scheduled);
    registry.Track(other);
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && registry.TrackedCount() == 3);
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == scheduled);
    QIQI_TEST_CHECK(registry.FindComposition(second_composition.Get()) == other);
    scheduled->finished = true;
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == scheduled && registry.TrackedCount() == 3);
    QIQI_TEST_CHECK(registry.FindPending(first.Get()) == starting && registry.TrackedCount() == 2);
    QIQI_TEST_CHECK(!registry.FindComposition(first_composition.Get()));
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    QIQI_TEST_CHECK(starting->context_removed && starting->finished && !starting->terminated);
    QIQI_TEST_CHECK(!scheduled->context_removed && !other->context_removed && !other->finished);
    QIQI_TEST_CHECK(registry.TrackedCount() == 2);
    QIQI_TEST_CHECK(!registry.FindPending(first.Get()) && registry.TrackedCount() == 1);
    const std::weak_ptr<CompositionCleanup> expired = other;
    other.reset();
    QIQI_TEST_CHECK(expired.expired() && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindComposition(second_composition.Get()) && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(!registry.FindPending(nullptr) && registry.TrackedCount() == 0);

    // Removal reaches every matching record, including already-finished and unpublished results.
    const auto pending = make_cleanup(first.Get(), first_composition.Get(), 3);
    const auto finished = make_cleanup(first.Get(), first_composition.Get(), 4);
    const auto unpublished = make_cleanup(first.Get(), nullptr, 0);
    const auto foreign = make_cleanup(second.Get(), second_composition.Get(), 5);
    finished->finished = true;
    registry.Track(pending);
    registry.Track(finished);
    registry.Track(unpublished);
    registry.Track(foreign);
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    for (const auto& cleanup : {pending, finished, unpublished}) {
        QIQI_TEST_CHECK(cleanup->context_removed && cleanup->finished && !cleanup->terminated);
    }
    QIQI_TEST_CHECK(!foreign->context_removed && !foreign->finished);
    QIQI_TEST_CHECK(registry.TrackedCount() == 4);
    QIQI_TEST_CHECK(registry.FindComposition(first_composition.Get()) == pending);
    registry.MarkContextRemoved(first.Get(), 0, GUID_NULL);
    QIQI_TEST_CHECK(!registry.FindPending(first.Get()) && registry.TrackedCount() == 1);
    QIQI_TEST_CHECK(registry.FindPending(second.Get()) == foreign);

    const auto survivor = make_cleanup(first.Get(), first_composition.Get(), 6);
    {
        CompositionCleanupRegistry transient;
        transient.Track(survivor);
        QIQI_TEST_CHECK(survivor.use_count() == 1);
    }
    QIQI_TEST_CHECK(survivor.use_count() == 1 && !survivor->finished && !survivor->context_removed);
    QIQI_TEST_CHECK(survivor->context.Get() == first.Get() &&
                    survivor->composition.Get() == first_composition.Get());
    QIQI_TEST_CHECK(first->request_edit_session_calls_ == 0 && second->request_edit_session_calls_ == 0);
    QIQI_TEST_CHECK(first->prop_->clear_calls_ == 0 && second->prop_->clear_calls_ == 0);
    QIQI_TEST_CHECK(first_composition->end_composition_calls_ == 0 &&
                    second_composition->end_composition_calls_ == 0);
    QIQI_TEST_CHECK(first->range_->set_text_calls_ == 0 && second->range_->set_text_calls_ == 0);
}

void TestCompositionCleanupExecution() {
    enum class Interrupt { None, RemovedBefore, TerminatedBefore, RemovedLookup, TerminatedLookup, RemovedClear, TerminatedClear };
    struct Case {
        bool has_range = true;
        Interrupt interrupt = Interrupt::None;
        HRESULT property_hr = S_OK;
        bool null_property = false;
        HRESULT clear_hr = S_OK;
        HRESULT end_hr = S_OK;
        unsigned int clear_calls = 1;
        unsigned int end_calls = 1;
    };
    const std::array cases{
        Case{},
        Case{.has_range = false, .clear_calls = 0},
        Case{.interrupt = Interrupt::RemovedBefore, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedBefore, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::RemovedLookup, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedLookup, .clear_calls = 0, .end_calls = 0},
        Case{.interrupt = Interrupt::RemovedClear, .end_calls = 0},
        Case{.interrupt = Interrupt::TerminatedClear, .end_calls = 0},
        Case{.property_hr = E_ACCESSDENIED, .clear_calls = 0},
        Case{.null_property = true, .clear_calls = 0},
        Case{.clear_hr = E_ACCESSDENIED},
        Case{.end_hr = TF_E_DISCONNECTED},
        Case{.clear_hr = E_ACCESSDENIED, .end_hr = TF_E_DISCONNECTED},
    };
    for (const auto& test : cases) {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        const auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context;
        cleanup->composition = composition;
        if (test.has_range) cleanup->range = context->range_;
        context->get_property_result_ = test.property_hr;
        context->return_null_property_ = test.null_property;
        context->prop_->clear_result_ = test.clear_hr;
        composition->end_result_ = test.end_hr;
        cleanup->context_removed = test.interrupt == Interrupt::RemovedBefore;
        cleanup->terminated = test.interrupt == Interrupt::TerminatedBefore;
        context->get_property_callback_ = [&]() noexcept {
            if (test.interrupt == Interrupt::RemovedLookup) cleanup->context_removed = true;
            if (test.interrupt == Interrupt::TerminatedLookup) cleanup->terminated = true;
        };
        context->prop_->clear_callback_ = [&](TfEditCookie ec) noexcept {
            QIQI_TEST_CHECK(ec == 71);
            if (test.interrupt == Interrupt::RemovedClear) cleanup->context_removed = true;
            if (test.interrupt == Interrupt::TerminatedClear) cleanup->terminated = true;
        };
        const HRESULT expected_hr = test.end_calls ? test.end_hr : S_OK;
        QIQI_TEST_CHECK(ExecuteCompositionCleanup(cleanup, 71) == expected_hr);
        QIQI_TEST_CHECK(cleanup->started && cleanup->finished);
        QIQI_TEST_CHECK(context->prop_->clear_calls_ == test.clear_calls &&
                        cleanup->attribute_clear_started == (test.clear_calls != 0));
        QIQI_TEST_CHECK(composition->end_composition_calls_ == test.end_calls &&
                        composition->ended_ == (test.end_calls != 0 && SUCCEEDED(test.end_hr)));
        if (test.clear_calls) QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 71);
        if (test.end_calls) QIQI_TEST_CHECK(composition->last_end_cookie_ == 71);
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
        context->get_property_callback_ = {};
        context->prop_->clear_callback_ = {};
    }

    for (const auto failure : {0, 1, 2}) {
        const auto context = MakeComObject<MockTfContext>();
        context->get_property_result_ = failure == 0 ? E_ACCESSDENIED : S_OK;
        context->return_null_property_ = failure == 1;
        context->prop_->clear_result_ = TF_E_DISCONNECTED;
        bool started = false;
        const HRESULT expected_hr = failure == 0 ? E_ACCESSDENIED : failure == 1 ? E_UNEXPECTED : TF_E_DISCONNECTED;
        QIQI_TEST_CHECK(ClearCompositionDisplayAttribute(
            73, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfRange*>(context->range_.Get()),
            []() noexcept { return true; }, &started) == expected_hr);
        QIQI_TEST_CHECK(started == (failure == 2) && context->prop_->clear_calls_ == (failure == 2 ? 1u : 0u));
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        bool looked_up = false;
        context->get_property_callback_ = [&]() noexcept { looked_up = true; };
        QIQI_TEST_CHECK(ClearCompositionDisplayAttribute(
            73, gsl::not_null<ITfContext*>(context.Get()), gsl::not_null<ITfRange*>(context->range_.Get()),
            []() noexcept { return false; }) == S_FALSE);
        QIQI_TEST_CHECK(!looked_up && context->prop_->clear_calls_ == 0);
        context->get_property_callback_ = {};
    }
    {
        const auto context = MakeComObject<MockTfContext>();
        const auto composition = MakeComObject<MockTfComposition>(context->range_);
        auto cleanup = std::make_shared<CompositionCleanup>();
        cleanup->context = context;
        cleanup->composition = composition;
        cleanup->range = context->range_;
        const std::weak_ptr<CompositionCleanup> released = cleanup;
        context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(ec == 79);
            cleanup.reset();
            QIQI_TEST_CHECK(!released.expired());
        };
        QIQI_TEST_CHECK(ExecuteCompositionCleanup(cleanup, 79) == S_OK);
        QIQI_TEST_CHECK(!cleanup && released.expired() && composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(composition->last_end_cookie_ == 79 && context->prop_->last_clear_cookie_ == 79);
        context->prop_->clear_callback_ = {};
    }
}

struct NativeTsfState {
    std::wstring text;
    std::wstring composition_text;
    unsigned int composition_count = 0;
    bool composing = false;
    bool display_attribute = false;
};

class NativeCleanupLayoutSink final : public ComObjectBase<NativeCleanupLayoutSink, ITfTextLayoutSink> {
public:
    STDMETHODIMP OnLayoutChange(ITfContext*, TfLayoutCode code, ITfContextView*) override {
        if (code != TF_LC_CHANGE || called || !callback) return S_OK;
        called = true;
        return callback();
    }
    bool called = false;
    std::function<HRESULT()> callback;
};

class NativeDocumentEventSink final : public ComObjectBase<NativeDocumentEventSink, ITfThreadMgrEventSink> {
public:
    explicit NativeDocumentEventSink(gsl::not_null<QiQiTextService*> service) noexcept : service_(service) {}
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* document) override { return service_->OnInitDocumentMgr(document); }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document) override {
        ++uninits;
        return service_->OnUninitDocumentMgr(document);
    }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) noexcept override { return S_OK; }
    STDMETHODIMP OnPushContext(ITfContext* context) override {
        ++pushes;
        return service_->OnPushContext(context);
    }
    STDMETHODIMP OnPopContext(ITfContext* context) override {
        ++pops;
        return service_->OnPopContext(context);
    }
    unsigned int pushes = 0;
    unsigned int pops = 0;
    unsigned int uninits = 0;
private:
    const gsl::not_null<QiQiTextService*> service_;
};

NativeTsfState ReadNativeTsfStateInSession(gsl::not_null<ITfContext*> context, TfEditCookie ec) {
    NativeTsfState state;
    ComPtr<ITfRange> range;
    ComPtr<ITfRange> end;
    QIQI_TEST_CHECK(context->GetStart(ec, &range) == S_OK);
    QIQI_TEST_CHECK(context->GetEnd(ec, &end) == S_OK);
    QIQI_TEST_CHECK(range->ShiftEndToRange(ec, end.Get(), TF_ANCHOR_END) == S_OK);
    const auto read_text = [ec](gsl::not_null<ITfRange*> target) {
        std::array<wchar_t, 16> text{};
        ULONG count = 0;
        QIQI_TEST_CHECK(target->GetText(ec, 0, text.data(), gsl::narrow_cast<ULONG>(text.size()), &count) == S_OK);
        return std::wstring(text.data(), count);
    };
    state.text = read_text(range.Get());
    ComPtr<ITfContextComposition> compositions;
    QIQI_TEST_CHECK(context->QueryInterface(IID_PPV_ARGS(&compositions)) == S_OK);
    ComPtr<IEnumITfCompositionView> enumerator;
    QIQI_TEST_CHECK(compositions->EnumCompositions(&enumerator) == S_OK);
    ComPtr<ITfRange> composition_range;
    for (;;) {
        ComPtr<ITfCompositionView> view;
        ULONG fetched = 0;
        const HRESULT hr = enumerator->Next(1, &view, &fetched);
        if (hr == S_FALSE) {
            QIQI_TEST_CHECK(fetched == 0);
            break;
        }
        QIQI_TEST_CHECK(hr == S_OK && fetched == 1 && view);
        ++state.composition_count;
        QIQI_TEST_CHECK(view->GetRange(&composition_range) == S_OK);
        state.composition_text = read_text(composition_range.Get());
    }
    const auto has_property = [&](REFGUID guid) {
        ComPtr<ITfProperty> property;
        QIQI_TEST_CHECK(context->GetProperty(guid, &property) == S_OK);
        ComPtr<IEnumTfRanges> ranges;
        QIQI_TEST_CHECK(property->EnumRanges(ec, &ranges,
            composition_range ? composition_range.Get() : range.Get()) == S_OK);
        bool present = false;
        for (;;) {
            ComPtr<ITfRange> property_range;
            ULONG fetched = 0;
            const HRESULT hr = ranges->Next(1, &property_range, &fetched);
            if (hr == S_FALSE) {
                QIQI_TEST_CHECK(fetched == 0);
                break;
            }
            QIQI_TEST_CHECK(hr == S_OK && fetched == 1 && property_range);
            VARIANT value = MakeInt4Variant(0);
            const auto clear = gsl::finally([&]() noexcept { ::VariantClear(&value); });
            QIQI_TEST_CHECK(property->GetValue(ec, property_range.Get(), &value) == S_OK);
            QIQI_TEST_CHECK(value.vt == VT_EMPTY || value.vt == VT_I4);
            present = present || (value.vt == VT_I4 && value.lVal != 0);
        }
        return present;
    };
    state.composing = has_property(GUID_PROP_COMPOSING);
    state.display_attribute = has_property(GUID_PROP_ATTRIBUTE);
    return state;
}

NativeTsfState ReadNativeTsfState(gsl::not_null<ITfContext*> context, TfClientId client) {
    NativeTsfState state;
    const auto session = MakeComObject<LambdaEditSession>([&](TfEditCookie ec) -> HRESULT {
        state = ReadNativeTsfStateInSession(context, ec);
        return S_OK;
    });
    HRESULT result = E_FAIL;
    QIQI_TEST_CHECK(context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READ, &result) == S_OK);
    QIQI_TEST_CHECK(result == S_OK);
    return state;
}

NativeTsfState WaitForNativeTsfState(gsl::not_null<ITfContext*> context, TfClientId client,
                                    std::wstring_view text, unsigned int composition_count) {
    const ULONGLONG started = ::GetTickCount64();
    for (;;) {
        const auto state = ReadNativeTsfState(context, client);
        if (state.text == text && state.composition_count == composition_count) {
            return state;
        }
        if (::GetTickCount64() - started >= 5000) {
            std::cerr << "[FAIL] Native TSF state: text_length=" << state.text.size()
                      << " expected_length=" << text.size() << " compositions=" << state.composition_count
                      << " expected_compositions=" << composition_count << '\n';
            QIQI_TEST_CHECK(false);
        }
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        QIQI_TEST_CHECK(::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE) != WAIT_FAILED);
    }
}

void TestAdditionalContextLifecycleCoverage(gsl::not_null<QiQiTextService*> service) {
    const auto objects_before = g_com_object_count.load();
    for (const bool remove_target : {false, true}) {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        auto unrelated_document = MakeComObject<MockTfDocumentMgr>();
        auto unrelated_context = unrelated_document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        QIQI_TEST_CHECK(unrelated_document->Push(unrelated_context.Get()) == S_OK);
        document->events = service.get();
        unrelated_document->events = service.get();
        const auto disconnect = gsl::finally([&]() noexcept {
            document->events = nullptr;
            unrelated_document->events = nullptr;
        });
        context->get_document_callback_ = [&] {
            const auto& removed_document = remove_target ? document : unrelated_document;
            QIQI_TEST_CHECK(removed_document->Pop(TF_POPF_ALL) == S_OK);
        };
        BOOL eaten = TRUE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK);
        if (remove_target) {
            QIQI_TEST_CHECK(!eaten && context->request_edit_session_calls_ == 0 && context->layout_advise_calls_ == 0);
            QIQI_TEST_CHECK(context->range_->text_.empty() && context->start_composition_calls_ == 0);
        } else {
            QIQI_TEST_CHECK(eaten && context->range_->text_ == L"a" && context->layout_sink_);
        }
        context->get_document_callback_ = {};
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
    }
    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        document->events = service.get();
        const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
        context->layout_advise_callback_ = [&] {
            QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
            BOOL nested_eaten = TRUE;
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &nested_eaten) == S_OK && !nested_eaten);
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &nested_eaten) == S_OK && !nested_eaten);
        };
        BOOL eaten = TRUE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(context->layout_advise_calls_ == 1 && context->layout_unadvise_calls_ == 1);
        QIQI_TEST_CHECK(!context->layout_sink_ && context->request_edit_session_calls_ == 0);
        context->layout_advise_callback_ = {};
        auto retry_context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(retry_context.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(retry_context.Get(), 'C', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(retry_context->range_->text_ == L"c" && retry_context->layout_sink_);
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(context->layout_unadvise_calls_ == 1 && !context->layout_sink_);
        QIQI_TEST_CHECK(retry_context->layout_unadvise_calls_ == 1 && !retry_context->layout_sink_);
    }
    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        context->layout_advise_result_ = E_ACCESSDENIED;
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(context->range_->text_ == L"a" && !context->layout_sink_);
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK && context->last_composition_->ended_);
        QIQI_TEST_CHECK(context->layout_unadvise_calls_ == 0);
    }
    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        auto alias = MakeComObject<MockTfDocumentMgrAlias>(gsl::not_null<ITfDocumentMgr*>(document.Get()));
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        const auto requests = context->request_edit_session_calls_;
        const auto composition = context->last_composition_;
        alias->identity_result = E_ACCESSDENIED;
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(alias.Get()) == S_OK);
        QIQI_TEST_CHECK(!composition->ended_ && context->layout_sink_);
        alias->identity_result = S_OK;
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(alias.Get()) == S_OK);
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests && !context->layout_sink_);
        QIQI_TEST_CHECK(context->layout_unadvise_calls_ == 1 && !composition->ended_);
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(alias.Get()) == S_OK && context->layout_unadvise_calls_ == 1);
    }
    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        const auto composition = context->last_composition_;
        document->get_base_result = E_ACCESSDENIED;
        QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), document.Get()) == S_OK);
        QIQI_TEST_CHECK(composition->ended_ && context->prop_->clear_calls_ == 1 && !context->layout_sink_);
    }
    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        const auto composition = context->last_composition_;
        document->events = service.get();
        const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
        context->prop_->clear_callback_ = [&](TfEditCookie) {
            QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
        };
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        context->prop_->clear_callback_ = {};
        QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(context->range_->text_ == L"a" && !context->layout_sink_);
    }
    {
        const QiQiConfig saved_config = LoadConfig(g_dll_instance);
        auto focus_config = saved_config;
        focus_config.show_status_bar = true;
        QIQI_TEST_CHECK(SaveConfigToRegistry(focus_config));
        const auto restore_config = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config));
        });
        auto focus_service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto focus_manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(focus_service->Activate(focus_manager.Get(), 2) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(focus_service->Deactivate() == S_OK); });
        for (const bool regain_focus : {false, true}) {
            QIQI_TEST_CHECK(focus_service->OnSetFocus(TRUE) == S_OK);
            const HWND status = FindThreadWindowByClass(kStatusBarWndClass);
            QIQI_TEST_CHECK(status && ::IsWindowVisible(status));
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext();
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            BOOL eaten = FALSE;
            const HRESULT key_hr = focus_service->OnKeyDown(context.Get(), 'A', 0, &eaten);
            if (!eaten) {
                std::cerr << "Focus fixture key state: shift=" << ::GetKeyState(VK_SHIFT)
                          << ", ctrl=" << ::GetKeyState(VK_CONTROL) << ", alt=" << ::GetKeyState(VK_MENU)
                          << ", caps=" << ::GetKeyState(VK_CAPITAL) << ", default_chinese=" << focus_config.default_chinese_mode
                          << ", open=" << ReadCompartmentInt4(focus_manager->openclose_compartment_.Get(), -1) << "\n";
            }
            QIQI_TEST_CHECK(key_hr == S_OK && eaten);
            const auto composition = context->last_composition_;
            document->events = focus_service.Get();
            const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
            composition->get_range_callback_ = [&] {
                if (regain_focus) {
                    QIQI_TEST_CHECK(focus_service->OnSetFocus(TRUE) == S_OK);
                } else {
                    QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
                }
            };
            QIQI_TEST_CHECK(focus_service->OnSetFocus(FALSE) == S_OK);
            composition->get_range_callback_ = {};
            QIQI_TEST_CHECK((::IsWindowVisible(status) != FALSE) == regain_focus);
            QIQI_TEST_CHECK(focus_service->OnSetFocus(FALSE) == S_OK && !::IsWindowVisible(status));
        }
        QIQI_TEST_CHECK(focus_service->OnSetFocus(TRUE) == S_OK);
        const HWND status = FindThreadWindowByClass(kStatusBarWndClass);
        QIQI_TEST_CHECK(status && ::IsWindowVisible(status));
        auto document = MakeComObject<MockTfDocumentMgr>();
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(focus_service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        document->get_top_callback = [&] { QIQI_TEST_CHECK(focus_service->OnSetFocus(FALSE) == S_OK); };
        QIQI_TEST_CHECK(focus_service->OnSetFocus(document.Get(), document.Get()) == S_OK);
        document->get_top_callback = {};
        QIQI_TEST_CHECK(!::IsWindowVisible(status) && context->last_composition_->ended_ && !context->layout_sink_);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestContextFsmKeyPrediction(gsl::not_null<QiQiTextService*> service) {
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    {
        auto inactive_service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = TRUE;
        QIQI_TEST_CHECK(inactive_service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(inactive_service->OnTestKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(inactive_service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    }
    for (const bool temporary_english : {false, true}) {
        auto current = MakeComObject<MockTfContext>();
        auto next = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(current.Get(), temporary_english ? 'Z' : 'A', 0, &eaten) == S_OK && eaten);
        const auto composition = current->last_composition_;
        const auto initial_text = current->range_->text_;
        const auto requests_before = current->request_edit_session_calls_;
        for (const UINT key : std::array<UINT, 5>{VK_BACK, VK_SPACE, VK_RETURN, VK_ESCAPE, '1'}) {
            QIQI_TEST_CHECK(service->OnTestKeyDown(current.Get(), key, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), key, 0, &eaten) == S_OK && !eaten);
        }
        for (const UINT key : {VK_LEFT, VK_NEXT}) {
            QIQI_TEST_CHECK(service->OnTestKeyDown(current.Get(), key, 0, &eaten) == S_OK);
            QIQI_TEST_CHECK((eaten != FALSE) == !temporary_english);
            QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), key, 0, &eaten) == S_OK && !eaten);
        }
        for (const UINT modifier : {VK_SHIFT, VK_CAPITAL}) {
            auto keyboard = clean_keyboard;
            gsl::at(keyboard, modifier) = modifier == VK_SHIFT ? BYTE{0x80} : BYTE{1};
            QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
            QIQI_TEST_CHECK(service->OnTestKeyDown(current.Get(), 'D', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), 'D', 0, &eaten) == S_OK && !eaten);
        }
        QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
        QIQI_TEST_CHECK(service->OnTestKeyDown(nullptr, 'B', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(!composition->ended_ && current->range_->text_ == initial_text);
        QIQI_TEST_CHECK(current->request_edit_session_calls_ == requests_before && next->request_edit_session_calls_ == 0);
        QIQI_TEST_CHECK(!next->layout_sink_ && next->start_composition_calls_ == 0);
        QIQI_TEST_CHECK(service->OnTestKeyDown(current.Get(), VK_BACK, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(current.Get(), 'B', 0, &eaten) == S_OK && eaten);
        const auto continued_text = initial_text + L"b";
        QIQI_TEST_CHECK(current->range_->text_ == continued_text && !composition->ended_);
        QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), 'C', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(next.Get(), 'C', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(composition->ended_ && current->range_->text_ == continued_text && next->range_->text_ == L"c");
        const auto next_composition = next->last_composition_;
        next_composition->get_range_callback_ = [&] {
            QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), 'D', 0, &eaten) == S_OK && !eaten);
        };
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        next_composition->get_range_callback_ = {};
    }
    {
        auto current = MakeComObject<MockTfContext>();
        auto next = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnPreservedKey(current.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnTestKeyDown(next.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(next.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(next->range_->text_.empty() && next->request_edit_session_calls_ == 0);
        QIQI_TEST_CHECK(service->OnPreservedKey(next.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
        auto full_width_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(service->OnTestKeyDown(full_width_context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(full_width_context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(full_width_context->range_->text_ == L"\uff41");
        QIQI_TEST_CHECK(service->OnPreservedKey(full_width_context.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnTestKeyDown(full_width_context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnPreservedKey(full_width_context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestReentrantCompositionEdits() {
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    const auto send_key = [&clean_keyboard](
        const ComPtr<QiQiTextService>& service, ITfContext* context, UINT key, BOOL* eaten) {
        QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
        return service->OnKeyDown(context, key, 0, eaten);
    };
    const QiQiConfig saved_config = LoadConfig(g_dll_instance);
    auto config = saved_config;
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.show_status_bar = false;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    const auto restore_config = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config)); });
    const auto objects_before = g_com_object_count.load();
    {
        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
        auto context = MakeComObject<MockTfContext>();
        auto next_context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(send_key(service, context.Get(), 'A', &eaten) == S_OK && eaten);
        auto newer_keyboard = clean_keyboard;
        gsl::at(newer_keyboard, VK_CONTROL) = BYTE{0x80};
        context->last_composition_->get_range_callback_ = [&]() {
            QIQI_TEST_CHECK(::SetKeyboardState(newer_keyboard.data()));
        };
        QIQI_TEST_CHECK(send_key(service, next_context.Get(), 'C', &eaten) == S_OK && eaten);
        context->last_composition_->get_range_callback_ = {};
        QIQI_TEST_CHECK(next_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(service->OnTestKeyDown(next_context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
        next_context->disabled_compartment_->get_value_callback_ = [&]() {
            QIQI_TEST_CHECK(::SetKeyboardState(newer_keyboard.data()));
        };
        QIQI_TEST_CHECK(service->OnTestKeyDown(next_context.Get(), 'D', 0, &eaten) == S_OK && eaten);
        next_context->disabled_compartment_->get_value_callback_ = {};
        QIQI_TEST_CHECK(service->OnTestKeyDown(next_context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
    }
    enum class Transition { Deactivate, ReactivateSameContext, SwitchContext, RemoveContext, ContinueSameContext };
    enum class Boundary { Range, Text, Clone, Collapse, Selection, Property, Clear, Attribute, End };
    struct EditCase { UINT key; Boundary boundary; };
    constexpr std::array<EditCase, 21> cases{{
        {'B', Boundary::Range}, {'B', Boundary::Text}, {'B', Boundary::Clone},
        {'B', Boundary::Collapse}, {'B', Boundary::Selection}, {'B', Boundary::Property}, {'B', Boundary::Attribute},
        {VK_SPACE, Boundary::Range}, {VK_SPACE, Boundary::Text}, {VK_SPACE, Boundary::Clone},
        {VK_SPACE, Boundary::Collapse}, {VK_SPACE, Boundary::Selection}, {VK_SPACE, Boundary::Property},
        {VK_SPACE, Boundary::Clear}, {VK_SPACE, Boundary::End},
        {VK_ESCAPE, Boundary::Range}, {VK_ESCAPE, Boundary::Text}, {VK_ESCAPE, Boundary::End},
        {0, Boundary::Range}, {0, Boundary::Property}, {0, Boundary::Clear}
    }};
    size_t edit_cases = 0;
    for (const auto& test_case : cases) {
        const auto [key, boundary] = test_case;
        const bool host_termination = key == 0;
        for (const auto transition : {Transition::Deactivate, Transition::ReactivateSameContext,
                                     Transition::SwitchContext, Transition::RemoveContext, Transition::ContinueSameContext}) {
            if (transition == Transition::ContinueSameContext && boundary != Boundary::End) continue;
            auto service = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext();
            auto next_context = MakeComObject<MockTfContext>();
            document->events = service.Get();
            const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(send_key(service, context.Get(), 'A', &eaten) == S_OK && eaten);
            const auto composition = context->last_composition_;
            const auto snapshot = [&]() {
                return std::tuple{
                    context->range_->text_, context->range_->set_text_calls_, context->prop_->clear_calls_,
                    context->range_->clone_calls_, context->set_selection_calls_, context->prop_->set_calls_,
                    composition->end_composition_calls_, context->get_active_view_calls_,
                    context->last_composition_.Get(), context->layout_sink_.Get(),
                    next_context->range_->text_, next_context->range_->set_text_calls_,
                    next_context->last_composition_.Get(), next_context->layout_sink_.Get(),
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            };
            auto after_transition = snapshot();
            bool entered = false;
            const auto interrupt = [&]() {
                if (std::exchange(entered, true)) return;
                if (transition == Transition::ContinueSameContext) {
                    QIQI_TEST_CHECK(send_key(service, context.Get(), 'C', &eaten) == S_OK && eaten);
                } else if (transition == Transition::Deactivate || transition == Transition::ReactivateSameContext) {
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (transition == Transition::ReactivateSameContext) {
                        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
                        QIQI_TEST_CHECK(send_key(service, context.Get(), 'C', &eaten) == S_OK && eaten);
                    }
                } else {
                    if (transition == Transition::RemoveContext) {
                        QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
                        QIQI_TEST_CHECK(composition->NotifyHostTermination(17) == S_OK);
                    }
                    QIQI_TEST_CHECK(send_key(service, next_context.Get(), 'C', &eaten) == S_OK && eaten);
                }
                after_transition = snapshot();
            };
            switch (boundary) {
                case Boundary::Range: composition->get_range_callback_ = interrupt; break;
                case Boundary::Text: context->range_->set_text_callback_ = interrupt; break;
                case Boundary::Clone: context->range_->clone_callback_ = interrupt; break;
                case Boundary::Collapse: context->range_->collapse_callback_ = interrupt; break;
                case Boundary::Selection: context->set_selection_callback_ = interrupt; break;
                case Boundary::Property: context->get_property_callback_ = interrupt; break;
                case Boundary::Clear: context->prop_->clear_callback_ = [&](TfEditCookie) { interrupt(); }; break;
                case Boundary::Attribute: context->prop_->set_callback_ = interrupt; break;
                case Boundary::End: composition->end_callback_ = [&](TfEditCookie) { interrupt(); }; break;
            }
            if (host_termination) {
                QIQI_TEST_CHECK(composition->NotifyHostTermination(17) == S_OK);
            } else {
                QIQI_TEST_CHECK(send_key(service, context.Get(), key, &eaten) == S_OK && eaten);
            }
            composition->get_range_callback_ = {};
            context->range_->set_text_callback_ = {};
            context->range_->clone_callback_ = {};
            context->range_->collapse_callback_ = {};
            context->set_selection_callback_ = {};
            context->get_property_callback_ = {};
            context->prop_->clear_callback_ = {};
            context->prop_->set_callback_ = {};
            composition->end_callback_ = {};
            if (!entered || snapshot() != after_transition) {
                std::cerr << "Edit boundary case: key=" << key << ", boundary=" << gsl::narrow_cast<int>(boundary)
                          << ", transition=" << gsl::narrow_cast<int>(transition) << "\n";
            }
            QIQI_TEST_CHECK(entered && snapshot() == after_transition);
            if (transition == Transition::ReactivateSameContext || transition == Transition::ContinueSameContext) {
                QIQI_TEST_CHECK(context->range_->text_ == L"c" && !context->last_composition_->ended_);
            } else if (transition != Transition::Deactivate) {
                if (next_context->range_->text_ != L"c" ||
                    !next_context->last_composition_ || next_context->last_composition_->ended_) {
                    std::cerr << "Replacement case: key=" << key << ", boundary=" << gsl::narrow_cast<int>(boundary)
                              << ", transition=" << gsl::narrow_cast<int>(transition)
                              << ", text_size=" << next_context->range_->text_.size()
                              << ", ctrl=" << ::GetKeyState(VK_CONTROL) << ", caps=" << ::GetKeyState(VK_CAPITAL) << "\n";
                }
                QIQI_TEST_CHECK(next_context->range_->text_ == L"c" && !next_context->last_composition_->ended_);
            }
            ++edit_cases;
        }
    }
    QIQI_TEST_CHECK(edit_cases == 86);
    for (const bool during_layout : {false, true}) {
        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(send_key(service, context.Get(), 'A', &eaten) == S_OK && eaten);
        const auto composition = context->last_composition_;
        const auto snapshot = [&]() {
            return std::tuple{
                context->range_->text_, context->range_->set_text_calls_, context->range_->clone_calls_,
                context->set_selection_calls_, context->prop_->set_calls_, context->get_active_view_calls_,
                context->last_composition_.Get(), manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
        };
        auto after_transition = snapshot();
        bool entered = false;
        const auto replace = [&]() {
            if (std::exchange(entered, true)) return;
            QIQI_TEST_CHECK(send_key(service, context.Get(), VK_SPACE, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(send_key(service, context.Get(), 'C', &eaten) == S_OK && eaten);
            after_transition = snapshot();
        };
        if (during_layout) {
            context->view_->get_text_ext_callback_ = replace;
        } else {
            composition->get_range_callback_ = replace;
        }
        QIQI_TEST_CHECK(send_key(service, context.Get(), 'B', &eaten) == S_OK && eaten);
        composition->get_range_callback_ = {};
        context->view_->get_text_ext_callback_ = {};
        QIQI_TEST_CHECK(entered && snapshot() == after_transition);
        QIQI_TEST_CHECK(context->range_->text_ == L"c" && context->last_composition_ != composition);
        QIQI_TEST_CHECK(composition->ended_ && !context->last_composition_->ended_);
    }
    enum class StartTransition { Deactivate, ReactivateSameContext, SwitchContext, RemoveContext, HostTermination };
    size_t start_cases = 0;
    for (const auto transition : {StartTransition::Deactivate, StartTransition::ReactivateSameContext,
                                 StartTransition::SwitchContext, StartTransition::RemoveContext,
                                 StartTransition::HostTermination}) {
        for (const bool asynchronous_cleanup : {false, true}) {
            if (asynchronous_cleanup &&
                (transition == StartTransition::RemoveContext || transition == StartTransition::HostTermination)) continue;
            auto service = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext(queue);
            auto next_context = MakeComObject<MockTfContext>();
            document->events = service.Get();
            const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            ComPtr<MockTfComposition> late_composition;
            bool entered = false;
            BOOL eaten = FALSE;
            context->start_composition_callback_ = [&]() {
                if (std::exchange(entered, true)) return;
                late_composition = context->last_composition_;
                if (transition == StartTransition::Deactivate || transition == StartTransition::ReactivateSameContext) {
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (transition == StartTransition::ReactivateSameContext) {
                        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
                        QIQI_TEST_CHECK(send_key(service, context.Get(), 'C', &eaten) == S_OK && eaten);
                    }
                } else {
                    if (transition == StartTransition::RemoveContext) {
                        QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
                    }
                    if (transition == StartTransition::RemoveContext || transition == StartTransition::HostTermination) {
                        QIQI_TEST_CHECK(late_composition->NotifyHostTermination(17) == S_OK);
                    }
                    QIQI_TEST_CHECK(send_key(service, next_context.Get(), 'C', &eaten) == S_OK && eaten);
                }
            };
            QIQI_TEST_CHECK(send_key(service, context.Get(), 'A', &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            context->allow_sync_edit_sessions_ = !asynchronous_cleanup;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && entered && late_composition);
            context->start_composition_callback_ = {};
            QIQI_TEST_CHECK(context->range_->text_.empty() && context->range_->set_text_calls_ == 0);
            for (unsigned int i = 0; i < 3 && queue->PendingCount() != 0; ++i) {
                QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0 && late_composition->ended_);
            const bool host_ended = transition == StartTransition::RemoveContext || transition == StartTransition::HostTermination;
            QIQI_TEST_CHECK(late_composition->end_composition_calls_ == (host_ended ? 0u : 1u));
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == (host_ended ? 0u : 1u));
            if (transition == StartTransition::ReactivateSameContext) {
                QIQI_TEST_CHECK(context->range_->text_ == L"c" && context->range_->set_text_calls_ == 1);
                QIQI_TEST_CHECK(context->last_composition_ != late_composition && !context->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(context->range_->text_.empty() && context->range_->set_text_calls_ == 0);
                if (transition != StartTransition::Deactivate) {
                    QIQI_TEST_CHECK(next_context->range_->text_ == L"c" && !next_context->last_composition_->ended_);
                }
            }
            context->allow_sync_edit_sessions_ = true;
            ++start_cases;
        }
    }
    QIQI_TEST_CHECK(start_cases == 8);
    size_t cleanup_cases = 0;
    for (const bool asynchronous_cleanup : {false, true}) {
        for (const bool remove_context : {false, true}) {
            auto service = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext(queue);
            auto next_context = MakeComObject<MockTfContext>();
            document->events = service.Get();
            const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(send_key(service, context.Get(), 'A', &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto composition = context->last_composition_;
            bool entered = false;
            context->get_property_callback_ = [&]() {
                if (std::exchange(entered, true)) return;
                if (remove_context) {
                    QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
                } else {
                    QIQI_TEST_CHECK(composition->NotifyHostTermination(17) == S_OK);
                }
            };
            context->allow_sync_edit_sessions_ = !asynchronous_cleanup;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            if (asynchronous_cleanup) {
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && !entered);
                QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
                QIQI_TEST_CHECK(send_key(service, next_context.Get(), 'C', &eaten) == S_OK && eaten);
                const auto ui_before = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(next_context->range_->text_ == L"c" && !next_context->last_composition_->ended_);
            }
            context->get_property_callback_ = {};
            QIQI_TEST_CHECK(entered && context->range_->text_ == L"a" && context->range_->set_text_calls_ == 1);
            QIQI_TEST_CHECK(composition->end_composition_calls_ == 0);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == (remove_context ? 0u : 1u));
            if (remove_context) {
                QIQI_TEST_CHECK(composition->NotifyHostTermination(17) == S_OK);
            } else {
                QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 17);
            }
            context->allow_sync_edit_sessions_ = true;
            ++cleanup_cases;
        }
    }
    QIQI_TEST_CHECK(cleanup_cases == 4);
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

struct UiWindowDestructionProbe {
    gsl::not_null<QiQiTextService*> service;
    gsl::not_null<MockTfThreadMgr*> manager;
    gsl::not_null<MockTfComposition*> composition;
    DWORD owner_thread = ::GetCurrentThreadId();
    unsigned int destroyed = 0;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR id, DWORD_PTR data) {
        auto* probe = reinterpret_cast<UiWindowDestructionProbe*>(data);
        if (message == WM_DESTROY) {
            QIQI_TEST_CHECK(::GetCurrentThreadId() == probe->owner_thread);
            QIQI_TEST_CHECK(probe->composition->ended_ && probe->manager->end_ui_calls_ == 1);
            QIQI_TEST_CHECK(probe->service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(probe->service->Activate(probe->manager.get(), 1) == E_UNEXPECTED);
        }
        if (message == WM_NCDESTROY) {
            ++probe->destroyed;
            QIQI_TEST_CHECK(::RemoveWindowSubclass(hwnd, &WindowProc, id));
        }
        return ::DefSubclassProc(hwnd, message, wparam, lparam);
    }
};

void TestDeactivationWindowLifecycle() {
    TestEditSessionTickets();
    TestCompositionEditTasks();
    TestCompositionCleanupRegistry();
    TestCompositionCleanupExecution();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    const auto objects_before = g_com_object_count.load();
    {
        const QiQiConfig saved_config = LoadConfig(g_dll_instance);
        auto config = saved_config;
        config.show_status_bar = true;
        config.default_chinese_mode = true;
        config.default_full_width = false;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
        const auto restore_config = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config)); });
        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto manager = MakeComObject<MockTfThreadMgr>();
        const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        auto* ui = dynamic_cast<CandidateUI*>(manager->last_ui_element_.Get());
        QIQI_TEST_CHECK(ui && ui->WindowHandle());
        const HWND status = FindThreadWindowByClass(kStatusBarWndClass);
        QIQI_TEST_CHECK(status && ::IsWindowVisible(status));
        RECT status_rect{};
        QIQI_TEST_CHECK(::GetClientRect(status, &status_rect));
        ::SendMessageW(status, WM_MOUSEMOVE, 0, MAKELPARAM(status_rect.right / 3, status_rect.bottom / 2));
        const HWND tooltip = FindThreadWindowByClass(kStatusBarTooltipWndClass);
        QIQI_TEST_CHECK(tooltip && ::IsWindowVisible(tooltip));
        ::SendMessageW(status, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, status_rect.bottom / 2));
        QIQI_TEST_CHECK(::GetCapture() == status);
        const std::array<HWND, 3> windows{ui->WindowHandle(), status, tooltip};
        const UiWindowDestructionProbe probe{gsl::not_null<QiQiTextService*>(service.Get()),
                                             gsl::not_null<MockTfThreadMgr*>(manager.Get()),
                                             gsl::not_null<MockTfComposition*>(context->last_composition_.Get())};
        for (const HWND window : windows) {
            QIQI_TEST_CHECK(::SetWindowSubclass(window, &UiWindowDestructionProbe::WindowProc, 1,
                                              reinterpret_cast<DWORD_PTR>(&probe)));
        }
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(probe.destroyed == windows.size() && ::GetCapture() != status);
        for (const HWND window : windows) QIQI_TEST_CHECK(!::IsWindow(window));
        QIQI_TEST_CHECK(!ui->WindowHandle() && ui->UiElementId() == TF_INVALID_UIELEMENTID);
        QIQI_TEST_CHECK(ui->Show(TRUE) == S_FALSE && !ui->WindowHandle());
        QIQI_TEST_CHECK(service->Deactivate() == S_OK && manager->end_ui_calls_ == 1);

        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(manager->last_ui_element_.Get() == ui && ui->WindowHandle());
        QIQI_TEST_CHECK(context->range_->text_ == L"b" && !context->last_composition_->ended_);
        const HWND reactivated_status = FindThreadWindowByClass(kStatusBarWndClass);
        QIQI_TEST_CHECK(reactivated_status && ::IsWindowVisible(reactivated_status));
        ::SendMessageW(reactivated_status, WM_COMMAND, static_cast<WPARAM>(LangBarMenuId::ToggleChinese), 0);
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
        ::SendMessageW(reactivated_status, WM_COMMAND, static_cast<WPARAM>(LangBarMenuId::ToggleChinese), 0);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'C', 0, &eaten) == S_OK && eaten);
        ::SendMessageW(ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
                       MAKELPARAM(20, ui->CurrentHeight() - 15));
        QIQI_TEST_CHECK(context->last_composition_->ended_);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        manager->fail_key_sink_ = true;
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == E_FAIL);
        QIQI_TEST_CHECK(!FindThreadWindowByClass(kStatusBarWndClass) && !ui->WindowHandle());
        manager->fail_key_sink_ = false;

        for (const bool reactivate_inside_begin : {false, true}) {
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            auto old_context = MakeComObject<MockTfContext>();
            auto new_context = MakeComObject<MockTfContext>();
            auto replacement_manager = MakeComObject<MockTfThreadMgr>();
            bool entered = false;
            DWORD stale_id = TF_INVALID_UIELEMENTID;
            manager->begin_ui_callback_ = [&](DWORD id) {
                if (std::exchange(entered, true)) return;
                stale_id = id;
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                QIQI_TEST_CHECK(ui->Show(TRUE) == S_FALSE && !ui->WindowHandle());
                if (reactivate_inside_begin) {
                    replacement_manager->next_ui_element_id_ = id;
                    QIQI_TEST_CHECK(service->Activate(replacement_manager.Get(), 1) == S_OK);
                    QIQI_TEST_CHECK(service->OnKeyDown(new_context.Get(), 'E', 0, &eaten) == S_OK && eaten);
                }
            };
            QIQI_TEST_CHECK(service->OnKeyDown(old_context.Get(), 'D', 0, &eaten) == S_OK && eaten);
            manager->begin_ui_callback_ = {};
            QIQI_TEST_CHECK(entered && std::count(manager->ended_ui_ids_.begin(), manager->ended_ui_ids_.end(), stale_id) == 1);
            if (reactivate_inside_begin) {
                // IDs belong to a manager; a different manager may reuse the same numeric ID.
                QIQI_TEST_CHECK(ui->UiElementId() == stale_id && replacement_manager->end_ui_calls_ == 0);
                QIQI_TEST_CHECK(ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()));
                QIQI_TEST_CHECK(new_context->range_->text_ == L"e" && !new_context->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() && !FindThreadWindowByClass(kStatusBarWndClass));
                QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID);
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        }

        for (const bool reactivate_inside_end : {false, true}) {
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            auto end_context = MakeComObject<MockTfContext>();
            auto replacement = MakeComObject<MockTfContext>();
            auto replacement_manager = MakeComObject<MockTfThreadMgr>();
            QIQI_TEST_CHECK(service->OnKeyDown(end_context.Get(), 'F', 0, &eaten) == S_OK && eaten);
            const DWORD ending_id = ui->UiElementId();
            manager->end_ui_callback_ = [&](DWORD id) {
                QIQI_TEST_CHECK(id == ending_id && ui->UiElementId() == TF_INVALID_UIELEMENTID);
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                if (reactivate_inside_end) {
                    replacement_manager->next_ui_element_id_ = id;
                    QIQI_TEST_CHECK(service->Activate(replacement_manager.Get(), 1) == S_OK);
                    QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'G', 0, &eaten) == S_OK && eaten);
                }
            };
            QIQI_TEST_CHECK(service->OnKeyDown(end_context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
            manager->end_ui_callback_ = {};
            QIQI_TEST_CHECK(std::count(manager->ended_ui_ids_.begin(), manager->ended_ui_ids_.end(), ending_id) == 1);
            if (reactivate_inside_end) {
                QIQI_TEST_CHECK(ui->UiElementId() == ending_id && replacement_manager->end_ui_calls_ == 0);
                QIQI_TEST_CHECK(ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()));
                QIQI_TEST_CHECK(replacement->range_->text_ == L"g" && !replacement->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() && !FindThreadWindowByClass(kStatusBarWndClass));
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        }

        for (const bool reactivate_during_position : {false, true}) {
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            auto positioning = MakeComObject<MockTfContext>();
            auto replacement = MakeComObject<MockTfContext>();
            QIQI_TEST_CHECK(service->OnKeyDown(positioning.Get(), 'A', 0, &eaten) == S_OK && eaten);
            positioning->view_->text_ext_ = {400, 400, 440, 424};
            RECT replacement_rect{};
            positioning->view_->get_text_ext_callback_ = [&]() {
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                if (reactivate_during_position) {
                    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
                    QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && eaten);
                    QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &replacement_rect));
                }
            };
            QIQI_TEST_CHECK(service->OnKeyDown(positioning.Get(), 'B', 0, &eaten) == S_OK && eaten);
            positioning->view_->get_text_ext_callback_ = {};
            if (reactivate_during_position) {
                RECT actual{};
                QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &actual));
                QIQI_TEST_CHECK(::EqualRect(&actual, &replacement_rect));
                QIQI_TEST_CHECK(replacement->range_->text_ == L"c");
            } else {
                QIQI_TEST_CHECK(!ui->WindowHandle() && !FindThreadWindowByClass(kStatusBarWndClass));
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        }

        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        auto moving = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(service->OnKeyDown(moving.Get(), 'A', 0, &eaten) == S_OK && eaten);
        RECT before_move{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &before_move));
        moving->view_->text_ext_ = {350, 250, 390, 274};
        QIQI_TEST_CHECK(service->OnLayoutChange(moving.Get(), TF_LC_CHANGE, moving->view_.Get()) == S_OK);
        RECT after_move{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &after_move));
        QIQI_TEST_CHECK(!::EqualRect(&before_move, &after_move));
        QIQI_TEST_CHECK((moving->last_edit_session_flags_ & TF_ES_READWRITE) == TF_ES_READ);
        moving->view_->fail_get_text_ext_ = true;
        QIQI_TEST_CHECK(service->OnLayoutChange(moving.Get(), TF_LC_CHANGE, moving->view_.Get()) == S_OK);
        RECT pending_layout{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &pending_layout));
        QIQI_TEST_CHECK(::EqualRect(&after_move, &pending_layout));
        const auto ignored_requests = moving->request_edit_session_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(moving.Get(), TF_LC_CREATE, moving->view_.Get()) == S_OK);
        QIQI_TEST_CHECK(moving->request_edit_session_calls_ == ignored_requests);
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(!::IsWindowVisible(ui->WindowHandle()));
        const auto unfocused_requests = moving->request_edit_session_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(moving.Get(), TF_LC_CHANGE, moving->view_.Get()) == S_OK);
        QIQI_TEST_CHECK(moving->request_edit_session_calls_ == unfocused_requests &&
                        !::IsWindowVisible(ui->WindowHandle()));
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);

        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        auto view_queue = std::make_shared<MockEditSessionQueue>();
        auto view_context = MakeComObject<MockTfContext>(view_queue);
        QIQI_TEST_CHECK(service->OnKeyDown(view_context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(view_queue->RunNext() == S_OK);
        RECT default_rect{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &default_rect));
        const auto default_view_queries = view_context->get_active_view_calls_;
        auto notified_view = MakeComObject<MockTfContextView>();
        notified_view->text_ext_ = {350, 250, 390, 274};
        unsigned int notified_queries = 0;
        notified_view->get_text_ext_callback_ = [&]() { ++notified_queries; };
        QIQI_TEST_CHECK(service->OnLayoutChange(view_context.Get(), TF_LC_CHANGE, notified_view.Get()) == S_OK);
        notified_view.Reset();
        QIQI_TEST_CHECK(view_queue->PendingCount() == 1 && notified_queries == 0);
        QIQI_TEST_CHECK(view_queue->RunNext() == S_OK && notified_queries == 1);
        RECT notified_rect{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &notified_rect));
        QIQI_TEST_CHECK(!::EqualRect(&default_rect, &notified_rect));
        QIQI_TEST_CHECK(view_context->get_active_view_calls_ == default_view_queries);
        QIQI_TEST_CHECK(service->OnLayoutChange(view_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(view_queue->RunNext() == S_OK);
        RECT restored_rect{};
        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &restored_rect));
        QIQI_TEST_CHECK(::EqualRect(&default_rect, &restored_rect));
        auto stale_view = MakeComObject<MockTfContextView>();
        stale_view->get_text_ext_callback_ = [&]() { ++notified_queries; };
        QIQI_TEST_CHECK(service->OnLayoutChange(view_context.Get(), TF_LC_CHANGE, stale_view.Get()) == S_OK);
        stale_view.Reset();
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(view_queue->RunNext() == S_OK && notified_queries == 1 && !ui->WindowHandle());

        auto queue = std::make_shared<MockEditSessionQueue>();
        auto deferred = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(deferred.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(deferred->range_->text_ == L"a" && ui->WindowHandle());
        deferred->allow_sync_edit_sessions_ = false;
        QIQI_TEST_CHECK(service->OnLayoutChange(deferred.Get(), TF_LC_CHANGE, deferred->view_.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(deferred.Get(), 'B', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 3);
        const auto pending_count = queue->PendingCount();
        for (size_t i = 0; i < pending_count; ++i) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        QIQI_TEST_CHECK(deferred->range_->text_ == L"a" && deferred->last_composition_->ended_);
        QIQI_TEST_CHECK(!ui->WindowHandle() && !FindThreadWindowByClass(kStatusBarWndClass));
        deferred->allow_sync_edit_sessions_ = true;
        QIQI_TEST_CHECK(service->ActivateEx(manager.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK);
        manager->show_own_ui_ = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(deferred.Get(), 'C', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(deferred->range_->text_ == L"c" && !ui->WindowHandle());
        QIQI_TEST_CHECK(ui->UiElementId() != TF_INVALID_UIELEMENTID);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        manager->show_own_ui_ = TRUE;
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        auto rejected_end = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(service->OnKeyDown(rejected_end.Get(), 'D', 0, &eaten) == S_OK && eaten);
        const DWORD rejected_id = ui->UiElementId();
        manager->end_ui_result_ = E_FAIL;
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(std::count(manager->ended_ui_ids_.begin(), manager->ended_ui_ids_.end(), rejected_id) == 1);
        QIQI_TEST_CHECK(!ui->WindowHandle() && ui->Show(TRUE) == S_FALSE);
        manager->end_ui_result_ = S_OK;
        {
            ComPtr<ITfUIElement> retained;
            {
                auto ephemeral = MakeComObject<QiQiTextService>(g_dll_instance);
                auto host = MakeComObject<MockTfThreadMgr>();
                auto target = MakeComObject<MockTfContext>();
                QIQI_TEST_CHECK(ephemeral->Activate(host.Get(), 1) == S_OK);
                QIQI_TEST_CHECK(ephemeral->OnKeyDown(target.Get(), 'A', 0, &eaten) == S_OK && eaten);
                retained = host->last_ui_element_;
                QIQI_TEST_CHECK(ephemeral->Deactivate() == S_OK);
            }
            auto* retained_ui = dynamic_cast<CandidateUI*>(retained.Get());
            QIQI_TEST_CHECK(retained_ui && retained_ui->Show(TRUE) == S_FALSE && !retained_ui->WindowHandle());
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(retained_ui->IsShown(&shown) == S_OK && !shown);
            // Exercise detached callbacks independently of the inactive-service presentation gate.
            retained_ui->SetPresentationEnabled(true);
            QIQI_TEST_CHECK(retained_ui->Show(TRUE) == S_OK && retained_ui->WindowHandle());
            ::SendMessageW(retained_ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON,
                           MAKELPARAM(20, retained_ui->CurrentHeight() - 15));
            QIQI_TEST_CHECK(retained_ui->IsShown(&shown) == S_OK && shown);
            QIQI_TEST_CHECK(retained_ui->DestroyPopupWindow());
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfServiceLifecycleTests(gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))) && key_sink);
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))) && comp_sink);

    // Deferred sessions must not cross activation or input-context epochs.
    {
        const auto ui_calls = [&] {
            return std::tuple{mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
        };
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto old_context = MakeComObject<MockTfContext>(queue);
        BOOL deferred_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(old_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        auto new_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2 && new_context->range_->text_.empty());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).context.Get() == old_context.Get());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).context.Get() == new_context.Get());
        const auto before_old_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(old_context->range_->text_.empty() && old_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(old_context->start_composition_calls_ == 0 && ui_calls() == before_old_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->range_->text_ == L"b" && new_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).flags == (TF_ES_ASYNCDONTCARE | TF_ES_READ));
        QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK && queue->PendingCount() == 1);
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        const auto before_old_layout = ui_calls();
        const auto old_view_calls = new_context->get_active_view_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->get_active_view_calls_ == old_view_calls && ui_calls() == before_old_layout);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        const auto current_view_calls = current_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(current_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(current_context->get_active_view_calls_ == current_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // No callback may write or create UI while the service is stopped.
        auto stopped_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        const auto while_stopped = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && stopped_context->range_->text_.empty());
        QIQI_TEST_CHECK(stopped_context->start_composition_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = true;
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == E_FAIL);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = false;
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);

        // An old commit/cancel must not affect a fresh composition on the same COM context.
        for (const UINT action_key : {VK_SPACE, VK_ESCAPE}) {
            auto same_context = MakeComObject<MockTfContext>(queue);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), action_key, 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
            const auto before_stale_action = ui_calls();
            const auto writes_before_stale_action = same_context->range_->set_text_calls_;
            const auto clears_before_stale_action = same_context->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(same_context->range_->set_text_calls_ == writes_before_stale_action);
            QIQI_TEST_CHECK(same_context->prop_->clear_calls_ == clears_before_stale_action &&
                            ui_calls() == before_stale_action);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"b" && same_context->start_composition_calls_ == 2);
            QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        }

        // A delayed cancel must not terminate or hide another context's fresh composition.
        auto cancelled_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), VK_ESCAPE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        auto fresh_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fresh_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->start_composition_calls_ == 1);
        const auto before_old_cancel = ui_calls();
        const auto fresh_writes = fresh_context->range_->set_text_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->range_->set_text_calls_ == fresh_writes);
        QIQI_TEST_CHECK(ui_calls() == before_old_cancel);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Resetting focus invalidates old work even when activation and context pointers are unchanged.
        auto focused_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_focus_reset = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_focus_reset);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->text_ == L"b");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Multiple current snapshots remain FIFO, including commit and the next composition.
        auto fifo_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 3);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"ab");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fifo_context->range_->set_text_calls_ == 3 && fifo_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(fifo_context->start_composition_calls_ == 2);
        const auto fifo_view_calls = fifo_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(fifo_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(fifo_context->get_active_view_calls_ == fifo_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // A -> B -> A requires an epoch, not merely pointer equality.
        auto context_a = MakeComObject<MockTfContext>(queue);
        auto context_b = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_b.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_context_switch = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_a->start_composition_calls_ == 0 && context_b->start_composition_calls_ == 0);
        QIQI_TEST_CHECK(context_a->range_->set_text_calls_ == 0 && context_b->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_context_switch);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_a->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(context_a->range_->text_ == L"c" && context_b->range_->text_.empty());

        // Never reuse A's composition range when B becomes the current input target.
        QIQI_TEST_CHECK(service->OnLayoutChange(context_a.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        const auto text_in_a = context_a->range_->text_;
        const auto reads_in_a = context_a->get_active_view_calls_;
        auto context_c = MakeComObject<MockTfContext>();
        const auto old_composition = context_a->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), VK_BACK, 0, &deferred_eaten) == S_OK && !deferred_eaten);
        QIQI_TEST_CHECK(context_c->start_composition_calls_ == 0 && context_c->range_->text_.empty());
        QIQI_TEST_CHECK(old_composition->ended_ && context_a->prop_->clear_calls_ > 0);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(context_c->range_->text_ == L"d");
        const auto before_context_layout = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(context_a->range_->text_ == text_in_a && context_a->get_active_view_calls_ == reads_in_a);
        QIQI_TEST_CHECK(ui_calls() == before_context_layout);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Host termination is another context-epoch reset, independent of activation.
        auto terminated_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(comp_sink->OnCompositionTerminated(1, nullptr) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_terminated_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(ui_calls() == before_terminated_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
    }

    const auto document_objects_before = g_com_object_count.load();
    // Document/stack notifications share the epoch barrier and old-host cleanup.
    {
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto document_a = MakeComObject<MockTfDocumentMgr>();
        auto document_b = MakeComObject<MockTfDocumentMgr>();
        document_a->events = service.get();
        document_b->events = service.get();
        const auto disconnect = gsl::finally([&]() noexcept {
            document_a->events = nullptr;
            document_b->events = nullptr;
        });
        auto context_a = document_a->CreateTestContext(queue);
        auto context_b = document_b->CreateTestContext(queue);
        auto modal = document_b->CreateTestContext();
        QIQI_TEST_CHECK(document_a->Push(context_a.Get()) == S_OK);
        QIQI_TEST_CHECK(document_b->Push(context_b.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'A', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        const auto composition_a = context_a->last_composition_;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'B', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(service->OnLayoutChange(context_a.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(composition_a->ended_ && context_a->range_->text_ == L"a");
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'C', 0, &handled) == S_OK && handled);
        const auto ui_before_stale = std::tuple{
            mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
        const auto old_views = context_a->get_active_view_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_a->range_->text_ == L"a" && context_a->get_active_view_calls_ == old_views);
        QIQI_TEST_CHECK((std::tuple{
            mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before_stale);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"c");
        const auto composition_b = context_b->last_composition_;
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'D', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(document_b->Push(modal.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_b.Get()) == S_OK);
        QIQI_TEST_CHECK(!composition_b->ended_);
        QIQI_TEST_CHECK(document_a->Pop(TF_POPF_ALL) == S_OK && !composition_b->ended_);
        QIQI_TEST_CHECK(document_b->Pop(0) == S_OK && !composition_b->ended_);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"cd");
        QIQI_TEST_CHECK(context_b->last_composition_ == composition_b && queue->PendingCount() == 0);

        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'E', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(service->OnLayoutChange(context_b.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        const auto requests_before_pop = context_b->request_edit_session_calls_;
        context_b->edit_session_request_result_ = TF_E_DISCONNECTED;
        context_b->document_link_.reset();
        QIQI_TEST_CHECK(document_b->Pop(TF_POPF_ALL) == S_OK);
        QIQI_TEST_CHECK(context_b->request_edit_session_calls_ == requests_before_pop);
        auto fresh = document_a->CreateTestContext(queue);
        QIQI_TEST_CHECK(document_a->Push(fresh.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(fresh.Get(), 'F', 0, &handled) == S_OK && handled);
        const auto views_before_stale = context_b->get_active_view_calls_;
        const auto ends_before_stale = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_b->range_->text_ == L"cd" && context_b->get_active_view_calls_ == views_before_stale);
        QIQI_TEST_CHECK(mock_tm->end_ui_calls_ == ends_before_stale);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fresh->range_->text_ == L"f");
        QIQI_TEST_CHECK(composition_b->NotifyHostTermination(23) == S_OK);
        QIQI_TEST_CHECK(fresh->last_composition_ && !fresh->last_composition_->ended_);

        const auto requests_before_uninit = fresh->request_edit_session_calls_;
        fresh->edit_session_request_result_ = TF_E_DISCONNECTED;
        fresh->document_link_.reset();
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(fresh->request_edit_session_calls_ == requests_before_uninit);
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(document_a.Get()) == S_OK);
        auto fallback = document_b->CreateTestContext();
        QIQI_TEST_CHECK(document_b->Push(fallback.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'G', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(fallback->range_->text_ == L"g");
        document_a->get_top_result = E_ACCESSDENIED;
        QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
        QIQI_TEST_CHECK(fallback->last_composition_->ended_);
        document_a->get_top_result = S_OK;
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'H', 0, &handled) == S_OK && handled);
        document_a->get_top_callback = [&] { QIQI_TEST_CHECK(service->Deactivate() == S_OK); };
        QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
        document_a->get_top_callback = {};
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'I', 0, &handled) == S_OK && !handled);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }

    // Focus A -> B -> A must preserve the asynchronous old-cleanup barrier.
    for (const bool remove_old_context : {false, true}) {
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto document_a = MakeComObject<MockTfDocumentMgr>();
        auto document_b = MakeComObject<MockTfDocumentMgr>();
        auto context_a = document_a->CreateTestContext(queue);
        auto context_b = document_b->CreateTestContext(queue);
        QIQI_TEST_CHECK(document_a->Push(context_a.Get()) == S_OK && document_b->Push(context_b.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'A', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        const auto old_composition = context_a->last_composition_;
        context_a->allow_sync_edit_sessions_ = false;
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && !old_composition->ended_);
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'B', 0, &handled) == S_OK && handled);
        if (remove_old_context) {
            context_a->edit_session_request_result_ = TF_E_DISCONNECTED;
            QIQI_TEST_CHECK(document_a->Pop(TF_POPF_ALL) == S_OK);
            QIQI_TEST_CHECK(service->OnPopContext(context_a.Get()) == S_OK);
            const auto clears = context_a->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            QIQI_TEST_CHECK(!old_composition->ended_ && context_a->prop_->clear_calls_ == clears);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"b");
            QIQI_TEST_CHECK(old_composition->NotifyHostTermination(23) == S_OK);
            QIQI_TEST_CHECK(!context_b->last_composition_->ended_);
        } else {
            QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
            QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'C', 0, &handled) == S_OK && handled);
            QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 2).flags == (TF_ES_ASYNC | TF_ES_READWRITE));
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && old_composition->ended_);
            const auto cleared_old_attribute = context_a->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->start_composition_calls_ == 0);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_a->range_->text_ == L"c");
            QIQI_TEST_CHECK(context_a->prop_->clear_calls_ == cleared_old_attribute);
            QIQI_TEST_CHECK(context_a->last_composition_ != old_composition);
            context_a->allow_sync_edit_sessions_ = true;
        }
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK && queue->PendingCount() == 0);
    }

    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        document->events = service.get();
        const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &handled) == S_OK && handled);
        const auto composition = context->last_composition_;
        const auto requests_before_removal = context->request_edit_session_calls_;
        composition->get_range_callback_ = [&] {
            context->edit_session_request_result_ = TF_E_DISCONNECTED;
            QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
        };
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        composition->get_range_callback_ = {};
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before_removal);
        QIQI_TEST_CHECK(composition->end_composition_calls_ == 0 && context->prop_->clear_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == document_objects_before);
    TestAdditionalContextLifecycleCoverage(service);
    TestContextFsmKeyPrediction(service);
    TestDeactivationWindowLifecycle();
    TestReentrantCompositionEdits();

    // Deactivation preserves preedit text and ends only the captured host composition.
    {
        enum class Failure { None, Synchronous, ReadOnly, Request, End, Range, Property, PropertyQuery };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const auto failure : {Failure::None, Failure::Synchronous, Failure::ReadOnly, Failure::Request,
                                   Failure::End, Failure::Range, Failure::Property, Failure::PropertyQuery}) {
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL cleanup_eaten = FALSE;
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto composition = context->last_composition_;
            QIQI_TEST_CHECK(composition && !composition->ended_);
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 1);
                if (failure != Failure::Synchronous) QIQI_TEST_CHECK(context->in_immediate_write_session_);
            };
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            context->allow_sync_edit_sessions_ = failure != Failure::Synchronous;
            context->read_only_ = failure == Failure::ReadOnly;
            if (failure == Failure::Request) context->edit_session_request_result_ = E_ACCESSDENIED;
            if (failure == Failure::End) composition->end_result_ = E_FAIL;
            if (failure == Failure::Range) composition->fail_get_range_ = true;
            if (failure == Failure::Property) context->prop_->clear_result_ = E_ACCESSDENIED;
            if (failure == Failure::PropertyQuery) context->get_property_result_ = E_ACCESSDENIED;
            const auto requests_before = context->request_edit_session_calls_;
            const auto writes_before = context->range_->set_text_calls_;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            const auto cleanup_requests = failure == Failure::Synchronous ? 2u : 1u;
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(context->last_edit_session_flags_ ==
                            gsl::narrow_cast<DWORD>(
                                (failure == Failure::Synchronous ? TF_ES_ASYNC : TF_ES_SYNC) | TF_ES_READWRITE));
            QIQI_TEST_CHECK(queue->PendingCount() == (failure == Failure::Synchronous ? 2u : 1u) && !context->layout_sink_);
            const bool can_edit = failure != Failure::Synchronous && failure != Failure::ReadOnly &&
                                  failure != Failure::Request;
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(composition->ended_ == (can_edit && failure != Failure::End));
            const bool can_clear = can_edit && failure != Failure::Range && failure != Failure::PropertyQuery;
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == (can_clear ? 1u : 0u));
            if (can_clear) QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 1);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(mock_tm->last_ui_element_->IsShown(&shown) == S_OK && shown == FALSE);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            if (failure == Failure::Synchronous) {
                const auto ui_before_cleanup = std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
                QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
                QIQI_TEST_CHECK((std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before_cleanup);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
        }

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto host_context = MakeComObject<MockTfContext>();
        BOOL cleanup_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(host_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto host_composition = host_context->last_composition_;
        const auto host_requests = host_context->request_edit_session_calls_;
        QIQI_TEST_CHECK(host_composition->NotifyHostTermination(17) == S_OK);
        QIQI_TEST_CHECK(host_composition->ended_ && host_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(host_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(host_context->prop_->clear_calls_ == 1 && host_context->prop_->last_clear_cookie_ == 17);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(host_context->request_edit_session_calls_ == host_requests);
        QIQI_TEST_CHECK(host_composition->end_composition_calls_ == 0);

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto reentrant_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(reentrant_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto reentrant_composition = reentrant_context->last_composition_;
        reentrant_composition->end_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(reentrant_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == E_UNEXPECTED);
            QIQI_TEST_CHECK(reentrant_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(reentrant_composition->ended_ && reentrant_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(reentrant_context->prop_->clear_calls_ == 1 && reentrant_context->range_->text_ == L"a");
        reentrant_composition->end_callback_ = {};

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto interrupted_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(interrupted_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto interrupted_composition = interrupted_context->last_composition_;
        interrupted_context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(interrupted_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(interrupted_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(interrupted_composition->ended_ && interrupted_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(interrupted_context->prop_->clear_calls_ == 1 && interrupted_context->range_->text_ == L"a");
        interrupted_context->prop_->clear_callback_ = {};

        // Late termination of A must not reset B's composition or candidate UI.
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
        auto previous_context = MakeComObject<MockTfContext>();
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(previous_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto previous_composition = previous_context->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto current_composition = current_context->last_composition_;
        const auto current_text = current_context->range_->text_;
        const auto ui_ends = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(previous_composition->NotifyHostTermination(19) == S_OK);
        QIQI_TEST_CHECK(current_context->range_->text_ == current_text && mock_tm->end_ui_calls_ == ui_ends);
        QIQI_TEST_CHECK(!current_composition->ended_);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(current_composition->ended_ && current_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }

    // Reentrant dispatch behind unfinished cleanup must retain the requeued task's snapshot.
    {
        const auto objects_before = g_com_object_count.load();
        {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto manager = MakeComObject<MockTfThreadMgr>();
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto old_composition = context->last_composition_;
            const auto writes_before = context->range_->set_text_calls_;
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(peer->OnSetFocus(FALSE) == S_OK && queue->PendingCount() == 1);
            bool reentered = false;
            old_composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 53 && !std::exchange(reentered, true));
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 1 &&
                                queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                const auto ui_before = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
                QIQI_TEST_CHECK(queue->RunNext(59) == S_OK && queue->PendingCount() == 1);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before &&
                                context->start_composition_calls_ == 1);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->PendingRequests().front().flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'C', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(queue->PendingCount() == 2);
            };
            QIQI_TEST_CHECK(queue->RunNext(53) == S_OK && reentered && queue->PendingCount() == 2);
            old_composition->end_callback_ = {};
            QIQI_TEST_CHECK(old_composition->ended_ && old_composition->end_composition_calls_ == 1 &&
                            old_composition->last_end_cookie_ == 53);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 53);
            QIQI_TEST_CHECK(queue->RunNext(61) == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(context->range_->text_ == L"b" && context->start_composition_calls_ == 2);
            QIQI_TEST_CHECK(queue->RunNext(67) == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"bc" && context->start_composition_calls_ == 2);
            context->allow_sync_edit_sessions_ = true;
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK && context->last_composition_->ended_);
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
    }

    // Stale edits cancel, but detached cleanup must still precede the next activation's writes.
    {
        const auto initial_objects = g_com_object_count.load();
        for (const UINT stale_key : {0u, UINT{VK_SPACE}, UINT{VK_ESCAPE}}) {
            for (const bool execute_cleanup : {false, true}) {
                auto queue = std::make_shared<MockEditSessionQueue>();
                auto context = MakeComObject<MockTfContext>(queue);
                auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
                auto manager = MakeComObject<MockTfThreadMgr>();
                BOOL eaten_deferred = FALSE;
                QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
                const auto old_composition = context->last_composition_;
                if (stale_key == 0) {
                    QIQI_TEST_CHECK(peer->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
                } else {
                    QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), stale_key, 0, &eaten_deferred) == S_OK &&
                                    eaten_deferred);
                }
                QIQI_TEST_CHECK(queue->PendingCount() == 1);
                context->allow_sync_edit_sessions_ = false;
                QIQI_TEST_CHECK(peer->Deactivate() == S_OK && queue->PendingCount() == 2);
                const auto writes_before = context->range_->set_text_calls_;
                const auto views_before = context->get_active_view_calls_;
                if (!execute_cleanup) {
                    peer.Reset();
                    manager.Reset();
                    QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && queue->PendingCount() == 0);
                    QIQI_TEST_CHECK(!old_composition->ended_ && old_composition->end_composition_calls_ == 0);
                    QIQI_TEST_CHECK(context->prop_->clear_calls_ == 0 && context->range_->text_ == L"a");
                    QIQI_TEST_CHECK(context->range_->set_text_calls_ == writes_before &&
                                    context->get_active_view_calls_ == views_before);
                    continue;
                }
                QIQI_TEST_CHECK(peer->Activate(manager.Get(), 2) == S_OK);
                QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'B', 0, &eaten_deferred) == S_OK && eaten_deferred);
                QIQI_TEST_CHECK(queue->PendingCount() == 3);
                QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 2).flags == (TF_ES_ASYNC | TF_ES_READWRITE));
                const auto ui_before = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
                QIQI_TEST_CHECK(queue->RunNext(31) == S_OK && queue->PendingCount() == 2);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before);
                QIQI_TEST_CHECK(context->get_active_view_calls_ == views_before &&
                                context->start_composition_calls_ == 1);
                QIQI_TEST_CHECK(!old_composition->ended_ && context->prop_->clear_calls_ == 0);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->RunNext(37) == S_OK && queue->PendingCount() == 1);
                QIQI_TEST_CHECK(old_composition->ended_ && old_composition->end_composition_calls_ == 1);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 37);
                QIQI_TEST_CHECK(context->range_->text_ == L"a" &&
                                context->range_->set_text_calls_ == writes_before);
                QIQI_TEST_CHECK((std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == ui_before);
                QIQI_TEST_CHECK(queue->RunNext(41) == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(context->range_->text_ == L"b" && context->start_composition_calls_ == 2);
                QIQI_TEST_CHECK(context->last_composition_.Get() != old_composition.Get() &&
                                !context->last_composition_->ended_ && context->prop_->clear_calls_ == 1);
                context->allow_sync_edit_sessions_ = true;
                QIQI_TEST_CHECK(peer->Deactivate() == S_OK && context->last_composition_->ended_);
            }
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
    }

    // Deferred cleanup survives deactivation, but cannot clear a new composition's attributes.
    {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const bool host_terminates : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto old_composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(!old_composition->ended_);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            if (host_terminates) {
                QIQI_TEST_CHECK(old_composition->NotifyHostTermination(23) == S_OK);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 23);
            }
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->PendingCount() == 2 && context->range_->text_ == L"a");
            if (!host_terminates) {
                QIQI_TEST_CHECK(context->last_edit_session_flags_ == (TF_ES_ASYNC | TF_ES_READWRITE));
            }
            const auto ui_before = std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
            QIQI_TEST_CHECK(queue->RunNext(29) == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(old_composition->end_composition_calls_ == (host_terminates ? 0u : 1u));
            QIQI_TEST_CHECK(old_composition->ended_ && context->range_->text_ == L"a");
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            QIQI_TEST_CHECK((std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"b");
            const auto new_composition = context->last_composition_;
            QIQI_TEST_CHECK(new_composition.Get() != old_composition.Get() && !new_composition->ended_);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            context->allow_sync_edit_sessions_ = true;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && new_composition->ended_);
        }

        for (const HRESULT failure : {E_ACCESSDENIED, TF_E_DISCONNECTED}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            context->async_edit_session_result_ = failure;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(!composition->ended_ && context->prop_->clear_calls_ == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && !context->layout_sink_);
        }
        const auto initial_objects = g_com_object_count.load();
        for (const bool execute_cleanup : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto peer_manager = MakeComObject<MockTfThreadMgr>();
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(peer->Activate(peer_manager.Get(), 2) == S_OK);
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK && queue->PendingCount() == 1);
            peer.Reset();
            peer_manager.Reset();
            if (execute_cleanup) {
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(!context->last_composition_->ended_);
            }
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }

    // Real TSF, private document, no system TIP activation: cleanup must remove host composition state.
    {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        ComPtr<ITfThreadMgr2> native_manager;
        QIQI_TEST_CHECK(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&native_manager)) == S_OK);
        TfClientId native_client = TF_CLIENTID_NULL;
        QIQI_TEST_CHECK(native_manager->ActivateEx(&native_client, TF_TMAE_NOACTIVATETIP) == S_OK);
        const auto deactivate_native = gsl::finally([&]() noexcept {
            std::ignore = log::CheckHResult(native_manager->Deactivate(), "NativeCleanupTest::Deactivate");
        });
        ComPtr<ITfDocumentMgr> document;
        QIQI_TEST_CHECK(native_manager->CreateDocumentMgr(&document) == S_OK);
        ComPtr<ITfContext> context;
        TfEditCookie owner_cookie = 0;
        QIQI_TEST_CHECK(document->CreateContext(native_client, 0, nullptr, &context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        const auto pop_native = gsl::finally([&]() noexcept {
            std::ignore = log::CheckHResult(document->Pop(TF_POPF_ALL), "NativeCleanupTest::Pop");
        });
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), native_client) == S_OK);
        BOOL native_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &native_eaten) == S_OK && native_eaten);
        const auto before = WaitForNativeTsfState(context.Get(), native_client, L"a", 1);
        QIQI_TEST_CHECK(before.composing && before.display_attribute);
        ComPtr<ITfContextOwnerServices> owner_services;
        QIQI_TEST_CHECK(context.As(&owner_services) == S_OK);
        const auto cleanup_layout_sink = MakeComObject<NativeCleanupLayoutSink>();
        ScopedTfSinkConnection layout_connection;
        QIQI_TEST_CHECK(layout_connection.Advise(context.Get(), IID_ITfTextLayoutSink, cleanup_layout_sink.Get()));
        cleanup_layout_sink->callback = [&]() -> HRESULT {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            const auto pending = ReadNativeTsfState(context.Get(), native_client);
            QIQI_TEST_CHECK(pending.text == L"a" && pending.composition_count == 1);
            return S_OK;
        };
        QIQI_TEST_CHECK(owner_services->OnLayoutChange() == S_OK && cleanup_layout_sink->called);
        const auto ended = WaitForNativeTsfState(context.Get(), native_client, L"a", 0);
        QIQI_TEST_CHECK(!ended.composing && !ended.display_attribute);

        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), native_client) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aa", 1).composing);
        cleanup_layout_sink->called = false;
        cleanup_layout_sink->callback = [&]() -> HRESULT {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), native_client) == S_OK);
            const auto ui_before = std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &native_eaten) == S_OK && native_eaten);
            QIQI_TEST_CHECK((std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
            QIQI_TEST_CHECK(ReadNativeTsfState(context.Get(), native_client).text == L"aa");
            return S_OK;
        };
        QIQI_TEST_CHECK(owner_services->OnLayoutChange() == S_OK && cleanup_layout_sink->called);
        const auto fresh = WaitForNativeTsfState(context.Get(), native_client, L"aab", 1);
        QIQI_TEST_CHECK(fresh.composition_text == L"b" && fresh.composing && fresh.display_attribute);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        const auto final_state = WaitForNativeTsfState(context.Get(), native_client, L"aab", 0);
        QIQI_TEST_CHECK(!final_state.composing && !final_state.display_attribute);
        layout_connection.Reset();
        cleanup_layout_sink->callback = {};

        // Forward actual stack notifications without changing the user's native document focus.
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), native_client) == S_OK);
        const auto document_events = MakeComObject<NativeDocumentEventSink>(service);
        ScopedTfSinkConnection document_connection;
        QIQI_TEST_CHECK(document_connection.Advise(native_manager.Get(), IID_ITfThreadMgrEventSink, document_events.Get()));
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'C', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aabc", 1).composing);
        ComPtr<ITfContext> modal_context;
        QIQI_TEST_CHECK(document->CreateContext(native_client, 0, nullptr, &modal_context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(document->Push(modal_context.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), document.Get()) == S_OK);
        QIQI_TEST_CHECK(document->Pop(0) == S_OK);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aabc", 1).display_attribute);
        ComPtr<ITfDocumentMgr> second_document;
        ComPtr<ITfContext> second_context;
        QIQI_TEST_CHECK(native_manager->CreateDocumentMgr(&second_document) == S_OK);
        QIQI_TEST_CHECK(second_document->CreateContext(
            native_client, 0, nullptr, &second_context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(second_document->Push(second_context.Get()) == S_OK);
        bool second_on_stack = true;
        const auto pop_second = gsl::finally([&]() noexcept {
            if (second_on_stack) {
                std::ignore = log::CheckHResult(second_document->Pop(TF_POPF_ALL), "NativeDocumentTest::Pop");
            }
        });
        QIQI_TEST_CHECK(service->OnSetFocus(second_document.Get(), document.Get()) == S_OK);
        const auto switched = WaitForNativeTsfState(context.Get(), native_client, L"aabc", 0);
        QIQI_TEST_CHECK(!switched.composing && !switched.display_attribute);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(second_context.Get(), 'D', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(second_context.Get(), native_client, L"d", 1).composing);
        const auto ui_ends_before_pop = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(second_document->Pop(TF_POPF_ALL) == S_OK);
        second_on_stack = false;
        QIQI_TEST_CHECK(document_events->pushes == 2 && document_events->pops == 2 && document_events->uninits == 1);
        QIQI_TEST_CHECK(mock_tm->end_ui_calls_ > ui_ends_before_pop);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), VK_BACK, 0, &native_eaten) == S_OK && !native_eaten);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        document_connection.Reset();
        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
    }
}

} // namespace qiqi::test
