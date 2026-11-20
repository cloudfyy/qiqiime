// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/svc/service.h"
#include <ctffunc.h>
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace qiqi::test {
namespace {

std::wstring ReadCandidateString(ITfCandidateString* candidate) {
    ScopedBstr text;
    QIQI_TEST_CHECK(candidate && candidate->GetString(text.Put()) == S_OK &&
                    text.IsValid());
    return std::wstring(text.Get());
}

void TestSearchCandidateList() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    ComPtr<ITfFnSearchCandidateProvider> provider;
    ComPtr<IUnknown> unknown;
    QIQI_TEST_CHECK(service->GetFunction(
                        GUID_NULL, IID_ITfFnSearchCandidateProvider,
                        unknown.GetAddressOf()) == S_OK &&
                    unknown);
    QIQI_TEST_CHECK(unknown.As(&provider) == S_OK && provider);
    ComPtr<IUnknown> explicit_provider;
    QIQI_TEST_CHECK(service->GetFunction(
                        kClsidQiQiTextService,
                        IID_ITfFnSearchCandidateProvider,
                        explicit_provider.GetAddressOf()) == S_OK &&
                    explicit_provider);
    QIQI_TEST_CHECK(service->GetFunction(
                        kGuidCandidateUIElement,
                        IID_ITfFnSearchCandidateProvider,
                        unknown.ReleaseAndGetAddressOf()) == E_NOINTERFACE &&
                    !unknown);
    QIQI_TEST_CHECK(provider->GetSearchCandidates(
                        nullptr, nullptr, nullptr) == E_INVALIDARG);

    const ScopedBstr query(::SysAllocString(L"wq"));
    const ScopedBstr invalid_query(::SysAllocString(L"!"));
    const ScopedBstr empty_query(::SysAllocString(L""));
    const ScopedBstr app_id(::SysAllocString(L"search-test"));
    QIQI_TEST_CHECK(query && invalid_query && empty_query && app_id);
    ComPtr<ITfCandidateList> list;
    QIQI_TEST_CHECK(provider->GetSearchCandidates(
                        empty_query.Get(), app_id.Get(),
                        list.GetAddressOf()) == S_FALSE && !list);
    QIQI_TEST_CHECK(provider->GetSearchCandidates(
                        invalid_query.Get(), app_id.Get(),
                        list.GetAddressOf()) == S_FALSE && !list);
    QIQI_TEST_CHECK(provider->GetSearchCandidates(
                        query.Get(), app_id.Get(),
                        list.GetAddressOf()) == S_OK && list);

    ULONG count = 0;
    QIQI_TEST_CHECK(list->GetCandidateNum(&count) == S_OK &&
                    count > 0);
    ComPtr<ITfCandidateString> first;
    ComPtr<ITfCandidateString> last;
    QIQI_TEST_CHECK(list->GetCandidate(0, &first) == S_OK && first);
    QIQI_TEST_CHECK(list->GetCandidate(count - 1, &last) == S_OK && last);
    ULONG first_index = ULONG_MAX;
    ULONG last_index = ULONG_MAX;
    QIQI_TEST_CHECK(first->GetIndex(&first_index) == S_OK &&
                    first_index == 0);
    QIQI_TEST_CHECK(last->GetIndex(&last_index) == S_OK &&
                    last_index == count - 1);
    QIQI_TEST_CHECK(!ReadCandidateString(first.Get()).empty());
    QIQI_TEST_CHECK(list->GetCandidate(count, last.ReleaseAndGetAddressOf()) ==
                        E_INVALIDARG &&
                    !last);
    QIQI_TEST_CHECK(list->SetResult(0, CAND_SELECTED) == S_OK);
    QIQI_TEST_CHECK(list->SetResult(0, CAND_FINALIZED) == S_OK);
    QIQI_TEST_CHECK(list->SetResult(ULONG_MAX, CAND_CANCELED) == S_OK);
    QIQI_TEST_CHECK(list->SetResult(
                        count, CAND_SELECTED) == E_INVALIDARG);

    ComPtr<IEnumTfCandidates> enumerator;
    QIQI_TEST_CHECK(list->EnumCandidates(&enumerator) == S_OK &&
                    enumerator);
    std::array<ITfCandidateString*, 2> batch{};
    ULONG fetched = ULONG_MAX;
    QIQI_TEST_CHECK(enumerator->Next(
                        gsl::narrow_cast<ULONG>(batch.size()),
                        batch.data(), &fetched) ==
                            (count >= batch.size() ? S_OK : S_FALSE) &&
                    fetched == std::min<ULONG>(
                        count, gsl::narrow_cast<ULONG>(batch.size())));
    for (ULONG i = 0; i < fetched; ++i) {
        ULONG index = ULONG_MAX;
        QIQI_TEST_CHECK(gsl::at(batch, i) &&
                        gsl::at(batch, i)->GetIndex(&index) == S_OK &&
                        index == i);
        gsl::at(batch, i)->Release();
        gsl::at(batch, i) = nullptr;
    }
    ComPtr<IEnumTfCandidates> clone;
    QIQI_TEST_CHECK(enumerator->Clone(&clone) == S_OK && clone);
    QIQI_TEST_CHECK(clone->Skip(count + 1) == S_FALSE);
    QIQI_TEST_CHECK(clone->Reset() == S_OK);
    QIQI_TEST_CHECK(enumerator->Reset() == S_OK);

    const ScopedBstr selected(::SysAllocString(L"你"));
    QIQI_TEST_CHECK(provider->SetResult(
                        nullptr, app_id.Get(), selected.Get()) ==
                    E_INVALIDARG);
    QIQI_TEST_CHECK(provider->SetResult(
                        query.Get(), app_id.Get(), selected.Get()) ==
                    E_NOTIMPL);
}

void TestIntegratedCandidateUiContract() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    ui->SetCandidates(
        L"ab", {L"one", L"two", L"three"}, 2, 0, 0);
    ComPtr<ITfCandidateListUIElementBehavior> behavior;
    ComPtr<ITfIntegratableCandidateListUIElement> integration;
    QIQI_TEST_CHECK(ui.As(&behavior) == S_OK && behavior);
    QIQI_TEST_CHECK(ui.As(&integration) == S_OK && integration);
    QIQI_TEST_CHECK(integration->SetIntegrationStyle(GUID_NULL) == E_NOTIMPL);
    BOOL eaten = TRUE;
    QIQI_TEST_CHECK(integration->OnKeyDown(
                        VK_DOWN, 0, &eaten) == E_NOTIMPL &&
                    !eaten);
    QIQI_TEST_CHECK(integration->SetIntegrationStyle(
                        kGuidIntegrationStyleSearchbox) == S_OK);
    BOOL shown = TRUE;
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_FALSE &&
                    ui->IsShown(&shown) == S_OK && !shown);
    TfIntegratableCandidateListSelectionStyle selection_style =
        STYLE_IMPLIED_SELECTION;
    QIQI_TEST_CHECK(integration->GetSelectionStyle(
                        &selection_style) == S_OK &&
                    selection_style == STYLE_ACTIVE_SELECTION);
    BOOL numbers = FALSE;
    QIQI_TEST_CHECK(integration->ShowCandidateNumbers(&numbers) == S_OK &&
                    numbers);
    QIQI_TEST_CHECK(behavior->SetSelection(3) == E_INVALIDARG);
    QIQI_TEST_CHECK(behavior->SetSelection(1) == E_NOTIMPL);
    QIQI_TEST_CHECK(behavior->Finalize() == E_NOTIMPL);
    QIQI_TEST_CHECK(behavior->Abort() == E_NOTIMPL);
    QIQI_TEST_CHECK(integration->FinalizeExactCompositionString() ==
                    E_NOTIMPL);

    CandidateUiAction observed_action = CandidateUiAction::Abort;
    size_t observed_index = SIZE_MAX;
    WPARAM observed_key = 0;
    ui->SetActionCallback([&](
        CandidateUiAction action, size_t index) -> HRESULT {
        observed_action = action;
        observed_index = index;
        return S_OK;
    });
    ui->SetIntegratedKeyDownCallback(
        [&](WPARAM wparam, LPARAM, BOOL* key_eaten) -> HRESULT {
            observed_key = wparam;
            *key_eaten = TRUE;
            return S_OK;
        });
    QIQI_TEST_CHECK(behavior->SetSelection(1) == S_OK &&
                    observed_action == CandidateUiAction::SetSelection &&
                    observed_index == 1);
    QIQI_TEST_CHECK(behavior->Finalize() == S_OK &&
                    observed_action == CandidateUiAction::Finalize);
    QIQI_TEST_CHECK(behavior->Abort() == S_OK &&
                    observed_action == CandidateUiAction::Abort);
    QIQI_TEST_CHECK(integration->FinalizeExactCompositionString() == S_OK &&
                    observed_action == CandidateUiAction::FinalizeExact);
    QIQI_TEST_CHECK(integration->OnKeyDown(
                        VK_UP, 17, &eaten) == S_OK &&
                    eaten && observed_key == VK_UP);
    ui->SetActionCallback({});
    ui->SetIntegratedKeyDownCallback({});
    QIQI_TEST_CHECK(behavior->Finalize() == E_NOTIMPL);
    ui->ResetIntegrationState();
    QIQI_TEST_CHECK(integration->OnKeyDown(
                        VK_DOWN, 0, &eaten) == E_NOTIMPL &&
                    !eaten);
}

