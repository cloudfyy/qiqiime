// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/presentation.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qiqi::test {
namespace {

KeyProcessResult PresentationSnapshot() {
    KeyProcessResult snapshot;
    snapshot.composition_text = L"abcd";
    snapshot.all_display_candidates = {L"one", L"two", L"three", L"four"};
    snapshot.page_size = 2;
    snapshot.current_page = 1;
    snapshot.selected_index = 0;
    return snapshot;
}

void CheckCandidateSnapshot(CandidateUI& ui) {
    UINT count = 0;
    UINT selection = 0;
    UINT page = 0;
    QIQI_TEST_CHECK(ui.GetCount(&count) == S_OK && count == 4 &&
                    ui.GetSelection(&selection) == S_OK && selection == 2 &&
                    ui.GetCurrentPage(&page) == S_OK && page == 1);
}

void TestPresentationBeginBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Outcome { HRESULT result; bool invalid_id; };
    const std::array outcomes{
        Outcome{S_OK, false}, Outcome{S_FALSE, false}, Outcome{E_ACCESSDENIED, false},
        Outcome{S_OK, true}, Outcome{E_ACCESSDENIED, true},
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto& outcome : outcomes) {
        for (const unsigned int stale_check : {0u, 1u, 2u, 3u, 4u, 5u}) {
            if (stale_check == 5 && (FAILED(outcome.result) || outcome.invalid_id)) continue;
            const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
            ui->SetPresentationEnabled(false);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            const ComPtr<ITfThreadMgr> thread_mgr = manager;
            manager->begin_ui_result_ = outcome.result;
            manager->return_invalid_ui_id_ = outcome.invalid_id;
            manager->end_ui_result_ = E_ACCESSDENIED;
            manager->end_ui_callback_ = [&](DWORD id) {
                QIQI_TEST_CHECK(id == 7 &&
                                ui->UiElementId() ==
                                    (stale_check == 4 ? 55 : TF_INVALID_UIELEMENTID));
            };
            constexpr DWORD flags = TF_TMAE_UIELEMENTENABLEDONLY;
            const EditSessionEpoch epoch{11, 13};
            const auto snapshot = PresentationSnapshot();
            unsigned int checks_seen = 0;
            unsigned int manager_reads = 0;
            PresentCompositionCandidates(*ui.Get(), flags, snapshot, nullptr, epoch, 157, GUID_NULL,
                [&] {
                    QIQI_TEST_CHECK(checks_seen == 1 && ++manager_reads == 1);
                    return thread_mgr.Get();
                },
                [&, checks = std::make_unique<unsigned int>(0)] {
                    checks_seen = ++*checks;
                    if (checks_seen >= 3) CheckCandidateSnapshot(*ui.Get());
                    const ULONG refs =
                        checks_seen == 1 ? 2UL : checks_seen == 5 ? 4UL : 3UL;
                    QIQI_TEST_CHECK(manager->AddRef() == refs + 1 && manager->Release() == refs);
                    if (checks_seen == stale_check) {
                        if (stale_check == 4) ui->SetUiElementId(55);
                        return false;
                    }
                    return true;
                });
            const bool began =
                stale_check != 1 && stale_check != 2 && stale_check != 3;
            const bool published =
                began && SUCCEEDED(outcome.result) && stale_check != 4;
            const DWORD expected_id = stale_check == 4 ? 55 :
                published && !outcome.invalid_id ? 7 : TF_INVALID_UIELEMENTID;
            const bool negotiated = SUCCEEDED(outcome.result) && !outcome.invalid_id;
            const unsigned int expected_checks =
                stale_check ? stale_check : negotiated ? 5u : 4u;
            const bool retires_begin_result =
                began && !outcome.invalid_id &&
                (stale_check == 4 || FAILED(outcome.result));
            QIQI_TEST_CHECK(checks_seen == expected_checks && ui->UiElementId() == expected_id &&
                            manager_reads == (stale_check == 1 ? 0u : 1u));
            QIQI_TEST_CHECK(manager->begin_ui_calls_ == (began ? 1u : 0u) &&
                            manager->update_ui_calls_ == 0 &&
                            manager->end_ui_calls_ ==
                                (retires_begin_result ? 1u : 0u));
            QIQI_TEST_CHECK(
                ui->HasPendingUiElementRetirements() == retires_begin_result);
            if (manager->update_ui_calls_) {
                QIQI_TEST_CHECK(manager->updated_ui_ids_ == std::vector<DWORD>{expected_id});
            }
            BOOL shown = TRUE;
            const ULONG expected_refs =
                ((published && !outcome.invalid_id) ? 3UL : 2UL) +
                (retires_begin_result ? 1UL : 0UL);
            QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && !shown && !ui->WindowHandle() &&
                            manager->AddRef() == expected_refs + 1 &&
                            manager->Release() == expected_refs);
            manager->end_ui_callback_ = {};
            manager->end_ui_result_ = S_OK;
            const HRESULT end_result = EndCompositionCandidatePresentation(
                *ui.Get(), thread_mgr, 157, GUID_NULL);
            QIQI_TEST_CHECK(SUCCEEDED(end_result));
            QIQI_TEST_CHECK(
                ui->UiElementLifecycle().State() == CandidateUiLifecycleState::Idle &&
                ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                !ui->HasPendingUiElementRetirements());
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 27 && g_com_object_count.load() == objects_before);
}

