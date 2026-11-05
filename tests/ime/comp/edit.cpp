#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/comp/tests.h"
#include "ime/comp/edit.h"
#include "ime/tsf/edit_session.h"
#include <array>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace qiqi::test {

namespace {

void TestEditSessionTickets() {
    static_assert(std::is_trivially_copyable_v<EditSessionTicket>);
    static_assert(std::is_nothrow_copy_constructible_v<EditSessionTicket>);
    const auto first = MakeComObject<MockTfContext>();
    const auto second = MakeComObject<MockTfContext>();
    const EditSessionTicket ticket{{7, 11}, first.Get()};
    struct Case {
        EditSessionEpoch epoch;
        const ITfContext* target;
        bool matches;
    };
    const std::array cases{
        Case{{7, 11}, first.Get(), true},
        Case{{8, 11}, first.Get(), false},
        Case{{7, 12}, first.Get(), false},
        Case{{8, 12}, first.Get(), false},
        Case{{7, 11}, second.Get(), false},
        Case{{8, 11}, second.Get(), false},
        Case{{7, 12}, second.Get(), false},
        Case{{7, 13}, first.Get(), false},
        Case{{7, 11}, nullptr, false},
    };
    for (const auto& test : cases) {
        QIQI_TEST_CHECK(ticket.Matches(test.epoch, test.target) == test.matches);
    }
    const EditSessionTicket detached{{7, 11}, nullptr};
    QIQI_TEST_CHECK(detached.Matches({7, 11}, nullptr));
    QIQI_TEST_CHECK(!detached.Matches({7, 11}, first.Get()));
    QIQI_TEST_CHECK(!detached.Matches({7, 12}, nullptr));
}

void TestCompositionEditTasks() {
    static_assert(std::is_copy_constructible_v<CompositionEditTask>);
    static_assert(std::is_nothrow_move_constructible_v<KeyProcessResult>);
    static_assert(std::is_nothrow_move_constructible_v<CompositionEditTask>);
    static_assert(!std::is_copy_assignable_v<CompositionEditTask>);
    static_assert(!std::is_move_assignable_v<CompositionEditTask>);
    const auto objects_before = g_com_object_count.load();
    constexpr GUID activity{0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 1, 2, 3, 4}};
    for (const auto action : {
             KeyActionType::PassThrough, KeyActionType::UpdateState, KeyActionType::Commit, KeyActionType::Cancel}) {
        ComPtr<LambdaEditSession> session;
        {
            auto context = MakeComObject<MockTfContext>();
            auto* const raw_context = context.Get();
            const EditSessionTicket ticket{{19, 29}, raw_context};
            KeyProcessResult snapshot;
            snapshot.action = action;
            snapshot.committed_text = L"commit";
            snapshot.composition_text = L"ab";
            snapshot.page_candidates = {L"third", L"fourth"};
            snapshot.all_display_candidates = {L"first", L"second", L"third", L"fourth", L"fifth"};
            snapshot.page_size = 2;
            snapshot.selected_index = 1;
            snapshot.current_page = 1;
            snapshot.total_pages = 3;
            snapshot.empty_4code_warning = true;
            snapshot.dup_4code_warning = true;
            snapshot.trigger_key_sound = true;
            const auto expected = snapshot;
            const CompositionEditTask task(context, snapshot, ticket, activity);
            CompositionEditTask copy = task;
            CompositionEditTask moved = std::move(copy);
            snapshot = {};
            const auto verify = [expected, ticket, raw_context, activity](const CompositionEditTask& current) {
                const auto& actual = current.Snapshot();
                QIQI_TEST_CHECK(std::tie(
                    actual.action, actual.committed_text, actual.composition_text, actual.page_candidates,
                    actual.all_display_candidates, actual.page_size, actual.selected_index, actual.current_page,
                    actual.total_pages, actual.empty_4code_warning, actual.dup_4code_warning, actual.trigger_key_sound) ==
                    std::tie(
                    expected.action, expected.committed_text, expected.composition_text, expected.page_candidates,
                    expected.all_display_candidates, expected.page_size, expected.selected_index, expected.current_page,
                    expected.total_pages, expected.empty_4code_warning, expected.dup_4code_warning, expected.trigger_key_sound));
                QIQI_TEST_CHECK(current.Context().Get() == raw_context &&
                                current.Ticket().Matches(ticket.epoch, raw_context));
                QIQI_TEST_CHECK(!current.Ticket().Matches({20, 29}, raw_context) &&
                                !current.Ticket().Matches({19, 30}, raw_context));
                QIQI_TEST_CHECK(IsEqualGUID(current.Activity(), activity));
                QIQI_TEST_CHECK(raw_context->request_edit_session_calls_ == 0 &&
                                raw_context->range_->set_text_calls_ == 0 && raw_context->prop_->clear_calls_ == 0);
            };
            verify(task);
            verify(moved);
            session = MakeComObject<LambdaEditSession>(
                [task = std::move(moved), verify](TfEditCookie ec) -> HRESULT {
                    QIQI_TEST_CHECK(ec == 47);
                    verify(task);
                    return S_OK;
                });
            context.Reset();
        }
        QIQI_TEST_CHECK(session->DoEditSession(47) == S_OK);
        session.Reset();
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionEditScopes() {
    static_assert(std::is_nothrow_destructible_v<CompositionEditScope>);
    static_assert(!std::is_copy_constructible_v<CompositionEditScope>);
    static_assert(!std::is_move_constructible_v<CompositionEditScope>);
    enum class Cleanup { None, Pending, Scheduled, Finished, Expired, OtherContext, Removed, Terminated };
    constexpr GUID parent{0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 1, 2, 3, 4}};
    constexpr GUID activity{0x23456789, 0x2345, 0x6789, {0xab, 0xcd, 0xef, 1, 2, 3, 4, 5}};
    const auto objects_before = g_com_object_count.load();
    {
        const auto first = MakeComObject<MockTfContext>();
        const auto second = MakeComObject<MockTfContext>();
        struct Identity {
            EditSessionEpoch epoch;
            const ITfContext* context;
            bool active = true;
            bool current = false;
        };
        const std::array identities{
            Identity{{7, 11}, first.Get(), true, true},
            Identity{{7, 11}, first.Get(), false},
            Identity{{8, 11}, first.Get()},
            Identity{{7, 12}, first.Get()},
            Identity{{8, 12}, first.Get()},
            Identity{{7, 11}, second.Get()},
            Identity{{7, 11}, nullptr},
        };
        size_t cases = 0;
        for (const auto action : {
                 KeyActionType::PassThrough, KeyActionType::UpdateState, KeyActionType::Commit, KeyActionType::Cancel}) {
            KeyProcessResult snapshot;
            snapshot.action = action;
            snapshot.committed_text = L"commit";
            snapshot.composition_text = L"preedit";
            const CompositionEditTask task(first, snapshot, {{7, 11}, first.Get()}, activity);
            for (const auto& identity : identities) {
                for (const auto cleanup : {
                         Cleanup::None, Cleanup::Pending, Cleanup::Scheduled, Cleanup::Finished, Cleanup::Expired,
                         Cleanup::OtherContext, Cleanup::Removed, Cleanup::Terminated}) {
                    CompositionCleanupCoordinator cleanups(101);
                    std::shared_ptr<CompositionCleanup> record;
                    if (cleanup != Cleanup::None) {
                        record = cleanups.TrackStarting(cleanup == Cleanup::OtherContext ? second.Get() : first.Get());
                        if (cleanup == Cleanup::Scheduled) record->id = 79;
                        if (cleanup == Cleanup::Finished || cleanup == Cleanup::Terminated) record->finished = true;
                        if (cleanup == Cleanup::Terminated) record->terminated = true;
                        if (cleanup == Cleanup::Removed) cleanups.MarkContextRemoved(first.Get(), parent);
                        if (cleanup == Cleanup::Expired) record.reset();
                    }
                    GUID current_activity = parent;
                    unsigned int checks = 0;
                    const auto slots_before = cleanups.TrackedCount();
                    const auto first_refs = first->AddRef();
                    std::ignore = first->Release();
                    const auto second_refs = second->AddRef();
                    std::ignore = second->Release();
                    {
                        const CompositionEditScope scope(task, current_activity);
                        QIQI_TEST_CHECK(IsEqualGUID(current_activity, activity));
                        const auto disposition = scope.Check(cleanups, [&](EditSessionTicket ticket) noexcept {
                            ++checks;
                            QIQI_TEST_CHECK(IsEqualGUID(current_activity, activity));
                            QIQI_TEST_CHECK(cleanups.TrackedCount() == slots_before);
                            QIQI_TEST_CHECK(ticket.Matches({7, 11}, first.Get()));
                            return identity.active && ticket.Matches(identity.epoch, identity.context);
                        });
                        const auto expected = !identity.current ? CompositionEditDisposition::Discard :
                            cleanup == Cleanup::Pending || cleanup == Cleanup::Scheduled ?
                                CompositionEditDisposition::Requeue : CompositionEditDisposition::Execute;
                        QIQI_TEST_CHECK(disposition == expected && checks == 1);
                        const bool pruned = identity.current &&
                            (cleanup == Cleanup::Finished || cleanup == Cleanup::Expired ||
                             cleanup == Cleanup::Removed || cleanup == Cleanup::Terminated);
                        QIQI_TEST_CHECK(cleanups.TrackedCount() == (pruned ? 0u : slots_before));
                        QIQI_TEST_CHECK(IsEqualGUID(current_activity, activity));
                        QIQI_TEST_CHECK(first->AddRef() == first_refs && first->Release() == first_refs - 1);
                        QIQI_TEST_CHECK(second->AddRef() == second_refs && second->Release() == second_refs - 1);
                    }
                    QIQI_TEST_CHECK(IsEqualGUID(current_activity, parent));
                    QIQI_TEST_CHECK(task.Snapshot().action == action && task.Snapshot().committed_text == L"commit" &&
                                    task.Snapshot().composition_text == L"preedit" && IsEqualGUID(task.Activity(), activity));
                    ++cases;
                }
            }
        }
        QIQI_TEST_CHECK(cases == 224);
        for (const auto& context : {first, second}) {
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == 0 && context->start_composition_calls_ == 0 &&
                            context->range_->text_.empty() && context->range_->set_text_calls_ == 0 &&
                            context->get_property_calls_ == 0 && context->prop_->clear_calls_ == 0 &&
                            context->set_selection_calls_ == 0);
        }
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestCompositionEditScopeNesting() {
    constexpr GUID parent{0x12345678, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 1, 2, 3, 4}};
    constexpr GUID outer_activity{0x23456789, 0x2345, 0x6789, {0xab, 0xcd, 0xef, 1, 2, 3, 4, 5}};
    constexpr GUID inner_activity{0x34567890, 0x3456, 0x7890, {0xcd, 0xef, 1, 2, 3, 4, 5, 6}};
    const auto objects_before = g_com_object_count.load();
    {
        CompositionCleanupCoordinator cleanups(103);
        const auto context = MakeComObject<MockTfContext>();
        const CompositionEditTask outer(context, {}, {{13, 17}, context.Get()}, outer_activity);
        const CompositionEditTask inner(context, {}, {{13, 17}, context.Get()}, inner_activity);
        GUID activity = parent;
        const auto enter = [&] {
            const CompositionEditScope outer_scope(outer, activity);
            QIQI_TEST_CHECK(IsEqualGUID(activity, outer_activity));
            const auto disposition = outer_scope.Check(
                cleanups, [calls = std::make_unique<unsigned int>(0), &activity, &inner, &cleanups, outer_activity](
                              EditSessionTicket ticket) mutable {
                    QIQI_TEST_CHECK(++*calls == 1);
                    QIQI_TEST_CHECK(ticket.Matches(inner.Ticket().epoch, inner.Context().Get()));
                    {
                        const CompositionEditScope inner_scope(inner, activity);
                        QIQI_TEST_CHECK(IsEqualGUID(activity, inner.Activity()));
                        QIQI_TEST_CHECK(inner_scope.Check(cleanups, [](EditSessionTicket) noexcept { return false; }) ==
                                        CompositionEditDisposition::Discard);
                        activity = GUID_NULL;
                    }
                    QIQI_TEST_CHECK(IsEqualGUID(activity, outer_activity));
                    return true;
                });
            QIQI_TEST_CHECK(disposition == CompositionEditDisposition::Execute &&
                            IsEqualGUID(activity, outer_activity));
            activity = inner_activity;
            return disposition;
        };
        QIQI_TEST_CHECK(enter() == CompositionEditDisposition::Execute && IsEqualGUID(activity, parent));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionEditTests() {
    TestEditSessionTickets();
    TestCompositionEditTasks();
    TestCompositionEditScopes();
    TestCompositionEditScopeNesting();
}

} // namespace qiqi::test
