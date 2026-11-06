# TSF Composition Lifecycle, Deactivation Cleanup, and Diagnostics

This document explains the invariants, ownership, reentrancy, and debugging workflow
for tasks 41 and 43 through 46. The implementation lives in
[`tip_service.cpp`](../src/ime/tsf/tip_service.cpp),
[`tip_composition.cpp`](../src/ime/tsf/tip_composition.cpp), and
[`tip_service.h`](../src/ime/tsf/tip_service.h).
For logging configuration, ETW sessions, and capture tools, see the
[logging guide](log.md).

## 1. Two Separate Problems

1. **Reject stale application callbacks (task 43).** Write, commit, cancel, and layout-read requests may execute after deactivation, reset, or a context switch. A strong COM reference prevents destruction; it does not prove that the request is still valid.
2. **Explicitly finish the old host composition (task 44).** Releasing the local `ITfComposition` reference does not end a TSF composition. The host's `GUID_PROP_COMPOSING` must be removed through `EndComposition` with a valid write cookie, or through host termination. The service must also clear `GUID_PROP_ATTRIBUTE` on the old range.

These paths cannot share a blanket "return when stale" rule. Application callbacks must stop, but independent cleanup of an old composition must remain executable after deactivation. Cleanup may operate only on the captured old objects; it must not change text, recreate compositions, or restore candidate UI.

The implementation **preserves preedit text** and only ends the composition and clears its display attribute. `Deactivate` returns synchronously, but host cleanup may finish later. Returning `S_OK` is not a guarantee that host cleanup has completed.

## 2. Application Edit-Session Barrier

Each service owns a noncopyable, nonmovable
[`CompositionLifecycleState`](../src/ime/comp/state.h), which maintains:

| State | Purpose |
|---|---|
| `IsActive()` | Whether application callbacks may currently execute |
| `IsResettingContext()` | Whether an outer context-reset scope is still executing; application edits and reactivation are rejected |
| `Epoch().activation` | Incremented on valid activation attempts and deactivation, so reactivating the same service cannot revive old requests |
| `Epoch().context` | Incremented before context reset, so an A-to-B-to-A switch cannot revive old requests |
| `Context()` | The current input target; pointer equality does not replace epoch checks |
| `Document()` / `Composition()` | Strong references to the active parent and current host composition |
| `Cleanups()` | The same cleanup coordinator and diagnostic sequence across activations |

The state owner preserves the original member-release order: document, context,
cleanup bookkeeping, then composition. Its individual mutation methods retain
the original assignment/move/release operations; they do not bundle a transition,
implicitly invalidate epochs, reset unrelated fields, or call `EndComposition`.
This includes allowing the original temporary states during reentrant teardown.
The service still orders invalidation before host calls, owns activation and
the pending preparation context, wires the shared preparation entry protocol, and decides when to
publish/reset UI.
The owner stays at the same address for the service lifetime, so live epoch
references and detached cleanup bookkeeping are not replaced on reactivation.

`ApplyEngineResult` and layout callbacks capture the epoch and target. Before executing, `IsEditSessionCurrent` checks activation state, both epoch counters, and the context. Expected stale-request rejection returns `S_OK` and logs `stale-edit-skipped`; it is not an API failure.

[`CheckCompositionEditSessionCurrent / CheckCompositionEditCurrent`](../src/ime/comp/state.h)
now own the original const-state checks and diagnostics behind the service's
same-named entry points. Composition identity is checked only after the ticket
passes, preserving the stale-ticket versus replaced-composition diagnostic order.
A null expected composition matches an empty current slot; no extra non-null
gate is added. These synchronous helpers retain no references, initialize no
logging provider and perform no host calls or state mutation.

`ResetContextAndUI` invalidates the old context **before** removing COM subscriptions, releasing the composition, or clearing UI. After switching targets, the caller checks for reentrant invalidation again before submitting a new request. An epoch is not a per-key sequence number: normal writes within one epoch still execute in FIFO order, rather than retaining only the latest write.

[`CompositionContextResetScope`](../src/ime/comp/state.h) borrows the state owner,
which must outlive the noncopyable, nonmovable scope. Every entry invalidates the
context epoch before checking for an existing reset, including nested attempts.
Only the outer scope returns `Entered() == true`; nested callers return without
repeating teardown. The entered scope restores the reset flag on normal, early,
and exceptional exit without rolling back the epoch. It does not release COM
references, mutate cleanup records, or call the host/UI. `IsCurrent(ticket)`
checks the owner's own reset flag.

[`ResetCompositionContextAndUI`](../src/ime/comp/state.h) now owns the original
log/scope/cleanup-choice/teardown sequence around that scope. The service
provides its real cleanup and UI callbacks, while retaining engine/Shift-tap
reset and activation/preparation entry policy. An entered reset invokes cleanup
only on the original host-cleanup path, then clears context, document, caret
validity and composition, resets the layout connection, and finally ends UI.
Removal still requests no new host edit session.

The outer reset flag spans all cleanup, COM release, subscription and UI work.
Nested attempts advance the context epoch but repeat none of that work.
Exceptions propagate, restoring the flag without undoing the epoch or earlier
steps. No additional active-state/identity gate, retry or post-UI reset is added.
The helper synchronously borrows state, caret flag, layout connection and
callbacks; it retains no service or edit cookie.

[`PrepareCompositionTarget`](../src/ime/comp/target.h) runs the synchronous
new-target workflow after the shared entry gates and activity switch. It
holds the original context reference and the service's pending-target slot,
predicts the next context epoch with a null target, invokes the original reset,
and rechecks the ticket after reset and after `GetDocumentMgr`. Only then does
it publish context/document, retarget the ticket, and advise the layout sink.
The final ticket check withdraws a subscription published after a reentrant
invalidation. The same predicate instance handles all three checks.

Document-query failure or null output and layout-subscription failure retain
the original diagnostics and nonfatal policy when the ticket is still current;
they do not independently reject input or grant edit permission. A non-null
document returned with a failure HRESULT is retained just as before. On exit,
the temporary document releases before the pending-target slot is cleared,
then the local context holder releases, all before the outer entry helper restores its
activity. The helper borrows state, pending storage, and sink connections only
for this call; it does not retain callbacks or an edit cookie. The service keeps
the actual engine/UI reset policy and callback wiring.

[`EnterCompositionTargetPreparation`](../src/ime/comp/target.h) owns the original
entry sequence: missing/inactive/resetting/preparing targets are rejected with
the original diagnostic before the same-target fast path. Only a new target
creates and switches activity ID, invokes the service's original preparation
callback, and restores the saved activity on all exits. It adds no host operation
or identity check and preserves the callback's boolean result or exception.
The pending pointer is non-owning and used only by the initial gate, before any
host call; the real preparation still owns the pending slot and retained context.
Nested accepted entry restores its parent's activity, while rejected entry leaves
activity untouched. Real preparation owners release before activity restoration.

[`CompositionEditTask`](../src/ime/comp/edit.h) owns the captured context, immutable
result snapshot, original ticket, and activity ID. Its synchronous
`CompositionEditScope` borrows that task and the service's activity slot: both must
outlive the scope. It switches to the task activity on entry and restores the
previous activity on every exit, including nested callbacks and early returns.
The scope cannot be copied or moved and retains no service or edit cookie.

The scope's `Check` first invokes the original ticket predicate. A stale task
returns `Discard` without querying or pruning cleanup records. Only a current
task checks the existing same-context cleanup barrier: unfinished unpublished or
ordinary cleanup returns `Requeue`; otherwise it returns `Execute`.
[`ExecuteCompositionEditInSession`](../src/ime/comp/edit.h) owns that scope and
the discard/requeue/execute routing. It logs the original requeue diagnostic and
invokes the service's original snapshot-resubmission callback, or constructs the
non-null context and invokes the original action/UI callback. It borrows the task
and callbacks synchronously, preserves execution results and exceptions, and
restores activity after callback completion even with nesting or activity mutation.
The scope's `Check` itself makes no host call or request.
`Execute` is only an entry decision, not a replacement for the
identity checks between subsequent host calls.