void TestCandidateUiLifecycleFsm() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    CandidateUiElementLifecycle lifecycle;
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    lifecycle.SetDocumentManager(document.Get());
    QIQI_TEST_CHECK(lifecycle.State() == CandidateUiLifecycleState::Idle &&
                    lifecycle.ActiveId() == TF_INVALID_UIELEMENTID);

    const auto begin = lifecycle.TryBeginRegistration();
    QIQI_TEST_CHECK(begin && !lifecycle.TryBeginRegistration() &&
                    lifecycle.State() == CandidateUiLifecycleState::Beginning &&
                    lifecycle.RetryRetirements(
                        "TestCandidateUiLifecycleFsm::PendingBegin") == E_PENDING &&
                    lifecycle.EndRegistrations(
                        "TestCandidateUiLifecycleFsm::PendingBegin") == E_PENDING);
    QIQI_TEST_CHECK(lifecycle.PublishRegistration(
                        *begin, 41, manager.Get()) &&
                    lifecycle.State() == CandidateUiLifecycleState::Active &&
                    lifecycle.ActiveId() == 41 &&
                    lifecycle.ActiveManager() == manager.Get());

    const auto update = lifecycle.TryBeginUpdate();
    QIQI_TEST_CHECK(update &&
                    lifecycle.State() == CandidateUiLifecycleState::Updating &&
                    lifecycle.ActiveId() == 41 &&
                    lifecycle.RetireActiveRegistration(
                        "TestCandidateUiLifecycleFsm::PendingUpdate") == E_PENDING);
    QIQI_TEST_CHECK(lifecycle.CompleteUpdate(*update));
    QIQI_TEST_CHECK(lifecycle.State() == CandidateUiLifecycleState::Active);

    manager->end_ui_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(lifecycle.RetireActiveRegistration(
                        "TestCandidateUiLifecycleFsm::EndUIElement") ==
                    E_ACCESSDENIED);
    QIQI_TEST_CHECK(lifecycle.State() == CandidateUiLifecycleState::Retiring &&
                    lifecycle.ActiveId() == TF_INVALID_UIELEMENTID &&
                    lifecycle.HasPendingRetirements() &&
                    lifecycle.TryBeginRegistration() == std::nullopt);
    QIQI_TEST_CHECK(lifecycle.RetireRegistration(
                        42, manager.Get(),
                        "TestCandidateUiLifecycleFsm::ConflictingRetirement") ==
                    E_UNEXPECTED);
    lifecycle.ClearDocumentManagerIfIdle();
    QIQI_TEST_CHECK(lifecycle.DocumentManager() == document.Get());

    QIQI_TEST_CHECK(lifecycle.RetryRetirements(
                        "TestCandidateUiLifecycleFsm::RetryEnd") ==
                    E_ACCESSDENIED);
    manager->end_ui_result_ = S_OK;
    QIQI_TEST_CHECK(lifecycle.RetryRetirements(
                        "TestCandidateUiLifecycleFsm::RetryEnd") == S_OK &&
                    lifecycle.State() == CandidateUiLifecycleState::Idle &&
                    !lifecycle.HasPendingRetirements());
    lifecycle.ClearDocumentManagerIfIdle();
    QIQI_TEST_CHECK(!lifecycle.DocumentManager() &&
                    manager->end_ui_calls_ == 3);
}

void TestPresentationFallbackAndUpdates() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    size_t fallback_cases = 0;
    for (const bool null_manager : {false, true}) {
        for (const unsigned int stale_check : {0u, 1u, 2u}) {
            const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            const ComPtr<ITfThreadMgr> thread_mgr = manager;
            manager->return_null_ui_manager_ = null_manager;
            manager->fail_query_ui_mgr_ = !null_manager;
            unsigned int checks = 0;
            PresentCompositionCandidates(
                *ui.Get(), DWORD{0}, PresentationSnapshot(), nullptr, {}, 163, GUID_NULL,
                [&thread_mgr]() noexcept { return thread_mgr.Get(); },
                [&] { return ++checks != stale_check; });
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && !shown && !ui->WindowHandle());
            QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                            manager->begin_ui_calls_ == 0 && manager->update_ui_calls_ == 0 &&
                            checks == (stale_check == 1 ? 1u : 2u));
            ++fallback_cases;
        }
    }
    size_t update_cases = 0;
    for (const HRESULT update_hr : {S_OK, E_ACCESSDENIED}) {
        for (const unsigned int stale_check : {0u, 1u, 2u}) {
            const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
            ui->SetUiElementId(61);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            const ComPtr<ITfThreadMgr> thread_mgr = manager;
            manager->update_ui_result_ = update_hr;
            unsigned int checks = 0;
            PresentCompositionCandidates(
                *ui.Get(), DWORD{0}, PresentationSnapshot(), nullptr, {}, 167, GUID_NULL,
                [&thread_mgr]() noexcept { return thread_mgr.Get(); },
                [&] { return ++checks != stale_check; });
            QIQI_TEST_CHECK(ui->UiElementId() == 61 && manager->begin_ui_calls_ == 0 &&
                            manager->end_ui_calls_ == 0 &&
                            checks == (stale_check == 0 ? 3u : stale_check));
            QIQI_TEST_CHECK(manager->updated_ui_ids_ ==
                            (stale_check == 0 ? std::vector<DWORD>{61} : std::vector<DWORD>{}));
            ++update_cases;
        }
    }
    QIQI_TEST_CHECK(fallback_cases == 6 && update_cases == 6 && g_com_object_count.load() == objects_before);
}

