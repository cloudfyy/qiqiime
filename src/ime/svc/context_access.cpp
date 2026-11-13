// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"

namespace qiqi {

bool QiQiTextService::CanQueryContextAccess(ITfContext* context) const noexcept {
    return context && composition_lifecycle_.IsActive() && !composition_lifecycle_.IsResettingContext() &&
           !preparing_input_context_;
}

ContextFacts QiQiTextService::ReadContextAccessFacts(
    gsl::not_null<ITfContext*> context, bool is_active_target) const noexcept {
    const EditSessionTicket ticket{composition_lifecycle_.Epoch(), context.get()};
    const InputScopeClassification cached_scope = is_active_target
        ? context_access_.CachedScopeFor(ticket)
        : InputScopeClassification::Unknown;
    return ReadFastContextFacts(
        context.get(), cached_scope,
        [this, is_active_target, ticket]() noexcept {
            return composition_lifecycle_.IsActive() && !composition_lifecycle_.IsResettingContext() &&
                   !preparing_input_context_ &&
                   (!is_active_target || composition_lifecycle_.IsCurrent(ticket));
        });
}

void QiQiTextService::EnforceContextAccessPolicy(const AccessPolicy& policy, bool is_active_target) {
    if (!policy.should_reset_active) return;
    if (is_active_target || engine_.IsComposing() || composition_lifecycle_.Composition() ||
        shift_tap_.IsCandidate()) {
        ResetCompositionAndUI();
    } else {
        composition_lifecycle_.InvalidateContext();
    }
}

AccessPolicy QiQiTextService::EvaluateContextAccess(ITfContext* context, bool enforce_reset) {
    if (!CanQueryContextAccess(context)) {
        return {.can_input = false, .can_learn = false, .should_reset_active = false};
    }
    const ComPtr<ITfContext> ctx_holder(context);
    const bool is_active_target = (context == composition_lifecycle_.Context().Get());
    const ContextFacts facts = ReadContextAccessFacts(
        gsl::not_null<ITfContext*>(ctx_holder.Get()), is_active_target);
    const AccessPolicy policy = EvaluatePolicy(facts);
    context_access_.LogIfChanged(service_log_id_, current_lifecycle_activity_, facts, policy);
    if (enforce_reset) EnforceContextAccessPolicy(policy, is_active_target);
    return policy;
}

AccessPolicy QiQiTextService::EvaluateContextAccessInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, EditSessionTicket ticket, bool enforce_reset) {
    if (!IsEditSessionCurrent(ticket)) {
        return {.can_input = false, .can_learn = false, .should_reset_active = false};
    }
    const bool query_wnd = context_access_.NeedsWindowQuery(ticket);
    const uint64_t rev = context_access_.Revision();
    ContextFacts facts = ReadFastContextFacts(
        context.get(), context_access_.CachedScopeFor(ticket),
        [this, ticket]() noexcept { return IsEditSessionCurrent(ticket); }, query_wnd);
    if (!IsEditSessionCurrent(ticket)) {
        return {.can_input = false, .can_learn = false, .should_reset_active = false};
    }
    if (query_wnd && !facts.HasError()) {
        context_access_.UpdateWindowFacts(
            ticket, rev, facts.is_native_password_edit, facts.is_native_readonly_edit);
    } else {
        context_access_.ApplyCachedWindowFacts(ticket, facts);
    }
    if (!facts.HasError() && !facts.IsBlocked() && context_access_.NeedsScopeQuery(ticket)) {
        const InputScopeQueryResult scope_res = QueryContextInputScopeInSession(
            ec, context, [this, ticket]() noexcept { return IsEditSessionCurrent(ticket); });
        if (!IsEditSessionCurrent(ticket)) {
            return {.can_input = false, .can_learn = false, .should_reset_active = false};
        }
        context_access_.UpdateScopeResult(ticket, rev, scope_res);
        facts.scope_state = scope_res.classification;
        if (scope_res.classification == InputScopeClassification::QueryError) {
            facts.has_query_error = true;
            facts.stage = scope_res.stage;
            facts.error_hr = scope_res.hr;
            facts.error_vt = scope_res.vt;
        }
    }
    const AccessPolicy policy = EvaluatePolicy(facts);
    context_access_.LogIfChanged(service_log_id_, current_lifecycle_activity_, facts, policy);
    if (enforce_reset && policy.should_reset_active && IsEditSessionCurrent(ticket)) {
        ResetCompositionAndUI();
    }
    return policy;
}

bool QiQiTextService::IsContextKeyboardDisabled(ITfContext* context) const noexcept {
    if (!CanQueryContextAccess(context)) return true;
    const bool is_active_target = (context == composition_lifecycle_.Context().Get());
    const ContextFacts facts = ReadContextAccessFacts(
        gsl::not_null<ITfContext*>(context), is_active_target);
    return !EvaluatePolicy(facts).can_input;
}

} // namespace qiqi