`RequestCompositionEditSession` now owns the original write-request protocol.
It queries the same cleanup coordinator while constructing the request
arguments, including the existing pruning and stage diagnostics. Matching
unfinished unpublished or scheduled cleanup selects
`TF_ES_ASYNC | TF_ES_READWRITE`; otherwise it uses
`TF_ES_ASYNCDONTCARE | TF_ES_READWRITE`. The service keeps its entry gates,
original context/service references and captured task. The callback's second
cleanup check and original-snapshot requeue remain separate and unchanged.
The helper adds no ticket check, wait, retry or cookie ownership.

The initialized session result and original
`FAILED(req_hr) || FAILED(session_hr)` diagnostic condition retain both actual
HRESULTs. Accepted asynchronous requests do not certify callback execution or
successful editing. A rejected request does not reset or reprocess the engine;
subsequent input still applies the complete captured result under the original
policy.

`ApplyCompositionEditActionInSession` selects the action sequence after those
entry checks. It synchronously borrows the original result, target and callbacks;
it retains no service or cookie. Cancel runs before the original empty-composition
identity check and ends UI only if that check succeeds. Commit likewise checks
the original empty-composition identity after the actual commit, then reads
the follow-up preedit at its original point to choose synchronization or UI end.
Update calls the service's original synchronization workflow directly, and
pass-through has no action. The helper adds no entry or post-update checks.
The production snapshot remains immutable, and real host operations and their
failure diagnostics still run in the service and existing components.

`SyncCompositionPresentationInSession` owns the original
start/update -> identity check -> position -> identity check -> publication
sequence. The service keeps the original ticket and executes the real callbacks.
The returned composition reference survives both positioning and publication,
including early or exceptional exit. It may be null: the original identity
predicate still decides whether to proceed, without added null/preedit gates.
The second check uses that same returned composition, not a refreshed service
slot. The helper retains no cookie, adds no scheduling or activity scope, and
does not alter UI permissions, position fallback or failure diagnostics.

### 2.1 In-Flight Host Calls

Entry checks alone are insufficient. `GetRange`, text writes, range cloning and
collapse, selection changes, property lookup/updates, and composition termination
can reenter the service. Application edits strongly retain their original
composition and check `IsCompositionEditCurrent` between host operations. This
checks the epoch, context, and actual composition identity. A replacement
composition in the same context and epoch must not inherit the old operation.

The update helper returns its captured composition so layout and candidate UI
publication can use the same identity barrier. Layout reads also recheck identity
between host calls. Commit/cancel detach the old shared composition and retire
its caret state **before** calling its captured `EndComposition`; a new composition
created inside that call must remain owned and visible when the old call returns.

These barriers cannot undo a host call that has already started. Tests inject
callbacks after observable text/selection/property writes and require that no
subsequent stale operation occur after invalidation.

[`FinishCompositionInSession`](../src/ime/comp/finish.h) shares the synchronous
ending sequence, not the policy of commit and cancel. Commit clears the display
attribute, replaces the text, and collapses the caret through the existing range
helper. Cancel only writes an empty string; it does not clear attributes or set
selection. Failed or empty range retrieval still permits best-effort termination
when the original identity is current, as do ordinary property/text/caret failures.
Invalidation stops the remaining operations at the original check boundaries.

[`FinishOrInsertCompositionInSession`](../src/ime/comp/finish.h) now borrows
the state owner and caret-validity flag, captures the original strong composition
reference, and selects those same finish/direct-insertion paths. The service
keeps its original ticket and identity predicate; every check uses the captured
composition, never a refreshed state slot. One predicate instance serves all
synchronous host boundaries, including noncopyable stateful predicates.

With no composition, cancel only clears the caret-validity flag, without a
ticket check or host call. Commit uses the original direct-insertion helper:
empty text leaves the caret unchanged, and nonempty text follows its existing
interface/identity/insert sequence. With a composition, the original pre-end
callback clears the shared composition first and the caret flag second.
The workflow adds no post-end state writes, scheduling, cookie retention or
new identity checks.

The finishing workflow retains the original composition strongly and supplies
the state detachment callback. The helper holds the acquired range through `EndComposition`
and performs no additional checks or state changes after detachment/termination.
This preserves a new composition and caret state created by a reentrant end call,
including when ending the old composition fails. The separate
[`InsertCommittedTextInSession`](../src/ime/comp/finish.h) handles direct commits:
empty text does nothing; nonempty text queries the insertion interface, checks
identity, and inserts without creating a composition, clearing attributes, or
adjusting selection. Neither helper stores an edit cookie or schedules work.

[`HandleCompositionTermination`](../src/ime/comp/finish.h) first gives the cleanup
coordinator the host-termination notification, then ignores unrelated non-null
notifications, and emits the original current-notification diagnostic.
The service retains the COM entry and actual identity/reset callbacks.
[`CompleteCurrentCompositionTermination`](../src/ime/comp/finish.h)
then captures the original ticket/context and detaches the old composition
before querying the host. [`ClearTerminatedCompositionInSession`](../src/ime/comp/finish.h)
then obtains the old range, clears only its display attribute with the existing
identity checks, and performs the final reset-eligibility check. It never writes
text, ends the already-terminated composition, or resets the engine/UI.
Its `CompositionTerminationResult` retains the range through the caller's original
scope, preserving release order: range, old composition, then context.
`may_reset` is an immediate identity-check result, not a cleanup-success flag or
permission to reuse after further host calls. A range/property/clear failure is
diagnosed but still allows the original service reset when identity remains valid.
A replacement composition created in the same context and epoch must suppress
that reset just as an epoch change does.

Completion releases the branch-local range, composition and context owners
before invoking the service's actual reset callback. Null notification or
missing context follows the original direct-reset route, without attribute
queries or added identity gates. The same synchronous identity predicate is
borrowed through every original check, including move-only predicates.
Exceptions propagate with original owners released; no edit cookie, service
or callback is retained. The service remains responsible for notification
routing and the actual engine/UI reset.

[`UpdateCompositionInSession`](../src/ime/comp/update.h) owns the captured
composition through range lookup, the existing text/caret helper, and optional
display-attribute publication, then returns that same composition for the
service's layout/UI identity checks. It does not publish shared composition
state, reset the engine, begin/end a composition, or request a new edit session.
Range and property lifetimes follow the original local scopes, including early
returns after invalidation. Failure/null range results return the captured
composition without an additional helper identity check; caret/property failures
retain their original best-effort behavior. There is no new check after
`SetValue`; the service's existing caller check still protects UI publication.

The attribute atom is borrowed by reference for this synchronous call, not
snapshotted at entry: its validity is read after text/caret edits, and its actual
value is read again after property lookup and identity validation. This preserves
updates made during host reentrancy, including the original behavior when an atom
becomes invalid after entering the attribute branch. Neither the atom reference
nor the identity predicate is retained after return.

### 2.2 Keyboard Event State

`OnTestKeyDown` and `OnKeyDown` sample modifiers and CapsLock before querying
compartments or preparing a new target. Host calls may pump newer keyboard
messages; their state must apply to the next event, not retroactively to the
event currently being processed. Preflight also snapshots the mouse-button state
used by Shift-tap tracking. This changes neither the engine transition table nor
normal FIFO ordering.

## 3. Cleanup State and Strong/Weak Ownership

[`ScheduleCurrentCompositionCleanup`](../src/ime/comp/state.h) now owns the
current-composition handoff. An empty slot keeps the original `cleanup-not-needed`
diagnostic and returns without registering or requesting cleanup. Otherwise it
moves the original composition to the existing coordinator, using the original
context, client ID, live epoch and activity arguments. It neither changes the
target/epoch/UI nor adds an active-state or application identity check.
Borrowed state/client/activity references do not escape; queued work retains
the existing independent cleanup record and captured request parameters.

The coordinator retrieves the captured composition's range **before** requesting
an edit session. Request failure, read-only rejection and asynchronous acceptance
can therefore follow an already completed range query; its callback can replace
application state before queuing. Later cleanup still targets only the captured
old objects. Missing context/client ID is diagnosed under the original policy,
without fabricating successful termination. Handoff itself does not certify
host cleanup or edit permission.

```text
Pending TSF request --strong--> LambdaEditSession --strong--> CompositionCleanup
                                                               |--strong--> old ITfContext
                                                               |--strong--> old ITfComposition
                                                               `--strong--> old ITfRange (if available)