void TestPresentationNegotiationFailures() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto objects_before = g_com_object_count.load();
    enum class Failure { QueryFailure, NullManager, BeginFailure, InvalidElementId };
    for (const auto failure : {
             Failure::QueryFailure, Failure::NullManager,
             Failure::BeginFailure, Failure::InvalidElementId}) {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        const auto document = MakeComObject<MockTfDocumentMgr>();
        const ComPtr<ITfThreadMgr> thread_mgr = manager;
        manager->fail_query_ui_mgr_ = failure == Failure::QueryFailure;
        manager->return_null_ui_manager_ = failure == Failure::NullManager;
        manager->begin_ui_result_ =
            failure == Failure::BeginFailure ? E_ACCESSDENIED : S_OK;
        manager->return_invalid_ui_id_ = failure == Failure::InvalidElementId;

        PresentCompositionCandidates(
            *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY, PresentationSnapshot(),
            document.Get(), {}, 169, GUID_NULL,
            [&thread_mgr]() noexcept { return thread_mgr.Get(); },
            []() noexcept { return true; });

        BOOL shown = TRUE;
        ComPtr<ITfDocumentMgr> returned_document;
        QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                        ui->IsShown(&shown) == S_OK && !shown && !ui->WindowHandle() &&
                        ui->GetDocumentMgr(
                            returned_document.GetAddressOf()) == E_FAIL &&
                        !returned_document);
        const bool queried = failure != Failure::QueryFailure &&
                             failure != Failure::NullManager;
        QIQI_TEST_CHECK(manager->begin_ui_calls_ == (queried ? 1u : 0u) &&
                        manager->update_ui_calls_ == 0);
        QIQI_TEST_CHECK(manager->end_ui_calls_ ==
                        (failure == Failure::BeginFailure ? 1u : 0u));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestPresentationNestedBegin() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto original_document = MakeComObject<MockTfDocumentMgr>();
    const auto nested_document = MakeComObject<MockTfDocumentMgr>();
    const ComPtr<ITfThreadMgr> thread_mgr = manager;
    manager->show_own_ui_ = FALSE;
    auto nested_snapshot = PresentationSnapshot();
    nested_snapshot.composition_text = L"nested";
    nested_snapshot.all_display_candidates = {L"nested"};
    nested_snapshot.page_size = 1;
    nested_snapshot.current_page = 0;
    nested_snapshot.selected_index = 0;
    manager->begin_ui_callback_ = [&](DWORD) {
        PresentCompositionCandidates(
            *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
            nested_snapshot, nested_document.Get(), {}, 171, GUID_NULL,
            [&thread_mgr]() noexcept { return thread_mgr.Get(); },
            []() noexcept { return true; });
    };

    PresentCompositionCandidates(
        *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
        PresentationSnapshot(), original_document.Get(), {}, 171, GUID_NULL,
        [&thread_mgr]() noexcept { return thread_mgr.Get(); },
        []() noexcept { return true; });

    BOOL shown = TRUE;
    ComPtr<ITfDocumentMgr> returned_document;
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 &&
                    manager->update_ui_calls_ == 1 &&
                    ui->UiElementId() == 7 &&
                    ui->IsShown(&shown) == S_OK && !shown &&
                    ui->GetDocumentMgr(returned_document.GetAddressOf()) == S_OK &&
                    returned_document.Get() == original_document.Get());
    PresentCompositionCandidates(
        *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
        nested_snapshot, nested_document.Get(), {}, 171, GUID_NULL,
        [&thread_mgr]() noexcept { return thread_mgr.Get(); },
        []() noexcept { return true; });
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 &&
                    manager->update_ui_calls_ == 2 &&
                    ui->GetDocumentMgr(
                        returned_document.ReleaseAndGetAddressOf()) == S_OK &&
                    returned_document.Get() == nested_document.Get());
    DWORD flags = 0;
    QIQI_TEST_CHECK(ui->GetUpdatedFlags(&flags) == S_OK && flags == 0);
    EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 171, GUID_NULL);
    QIQI_TEST_CHECK(manager->end_ui_calls_ == 1);
}

