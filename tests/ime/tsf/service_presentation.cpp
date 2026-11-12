// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/service_lifecycle.h"
#include "ime/tsf/tip_service.h"
#include <algorithm>
#include <array>
#include <vector>

namespace qiqi::test {

void RunTsfCandidatePresentationTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Outcome { HRESULT result; bool invalid_id; };
    const std::array outcomes{
        Outcome{S_OK, false}, Outcome{S_FALSE, false}, Outcome{E_ACCESSDENIED, false},
        Outcome{S_OK, true}, Outcome{E_ACCESSDENIED, true},
    };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    size_t cases = 0;
    for (const auto& outcome : outcomes) {
        for (const bool reactivate : {false, true}) {
            const auto context = MakeComObject<MockTfContext>();
            const auto replacement_context = MakeComObject<MockTfContext>();
            const auto replacement = MakeComObject<MockTfThreadMgr>();
            const DWORD allocated_id = mock_tm->next_ui_element_id_;
            const auto ends_before = mock_tm->end_ui_calls_;
            const auto updates_before = mock_tm->update_ui_calls_;
            mock_tm->begin_ui_result_ = outcome.result;
            mock_tm->return_invalid_ui_id_ = outcome.invalid_id;
            const auto clear_controls = gsl::finally([&]() noexcept {
                mock_tm->begin_ui_callback_ = {};
                mock_tm->begin_ui_result_ = S_OK;
                mock_tm->return_invalid_ui_id_ = false;
            });
            unsigned int callbacks = 0;
            CandidateUI* ui = nullptr;
            BOOL eaten = FALSE;
            mock_tm->begin_ui_callback_ = [&](DWORD id) {
                QIQI_TEST_CHECK(++callbacks == 1 && id == (outcome.invalid_id ? TF_INVALID_UIELEMENTID : allocated_id));
                ui = dynamic_cast<CandidateUI*>(mock_tm->last_ui_element_.Get());
                QIQI_TEST_CHECK(ui && ui->UiElementId() == TF_INVALID_UIELEMENTID);
                QIQI_TEST_CHECK(service->Deactivate() == S_OK && !ui->WindowHandle());
                if (reactivate) {
                    replacement->next_ui_element_id_ = allocated_id;
                    QIQI_TEST_CHECK(service->Activate(replacement.Get(), 1) == S_OK);
                    QIQI_TEST_CHECK(service->OnKeyDown(replacement_context.Get(), 'C', 0, &eaten) == S_OK && eaten);
                }
            };
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            mock_tm->begin_ui_callback_ = {};
            mock_tm->begin_ui_result_ = S_OK;
            mock_tm->return_invalid_ui_id_ = false;
            const bool reclaimed = SUCCEEDED(outcome.result) && !outcome.invalid_id;
            QIQI_TEST_CHECK(callbacks == 1 && ui && mock_tm->end_ui_calls_ == ends_before + (reclaimed ? 1u : 0u) &&
                            mock_tm->update_ui_calls_ == updates_before &&
                            context->range_->text_ == L"a" && context->last_composition_->ended_);
            if (reclaimed) {
                QIQI_TEST_CHECK(std::count(mock_tm->ended_ui_ids_.begin(), mock_tm->ended_ui_ids_.end(), allocated_id) == 1);
            }
            if (reactivate) {
                QIQI_TEST_CHECK(ui->UiElementId() == allocated_id && replacement->end_ui_calls_ == 0 &&
                                ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()));
                QIQI_TEST_CHECK(service->OnKeyDown(replacement_context.Get(), 'D', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(replacement_context->range_->text_ == L"cd" && ui->UiElementId() == allocated_id &&
                                !replacement_context->last_composition_->ended_ && replacement->end_ui_calls_ == 0);
            } else {
                QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID && !ui->WindowHandle() &&
                                service->OnKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            if (reactivate) {
                QIQI_TEST_CHECK(replacement->ended_ui_ids_ == std::vector<DWORD>{allocated_id} &&
                                replacement_context->last_composition_->ended_);
            }
            QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 10 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