QiQiTextService --------------------weak--------------------> CompositionCleanup
Old composition --possibly strong--> composition sink (service instance)
```

The cleanup callback captures only `cleanup`, not the service, thread manager, or new context directly. The service stores only weak references to cleanup states, avoiding a strong-reference cycle between the service and its cleanup tasks. A composition may still retain the service through its sink. "No direct service capture" does not mean the service must be destroyed when deactivation returns.

`Deactivate` releases its reference to the thread manager received by `Activate` before returning. It does not wait for asynchronous callbacks, start a worker thread, or block the message pump. Captured state is released through RAII after the request executes or the host releases it. The diagnostic destructor logs `released` before member COM references are destroyed.

| Cleanup state | Exact meaning |
|---|---|
| `started` | The cleanup edit callback has entered; host termination notifications do not set it |
| `attribute_clear_started` | The actual `ITfProperty::Clear` call has begun, not merely property lookup; prevents duplicate clearing during reentrancy |
| `finished` | The edit callback has exited, host termination has handled a task that had not started, or context removal has invalidated cleanup; **this does not mean attribute clearing or composition termination succeeded** |
| `terminated` | The host has reported termination of the old composition; do not call `EndComposition` again |
| `async_requested` | An asynchronous request has been attempted, not necessarily accepted or executed |
| `context_removed` | A Pop/uninitialization notification removed the captured context; skip further edits even if an old callback is later delivered |

The service removes expired or finished weak references when adding or querying tasks. `tracked_cleanups` is only the number of entries in the weak-reference container at that moment. It may include finished or expired entries and is not a pending-request count.

An unpublished `StartComposition` result is temporarily tracked in the same weak
container with `id == 0`, a captured context, and the host's output composition.
This is not a scheduled cleanup and consumes no cleanup diagnostic ID. Removal
and host termination observed before `StartComposition` returns invalidate that
result. A late, still-connected live composition is handed to independent cleanup
with its captured client ID; it must not replace the new shared composition.

[`CompositionCleanupCoordinator::ResolveStarting`](../src/ime/comp/cleanup.cpp)
resolves that unpublished result after the begin helper diagnoses the start HRESULT,
marks the record finished, and checks the original ticket, in that order.
`Publish` requires a successful result, a non-null composition, and a still-current,
connected, unterminated record. Stale live results use the existing independent
cleanup path even if the start HRESULT failed. The resolver owns a strong record
reference across scheduling and reads its termination flag afterward because
scheduling can reenter. `Discard` does not certify cleanup success; `Terminated`
only tells the service to recheck its ticket before resetting the engine/UI.
Successful starts returning no composition are diagnosed with their original HRESULT.

[`BeginCompositionInSession`](../src/ime/comp/start.h) combines preparation,
unpublished registration, the host start call, its HRESULT diagnostic, the finished
mark, the original identity check, and that resolver, in the original order.
Its owning `CompositionStartAttempt` retains partial or complete preparation
resources and the starting record through the caller's original branch scope.
The record releases before the preparation interfaces/range, just as the original
locals did. Unready preparation returns without registering or starting.

Preparation and the post-start check use the same predicate instance, including
stateful, noncopyable predicates. Epoch/activity references remain live across
synchronous host reentrancy; neither those references, the predicate, nor the edit
cookie are retained in the attempt. Extracting the workflow does not add a
cleanup policy for a current failed start that happens to return a composition.

[`EnsureAndUpdateCompositionInSession`](../src/ime/comp/start.h) now owns the
original begin-result -> publication -> update glue. It borrows the lifecycle
state and synchronously uses the same identity predicate for preparation,
resolution, termination-reset authorization and actual composition updates.
Only `Terminated` followed by the original empty-composition identity check
invokes the service's real reset callback. `Publish` moves the accepted composition
into the state owner. The attempt, starting record and preparation resources
release at the end of the original branch, before update's first `GetRange`.
An existing composition bypasses Begin without an extra entry check.

The service retains its original ticket and client-ID snapshot. Epoch/activity
references and the attribute atom remain live across host callbacks; the helper
retains none of those references or an edit cookie. Update owns and returns the
captured composition even if range retrieval fails or same-epoch replacement
makes it stale. Publication is not rolled back by update failure, and the original
later presentation checks still decide whether UI work may continue.

## 4. Deactivation Algorithm and Request Results

1. Set `deactivating_`, invalidate application activation state, and increment the activation epoch.
2. Transfer the current composition into independent state, capturing the old context, range, and diagnostic IDs.
3. Request `TF_ES_SYNC | TF_ES_READWRITE`. The callback clears the display attribute first, then calls `EndComposition` on the old composition.
4. Request `TF_ES_ASYNC | TF_ES_READWRITE` on the same cleanup session **only if** the outer request succeeded, the session result is `TF_E_SYNCHRONOUS`, the callback has not started, the host has not terminated the composition, and the captured context has not been removed.
5. Without waiting for asynchronous completion, reset the local engine, context, candidate UI, subscriptions, and thread-manager reference.

Requests have two result layers; inspect both:

| Outer `request_hr` | Inner `session_hr` | Meaning and handling |
|---|---|---|
| Success | Synchronous callback result | The callback executed; inspect individual API results in the stage logs |
| Success | `TF_E_SYNCHRONOUS` | Synchronous write access is unavailable here; attempt asynchronous recovery only under the conditions above |
| Success | `TF_S_ASYNC` | Queued only; execution may happen later |
| Failure | Any value | The outer request failed; an initialized inner result is not evidence of execution |
| Success | Failure such as `TS_E_READONLY` | Write access is unavailable; do not retry indefinitely or use asynchronous requests to bypass access restrictions |

TSF may reject synchronous read/write sessions during notifications such as layout changes, as specified by the API. An asynchronous request can execute after the notification returns. However, attempting to upgrade read access to write access inside a read session may cause even the asynchronous request to fail. Unrecoverable disconnection, read-only access, or request rejection can leave host composition state or attributes behind. Preserve actual HRESULTs; do not report complete cleanup in these cases.

If range retrieval or attribute clearing fails, cleanup still attempts to end the composition. Consequently, composition termination can succeed while the display attribute remains. Callback exit does not imply complete success. Without a range, there is no `attribute-clear` stage.

### 4.1 Native Windows and Retained UI Objects (Task 41)

Deactivation releases native UI resources even when queued sessions, the host, or
external clients still retain COM references. The candidate and status-bar objects
remain allocated for reactivation; their candidate-selection, paging, menu, and
position callbacks are not disconnected merely because the service is inactive.

The local teardown order is:

1. Disable presentation on both UI objects and invalidate the activation epoch.
2. Reset composition/context state using the cleanup algorithm above. Invalidate
   the published candidate UI-element ID and attempt `EndUIElement`.
3. Destroy the candidate popup, status-bar tooltip, and status bar on their owning
   UI thread. Release status-bar capture/drag state and clear hover/tooltip state.
4. Unadvise TSF subscriptions and release the thread manager and dictionaries.

Independent asynchronous composition cleanup can still finish after window
destruction. This does not delay `Deactivate` or permit the cleanup to restore UI.
An in-flight `BeginUIElement` has no published ID yet: its late successful ID is
ended through the captured old manager after the host call returns.

UI objects start presentation-disabled when owned by a service. Successful
activation enables them again; standalone UI objects retain their previous
default behavior. While disabled or destroying a window, candidate `Show(TRUE)`
returns `S_FALSE`, reports `IsShown == FALSE`, and does not create a popup.
Status-bar and tooltip creation/show paths obey the same presentation gate.
Thus even a host-retained candidate interface cannot reopen an inactive service's
window. Final service destruction also disconnects candidate callbacks that
capture the service.

`BeginOrUpdateCandidateUI` validates the captured epoch/context after host calls
and before publishing an ID or showing/updating UI. A stale begin retires only
its provisional ID; it must not overwrite or hide a reactivated service's UI.
`EndCandidateUI` invalidates the shared ID before calling the host and performs
no shared-state reset after `EndUIElement` returns. Nested teardown therefore
cannot end the same published ID twice or erase a newer registration.

Candidate-position queries also retain the old composition and recheck the
epoch/context around host calls before updating caret state or moving a window.
A regression reproduced an old `GetTextExt` result moving a replacement popup
after reentrant deactivation/reactivation; the result must instead be discarded.

[`QueryCompositionLayoutInSession`](../src/ime/comp/layout.h) now owns this
view/range/text-extent query protocol, preserving short-circuit host calls,
identity checks and HRESULT diagnostics. A supplied notification view takes
precedence over the active view. The final `current` check is independent of
query success and also rejects composition replacement within the same epoch.
The result owns its range through the caller's UI work; the service retains
the original composition snapshot and view parameter. The query range reference
is released before that composition reference, with the view parameter last.
The lifetime mocks destroy composition, range, then view because the composition
also owns the range. No cookie or callback escapes the synchronous query.
Raw rectangles and clipping outputs are unchanged, including zero or inverted
rectangles. The service retains the actual window movement, GUI/cursor query
algorithm and all coordinate/DPI policy.

[`ApplyCompositionLayoutPosition`](../src/ime/comp/layout.h) now applies the
query's original result protocol while borrowing that query and the live caret
flag. A stale query does nothing, even if it obtained a rectangle. Successful
TSF positioning sets the caret-validity flag before moving to the unmodified
rectangle. An unsuccessful query calls the service's original GUI/cursor
fallback only when the current flag is false; successful fallback moves without
automatically making that flag true. Failure, including failure with rectangle
output, logs unchanged positioning without movement.

Clipped, zero or inverted rectangles gain no extra gate or normalization.
The original callbacks execute synchronously, with no added identity check or
state write afterward. Changes to the live flag inside callbacks survive.
The service's original query range, composition and view owners span the window
work and release in their original order. This helper copies only the rectangle,
not the owning query; it retains no service, callback, flag reference or cookie.

`CompositionLayoutTask` owns the notification context, optional view, ticket and
activity ID for deferred layout callbacks, but not a service, composition or
edit cookie. The service retains its original local references and its strong
callback reference. `ExecuteCompositionLayoutInSession` restores the previous
activity ID on stale, normal, nested and exceptional exits, and preserves the
original ticket-check/log/position-update order. Layout tasks deliberately do
not freeze the composition at notification time: a same-epoch replacement is
queried as the current composition when the callback runs. The query itself
then retains and checks that composition as described above.

`RequestCompositionLayoutSession` preserves `TF_ES_ASYNCDONTCARE | TF_ES_READ`,
the initialized session result, the request-return log and actual HRESULTs.
Session-result diagnostics run only after a successful request, including
`S_FALSE`; there is no added retry, write barrier or new queue. The callback
may run synchronously before the request returns. `OnLayoutChange` still
returns `S_OK` after diagnostics; that does not certify request or query success.

The shared Win32 destruction helper validates window ownership before calling
`DestroyWindow`. Wrong-thread calls are rejected and logged with owner/current
thread IDs; native failures retain the handle for an owner-thread retry and log
the actual Win32 error. `WM_NCDESTROY` detaches the matching stored handle,
including external native destruction, preventing reuse of a stale HWND.
The destructor retains the same idempotent cleanup as a final safeguard.

`Deactivate` returns `E_FAIL` if native UI destruction fails, while continuing
the remaining local TSF teardown. `S_OK` means local window teardown succeeded;
it still does not certify successful host composition cleanup or successful host
UI-element deregistration. `TsfLifecycle(..., stage=ui-windows-destroyed, ...)`
reports separate candidate/status destruction results, not host cleanup results.
`ui-begin-stale` identifies a late host registration result and its provisional
ID; `ui-end` records logical ID retirement and whether an old manager is retained.
The normal TSF STA/UI-thread contract remains required: this implementation does
not marshal final destruction to another thread or promise safe final release
from an arbitrary worker thread.

## 5. Key Execution Sequences

### 5.1 Synchronous Success

```text
Deactivate -> invalidate old application requests -> sync-request
  -> callback-enter -> attribute-clear -> end-composition -> callback-exit
  -> sync-result -> release cleanup state -> finish local cleanup -> return