void TestPresentationReentrantUpdateVisibility() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const ComPtr<ITfThreadMgr> thread_mgr = manager;
    manager->show_own_ui_ = TRUE;
    const auto present = [&](const KeyProcessResult& snapshot) {
        PresentCompositionCandidates(
            *ui.Get(), DWORD{0}, snapshot, document.Get(), {}, 172, GUID_NULL,
            [&thread_mgr]() noexcept { return thread_mgr.Get(); },
            []() noexcept { return true; });
    };
    present(PresentationSnapshot());
    BOOL shown = FALSE;
    QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && shown &&
                    ui->WindowHandle() && ::IsWindowVisible(ui->WindowHandle()));

    bool nested_seen = false;
    manager->update_ui_callback_ = [&](DWORD) {
        nested_seen = true;
        present(PresentationSnapshot());
        BOOL nested_shown = FALSE;
        QIQI_TEST_CHECK(ui->IsShown(&nested_shown) == S_OK && nested_shown);
    };
    auto changed = PresentationSnapshot();
    changed.selected_index = 1;
    present(changed);
    QIQI_TEST_CHECK(nested_seen &&
                    manager->begin_ui_calls_ == 1 &&
                    manager->update_ui_calls_ == 1 &&
                    ui->IsShown(&shown) == S_OK && shown);
    manager->update_ui_callback_ = {};
    EndCompositionCandidatePresentation(
        *ui.Get(), thread_mgr, 172, GUID_NULL);
}

void TestPresentationUpdateRetry() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const ComPtr<ITfThreadMgr> thread_mgr = manager;
    ui->SetUiElementId(61);
    manager->update_ui_result_ = E_UNEXPECTED;
    const auto snapshot = PresentationSnapshot();
    const auto present = [&] {
        PresentCompositionCandidates(
            *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
            snapshot, document.Get(), {}, 181, GUID_NULL,
            [&thread_mgr]() noexcept { return thread_mgr.Get(); },
            []() noexcept { return true; });
    };

    present();
    DWORD flags = 0;
    QIQI_TEST_CHECK(manager->update_ui_calls_ == 1 &&
                    ui->GetUpdatedFlags(&flags) == S_OK && flags != 0);
    present();
    QIQI_TEST_CHECK(manager->update_ui_calls_ == 2 &&
                    ui->GetUpdatedFlags(&flags) == S_OK && flags != 0);
    manager->update_ui_result_ = S_OK;
    present();
    QIQI_TEST_CHECK(manager->update_ui_calls_ == 3 &&
                    ui->GetUpdatedFlags(&flags) == S_OK && flags == 0);
    present();
    QIQI_TEST_CHECK(manager->update_ui_calls_ == 3);
}

void TestPresentationPermissions() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const DWORD flags : {DWORD{0}, DWORD{TF_TMAE_UIELEMENTENABLEDONLY}}) {
        for (const BOOL show_own : {FALSE, TRUE}) {
            for (const bool enabled : {false, true}) {
                const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
                ui->SetPresentationEnabled(enabled);
                const auto manager = MakeComObject<MockTfThreadMgr>();
                const auto document = MakeComObject<MockTfDocumentMgr>();
                const ComPtr<ITfThreadMgr> thread_mgr = manager;
                manager->show_own_ui_ = show_own;
                PresentCompositionCandidates(
                    *ui.Get(), flags, PresentationSnapshot(), document.Get(), {}, 173, GUID_NULL,
                    [&thread_mgr]() noexcept { return thread_mgr.Get(); }, []() noexcept { return true; });
                BOOL shown = FALSE;
                const bool expected = enabled && show_own != FALSE;
                QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && (shown != FALSE) == expected &&
                                (ui->WindowHandle() != nullptr) == expected);
                if (expected) QIQI_TEST_CHECK(::IsWindowVisible(ui->WindowHandle()));
                ComPtr<ITfDocumentMgr> returned_document;
                QIQI_TEST_CHECK(ui->GetDocumentMgr(returned_document.GetAddressOf()) == S_OK &&
                                returned_document.Get() == document.Get());
                QIQI_TEST_CHECK(
                    ui->UiElementId() == 7 &&
                    manager->updated_ui_ids_ ==
                        (show_own == FALSE ? std::vector<DWORD>{7} : std::vector<DWORD>{}));
                manager->end_ui_callback_ = [&](DWORD id) {
                    BOOL ending_shown = TRUE;
                    QIQI_TEST_CHECK(id == 7 && ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                    ui->IsShown(&ending_shown) == S_OK && !ending_shown);
                };
                QIQI_TEST_CHECK(ui->Show(FALSE) == S_OK);
                auto changed_snapshot = PresentationSnapshot();
                changed_snapshot.selected_index = 1;
                PresentCompositionCandidates(
                    *ui.Get(), flags, changed_snapshot, document.Get(), {}, 175, GUID_NULL,
                    [&thread_mgr]() noexcept { return thread_mgr.Get(); },
                    []() noexcept { return true; });
                QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && !shown &&
                                (!ui->WindowHandle() ||
                                 !::IsWindowVisible(ui->WindowHandle())));
                QIQI_TEST_CHECK(manager->updated_ui_ids_.size() ==
                                (show_own == FALSE ? 2u : 1u));
                EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 173, GUID_NULL);
                QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID && manager->end_ui_calls_ == 1);
                QIQI_TEST_CHECK(
                    ui->GetDocumentMgr(returned_document.ReleaseAndGetAddressOf()) == E_FAIL);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 8 && g_com_object_count.load() == objects_before);
}

