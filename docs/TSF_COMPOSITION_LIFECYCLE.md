# TSF Composition Lifecycle, Deactivation Cleanup, and Diagnostics

This document explains the invariants, ownership, reentrancy, and debugging workflow
for tasks 43, 44, and 45. The implementation lives in
[`tip_service.cpp`](../src/Ime/tsf/tip_service.cpp)、
[`tip_composition.cpp`](../src/Ime/tsf/tip_composition.cpp), and
[`tip_service.h`](../src/Ime/tsf/tip_service.h).
For logging configuration, ETW sessions, and capture tools, see the
[logging guide](LOGGING.md).

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
| `finished` | The edit callback has exited, host termination has handled a task that had not started, or context removal has invalidated cleanup; **this does not mean attribute clearing or composition termination succeeded** |
| `terminated` | The host has reported termination of the old composition; do not call `EndComposition` again |
| `async_requested` | An asynchronous request has been attempted, not necessarily accepted or executed |
| `context_removed` | A Pop/uninitialization notification removed the captured context; skip further edits even if an old callback is later delivered |

The service removes expired or finished weak references when adding or querying tasks. `tracked_cleanups` is only the number of entries in the weak-reference container at that moment. It may include finished or expired entries and is not a pending-request count.

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

### 5.4 Early Host Termination of the Old Composition

`OnCompositionTerminated` distinguishes composition identities:

- **Old composition awaiting cleanup:** set `terminated`. If the callback has not started, clear the old attribute using the notification's write cookie and set `finished`. The later edit callback logs `skip-host-terminated` without clearing attributes or ending the composition again.
- **Old composition whose cleanup is executing:** set `terminated`. The active callback handles the reentrant outcome and, when needed, logs `skip-end-after-host-termination` to avoid duplicate termination.
- **Current composition:** clear its attribute, recheck epochs after reentrancy, then reset the engine, context, and UI.
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
5. **No stage records at Info/Off:** this is normal filtering. Temporarily enable Debug and a receiver as described in the [logging guide](LOGGING.md), then restore configuration after capture.

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

`PrepareInputContext` now runs before processing an actual key-down, and before contextual mode-toggle cancellation:

1. Reject missing/inactive targets and reentrant transitions without consuming the key.
2. When the target differs, reset the old engine and invalidate its epoch **before** interpreting the new key.
3. Reuse the old-composition cleanup path, preserving its preedit text rather than deleting it.
4. Capture the new parent document and attach the layout sink. Recheck epochs after reentrant COM calls, and remove a subscription published after an invalidating notification.
5. Only then process the key and submit its result for the prepared context.

`ApplyEngineResult` no longer switches targets after the fact. UI/configuration callbacks can submit results only for an already prepared current context. Backspace in a fresh context is therefore passed through when its engine has no composition, instead of creating text derived from the previous context.

The shared reset path also fixes a second gap in task 43: context switching and focus loss previously released the local composition reference without explicitly ending the host composition. Connected-target resets now use `ScheduleCompositionCleanup`, the same synchronous attempt and controlled asynchronous recovery used by deactivation. If focus returns to the same context before old cleanup executes, `HasPendingCompositionCleanup` still forces the new write behind it.

### 8.4 Removal Is Not a New Write Opportunity

Pop and document-uninitialization handling use `ResetCompositionAndUI(false)`. They invalidate callbacks, release local context/document/composition references, detach subscriptions, and end candidate UI **without requesting a new edit session**. Removal does not prove that editing access remains available. Any remaining host termination belongs to TSF/the host; local release is not logged as successful `EndComposition`.

Removal also marks matching independent cleanup states with `context_removed` and `finished`. A cleanup request accepted before removal may still be delivered by a scheduler; it logs `skip-removed-context` rather than clearing attributes or ending the old composition. Removal during attribute clearing prevents the subsequent `EndComposition` call. Old host termination notifications must not clear a newly bound composition.

### 8.5 Additional Diagnostic Fields

