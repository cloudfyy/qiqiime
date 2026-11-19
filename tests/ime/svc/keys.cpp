// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/test.h"

namespace qiqi::test::service_cases {

void TestCodeCompletionConfigReload() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) != kDefaultConfigRegSubKey);
    const QiQiConfig original = LoadConfig();
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(original));
    });
    QiQiConfig config = original;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.user_dict_path.clear();
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.show_status_bar = false;
    QIQI_TEST_CHECK(SaveConfig(config));
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    });
    WubiEngine expected;
    for (const bool enabled : {false, true}) {
        config.enable_code_completion = enabled;
        QIQI_TEST_CHECK(SaveConfig(config));
        QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
        expected.ApplyConfig(config);
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        for (const UINT key : {'W', 'Q'}) {
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        }
        ComPtr<ITfCandidateListUIElement> candidates;
        QIQI_TEST_CHECK(manager->last_ui_element_.As(&candidates) == S_OK && candidates);
        UINT count = 0;
        QIQI_TEST_CHECK(candidates->GetCount(&count) == S_OK);
        QIQI_TEST_CHECK(count == expected.LookupCandidates(L"wq").size());
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
    }
}

void TestModeSwitchCompositionPolicy(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> manager, bool commit) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const QiQiConfig original_config = LoadConfig();
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(original_config));
        QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    });
    QiQiConfig config = original_config;
    config.commit_on_english_switch = commit;
    config.candidate_layout = CandidateLayout::Horizontal;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    const ScopedWindow popup_owner(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 100, 100, 600, 400,
        nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(popup_owner);

    enum class Switch { Shift, PreservedKey, LangBarClick, LangBarMenu, OpenClose, Conversion };
    unsigned int cases = 0;
    for (const bool deferred : {false, true}) {
        for (const auto method : {Switch::Shift, Switch::PreservedKey, Switch::LangBarClick,
                                  Switch::LangBarMenu, Switch::OpenClose, Switch::Conversion}) {
            for (const std::wstring code : {L"a", L"ab", L"abc", L"zhello", L"z"}) {
                const auto case_name = std::format(
                    "mode-switch.commit-{}.deferred-{}.method-{}.case-{}",
                    commit, deferred, std::to_underlying(method), ++cases);
                const qiqi::test::TsfTestCaseTrace case_trace(case_name);
                const std::wstring expected = commit ? code : L"";
                const auto queue = deferred ? std::make_shared<MockEditSessionQueue>() : nullptr;
                auto context = MakeComObject<MockTfContext>(queue);
                context->view_->hwnd_ = popup_owner.Get();
                BOOL eaten = FALSE;
                for (const wchar_t ch : code) {
                    QIQI_TEST_CHECK(service->OnKeyDown(
                        context.Get(), gsl::narrow_cast<WPARAM>(ch - L'a' + L'A'), 0, &eaten) == S_OK && eaten);
                }
                if (queue) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                const auto composition = context->last_composition_;
                QIQI_TEST_CHECK(composition && !composition->ended_);
                if (cases == 1) {
                    const auto element = manager->last_ui_element_;
                    const auto* const ui = dynamic_cast<const CandidateUI*>(element.Get());
                    QIQI_TEST_CHECK(ui && ui->WindowHandle());
                    const int horizontal_height = ui->CurrentHeight();
                    config.candidate_layout = CandidateLayout::Vertical;
                    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
                    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
                    QIQI_TEST_CHECK(ui->CurrentHeight() > horizontal_height && !composition->ended_);
                    config.candidate_layout = CandidateLayout::Horizontal;
                    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
                    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
                    QIQI_TEST_CHECK(ui->CurrentHeight() == horizontal_height && !composition->ended_);
                }
                composition->end_callback_ = [&](TfEditCookie) {
                    BOOL nested_eaten = TRUE;
                    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'D', 0, &nested_eaten) == S_OK &&
                                    !nested_eaten);
                    const auto requests = context->request_edit_session_calls_;
                    const VARIANT value = MakeInt4Variant(0);
                    QIQI_TEST_CHECK(manager->openclose_compartment_->SetValue(1, &value) == S_OK);
                    QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests);
                };
                switch (method) {
                    case Switch::Shift:
                        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && eaten);
                        QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && eaten);
                        break;
                    case Switch::PreservedKey:
                        QIQI_TEST_CHECK(service->OnPreservedKey(
                            context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
                        break;
                    case Switch::LangBarClick:
                    case Switch::LangBarMenu: {
                        ComPtr<ITfLangBarItemButton> button;
                        QIQI_TEST_CHECK(manager->langbar_item_.As(&button) == S_OK && button);
                        if (method == Switch::LangBarClick) {
                            QIQI_TEST_CHECK(button->OnClick(TF_LBI_CLK_LEFT, POINT{}, nullptr) == S_OK);
                        } else {
                            QIQI_TEST_CHECK(button->OnMenuSelect(
                                static_cast<UINT>(LangBarMenuId::ToggleChinese)) == S_OK);
                        }
                        break;
                    }
                    case Switch::OpenClose: {
                        const VARIANT value = MakeInt4Variant(0);
                        QIQI_TEST_CHECK(manager->openclose_compartment_->SetValue(1, &value) == S_OK);
                        break;
                    }
                    case Switch::Conversion: {
                        const VARIANT value = MakeInt4Variant(
                            manager->convmode_compartment_->last_val_ & ~TF_CONVERSIONMODE_NATIVE);
                        QIQI_TEST_CHECK(manager->convmode_compartment_->SetValue(1, &value) == S_OK);
                        break;
                    }
                }
                QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 0);
                if (queue) {
                    while (queue->PendingCount() != 0) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                }
                composition->end_callback_ = {};
                QIQI_TEST_CHECK(context->range_->text_ == expected);
                QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                if (commit) QIQI_TEST_CHECK(context->prop_->clear_calls_ != 0);
                BOOL shown = TRUE;
                QIQI_TEST_CHECK(manager->last_ui_element_ &&
                                manager->last_ui_element_->IsShown(&shown) == S_OK && !shown);
                QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(context->range_->text_ == expected);
                QIQI_TEST_CHECK(service->OnPreservedKey(
                    context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
                if (queue) {
                    while (queue->PendingCount() != 0) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                }
                QIQI_TEST_CHECK(context->range_->text_.empty());
            }
        }
    }
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
}

