#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include "common/guids.h"
#include "ime/comp/tests.h"
#include <array>
#include <format>
#include <commctrl.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace qiqi::test {

namespace {

void TestActivationTransitionBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Boundary {
        ThreadSink, KeySink, FirstKey, SecondKey, ThirdKey, FourthKey, FunctionProvider, LangBar,
        OpenBind, ConversionBind, GlobalManager, ConfigBind, PositionBind, PositionRead, OpenWrite, ConversionWrite
    };
    enum class Action { NestedActivation, Cancel, CancelTwice };
    using Stage = MockTfThreadMgr::ActivationStage;
    constexpr std::array stages{
        std::pair{Boundary::ThreadSink, Stage::ThreadSink}, std::pair{Boundary::KeySink, Stage::KeySink},
        std::pair{Boundary::FunctionProvider, Stage::FunctionProvider}, std::pair{Boundary::LangBar, Stage::LangBar},
        std::pair{Boundary::GlobalManager, Stage::GlobalCompartments},
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
    size_t cases = 0;
    for (const auto boundary : {
             Boundary::ThreadSink, Boundary::KeySink, Boundary::FirstKey, Boundary::SecondKey,
             Boundary::ThirdKey, Boundary::FourthKey, Boundary::FunctionProvider, Boundary::LangBar,
             Boundary::OpenBind, Boundary::ConversionBind, Boundary::GlobalManager, Boundary::ConfigBind,
             Boundary::PositionBind, Boundary::PositionRead, Boundary::OpenWrite, Boundary::ConversionWrite}) {
        for (const bool host_fails : {false, true}) {
            if (host_fails && boundary != Boundary::ThreadSink && boundary != Boundary::KeySink) continue;
            for (const auto action : {Action::NestedActivation, Action::Cancel, Action::CancelTwice}) {
                const auto case_name = std::format(
                    "activation.boundary-{}.action-{}.host-fails-{}",
                    std::to_underlying(boundary), std::to_underlying(action), host_fails);
                const qiqi::test::TsfTestCaseTrace case_trace(case_name);
                const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
                const auto manager = MakeComObject<MockTfThreadMgr>();
                const auto nested_manager = MakeComObject<MockTfThreadMgr>();
                const auto context = MakeComObject<MockTfContext>();
                manager->show_own_ui_ = FALSE;
                manager->openclose_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
                manager->convmode_compartment_ = MakeComObject<MockTfCompartment>(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
                manager->global_config_compartment_ = MakeComObject<MockTfCompartment>(kGuidCompartmentConfigVersion);
                manager->global_pos_compartment_ = MakeComObject<MockTfCompartment>(kGuidCompartmentStatusBarPos);
                const std::array compartments{
                    manager->openclose_compartment_, manager->convmode_compartment_,
                    manager->global_config_compartment_, manager->global_pos_compartment_,
                };
                const auto clear_callbacks = [&]() noexcept {
                    manager->activation_callback_ = {};
                    manager->preserve_key_callback_ = {};
                    for (const auto& compartment : compartments) {
                        compartment->advise_callback_ = {};
                        compartment->set_value_callback_ = {};
                        compartment->get_value_callback_ = {};
                    }
                };
                const auto deactivate = gsl::finally([&]() noexcept {
                    clear_callbacks();
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                });
                bool entered = false;
                const auto interrupt = [&] {
                    if (std::exchange(entered, true)) return;
                    if (action != Action::NestedActivation) {
                        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                        if (action == Action::CancelTwice) QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                        BOOL eaten = TRUE;
                        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(context->get_document_calls_ == 0 && context->request_edit_session_calls_ == 0);
                    }
                    QIQI_TEST_CHECK(service->Activate(nested_manager.Get(), 2) == E_UNEXPECTED);
                    QIQI_TEST_CHECK(nested_manager->advise_thread_sink_calls_ == 0 &&
                                    nested_manager->advise_key_sink_calls_ == 0);
                };
                manager->activation_callback_ = [&](Stage stage) {
                    for (const auto& [expected_boundary, expected_stage] : stages) {
                        if (boundary == expected_boundary && stage == expected_stage) interrupt();
                    }
                };
                manager->preserve_key_callback_ = [&](TfClientId client, REFGUID guid, const TF_PRESERVEDKEY& key) {
                    manager->preserved_keys_.push_back({client, guid, key});
                    const auto key_boundary = manager->preserve_key_calls_ == 1 ? Boundary::FirstKey :
                        manager->preserve_key_calls_ == 2 ? Boundary::SecondKey :
                        manager->preserve_key_calls_ == 3 ? Boundary::ThirdKey : Boundary::FourthKey;
                    if (boundary == key_boundary) interrupt();
                    return S_OK;
                };
                constexpr std::array bind_boundaries{
                    Boundary::OpenBind, Boundary::ConversionBind, Boundary::ConfigBind, Boundary::PositionBind};
                for (size_t i = 0; i < compartments.size(); ++i) {
                    gsl::at(compartments, i)->advise_callback_ = [&, i] {
                        if (boundary == gsl::at(bind_boundaries, i)) interrupt();
                    };
                }
                manager->global_pos_compartment_->get_value_callback_ = [&] {
                    if (boundary == Boundary::PositionRead) interrupt();
                };
                manager->openclose_compartment_->set_value_callback_ = [&] {
                    if (boundary == Boundary::OpenWrite) interrupt();
                };
                manager->convmode_compartment_->set_value_callback_ = [&] {
                    if (boundary == Boundary::ConversionWrite) interrupt();
                };
                manager->fail_advise_ = host_fails && boundary == Boundary::ThreadSink;
                manager->fail_key_sink_ = host_fails && boundary == Boundary::KeySink;
                const HRESULT result = service->Activate(manager.Get(), 1);
                const HRESULT expected = action == Action::NestedActivation ? (host_fails ? E_FAIL : S_OK) : E_ABORT;
                QIQI_TEST_CHECK(entered && result == expected);
                if (expected == S_OK) {
                    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 1 && manager->preserve_key_calls_ == 4);
                }
                clear_callbacks();
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                QIQI_TEST_CHECK(!manager->key_event_sink_ && !manager->single_sink_ && !manager->langbar_item_);
                QIQI_TEST_CHECK(manager->preserved_keys_.empty());
                QIQI_TEST_CHECK(manager->advise_thread_sink_calls_ == 1 &&
                                manager->unadvise_thread_sink_calls_ == (manager->fail_advise_ ? 0u : 1u));
                QIQI_TEST_CHECK(manager->unadvise_key_sink_calls_ ==
                                (manager->fail_key_sink_ ? 0u : manager->advise_key_sink_calls_));
                for (const auto& compartment : compartments) QIQI_TEST_CHECK(!compartment->sink_);
                if (action != Action::NestedActivation && boundary == Boundary::OpenWrite) {
                    QIQI_TEST_CHECK(manager->convmode_compartment_->set_value_calls_ == 0);
                }
                manager->fail_advise_ = false;
                manager->fail_key_sink_ = false;
                QIQI_TEST_CHECK(service->Activate(manager.Get(), 3) == S_OK);
                BOOL eaten = FALSE;
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(context->range_->text_ == L"b" && context->layout_sink_);
                QIQI_TEST_CHECK(service->Deactivate() == S_OK && !context->layout_sink_ &&
                                context->last_composition_->ended_ && manager->preserved_keys_.empty());
                ++cases;
            }
        }
    }
    {
        const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        manager->show_own_ui_ = FALSE;
        const auto deactivate = gsl::finally([&]() noexcept {
            manager->preserve_key_callback_ = {};
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        });
        manager->preserve_key_callback_ = [](TfClientId, REFGUID, const TF_PRESERVEDKEY&) -> HRESULT {
            throw std::runtime_error("injected activation failure");
        };
        bool caught = false;
        try {
            std::ignore = service->Activate(manager.Get(), 1);
        } catch (const std::runtime_error& error) {
            QIQI_TEST_CHECK(std::string_view(error.what()) == "injected activation failure");
            caught = true;
        }
        QIQI_TEST_CHECK(caught && service->Deactivate() == S_OK);
        manager->preserve_key_callback_ = {};
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 3) == S_OK);
        ++cases;
    }
    for (const bool during_key_registration : {false, true}) {
        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        bool entered = false;
        const auto deactivate = gsl::finally([&]() noexcept {
            manager->activation_callback_ = {};
            manager->preserve_key_callback_ = {};
            if (service) QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        });
        const auto release_caller = [&] {
            if (std::exchange(entered, true)) return;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            service.Reset();
        };
        manager->activation_callback_ = [&](Stage stage) {
            if (!during_key_registration && stage == Stage::ThreadSink) release_caller();
        };
        manager->preserve_key_callback_ = [&](TfClientId client, REFGUID guid, const TF_PRESERVEDKEY& key) {
            manager->preserved_keys_.push_back({client, guid, key});
            if (during_key_registration) release_caller();
            return S_OK;
        };
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == E_ABORT);
        QIQI_TEST_CHECK(entered && !service && !manager->key_event_sink_ && !manager->single_sink_ &&
                        !manager->langbar_item_ && manager->preserved_keys_.empty());
        ++cases;
    }
    QIQI_TEST_CHECK(cases == 57 && g_com_object_count.load() == objects_before);
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

} // namespace

void TestDeactivationWindowLifecycle() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestActivationTransitionBoundaries();
    RunTsfCompositionEditTests();
    RunTsfCompositionCleanupTests();
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

} // namespace qiqi::test