All records retain the existing service/cleanup correlation scheme:

- `document-target`: `source=focus|push`, `retained`, and `covered_by_modal` explain why the current target was retained or reset.
- `document-uninit`: `related` says whether the cached parent matched.
- `context-push`: `related` distinguishes current-document stack changes from unrelated notifications.
- `context-pop`: `current` identifies removal of the actual input target.
- `context-reset`: `try_host_cleanup=false` identifies the removal path; `cleanup-skipped-removed-context` explicitly reports local release without claiming host termination.
- `input-document-unavailable` preserves the actual parent-query HRESULT. Editing is not assumed to succeed: the subsequent edit-session request still determines access, while no parent relationship is fabricated.

## 9. Validation and Remaining Boundaries

[`tsf_service.cpp`](../tests/Ime/tsf/tsf_service.cpp) covers deferred mock scheduling, failure injection, reentrancy, host termination, reactivation isolation, and COM reference release. Real Windows TSF private-document tests trigger synchronous rejection inside a layout notification, then use a bounded message pump to verify asynchronous completion. They check preserved text, composition enumeration, both property types, and isolation of the new composition, without changing user focus or activating the system TIP.

An owned debug child process captures actual `OutputDebugString` messages. Assertions cover ID correlation, synchronous callback entry before request return, asynchronous queue acceptance before execution, forced asynchronous writes, skipping after early host termination, request rejection, no retry for read-only access, queued-request abandonment, accurate callback-exit reporting despite API failures, stale application requests, and Info/Off filtering. Synthetic text includes a marker that must never appear in output. These tests add no production sink, use no `Sleep`, and do not replace debugger-free ETW integration tests against the real DLL.

Task 45 adds scoped document mocks with a weak context-to-parent link, preserving ownership without a mock document/context cycle. Tests verify focus changes, unchanged/modal targets, unrelated Pop/uninitialization, disconnection before notification, query failure, query reentrancy, old layout/write callbacks, A-to-B-to-A asynchronous cleanup ordering, removal of a context whose cleanup is already queued, and COM object-count recovery. Existing tests that passed a fabricated `ITfDocumentMgr*` are replaced with actual mock document objects.

Real private TSF documents also forward actual Push/Pop/uninitialization notifications into the service through an owned, scoped event-sink connection. Assertions cover preservation of the main composition across modal Push/Pop, old host cleanup on a non-null document switch, fresh composition contents in the second document, and UI termination on full context removal. The test does not call native `SetFocus`, activate the system TIP, or assume a fixed notification order. Captured debug-child logs additionally assert modal-retention and non-current-pop fields.

See the [development guide](DEVELOPMENT.md) for targeted test commands; validate both architectures. Browser/UWP host compatibility is not certified, and successful host cleanup is not guaranteed for read-only, disconnected, or rejected requests.

Window destruction under task 41, the broader regression matrix under task 46, and real browser/UWP host validation under task 47 remain separate work. This document does not claim that the entire document/window lifecycle is complete.

## 10. Official References

- [ITfContext::RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession): synchronous/asynchronous flags, locks, and two-layer results.
- [ITfComposition::EndComposition](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcomposition-endcomposition): write cookies and composition termination.
- [ITfTextInputProcessor::Deactivate](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftextinputprocessor-deactivate): deactivation and thread-manager reference release.
- [ITfCompositionSink::OnCompositionTerminated](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompositionsink-oncompositionterminated): host termination notifications.
- [ITfContextOwnerServices::OnLayoutChange](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextownerservices-onlayoutchange): the layout notification entry point used by real-document tests.
- [ITfDocumentMgr::Push](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-push): primary/modal contexts, the two-context limit, and notification semantics.
- [ITfDocumentMgr::Pop](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-pop): removal, uninitialization, same-thread requirements, and reentrancy restrictions.
- [ITfThreadMgrEventSink::OnSetFocus](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgreventsink-onsetfocus): focused and previously focused documents.
