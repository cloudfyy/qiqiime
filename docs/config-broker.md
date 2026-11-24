# QiQiIME configuration broker plan

## Goal

Make a signed `QiQiIMEConfigBroker.exe` the only runtime authority for reading
and writing QiQiIME user configuration. QiQiIME DLLs and tools must not open
`HKCU\Software\QiQiIME`; they access a cached snapshot obtained from the broker.
The broker alone reads and writes the real desktop-user registry and skin
catalog.

This does not turn every configuration field read into a DCOM call. Input,
candidate layout, painting, and status synchronization only access an
in-process immutable snapshot. DCOM is used for initial load, notification
refresh, and explicit save operations.

## Public contract

The broker exposes one custom `IQiQiConfigBroker` DCOM interface and its
MIDL-generated native asynchronous facade. Clients
exchange a bounded binary request/response envelope through a conformant
`byte*` array plus a separate `ULONG` length. MIDL generates an architecture-specific COM
proxy/stub DLL for x86 and x64; the interface itself is not `IDispatch`.
The unpublished v1 ABI is intentionally not retained. The traced v2 ABI uses
new synchronous and asynchronous interface IIDs, so a stale v1 proxy cannot
be mistaken for a compatible implementation.

Every operation carries a fixed `QIQI_CONFIG_TRACE_CONTEXT`:

- a process-spanning `trace_id`;
- the client operation's `caller_activity_id`;
- its optional in-process `parent_activity_id`;
- reserved flags, which are currently required to be zero.

The matching `QIQI_CONFIG_TRACE_RESULT` returns the Broker operation
`broker_activity_id`, the last committed trace/activity IDs, the revision at
that commit, and the current snapshot revision. Async calls receive the
context in `Begin_*` and return the result from `Finish_*`.

Operations:

| Operation | Purpose |
| --- | --- |
| `Read` | Return the latest validated configuration, skin catalog, and revision |
| `Save` | Atomically persist a complete configuration for an expected revision |
| `Reset` | Restore safe defaults after an explicit user request |
| `PublishSyncDirectory` | Publish only the validated user-dictionary synchronization directory |

The wire envelope carries:

- format magic and schema version;
- request/response operation;
- monotonically increasing 64-bit revision;
- expected revision for writes;
- bounded UTF-16 strings;
- complete scalar `QiQiConfig`;
- active skin catalog;
- checksum.

Malformed, unsupported, oversized, or truncated data is rejected with a real
HRESULT. Save uses optimistic concurrency: if the expected revision is stale,
the broker returns `ERROR_REVISION_MISMATCH` without writing anything.

## Ownership

### Broker-only state

Only the broker is allowed to access:

- `HKCU\Software\QiQiIME`;
- the skin catalog under `Software\QiQiIME\Skins`;
- `UserDictSyncDirectory`;
- the configuration sequence.
- `ConfigCommitTraceId`, `ConfigCommitActivityId`, and
  `ConfigCommitRevision`.

Installer uninstall/cleanup remains an administrative exception because it
removes user data only after explicit user confirmation.

### Client state

Each process keeps the last valid decoded `ConfigSnapshot` in a synchronized
cache. Consumers copy values from that cache:

- key handling;
- candidate rendering;
- status bars;
- theme and font resolution;
- dictionary loading;
- settings previews and dialogs.

The cache is invalidated only by the global configuration compartment or a
successful local save.

## IPC and notification flow

Read:

1. A process loads once from the broker and decodes one complete snapshot.
2. The snapshot is copied into the local cache.
3. Hot paths read only the cache.

Save:

1. The client sends the expected revision and complete validated snapshot.
2. The broker validates and saves it in a KTM registry transaction.
3. The broker publishes dictionary/channel addresses if needed.
4. The broker increments the durable configuration sequence and atomically
   records `ConfigCommitTraceId`, `ConfigCommitActivityId`, and
   `ConfigCommitRevision` in the same transaction.