void TestInputMethodSwitchCompositionPolicy() {
    const TsfTestCaseTrace trace(__func__);
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) != kDefaultConfigRegSubKey);
    const QiQiConfig original = LoadConfig();
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(original));
    });
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    enum class SwitchExit { Deactivation, OwnProfile, OtherProfile, KeyboardLayout };
    for (const bool commit : {false, true}) {
        auto config = original;
        config.commit_on_english_switch = commit;
        config.default_chinese_mode = true;
        config.default_full_width = false;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.show_status_bar = false;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
        for (const bool deferred : {false, true}) {
            for (const auto method : {SwitchExit::Deactivation, SwitchExit::OwnProfile,
                                      SwitchExit::OtherProfile, SwitchExit::KeyboardLayout}) {
                for (const auto* code : {L"ab", L"zhello"}) {
                    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
                    const auto manager = MakeComObject<MockTfThreadMgr>();
                    manager->show_own_ui_ = FALSE;
                    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK && manager->profile_sink_);
                    const auto deactivate = gsl::finally([&]() noexcept {
                        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    });
                    const auto queue = deferred ? std::make_shared<MockEditSessionQueue>() : nullptr;
                    const auto context = MakeComObject<MockTfContext>(queue);
                    BOOL eaten = FALSE;
                    for (const wchar_t key : std::wstring_view(code)) {
                        QIQI_TEST_CHECK(service->OnKeyDown(
                            context.Get(), gsl::narrow_cast<WPARAM>(key - L'a' + L'A'), 0, &eaten) == S_OK && eaten);
                    }
                    if (queue) {
                        while (queue->PendingCount()) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                    }
                    const auto composition = context->last_composition_;
                    QIQI_TEST_CHECK(composition && !composition->ended_ && context->range_->text_ == code);
                    const auto sink = manager->profile_sink_;
                    const auto notify = [&](REFCLSID clsid, REFGUID profile, DWORD flags) {
                        return sink->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, 0, clsid,
                                                 GUID_TFCAT_TIP_KEYBOARD, profile, nullptr, flags);
                    };
                    QIQI_TEST_CHECK(notify(CLSID_NULL, GUID_NULL, 0) == S_OK);
                    QIQI_TEST_CHECK(notify(kClsidQiQiTextService, kGuidCandidateUIElement, 0) == S_OK);
                    QIQI_TEST_CHECK(sink->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, 0, CLSID_NULL,
                        GUID_TFCAT_TIP_SPEECH, GUID_NULL, nullptr, TF_IPSINK_FLAG_ACTIVE) == S_OK);
                    QIQI_TEST_CHECK(!composition->ended_ && context->range_->text_ == code);
                    const auto writes_before = context->range_->set_text_calls_;
                    context->allow_sync_edit_sessions_ = !deferred;
                    if (method != SwitchExit::Deactivation) {
                        if (method == SwitchExit::OwnProfile) {
                            QIQI_TEST_CHECK(notify(kClsidQiQiTextService, kGuidProfileWubi86, 0) == S_OK);
                        } else if (method == SwitchExit::OtherProfile) {
                            QIQI_TEST_CHECK(notify(CLSID_NULL, kGuidCandidateUIElement, TF_IPSINK_FLAG_ACTIVE) == S_OK);
                        } else {
                            QIQI_TEST_CHECK(sink->OnActivated(TF_PROFILETYPE_KEYBOARDLAYOUT, 0,
                                CLSID_NULL, GUID_NULL, GUID_NULL, nullptr, TF_IPSINK_FLAG_ACTIVE) == S_OK);
                        }
                        QIQI_TEST_CHECK(notify(kClsidQiQiTextService, kGuidProfileWubi86, 0) == S_OK);
                    } else {
                        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                        QIQI_TEST_CHECK(!manager->profile_sink_ && manager->unadvise_profile_sink_calls_ == 1);
                    }
                    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'C', 0, &eaten) == S_OK && !eaten);
                    if (queue) {
                        QIQI_TEST_CHECK(queue->PendingCount() == 1 && !composition->ended_ &&
                                        context->range_->text_ == code);
                        while (queue->PendingCount()) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                    }
                    QIQI_TEST_CHECK(context->range_->text_ == (commit ? std::wstring(code) : L""));
                    QIQI_TEST_CHECK(context->range_->set_text_calls_ == writes_before + (commit ? 0u : 1u));
                    QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                    if (method != SwitchExit::Deactivation) {
                        QIQI_TEST_CHECK(notify(kClsidQiQiTextService, kGuidProfileWubi86, TF_IPSINK_FLAG_ACTIVE) == S_OK);
                        const auto next = MakeComObject<MockTfContext>();
                        QIQI_TEST_CHECK(service->OnKeyDown(next.Get(), 'D', 0, &eaten) == S_OK && eaten);
                        QIQI_TEST_CHECK(next->range_->text_ == L"d");
                    }
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    QIQI_TEST_CHECK(context->range_->set_text_calls_ == writes_before + (commit ? 0u : 1u));
                    QIQI_TEST_CHECK(service->Activate(manager.Get(), 2) == S_OK);
                    const auto next = MakeComObject<MockTfContext>();
                    QIQI_TEST_CHECK(service->OnKeyDown(next.Get(), 'D', 0, &eaten) == S_OK && eaten);
                    QIQI_TEST_CHECK(next->range_->text_ == L"d");
                    QIQI_TEST_CHECK(context->range_->text_ == (commit ? std::wstring(code) : L""));
                }
            }
        }
        if (!commit) {
           const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
           const auto manager = MakeComObject<MockTfThreadMgr>();
           manager->show_own_ui_ = FALSE;
           QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
           const auto deactivate = gsl::finally([&]() noexcept {
               QIQI_TEST_CHECK(service->Deactivate() == S_OK);
           });
           const auto queue = std::make_shared<MockEditSessionQueue>();
           const auto context = MakeComObject<MockTfContext>(queue);
           BOOL eaten = FALSE;
           QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
           QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
           const auto old_composition = context->last_composition_;
           const auto sink = manager->profile_sink_;
           context->allow_sync_edit_sessions_ = false;
           QIQI_TEST_CHECK(sink->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, 0, kClsidQiQiTextService,
               GUID_TFCAT_TIP_KEYBOARD, kGuidProfileWubi86, nullptr, 0) == S_OK);
           QIQI_TEST_CHECK(queue->PendingCount() == 1 && !old_composition->ended_);
           QIQI_TEST_CHECK(sink->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, 0, kClsidQiQiTextService,
               GUID_TFCAT_TIP_KEYBOARD, kGuidProfileWubi86, nullptr, TF_IPSINK_FLAG_ACTIVE) == S_OK);
           QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
           QIQI_TEST_CHECK(queue->PendingCount() == 2 && context->range_->text_ == L"a");
           QIQI_TEST_CHECK(queue->RunNext(89) == S_OK && queue->PendingCount() == 1);
           QIQI_TEST_CHECK(old_composition->ended_ && old_composition->end_composition_calls_ == 1 &&
                           context->range_->text_.empty());
           QIQI_TEST_CHECK(queue->RunNext(97) == S_OK && queue->PendingCount() == 0);
           const auto new_composition = context->last_composition_;
           QIQI_TEST_CHECK(new_composition.Get() != old_composition.Get() &&
                           !new_composition->ended_ && context->range_->text_ == L"b");
           QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
           QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
           QIQI_TEST_CHECK(new_composition->ended_ && context->range_->text_.empty());
        }
    }
    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    manager->fail_profile_sink_ = true;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == E_FAIL);
    QIQI_TEST_CHECK(!manager->key_event_sink_ && !manager->profile_sink_ &&
                    manager->preserved_keys_.empty() && manager->unadvise_thread_sink_calls_ == 1);
}

