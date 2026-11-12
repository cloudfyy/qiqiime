// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "ime/comp/edit.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <memory>
#include <optional>
#include <stdexcept>

namespace qiqi::test {
namespace {

void TestEditEntryGates() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Target { Missing, Same, Other };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool active : {false, true}) {
        for (const bool resetting : {false, true}) {
            for (const bool preparing : {false, true}) {
                for (const bool bound : {false, true}) {
                    for (const auto target : {Target::Missing, Target::Same, Target::Other}) {
                        const auto context = MakeComObject<MockTfContext>();
                        const auto other = MakeComObject<MockTfContext>();
                        CompositionLifecycleState state(647);
                        state.SetActive(active);
                        if (bound) state.SetContext(context);
                        std::optional<CompositionContextResetScope> scope;
                        if (resetting) scope.emplace(state);
                        const ITfContext* pending = preparing ? other.Get() : nullptr;
                        ITfContext* input = target == Target::Missing ? nullptr :
                            target == Target::Same ? context.Get() : other.Get();
                        const GUID activity{0x123, 0x234, 0x345, {1, 2, 3, 4, 5, 6, 7, 8}};
                        const auto refs = context->AddRef();
                        std::ignore = context->Release();
                        const auto other_refs = other->AddRef();
                        std::ignore = other->Release();
                        unsigned int calls_seen = 0;
                        EnterCompositionEditRequest(
                            input, state, pending, 647, activity,
                            [&, calls = std::make_unique<unsigned int>(0)](
                                const ComPtr<ITfContext>& owner, EditSessionTicket ticket) {
                                calls_seen = ++*calls;
                                QIQI_TEST_CHECK(calls_seen == 1 && owner.Get() == context.Get() &&
                                                ticket.Matches(state.Epoch(), context.Get()) && state.IsCurrent(ticket) &&
                                                context->AddRef() == refs + 1 && context->Release() == refs);
                            });
                        const bool eligible = active && !resetting && !preparing && bound && target == Target::Same;
                        QIQI_TEST_CHECK(calls_seen == (eligible ? 1u : 0u) &&
                                        state.IsActive() == active && state.IsResettingContext() == resetting &&
                                        state.Context().Get() == (bound ? context.Get() : nullptr) &&
                                        state.Epoch().context == (resetting ? 1u : 0u) &&
                                        context->AddRef() == refs && context->Release() == refs - 1 &&
                                        other->AddRef() == other_refs && other->Release() == other_refs - 1 &&
                                        context->get_document_calls_ == 0 && other->get_document_calls_ == 0 &&
                                        context->request_edit_session_calls_ == 0 && other->request_edit_session_calls_ == 0 &&
                                        context->range_->set_text_calls_ == 0 && context->prop_->clear_calls_ == 0);
                        ++cases;
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 48 && g_com_object_count.load() == objects_before);
}

void TestEditEntryOwners() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    enum class Exit { Success, Exception, Nested, Deferred };
    const auto objects_before = g_com_object_count.load();
    for (const auto exit : {Exit::Success, Exit::Exception, Exit::Nested, Exit::Deferred}) {
        auto context = MakeComObject<MockTfContext>();
        auto* const raw = context.Get();
        CompositionLifecycleState state(653);
        state.SetActive(true);
        state.SetContext(context);
        bool callback_finished = false;
        bool released = false;
        bool released_after_callback = false;
        raw->release_observer_ = [&]() noexcept {
            released = true;
            released_after_callback = callback_finished;
        };
        ComPtr<LambdaEditSession> session;
        bool caught = false;
        unsigned int calls = 0;
        try {
            EnterCompositionEditRequest(
                raw, state, nullptr, 653, GUID_NULL,
                [&](const ComPtr<ITfContext>& owner, EditSessionTicket ticket) {
                    ++calls;
                    const auto mark_finished = gsl::finally([&]() noexcept { callback_finished = true; });
                    QIQI_TEST_CHECK(owner.Get() == raw && ticket.Matches({0, 0}, raw));
                    if (exit == Exit::Nested) {
                        EnterCompositionEditRequest(
                            raw, state, nullptr, 653, GUID_NULL,
                            [&](const ComPtr<ITfContext>& nested, EditSessionTicket expected) {
                                QIQI_TEST_CHECK(nested.Get() == raw && expected.Matches({0, 0}, raw) &&
                                                raw->AddRef() == 5 && raw->Release() == 4);
                                state.InvalidateContext();
                            });
                        QIQI_TEST_CHECK(ticket.Matches({0, 0}, raw) && !state.IsCurrent(ticket));
                    }
                    if (exit == Exit::Deferred) {
                        KeyProcessResult snapshot;
                        snapshot.composition_text = L"captured";
                        session = MakeComObject<LambdaEditSession>(
                            [task = CompositionEditTask(owner, snapshot, ticket, GUID_NULL), raw](TfEditCookie ec) {
                                QIQI_TEST_CHECK(ec == 659 && task.Context().Get() == raw &&
                                                task.Ticket().Matches({0, 0}, raw) &&
                                                task.Snapshot().composition_text == L"captured");
                                return S_FALSE;
                            });
                        snapshot = {};
                    }
                    state.ClearContext();
                    state.InvalidateContext();
                    context.Reset();
                    QIQI_TEST_CHECK(!released && owner.Get() == raw && ticket.Matches({0, 0}, raw));
                    if (exit == Exit::Exception) throw std::runtime_error("edit request entry");
                });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        QIQI_TEST_CHECK(calls == 1 && callback_finished && caught == (exit == Exit::Exception) &&
                        !state.Context() && !context && released == (exit != Exit::Deferred) &&
                        state.Epoch().context == (exit == Exit::Nested ? 2u : 1u));
        if (session) {
            QIQI_TEST_CHECK(!released && session->DoEditSession(659) == S_FALSE && !released);
            session.Reset();
        }
        QIQI_TEST_CHECK(released && released_after_callback);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionEditEntryTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestEditEntryGates();
    TestEditEntryOwners();
}

} // namespace qiqi::test
