// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/etw.h"
#include "common/support.h"
#include "common/version.h"
#include "ime/tsf/mocks.h"
#include "ime/svc/service.h"
#include <ctffunc.h>
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::test {
namespace {

std::error_code RemoveDirectoryNoThrow(
    const std::filesystem::path& directory) noexcept {
    std::error_code error;
    try {
        std::ignore = std::filesystem::remove_all(directory, error);
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    return error;
}

class SearchIntegrationTestEnvironment final {
public:
    explicit SearchIntegrationTestEnvironment(std::wstring_view name) {
        const auto suffix =
            std::wstring(name) + L"_" +
            std::to_wstring(::GetCurrentProcessId()) + L"_" +
            std::to_wstring(::GetTickCount64());
        registry_subkey_ =
            L"Software\\QiQiIME_SearchIntegrationTest_" + suffix;
        previous_registry_subkey_ = GetActiveConfigRegistrySubKey();
        appdata_directory_ =
            std::filesystem::temp_directory_path() /
            (L"QiQiIME_SearchIntegrationTest_" + suffix);
        std::error_code directory_error;
        QIQI_TEST_CHECK(std::filesystem::create_directory(
            appdata_directory_, directory_error) && !directory_error);

        wchar_t* previous_appdata = nullptr;
        size_t previous_appdata_length = 0;
        QIQI_TEST_CHECK(_wdupenv_s(
            &previous_appdata, &previous_appdata_length,
            L"LOCALAPPDATA") == 0);
        previous_appdata_.reset(previous_appdata);
        QIQI_TEST_CHECK(_wputenv_s(
            L"LOCALAPPDATA", appdata_directory_.c_str()) == 0);
        SetConfigRegistrySubKeyOverrideForTest(registry_subkey_.c_str());
    }

    ~SearchIntegrationTestEnvironment() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_registry_subkey_);
        const auto registry_status =
            ::RegDeleteTreeW(HKEY_CURRENT_USER, registry_subkey_.c_str());
        QIQI_TEST_CHECK_CLEANUP(
            registry_status == ERROR_SUCCESS ||
            registry_status == ERROR_FILE_NOT_FOUND);
        QIQI_TEST_CHECK_CLEANUP(_wputenv_s(
            L"LOCALAPPDATA",
            previous_appdata_ ? previous_appdata_.get() : L"") == 0);
        const std::error_code directory_error =
            RemoveDirectoryNoThrow(appdata_directory_);
        QIQI_TEST_CHECK_CLEANUP(!directory_error);
    }

    SearchIntegrationTestEnvironment(
        const SearchIntegrationTestEnvironment&) = delete;
    SearchIntegrationTestEnvironment& operator=(
        const SearchIntegrationTestEnvironment&) = delete;
    SearchIntegrationTestEnvironment(
        SearchIntegrationTestEnvironment&&) = delete;
    SearchIntegrationTestEnvironment& operator=(
        SearchIntegrationTestEnvironment&&) = delete;

private:
    std::wstring registry_subkey_;
    const wchar_t* previous_registry_subkey_ = nullptr;
    std::filesystem::path appdata_directory_;
    std::unique_ptr<wchar_t, decltype(&std::free)>
        previous_appdata_{nullptr, &std::free};
};

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

void TestSearchIntegrationDiagnostics() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto captured = CaptureLogEvents([](TRACEHANDLE) {
        const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(service->ActivateEx(
            manager.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK_CLEANUP(service->Deactivate() == S_OK);
        });
        ComPtr<ITfFunctionProvider> registered_provider;
        ComPtr<ITfFunctionProvider> expected_provider;
        QIQI_TEST_CHECK(manager->single_sink_.As(&registered_provider) == S_OK &&
            service.As(&expected_provider) == S_OK &&
            registered_provider.Get() == expected_provider.Get());
        ComPtr<IUnknown> function;
        QIQI_TEST_CHECK(registered_provider->GetFunction(
            GUID_NULL, IID_ITfFnSearchCandidateProvider,
            function.GetAddressOf()) == S_OK && function);
        const ScopedBstr query(::SysAllocString(L"search-privacy-canary-query"));
        const ScopedBstr application(::SysAllocString(L"search-privacy-canary-app"));
        const ScopedBstr result(::SysAllocString(L"search-privacy-canary-result"));
        QIQI_TEST_CHECK(query && application && result);
        ComPtr<ITfCandidateList> candidates;
        QIQI_TEST_CHECK(service->GetSearchCandidates(
            query.Get(), application.Get(), &candidates) == S_FALSE &&
            !candidates);
        QIQI_TEST_CHECK(service->SetResult(
            query.Get(), application.Get(), result.Get()) == E_NOTIMPL);
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        QIQI_TEST_CHECK(ui->SetIntegrationStyle(
            kGuidIntegrationStyleSearchbox) == S_OK);
    });
    constexpr std::string_view kActivationPrefix = "QiQiTextService::ActivateEx(version=";
    const auto expected_activation_prefix =
        std::format("{}{}, arch={}, ", kActivationPrefix, version::kVersion, version::kArch);
    bool saw_activation = false;
    bool saw_registration = false;
    bool saw_discovery = false;
    bool saw_query = false;
    bool saw_feedback = false;
    bool saw_style = false;
    for (const auto& event : captured) {
        QIQI_TEST_CHECK(!event.message.contains("search-privacy-canary-"));
        if (event.message.starts_with(kActivationPrefix)) {
            QIQI_TEST_CHECK(event.message.starts_with(expected_activation_prefix));
            saw_activation = true;
        }
        if (!event.message.starts_with("SearchIntegration(")) continue;
        saw_registration |= event.message.contains("stage=provider-register,") &&
            event.message.contains("registered=true, hr=0x00000000");
        saw_discovery |= event.message.contains("stage=get-function,") &&
            event.message.contains("search_provider=true") &&
            event.message.contains("hr=0x00000000");
        saw_query |= event.message.contains("stage=get-search-candidates,") &&
            event.message.contains("query_present=true") &&
            event.message.contains("application_id_present=true") &&
            event.message.contains("candidate_count=0, hr=0x00000001");
        saw_feedback |= event.message.contains("stage=set-result,") &&
            event.message.contains("feedback_applied=false, hr=0x80004001");
        saw_style |= event.message.contains("stage=set-integration-style,") &&
            event.message.contains("searchbox=true, shown=false,") &&
            event.message.contains(
                "total_count=0, exposed_count=0, page_size=5, page=0,") &&
            event.message.contains("hr=0x00000000");
    }
    QIQI_TEST_CHECK(saw_activation);
    if (!(saw_registration && saw_discovery && saw_query &&
          saw_feedback && saw_style)) {
        std::fprintf(stderr,
            "[FAIL] Search diagnostics: registration=%d discovery=%d query=%d feedback=%d style=%d\n",
            saw_registration, saw_discovery, saw_query, saw_feedback, saw_style);
        for (const auto& event : captured) {
            if (event.message.starts_with("SearchIntegration(")) {
                std::fprintf(stderr, "%s\n", event.message.c_str());
            }
        }
    }
    QIQI_TEST_CHECK(saw_registration && saw_discovery && saw_query &&
                    saw_feedback && saw_style);
}

