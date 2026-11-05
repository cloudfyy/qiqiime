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

Each service maintains:

| State | Purpose |
|---|---|
| `is_active_` | Whether application callbacks may currently execute |
| `edit_session_epoch_.activation` | Incremented on valid activation attempts and deactivation, so reactivating the same service cannot revive old requests |
| `edit_session_epoch_.context` | Incremented before context reset, so an A-to-B-to-A switch cannot revive old requests |
| `active_context_` | The current input target; pointer equality does not replace epoch checks |

`ApplyEngineResult` and layout callbacks capture the epoch and target. Before executing, `IsEditSessionCurrent` checks activation state, both epoch counters, and the context. Expected stale-request rejection returns `S_OK` and logs `stale-edit-skipped`; it is not an API failure.

`ResetContextAndUI` invalidates the old context **before** removing COM subscriptions, releasing the composition, or clearing UI. After switching targets, the caller checks for reentrant invalidation again before submitting a new request. An epoch is not a per-key sequence number: normal writes within one epoch still execute in FIFO order, rather than retaining only the latest write.

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

The service retains the original composition strongly and supplies the state
detachment callback. The helper holds the acquired range through `EndComposition`
and performs no additional checks or state changes after detachment/termination.
This preserves a new composition and caret state created by a reentrant end call,
including when ending the old composition fails. The separate
[`InsertCommittedTextInSession`](../src/ime/comp/finish.h) handles direct commits:
empty text does nothing; nonempty text queries the insertion interface, checks
identity, and inserts without creating a composition, clearing attributes, or
adjusting selection. Neither helper stores an edit cookie or schedules work.

For the current composition's host-termination notification, the service still
handles tracked/unrelated/null notifications and detaches the old composition
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
resources and the starting record through the service's original branch scope.
The record releases before the preparation interfaces/range, just as the original
locals did. Unready preparation returns without registering or starting.

Preparation and the post-start check use the same predicate instance, including
stateful, noncopyable predicates. Epoch/activity references remain live across
synchronous host reentrancy; neither those references, the predicate, nor the edit
cookie are retained in the attempt. The service still rechecks identity before a
termination-triggered engine/UI reset and alone moves an accepted composition
into shared state. Extracting the workflow does not add a cleanup policy for a
current failed start that happens to return a composition.

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