void TestPresentationLiveManagerLookup() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        ui->SetPresentationEnabled(false);
        const auto original = MakeComObject<MockTfThreadMgr>();
        original->fail_query_ui_mgr_ = true;
        const auto replacement = MakeComObject<MockTfThreadMgr>();
        replacement->next_ui_element_id_ = 67;
        ComPtr<ITfThreadMgr> live_manager = original;
        unsigned int checks = 0;
        unsigned int reads = 0;
        PresentCompositionCandidates(
            *ui.Get(), DWORD{0}, PresentationSnapshot(), nullptr, {}, 177, GUID_NULL,
            [&] {
                QIQI_TEST_CHECK(checks == 1 && ++reads == 1);
                return live_manager.Get();
            },
            [&] {
                if (++checks == 1) live_manager = replacement;
                return true;
            });
        QIQI_TEST_CHECK(reads == 1 && checks == 5 && original->begin_ui_calls_ == 0 &&
                        replacement->updated_ui_ids_.empty() && ui->UiElementId() == 67);
        EndCompositionCandidatePresentation(*ui.Get(), live_manager, 177, GUID_NULL);
        QIQI_TEST_CHECK(original->end_ui_calls_ == 0 && replacement->ended_ui_ids_ == std::vector<DWORD>{67});
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestPresentationManagerChangeSnapshot() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto original = MakeComObject<MockTfThreadMgr>();
    const auto replacement = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    original->show_own_ui_ = FALSE;
    replacement->show_own_ui_ = FALSE;
    ComPtr<ITfThreadMgr> live_manager = original;
    const auto snapshot = PresentationSnapshot();
    const auto present = [&] {
        PresentCompositionCandidates(
            *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
            snapshot, document.Get(), {}, 178, GUID_NULL,
            [&live_manager]() noexcept { return live_manager.Get(); },
            []() noexcept { return true; });
    };
    present();
    QIQI_TEST_CHECK(original->begin_ui_calls_ == 1 &&
                    original->update_ui_calls_ == 1 &&
                    original->end_ui_calls_ == 0);

    live_manager = replacement;
    DWORD replacement_flags = 0;
    replacement->update_ui_callback_ = [&](DWORD id) {
        QIQI_TEST_CHECK(id == replacement->next_ui_element_id_ - 1);
        QIQI_TEST_CHECK(ui->GetUpdatedFlags(&replacement_flags) == S_OK);
    };
    present();
    QIQI_TEST_CHECK(original->end_ui_calls_ == 1 &&
                    replacement->begin_ui_calls_ == 1 &&
                    replacement->update_ui_calls_ == 1 &&
                    replacement_flags == CandidateUI::kAllCandidateUpdateFlags &&
                    ui->UiElementId() != TF_INVALID_UIELEMENTID);
    EndCompositionCandidatePresentation(*ui.Get(), live_manager, 178, GUID_NULL);
    QIQI_TEST_CHECK(replacement->end_ui_calls_ == 1);
}

void TestPresentationManagerFailureRetry() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    for (const bool manager_query_fails : {false, true}) {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        ui->SetPresentationEnabled(false);
        const auto original = MakeComObject<MockTfThreadMgr>();
        const auto replacement = MakeComObject<MockTfThreadMgr>();
        const auto original_document = MakeComObject<MockTfDocumentMgr>();
        const auto replacement_document = MakeComObject<MockTfDocumentMgr>();
        ComPtr<ITfDocumentMgr> current_document = original_document;
        original->show_own_ui_ = FALSE;
        replacement->show_own_ui_ = FALSE;
        ComPtr<ITfThreadMgr> live_manager = original;
        const auto present = [&] {
            PresentCompositionCandidates(
                *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
                PresentationSnapshot(), current_document.Get(), {}, 178, GUID_NULL,
                [&live_manager]() noexcept { return live_manager.Get(); },
                []() noexcept { return true; });
        };
        present();
        QIQI_TEST_CHECK(original->begin_ui_calls_ == 1 &&
                        original->update_ui_calls_ == 1 &&
                        ui->UiElementId() == 7);

        live_manager = replacement;
        current_document = replacement_document;
        replacement->fail_query_ui_mgr_ = manager_query_fails;
        original->end_ui_result_ = E_ACCESSDENIED;
        present();
        QIQI_TEST_CHECK(original->end_ui_calls_ == 1 &&
                        replacement->begin_ui_calls_ == 0 &&
                        ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                        ui->HasPendingUiElementRetirements());
        ComPtr<ITfDocumentMgr> retained_document;
        QIQI_TEST_CHECK(
            ui->GetDocumentMgr(retained_document.GetAddressOf()) == S_OK &&
            retained_document.Get() == original_document.Get());

        present();
        QIQI_TEST_CHECK(original->end_ui_calls_ == 2 &&
                        replacement->begin_ui_calls_ == 0 &&
                        ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                        ui->HasPendingUiElementRetirements());

        original->end_ui_result_ = S_OK;
        replacement->fail_query_ui_mgr_ = false;
        present();
        QIQI_TEST_CHECK(original->end_ui_calls_ == 3 &&
                        replacement->begin_ui_calls_ == 1 &&
                        replacement->update_ui_calls_ == 1 &&
                        ui->UiElementId() == 7 &&
                        !ui->HasPendingUiElementRetirements());
        QIQI_TEST_CHECK(
            ui->GetDocumentMgr(
                retained_document.ReleaseAndGetAddressOf()) == S_OK &&
            retained_document.Get() == replacement_document.Get());
        EndCompositionCandidatePresentation(
            *ui.Get(), live_manager, 178, GUID_NULL);
        QIQI_TEST_CHECK(replacement->end_ui_calls_ == 1);
    }
}

