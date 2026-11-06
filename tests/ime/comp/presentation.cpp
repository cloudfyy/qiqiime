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
        for (const unsigned int stale_check : {0u, 1u, 2u, 3u, 4u}) {
            if (stale_check == 4 && FAILED(outcome.result)) continue;
            const auto ui = MakeComObject<CandidateUI>(g_dll_instance);
            ui->SetPresentationEnabled(false);
            const auto manager = MakeComObject<MockTfThreadMgr>();
            const ComPtr<ITfThreadMgr> thread_mgr = manager;
            manager->begin_ui_result_ = outcome.result;
            manager->return_invalid_ui_id_ = outcome.invalid_id;
            manager->end_ui_result_ = E_ACCESSDENIED;
            manager->end_ui_callback_ = [&](DWORD id) {
                QIQI_TEST_CHECK(id == 7 && ui->UiElementId() == 55 && stale_check == 3);
            };
            constexpr DWORD flags = TF_TMAE_UIELEMENTENABLEDONLY;
            const EditSessionEpoch epoch{11, 13};
            const auto snapshot = PresentationSnapshot();
            unsigned int checks_seen = 0;
            unsigned int manager_reads = 0;
            PresentCompositionCandidates(*ui.Get(), flags, snapshot, epoch, 157, GUID_NULL,
                [&] {
                    QIQI_TEST_CHECK(checks_seen == 1 && ++manager_reads == 1);
                    return thread_mgr.Get();
                },
                [&, checks = std::make_unique<unsigned int>(0)] {
                    checks_seen = ++*checks;
                    CheckCandidateSnapshot(*ui.Get());
                    const ULONG refs = checks_seen == 1 ? 2UL : 3UL;
                    QIQI_TEST_CHECK(manager->AddRef() == refs + 1 && manager->Release() == refs);
                    if (checks_seen == stale_check) {
                        if (stale_check == 3) ui->SetUiElementId(55);
                        return false;
                    }
                    return true;
                });
            const bool began = stale_check != 1 && stale_check != 2;
            const bool published = began && SUCCEEDED(outcome.result) && stale_check != 3;
            const DWORD expected_id = stale_check == 3 ? 55 :
                published && !outcome.invalid_id ? 7 : TF_INVALID_UIELEMENTID;
            const unsigned int expected_checks = stale_check ? stale_check : SUCCEEDED(outcome.result) ? 4u : 3u;
            QIQI_TEST_CHECK(checks_seen == expected_checks && ui->UiElementId() == expected_id &&
                            manager_reads == (stale_check == 1 ? 0u : 1u));
            QIQI_TEST_CHECK(manager->begin_ui_calls_ == (began ? 1u : 0u) &&
                            manager->update_ui_calls_ == (published && stale_check == 0 ? 1u : 0u) &&
                            manager->end_ui_calls_ ==
                                (stale_check == 3 && SUCCEEDED(outcome.result) && !outcome.invalid_id ? 1u : 0u));
            if (manager->update_ui_calls_) {
                QIQI_TEST_CHECK(manager->updated_ui_ids_ == std::vector<DWORD>{expected_id});
            }
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && !shown && !ui->WindowHandle() &&
                            manager->AddRef() == 3 && manager->Release() == 2);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 23 && g_com_object_count.load() == objects_before);
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
            PresentCompositionCandidates(*ui.Get(), DWORD{0}, PresentationSnapshot(), {}, 163, GUID_NULL,
                [&thread_mgr]() noexcept { return thread_mgr.Get(); },
                [&] { return ++checks != stale_check; });
            BOOL shown = FALSE;
            QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && (shown != FALSE) == (stale_check == 0));
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
            PresentCompositionCandidates(*ui.Get(), DWORD{0}, PresentationSnapshot(), {}, 167, GUID_NULL,
                [&thread_mgr]() noexcept { return thread_mgr.Get(); },
                [&] { return ++checks != stale_check; });
            QIQI_TEST_CHECK(ui->UiElementId() == 61 && manager->begin_ui_calls_ == 0 &&
                            manager->end_ui_calls_ == 0 && checks == (stale_check == 1 ? 1u : 2u));
            QIQI_TEST_CHECK(manager->updated_ui_ids_ ==
                            (stale_check == 0 ? std::vector<DWORD>{61} : std::vector<DWORD>{}));
            ++update_cases;
        }
    }
    QIQI_TEST_CHECK(fallback_cases == 6 && update_cases == 6 && g_com_object_count.load() == objects_before);
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
                const ComPtr<ITfThreadMgr> thread_mgr = manager;
                manager->show_own_ui_ = show_own;
                PresentCompositionCandidates(
                    *ui.Get(), flags, PresentationSnapshot(), {}, 173, GUID_NULL,
                    [&thread_mgr]() noexcept { return thread_mgr.Get(); }, []() noexcept { return true; });
                BOOL shown = FALSE;
                const bool expected = enabled && show_own != FALSE;
                QIQI_TEST_CHECK(ui->IsShown(&shown) == S_OK && (shown != FALSE) == expected &&
                                (ui->WindowHandle() != nullptr) == expected);
                if (expected) QIQI_TEST_CHECK(::IsWindowVisible(ui->WindowHandle()));
                QIQI_TEST_CHECK(ui->UiElementId() == 7 && manager->updated_ui_ids_ == std::vector<DWORD>{7});
                manager->end_ui_callback_ = [&](DWORD id) {
                    BOOL ending_shown = TRUE;
                    QIQI_TEST_CHECK(id == 7 && ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                    ui->IsShown(&ending_shown) == S_OK && !ending_shown);
                };
                EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 173, GUID_NULL);
                QIQI_TEST_CHECK(ui->UiElementId() == TF_INVALID_UIELEMENTID && manager->end_ui_calls_ == 1);
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
        PresentCompositionCandidates(*ui.Get(), DWORD{0}, PresentationSnapshot(), {}, 177, GUID_NULL,
            [&] {
                QIQI_TEST_CHECK(checks == 1 && ++reads == 1);
                return live_manager.Get();
            },
            [&] {
                if (++checks == 1) live_manager = replacement;
                return true;
            });
        QIQI_TEST_CHECK(reads == 1 && checks == 4 && original->begin_ui_calls_ == 0 &&
                        replacement->updated_ui_ids_ == std::vector<DWORD>{67} && ui->UiElementId() == 67);
        EndCompositionCandidatePresentation(*ui.Get(), live_manager, 177, GUID_NULL);
        QIQI_TEST_CHECK(original->end_ui_calls_ == 0 && replacement->ended_ui_ids_ == std::vector<DWORD>{67});
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
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
        ComPtr<ITfThreadMgr> thread_mgr;
        if (test.manager != Manager::Missing) thread_mgr = manager;
        manager->fail_query_ui_mgr_ = test.manager == Manager::Failed;
        manager->return_null_ui_manager_ = test.manager == Manager::NullOutput;
        manager->end_ui_result_ = test.end_hr;
        manager->end_ui_callback_ = [&](DWORD id) {
            QIQI_TEST_CHECK(id == 7 && ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                            manager->AddRef() == 5 && manager->Release() == 4);
            thread_mgr = replacement;
            ui->SetUiElementId(7);
        };
        EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 179, GUID_NULL);
        const bool ended = test.manager == Manager::Available && test.valid_id;
        QIQI_TEST_CHECK(manager->end_ui_calls_ == (ended ? 1u : 0u) && replacement->end_ui_calls_ == 0 &&
                        ui->UiElementId() == (ended ? 7 : TF_INVALID_UIELEMENTID));
        EndCompositionCandidatePresentation(*ui.Get(), thread_mgr, 179, GUID_NULL);
        QIQI_TEST_CHECK(manager->end_ui_calls_ == (ended ? 1u : 0u) &&
                        replacement->end_ui_calls_ == (ended ? 1u : 0u) &&
                        ui->UiElementId() == TF_INVALID_UIELEMENTID);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
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
    TestPresentationBeginBoundaries();
    TestPresentationFallbackAndUpdates();
    TestPresentationPermissions();
    TestPresentationLiveManagerLookup();
    TestPresentationEndOwnership();
    TestPresentationSynchronization();
    RunTsfCompositionPresentationEntryTests();
}

} // namespace qiqi::test
