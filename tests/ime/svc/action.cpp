// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/svc/lifecycle.h"
#include "ime/svc/service.h"
#include <array>
#include <tuple>
#include <utility>

namespace qiqi::test {

void RunTsfEditActionTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Transition { None, Deactivate, Reactivate, ReplaceComposition };
    const QiQiConfig saved_config = LoadConfig(g_dll_instance);
    auto config = saved_config;
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.auto_commit_4code = false;
    config.push_commit_5th = true;
    config.clear_on_empty_4code = false;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.show_status_bar = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config));
    });
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const UINT key : {UINT{VK_SPACE}, UINT{VK_ESCAPE}, UINT{'Q'}}) {
        for (const auto transition : {
                 Transition::None, Transition::Deactivate, Transition::Reactivate, Transition::ReplaceComposition}) {
            const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            const auto deactivate = gsl::finally([&]() noexcept {
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            });
            const auto context = MakeComObject<MockTfContext>();
            const auto send_key = [&](UINT input) {
                QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
                BOOL eaten = FALSE;
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), input, 0, &eaten) == S_OK && eaten);
            };
            for (unsigned int index = 0; index != 4; ++index) send_key('A');
            QIQI_TEST_CHECK(context->range_->text_ == L"aaaa");
            const auto composition = context->last_composition_;
            const auto* const ui = dynamic_cast<const CandidateUI*>(manager->last_ui_element_.Get());
            QIQI_TEST_CHECK(ui);
            const DWORD old_id = ui->UiElementId();
            const auto before_ui = std::tuple{
                manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            auto after_reentry = before_ui;
            bool entered = false;
            const auto clear_callback = gsl::finally([&]() noexcept { composition->end_callback_ = {}; });
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 1);
                if (std::exchange(entered, true)) return;
                if (transition == Transition::Deactivate || transition == Transition::Reactivate) {
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (transition == Transition::Reactivate) {
                        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
                        send_key('C');
                    }
                } else if (transition == Transition::ReplaceComposition) {
                    send_key('C');
                }
                after_reentry = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            };
            send_key(key);
            composition->end_callback_ = {};
            QIQI_TEST_CHECK(entered && composition->ended_ && composition->end_composition_calls_ == 1);
            const auto after_ui = std::tuple{
                manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            if (transition == Transition::None) {
                if (key == 'Q') {
                    QIQI_TEST_CHECK(context->range_->text_ == L"q" &&
                                    context->last_composition_.Get() != composition.Get() &&
                                    !context->last_composition_->ended_ && ui->UiElementId() == old_id &&
                                    manager->end_ui_calls_ == std::get<2>(before_ui) &&
                                    manager->begin_ui_calls_ == std::get<0>(before_ui));
                } else {
                    QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                    manager->end_ui_calls_ == std::get<2>(before_ui) + 1 &&
                                    context->range_->text_.empty() == (key == VK_ESCAPE));
                }
            } else {
                QIQI_TEST_CHECK(after_ui == after_reentry);
                if (transition == Transition::Deactivate) {
                    BOOL eaten = TRUE;
                    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten &&
                                    !ui->WindowHandle() && ui->UiElementId() == TF_INVALID_UIELEMENTID);
                } else {
                    const bool pushed_replacement = key == 'Q' && transition == Transition::ReplaceComposition;
                    QIQI_TEST_CHECK(context->range_->text_ == (pushed_replacement ? L"qc" : L"c") &&
                                    context->last_composition_.Get() != composition.Get() &&
                                    !context->last_composition_->ended_ && ui->UiElementId() != TF_INVALID_UIELEMENTID);
                }
            }
            if (transition != Transition::Deactivate) {
                send_key('D');
                const auto expected = transition == Transition::ReplaceComposition && key == 'Q' ? L"qcd" :
                    transition != Transition::None ? L"cd" : key == 'Q' ? L"qd" : L"d";
                QIQI_TEST_CHECK(context->range_->text_ == expected);
            }
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 12 && g_com_object_count.load() == objects_before);
}

void RunTsfPresentationSyncTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> manager) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary { Update, Position };
    enum class Transition { Deactivate, Reactivate, Replace };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    size_t cases = 0;
    for (const auto boundary : {Boundary::Update, Boundary::Position}) {
        for (const auto transition : {Transition::Deactivate, Transition::Reactivate, Transition::Replace}) {
            const auto context = MakeComObject<MockTfContext>();
            const auto send_key = [&](UINT key) {
                QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
                BOOL eaten = FALSE;
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
            };
            send_key('A');
            const auto composition = context->last_composition_;
            bool entered = false;
            std::wstring after_text;
            auto after_ui = std::tuple{
                manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            const auto interrupt = [&] {
                if (std::exchange(entered, true)) return;
                if (transition == Transition::Replace) {
                    send_key(VK_ESCAPE);
                    send_key('C');
                } else {
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (transition == Transition::Reactivate) {
                        QIQI_TEST_CHECK(service->Activate(manager.get(), 1) == S_OK);
                        send_key('C');
                    }
                }
                after_text = context->range_->text_;
                after_ui = std::tuple{
                    manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_};
            };
            const auto clear_callbacks = gsl::finally([&]() noexcept {
                composition->get_range_callback_ = {};
                context->view_->get_text_ext_callback_ = {};
            });
            if (boundary == Boundary::Update) composition->get_range_callback_ = interrupt;
            else context->view_->get_text_ext_callback_ = interrupt;
            send_key('B');
            composition->get_range_callback_ = {};
            context->view_->get_text_ext_callback_ = {};
            QIQI_TEST_CHECK(entered && composition->ended_ && context->range_->text_ == after_text &&
                            (std::tuple{manager->begin_ui_calls_, manager->update_ui_calls_, manager->end_ui_calls_}) == after_ui);
            if (transition != Transition::Deactivate) {
                QIQI_TEST_CHECK(context->range_->text_ == L"c" &&
                                context->last_composition_.Get() != composition.Get());
                send_key('D');
                QIQI_TEST_CHECK(context->range_->text_ == L"cd");
            }
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && service->Activate(manager.get(), 1) == S_OK);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 6 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