void TestPresentationReentrantEndFailure() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const ComPtr<ITfThreadMgr> thread_mgr = manager;
    manager->show_own_ui_ = FALSE;
    manager->end_ui_result_ = E_ACCESSDENIED;
    manager->begin_ui_callback_ = [&](DWORD id) {
        QIQI_TEST_CHECK(id == 7);
        ui->SetUiElementId(55, manager.Get());
    };
    const auto present = [&] {
        PresentCompositionCandidates(
            *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY,
            PresentationSnapshot(), document.Get(), {}, 179, GUID_NULL,
            [&thread_mgr]() noexcept { return thread_mgr.Get(); },
            []() noexcept { return true; });
    };
    present();
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 &&
                    manager->end_ui_calls_ == 1 &&
                    manager->update_ui_calls_ == 0 &&
                    ui->UiElementId() == 55 &&
                    ui->HasPendingUiElementRetirements());
    present();
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 &&
                    manager->end_ui_calls_ == 2 &&
                    manager->update_ui_calls_ == 0 &&
                    ui->UiElementId() == 55 &&
                    ui->HasPendingUiElementRetirements());
    manager->end_ui_result_ = S_OK;
    present();
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 1 &&
                    manager->end_ui_calls_ == 3 &&
                    manager->update_ui_calls_ == 1 &&
                    ui->UiElementId() == 55 &&
                    !ui->HasPendingUiElementRetirements());
    EndCompositionCandidatePresentation(
        *ui.Get(), thread_mgr, 179, GUID_NULL);
}

