# Reentrancy-Safe Host Operations: Implementation Plan

## Status and goal

This is a proposed implementation plan, not a description of completed changes.
The current checks remain in place until each migration is verified.

The goal is to make input state safe during same-thread host reentrancy and reduce
the chance of capturing the wrong state, omitting a check, or touching a replacement
composition/UI. STA does not prevent nested callbacks during external calls.

The scope is composition editing and presentation. Activation, target preparation,
and independent cleanup retain their distinct protocols. This work does not add
locks, change TSF scheduling, replay keys, or roll back completed host operations.

## 1. Existing mechanisms to reuse

| Mechanism | Existing implementation |
| --- | --- |
| Activation/context versions and original target | [EditSessionTicket](../src/ime/tsf/edit_session.h#L21) |
| Active/reset gates and identity checks with diagnostics | [CompositionLifecycleState and checks](../src/ime/comp/state.h) |
| Owned result snapshot, stale rejection, cleanup barrier and requeue | [CompositionEditTask and execution](../src/ime/comp/edit.h) |
| Checks between text, range and caret operations | [WriteCompositionTextAndCaret](../src/ime/comp/range.h) |
| Attribute-clear checks and real HRESULT handling | [ClearCompositionDisplayAttribute](../src/ime/comp/attribute.h) |
| Captured composition across positioning and candidate publication | [SyncCompositionPresentationInSession](../src/ime/comp/presentation.h) |
| Start-result tracking before publication | [CompositionStartAttempt](../src/ime/comp/start.h) |
| Detach-before-end and direct insertion | [FinishOrInsertCompositionInSession](../src/ime/comp/finish.h) |
| Pending target, predicted ticket and subscription withdrawal | [PrepareCompositionTarget](../src/ime/comp/target.h) |
| Captured old objects with independent termination/removal checks | [CompositionCleanup](../src/ime/comp/cleanup.h) |

There is already substantial reuse through templated `is_current` callbacks.
The missing abstraction is a named, fixed identity for one synchronous operation,
not a universal wrapper for every COM method.

## 2. Required invariants

1. Capture the operation's original ticket before external work. Never replace it
   with current epochs after reentrancy merely to make validation succeed.
2. Retain the original context and, when relevant, composition. Object lifetime
   and permission to continue are separate checks.
3. Validate before a host operation if its entry validity is not already
   established, and after a reentrant boundary before dependent work continues.
4. State invalidation stops subsequent document/selection/UI work. It does not
   undo an already-started host call.
5. Preserve actual API errors and existing best-effort/nonfatal policies.
   Stale rejection is not an API failure or proof of a successful edit.
6. Keep TSF edit cookies within their granted sessions. Requeue the immutable
   result, not the key or the old cookie.
7. Strong-reference destruction can itself reenter. Preserve existing reference
   release boundaries and member destruction order during migration.

## 3. Proposed checked-identity objects

Introduce two small synchronous guard types alongside the lifecycle checks in
[state.h](../src/ime/comp/state.h). Names below are proposed APIs:

| Type | Captured identity | What `Current()` checks |
| --- | --- | --- |
| `ContextEditGuard` | Original ticket and retained context | Existing active/reset, version and target checks |
| `CompositionEditGuard` | Context identity plus retained expected composition, possibly null | Context validity and exact expected composition |

Both guards expose `[[nodiscard]] bool Current() const` and reuse the existing
diagnostic helpers. They must not silently recapture state or perform host cleanup.
Use explicit constructors/factories to distinguish context-only checks from an
expected empty composition; do not encode both meanings with one nullable argument.

Implementation constraints:

- Use value snapshots for epochs/tickets, not references to live counters.
- Borrow the stable lifecycle owner only within the synchronous call. The
  enclosing service callback retains the service; guards do not add a self-cycle.
- Prefer existing retained `ComPtr` owners when passing context/composition into
  a guard. Audit additional `AddRef`/`Release` and their timing rather than
  assuming reference-count operations are harmless.
- Do not let guards escape into deferred tasks. Tasks continue to own their
  existing snapshots and references; create guards inside the delivered callback.
- Treat guards as fixed identities, not automatically updated state. Delete
  assignment and avoid copying/moving them unless an actual call site requires it.
- Add no heap allocation, type-erased `std::function`, or object pool for checking.
- Destructors release owned references only; they must not validate-and-repair,
  end compositions, restore UI, or invoke a user continuation.

### Usage pattern

The intended shape is the following pseudocode, not a replacement API contract:

```cpp
auto guard = /* original ticket and already retained objects */;
if (!guard.Current()) return;

const HRESULT hr = original_range->SetText(/* original session arguments */);
// Diagnose hr using the existing operation's error policy.
if (!guard.Current()) return;
// Continue only according to that operation's existing HRESULT policy.

// The next dependent host operation uses the same guard, not a new ticket.
```

Existing template helpers can receive a lightweight lambda that calls
`guard.Current()`. This preserves their dependency boundary without duplicating
ticket/composition captures at every adapter.

This abstraction prevents inconsistent capture and reduces repeated plumbing,
but does not automatically prove that every required checkpoint exists.
Checkpoint coverage must also be verified by boundary-injection tests and review.

## 4. Operations that need separate treatment

| Operation | Required treatment |
| --- | --- |
| Start a composition | Check the original context and expected empty slot before starting. Track the unpublished result first. Resolve reentrant termination/invalidation before publication. Only then derive a guard for the returned composition, keeping the original ticket. |
| Commit/cancel | Retain the old composition, guard intermediate operations, then detach shared state before `EndComposition`. A successful continuation after ending checks the expected empty slot, not the ended composition. Never overwrite a reentrant replacement. |
| Update and layout | Use the same captured composition across text updates, positioning and candidate publication. Guard after position queries and before UI publication. |
| Target preparation | Keep pending-target cancellation, predicted post-reset ticket and subscription-cookie ownership. Do not replace these with a guard that requires an already-bound target. |
| Independent old cleanup | Use captured-old-object termination/removal checks. Do not require service activation or equality with the current composition. |
| RequestEditSession | Prepare ownership before the request, which may invoke the callback synchronously. Preserve request/session HRESULT diagnostics and the explicit ASYNC cleanup barrier. |
| COM output and UI registration | Acquire outputs into owned local objects. If invalidated, release or withdraw according to the existing protocol; do not publish returned objects/cookies into new state. |

An expected empty composition is valid. Context-only validity and
`current composition == nullptr` must remain distinct checks.

Do not introduce a generic `CallAndCheck` that collapses HRESULT and validity
into one bool. Such a wrapper could obscure failed-but-current best-effort paths,
successful-but-stale outputs, synchronous callbacks, or required compensation.
Consider a narrowly typed wrapper only after multiple migrated sites demonstrate
the same call, result, ownership and failure semantics.

## 5. Incremental implementation

### Phase A: characterize and introduce the guards

- Inventory checkpoints in range, attributes, start, finish, layout and
  presentation: original identity, host call, subsequent dependent mutation,
  API error policy, and reference release order.
- Reuse the existing test matrix; add only cases needed to distinguish guard
  modes and expose missing checkpoints.
- Implement the two guards and delegate validity/logging to existing helpers.
- Do not change scheduling, edit-result ownership or cleanup protocols.

### Phase B: migrate a small pilot

- Start with the composition-update adapter and text/caret helper.
- Replace repeated capture plumbing with one explicit guard per operation.
- Preserve host-call order, diagnostic labels, HRESULT handling, consumption
  timing, and current `ComPtr` lifetimes.
- Run the focused composition/service suite before extending the migration.
- Stop and revise the abstraction if it increases captures, reference retention,
  special-case flags or indirection rather than reducing them.

### Phase C: extend only where the pilot fits

- Migrate update-to-position-to-publication checks and compatible attribute
  operations.
- Handle start and finish with explicit identity transitions; do not mechanically
  apply the update guard to them.
- Keep pending preparation and independent cleanup on their existing protocols.
- Add short comments only at intentional exceptions and identity transitions.
- Update [composition contracts](composition.md) with the final API and examples.

## 6. Validation and acceptance

Use [existing reentrancy cases](../tests/ime/svc/reentrancy.cpp),
[composition tests](../tests/ime/comp), and lifecycle ownership tests.
Keep runs on each architecture sequential and retain diagnostic logs.

| Scenario | Required observation |
| --- | --- |
| No reentrancy | Same host-call order, text, selection, candidates, return/consumption behavior |
| Deactivate or reactivate during a host call | Old dependent work stops; new activation survives |
| Switch context or A-to-B-to-A | Original ticket stays stale even if the target pointer matches again |
| Replace C1 with C2 in the same context/version | C1 performs no subsequent dependent text, selection or UI mutations |
| Expected empty slot | Valid empty slot permits the intended continuation; a reentrant C2 blocks it |
| Host failure with current identity | Existing real-error diagnostics and failure/best-effort behavior remain |
| Successful call returning after invalidation | Returned objects are not incorrectly published; required compensation occurs |
| EndComposition reentrancy, including failure | New composition/caret/UI state is not overwritten by the old path |
| Nested reset or pending-context Pop | Existing version/reset ownership and preparation cancellation remain |
| Deferred callback and cleanup requeue | Same result is retried without processing the key twice or retaining a cookie |
| Service no longer active during old cleanup | Old cleanup can complete without reviving current UI |
| Guard/reference teardown | No ownership cycle, leak, extra host cleanup or changed critical release order |

First build `qiqi_tsf_ui_test` in the existing x64 configuration and run
`QiQiCompositionLifecycleTest`. After the pilot passes, validate x86 and run
`QiQiTsfUiTest` for presentation changes. Treat failures and permission skips
explicitly; do not report an unexecuted scenario as passing.

The change is accepted only when required boundaries have observable tests:
inject reentrancy at a boundary, snapshot the replacement state, and verify that
the old continuation performs no forbidden later mutations. Existing tests that
already prove a boundary need not be duplicated.

Memory improvements are not an acceptance claim for this refactor. Use the
[allocation/retention metrics follow-up](arch.md#tracked-follow-up) before
optimizing snapshot storage or pooling edit sessions.