void TestIntegratedCandidateUiContract() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    ui->SetCandidates(
        L"ab", {L"one", L"two", L"three", L"four"}, 2, 1, 1);
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
    UINT count = 0;
    UINT selection = UINT_MAX;
    QIQI_TEST_CHECK(behavior->GetCount(&count) == S_OK && count == 4);
    QIQI_TEST_CHECK(behavior->GetSelection(&selection) == S_OK &&
                    selection == 3);
    ScopedBstr first_exposed;
    QIQI_TEST_CHECK(behavior->GetString(
                        0, first_exposed.Put()) == S_OK &&
                    std::wstring(first_exposed.Get()) == L"one");
    QIQI_TEST_CHECK(behavior->GetString(
                        3, first_exposed.Put()) == S_OK &&
                    std::wstring(first_exposed.Get()) == L"four");
    QIQI_TEST_CHECK(behavior->GetString(4, first_exposed.Put()) ==
                    E_INVALIDARG);
    QIQI_TEST_CHECK(behavior->GetString(
                        UINT_MAX, first_exposed.Put()) == E_INVALIDARG);
    std::array<UINT, 2> page_indices{
        UINT_MAX, UINT_MAX};
    UINT page_count = UINT_MAX;
    QIQI_TEST_CHECK(behavior->GetPageIndex(
                        page_indices.data(),
                        gsl::narrow_cast<UINT>(page_indices.size()),
                        &page_count) == S_OK &&
                        page_count == 2 &&
                        gsl::at(page_indices, 0) == 0 &&
                        gsl::at(page_indices, 1) == 2);
    UINT current_page = UINT_MAX;
    QIQI_TEST_CHECK(behavior->GetCurrentPage(&current_page) == S_OK &&
                    current_page == 1);
    DWORD flags = 0;
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    (flags & TF_CLUIE_PAGEINDEX) != 0 &&
                    (flags & TF_CLUIE_CURRENTPAGE) != 0);
    QIQI_TEST_CHECK(behavior->SetSelection(4) == E_INVALIDARG);
    QIQI_TEST_CHECK(behavior->SetSelection(UINT_MAX) == E_INVALIDARG);
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
            if (!key_eaten) return E_POINTER;
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
    QIQI_TEST_CHECK(behavior->GetCount(&count) == S_OK && count == 4);
    QIQI_TEST_CHECK(behavior->GetSelection(&selection) == S_OK &&
                    selection == 3);
    page_indices.fill(UINT_MAX);
    QIQI_TEST_CHECK(behavior->GetPageIndex(
                        page_indices.data(),
                        gsl::narrow_cast<UINT>(page_indices.size()),
                        &page_count) == S_OK &&
                        page_count == 2 &&
                        gsl::at(page_indices, 0) == 0 &&
                        gsl::at(page_indices, 1) == 2);
    QIQI_TEST_CHECK(behavior->GetCurrentPage(&current_page) == S_OK &&
                    current_page == 1);
    QIQI_TEST_CHECK(integration->OnKeyDown(
                        VK_DOWN, 0, &eaten) == E_NOTIMPL &&
                    !eaten);
}

