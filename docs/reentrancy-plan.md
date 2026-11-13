# Reentrancy-Safe Host Operations: Implementation Plan

## Status and goal

The checked-identity pilot is implemented; the broader migration remains a plan.
Existing validity checks, diagnostics, and host-operation checkpoints are reused.

Completed pilot:

- Fixed-ticket context checks in admitted editing and update/start adapters.
- Retained-composition checks in update, layout and candidate publication.
- Focused tests for replacement, expected empty state, invalidated versions, and
  original-composition retention.

Start-result publication, commit/cancel, pending target preparation and independent
cleanup still use their existing specialized protocols.

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

## 3. Checked-identity objects

The pilot adds two small synchronous guard types alongside the lifecycle checks in
[state.h](../src/ime/comp/state.h):

| Type | Captured identity | What `Current()` checks |
| --- | --- | --- |
| [ContextEditGuard](../src/ime/comp/state.h#L127) | Original ticket; borrows lifecycle state and diagnostic activity | Existing active/reset, version and target checks; explicit composition checks use the same ticket |
| [CompositionEditGuard](../src/ime/comp/state.h#L154) | Retained expected composition, possibly null, and typed validity callback | Calls the existing predicate with that fixed composition |

Both guards expose `[[nodiscard]] bool Current() const` and reuse the existing
diagnostic helpers/predicates. `CompositionEditGuard` is also callable so existing
templated helpers accept it through `std::ref` without new type erasure.
`ContextEditGuard::Current()` is context-only;
`CurrentComposition(nullptr)` explicitly checks an expected empty composition.
Neither guard silently recaptures versions or performs host cleanup.

The implementation deliberately reuses the caller's existing context owner,
rather than adding context `AddRef`/`Release` calls at every guard checkpoint.
The service/task must retain that original context for the synchronous operation.
The composition guard receives the existing composition owner by move where
possible, preserving its original release boundary. Its typed predicate can borrow
the enclosing context guard; both must remain within the synchronous scope.

Implementation constraints:

- Use value snapshots for epochs/tickets, not references to live counters.
- Borrow the stable lifecycle owner and activity only within the synchronous call. The
  enclosing service callback retains the service; guards do not add a self-cycle.
- Keep the original context alive through its existing `ComPtr` owner and move
  existing composition ownership into a guard where possible. Audit
  `AddRef`/`Release` and their timing rather than
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

### Execution order and prerequisites

Phase labels group related work; they are not a requirement to finish all of C
before starting D. Use this order:

| Order | Work | Prerequisite and completion gate |
| --- | --- | --- |
| 1 | Establish guards and a small migration pilot (A/B; completed) | Existing diagnostics and ownership semantics reused; x64/x86 lifecycle and UI tests pass |
| 2 | Inventory remaining reentrant boundaries and classify protocols (C) | Record original identity, dependent continuation, failure policy and ownership; distinguish ordinary edits from start/finish, preparation and cleanup |
| 3 | Mark the already-reviewed pilot methods (D) | Step 2 supplies precise marker rules; mark only methods whose required protection is understood |
| 4 | Implement protection-presence checking and run in report-only CI (D) | Step 3 supplies real fixtures; recognize protected-helper delegation and report parse/configuration failures |
| 5 | Extend guard reuse to compatible remaining sites (C) | Step 4 reports omissions; each migration preserves protocol and release order and passes focused reentrancy tests |
| 6 | Enable blocking CI for verified presence rules (D) | Step 4 fixtures and step 5 migrations demonstrate acceptable false-positive behavior; unsupported protocols remain explicit |
| 7 | Add high-confidence boundary/control-flow rules (E) | Reviewed call inventory, guard semantics and helper summaries are stable; add negative/positive fixtures before making each rule blocking |

The next action is step 2, not a broad mechanical guard rewrite or immediate
full control-flow analyzer. Steps 5 and 6 cover only protocols whose rules have
been verified; do not delay useful pilot checks until every specialized protocol
has been generalized. Runtime reentrancy tests remain a gate throughout.

### Phase A: characterize and introduce the guards (pilot completed)

- Inventory checkpoints in range, attributes, start, finish, layout and
  presentation: original identity, host call, subsequent dependent mutation,
  API error policy, and reference release order.
- Reuse the existing test matrix; add only cases needed to distinguish guard
  modes and expose missing checkpoints.
- Implement the two guards and delegate validity/logging to existing helpers.
- Do not change scheduling, edit-result ownership or cleanup protocols.

### Phase B: migrate a small pilot (implemented)

- Start with the composition-update adapter and text/caret helper.
- Replace repeated capture plumbing with one explicit guard per operation.
- Preserve host-call order, diagnostic labels, HRESULT handling, consumption
  timing, and current `ComPtr` lifetimes.
- Run the focused composition/service suite before extending the migration.
- Stop and revise the abstraction if it increases captures, reference retention,
  special-case flags or indirection rather than reducing them.

### Phase C: extend only where the pilot fits (partially implemented)

- Migrate update-to-position-to-publication checks and compatible attribute
  operations.
- Handle start and finish with explicit identity transitions; do not mechanically
  apply the update guard to them.
- Keep pending preparation and independent cleanup on their existing protocols.
- Add short comments only at intentional exceptions and identity transitions.
- Update [composition contracts](composition.md) with the final API and examples.

The pilot has migrated update-to-position-to-publication and layout identity
plumbing. It does not claim a completed audit of all TSF/UI calls, and deliberately
does not unify start publication, finish transitions, target preparation or cleanup.

### Phase D: mark protected methods and report missing guards (planned)

Use structured source comments immediately above function declarations or
definitions. The comments describe the required protection, not whether the
function itself is a callback:

```cpp
// qiqi-reentrancy: context
// qiqi-reentrancy: composition
// qiqi-reentrancy: cleanup
```

Use one applicable marker per function:

| Marker | Required protection |
| --- | --- |
| `context` | Original-ticket context checks |
| `composition` | Original-ticket and expected-composition checks, including explicitly expected empty state |
| `cleanup` | Independent captured-object termination/removal checks, not current-service activation checks |

Start with methods that call the host and then continue dependent work. Keep
start/finish identity transitions and pending preparation explicit; do not force
them into an inaccurate marker just to satisfy the checker. Define additional
protocol-specific markers only when their checks can be specified precisely.

Implement a checker that associates comments with actual C++ function symbols
and definitions. Prefer the project's existing tooling when applicable; otherwise
evaluate an AST-based tool using the real compile configuration, including
templates and declaration-to-definition association. Do not claim semantic
validation from a regex search for a method name or `guard.Current()`.

First-stage checks:

- Reject unknown marker values and conflicting declaration/definition markers.
- Report a marked method without the corresponding guard/check mechanism.
- Recognize explicit delegation to a reviewed protected helper, including typed
  predicates, rather than requiring redundant local guard construction.
- Report source location, protection category and the missing requirement.
- Integrate the checker into CI after a small, verified pilot. Parse/configuration
  failures must be reported as failures, not an empty successful scan.

This first stage verifies declared protection presence only. It cannot prove
checkpoint placement, find every unmarked method that needs protection, or
establish that all execution paths are safe. C++ contracts alone cannot enforce
the source-level obligation to call a guard.

### Phase E: check reentrant boundaries and continuation paths (planned)

Extend the validated checker incrementally with AST/control-flow analysis:

- Maintain a reviewed inventory of potentially reentrant host calls and helper
  summaries; do not assume every local instruction can reenter.
- Identify dependent document, selection and UI mutations after those calls.
- Report continuations not gated by a successful original-identity check.
- Flag replacing the original ticket with current versions after a host call
  when that replacement is used to authorize old work.
- Model valid early exits, protected helper delegation and explicit start/finish
  identity transitions. Independent cleanup retains its own validity rules.

Do not treat any nearby guard call as sufficient: its result must control the
relevant continuation and use the correct original identity. Introduce hard CI
failures only for high-confidence rules supported by representative fixtures.
Unsupported analysis must remain explicit, not be reported as proof of safety.
Reentrancy-injection tests remain necessary even after static checking is added.

## 6. Validation and acceptance

Use [existing reentrancy cases](../tests/ime/svc/reentrancy.cpp),
[composition tests](../tests/ime/comp), and lifecycle ownership tests.
The pilot adds [TestCapturedEditGuards](../tests/ime/comp/state.cpp#L150) for its
new fixed-identity and ownership behavior.
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

For the marker/checker work, add focused fixtures for missing protection,
incorrect guard category, ignored check results, and conflicting markers.
Include valid helper delegation, expected empty slots, cleanup, and start/finish
transitions to prevent false positives. Boundary-analysis fixtures must distinguish
an entry-only check from a valid post-call check and detect recaptured versions
used to authorize an old continuation. Test parser/configuration failures and CI
exit status. A passing presence check must never be labeled full reentrancy safety.

Memory improvements are not an acceptance claim for this refactor. Use the
[allocation/retention metrics follow-up](arch.md#tracked-follow-up) before
optimizing snapshot storage or pooling edit sessions.
