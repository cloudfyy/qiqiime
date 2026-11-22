// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scope.h"
#include "ime/svc/service.h"

#include <iostream>
#include <memory>

namespace qiqi::test::restricted_detail {

void ObserveCurrentService() {
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const auto queue = std::make_shared<MockEditSessionQueue>();
    const auto context = document->CreateTestContext(queue);
    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
    });
    ComPtr<ITfCompartment> disabled;
    QIQI_TEST_CHECK(context->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled) == S_OK);
    BOOL eaten = FALSE;
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(queue->PendingCount() == 1);
    QIQI_TEST_CHECK(WriteCompartmentInt4(disabled.Get(), 1, 1) == S_OK);
    QIQI_TEST_CHECK(queue->RunNext() == S_OK);
    QIQI_TEST_CHECK(context->start_composition_calls_ == 0);
    std::cout << "[OBSERVE] queued-write-after-disable: composition_started="
              << (context->start_composition_calls_ != 0) << '\n';
    const auto requests = context->request_edit_session_calls_;
    const auto ui_ends = manager->end_ui_calls_;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-test-keydown: eaten=" << eaten << '\n';
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-keydown: eaten=" << eaten
              << " cleanup_requested=" << (context->request_edit_session_calls_ != requests)
              << " candidate_ended=" << (manager->end_ui_calls_ != ui_ends) << '\n';
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);

    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-shift-test-keyup: eaten=" << eaten << '\n';
    const auto mode_before = manager->convmode_compartment_->last_val_;
    QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(manager->convmode_compartment_->last_val_ == mode_before);
    std::cout << "[OBSERVE] disabled-shift-keyup: eaten=" << eaten
              << " mode_changed=" << (manager->convmode_compartment_->last_val_ != mode_before) << '\n';
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-preserved-toggle: eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] compartment-read-error=0x80070005: test_keydown_eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = S_FALSE;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
    std::cout << "[OBSERVE] compartment-unset=S_FALSE/VT_EMPTY: test_keydown_eaten=" << eaten << '\n';
}

} // namespace qiqi::test::restricted_detail