void TestIntegratedServiceRouting() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    enum class Action { Key, SelectFinalize, Exact, Abort, PhraseChar };
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
             Action::Exact, Action::Abort, Action::PhraseChar}) {
        config.space_enter_mode = action == Action::Key
            ? WubiSpaceEnterMode::SpaceSelectEnterCode : WubiSpaceEnterMode::EnterSelectSpaceClear;
        config.default_chinese_punct = action != Action::PhraseChar && saved.default_chinese_punct;
        config.phrase_char_selection = false;
        QIQI_TEST_CHECK(SaveConfigToRegistry(config));
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
        } else if (action == Action::PhraseChar) {
            for (const UINT key : {'W', 'Q', 'V', 'B'}) send(key);
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
            config.space_enter_mode = WubiSpaceEnterMode::EnterSelectSpaceClear;
            QIQI_TEST_CHECK(SaveConfigToRegistry(config));
            QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
            QIQI_TEST_CHECK(context->range_->text_ == L"ab" && !context->last_composition_->ended_);
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(integration->OnKeyDown(VK_SPACE, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(context->last_composition_->ended_ && context->range_->text_.empty());
            break;
        }
        case Action::SelectFinalize: {
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
            UINT count = 0;
            QIQI_TEST_CHECK(
                behavior->GetCount(&count) == S_OK &&
                count == provider_count);
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
        case Action::PhraseChar: {
            BOOL eaten = TRUE;
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_OEM_6, 0, &eaten) == S_OK && !eaten);
            config.phrase_char_selection = true;
            QIQI_TEST_CHECK(SaveConfigToRegistry(config));
            QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
            QIQI_TEST_CHECK(context->range_->text_ == L"wqvb" && !context->last_composition_->ended_);
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_OEM_6, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(integration->OnKeyDown(VK_OEM_6, 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(context->last_composition_->ended_ && context->range_->text_ == L"好");
            QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_OEM_6, 0, &eaten) == S_OK && !eaten);
            break;
        }
        }
    }
}