void TestFocusChangeCompositionPolicy() {
    const TsfTestCaseTrace trace(__func__);
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) != kDefaultConfigRegSubKey);
    const QiQiConfig original = LoadConfig();
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(original));
    });
    enum class FocusChange { Window, NoDocument, OtherDocument, OtherInputTarget, HostFirstWindow, HostFirstDocument };
    for (const bool commit : {false, true}) {
        auto config = original;
        config.commit_on_english_switch = commit;
        config.default_chinese_mode = true;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.show_status_bar = false;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
        for (const auto change : {FocusChange::Window, FocusChange::NoDocument,
                                  FocusChange::OtherDocument, FocusChange::OtherInputTarget,
                                  FocusChange::HostFirstWindow, FocusChange::HostFirstDocument}) {
            const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            manager->show_own_ui_ = FALSE;
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            const auto deactivate = gsl::finally([&]() noexcept {
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            });
            const auto queue = std::make_shared<MockEditSessionQueue>();
            const auto old_document = MakeComObject<MockTfDocumentMgr>();
            const auto new_document = MakeComObject<MockTfDocumentMgr>();
            const auto old_context = old_document->CreateTestContext(queue);
            const auto new_context = new_document->CreateTestContext(queue);
            QIQI_TEST_CHECK(old_document->Push(old_context.Get()) == S_OK);
            QIQI_TEST_CHECK(new_document->Push(new_context.Get()) == S_OK);
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(old_context.Get(), 'A', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && old_context->range_->text_ == L"a");
            const auto composition = old_context->last_composition_;
            const auto writes = old_context->range_->set_text_calls_;
            const auto old_ui = manager->last_ui_element_;
            QIQI_TEST_CHECK(composition && old_ui && !composition->ended_);
            QIQI_TEST_CHECK(service->OnSetFocus(old_document.Get(), old_document.Get()) == S_OK);
            QIQI_TEST_CHECK(!composition->ended_ && old_context->range_->set_text_calls_ == writes);
            const bool host_first = change == FocusChange::HostFirstWindow || change == FocusChange::HostFirstDocument;
            if (host_first) {
                QIQI_TEST_CHECK(composition->NotifyHostTermination(613) == S_OK);
                QIQI_TEST_CHECK(old_context->range_->text_ == (commit ? L"a" : L""));
                QIQI_TEST_CHECK(old_context->range_->set_text_calls_ == writes + (commit ? 0u : 1u));
                QIQI_TEST_CHECK(queue->PendingCount() == 0 && composition->end_composition_calls_ == 0);
            }
            old_context->allow_sync_edit_sessions_ = change != FocusChange::Window;
            switch (change) {
                case FocusChange::Window:
                case FocusChange::HostFirstWindow:
                    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
                    break;
                case FocusChange::NoDocument:
                    QIQI_TEST_CHECK(service->OnSetFocus(nullptr, old_document.Get()) == S_OK);
                    break;
                case FocusChange::OtherDocument:
                case FocusChange::HostFirstDocument:
                    QIQI_TEST_CHECK(service->OnSetFocus(new_document.Get(), old_document.Get()) == S_OK);
                    break;
                case FocusChange::OtherInputTarget:
                    QIQI_TEST_CHECK(service->OnKeyDown(new_context.Get(), 'B', 0, &eaten) == S_OK && eaten);
                    break;
            }
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(old_ui->IsShown(&shown) == S_OK && !shown);
            if (change == FocusChange::Window) {
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && !composition->ended_);
                QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            }
            QIQI_TEST_CHECK(old_context->range_->text_ == (commit ? L"a" : L""));
            QIQI_TEST_CHECK(old_context->range_->set_text_calls_ == writes + (commit ? 0u : 1u));
            QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == (host_first ? 0u : 1u));
            if (host_first) {
                old_context->range_->text_ = L"host-finalized";
                QIQI_TEST_CHECK(service->OnCompositionTerminated(617, composition.Get()) == S_OK);
                QIQI_TEST_CHECK(old_context->range_->text_ == L"host-finalized" &&
                                old_context->range_->set_text_calls_ == writes + (commit ? 0u : 1u));
            }
            if (change == FocusChange::OtherInputTarget) {
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && new_context->range_->text_ == L"b");
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0);
        }
    }
}

