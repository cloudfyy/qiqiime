#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/edit.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <stdexcept>

namespace qiqi::test {

namespace {

constexpr GUID parent{0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 1, 2, 3, 4}};
constexpr GUID edit_activity{0x23456789, 0x2345, 0x6789, {0xab, 0xcd, 0xef, 1, 2, 3, 4, 5}};
constexpr GUID inner_activity{0x34567890, 0x3456, 0x7890, {0xcd, 0xef, 1, 2, 3, 4, 5, 6}};

void TestEditExecutionRoutes() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Cleanup { None, Pending, Scheduled, Finished, Expired, OtherContext, Removed, Terminated };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool current : {false, true}) {
        for (const auto cleanup : {
                 Cleanup::None, Cleanup::Pending, Cleanup::Scheduled, Cleanup::Finished,
                 Cleanup::Expired, Cleanup::OtherContext, Cleanup::Removed, Cleanup::Terminated}) {
            for (const auto action : {
                     KeyActionType::PassThrough, KeyActionType::UpdateState, KeyActionType::Commit, KeyActionType::Cancel}) {
                const auto context = MakeComObject<MockTfContext>();
                const auto other = MakeComObject<MockTfContext>();
                CompositionCleanupCoordinator cleanups(613);
                std::shared_ptr<CompositionCleanup> record;
                if (cleanup != Cleanup::None) {
                    record = cleanups.TrackStarting(cleanup == Cleanup::OtherContext ? other.Get() : context.Get());
                    if (cleanup == Cleanup::Scheduled) record->id = 617;
                    if (cleanup == Cleanup::Finished || cleanup == Cleanup::Terminated) record->finished = true;
                    if (cleanup == Cleanup::Terminated) record->terminated = true;
                    if (cleanup == Cleanup::Removed) cleanups.MarkContextRemoved(context.Get(), parent);
                    if (cleanup == Cleanup::Expired) record.reset();
                }
                KeyProcessResult snapshot;
                snapshot.action = action;
                snapshot.committed_text = L"commit";
                snapshot.composition_text = L"preedit";
                const CompositionEditTask task(context, snapshot, {{7, 11}, context.Get()}, edit_activity);
                const auto slots_before = cleanups.TrackedCount();
                const auto refs = context->AddRef();
                std::ignore = context->Release();
                GUID activity = parent;
                unsigned int checks_seen = 0;
                unsigned int requeues_seen = 0;
                unsigned int executions_seen = 0;
                const bool pending = cleanup == Cleanup::Pending || cleanup == Cleanup::Scheduled;
                const HRESULT execution_result = action == KeyActionType::Commit ? E_FAIL : S_FALSE;
                const auto result = ExecuteCompositionEditInSession(
                    619, task, activity, cleanups, 613,
                    [&, checks = std::make_unique<unsigned int>(0)](EditSessionTicket ticket) {
                        checks_seen = ++*checks;
                        QIQI_TEST_CHECK(checks_seen == 1 && ticket.Matches({7, 11}, context.Get()) &&
                                        IsEqualGUID(activity, edit_activity) && cleanups.TrackedCount() == slots_before);
                        return current;
                    },
                    [&, calls = std::make_unique<unsigned int>(0)](ITfContext* target, const KeyProcessResult& value) {
                        requeues_seen = ++*calls;
                        QIQI_TEST_CHECK(checks_seen == 1 && current && pending && target == context.Get() &&
                                        &value == &task.Snapshot() && IsEqualGUID(activity, edit_activity));
                        activity = GUID_NULL;
                    },
                    [&, calls = std::make_unique<unsigned int>(0)](
                        TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& value,
                        EditSessionTicket ticket) {
                        executions_seen = ++*calls;
                        QIQI_TEST_CHECK(checks_seen == 1 && current && !pending && ec == 619 && target.get() == context.Get() &&
                                        &value == &task.Snapshot() && ticket.Matches({7, 11}, context.Get()) &&
                                        IsEqualGUID(activity, edit_activity));
                        activity = GUID_NULL;
                        return execution_result;
                    });
                const bool pruned = current && (cleanup == Cleanup::Finished || cleanup == Cleanup::Expired ||
                                                cleanup == Cleanup::Removed || cleanup == Cleanup::Terminated);
                QIQI_TEST_CHECK(result == (current && !pending ? execution_result : S_OK) && checks_seen == 1 &&
                                requeues_seen == (current && pending ? 1u : 0u) &&
                                executions_seen == (current && !pending ? 1u : 0u) && IsEqualGUID(activity, parent) &&
                                cleanups.TrackedCount() == (pruned ? 0u : slots_before) &&
                                task.Snapshot().action == action && task.Snapshot().committed_text == L"commit" &&
                                task.Snapshot().composition_text == L"preedit" &&
                                context->AddRef() == refs && context->Release() == refs - 1 &&
                                context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0 &&
                                context->prop_->clear_calls_ == 0 && context->start_composition_calls_ == 0);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 64 && g_com_object_count.load() == objects_before);
}

void TestEditExecutionUnwind() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    for (const int boundary : {0, 1, 2, 3}) {
        const auto context = MakeComObject<MockTfContext>();
        CompositionCleanupCoordinator cleanups(631);
        std::shared_ptr<CompositionCleanup> pending;
        if (boundary == 1) pending = cleanups.TrackStarting(context.Get());
        const CompositionEditTask task(context, {}, {{13, 17}, context.Get()}, edit_activity);
        const CompositionEditTask inner(context, {}, {{13, 17}, context.Get()}, inner_activity);
        const auto refs = context->AddRef();
        std::ignore = context->Release();
        GUID activity = parent;
        bool caught = false;
        HRESULT result = E_FAIL;
        unsigned int checks = 0;
        unsigned int callbacks = 0;
        try {
            result = ExecuteCompositionEditInSession(
                637, task, activity, cleanups, 631,
                [&](EditSessionTicket ticket) {
                    ++checks;
                    QIQI_TEST_CHECK(ticket.Matches({13, 17}, context.Get()) && IsEqualGUID(activity, edit_activity));
                    if (boundary == 0) {
                        activity = GUID_NULL;
                        throw std::runtime_error("edit identity");
                    }
                    return true;
                },
                [&](ITfContext* target, const KeyProcessResult& value) {
                    ++callbacks;
                    QIQI_TEST_CHECK(boundary == 1 && target == context.Get() &&
                                    &value == &task.Snapshot() && IsEqualGUID(activity, edit_activity));
                    if (boundary == 1) {
                        activity = GUID_NULL;
                        throw std::runtime_error("edit requeue");
                    }
                },
                [&](TfEditCookie ec, gsl::not_null<ITfContext*> target, const KeyProcessResult& value,
                    EditSessionTicket ticket) {
                    ++callbacks;
                    QIQI_TEST_CHECK(ec == 637 && target.get() == context.Get() && &value == &task.Snapshot() &&
                                    ticket.Matches({13, 17}, context.Get()) && IsEqualGUID(activity, edit_activity));
                    if (boundary == 2) {
                        activity = GUID_NULL;
                        throw std::runtime_error("edit execute");
                    }
                    QIQI_TEST_CHECK(boundary == 3 && ExecuteCompositionEditInSession(
                        641, inner, activity, cleanups, 631,
                        [&](EditSessionTicket expected) {
                            QIQI_TEST_CHECK(expected.Matches({13, 17}, context.Get()) && IsEqualGUID(activity, inner_activity));
                            return true;
                        },
                        [&](ITfContext*, const KeyProcessResult&) { QIQI_TEST_CHECK(boundary != 3); },
                        [&](TfEditCookie cookie, gsl::not_null<ITfContext*> nested, const KeyProcessResult& data,
                            EditSessionTicket expected) {
                            QIQI_TEST_CHECK(cookie == 641 && nested.get() == context.Get() && &data == &inner.Snapshot() &&
                                            expected.Matches({13, 17}, context.Get()) && IsEqualGUID(activity, inner_activity));
                            activity = GUID_NULL;
                            return S_FALSE;
                        }) == S_FALSE && IsEqualGUID(activity, edit_activity));
                    activity = GUID_NULL;
                    return S_FALSE;
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(caught == (boundary != 3) && result == (boundary == 3 ? S_FALSE : E_FAIL) &&
                        checks == 1 && callbacks == (boundary == 0 ? 0u : 1u) &&
                        IsEqualGUID(activity, parent) && context->AddRef() == refs && context->Release() == refs - 1 &&
                        context->request_edit_session_calls_ == 0 && context->range_->set_text_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionEditExecutionTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestEditExecutionRoutes();
    TestEditExecutionUnwind();
}

} // namespace qiqi::test