```

A synchronous callback runs before `RequestEditSession` returns, so `callback-enter` normally precedes `sync-result`. `EndComposition` may also reenter a host termination notification. Do not assume stage logs can never interleave.

### 5.2 Synchronous Rejection and Asynchronous Recovery

```text
Deactivate -> sync-result(TF_E_SYNCHRONOUS)
  -> async-request -> async-result(queued=true)
  -> finish local cleanup -> return
Later, TSF -> callback-enter -> attribute-clear -> end-composition -> callback-exit -> released
```

A host may abandon a queued request. `released(started=false, finished=false)` means neither callback execution nor termination handling was observed before state release. Check request results to distinguish rejection from acceptance without execution. This is not a cleanup-success event.

### 5.3 Immediate Reactivation on the Same Context

The default `TF_ES_ASYNCDONTCARE` value is **0**, not a separate flag bit. It permits synchronous execution. Synchronous requests may take priority over queued asynchronous requests, so merely queuing the old cleanup is insufficient:

```text
Old cleanup queued -> Deactivate returns -> reactivate on the same context
  -> new application write finds unfinished cleanup -> write-forced-async
  -> new write uses TF_ES_ASYNC and queues behind old cleanup
  -> end old composition / clear old attribute -> execute new write / create new composition
```

Other contexts are unaffected. Normal write callbacks still pass the epoch barrier; old-composition cleanup intentionally does not. Do not "simplify" this by allowing the new write to execute synchronously, or by making cleanup use the service's current composition or range.

If reactivation occurs inside an outstanding `StartComposition`, a new write can
already be queued before the late old result is available for cleanup. Its edit
callback checks again for matching unfinished cleanup and resubmits the captured
result using explicit `TF_ES_ASYNC`, behind that cleanup. It does not process the
key again or advance the FSM. Under asynchronous FIFO ordering:

```text
new write -> requeue behind cleanup -> old cleanup -> new write
```

### 5.4 Early Host Termination of the Old Composition

`OnCompositionTerminated` distinguishes composition identities:

- **Old composition awaiting cleanup:** set `terminated`. If attribute clearing has not started and the range is available, clear the old attribute using the notification's write cookie. If the callback has not started, also set `finished`. The later edit callback logs `skip-host-terminated` without clearing attributes or ending the composition again.
- **Old composition whose cleanup is executing:** set `terminated`. Termination inside `GetProperty` still clears the attribute with the notification's write cookie, because `started` alone does not mean `Clear` has begun. Termination inside the actual `Clear` call does not clear again. The outer property lookup rechecks termination/removal and skips its stale clear and subsequent `EndComposition`.
- **Current composition:** detach it, clear its attribute, recheck epochs and the expected empty composition slot after reentrancy, then reset the engine, context, and UI. A replacement must not be reset.
- **Unpublished composition being started:** mark the temporary state terminated; do not publish or explicitly end the late result.
- **Neither current nor awaiting cleanup:** ignore the old notification; it must not reset the new composition or UI.

Duplicate old notifications do not clear attributes again. The host notification supplies a valid cookie, so no additional edit session is needed. The host handles termination semantics; the service only clears the display attribute.

## 6. Reentrancy Rules

- Reentrant `Deactivate` during deactivation returns `S_OK` and logs `deactivate-reentrant`. It must not move the composition or release resources again.
- Reentrant `ActivateEx` during deactivation returns the actual `E_UNEXPECTED` and logs the failure. Normal activation is allowed only after the outer deactivation completes.
- Attribute clearing and composition termination can both trigger COM reentrancy. Do not blindly operate on the service's current state after either call. Independent cleanup always uses old captured state.
- RAII resets `deactivating_` and sets `finished` when the callback exits. Do not introduce message-pump waits, background threads, or host edit API calls without a valid write cookie.
- Context reset and input binding have their own RAII guards. Reentrant input during either transition is not consumed, and activation during a lifecycle transition returns `E_UNEXPECTED`. A notification during document queries or layout-sink registration invalidates the outer attempt; it cannot bind a stale target or publish an orphaned subscription.
- Focus UI processing also checks a per-service focus revision. A newer reentrant focus notification supersedes the older one. Context removal alone does not supersede focus loss, so removal during cleanup must not prevent the status bar from being hidden.

### 6.1 Candidate UI Registration and Withdrawal

[`PresentCompositionCandidates`](../src/ime/comp/presentation.h) owns the
synchronous snapshot/manager/Begin/Update protocol. The service keeps its
original entry gates, ticket and strong target reference around this call.
`CandidateUI` is assigned only in the service constructor, so the helper borrows
that stable object. A getter reads the live thread manager at the original query
boundary after the first ticket check; it does not eagerly copy a manager.
The same predicate instance handles the original checks after snapshot update,
manager query, Begin, and Show, where applicable.

A stale successful Begin with a valid ID is ended through the captured old
`ITfUIElementMgr`. It does not clear a subsequently published ID or query a new
manager to end the same number. Failed Begin/manager queries retain the original
diagnosed popup fallback, still subject to the presentation-enabled gate.
Successful negotiation retains `show_own_ui` and activation-flag policy.
Unusual success-with-invalid-ID and failure-with-ID outputs keep the existing
handling; tests for them are not proof that the host registered an element.

`EndCompositionCandidatePresentation` first captures the old thread manager,
then detaches the shared element ID and hides UI before querying/ending through
that manager. A reentrant replacement may reuse the numeric ID without being
cleared afterward. Actual End failures remain diagnosed; this protocol adds no
retry. All local manager references release before the service's original target
holder. No geometry, GUI/cursor fallback, display-permission or FSM rule changes.

## 7. Log Protocol and Troubleshooting

Logs use the existing TraceLogging **`message` string**, not new ETW payload fields. ETW event headers provide PID, TID, timestamp, and level. Debugger output uses the same messages. Detailed stages are visible at Debug/Trace; actual failures also produce Warn records with the real HRESULT.

- `TsfLifecycle(service=..., stage=...)`: activation, deactivation, context reset, stale callbacks, and ordinary host termination.
- `TsfCleanup(service=..., cleanup=..., stage=...)`: the full trace of one independent old-composition cleanup.
- `service` is an atomically allocated service-instance ID within the loaded module. `cleanup` increments within that service instance. Correlate by **PID + module-load lifetime + service + cleanup**. IDs may restart after module unload/reload; do not merge records across module-load lifetimes using numeric IDs alone.
- `activation` and `context` are epochs at the time of logging. In `stale-edit-skipped`, the value before the slash is the request snapshot; the value after it is current. `context_before/after` are epochs before/after reset, **not pointers or COM object IDs**. Cleanup is created after deactivation invalidates application requests, so `created.activation` is the new deactivation epoch, not the original input request's snapshot.
- Do not log input text, keys, candidates, range contents, or raw COM addresses. IDs do not change ownership, and diagnostics introduce no production test interface.

| `TsfCleanup.stage` | Diagnostic meaning |
|---|---|
| `created`, `unavailable`, `get-range` | Capture of the old composition, missing context/client ID, and range-query result |
| `sync-request`, `sync-result` | Flags, actual outer/inner HRESULTs, and callback/host-termination state when the request returns |
| `async-request`, `async-result` | Recovery attempt and queue acceptance; `queued=true` is not completion |
| `sync-failed-no-fallback`, `async-rejected` | No recovery attempted, or asynchronous rejection; Warn preserves the decisive actual HRESULT |
| `callback-enter`, `callback-exit` | Actual execution entry/exit; exit does not claim API success |
| `attribute-clear`, `end-composition` | Two independent API results; inspect both |
| `host-terminated`, `host-attribute-clear` | Early host termination, including duplicate-notification and callback-start state |
| `skip-host-terminated`, `skip-end-after-host-termination` | Expected prevention of duplicate edits/termination, not cleanup failure |
| `write-forced-async` | Which old cleanup caused a new write to require asynchronous scheduling |
| `context-removed`, `skip-removed-context`, `skip-end-after-context-removal` | A removed context invalidated previously queued cleanup; no further attribute or termination calls are made after observing removal |
| `skip-request-invalidated` | Range retrieval reentered termination/removal; do not submit a new edit-session request for the invalidated cleanup |
| `released` | State snapshot at the start of destruction; not proof that all COM objects are destroyed or cleanup succeeded |

Additional `TsfLifecycle.stage` values include `replaced-composition-edit-skipped`
(same epoch, different composition), `composition-start-write-deferred`,
`composition-start-terminated`, `composition-start-removed`,
`stale-composition-start`, and `write-requeued-behind-cleanup`. These expose
logical outcomes without logging input text, key values, or COM addresses.

For a recoverable synchronous rejection, filtering by the same IDs should show:

```text
TsfCleanup(service=1, cleanup=2, stage=sync-result, request_hr=0x00000000, session_hr=..., started=false, terminated=false)
TsfCleanup(service=1, cleanup=2, stage=async-result, request_hr=0x00000000, session_hr=..., queued=true)
TsfCleanup(service=1, cleanup=2, stage=callback-enter, async_requested=true, terminated=false, has_range=true)
TsfCleanup(service=1, cleanup=2, stage=attribute-clear, hr=0x00000000)
TsfCleanup(service=1, cleanup=2, stage=end-composition, hr=0x00000000)
TsfCleanup(service=1, cleanup=2, stage=callback-exit, terminated=false)
```

Other stages are omitted from this example. Actual logs retain full hexadecimal HRESULTs.

1. **Queued, but no callback entry:** first confirm capture is still active, the log level has not changed, and the host thread is pumping messages. Then inspect `released`. Missing events alone prove neither a leak nor success.
2. **Callback ran, but attributes remain:** check `get-range`, `attribute-clear`, and `end-composition` separately. Do not rely on `callback-exit` or `finished`.
3. **New attributes disappear after reactivation:** inspect the old IDs' `write-forced-async`, termination, and skip stages. Compare new epochs to determine whether FIFO was broken or new objects were used by old cleanup.
4. **Expected new input is discarded:** inspect snapshot/current epochs, `active`, and `target_matches` in `stale-edit-skipped`. Look backward for the corresponding `context-reset` or deactivation.
5. **No stage records at Info/Off:** this is normal filtering. Temporarily enable Debug and a receiver as described in the [logging guide](log.md), then restore configuration after capture.

## 8. Document and Context Notifications

### 8.1 Context Is Still the Input Identity

`active_context_` remains the input target. `active_document_` caches only that context's parent, obtained during input binding. It is not a document registry, focus history, or list of every document in the process. The cached parent makes uninitialization recognizable even when the context's `GetDocumentMgr` already fails after disconnection. Both references are released on reset/deactivation. Parent comparisons use COM `IUnknown` identity when interface pointers differ.

`OnInitDocumentMgr` owns no document-wide resources and remains a no-op. Notifications do not proactively bind a new input target; the next actual key-down binds its context.

### 8.2 Reconciling Focus and Stack Changes

`ReconcileDocumentTarget` queries the incoming document's `GetTop` and `GetBase`:

| Relationship | Action |
|---|---|
| Current context is the top | Retain the current composition and epoch |
| Current context is the base, covered by a secondary/modal context | Retain the main composition; a modal push alone is not target invalidation |
| Neither top nor base is current, or a stack query fails | Invalidate old application requests and reset the old target, attempting authorized host cleanup |
| A query reenters deactivation/reset and changes the captured epoch | Return `false`; the outer focus notification must not continue with stale state |

`OnPushContext` first checks the pushed context's parent. An unrelated document cannot reset current input. A related push uses the same reconciliation logic as focus changes. No Push/Pop is initiated from these callbacks, and no callback order beyond the API's guarantees is assumed.

`OnPopContext` compares the removed context itself, not just its document. Popping an unrelated context or the modal context above the current main context does not reset current input. Removing the actual input context does. `OnUninitDocumentMgr` resets only when the cached parent matches. Repeated notifications are harmless after the active relationship has been cleared.

Preserving the main composition on a modal notification is not a request to process keys for the modal UI. Existing requests still require a TSF-granted edit cookie and the epoch barrier. If an actual key-down arrives for a different context, it follows the explicit input-target transition described below.

### 8.3 Reset Before Processing a New Target's Key

Previously, `ApplyEngineResult` noticed context changes only after `engine_.ProcessKey` had already run. Consequently, Backspace in a fresh context could operate on the previous context's engine composition. Merely choosing a new COM range did not prevent that cross-target carryover.

`PrepareInputContext` now runs before processing an actual key-down, and before contextual mode-toggle commit:

1. Reject missing/inactive targets and reentrant transitions without consuming the key.
2. When the target differs, reset the old engine and invalidate its epoch **before** interpreting the new key.
3. Reuse the old-composition cleanup path, preserving its preedit text rather than deleting it.
4. Capture the new parent document and attach the layout sink. Recheck epochs after reentrant COM calls, and remove a subscription published after an invalidating notification.
5. Only then process the key and submit its result for the prepared context.

The preparation guard retains the pending context, not just a Boolean flag. A matching `OnPopContext` during `GetDocumentMgr` invalidates the epoch even before `active_context_` is assigned, so the outer operation declines the key without attaching a sink or requesting an edit session. Unrelated removals do not cancel preparation. RAII releases the pending reference on every exit.

`ApplyEngineResult` no longer switches targets after the fact. UI/configuration callbacks can submit results only for an already prepared current context. Backspace in a fresh context is therefore passed through when its engine has no composition, instead of creating text derived from the previous context.

Switching from Chinese to English with Shift, Ctrl+Space, the language/status bar,
or either system mode compartment follows `commit_on_english_switch`. Its default,
`true`, commits the current raw code, preserving the preedit literally, including
a temporary-English `z` guide; it does not select a Chinese candidate. `false`
cancels only the current composition and clears its preedit, not previously
committed text. Escape still cancels, and Space/Enter in temporary English still
omit its guide. The setting is persisted as `CommitOnEnglishSwitch` (DWORD 1/0),
defaults to commit when absent, and participates in live configuration sync and
dictionary-manager backup/restore.

Both policies use the existing application edit-session path and FIFO ordering.
Commit clears display attributes, collapses the caret and ends the composition;
cancel removes the composition text and ends the composition. Switching targets
first resets the engine, so an old target's code is never committed into a new
target. The policy does not apply to focus-loss/deactivation cleanup.

This option changes the host edit action, not the key FSM's state transition:
both policies call `SetChineseMode(false)`, clearing engine composition data and
entering `English` before submitting the captured host edit. A synchronous
`EndComposition` callback therefore observes English key prediction, and a
reentrant system-mode notification cannot resubmit the old raw code. Switching
back enters `Idle`; width and punctuation settings
are retained. No new key event, transition-table entry or engine state is needed.
Regression tests cover both policies across idle, all four Wubi composing states
and temporary English, in half/full-width modes.

`OnTestKeyDown` must predict that same boundary without performing it. For the current context it uses `ShouldEatKey`; for a different context it uses the read-only `ShouldEatKeyAfterReset`. Both routes share the configured key classifier and transition table. The preview does not bind contexts, reset the live engine, end host compositions, or request edit sessions; the existing Shift-tap tracking remains separate. Missing/inactive targets and reentrant reset/binding are declined. A positive prediction is not an edit-permission grant: the actual callback still prepares and rechecks its target.

These lifecycle and epoch checks do not classify every password, read-only, or otherwise restricted input field. The existing disabled-compartment checks are not a unified policy across every key and asynchronous callback; that policy remains separate task 36 research.

The engine's seven states and sixteen key-event categories are unchanged. Lifecycle reset is external to the key transition table. `StateAfterCompositionReset`, shared by actual reset and preview, maps Chinese composing states (including `TempEnglish`) to `Idle`, leaves `Idle` unchanged, and preserves `English`. Full-width and punctuation modes are retained. A deferred old-host cleanup must not keep the new target's FSM in a composing state.

The shared reset path also fixes a second gap in task 43: context switching and focus loss previously released the local composition reference without explicitly ending the host composition. Connected-target resets now use `ScheduleCompositionCleanup`, the same synchronous attempt and controlled asynchronous recovery used by deactivation. If focus returns to the same context before old cleanup executes, `HasPendingCompositionCleanup` still forces the new write behind it.

### 8.4 Removal Is Not a New Write Opportunity

Pop and document-uninitialization handling use `ResetCompositionAndUI(false)`. They invalidate callbacks, release local context/document/composition references, detach subscriptions, and end candidate UI **without requesting a new edit session**. Removal does not prove that editing access remains available. Any remaining host termination belongs to TSF/the host; local release is not logged as successful `EndComposition`.

Removal also marks matching independent cleanup states with `context_removed` and `finished`. A cleanup request accepted before removal may still be delivered by a scheduler; it logs `skip-removed-context` rather than clearing attributes or ending the old composition. Removal during attribute clearing prevents the subsequent `EndComposition` call. Old host termination notifications must not clear a newly bound composition.

### 8.5 Additional Diagnostic Fields

All records retain the existing service/cleanup correlation scheme:

- `document-target`: `source=focus|push`, `retained`, and `covered_by_modal` explain why the current target was retained or reset.
- `document-uninit`: `related` says whether the cached parent matched.
- `context-push`: `related` distinguishes current-document stack changes from unrelated notifications.
- `context-pop`: `current` identifies removal of the actual input target; `preparing` identifies a target still being bound.
- `context-reset`: `try_host_cleanup=false` identifies the removal path; `cleanup-skipped-removed-context` explicitly reports local release without claiming host termination.
- `input-document-unavailable` preserves the actual parent-query HRESULT. Editing is not assumed to succeed: the subsequent edit-session request still determines access, while no parent relationship is fabricated.

## 9. Validation and Remaining Boundaries

The [real-host test record](hosts.md) includes partial manual feedback from 2026-11-03. It is not a completed compatibility matrix or proof that every UWP/AppContainer host is supported; task 47 remains open.

The [service start regressions](../tests/ime/tsf/service_reentrancy.cpp) exercise
five start outcomes through the real service: `S_OK`, `S_FALSE`, failure with a
composition, successful null output, and failed null output. Five current-target
cases check recovery of the original engine snapshot and immunity to a late
termination from a failed, unpublished result. The start-reentrancy matrix has
38 cases spanning deactivation, same-context reactivation, target switches,
removal, and host termination with synchronous/asynchronous cleanup where
applicable. Null outputs do not fabricate host termination notifications.
Checks cover cleanup of stale non-null results even after failure, no cleanup
for null results, unchanged new-target UI/text, and cleanup's own edit cookie.
These are mock-host outcome injections, not evidence that a specific real host
returns a non-null composition on failure.

The existing 91 edit-boundary and four cleanup-reentrancy cases remain intact.
This stage adds tests, not production behavior: it does not change the existing
policy for a current failed start with non-null output. The current composition,
epochs, context/document, and cleanup coordinator now share the state owner
described above. Cross-stage preparation, activation, actual teardown, and UI
coordination remain in the service; the complete R05 coordinator is unfinished.

The [start/update component tests](../tests/ime/comp/start_update.cpp) add
34 cases crossing empty/nonempty input with existing/new compositions, start
HRESULT/null outputs, preparation failure, stale cleanup, termination/reset
decisions, same-epoch replacement, range failure before/after publication and
an atom changed during Start. They verify exact identity-check counts, published
and returned identities, move-only predicates/reset callbacks, preparation release
before update, retained references and cleanup's original client ID/edit cookie.
Four additional exception cases cover post-start identity, update range/identity
and termination reset; exceptions propagate while original owners release.
These run through the existing start entry in both full and focused test modes.

The [edit-execution tests](../tests/ime/comp/edit_execution.cpp) add 64
current/stale, eight-cleanup-state and four-action combinations. They verify
ticket-before-registry ordering, discard without pruning, pending requeue versus
finished/expired/removed/terminated pruning, original snapshot address,
ticket/cookie, move-only callbacks, precise references and return propagation.
Three identity/requeue/execute exception cases and one nested execution case
check owner preservation and activity restoration after callback mutation.
The original 224 scope cases and real-service requeue/action regressions remain.

The [identity tests](../tests/ime/comp/identity.cpp) add 96 combinations of active
state, reset scope, bound target, ticket validity, current composition presence
and null/original/replacement identity. They check results, unchanged ownership
and state, precise reference counts and zero host operations. The existing
56-case ticket matrix also checks the new diagnostic entry directly; all original
service identity/reentrancy call sites remain unchanged.

The [termination-completion tests](../tests/ime/comp/termination.cpp) add 24
empty/nonempty-preedit and outcome/reentrancy combinations. They verify normal
and inactive behavior, range/property/clear failures, same-epoch replacement at
range/property/clear, epoch invalidation, null notification and absent context.
Checks lock exact traces, original ticket/cookie, strong references and release
before the real reset callback, with no host EndComposition or text writes.
Three range/identity/reset exception cases verify propagation and owner release.
Original low-level termination and real-service reentrancy matrices remain.

The [termination-route tests](../tests/ime/comp/termination_route.cpp) add 20
active/inactive combinations with current, null/current, null/empty, missing
context, unrelated, starting/current, starting/other, scheduled, removed and
expired weak-record notifications. They verify coordinator-first routing even
when a starting record overlaps the current slot, duplicate consumption without
application callbacks or repeated attribute clearing, and deferred delivery
without ending the already-terminated composition. Checks cover the original
cookie, identity/reset counts, state/epoch, unchanged text and final COM counts.
Move-only callbacks are synchronously borrowed by the completion helper.

The [finish-state tests](../tests/ime/comp/finish_state.cpp) add 60 cases:
commit/cancel and three text shapes (empty, ordinary, embedded null/surrogate pair)
cross ten composition states/outcomes. They check exact host/identity traces,
no-composition behavior, captured identities, range/finish errors, same-epoch
replacement at range/text/end, pre-end state/caret retirement, and strong
composition/range reference counts. Eight range/check/text/end exception cases
verify propagation and unchanged or detached state with original owners released.
The original low-level finish/direct-insert fault matrices and real-service
action/handoff regressions remain in both test modes.

The [state tests](../tests/ime/comp/state.cpp) use real reset scopes in the
56-case identity matrix. Another 36 cases cross active/inactive state,
bound/unbound targets, nested depths 0/1/3, and normal/early/exceptional exit;
they check epoch advancement, flag restoration, unchanged ownership/records,
and absence of host operations. Two real-service cases inject one or three
nested focus-loss resets during old cleanup: input and activation are rejected
until the outer reset finishes, cleanup/UI are not repeated, and a fresh
composition works afterward.

The [context-reset tests](../tests/ime/comp/context_reset.cpp) add 32
active/cleanup/composition/layout-connection/nested combinations using the real
cleanup coordinator and scoped connection. They verify exact cleanup/UI traces,
edit cookie, attribute clearing, retained references and no text write.
Six cleanup/UI callback cases cover one/three nested attempts or exceptions;
four last-owner cases check precise context/composition/range final releases
and document lifetime through its existing weak parent link. Connected layout
sources retain the context until subscription reset, after document/composition
release; an unconnected context releases earlier. UI remains the final step.

The [cleanup-handoff tests](../tests/ime/comp/cleanup_handoff.cpp) add 24
active/composition/request-condition cases covering immediate/deferred cleanup,
request rejection, read-only access and unavailable client/context. They check
the pre-request range query, exact flags/client ID/edit cookie, current-slot
transfer, retained references and unchanged preedit. Twelve range/attribute/end
reentrancy cases cross synchronous/deferred execution with same-epoch replacement
or context-epoch invalidation; old cleanup cannot modify the replacement.
Existing reset and state-owner-destruction tests now use the same handoff helper,
including cleanup executing after the state owner has been destroyed.

The [target-entry tests](../tests/ime/comp/target_entry.cpp) add 48
active/resetting/preparing/target-kind/preparation-result combinations and four
normal/failure/exception/nested activity cases. They check unavailable-before-same
ordering, zero preparation callbacks on gate/fast-path returns, precise activity
restoration even after callback mutation, preparing-target reentry rejection,
unchanged state and no extra COM references.

The [target component tests](../tests/ime/comp/target.cpp) add 35 document-result,
subscription-result, and invalidation-boundary cases plus two exception/resource
unwind cases. They check exact call order, predicted tickets, strong-reference
counts, and move-only stateful callbacks. Eight
[real-service preparation cases](../tests/ime/tsf/service_target.cpp) exercise
document-query and layout-advise reentrancy with continued preparation, focus
loss, target removal, or deactivation. Nested input and activation cannot
interleave, late subscriptions are withdrawn, same-target input avoids a
second preparation, and a fresh target remains usable.

The [presentation components](../tests/ime/comp/presentation.cpp) add 50 cases:
23 Begin outcome/check boundaries, six unavailable-manager paths, six existing-ID
updates, eight host-permission/presentation gates, six End ownership paths, and
one late-manager lookup. Checks include exact IDs, shared stateful predicates,
reference counts and actual popup visibility. Ten
[service presentation cases](../tests/ime/tsf/service_presentation.cpp) combine
five HRESULT/ID outcomes with deactivation or different-manager reactivation
inside Begin, verifying old/new ID isolation and continued new-target input.

The [layout component tests](../tests/ime/comp/layout.cpp) add 35 cases for
notification/active views, HRESULT and null/non-null output combinations,
`S_FALSE`, clipped/zero/inverted rectangles, epoch invalidation and same-epoch
composition replacement. Six lifetime cases verify range ownership through
caller UI work and reference-release order on current, stale and no-layout
results. Ten [service layout cases](../tests/ime/tsf/service_layout.cpp) inject
deactivation or reactivation at view/range/extent boundaries, checking exact
query counts, view routing, replacement-popup isolation and continued input.
These mock regressions do not certify positioning in real hosts.

The [layout-position tests](../tests/ime/comp/layout_position.cpp) add 72
current/result/history/fallback/raw-rectangle combinations, all with clipped
output, checking exact call sequences, move-only callbacks, flag timing and
unchanged query ownership/shape. Nine callback-boundary cases exercise live
flag mutation on successful/failed fallback and movement, nested application,
fallback/movement exceptions, precise range references and final release.
They preserve the original rules rather than introducing a new real-host
positioning fix.

The [edit request tests](../tests/ime/comp/edit.cpp) add 80 cases crossing eight
cleanup states with ten request/callback outcomes. They check exact read/write
and timing flags, client IDs, registry pruning, synchronous/deferred entry,
immutable snapshots, rejection and abandonment. Fourteen
[service write-request cases](../tests/ime/tsf/service_deferred.cpp) cover
immediate, host-deferred and pending-cleanup modes, including `S_FALSE`, request
failure, read-only rejection and asynchronous rejection. A pending cleanup
forces queuing even when the mock host would otherwise execute immediately;
old cleanup completes before new text is written, and subsequent successful
input restores the complete engine snapshot without processing a key twice.
These mock controls do not certify real-host lock or edit-permission behavior.

The [action component tests](../tests/ime/comp/edit_action.cpp) add 32 cases
crossing four actions, committed/preedit text shapes and post-action identity,
16 nested/exceptional callback-boundary cases and four post-commit preedit-read
cases. They check exact call order, zero extra checks, cookie/target forwarding,
borrowed result/text identity and activity restoration by the original edit
scope. The mutable alias used by the read-timing fixture verifies the helper's
read point; it does not change the production task's immutable snapshot policy.
Twelve [service action cases](../tests/ime/tsf/service_action.cpp) cover ordinary
commit, cancel and fifth-code commit-and-restart during continued activation,
deactivation, reactivation or same-epoch replacement. They verify that stale
action completion cannot end replacement UI or overwrite its preedit, while
ordinary fifth-code handoff retains the original UI registration and accepts
subsequent input.

Presentation synchronization adds 17
[component cases](../tests/ime/comp/presentation.cpp): twelve null/non-null
composition, empty/non-empty preedit and invalidation combinations plus five
exception boundaries. Checks cover exact order, zero additional checks,
borrowed snapshot/text identity and composition reference counts through
publication and exit. Six [service cases](../tests/ime/tsf/service_action.cpp)
inject deactivation, reactivation or same-epoch replacement during update or
positioning. The old synchronization cannot publish additional candidate work
or overwrite replacement text; fresh input continues normally.

The layout session extraction adds another 46 component cases: four
snapshot/copy/move/resource cases, 22 identity/activity/nesting/exception cases
and 20 request-result/timing/read-only/abandonment cases. Fourteen additional
service cases cover twelve deferred current/stale/replacement/abandonment
combinations and two retained-service execution/abandonment paths after all
external references are released. They verify notification-view lifetime,
query counts, current-composition selection, input recovery and final COM
object-count restoration. Existing query tests remain unchanged.

`QiQiCompositionLifecycleTest` runs composition components and the complete
service lifecycle/key-flow tests without the standalone popup/candidate/status
UI suites. It uses `RUN_SERIAL` and retains the same native TSF, cleanup, and
reentrancy assertions. It supplements, rather than replaces, `QiQiTsfUiTest`;
a focused pass must not be reported as a full GUI-suite pass.

[`service_lifecycle.cpp`](../tests/ime/tsf/service_lifecycle.cpp) retains the ordered entry point for the deferred, document/context, window, reentrancy, cleanup, and native TSF suites under [`tests/ime/tsf`](../tests/ime/tsf); independent composition component tests mirror the production module under [`tests/ime/comp`](../tests/ime/comp). Together with [`service.cpp`](../tests/ime/tsf/service.cpp), they cover deferred mock scheduling, failure injection, reentrancy, host termination, reactivation isolation, and COM reference release. The [real Windows TSF private-document tests](../tests/ime/tsf/service_native.cpp) trigger synchronous rejection inside a layout notification, then use a bounded message pump to verify asynchronous completion. They check preserved text, composition enumeration, both property types, and isolation of the new composition, without changing user focus or activating the system TIP.

An owned debug child process captures actual `OutputDebugString` messages. Assertions cover ID correlation, synchronous callback entry before request return, asynchronous queue acceptance before execution, forced asynchronous writes, skipping after early host termination, request rejection, no retry for read-only access, queued-request abandonment, accurate callback-exit reporting despite API failures, stale application requests, and Info/Off filtering. Synthetic text includes a marker that must never appear in output. These tests add no production sink, use no `Sleep`, and do not replace debugger-free ETW integration tests against the real DLL.

Task 45 adds scoped document mocks with a weak context-to-parent link, preserving ownership without a mock document/context cycle. Tests verify focus changes, unchanged/modal targets, unrelated Pop/uninitialization, disconnection before notification, query failure, query reentrancy, old layout/write callbacks, A-to-B-to-A asynchronous cleanup ordering, removal of a context whose cleanup is already queued, and COM object-count recovery. Existing tests that passed a fabricated `ITfDocumentMgr*` are replaced with actual mock document objects.

Real private TSF documents also forward actual Push/Pop/uninitialization notifications into the service through an owned, scoped event-sink connection. Assertions cover preservation of the main composition across modal Push/Pop, old host cleanup on a non-null document switch, fresh composition contents in the second document, and UI termination on full context removal. The test does not call native `SetFocus`, activate the system TIP, or assume a fixed notification order. Captured debug-child logs additionally assert modal-retention and non-current-pop fields.

The post-0.9.19 coverage review adds direct assertions for removal during parent lookup, unrelated removal during binding, layout-sink publication after reentrant invalidation, layout subscription rejection, different document interface pointers sharing canonical `IUnknown` identity, identity-query rejection, `GetBase` failure, and removal inside attribute clearing before `EndComposition`. A dedicated service with an isolated status-bar configuration checks actual window visibility: ordinary removal must not suppress focus-loss hiding, a newer reentrant focus gain must remain visible, and focus loss during document reconciliation must not be overwritten. Configuration, subscriptions, and service ownership are restored with RAII, and COM object counts must recover.

The parent-lookup removal test failed against the pre-review code, which continued binding an already removed target. Retaining the pending context in the preparation guard closes that gap. This is branch/behavior coverage, not a measured coverage percentage or proof of arbitrary host callback orderings. The existing Shift-tap integration case still uses the real 350 ms deadline; its thread keyboard state is reset immediately before the sequence to reduce ambient-state interference without relaxing production timing.

FSM boundary regressions compare reset previews for all seven valid engine states, both width modes, all sixteen modifier combinations, and virtual keys 0 through 255. They also assert preservation of the live raw code, candidates, page, and selection. TSF tests exercise old/new-context predictions in Wubi and temporary English composition, Shift/CapsLock, English/full-width mode retention, inactive/null targets, and reset/binding reentrancy. Preflight alone must leave host text, composition ownership, subscriptions, and edit-request counts unchanged; continued input in the old target and actual input in the new target verify that prediction did not mutate the engine.
See the [development guide](dev.md) for targeted test commands; validate both architectures. Browser/UWP host compatibility is not certified, and successful host cleanup is not guaranteed for read-only, disconnected, or rejected requests.

Task 41 regressions observe actual native destruction while the service and host
UI references remain alive, verify composition/UI-end ordering and owner-thread
execution, and exercise tooltip/capture cleanup, repeated deactivation,
activation rollback, reactivation callbacks, stale queued layout/write/cleanup
requests, UILess mode, and retained UI after final service release. Reentrant
begin/end calls must not publish stale IDs, double-end an ID, or hide newer UI.
Replacement managers deliberately reuse the same numeric UI ID to verify that
old-manager cleanup cannot deregister the replacement manager's element.
Standalone UI regressions cover external native destruction, recreation, and
wrong-thread rejection followed by owner-thread cleanup.

### 9.1 Regression Inventory (Task 46)

The matrix consolidates existing regressions rather than claiming they are all
new. Tests assert actual text, API call counts, composition identities, layout
subscriptions, UI registration, and COM object-count recovery, not just `S_OK`.

| Surface | Covered scenarios | Evidence |
|---|---|---|
| Queued application edits | Update/commit/cancel/layout after deactivation, focus reset, reactivation, and A-to-B-to-A; valid FIFO updates remain ordered | External `MockEditSessionQueue` and document/UI call counters |
| Activation and teardown | Repeated deactivation, activation rollback, retained host/UI references, later reactivation | Existing lifecycle tests and native HWND destruction observations |
| Document/context notifications | Modal retention, unrelated notifications, current Pop/uninitialization, disconnected parent, binding/query reentrancy, subscription rejection | Scoped document mocks and real private TSF documents |
| Independent cleanup | Sync success, controlled async fallback, rejection/read-only/disconnection, early/duplicate host termination, abandoned queues, removal after acceptance | Existing failure matrix, host properties, and debug-child stage assertions |
| UI lifetime and reentrancy | Real window destruction, owner-thread checks, Begin/End reentrancy, replacement manager with reused numeric ID, stale position results | Task 41 regressions |
| In-flight application edits | 84 update/commit/cancel/host-termination boundary/transition combinations, plus two same-context continuations inside old `EndComposition` | 86 cases in `TestReentrantCompositionEdits` |
| Late composition start | Deactivation, same-context reactivation, target switch with sync/async cleanup; removal and host termination | Eight start-result cases with bounded FIFO execution |
| Cleanup property lookup | Sync/async cleanup interrupted by removal or host termination before actual attribute clearing | Four cases; no removed-context edits, exactly one host-cookie clear |
| Same-epoch replacement | Commit and start a replacement inside old range lookup or layout lookup | Two cases; no later old edits or UI updates |
| Keyboard state reentrancy | New Ctrl state injected during target cleanup and compartment lookup | Two key-entry cases; current event keeps its snapshot, next event observes Ctrl |

The added lifecycle matrix has explicit 86/8/4 case-count assertions. Its two
same-epoch replacement cases and two keyboard-state regressions are additional
to those 98 cases. Plain-key fixtures isolate and restore thread keyboard state;
modifier-specific tests retain their intentional state and the real Shift-tap
deadline. This inventory is not a measured coverage percentage.

Task 47 still requires actual VS Code/browser focus and UI validation. Mock and
private-document tests are not proof of arbitrary host callback orderings,
successful cleanup without write access, or certification of the entire
document/window lifecycle.

## 10. Official References

- [ITfContext::RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession): synchronous/asynchronous flags, locks, and two-layer results.
- [ITfComposition::EndComposition](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcomposition-endcomposition): write cookies and composition termination.
- [ITfTextInputProcessor::Deactivate](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftextinputprocessor-deactivate): deactivation and thread-manager reference release.
- [ITfCompositionSink::OnCompositionTerminated](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompositionsink-oncompositionterminated): host termination notifications.
- [ITfContextOwnerServices::OnLayoutChange](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextownerservices-onlayoutchange): the layout notification entry point used by real-document tests.
- [ITfDocumentMgr::Push](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-push): primary/modal contexts, the two-context limit, and notification semantics.
- [ITfDocumentMgr::Pop](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-pop): removal, uninitialization, same-thread requirements, and reentrancy restrictions.
- [ITfThreadMgrEventSink::OnSetFocus](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgreventsink-onsetfocus): focused and previously focused documents.
