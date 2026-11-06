#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/edit.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qiqi::test {
namespace {

void TestActionBranches() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Step { Cancel, Commit, Check, Sync, End };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto action : {
             KeyActionType::PassThrough, KeyActionType::UpdateState, KeyActionType::Commit, KeyActionType::Cancel}) {
        for (const bool has_committed_text : {false, true}) {
            for (const bool has_preedit : {false, true}) {
                for (const bool current : {false, true}) {
                    const auto context = MakeComObject<MockTfContext>();
                    KeyProcessResult result;
                    result.action = action;
                    result.committed_text = has_committed_text ? L"commit" : L"";
                    result.composition_text = has_preedit ? L"ab" : L"";
                    result.page_candidates = {L"first", L"second"};
                    result.selected_index = 1;
                    result.current_page = 2;
                    result.dup_4code_warning = true;
                    std::vector<Step> trace;
                    unsigned int checks_seen = 0;
                    const auto check_args = [&](TfEditCookie ec, gsl::not_null<ITfContext*> target) {
                        QIQI_TEST_CHECK(ec == 317 && target.get() == context.Get());
                    };
                    QIQI_TEST_CHECK(ApplyCompositionEditActionInSession(
                        317, gsl::not_null<ITfContext*>(context.Get()), result,
                        [&, checks = std::make_unique<unsigned int>(0)] {
                            checks_seen = ++*checks;
                            trace.push_back(Step::Check);
                            return current;
                        },
                        [&, calls = std::make_unique<unsigned int>(0)](
                            TfEditCookie ec, gsl::not_null<ITfContext*> target) {
                            QIQI_TEST_CHECK(++*calls == 1);
                            check_args(ec, target);
                            trace.push_back(Step::Cancel);
                        },
                        [&, calls = std::make_unique<unsigned int>(0)](
                            TfEditCookie ec, gsl::not_null<ITfContext*> target, const std::wstring& text) {
                            QIQI_TEST_CHECK(++*calls == 1 && &text == &result.committed_text);
                            check_args(ec, target);
                            trace.push_back(Step::Commit);
                        },
                        [&, calls = std::make_unique<unsigned int>(0)](
                            TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& snapshot) {
                            QIQI_TEST_CHECK(++*calls == 1 && &snapshot == &result &&
                                            snapshot.page_candidates.size() == 2 && snapshot.selected_index == 1 &&
                                            snapshot.current_page == 2 && snapshot.dup_4code_warning);
                            check_args(ec, target);
                            trace.push_back(Step::Sync);
                        },
                        [&, calls = std::make_unique<unsigned int>(0)] {
                            QIQI_TEST_CHECK(++*calls == 1);
                            trace.push_back(Step::End);
                        }) == S_OK);
                    std::vector<Step> expected;
                    if (action == KeyActionType::Cancel) {
                        expected = {Step::Cancel, Step::Check};
                        if (current) expected.push_back(Step::End);
                    } else if (action == KeyActionType::Commit) {
                        expected = {Step::Commit, Step::Check};
                        if (current) expected.push_back(has_preedit ? Step::Sync : Step::End);
                    } else if (action == KeyActionType::UpdateState) {
                        expected = {Step::Sync};
                    }
                    QIQI_TEST_CHECK(trace == expected &&
                                    checks_seen == (action == KeyActionType::Cancel || action == KeyActionType::Commit ? 1u : 0u) &&
                                    context->request_edit_session_calls_ == 0 &&
                                    context->range_->set_text_calls_ == 0 && context->start_composition_calls_ == 0);
                    ++cases;
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 32 && g_com_object_count.load() == objects_before);
}

void TestActionCallbackBoundaries() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Stage { Cancel, Commit, CancelCheck, CommitCheck, CommitSync, UpdateSync, CancelEnd, CommitEnd, NestedSync };
    constexpr std::array boundaries{
        Stage::Cancel, Stage::Commit, Stage::CancelCheck, Stage::CommitCheck,
        Stage::CommitSync, Stage::UpdateSync, Stage::CancelEnd, Stage::CommitEnd,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const auto boundary : boundaries) {
        for (const bool throws : {false, true}) {
            const auto context = MakeComObject<MockTfContext>();
            KeyProcessResult result;
            const bool cancel = boundary == Stage::Cancel || boundary == Stage::CancelCheck || boundary == Stage::CancelEnd;
            result.action = cancel ? KeyActionType::Cancel :
                boundary == Stage::UpdateSync ? KeyActionType::UpdateState : KeyActionType::Commit;
            result.committed_text = L"commit";
            if (boundary == Stage::CommitSync) result.composition_text = L"next";
            const GUID previous_activity = log::CreateActivityId();
            const GUID outer_activity = log::CreateActivityId();
            const GUID inner_activity = log::CreateActivityId();
            GUID activity = previous_activity;
            const CompositionEditTask task(context, result, {{331, 337}, context.Get()}, outer_activity);
            const auto& snapshot = task.Snapshot();
            std::vector<Stage> trace;
            bool caught = false;
            const auto visit = [&](Stage stage) {
                QIQI_TEST_CHECK(::IsEqualGUID(activity, outer_activity));
                trace.push_back(stage);
                if (stage != boundary) return;
                if (throws) throw std::runtime_error("action boundary");
                KeyProcessResult nested_result;
                nested_result.action = KeyActionType::UpdateState;
                const CompositionEditTask nested(context, nested_result, task.Ticket(), inner_activity);
                {
                    const CompositionEditScope nested_scope(nested, activity);
                    QIQI_TEST_CHECK(ApplyCompositionEditActionInSession(
                        347, gsl::not_null<ITfContext*>(context.Get()), nested.Snapshot(),
                        [] { QIQI_TEST_CHECK(false); return false; },
                        [](TfEditCookie, gsl::not_null<ITfContext*>) { QIQI_TEST_CHECK(false); },
                        [](TfEditCookie, gsl::not_null<ITfContext*>, const std::wstring&) { QIQI_TEST_CHECK(false); },
                        [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& inner) {
                            QIQI_TEST_CHECK(ec == 347 && target.get() == context.Get() && &inner == &nested.Snapshot() &&
                                            ::IsEqualGUID(activity, inner_activity));
                            trace.push_back(Stage::NestedSync);
                        },
                        [] { QIQI_TEST_CHECK(false); }) == S_OK);
                }
                QIQI_TEST_CHECK(::IsEqualGUID(activity, outer_activity));
            };
            try {
                const CompositionEditScope scope(task, activity);
                QIQI_TEST_CHECK(ApplyCompositionEditActionInSession(
                    349, gsl::not_null<ITfContext*>(context.Get()), snapshot,
                    [&] { visit(cancel ? Stage::CancelCheck : Stage::CommitCheck); return true; },
                    [&](TfEditCookie ec, gsl::not_null<ITfContext*> target) {
                        QIQI_TEST_CHECK(ec == 349 && target.get() == context.Get());
                        visit(Stage::Cancel);
                    },
                    [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const std::wstring& text) {
                        QIQI_TEST_CHECK(ec == 349 && target.get() == context.Get() && &text == &snapshot.committed_text);
                        visit(Stage::Commit);
                    },
                    [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& supplied) {
                        QIQI_TEST_CHECK(ec == 349 && target.get() == context.Get() && &supplied == &snapshot);
                        visit(result.action == KeyActionType::UpdateState ? Stage::UpdateSync : Stage::CommitSync);
                    },
                    [&] { visit(cancel ? Stage::CancelEnd : Stage::CommitEnd); }) == S_OK);
            } catch (const std::runtime_error& error) {
                caught = true;
                QIQI_TEST_CHECK(std::string_view(error.what()) == "action boundary");
            }
            const std::vector<Stage> flow = cancel ? std::vector{Stage::Cancel, Stage::CancelCheck, Stage::CancelEnd} :
                boundary == Stage::UpdateSync ? std::vector{Stage::UpdateSync} :
                boundary == Stage::CommitSync ? std::vector{Stage::Commit, Stage::CommitCheck, Stage::CommitSync} :
                    std::vector{Stage::Commit, Stage::CommitCheck, Stage::CommitEnd};
            std::vector<Stage> expected;
            for (const auto stage : flow) {
                expected.push_back(stage);
                if (stage == boundary) {
                    if (throws) break;
                    expected.push_back(Stage::NestedSync);
                }
            }
            QIQI_TEST_CHECK(trace == expected && caught == throws && ::IsEqualGUID(activity, previous_activity));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 16 && g_com_object_count.load() == objects_before);
}

void TestCommitPreeditReadTiming() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    size_t cases = 0;
    for (const bool before : {false, true}) {
        for (const bool after : {false, true}) {
            const auto context = MakeComObject<MockTfContext>();
            KeyProcessResult result;
            result.action = KeyActionType::Commit;
            result.committed_text = L"commit";
            result.composition_text = before ? L"before" : L"";
            unsigned int synced = 0;
            unsigned int ended = 0;
            QIQI_TEST_CHECK(ApplyCompositionEditActionInSession(
                353, gsl::not_null<ITfContext*>(context.Get()), result, [] { return true; },
                [](TfEditCookie, gsl::not_null<ITfContext*>) { QIQI_TEST_CHECK(false); },
                [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const std::wstring& text) {
                    QIQI_TEST_CHECK(ec == 353 && target.get() == context.Get() && text == L"commit");
                    result.composition_text = after ? L"after" : L"";
                },
                [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& snapshot) {
                    QIQI_TEST_CHECK(ec == 353 && target.get() == context.Get() &&
                                    &snapshot == &result && snapshot.composition_text == L"after");
                    ++synced;
                },
                [&] { ++ended; }) == S_OK);
            QIQI_TEST_CHECK(synced == (after ? 1u : 0u) && ended == (after ? 0u : 1u));
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 4);
}

} // namespace

void RunTsfCompositionEditActionTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestActionBranches();
    TestActionCallbackBoundaries();
    TestCommitPreeditReadTiming();
}

} // namespace qiqi::test
