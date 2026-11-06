#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include "common/guids.h"
#include "ime/comp/tests.h"
#include <array>
#include <commctrl.h>
#include <memory>
#include <utility>

namespace qiqi::test {

namespace {

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