5. A stale CAS does not update the sequence or commit trace metadata.
6. The broker increments the global TSF `VT_I4` compartment after commit.
7. A TSF notification failure is reported in the snapshot but does not roll
   back the durable configuration or its trace metadata.
8. Other processes receive the event and refresh once.
9. A client updates its own cache only after the broker returns the committed
   revision.

An unchanged synchronization-directory publication keeps the same revision
and does not emit another TSF notification.

`VT_I4` is notification only. The revision token may wrap; readers compare the
full broker revision and tolerate notification-token rollover.

DCOM connection points and callback sinks were considered. They add a reverse
activation path from the broker into UWP/LPAC clients and are therefore more
dependent on package COM ACLs, marshaling availability, and client liveness.
The TSF global compartment is already available to ordinary UWP and full-trust
hosts, so it remains the non-authoritative wake-up signal. The complete revision
and content are always read from the broker after that signal.

The public client API is asynchronous for notification-driven refreshes.
After a compartment event, one process worker creates an `AsyncIQiQiConfigBroker`
call through `ICallFactory`, waits with `ISynchronize`, and finishes the native
asynchronous call. It then publishes the decoded result to the synchronized
cache or registered UI thread's message-only window. This keeps TSF and input
threads away from DCOM. Explicit UI saves may use the synchronous interface.
Clients already initialized as STA reuse that apartment; the client helper owns
and uninitializes COM only when it initialized the thread itself.

## Performance rules

The following paths must make zero DCOM calls:

- `OnKeyDown`, `OnKeyUp`, and candidate selection;
- composition creation, candidate layout, and painting;
- focus enter/leave;
- each timer tick;
- reading individual UI fields.

Broker calls are limited to:

- first access in a process;
- one refresh after a committed configuration notification;
- explicit Save, Reset, or backup restore;
- opening a Settings/Dictionary Manager view whose cache is not current.

Event bursts are coalesced, with a pending refresh retained if a newer event
arrives while the current read is in flight. A failed refresh retains the last valid snapshot
and is retried with a bounded delay. Input is never blocked waiting for a
broker retry.

## DCOM security

- Local activation only; remote activation rights are not granted.
- Broker process is the signed installed executable and runs as the
  interactive non-AppContainer user, never as SYSTEM.
- Launch and access ACLs grant:
  - the interactive user session;
  - `S-1-15-2-1`;
  - `S-1-15-2-2` where applicable;
  - the `lpacCom` capability SID.
- Clients use packet privacy and identity impersonation.
- The broker verifies that the caller token belongs to its user SID and
  desktop session before each mutation.
- No method accepts arbitrary registry paths, arbitrary file paths, COM
  commands, or ACL changes.
- A process not carrying one of the allowed principals receives
  `E_ACCESSDENIED`.

The broker cannot prove that a call originated in `qiqiime.dll` rather than a
different component of the host process. The security boundary is therefore
the calling user/package/security principal, not an individual in-process DLL.
The API's limited field-level behavior is part of the trust boundary.

## Lifetime and registration

- The executable is a local COM server with a dedicated CLSID/AppID.
- `-Embedding` runs the class factory; normal invocation performs no UI work.
- The broker uses its own `QiQiIME.ConfigBroker.TraceLogging` provider GUID
  `{8E7B9D31-5A42-4C81-9F11-202610290011}`; it does not initialize or reuse
  the in-process `QiQiIME.TraceLogging` provider.
- Broker diagnostics cover command parsing, registration/unregistration,
  singleton acquisition, COM security/class registration, idle or requested
  shutdown, class-object revocation, and the final HRESULT. Diagnostics never
  include raw command lines, registry keys, or user configuration values.
- If TraceLogging registration fails, the Broker keeps logging to
  `OutputDebugString` and, when explicitly enabled, its diagnostic file instead
  of silently dropping startup failures.
- Configuration RPC Start/Stop events use ETW `ActivityId`,
  `RelatedActivityId`, and a structured `trace_id` field. A Broker operation
  activity is related to the client operation activity, and client cache
  refresh events can relate to the durable commit activity even when the
  subscriber runs in another process.
