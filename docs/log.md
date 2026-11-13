# Logging and Troubleshooting

QiQiIME emits local ETW TraceLogging events and native debugger output.
Use `QiQiIMELogMonitor.exe` for interactive capture, or ETW tools for offline
analysis. The monitor does not import ETL files or retained test logs.
For instrumentation rules, see the [development guide](dev.md#diagnostics).

## GUI monitor

Build the monitor with:

```powershell
cmake --build --preset x64-release --target QiQiIMELogMonitor
.\build\x64\Release\QiQiIMELogMonitor.exe
```

Use the corresponding x86 paths for that architecture. A monitor can receive
events from either x64 or x86 hosts; its own architecture does not identify the
event source's architecture.

1. Select **IME events**, then **Start capture**. Capture starts with ordinary
   permissions. If ETW reports a permission failure, the monitor offers an
   optional administrator restart; cancellation keeps the original window.
2. Reproduce the problem in the application hosting the IME. Capture needs no
   debugger, receives only events emitted while enabled, and does not replay
   earlier activity.
3. Filter by process name, exact PID, ETW level, category, activity, or message.
   Name/PID lists describe buffered sources, not all running applications.
   Use **Filter by selected activity** to follow related operations.
4. Select rows to inspect details or copy with `Ctrl+C` / `Ctrl+Insert`.
   **Pause auto-scroll** does not stop capture or aggregation.
5. **Stop capture** retains existing records. **Clear** clears only the current
   source; it does not stop capture, remove exports, or reset capture loss counts.
6. **Export** saves a filtered snapshot after privacy confirmation. Inspect and
   redact it before sharing. A write failure retains memory records but can leave
   an incomplete target file.

The original monitor stays open if administrator restart fails. After a successful
restart, the new window opens on the
original monitor's screen where possible; logs, filters, and capture state are
not transferred. Export first, then restart capture. Do not elevate the tested
application or change user groups/ETW ACLs merely to obtain diagnostics.

The window supports runtime English/Simplified Chinese selection, system themes,
high contrast, and per-monitor DPI. The log-list/detail splitter is keyboard
accessible with `Tab`, arrow keys, `Home`, and `End`.

### Metrics capture

Stop capture, select **Metrics view**, then start **Logs + Metrics**.
Starting from IME events or Diagnostics selects **Logs** only. Changing views
during capture does not change the active subscription; stop and restart to
change modes. After an administrator restart, select Metrics again if needed.

Metrics have independent instance/metric/outcome/mode filters and cumulative or
recent-60-second windows. Clearing metrics resets statistics, not log records or
capture loss counts; stopping freezes statistics. JSON export contains metrics,
not the hidden log list. See [metrics semantics and export](metrics.md).

### Buffers and monitor diagnostics

| Buffer | Default limit |
| --- | --- |
| Displayed events | 10,000 records / 16 MiB |
| Incoming log queue | 2,000 records / 4 MiB |
| Monitor diagnostic view and queue, each | 512 records / 1 MiB |

Record and byte limits apply independently. They are storage budgets, not process
working-set limits. Inspect ETW event/buffer loss, local eviction, decode errors,
and allocation failures separately.

The monitor's own provider is `QiQiIME.LogMonitor`,
`{6AB897B4-D148-4DF9-8EB7-4790A274951E}`. It reports capture/API failures without
re-emitting collected IME messages. Each window owns a uniquely named session
and stops only that session; a failed stop retains ownership for retry.

## Provider lifecycle

| Item | Value |
| --- | --- |
| IME provider | `QiQiIME.TraceLogging` |
| Provider GUID | `{8E7B9D31-5A42-4C81-9F11-202610290099}` |
| Ordinary payload | `process`, `message` |
| Event metadata | Time, PID/TID, level, keyword, opcode, activity/related activity |
| Ordinary keywords | `Logs = 0x00FF` |
| Typed metrics keyword | `Metrics = 0x0100` |

Initialize the provider explicitly outside `DllMain`. IME instances in one
module share registration; different processes have independent state.
`Deactivate` does not unregister it. Queries and writes do not initialize it
implicitly. Registration failure may prevent ETW output; inspect the
nonrecursive debugger fallback rather than assuming that no events means success.

The DLL runs inside the host, not a separate IME process. Identify the actual
host PID: browsers and Electron applications can have multiple similar processes.
The settings tool is a separate `QiQiIMESettings.exe` process.

## Application log level

An active ETW session requests its own event level. Unless application
`LogLevel` is **Off**, Verbose capture can receive detailed events without
changing the registry or restarting the host.

The DWORD `HKCU\Software\QiQiIME\LogLevel` controls the ordinary debugger threshold
and the Off switch. There is no log-level control in the settings GUI.

| Value | Application level | ETW mapping |
| --- | --- | --- |
| 0 | Trace | Verbose |
| 1 | Debug | Verbose |
| 2 | Info, default | Informational |
| 3 | Warn | Warning |
| 4 | Error | Error |
| 5 | Critical | Error |
| 6 | Off | Ordinary events disabled |

Typed metrics are independent of this setting, including Off, and require their
own keyword subscription. Selecting a display category does not enable capture.

To adjust debugger verbosity, first save a valid configuration in Settings.
Run the following in the tested user's PowerShell, not a different administrator's
HKCU, and keep the window open so the original value can be restored:

```powershell
$configKey = 'HKCU:\Software\QiQiIME'
$config = Get-ItemProperty -Path $configKey -ErrorAction Stop
if (-not $config.ConfigSequence) { throw 'Save a valid configuration in Settings first.' }
$previousLogLevel = $config.PSObject.Properties['LogLevel']
New-ItemProperty -Path $configKey -Name LogLevel -PropertyType DWord -Value 1 -Force -ErrorAction Stop
```

Restart the tested application to read the change. Afterwards, restore the value
in the same shell and restart the application again:

```powershell
if ($null -ne $previousLogLevel) {
    Set-ItemProperty -Path $configKey -Name LogLevel -Value $previousLogLevel.Value -ErrorAction Stop
} else {
    Remove-ItemProperty -Path $configKey -Name LogLevel -ErrorAction Stop
}
```

## Native debugger

Attach Visual Studio to the actual host PID using **Native** code debugging.
Open **Output > Debug**, reproduce the issue, then **Detach All** rather than
terminating the user's application. Use Debug/Trace application verbosity when
needed. Ordinary debugger output requires `IsDebuggerPresent()`; opening DebugView
alone does not satisfy that condition.

## Offline ETW capture

`logman.exe` is included with Windows. Use a shell with ETW control permission;
the tested application should retain its normal privileges. This example uses a
unique session, subscribes only to ordinary QiQiIME events, and limits the circular
ETL file to 64 MB:

```powershell
$traceDirectory = Join-Path $env:TEMP 'QiQiIME-Traces'
New-Item -ItemType Directory -Path $traceDirectory -Force -ErrorAction Stop | Out-Null
$sessionName = 'QiQiIME-' + [guid]::NewGuid().ToString('N')
$traceFile = Join-Path $traceDirectory ($sessionName + '.etl')
$provider = '{8E7B9D31-5A42-4C81-9F11-202610290099}'
logman create trace $sessionName -p $provider 0x00ff 5 -o $traceFile -f bincirc -max 64 -ets
if ($LASTEXITCODE -ne 0) { throw "ETW capture start failed: $LASTEXITCODE" }
Write-Host "Session: $sessionName"
Write-Host "Output: $traceFile"
```

Reproduce the problem and record the time, application/DLL version, PID, and
architecture. Then stop the owned session in the same shell:

```powershell
logman stop $sessionName -ets
if ($LASTEXITCODE -ne 0) { throw "ETW capture stop failed: $LASTEXITCODE" }
Get-ChildItem -LiteralPath $traceDirectory -Filter "$sessionName*.etl"
```

Always stop the session, even if reproduction fails. If the shell was closed,
use the printed full session name; do not stop another user's or tool's session.
`-ets` operates a live session, not a persistent collector schedule.
ETW level `5` means Verbose, not application Critical. Keyword `0x00FF` excludes
metrics; zero/all-keyword masks can include them. Circular capture overwrites old
events, so stop promptly.

### Reading ETL in WPA

Install Windows Performance Analyzer from the Windows ADK Performance Toolkit.
Open the stopped ETL, add **System Activity > Generic Events**, filter Provider
Name to `QiQiIME.TraceLogging`, and inspect payload plus PID/TID/time.
Look at preceding Info/Verbose context as well as Warning/Error events.
These self-describing events need no PDB to decode; source stacks require matching
symbols. ETL is not a plain-text export.

## Diagnosing specific behavior

### Candidate positioning

Correlate `CandidatePosition`, `CandidatePositionPopup`, and `CandidateVisibility`
by PID/TID, service, activation, and context. Check layout subscription/notification,
request versus callback execution, queried rectangles, DPI, fallback, and final
window position.

`visible`, `topmost`, and `has_owner` do not prove that the user can see the window.
`cloaked=-1` denotes a failed DWM query; inspect `cloak_hr`. Respect the host's
UILess decision. A layout notification returning unchanged coordinates is not
proof that the candidate followed scrolling.

### Shift and input mode

Capture All/Verbose around several left/right Shift taps and a language-bar click.
Correlate `ShiftTap(service=..., attempt=...)`, not isolated toggle messages.
`armed`, `accepted`, and `toggle-complete` describe the normal sequence.
`ctrl`, `alt`, `mouse-button`, `other-key-cancelled`, `context-reset-cancelled`,
and `timeout` explain cancellation; the production tap limit is 350 ms.

Hosts may omit test callbacks; actual key-down can arm the same tracker.
If toggling completes but the icon does not change, inspect
`SyncTsfCompartments` HRESULTs and language-bar notifications. API acceptance
does not guarantee a completed system repaint.

### Focus timing

`FocusPerformance(` and `ReloadConfigIfModified(focus_id=` expose integer
nanoseconds. Correlate PID and `id` / `focus_id`; IDs are local to the process.
`reload_ns`, `position_ns`, and `total_ns` measure reload checks, shared status-bar
position synchronization, and total handling. Inspect `config_status`,
`phrase_frequency_status`, `dictionary_reload_calls`, and `completed` separately.

Repeated state does not prove duplicate notifications, and `completed=true`
does not certify every nested operation. These timings include instrumentation;
use the [external benchmark](dev.md#focus-performance-benchmarks) for overhead.

### Composition and activation

Use `TsfLifecycle(`, `TsfCleanup(`, service IDs, cleanup IDs, and activity IDs.
Keep `request_hr` separate from `session_hr`. Queue acceptance, callback exit,
`finished=true`, and local deactivation do not prove that host attributes were
cleared or the composition ended.

`activation-cancel-requested` invalidates an in-flight activation. The outer
activation reports `E_ABORT`; successful late registrations still need teardown.
See the [composition protocol](composition.md#7-log-protocol-and-troubleshooting)
for field meanings, ordering, and reentrancy.

## Test hang diagnostics

`QiQiTsfUiTest` and `QiQiCompositionLifecycleTest` retain phase/function logs
separately from production capture. CTest stores them under
`build\<arch>\tests\ime\test-logs`; direct EXE runs use the current directory's
`test-logs`.

Find the last unmatched `CASE-BEGIN` and inspect phase, assertion, thread, and
modifier-key evidence. A new `CASE-END` resets the watchdog's 60-second inactivity
timer. The child has a 90-second limit; CTest reserves 150 seconds for diagnostics.
Do not override it with `--timeout 90`.

Retain the run's stdout/stderr, dump, archived matching EXE/PDB, debugger commands,
and analysis together. Normal completion produces no dump. The test watchdog is
not a production crash monitor. Probe mode deliberately hangs and fails; it is
not a passing functional test or proof of a historical hang's cause.
Setup and commands are in the [development guide](dev.md#tsf-hang-diagnostics).

## Troubleshooting

- **No events:** check the loaded DLL, actual host PID, registration, subscription,
  Off setting, capture state, and monitor diagnostics. Session existence alone
  does not prove that events arrived.
- **Access denied:** retain the failing API and actual HRESULT/Win32 code.
  `hr=0x80070005` and `error=5` use different error domains.
- **Success-like statuses:** `S_FALSE` and `TF_S_ASYNC` are not failures and do not
  by themselves certify host writes or cleanup.
- **No layout:** `TS_E_NOLAYOUT` can be temporary; inspect subsequent events.
- **Missing details:** inspect capture loss, buffer eviction, filters, start time,
  and whether the correct DLL/module instance was observed.

Do not log entered text, passwords, clipboard contents, or dictionary phrases.
Exports, ETL, paths, and dumps still require review before sharing.

## References

- [logman create trace](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace)
- [TraceLogging capture and display](https://learn.microsoft.com/en-us/windows/win32/tracelogging/tracelogging-record-and-display-tracelogging-events)
- [Windows Performance Analyzer](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-analyzer)
- [Privacy policy](../PRIVACY.md)