void TestPresentationEndOwnership() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Manager { Available, Missing, Failed, NullOutput };
    struct Case { Manager manager; bool valid_id; HRESULT end_hr; };
    const std::array cases{
        Case{Manager::Available, true, S_OK}, Case{Manager::Available, true, E_ACCESSDENIED},
        Case{Manager::Available, false, S_OK}, Case{Manager::Missing, true, S_OK},
        Case{Manager::Failed, true, S_OK}, Case{Manager::NullOutput, true, S_OK},
    };
    const auto objects_before = g_com_object_count.load();
    for (const auto& test : cases) {
        const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
        ui->SetUiElementId(test.valid_id ? 7 : TF_INVALID_UIELEMENTID);
        const auto manager = MakeComObject<MockTfThreadMgr>();
        const auto replacement = MakeComObject<MockTfThreadMgr>();
        const auto original_document = MakeComObject<MockTfDocumentMgr>();
        const auto replacement_document = MakeComObject<MockTfDocumentMgr>();
        ui->SetCandidates(
            L"abcd", {L"one"}, 2, 0, 0, original_document.Get());
        ComPtr<ITfThreadMgr> thread_mgr;
        if (test.manager != Manager::Missing) thread_mgr = manager;
        manager->fail_query_ui_mgr_ = test.manager == Manager::Failed;
        manager->return_null_ui_manager_ = test.manager == Manager::NullOutput;
        manager->end_ui_result_ = test.end_hr;
        manager->end_ui_callback_ = [&](DWORD id) {
            QIQI_TEST_CHECK(id == 7);
            if (manager->end_ui_calls_ == 1) {
                const ULONG refs = manager->AddRef();
                manager->Release();
                QIQI_TEST_CHECK(
                    ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                    refs == 4);
            }
            thread_mgr = replacement;
            ui->SetUiElementId(7);
            ui->SetCandidates(
                L"replacement", {L"new"}, 2, 0, 0, replacement_document.Get());
        };
        const HRESULT first_end_hr =
            EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 179, GUID_NULL);
        const bool ended = test.manager == Manager::Available && test.valid_id;
        const bool failed_end = ended && FAILED(test.end_hr);
        QIQI_TEST_CHECK(first_end_hr ==
                            (ended ? test.end_hr :
                             test.valid_id ? E_NOINTERFACE : S_OK) &&
                        manager->end_ui_calls_ ==
                            (ended ? failed_end ? 2u : 1u : 0u) &&
                        replacement->end_ui_calls_ ==
                            (ended && !failed_end ? 1u : 0u) &&
                        ui->UiElementId() ==
                            (failed_end || !ended && test.valid_id
                                 ? 7 : TF_INVALID_UIELEMENTID));
        ComPtr<ITfDocumentMgr> retained_document;
        if (failed_end || !ended && test.valid_id) {
            QIQI_TEST_CHECK(
                ui->GetDocumentMgr(retained_document.GetAddressOf()) == S_OK &&
                retained_document.Get() ==
                    (failed_end
                         ? replacement_document.Get()
                         : original_document.Get()));
        } else {
            QIQI_TEST_CHECK(
                ui->GetDocumentMgr(
                    retained_document.GetAddressOf()) == E_FAIL);
        }
        if (failed_end) {
            QIQI_TEST_CHECK(EndCompositionCandidatePresentation(
                *ui.Get(), thread_mgr, 179, GUID_NULL) == test.end_hr);
            QIQI_TEST_CHECK(manager->end_ui_calls_ == 4 &&
                            replacement->end_ui_calls_ == 0 &&
                            ui->UiElementId() == 7);
            manager->end_ui_result_ = S_OK;
            QIQI_TEST_CHECK(EndCompositionCandidatePresentation(
                *ui.Get(), thread_mgr, 179, GUID_NULL) == S_OK);
            QIQI_TEST_CHECK(manager->end_ui_calls_ == 5 &&
                            replacement->end_ui_calls_ == 1 &&
                            ui->UiElementId() == TF_INVALID_UIELEMENTID);
        } else if (!ended && test.valid_id) {
            QIQI_TEST_CHECK(EndCompositionCandidatePresentation(
                *ui.Get(), thread_mgr, 179, GUID_NULL) == E_NOINTERFACE);
        }
        QIQI_TEST_CHECK(
            !ended && test.valid_id
                ? ui->GetDocumentMgr(
                      retained_document.ReleaseAndGetAddressOf()) == S_OK
                : ui->GetDocumentMgr(
                      retained_document.ReleaseAndGetAddressOf()) == E_FAIL);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestPresentationEndFailureRetention() {
    const qiqi::test::TsfTestCaseTrace trace(__func__);
    const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const ComPtr<ITfThreadMgr> thread_mgr = manager;
    ui->SetUiElementId(77);
    ui->SetCandidates(
        L"abcd", {L"one"}, 2, 0, 0, document.Get());
    manager->end_ui_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(EndCompositionCandidatePresentation(
        *ui.Get(), thread_mgr, 180, GUID_NULL) == E_ACCESSDENIED);
    ComPtr<ITfDocumentMgr> retained_document;
    QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                    ui->HasPendingUiElementRetirements() &&
                    ui->GetDocumentMgr(retained_document.GetAddressOf()) == S_OK &&
                    retained_document.Get() == document.Get());
    PresentCompositionCandidates(
        *ui.Get(), TF_TMAE_UIELEMENTENABLEDONLY, PresentationSnapshot(),
        document.Get(), {}, 180, GUID_NULL,
        [&thread_mgr]() noexcept { return thread_mgr.Get(); },
        []() noexcept { return true; });
    QIQI_TEST_CHECK(manager->begin_ui_calls_ == 0 &&
                    manager->end_ui_calls_ == 3 &&
                    ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                    ui->HasPendingUiElementRetirements());
    manager->end_ui_result_ = S_OK;
    QIQI_TEST_CHECK(EndCompositionCandidatePresentation(
        *ui.Get(), thread_mgr, 180, GUID_NULL) == S_OK);
    QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                    !ui->HasPendingUiElementRetirements() &&
                    manager->end_ui_calls_ == 4 &&
                    ui->GetDocumentMgr(
                        retained_document.ReleaseAndGetAddressOf()) == E_FAIL);
}