void TestShiftTapTracking() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    ShiftTapTracker tracker;
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.Sequence() == 0 && tracker.ElapsedMs(1000) == 0);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(1000, {})) == "no-candidate");

    for (const uint64_t elapsed : {uint64_t{0}, uint64_t{349}, uint64_t{350}, uint64_t{351}}) {
        tracker.Reset();
        const auto sequence = tracker.Sequence();
        QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1000, false, true, {})) == "armed");
        QIQI_TEST_CHECK(tracker.Sequence() == sequence + 1);
        const char* reason = tracker.ReleaseReason(1000 + elapsed, {});
        QIQI_TEST_CHECK((reason == nullptr) == (elapsed <= 350));
        if (reason) QIQI_TEST_CHECK(std::string_view(reason) == "timeout");
        QIQI_TEST_CHECK(tracker.IsCandidate() && tracker.ElapsedMs(1000 + elapsed) == elapsed);
    }

    tracker.Reset();
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1000, false, true, {})) == "armed");
    const auto sequence = tracker.Sequence();
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1220, false, true, {})) == "press-preserved");
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1349, true, true, {})) == "repeat-preserved");
    QIQI_TEST_CHECK(tracker.ReleaseReason(1350, {}) == nullptr && tracker.Sequence() == sequence);
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1351, true, true, {})) == "repeat-timeout");
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(1351) == 351);
    tracker.OnKeyDown(1352, true, true, {});
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.Sequence() == sequence);
    tracker.Reset();
    QIQI_TEST_CHECK(tracker.ElapsedMs(1352) == 0 && tracker.Sequence() == sequence);

    for (uint32_t mask = 0; mask < 8; ++mask) {
        const ShiftTapModifiers modifiers{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
        for (const bool enabled : {false, true}) {
            tracker.Reset();
            const char* expected = !enabled ? "disabled" : modifiers.ctrl ? "ctrl" :
                                   modifiers.alt ? "alt" : modifiers.mouse ? "mouse-button" : "armed";
            QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(2000, false, enabled, modifiers)) == expected);
            QIQI_TEST_CHECK(tracker.IsCandidate() == (enabled && mask == 0));
        }

        tracker.Reset();
        tracker.OnKeyDown(2000, false, true, {});
        const char* reason = tracker.ReleaseReason(2100, modifiers);
        if (mask == 0) {
            QIQI_TEST_CHECK(reason == nullptr);
        } else {
            const char* expected = modifiers.mouse ? "mouse-button" : modifiers.ctrl ? "ctrl" : "alt";
            QIQI_TEST_CHECK(reason && std::string_view(reason) == expected);
        }
        QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(2351, modifiers)) == "timeout");
    }

    tracker.Reset();
    tracker.OnKeyDown(3000, false, true, {});
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(3100, true, true, {false, false, true})) ==
                    "repeat-mouse-button");
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(3100) == 100);
    tracker.OnKeyDown(3200, false, true, {});
    tracker.Disarm();
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(3250) == 50);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(3250, {})) == "no-candidate");
    tracker.Reset();
    tracker.OnKeyDown(3300, true, true, {});
    QIQI_TEST_CHECK(!tracker.IsCandidate());
    tracker.OnKeyDown(0, false, true, {});
    QIQI_TEST_CHECK(tracker.ReleaseReason(350, {}) == nullptr);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(351, {})) == "timeout");
}