void TestCandidateFilterConfigReload(bool practice) {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) != kDefaultConfigRegSubKey);
    const QiQiConfig saved = LoadConfig(g_dll_instance);
    const auto restore = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(saved));
    });
    QiQiConfig config = saved;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.user_dict_path.clear();
    config.default_chinese_mode = true;
    config.show_status_bar = false;
    config.enable_code_completion = true;
    config.show_code_hint = false;
    config.auto_commit_4code = true;
    config.space_enter_mode = WubiSpaceEnterMode::SpaceSelectEnterCode;
    config.single_character_mode = false;
    config.full_code_practice = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    std::array<BYTE, 256> keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));

    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    manager->show_own_ui_ = FALSE;
    QIQI_TEST_CHECK(service->ActivateEx(manager.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    });
    const auto context = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;
    for (const UINT key : {'W', 'Q'}) {
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    ComPtr<ITfCandidateListUIElement> candidates;
    QIQI_TEST_CHECK(manager->last_ui_element_.As(&candidates) == S_OK && candidates);
    UINT original_count = 0;
    QIQI_TEST_CHECK(candidates->GetCount(&original_count) == S_OK && original_count > 1);
    const auto composition = context->last_composition_;
    QIQI_TEST_CHECK(composition && !composition->ended_);

    config.single_character_mode = !practice;
    config.full_code_practice = practice;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    QIQI_TEST_CHECK(context->last_composition_.Get() == composition.Get() &&
                    context->range_->text_ == L"wq" && !composition->ended_);
    const ScopedBstr query(::SysAllocString(L"wq"));
    QIQI_TEST_CHECK(query);
    ComPtr<ITfCandidateList> search_results;
    if (practice) {
        QIQI_TEST_CHECK(service->GetSearchCandidates(query.Get(), nullptr, search_results.GetAddressOf()) == S_FALSE &&
                        !search_results);
        for (const UINT key : {'I', 'Y'}) {
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        }
        QIQI_TEST_CHECK(composition->ended_ && context->range_->text_ == L"你");
        return;
    }

    UINT filtered_count = 0;
    QIQI_TEST_CHECK(candidates->GetCount(&filtered_count) == S_OK &&
                    filtered_count > 0 && filtered_count < original_count);
    ScopedBstr first;
    QIQI_TEST_CHECK(candidates->GetString(0, first.Put()) == S_OK && first);
    QIQI_TEST_CHECK(service->GetSearchCandidates(query.Get(), nullptr, search_results.GetAddressOf()) == S_OK &&
                    search_results);
    ULONG search_count = 0;
    QIQI_TEST_CHECK(search_results->GetCandidateNum(&search_count) == S_OK && filtered_count == search_count);
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(composition->ended_ && context->range_->text_ == first.Get());
}

void TestZPinyinFromSettings() {
    const TsfTestCaseTrace trace(__func__);
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) != kDefaultConfigRegSubKey);
    const QiQiConfig saved = LoadConfig(g_dll_instance);
    const auto restore = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(SaveConfigToRegistry(saved));
    });
    QiQiConfig config;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    std::array<BYTE, 256> keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(::SetKeyboardState(keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    manager->show_own_ui_ = FALSE;
    QIQI_TEST_CHECK(service->ActivateEx(manager.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(service->Deactivate() == S_OK);
    });
    const auto context = MakeComObject<MockTfContext>();

    config.enable_z_pinyin_lookup = true;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    BOOL eaten = FALSE;
    for (const UINT key : {'Z', 'N', 'I'}) {
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(context->range_->text_ == L"zni" && !context->last_composition_->ended_);
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context->range_->text_ == L"你" && context->last_composition_->ended_);
}

} // namespace

void RunTsfSearchIntegrationTests() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const SearchIntegrationTestEnvironment environment(L"SearchIntegration");
    const auto objects_before = g_com_object_count.load();
    TestSearchCandidateList();
    TestIntegratedCandidateUiContract();
    TestIntegratedServiceRouting();
    TestCandidateFilterConfigReload(false);
    TestCandidateFilterConfigReload(true);
    TestZPinyinFromSettings();
    TestSearchIntegrationDiagnostics();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