- Log Monitor decodes `trace_id`, groups the display by trace activity
  ancestry, supports trace/activity filtering, and includes all three IDs in
  copied and exported logs.
- MIDL generates one registered proxy/stub DLL per architecture from the
  `IQiQiConfigBroker` definition. Clients never query `IDispatch`.
- `QiQiIMEConfigProxy.dll` is not a second server. It is an in-process
  architecture-specific marshalling library loaded by the calling process;
  only `QiQiIMEConfigBroker.exe` owns configuration state.
- The broker uses per-user identity and idles out after a bounded inactivity
  interval once no objects or server locks remain.
- A per-session named singleton prevents competing broker processes. MSI
  shutdown signals the broker and waits for that singleton object to disappear
  before continuing file replacement or unregistering COM.
- Installation registers one native server path and replaces the AppID DACLs.
- Uninstall removes the registration and broker file through the normal MSI
  flow.

## Implementation phases

### Phase 1: codec and storage

1. Move QiQi registry store operations into a broker-only static library.
2. Add a versioned wire codec with malformed-input and limit tests.
3. Add a broker request handler that performs read/save/reset under a single
   synchronization boundary.
4. Preserve existing registry transaction, ACL, skin-catalog, and error
   behavior.

### Phase 2: broker process and security

1. Add the executable, custom-interface class factory, idle lifetime, and
   registration commands.
2. Add COM security ACL tests and caller identity/session checks.
3. Extend the UWP, Full Trust, and LPAC fixtures to activate the broker and run
   read/save/no-write-back regressions.

### Phase 3: client cache and reads

1. Add the DCOM client and process-wide immutable snapshot cache.
2. Route `LoadConfig`, skin lookup, and config revision queries through the
   broker.
3. Remove broker RPCs from focus paths.
4. Keep defaults and last valid cache on broker failure.

### Phase 4: all writers

1. Route Settings OK, Quick Settings, dictionary selection, backup restore,
   skin initialization/publishing, and sync-directory publication through the
   broker.
2. Return conflicts and save failures without touching package-private HKCU.
3. Make the broker publish the global compartment after commit.

### Phase 5: integration and packaging

1. Update installer registration and staged files.
2. Add x64/x86 client tests and x64/x86 server activation tests.
3. Run Common, Settings, DictManager, Service, UWP, Full Trust, and LPAC
   regressions.
4. Verify source scans find QiQi user-registry access only in broker/store and
   approved uninstall cleanup.
5. Build MSI only if explicitly requested.

## Test matrix

Implemented local tests cover:

- every `QiQiConfig` scalar round-trip;
- enum/range/string-limit rejection;
- malformed binary, unknown version, trailing bytes, checksum failure;
- revision rollover and stale save;
- repeated identical saves;
- transaction failure and short write;
- skin catalog integrity;
- registry shadow independence.

Process tests cover:

Implemented process tests cover:

- Broker registration, direct-launch rejection, duplicate-instance shutdown,
  and idle/restart behavior;
- synchronous and native asynchronous DCOM;
- STA synchronous client use;
- concurrent writers and stale expected revisions;
- x86 callers through the architecture-specific proxy;
- sync and native-async operation trace propagation, CAS preservation, and
  restart-persistent commit trace metadata;
- Broker diagnostic Start/Stop pairing without configuration payload leakage;
- Log Monitor trace-ID filtering, correlation display, and export;
- proxy static-runtime loading;
- Settings, Dictionary Manager, Log Monitor, and TSF regressions.

Remaining real-host acceptance tests:

- broker outage before and after commit;
- ordinary UWP read/write;
- packaged Full Trust read/write;
- LPAC `zero` denial and `lpacCom` success;
- wrong user/session denial;
- duplicate/lost/late notification under a real host.

The performance acceptance check records broker call sites and asserts that
key handling, focus handling, candidate layout, and candidate painting issue
zero broker calls.