void TestIntegratedServiceRouting() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    enum class Action { Key, SelectFinalize, Exact, Abort };
    const auto dictionary_path =
        std::filesystem::temp_directory_path() /
        std::format(L"qiqi_search_test_{}.txt", ::GetCurrentProcessId());
    auto qdict_path = dictionary_path;
    qdict_path.replace_extension(L".qdict");
    const auto cleanup_dictionary = gsl::finally([&]() noexcept {
        WubiMappedDict::ClearProcessCache();
        std::error_code ec;
        std::filesystem::remove(dictionary_path, ec);
        std::filesystem::remove(qdict_path, ec);
        std::filesystem::remove_all(
            GetSyncChannelDirectoryForFile(dictionary_path), ec);
    });
    {
        std::ofstream output(dictionary_path, std::ios::binary | std::ios::trunc);
        output << "wq 搜索候选一 搜索候选二 搜索候选三\n";
        output.close();
        QIQI_TEST_CHECK(output.good());
    }
    WubiMappedDict::ClearProcessCache();

    const QiQiConfig saved = LoadConfig(g_dll_instance);
    QiQiConfig config = saved;
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.auto_commit_4code = false;
    config.clear_on_empty_4code = false;
    config.enable_code_completion = true;
    config.page_size = 2;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    config.user_dict_path = dictionary_path.wstring();
    config.show_status_bar = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    const auto restore = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(saved));
    });
    std::array<BYTE, 256> keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};

    for (const auto action : {
             Action::Key, Action::SelectFinalize,
             Action::Exact, Action::Abort}) {
        QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
        const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        manager->show_own_ui_ = FALSE;
        QIQI_TEST_CHECK(service->ActivateEx(
            manager.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        });
        const auto context = MakeComObject<MockTfContext>();
        const auto send = [&](UINT key) {
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(service->OnKeyDown(
                                context.Get(), key, 0, &eaten) == S_OK &&
                            eaten);
        };
        if (action == Action::SelectFinalize) {
            send('W');
            send('Q');
        } else {
            send('A');
        }
        auto* const raw_ui =
            dynamic_cast<CandidateUI*>(manager->last_ui_element_.Get());
        QIQI_TEST_CHECK(raw_ui);
        const auto ui = ComPtr<CandidateUI>(raw_ui);
        ComPtr<ITfCandidateListUIElementBehavior> behavior;
        ComPtr<ITfIntegratableCandidateListUIElement> integration;
        QIQI_TEST_CHECK(ui.As(&behavior) == S_OK &&
                        ui.As(&integration) == S_OK);
        QIQI_TEST_CHECK(integration->SetIntegrationStyle(
                            kGuidIntegrationStyleSearchbox) == S_OK);

        switch (action) {
        case Action::Key: {
            BOOL eaten = FALSE;
            QIQI_TEST_CHECK(integration->OnKeyDown(
                                'B', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(context->range_->text_ == L"ab");
            break;
        }
        case Action::SelectFinalize: {
            UINT count = 0;
            QIQI_TEST_CHECK(behavior->GetCount(&count) == S_OK &&
                            count > 2);
            constexpr UINT selected = 2;
            ComPtr<ITfFnSearchCandidateProvider> provider;
            ComPtr<IUnknown> provider_unknown;
            QIQI_TEST_CHECK(service->GetFunction(
                                GUID_NULL, IID_ITfFnSearchCandidateProvider,
                                provider_unknown.GetAddressOf()) == S_OK &&
                            provider_unknown.As(&provider) == S_OK);
            const ScopedBstr query(::SysAllocString(L"wq"));
            ComPtr<ITfCandidateList> provider_list;
            QIQI_TEST_CHECK(provider->GetSearchCandidates(
                                query.Get(), nullptr,
                                provider_list.GetAddressOf()) == S_OK &&
                            provider_list);
            ULONG provider_count = 0;
            QIQI_TEST_CHECK(provider_list->GetCandidateNum(
                                &provider_count) == S_OK &&
                            provider_count > selected);
            ComPtr<ITfCandidateString> provider_candidate;
            QIQI_TEST_CHECK(provider_list->GetCandidate(
                                selected, &provider_candidate) == S_OK &&
                            provider_candidate);
            const auto expected =
                ReadCandidateString(provider_candidate.Get());
            ScopedBstr integrated_candidate;
            QIQI_TEST_CHECK(behavior->GetString(
                                selected, integrated_candidate.Put()) ==
                                S_OK &&
                            std::wstring(integrated_candidate.Get()) ==
                                expected);
            QIQI_TEST_CHECK(context->range_->text_ == L"wq" &&
                            !context->last_composition_->ended_);
            QIQI_TEST_CHECK(behavior->SetSelection(selected) == S_OK);
            UINT current = UINT_MAX;
            QIQI_TEST_CHECK(behavior->GetSelection(&current) == S_OK &&
                            current == selected);
            QIQI_TEST_CHECK(context->range_->text_ == L"wq" &&
                            !context->last_composition_->ended_);
            QIQI_TEST_CHECK(behavior->Finalize() == S_OK);
            QIQI_TEST_CHECK(context->last_composition_->ended_ &&
                            context->range_->text_ == expected);
            break;
        }
        case Action::Exact:
            QIQI_TEST_CHECK(
                integration->FinalizeExactCompositionString() == S_OK);
            QIQI_TEST_CHECK(context->last_composition_->ended_ &&
                            context->range_->text_ == L"a");
            break;
        case Action::Abort:
            QIQI_TEST_CHECK(behavior->Abort() == S_OK);
            QIQI_TEST_CHECK(context->last_composition_->ended_ &&
                            context->range_->text_.empty());
            break;
        }
    }
}

} // namespace

void RunTsfSearchIntegrationTests() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto objects_before = g_com_object_count.load();
    TestSearchCandidateList();
    TestIntegratedCandidateUiContract();
    TestIntegratedServiceRouting();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