void TestPreservedKeyRegistrationLifetime(gsl::not_null<QiQiTextService*> service) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto manager = MakeComObject<MockTfThreadMgr>();
    const auto reset_counters = [&]() {
        manager->advise_key_sink_calls_ = 0;
        manager->unadvise_key_sink_calls_ = 0;
        manager->preserve_key_calls_ = 0;
        manager->unpreserve_key_calls_ = 0;
        manager->preserved_keys_.clear();
        manager->unpreserved_keys_.clear();
        manager->preserve_key_callback_ = {};
        manager->unpreserve_key_callback_ = {};
        manager->unadvise_key_sink_result_ = S_OK;
        manager->unpreserve_key_result_ = S_OK;
    };

    // 1. All succeed: exact 4 keys registered with client ID, unregistered on Deactivate, no duplicate on second Deactivate.
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 11) == S_OK);
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 1 && manager->key_sink_client_id_ == 11 && manager->key_event_sink_);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 4);
    for (const auto& entry : manager->preserved_keys_) {
        QIQI_TEST_CHECK(entry.client_id == 11);
    }
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(manager->unadvise_key_sink_calls_ == 1 && manager->last_unadvised_key_sink_client_id_ == 11 &&
                    !manager->key_event_sink_);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);

    // 2. Rollback when AdviseKeyEventSink or QueryInterface(ITfKeystrokeMgr) fails: no PreserveKey, UnpreserveKey, or UnadviseKeyEventSink.
    reset_counters();
    manager->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 12) == E_FAIL);
    manager->fail_query_key_mgr_ = false;
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 0 && manager->unpreserve_key_calls_ == 0);

    manager->fail_key_sink_ = true;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 12) == E_FAIL);
    manager->fail_key_sink_ = false;
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 1 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 0 && manager->unpreserve_key_calls_ == 0);

    // 3. Partial success: TF_E_ALREADY_EXISTS and E_OUTOFMEMORY are NOT claimed as owned and NOT unpreserved on Deactivate.
    reset_counters();
    manager->preserve_key_callback_ = [&](TfClientId tid, REFGUID rguid, const TF_PRESERVEDKEY& pk) -> HRESULT {
        if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) {
            return TF_E_ALREADY_EXISTS;
        }
        if (IsEqualGUID(rguid, kGuidPreservedKeyTogglePunctuation)) {
            return E_OUTOFMEMORY;
        }
        manager->preserved_keys_.push_back({tid, rguid, pk});
        return S_OK;
    };
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 13) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 2);
    // Preserved key dispatch remains functional.
    auto context = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 2 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(manager->unpreserved_keys_.size() == 2);
    QIQI_TEST_CHECK(IsEqualGUID(gsl::at(manager->unpreserved_keys_, 0).guid, kGuidPreservedKeyToggleFullWidth));
    QIQI_TEST_CHECK(IsEqualGUID(gsl::at(manager->unpreserved_keys_, 1).guid, kGuidPreservedKeyAddUserWord));
    QIQI_TEST_CHECK(manager->unadvise_key_sink_calls_ == 1 && manager->last_unadvised_key_sink_client_id_ == 13);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 2 && manager->unadvise_key_sink_calls_ == 1);

    // 4. All PreserveKey fail (e.g. TF_E_ALREADY_EXISTS / E_FAIL): key sink is still unadvised on Deactivate, 0 UnpreserveKey calls.
    reset_counters();
    manager->preserve_key_callback_ = [](TfClientId, REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        return IsEqualGUID(rguid, kGuidPreservedKeyToggleMode) ? TF_E_ALREADY_EXISTS : E_FAIL;
    };
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 14) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 1);

    // 5. UnpreserveKey and UnadviseKeyEventSink failures on Deactivate still retire local ownership and allow clean reactivation.
    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 15) == S_OK);
    QIQI_TEST_CHECK(manager->preserved_keys_.size() == 4);
    manager->unpreserve_key_callback_ = [&](REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        return IsEqualGUID(rguid, kGuidPreservedKeyToggleFullWidth) ? CONNECT_E_NOCONNECTION : E_FAIL;
    };
    manager->unadvise_key_sink_result_ = E_FAIL;
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);

    // 6. QueryInterface(ITfKeystrokeMgr) failure during UninitKeyEventSink retires local ownership without retrying on second Deactivate,
    //    and subsequent reactivation succeeds cleanly.
    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 16) == S_OK);
    manager->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    manager->fail_query_key_mgr_ = false;
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);

    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 17) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 4);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);
}

} // namespace qiqi::test::service_cases