void TestPresentationSynchronization() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Step { Update, Check, Position, Present };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool has_composition : {false, true}) {
        for (const bool has_preedit : {false, true}) {
            for (const unsigned int stale_check : {0u, 1u, 2u}) {
                const auto context = MakeComObject<MockTfContext>();
                auto original = has_composition ? MakeComObject<MockTfComposition>(context->range_) :
                    ComPtr<MockTfComposition>{};
                auto* const expected_composition = original.Get();
                bool released = false;
                if (original) original->release_observer_ = [&]() noexcept { released = true; };
                auto snapshot = PresentationSnapshot();
                if (!has_preedit) snapshot.composition_text.clear();
                std::vector<Step> trace;
                unsigned int checks_seen = 0;
                SyncCompositionPresentationInSession(
                    359, gsl::not_null<ITfContext*>(context.Get()), snapshot,
                    [&, calls = std::make_unique<unsigned int>(0)](
                        TfEditCookie ec, gsl::not_null<ITfContext*> target, const std::wstring& text) {
                        QIQI_TEST_CHECK(++*calls == 1 && ec == 359 && target.get() == context.Get() &&
                                        &text == &snapshot.composition_text);
                        trace.push_back(Step::Update);
                        ComPtr<ITfComposition> captured = original;
                        original.Reset();
                        return captured;
                    },
                    [&, checks = std::make_unique<unsigned int>(0)](ITfComposition* composition) {
                        checks_seen = ++*checks;
                        trace.push_back(Step::Check);
                        QIQI_TEST_CHECK(composition == expected_composition && !released);
                        if (composition) {
                            QIQI_TEST_CHECK(composition->AddRef() == 2 && composition->Release() == 1);
                        }
                        return checks_seen != stale_check;
                    },
                    [&, calls = std::make_unique<unsigned int>(0)](
                        TfEditCookie ec, gsl::not_null<ITfContext*> target) {
                        QIQI_TEST_CHECK(++*calls == 1 && ec == 359 && target.get() == context.Get() && !released);
                        trace.push_back(Step::Position);
                    },
                    [&, calls = std::make_unique<unsigned int>(0)](const KeyProcessResult& result) {
                        QIQI_TEST_CHECK(++*calls == 1 && &result == &snapshot && !released &&
                                        result.all_display_candidates.size() == 4 && result.current_page == 1);
                        trace.push_back(Step::Present);
                    });
                std::vector<Step> expected{Step::Update, Step::Check};
                if (stale_check != 1) {
                    expected.push_back(Step::Position);
                    expected.push_back(Step::Check);
                    if (stale_check == 0) expected.push_back(Step::Present);
                }
                QIQI_TEST_CHECK(trace == expected && checks_seen == (stale_check == 1 ? 1u : 2u) &&
                                released == has_composition && context->request_edit_session_calls_ == 0 &&
                                context->range_->set_text_calls_ == 0);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 12 && g_com_object_count.load() == objects_before);
    enum class Boundary { Update, FirstCheck, Position, SecondCheck, Present };
    for (const auto boundary : {
             Boundary::Update, Boundary::FirstCheck, Boundary::Position, Boundary::SecondCheck, Boundary::Present}) {
        const auto context = MakeComObject<MockTfContext>();
        auto original = MakeComObject<MockTfComposition>(context->range_);
        auto* const expected_composition = original.Get();
        bool released = false;
        original->release_observer_ = [&]() noexcept { released = true; };
        const auto snapshot = PresentationSnapshot();
        std::vector<Boundary> trace;
        const auto visit = [&](Boundary stage) {
            QIQI_TEST_CHECK(!released);
            trace.push_back(stage);
            if (stage == boundary) throw std::runtime_error("presentation boundary");
        };
        unsigned int checks = 0;
        bool caught = false;
        try {
            SyncCompositionPresentationInSession(
                367, gsl::not_null<ITfContext*>(context.Get()), snapshot,
                [&](TfEditCookie, gsl::not_null<ITfContext*>, const std::wstring&) {
                    ComPtr<ITfComposition> captured = original;
                    original.Reset();
                    visit(Boundary::Update);
                    return captured;
                },
                [&](ITfComposition* composition) {
                    QIQI_TEST_CHECK(composition == expected_composition);
                    visit(++checks == 1 ? Boundary::FirstCheck : Boundary::SecondCheck);
                    return true;
                },
                [&](TfEditCookie, gsl::not_null<ITfContext*>) { visit(Boundary::Position); },
                [&](const KeyProcessResult&) { visit(Boundary::Present); });
        } catch (const std::runtime_error& error) {
            caught = true;
            QIQI_TEST_CHECK(std::string_view(error.what()) == "presentation boundary");
        }
        std::vector<Boundary> expected;
        for (const auto stage : {
                 Boundary::Update, Boundary::FirstCheck, Boundary::Position, Boundary::SecondCheck, Boundary::Present}) {
            expected.push_back(stage);
            if (stage == boundary) break;
        }
        QIQI_TEST_CHECK(caught && released && trace == expected);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionPresentationTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCandidateUiLifecycleFsm();
    TestPresentationBeginBoundaries();
    TestPresentationFallbackAndUpdates();
    TestPresentationNegotiationFailures();
    TestPresentationNestedBegin();
    TestPresentationReentrantUpdateVisibility();
    TestPresentationUpdateRetry();
    TestPresentationPermissions();
    TestPresentationLiveManagerLookup();
    TestPresentationManagerChangeSnapshot();
    TestPresentationManagerFailureRetry();
    TestPresentationReentrantEndFailure();
    TestPresentationEndOwnership();
    TestPresentationEndFailureRetention();
    TestPresentationSynchronization();
    RunTsfCompositionPresentationEntryTests();
}

} // namespace qiqi::test
