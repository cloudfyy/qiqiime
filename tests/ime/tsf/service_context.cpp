#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include <array>
#include <format>
#include <iostream>
#include <memory>
#include <tuple>

namespace qiqi::test {

namespace {

void TestPushedContextResolution(gsl::not_null<QiQiTextService*> service) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    for (const HRESULT result : {S_OK, S_FALSE, E_ACCESSDENIED, TF_E_DISCONNECTED}) {
        for (const bool null_document : {false, true}) {
            const auto case_name = std::format(
                "context-push.result-0x{:08x}.null-{}", gsl::narrow_cast<unsigned int>(result), null_document);
            const qiqi::test::TsfTestCaseTrace case_trace(case_name);
            const auto document = MakeComObject<MockTfDocumentMgr>();
            const auto context = document->CreateTestContext();
            const auto pushed = document->CreateTestContext();
            const auto reset = gsl::finally([&]() noexcept {
                QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
            });
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            const auto composition = context->last_composition_;
            QIQI_TEST_CHECK(document->Push(pushed.Get()) == S_OK);
            pushed->get_document_result_ = result;
            pushed->return_null_document_ = null_document;
            const auto parent_queries = pushed->get_document_calls_;
            const auto top_queries = document->get_top_calls;
            const auto base_queries = document->get_base_calls;
            QIQI_TEST_CHECK(service->OnPushContext(pushed.Get()) == S_OK);
            const auto reconciliations = SUCCEEDED(result) && !null_document ? 1u : 0u;
            QIQI_TEST_CHECK(pushed->get_document_calls_ == parent_queries + 1);
            QIQI_TEST_CHECK(document->get_top_calls == top_queries + reconciliations);
            QIQI_TEST_CHECK(document->get_base_calls == base_queries + reconciliations);
            QIQI_TEST_CHECK(!composition->ended_ && context->layout_sink_);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && pushed->request_edit_session_calls_ == 0);
            QIQI_TEST_CHECK(!pushed->layout_sink_);
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(context->last_composition_ == composition && context->range_->text_ == L"ab");
        }
    }
    {
        const qiqi::test::TsfTestCaseTrace case_trace("context-push.reentrant-focus-target");
        const auto document = MakeComObject<MockTfDocumentMgr>();
        const auto context = document->CreateTestContext();
        const auto pushed = document->CreateTestContext();
        const auto replacement_document = MakeComObject<MockTfDocumentMgr>();
        const auto replacement = replacement_document->CreateTestContext();
        const auto reset = gsl::finally([&]() noexcept {
            pushed->get_document_callback_ = {};
            QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        });
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        QIQI_TEST_CHECK(replacement_document->Push(replacement.Get()) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        const auto composition = context->last_composition_;
        QIQI_TEST_CHECK(document->Push(pushed.Get()) == S_OK);
        const auto top_queries = document->get_top_calls;
        const auto base_queries = document->get_base_calls;
        unsigned int callbacks = 0;
        pushed->get_document_callback_ = [&] {
            ++callbacks;
            QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
            QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'B', 0, &eaten) == S_OK && eaten);
        };
        QIQI_TEST_CHECK(service->OnPushContext(pushed.Get()) == S_OK);
        QIQI_TEST_CHECK(callbacks == 1 && composition->ended_ && !context->layout_sink_);
        QIQI_TEST_CHECK(document->get_top_calls == top_queries && document->get_base_calls == base_queries);
        QIQI_TEST_CHECK(replacement_document->get_top_calls == 0 && replacement_document->get_base_calls == 0);
        QIQI_TEST_CHECK(replacement->range_->text_ == L"b" && replacement->layout_sink_);
        QIQI_TEST_CHECK(!replacement->last_composition_->ended_ && pushed->request_edit_session_calls_ == 0);
        QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(replacement->range_->text_ == L"bc");
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestAdditionalContextLifecycleCoverage(gsl::not_null<QiQiTextService*> service) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestPushedContextResolution(service);
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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

} // namespace

void RunTsfDocumentLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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
}

} // namespace qiqi::test
