# Reentrancy-Safe Host Operations: Implementation Plan

## Status and goal

The scoped checked-identity migration and specialized-protocol review are implemented.
Existing validity checks, diagnostics, and host-operation checkpoints are reused.
Final dual-architecture tests, lint and measured native coverage remain release gates.

AST-based checking and its CI targets are deferred because their tooling/setup
cost is outside the current scope. Do not install LLVM development components or
implement the checker for this plan now. Existing markers remain descriptive
comments; runtime tests and manual boundary review remain the verification gates.

Completed pilot:

- Fixed-ticket context checks in admitted editing and update/start adapters.
- Retained-composition checks in update, layout and candidate publication.
- Focused tests for replacement, expected empty state, invalidated versions, and
  original-composition retention.

Start-result publication, commit/cancel, pending target preparation and independent
cleanup still use their existing specialized protocols.
Commit/cancel adapters now also reuse fixed-ticket context guards and the shared
composition guard for original-object ownership. Their detach-before-end,
expected-empty continuation, HRESULT and caret behavior remain unchanged.
Specialized-protocol review also adds a post-release checkpoint before a
current-composition termination resets local state. A regression demonstrates
that a replacement, switched target, or new activation created while releasing
the old composition must survive the old callback.
Start publication now also retains its published object across preparation
teardown and validates it before updating; a second regression prevents old
input from being redirected to a replacement published during that teardown.

Completed non-AST work:

- Commit/cancel guard reuse with unchanged detach-before-end and expected-empty rules.
- Review of start/finish, pending target preparation and independent old cleanup;
  preparation cancellation, stale subscription withdrawal and independent cleanup
  retain their existing protocols.
- Release-boundary regressions for same-context replacement, activation changes,
  context switches including A-to-B-to-A, and failed range lookup.
- Existing host-order, reference-count and exception-unwind assertions remain gates.

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
| 2 | Inventory remaining reentrant boundaries and classify protocols (C; scoped inventory completed below) | Record original identity, dependent continuation, failure policy and ownership; distinguish ordinary edits from start/finish, preparation and cleanup |
| 3 | Mark the already-reviewed pilot methods (D; initial eight markers added) | Step 2 supplies precise marker rules; mark only methods whose required protection is understood |
| 4 | Protection-presence checker and report-only CI (D; deferred) | Not required for the remaining guard migration |
| 5 | Extend guard reuse to compatible remaining sites (C; implemented) | Compatible commit/cancel sites migrated; specialized release gaps fixed and covered by focused reentrancy tests |
| 6 | Blocking CI for presence rules (D; deferred) | Requires resuming and validating the checker first |
| 7 | Boundary/control-flow analysis (E; deferred) | Requires a separately approved tooling effort |

Step 5 uses the inventory rather than depending on a checker. Compatible repeated
capture/check plumbing is migrated; specialized start/finish, preparation and
cleanup protocols keep distinct validity rules. Runtime reentrancy tests remain
a gate throughout; the inventory is not a whole-repository safety certification.

### Scoped host-boundary inventory (step 2)

The inventory covers composition editing, layout/presentation, target preparation,
cleanup, and the access queries they depend on. It is not a complete audit of all
TSF, configuration, activation, or native-window code. Each row records current
behavior, not a request to insert checks mechanically after every instruction.

