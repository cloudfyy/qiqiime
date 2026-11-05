#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include "common/guids.h"
#include "ime/tsf/edit_session.h"
#include <iostream>
#include <tuple>

namespace qiqi::test {

namespace {

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

} // namespace

void RunTsfNativeLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink) {
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
