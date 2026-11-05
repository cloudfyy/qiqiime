#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include "common/file_util.h"
#include <array>
#include <iostream>
#include <memory>
#include <tuple>
#include <utility>

namespace qiqi::test {

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
            if (transition == Transition::ContinueSameContext && boundary != Boundary::End && !host_termination) continue;
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
                    if (host_termination) {
                        QIQI_TEST_CHECK(send_key(service, context.Get(), VK_ESCAPE, &eaten) == S_OK && eaten);
                    }
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
    QIQI_TEST_CHECK(edit_cases == 89);
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

} // namespace qiqi::test
