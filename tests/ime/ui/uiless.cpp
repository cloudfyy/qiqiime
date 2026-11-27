// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/controls.h"
#include "ime/comp/presentation.h"
#include "ime/tsf/mocks.h"
#include "ime/ui/candobj.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ime/ui/candidate.h"

namespace qiqi::test::candidate_cases {

using Microsoft::WRL::ComPtr;

void TestCandidateUilessContract() {
    const TsfTestCaseTrace trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const auto replacement_document = MakeComObject<MockTfDocumentMgr>();
    const std::vector<std::wstring> candidates{L"一", L"二", L"三"};

    ui->SetCandidates(L"ab", candidates, 2, 0, 0, document.Get());
    DWORD flags = 0;
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == (TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT |
                              TF_CLUIE_SELECTION | TF_CLUIE_STRING |
                              TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE));
    ComPtr<ITfDocumentMgr> returned;
    QIQI_TEST_CHECK(ui->GetDocumentMgr(returned.GetAddressOf()) == S_OK &&
                    returned.Get() == document.Get());
    const auto acknowledge = [&]() noexcept {
        ui->AcknowledgeUiElementUpdate(ui->UiElementUpdateRevision());
    };

    ui->SetUiElementId(41);
    acknowledge();
    ui->SetCandidates(L"ab", candidates, 2, 0, 0, document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK && flags == 0);
    ui->SetCandidates(L"ab", candidates, 2, 0, 1, document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == TF_CLUIE_SELECTION);
    acknowledge();
    ui->SetCandidates(L"ab", candidates, 2, 1, 0, document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == (TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE));
    acknowledge();
    ui->SetCandidates(
        L"ab", {L"一", L"二", L"三", L"四"}, 2, 1, 0, document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == (TF_CLUIE_COUNT | TF_CLUIE_STRING));
    acknowledge();
    ui->SetCandidates(
        L"ab", {L"一", L"二", L"三", L"四"}, 2, 1, 0, replacement_document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == TF_CLUIE_DOCUMENTMGR);
    QIQI_TEST_CHECK(ui->GetDocumentMgr(returned.ReleaseAndGetAddressOf()) == S_OK &&
                    returned.Get() == replacement_document.Get());
    acknowledge();

    ui->ClearUiElementDocument();
    QIQI_TEST_CHECK(ui->GetDocumentMgr(returned.ReleaseAndGetAddressOf()) == E_FAIL &&
                    !returned);
    ui->SetCandidates(L"z", {}, 5, 0, 0, replacement_document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    (flags & TF_CLUIE_SELECTION) != 0);
    UINT selection = 99;
    QIQI_TEST_CHECK(ui->GetSelection(&selection) == S_FALSE && selection == 99);
    acknowledge();
    ui->SetCandidates(L"z", {L"字"}, 5, 0, 0, replacement_document.Get());
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    (flags & TF_CLUIE_SELECTION) != 0);
    const std::vector<std::wstring> raw_candidates{L"原始候选"};
    ui->SetCandidates(
        L"wq", {L"显示候选(~a)"}, 5, 0, 0,
        replacement_document.Get(), &raw_candidates);
    ScopedBstr integrated_string;
    QIQI_TEST_CHECK(ui->GetString(
                        0, integrated_string.Put()) == S_OK &&
                    std::wstring(integrated_string.Get()) ==
                    L"显示候选(~a)");
    acknowledge();
    const std::vector<std::wstring> replacement_raw_candidates{L"更新原始候选"};
    ui->SetCandidates(
        L"wq", {L"显示候选(~a)"}, 5, 0, 0,
        replacement_document.Get(), &replacement_raw_candidates);
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK &&
                    flags == TF_CLUIE_STRING);
    ComPtr<ITfIntegratableCandidateListUIElement> integration;
    QIQI_TEST_CHECK(ui.As(&integration) == S_OK &&
                    integration->SetIntegrationStyle(
                        kGuidIntegrationStyleSearchbox) == S_OK);
    QIQI_TEST_CHECK(ui->GetString(
                        0, integrated_string.Put()) == S_OK &&
                    std::wstring(integrated_string.Get()) ==
                    L"更新原始候选");
    ui->ResetIntegrationState();
    QIQI_TEST_CHECK(ui->GetString(
                        0, integrated_string.Put()) == S_OK &&
                    std::wstring(integrated_string.Get()) ==
                    L"显示候选(~a)");
}

} // namespace qiqi::test::candidate_cases
