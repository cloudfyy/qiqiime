// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/svc/lifecycle.h"
#include "ime/tsf/tip_service.h"
#include <array>
#include <tuple>

namespace qiqi::test {

void RunTsfTargetPreparationTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { Document, Advise };
    enum class Event { None, FocusLoss, Remove, Deactivate };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    size_t cases = 0;
    for (const auto boundary : {Boundary::Document, Boundary::Advise}) {
        for (const auto event : {Event::None, Event::FocusLoss, Event::Remove, Event::Deactivate}) {
            auto document = MakeComObject<MockTfDocumentMgr>();
            auto context = document->CreateTestContext();
            auto other_context = MakeComObject<MockTfContext>();
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            document->events = service.get();
            const auto disconnect = gsl::finally([&]() noexcept {
                document->events = nullptr;
                context->get_document_callback_ = {};
                context->layout_advise_callback_ = {};
            });
            unsigned int callbacks = 0;
            const auto callback = [&] {
                ++callbacks;
                QIQI_TEST_CHECK(callbacks == 1);
                const auto ui_before = std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
                for (ITfContext* target : {context.Get(), other_context.Get()}) {
                    BOOL nested_eaten = TRUE;
                    QIQI_TEST_CHECK(service->OnTestKeyDown(target, 'B', 0, &nested_eaten) == S_OK && !nested_eaten);
                    QIQI_TEST_CHECK(service->OnKeyDown(target, 'B', 0, &nested_eaten) == S_OK && !nested_eaten);
                }
                QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == E_UNEXPECTED);
                QIQI_TEST_CHECK((std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
                if (event == Event::FocusLoss) {
                    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
                } else if (event == Event::Remove) {
                    QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
                } else if (event == Event::Deactivate) {
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                }
            };
            if (boundary == Boundary::Document) context->get_document_callback_ = callback;
            else context->layout_advise_callback_ = callback;
            const auto document_calls_before = context->get_document_calls_;
            BOOL eaten = TRUE;
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK);
            const bool ready = event == Event::None;
            const unsigned int advised = ready || boundary == Boundary::Advise ? 1u : 0u;
            QIQI_TEST_CHECK((eaten != FALSE) == ready && callbacks == 1);
            QIQI_TEST_CHECK(context->get_document_calls_ == document_calls_before + 1 &&
                            context->layout_advise_calls_ == advised &&
                            context->layout_unadvise_calls_ == (ready ? 0u : advised));
            QIQI_TEST_CHECK((context->layout_sink_ != nullptr) == ready &&
                            context->request_edit_session_calls_ == (ready ? 1u : 0u) &&
                            context->start_composition_calls_ == (ready ? 1u : 0u));
            QIQI_TEST_CHECK(context->range_->text_ == (ready ? L"a" : L"") &&
                            other_context->get_document_calls_ == 0 &&
                            other_context->layout_advise_calls_ == 0 &&
                            other_context->request_edit_session_calls_ == 0 &&
                            other_context->range_->text_.empty());
            context->get_document_callback_ = {};
            context->layout_advise_callback_ = {};
            if (ready) {
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(context->range_->text_ == L"ad" &&
                                context->get_document_calls_ == document_calls_before + 1 &&
                                context->layout_advise_calls_ == 1);
            }
            if (event == Event::Deactivate) {
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'E', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            }
            QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
            auto fresh_document = MakeComObject<MockTfDocumentMgr>();
            auto fresh_context = fresh_document->CreateTestContext();
            QIQI_TEST_CHECK(fresh_document->Push(fresh_context.Get()) == S_OK);
            QIQI_TEST_CHECK(service->OnKeyDown(fresh_context.Get(), 'C', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(service->OnKeyDown(fresh_context.Get(), 'D', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(fresh_context->range_->text_ == L"cd" && fresh_context->layout_sink_ &&
                            context->range_->text_ == (ready ? L"ad" : L"") && callbacks == 1);
            QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK && !fresh_context->layout_sink_ &&
                            fresh_context->last_composition_->ended_ && !context->layout_sink_ &&
                            context->layout_unadvise_calls_ == advised);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 8 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