| Operation | Host boundary and dependent continuation | Original identity and ownership | Error policy / classification |
| --- | --- | --- | --- |
| [EnterCompositionEditRequest / RequestCompositionEditSession](../src/ime/comp/edit.h) | `RequestEditSession` may deliver synchronously; later callbacks validate before editing | Task owns context/result; callback owns service; ticket captured before request | Log actual request/session HRESULTs; no automatic request-failure retry. Context admission plus request protocol |
| [ExecuteCompositionEditInSession](../src/ime/comp/edit.h) | Check ticket before pending cleanup; requeue the result or delegate execution | Original task and activity scope remain alive throughout callback | Stale/requeue returns do not mean edits succeeded. Context-protected delegation |
| [ApplyAdmittedCompositionEditInSession](../src/ime/svc/composition.cpp) | Access queries can reset state; recheck before/after deferred promotion and action dispatch | Fixed ticket in context guard; context/service retained by enclosing callback | Preserve `can_input`/`can_learn` policy and skip semantics. `context` |
| [UpdateCompositionInSession / WriteCompositionTextAndCaret](../src/ime/comp/update.h) | `GetRange`, `SetText`, `Clone`, `Collapse`, `SetSelection`, `GetProperty`, `SetValue`; recheck before subsequent mutations | Composition guard retains original object; local range/property/caret owners; helper receives the same guard predicate | Range/clone failures stop their path; text/caret/attribute best-effort behavior remains. `composition` delegation |
| [ClearCompositionDisplayAttribute](../src/ime/comp/attribute.h) | Check before `GetProperty`, then before `Clear`; caller checks before its continuation | Local property owner; context/range and validity predicate supplied by caller | Return real HRESULT, diagnose missing property; stale uses `S_FALSE`. Shared helper for composition and independent-cleanup policies |
| [PrepareCompositionStart / BeginCompositionInSession](../src/ime/comp/start.h) | QI, query-only insertion and `StartComposition`; validate prepared outputs and resolve reentrant termination before publication | Original ticket and expected empty slot; preparation resources and tracked unpublished result | Distinguish failed/null/stale/terminated results and cleanup handoff. Specialized start protocol; not a plain `composition` marker |
| [SyncCompositionPresentationInSession](../src/ime/comp/presentation.h) | Update returns original object; check before position and again before candidate publication | Returned composition moved into a guard; outer context predicate stays fixed | Preserve update result/failure behavior; no blanket non-null guard. `composition` |
| [QueryCompositionLayoutInSession / UpdateCompositionLayoutPositionInSession](../src/ime/comp/layout.h) | `GetActiveView`, `GetRange`, `GetTextExt`; invalidation prevents positioning and fallback publication | Original composition guard, ticket, retained view and returned range | Preserve optional-position/fallback diagnostics and real errors. `composition` delegation |
| [UpdateCandidateWindowPositionInSession](../src/ime/svc/composition.cpp) | `GetWnd` and position helper can reenter; recheck before moving the captured UI | Fixed context guard, original composition owner and captured UI; view borrowed only synchronously | Failed window lookup keeps diagnosed fallback policy. `composition` |
| [PresentCompositionCandidates / BeginOrUpdateCandidateUI](../src/ime/comp/presentation.h) | Manager QI and `BeginUIElement`; check before publish/update, withdraw a stale returned element ID | Outer adapter has fixed ticket; local UI-manager owner and returned ID; outer composition checks remain separate | Preserve popup/UILess and manager-failure fallback; stale successful registration needs compensation. Context protection plus UI-registration protocol |
| [EndCompositionCandidatePresentation](../src/ime/comp/presentation.h) | Hide/detach old element ID before manager QI / `EndUIElement`; no late overwrite of current ID | Capture old manager and numeric ID before host calls | Diagnose HRESULT; do not recapture replacement manager/ID. Specialized retirement protocol |
| [FinishCompositionInSession / FinishOrInsertCompositionInSession](../src/ime/comp/finish.h) | Guard range/text/attribute/caret steps; detach old state before `EndComposition` | Retained original context/composition/range; caller's fixed ticket | Preserve best-effort failures and explicit transition from old composition to expected empty slot. Specialized finish protocol |
| [InsertCommittedTextInSession](../src/ime/comp/finish.h) | QI, check expected empty state, then final `InsertTextAtSelection` | Local insertion/range owners; caller retains context and expected-empty predicate | Empty text is a no-op; diagnose insertion failure; no following dependent mutation. Finish helper, not context-only |
| [HandleCompositionTermination / CompleteCurrentCompositionTermination](../src/ime/comp/finish.h) | Route tracked old notifications first; detach current composition before range/property cleanup; check before reset | Captured context, old composition and returned range; ticket plus expected empty slot | Ignore unrelated old notifications; preserve cleanup routing and reset rules. Specialized termination protocol |
| [PrepareCompositionTarget](../src/ime/comp/target.h) | Reset, `GetDocumentMgr`, sink `Advise`; validate predicted ticket and withdraw registration after invalidation | Pending context owner, local parent document, post-reset ticket and sink cookie | Missing parent/layout connection keeps diagnosed nonfatal policy; pending Pop cancels. Specialized preparation protocol |
| [MatchesCompositionDocument / ReconcileCompositionDocumentTarget](../src/ime/comp/target.h) | COM identity queries; `GetTop`/`GetBase` read group, then validate before reset or document assignment | Captured original context/ticket and local top/base identities | Diagnose actual errors; preserve existing grouped-query order. Document reconciliation, requires dedicated summary |
| [ReadFastContextFacts / QueryContextInputScopeInSession](../src/ime/comp/access_policy.h) | Compartment/status/view/property/scope queries; caller-supplied validity checks gate use of facts | Local COM/VARIANT/scope-array owners; context/cache ticket supplied by access facade | Preserve Unknown vs QueryError and optional-signal policy; release boundaries can reenter. Read-only query protocol, not a composition guard |
| [RequestCompositionCleanup / ExecuteCompositionCleanup](../src/ime/comp/cleanup.cpp) | `GetRange`, sync/async requests, attribute clear and `EndComposition`; recheck termination/removal before continuing | Shared captured-old cleanup record owns context/composition/range; registry does not own callback lifetime | Retry only synchronous-access refusal under existing conditions; log real errors and finish evidence. Independent `cleanup` |
| [HandleCompositionCleanupTermination](../src/ime/comp/cleanup.cpp) | Record termination before clearing attributes; distinguish unpublished start records from scheduled cleanup | Find and retain matching old cleanup record; no current-service equality requirement | Avoid duplicate clear/end work; preserve removed-context checks. Independent `cleanup` with start-record special case |

#### Eligibility and retained exceptions

- Mark ordinary context/composition pilot adapters and their reviewed protected
  helpers now. Existing strong owners and error semantics remain unchanged.
- Do not require a composition guard in access queries or a current-service guard
  in old cleanup. `ClearCompositionDisplayAttribute` remains multi-policy.
- Do not add a plain marker to start, finish, termination, preparation or UI
  retirement until the checker can represent their explicit identity transitions.
- Terminal calls may have no further dependent mutation; still diagnose their
  results. COM outputs and local-reference destruction remain ownership boundaries,
  not places to invent success or silently refresh the original ticket.
- `GetTop`/`GetBase` and compartment fact reads retain their existing grouped-query
  contracts; the checker needs reviewed helper summaries, not an indiscriminate
  "one guard immediately after every COM call" rule.

Existing evidence includes [reentrant edit/start cases](../tests/ime/svc/reentrancy.cpp),
[start publication](../tests/ime/comp/start_update.cpp),
[target preparation/reconciliation](../tests/ime/comp/target.cpp),
[presentation](../tests/ime/comp/presentation.cpp),
[layout](../tests/ime/comp/layout.cpp),
[cleanup](../tests/ime/comp/cleanup.cpp), and
[edit execution routes](../tests/ime/comp/edit_execution.cpp).
These tests are prior behavior evidence, not proof that an unimplemented static
checker has verified every boundary.

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

### Phase D: mark protected methods and report missing guards (markers piloted; checker deferred)

The checker work below is retained as a future design reference, not active work.

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

The initial markers are on definitions only; declarations inherit the same
symbol's classification. The checker must associate declarations and definitions,
not require duplicate comments:

| Marked definition | Category | Protection the first-stage checker must recognize |
| --- | --- | --- |
| [ApplyCompositionEditInSession](../src/ime/svc/composition.cpp#L167) | `context` | Delegation to `ExecuteCompositionEditInSession` with original-ticket validity callback |
| [ApplyAdmittedCompositionEditInSession](../src/ime/svc/composition.cpp) | `context` | Local `ContextEditGuard`, access/promotion checkpoints and protected action dispatch |
| [SyncCompositionUIInSession](../src/ime/svc/composition.cpp) | `composition` | Fixed context guard delegates to guarded `SyncCompositionPresentationInSession` |
| [UpdateCandidateWindowPositionInSession](../src/ime/svc/composition.cpp) | `composition` | Local fixed context/composition guards plus protected layout helper |
| [BeginOrUpdateCandidateUI](../src/ime/svc/composition.cpp) | `context` | `EnterCompositionCandidatePresentation` supplies ticket; `PresentCompositionCandidates` receives original-ticket validity callback |
| [UpdateCompositionInSession](../src/ime/comp/update.h) | `composition` | Local `CompositionEditGuard`, reused by text/caret helper |
| [SyncCompositionPresentationInSession](../src/ime/comp/presentation.h) | `composition` | Returned composition retained in local guard before position/publication |
| [UpdateCompositionLayoutPositionInSession](../src/ime/comp/layout.h) | `composition` | Local composition guard passed to query helper |

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

### Phase E: check reentrant boundaries and continuation paths (deferred)

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
