# Development Guide

This guide covers building, testing, diagnostics, and release preparation.
For installing released binaries, see the [installation guide](install.md).
Release-specific changes and validation results belong in the
[release notes](release-notes.md), not in this workflow guide.

## Prerequisites

Use a Windows x64 development machine to build both architectures.

| Tool | Requirement |
| --- | --- |
| Visual Studio | The presets use VS 2026 with Desktop development with C++, x64/x86 MSVC tools, and CppCoreCheck. Other generators require separate compatibility validation. |
| Windows SDK | Headers and libraries for TSF, Direct2D, and DirectWrite; SDK 10.0.22621.0 or newer. |
| SDK Debugging Tools | Both `Debuggers\x64\cdb.exe` and `Debuggers\x86\cdb.exe`. CMake requires the debugger matching each test architecture. SDK headers alone are not sufficient. |
| CMake | 4.4.4 or newer. |
| vcpkg | The bundled Visual Studio installation or a checkout selected by `VCPKG_ROOT`. Manifest mode provides Microsoft GSL. |
| PowerShell | Windows PowerShell 5.1 or PowerShell 7+. Cloud lint installs PSScriptAnalyzer on the runner; local lint is not part of this repository's workflow. |
| JavaScript tests | Node.js 22+ for the CI report and website tests; npm, Chromium and the locked Playwright dependencies in `tests/site` for browser checks. The production website needs no dependency installation or compilation. |
| Packaging only | .NET SDK 8.0+ and WiX CLI v4/v7+. WiX 7 requires accepting its EULA. |

The project requests C++26 draft mode through `/std:c++latest`; this is not a
claim of complete C++26 support. Release binaries statically link the MSVC runtime.

## Repository layout

| Location | Responsibility |
| --- | --- |
| [src/common](../src/common) | Configuration, files, RAII wrappers, logging, metrics, and shared UI helpers. |
| [src/ime/engine](../src/ime/engine), [src/ime/fsm](../src/ime/fsm) | Dictionaries, input state, and key dispatch. |
| [src/ime/comp](../src/ime/comp) | Composition state, edit/cleanup protocols, presentation, and layout. |
| [src/ime/svc](../src/ime/svc) | COM service, key coordination, context access, and lifecycle adapters. |
| [src/ime/tsf](../src/ime/tsf), [src/ime/ui](../src/ime/ui) | TSF registration/helpers and native windows. |
| [src/tools](../src/tools) | Standalone settings, dictionary manager, and log monitor. |
| [tests](../tests) | Tests mirroring production modules, plus installer and script checks. |
| [site](../site), [tests/site](../tests/site) | Static bilingual website, Node checks, and Chromium browser regressions. |
| [scripts](../scripts), [installer](../installer) | Build, lint, registration, and release packaging. |
| [data](../data), [licenses](../licenses) | Bundled dictionaries/assets and third-party license texts. |

Each module owns its CMake source list. Keep related test suites in short,
thematic files: common tests use `tests\common\core`, dictionary tests use
`tests\ime\engine\dict`, and TSF service tests use [tests/ime/svc](../tests/ime/svc).
TSF mocks live in `tests\ime\tsf\mock`, with `mocks.h` as the aggregate header.
Share helpers through headers; do not include test `.cpp` files.
Use distinct test-entry basenames when a target also compiles the production entry
point (for example `settings_main.cpp` versus `main.cpp`), avoiding collisions in
MSVC module-dependency scan output.

## Build and test

The vcpkg manifest includes Microsoft GSL and the header-only `nlohmann-json`
parser. Configuration transfer reuses `kConfigFields`, atomic file writes,
and registry transactions; JSON is an exchange format, not a replacement
configuration store. Keep transferred scalar fields separate from local paths.
Duplicate JSON keys (including escaped spellings) must be rejected before a
parsed object can overwrite them. Tests use isolated registry keys and files;
the Settings import preview stages edits and never saves on its own.

From the repository root, build and test both native architectures sequentially:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
```

The default configuration is Release. Use `-Config Debug` for Debug.
The command does not create installers unless `-Package` is supplied.
It does not run the website's Node or Chromium tests; those are separate gates.

### Required validation by change scope

Choose tests from the changed behavior and its dependent callers, not just the
language or file extension. Add focused regressions for behavior introduced or
fixed by the change; keep existing assertions and failure diagnostics intact.

| Change | Required validation |
| --- | --- |
| Native C++ source, headers, or embedded resources | Build affected targets and run their CTest suites on x64 and x86, sequentially. Run full native suites for cross-cutting changes or when targeted failures require wider validation. Review C++ quality checks in cloud Nightly Lint, not locally. |
| Installer authoring, build/deployment/release scripts | Run related installer/script fixture tests; review Error-level PSScriptAnalyzer in cloud Nightly Lint. Build/test both architectures when native actions or runtime payloads change. Use isolated fixtures, not a real installation, registration, uninstallation, or user-data deletion. |
| Nightly workflow or report logic | Run the focused Node report tests and synthetic log fixtures below; validate workflow YAML and verify cloud execution and reminder delivery. Do not run local lint. |
| Website source/assets, website tests, release-data generator, or Website workflow | Run the website JavaScript syntax checks, all three Node test files, and the Chromium suite described below. Keep these checks separate from native CTest results. |
| Markdown-only documentation with no behavior or executable examples changed | Check affected links and consistency; no native rebuild or browser run is required. Validate changed executable examples with the relevant tool when feasible. |
| Mixed changes | Run the union of the applicable checks; a passing native suite does not excuse a failing website suite or vice versa. |

For website changes, check both Chinese and English, desktop and mobile layouts,
keyboard navigation, and the JavaScript-disabled fallback. Add regressions for
the affected interactions, storage/clipboard failures, or download metadata
contracts rather than relying only on HTML text checks. Website presentation,
content, screenshots, and download instructions must remain consistent when a
native feature or release contract changes; if that requires site edits, run
the website gates too.

A requested **whole-repository test or coverage run** must include full x64/x86
native suites, website Node tests, Chromium tests, and existing safe
installer/script fixtures. Report each result separately, including skips,
permission limits, unavailable tools, and tests not run. An unavailable tool or
skipped integration test is not a pass. Do not retry until green without
retaining failed attempts and explaining what was checked or changed.
CI path filters do not replace these local change-scope decisions. Native-only
changes do not require browser tests unless they affect the website contract;
website-only changes do not require rebuilding the IME.

For individual architectures:

```powershell
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure

cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release --output-on-failure
```

The x86 preset selects Win32. Debug build presets are also available; run Debug
tests with `ctest --test-dir build\<arch> -C Debug --output-on-failure`.
Outputs are under `build\<arch>\<Config>`: `qiqiime.dll`,
`QiQiIMESettings.exe`, `QiQiIMEDictManager.exe`, and `QiQiIMELogMonitor.exe`,
plus test executables and matching PDBs.

### Focused tests

Build the affected targets, then select tests by name rather than ordinal:

```powershell
cmake --build --preset x64-release --target qiqiime qiqi_tsf_ui_test
ctest --test-dir build\x64 -C Release -R '^QiQi(TsfUi|CompositionLifecycle)Test$' --output-on-failure
```

Repeat with the x86 preset and build directory. Use
`ctest --test-dir build\x64 -C Release -N` to list the current tests.

| Area | CTest names |
| --- | --- |
| Common, synchronization, engine | `QiQiCommonTest`, `QiQiChangeSyncTest`, `QiQiEngineTest` |
| TSF and composition | `QiQiTsfUiTest`, `QiQiCompositionLifecycleTest` |
| Desktop tools | `QiQiSettingsTest`, `QiQiDictManagerTest`, `QiQiLogMonitorTest`, `QiQiLogMonitorStartupTest`, `QiQiStandaloneToolsTest` |
| Metrics | `QiQiMetricsTest`, `QiQiMonitorMetricsTest` |
| Installer sources and runtime layout | `QiQiInstallerShortcutsTest`, `QiQiRuntimeArchitectureTest` |
| Installer basic/reduced uninstall consent | `QiQiInstallerActionsTest` |

Additional ETW test modes appear in the CTest listing. Global ETW integration
tests can return skip code 77 when capture permissions are unavailable; a skip
is not a pass. Private-session ETW tests do not replace global or real-host tests.

Run the two architectures sequentially. GUI/TSF suites share desktop and
single-instance state; avoid keyboard/mouse interaction during those tests.
Clipboard fixture acquisition shares a bounded access-denied contention helper
with Log Monitor tests. Expected-denial probes still call `ScopedClipboard`
directly without retry; exhausted setup logs the holder PID/TID without content.
ChangeSync concurrent-writer diagnostics retain completed writes, elapsed time,
lock/revision conflict counts and the last HRESULT. The 30-writes-per-child
assertions and original timeout budgets remain unchanged; a passing rerun does
not explain an earlier deadline failure.
ChangeSync assertions are flushed before exception unwinding so cleanup aborts
cannot hide the original check. Stage, fixture and child-process markers include
PIDs; cleanup failures report the directory, original error category/code and
unwinding state, then probe delete access for remaining entries without deleting
them. Probe errors are separate observations, not the original removal error or
proof of which process owns a conflicting handle. Enumeration failures are also
reported. After child teardown, fixture cleanup retries only
`ERROR_SHARING_VIOLATION`, with 50 ms pauses in a 2-second retry window, to tolerate
temporary locks on copied AppContainer reader executables. Each retry and the
recovery/exhaustion result retain the error codes and elapsed time. Other errors
fail immediately; a persistent conflict still reports the remaining entries and
aborts. Child/writer timeouts and the CTest timeout are unchanged. Regressions
cover a real conflicting file handle released after the first failure, a
conflict held through the retry deadline, and a non-sharing error without retry.
The layout fallback/DPI regression injects a thread-local `GUITHREADINFO`
snapshot while retaining real host-window coordinate conversion and candidate
position assertions. It must not create/destroy the desktop caret or assume
`DestroyCaret` removes another thread's caret from a shared input queue.
The override is restored on scope exit; normal layout queries still read Windows.
First-key positioning regressions model a windowless text store returning a
successful old extent until the initial write session ends. Under top/left/right
work areas (including mixed DPI and negative coordinates), a single strict
asynchronous read must move the native popup to the settled extent without
another key or a layout notification. The tests retain stale/settled rectangles,
check actual HWND coordinates, and cover one-shot scheduling, native-caret/UILess
exclusion, focus/deactivation/context/composition replacement, abandonment,
request rejection and a failed requery. This models the captured SearchHost
behavior, not a substitute for rechecking the loaded DLL in that host.
Candidate popup light-dismiss regressions use a scoped, current-process/thread
WinEvent hook to check native IME show/hide/change ordering and HWND/object/child
identifiers. They cover repeated calls, unchanged and hidden geometry, movement,
layout/DPI resizing, externally destroyed windows, destructor teardown,
presentation-disabled UI and production-presenter negotiation with a mocked
UILess host. The hook is removed before its capture state dies.
Synchronous teardown-subclass regressions release the last external UI owner
during explicit HWND destruction and reenter display/show methods during the
object destructor. They verify that callbacks cannot free an active cleanup
scope or resurrect a destructing object, and that COM-object counts return to
their baseline after native window teardown.
Real SearchHost interaction and taskbar relocation still require host testing;
these notifications do not change caret-coordinate or popup-placement policy.
`QiQiCompositionLifecycleTest` omits standalone UI suites and supplements,
rather than replaces, the full `QiQiTsfUiTest`.

Tests must isolate configuration, files, and owned child processes. Do not run
production registration or data-removal commands as a substitute for tests.
Service input-method-switch and code-completion regressions run inside the
existing lifecycle fixture's private registry key and temporary `LOCALAPPDATA`.
Their entry guards reject the production configuration key before loading or
saving settings. Snapshot/restore of real preferences is not isolation:
`QIQI_TEST_CHECK` aborts on failure, so RAII cleanup cannot restore those values.
Focus metrics and benchmark fixtures redirect `HKEY_CURRENT_USER` to a newly
created disposable key before any configuration access, and use private
`LOCALAPPDATA` directories under the test working directory. Process-wide
redirection isolates both the test executable and the loaded IME DLL; a
module-local configuration-key override alone would not isolate that DLL.
`QiQiFocusEnvironmentTest` verifies both modules against synthetic sentinel
configuration, including failure unwinding, without requiring ETW capture or
native TSF activation. Keep these process-wide fixtures serial.
For manual compatibility checks, use the [host test guide](hosts.md).

The Log Monitor test runner also accepts `--menu-layout` (real popup geometry
after 100/150/200% font changes) and `--desktop` (desktop appearance regression).
Normal unit-test and desktop modes use isolated registry configuration, not
the user's saved preferences. Configuration-save failures report the actual
HRESULT, facility, and code; do not infer their cause from `GetLastError()`
after a bool-returning configuration API.
Dictionary Manager display fixtures use GUID-named keys outside
`Software\QiQiIME`, with explicit private-key permissions and checked teardown.
A read-only test descendant under the real configuration tree can block its
uninstall deletion even by an elevated action. Aborting assertions must leave
only independent disposable keys, not production descendants; historical
orphans require separately authorized, narrowly scoped cleanup.
The common configuration regression verifies that registry-only reads neither
create missing keys nor modify existing DACLs. Default initialization belongs
to `LoadConfig`, not the registry-only read helpers.
Backup-restore success regressions print the conflict mode and the existing
structured result (stage, stored HRESULT, operation counts and refresh failure)
before asserting success. Phrase-order restore failures preserve the engine's
channel creation/read/publish HRESULT instead of replacing it with `E_FAIL`;
if channel creation and the existing direct-save fallback both fail, the
original channel-creation error is retained. The boolean promotion API keeps its
existing success/failure behavior. A private `writer.lock` sharing-denial
regression verifies the exact HRESULT, partial-result counts and formatted
message. Restore is not retried as a whole after partially applied changes.
Preserve the failed fixture and logs when an
intermittent failure cannot be reproduced; a later passing run is not a fix.

### TSF hang diagnostics

CTest launches the full and focused TSF suites through
[test_watchdog.ps1](../tests/ime/test_watchdog.ps1):

- Each run retains independent phase and `CASE-BEGIN`/`CASE-END` logs. Under
  CTest they are in `build\<arch>\tests\ime\test-logs`; direct EXE runs use the
  current working directory's `test-logs`. Files are not automatically deleted
  or overwritten.
- A new `CASE-END` resets the progress timer. After 60 seconds without completion,
  the watchdog captures a full dump using the matching-architecture SDK debugger.
- The child test has a 90-second execution limit; CTest reserves 150 seconds for
  execution and diagnostics. Do not override that budget with `--timeout 90`.
- Dumps retain matching Release EXE/PDB copies, debugger commands, stdout/stderr,
  and offline analysis. Never use an unrelated or Debug PDB for a Release dump.
- Direct EXE runs retain test logs but have no watchdog. To test the watchdog,
  use the script's `-Mode probe`, which deliberately hangs and exits unsuccessfully;
  never launch `--test-hang-diagnostics-probe` directly.

Preserve failure evidence before deleting build directories. Dumps can contain
sensitive process memory: keep them local and review them before sharing.
See the [logging guide](log.md#test-hang-diagnostics) for interpretation.

## Code quality and lint

- Follow the C++ Core Guidelines and existing GSL/RAII helpers: `ComPtr`,
  `ScopedHandle`, `gsl::finally`, `gsl::not_null`, and `std::span`.
- Reuse common configuration, file, window, and command-UI helpers rather than
  copying implementations. Preserve ownership and destruction order across
  reentrant host callbacks; see the [composition contracts](composition.md).
- Add focused regressions for changed behavior and failure paths. Do not weaken
  assertions, production timing, or small-screen/DPI checks to make tests pass.
- Preserve SPDX headers and third-party notices. Licensing and dictionary
  provenance are documented in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Do not run lint locally. Push/PR [CI](../.github/workflows/ci.yml) retains
both native builds and unit tests; the expensive analysis runs separately in
[Nightly Lint](../.github/workflows/nightly-lint.yml):

- Daily **03:30 Asia/Shanghai (UTC+8)**: analyze the default branch on Windows
  for **x64 and x86**. The workflow can also be started in GitHub Actions with
  `task: lint`; no installer is built and unit tests are not rerun by this job.
- Each architecture retains configure output, full compiler output, lint
  transcript, findings and a result JSON as downloadable Actions artifacts
  for **14 days**, including failed checks. Setup failures can leave some logs
  absent; the step outcomes and Actions job logs identify the failure.
- Daily **08:30 UTC+8**: post an `@cloudfyy` reminder in
  [Nightly lint reports (#1)](https://github.com/cloudfyy/qiqiime/issues/1).
  The comment links today's run and reports both architectures. Missing,
  unfinished, cancelled or failed checks are never reported as a pass.
  Manual `task: remind` updates the same day's bot comment instead of duplicating
  it. Close the tracking issue to pause comments; reopen it to resume.
- GitHub schedules can be delayed. Notification delivery depends on the user's
  GitHub mention/notification settings, not a local desktop alarm. Public-repo
  schedules may be disabled after 60 days without repository activity; re-enable
  the workflow in Actions if necessary.

[lint.ps1](../scripts/lint.ps1) rebuilds `ALL_BUILD` with CppCoreCheck. Repository
source, test and native installer warnings have a zero baseline, with the existing C26490 and
external-dependency exclusions. Normal compilation also enables `/W4`.

The PowerShell gate checks Error-level PSScriptAnalyzer findings in `scripts`
and `tests`. The nightly job requires a successful PSScriptAnalyzer import;
the script's legacy syntax-only fallback is not accepted as a nightly lint run.
Report logic and synthetic log fixtures can be tested locally without running
a compiler analysis or PSScriptAnalyzer:

```powershell
node --test tests\ci\nightly_lint_report.test.mjs
pwsh -NoProfile -File tests\ci\lint_output.ps1
```

### Coverage across runtimes

Collect coverage when requested or needed to locate meaningful missing tests,
not as a mandatory expensive rerun after every edit. A whole-repository report
must identify the measured production files, architectures, configuration,
revision, test results, and remaining gaps. Publish native executable-line,
JavaScript line/range/function, and PowerShell command metrics separately;
do not combine them into one percentage or claim branch coverage from a
format's placeholder branch rate. List unmeasured files and explain exclusions
instead of counting missing mappings or unimplemented TODO features as covered.

#### Native C++

Use Visual Studio Enterprise's `Microsoft.CodeCoverage.Console` with matching
PDBs and isolated binary copies. Static native instrumentation needs suitable
`/PROFILE /INCREMENTAL:NO` linking; do not change production release flags or
instrument installed binaries. Enable child-process collection when testing tools.

Run coverage tests serially. Include test translation units during collection
when they instantiate production templates; apply the production-source filter
(`src` and native `installer` actions) when reporting.
Optimized native line tables may attribute inlined work to callers. Keep the
boundary-test evidence and disclose unmapped checkpoints rather than claiming
complete helper coverage from a covered caller.
Merge duplicate
static-library lines by source path and line number, and list production files
missing from the report. State the configuration, source revision, and measured
scope. Line coverage is not branch coverage or real-host certification.
Verify that instrumentation restores the original binaries/PDBs, and never
package instrumented outputs.

Allow the native coverage collector to finish even when its child test fails:
retain the test's exit status and stderr, then verify every original binary
and PDB. If collection is interrupted, use the official `uninstrument`
command on only the isolated copies and compare with the saved hashes before
reusing that tree. A later successful rerun does not erase the earlier failure.

#### Website and PowerShell

The site tests can collect local V8 evidence without new packages:

```powershell
node --test --experimental-test-coverage tests\site\site.test.mjs tests\site\release.test.mjs tests\site\appearance.test.mjs
$previousCoverageDirectory = $env:QIQI_SITE_COVERAGE_DIR
$env:QIQI_SITE_COVERAGE_DIR = Join-Path $env:TEMP "qiqi-site-coverage"
Push-Location tests\site
try { npm run test:browser } finally {
    Pop-Location
    $env:QIQI_SITE_COVERAGE_DIR = $previousCoverageDirectory
}
```

The optional browser fixture records Chromium JavaScript ranges per test and
does not enable the debugger in JavaScript-disabled contexts. Navigation can
limit attribution of callbacks from earlier documents; retain behavior
assertions and do not equate V8 ranges with native line or branch coverage.
Pester coverage of safe PowerShell fixtures is command coverage, a separate
metric. Do not execute real registration, installation, uninstallation or
data removal merely to increase coverage.

### Focus performance benchmarks

The [focus benchmark script](../tests/ime/focus_benchmark.ps1) is intentionally
outside regular CTest. After building normal x64 Release outputs, create a
same-source comparison build using the already installed dependencies:

```powershell
cmake -S . -B build\metrics-baseline\x64 -G "Visual Studio 18 2026" -A x64 -DQIQI_METRICS_BENCHMARK_BASELINE=ON -DVCPKG_INSTALLED_DIR="$PWD\build\x64\vcpkg_installed" -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build\metrics-baseline\x64 --config Release --target qiqiime
.\tests\ime\focus_benchmark.ps1 -TestExecutable .\build\x64\Release\qiqi_tsf_ui_test.exe -BaselineDll .\build\metrics-baseline\x64\Release\qiqiime_metrics_baseline.dll -CurrentDll .\build\x64\Release\qiqiime.dll -Output .\focus-benchmark-x64.json
```

For x86, use the corresponding paths and `-A Win32`. Use a new output file for
each run. Never install or distribute the `.metrics-baseline` DLL.
Measurement limits and interpretation belong in the [metrics guide](metrics.md).

## Diagnostics

Production diagnostics use local ETW. Capture and filtering instructions are in the
[logging guide](log.md); data-handling boundaries are in the
[privacy policy](../PRIVACY.md).

- Initialize logging explicitly through `EnsureInitialized` outside `DllMain`.
  `InitLogger` also sets the log level. Registration queries and writes must not
  implicitly initialize the provider.
- Logging is module-scoped. `Deactivate` must not shut down the provider shared
  by other IME instances. Shut it down only after callers have stopped.
- Record the operation and actual failure code. Use `log::CheckHResult` for
  HRESULTs; do not classify `S_FALSE` or `TF_S_ASYNC` as failures.
- Capture `GetLastError` immediately only when the API documents that contract.
  Registry, ACL, and ETW registration APIs use their directly returned error codes.
  Do not invent codes for APIs without a documented last-error contract.
- Distinguish optional probes and expected fallbacks from final failures.
  Never replace an actual error with silent success or a stale error code.
- Use existing keyword, opcode, and explicit activity-ID helpers for correlation;
  do not alter the host thread's implicit activity ID.
- Never log entered text, passwords, clipboard contents, or dictionary phrases.
  Avoid per-key logging and redact private paths before sharing diagnostics.
- Logging failures must use a nonrecursive fallback; diagnostic formatting must
  not throw across COM/Win32 callback boundaries.

## Versions and release packaging

### Shared desktop appearance

Desktop usage and supported surfaces are documented in
[display and easy reading](accessibility.md); implementation decisions are in the
[desktop plan](desktop-accessibility-plan.md).
`QiQiConfig`/`kConfigFields` are the only persistence/merge/backup field mechanism:

| Backup key | Registry DWORD | Values |
|---|---|---|
| `ui_theme` | `UiTheme` | 0 system (default), 1 light, 2 dark, 3 app high contrast |
| `ui_text_scale` | `UiTextScale` | 0=100% (default), 1=125%, 2=150%, 3=200% |
| `easy_reading` | `EasyReading` | 0 off (default), 1 on |

`DisplayPreferences` is a value snapshot. `ResolveDisplayAppearance` resolves
Windows high contrast before the selected theme; its palette supplies D2D/GDI
colors including selection foreground/background. Its `SystemThemeSource` argument
selects only the automatic source: candidate popups, their Settings preview,
standalone tools, the floating status bar and its tooltips use App mode.
IME custom quick menus retain Windows mode. Settings caches a separate preview
palette without changing the tool window's preferences. Candidate system-surface
detection and Windows-mode exceptions are deferred; no host probe is added to
the input or paint paths. Both sources preserve explicit Light/Dark/High contrast choices,
and refresh on `WM_SETTINGCHANGE`/`WM_THEMECHANGED`. Use `ScaleDisplayPx` for
text-scaled dimensions, and ordinary DPI-only scaling for minimum DIP targets.
Do not double-apply DPI or change test overrides to set production preferences.
`FluentD2DThemeContext` owns scaled text resources and brushes via existing RAII.
Theme changes refresh cached colors; font-scale changes also rebuild fonts and
layouts. Do not query the registry in each paint call.

Settings reuses the existing controls and bindings across five categorized tabs.
The native tab control retains its accessible names and arrow-key selection;
its strip and overflow arrows paint with the shared appearance palette.
Ctrl+Tab / Ctrl+Shift+Tab cycle pages. Only active-page controls are visible and
focusable; page scroll offsets and unsaved values survive page/theme/language
changes. Layout measures actual closed combo heights, clips scrolling content
(including owner drawing), follows keyboard focus, and fixes OK/Cancel outside
the page viewport. Labels/fields reflow vertically for narrow or enlarged text.
Combo fields use a fixed, DPI/text-scaled base width and align to the page's
right edge; page size and layout share one row. About, Shortcuts, Restore defaults,
OK and Cancel share a fixed footer on every page; narrow/enlarged layouts use two
footer rows. Treat all five as footer controls for painting and focus scrolling.
Browse and Dictionary Manager sit beside the custom-dictionary radio (below it
when space is insufficient); Browse is enabled only while Custom is selected.
Radio commands explicitly synchronize the checked scheme before updating dependent
controls. Selecting Custom does not open the picker; only Browse does, and OK
still validates the selected file. Extra is visually nested under Jidian Core and
enabled only for that scheme; selecting another dictionary clears its check state.
Restore defaults is the sole reset action and includes display preferences. Disabled
Fluent buttons/toggles use inactive styling as well as native disabled state.
Preview measurement and painting share a horizontal,
wrapping item layout. During scrolling, keep existing window clips until new
positions are installed, disable bitmap copying/redrawing while moving controls,
then invalidate the complete page and children together to avoid stale pixels.
Combo frame drawing also intersects the assigned window region, including
owner-draw/GetDC paths; selection and pointer events invalidate instead of painting
synchronously. Display-preview font changes defer repaint until layout finishes.
Settings static labels are owner-drawn through the parent's viewport clip too,
so scrolled labels cannot paint over the tab strip during text/font updates.
High-contrast disabled Settings buttons/toggles add a localized Unavailable line
and omit the outer border; layout reserves the additional line. Disabled size
items append an inline Unavailable status rather than relying on color alone.
Settings enables `clip_controls_to_dc`: DC render sessions bind only the visible
clip rectangle and translate drawing coordinates without changing layout.
The RAII guard restores the previous transform. This bounds the Direct2D copy
itself; a GDI clip or window region alone is not sufficient. Other Fluent
surfaces retain the default rendering behavior.
Display edits preview locally and remain unsaved until acceptance.
The desktop `easy_reading` field is presented as Comfort mode / 关怀模式.
While enabled, text sizes below 150% are disabled in Settings; loaded legacy
combinations preview at 150% without mutating the bound config before acceptance.
Disabling the mode retains text size. Only the painted switch accepts mouse clicks
for this mode; keyboard activation remains supported. Drawing and hit testing
reuse `FluentToggleBounds`, including the high-contrast fallback.
Preserve baseline/local/external field merging. Candidate measurement, hit testing,
selection and painting share the same rectangles in both D2D and GDI paths.
Do not change engine pagination to accommodate screen space.

The IME runs inside host processes. Use the window-only
`ApplyDisplayThemeToWindow` for these surfaces; do not introduce calls that set a
process-wide preferred app theme and recolor host menus. Host-rendered TSF UI is
outside our appearance control. The independent settings do not yet apply to
all other native tools or dialogs. Add regression cases to the existing common,
settings and TSF/UI runners rather than adding a second UI test framework.

### Version source

[VERSION](../VERSION) is the numeric version source. CMake configuration generates
`build\<arch>\generated\common\version.h` and per-product `version.rc` files
from the shared templates. The IME DLL and all three tools carry the same numeric
version and architecture-specific descriptions.

Display versions add the Git hash captured at configuration, such as
`1.0.7+abcdef0`. Rerun configuration when changing only Git identity; a local
pre-commit build is not a tag build. Keep each binary with its own matching PDB.
Non-Debug builds retain `/Zi` and `/DEBUG:FULL` without disabling optimization.

Only generate release packages when explicitly needed:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
```

The script first builds and tests both architectures, then stages:

- `QiQiIME-v<version>-win-x64-x86.zip`: runtime binaries, dictionaries, deployment
  scripts, and required documentation/licenses.
- `QiQiIME-v<version>-win-x64-x86-symbols.zip`: matching product PDBs, not test PDBs.
- With WiX available, four MSI files:
  `QiQiIME-v<version>-win-<x64|x86>-<zh-CN|en-US>.msi`.

Without WiX, ZIP packaging still runs but MSI generation is skipped with a warning.
When MSI files are generated, [packages.ps1](../tests/installer/packages.ps1)
checks their databases for architecture, upgrade/component identity, payload,
shortcuts, and registration order. Ordinary CTest checks sources without creating
MSI files; it does not certify real installation or upgrade behavior.

The MSI embeds `QiQiIMEInstallerActions.dll`, not a runtime installation file.
Interactive uninstall uses one native data-choice page for full/basic/reduced
entry points, followed by MSI progress and a post-commit result. Configuration
and managed dictionary data are independent default-retained choices.
The native choice dialog uses the visible MSI dialog for the matching product
as its owner when present, preserving modal disable/restore and owned-window
stacking instead of forcing a permanently topmost window. Completion messages
format HRESULTs in hexadecimal and include the first failing cleanup operation.
The protected result stores this operation separately from the original HRESULT;
older jobs without that field remain readable and report an unknown operation.
Tests exercise the production ACL validator with private security descriptors,
not only the sandbox path that deliberately bypasses machine ownership checks.
The elevated commit backend validates the initiating `UserSID` and its registered
profile instead of using SYSTEM's or the UAC administrator's Local AppData.
It isolates mapped old caches before scheduling restart deletion and stores
product-specific operational state separately from removable preferences.
The install gate runs before related-product discovery or file/registry changes,
and does not confuse unrelated pending-reboot flags with QiQi cleanup.
The cleanup commit action logs its actual failure without aborting program removal;
the post-`InstallFinalize` action reports incomplete cleanup and fails the final
result without rolling back already committed removal. Never treat MSI rollback
as restoration of deleted user data.

`QiQiInstallerActionsTest` uses an isolated, unregistered MSI fixture and private
filesystem/registry roots. It tests option compatibility, serialization and
UI/upgrade guards, automates the two-choice dialog in Chinese/English, and uses
injected reboot scheduling/boot identity for cleanup and install-gate regressions.
It also loads the real action DLL and calls its UI-only export with the restricted
fixture handle. `MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE` deliberately forbids
executing DLL actions via `MsiDoAction`; this is not bypassed or counted as a
successful real MSI execution. Settings/full/basic/reduced entry points, elevated
completion and actual reboot deletion still require a dedicated installation host.
Tests must never invoke real HKLM cleanup, reboot scheduling or installed-product
actions. The legacy Settings EXE cleanup remains the separate ZIP path.
Both paths share the managed data/synchronization catalog in
[dictfiles.h](../src/common/dictfiles.h), but retain their different
execution privileges and safety policies. Installer MSI records use the SDK's
`PMSIHANDLE` RAII; the nonthrowing failure-reporting boundary uses `gsl::finally`
because the SDK wrapper does not declare its conversions nonthrowing.
Packaging stages the native action DLL and retains its matching PDB alongside
the other symbols. These tests do not certify the actual Windows Settings
uninstall entry point; validate that flow with the newly installed package.

Packaging replaces same-version output paths under `dist`. Preserve any previous
artifacts you need. Do not include test logs, dumps, or instrumented binaries.
For documentation/license payload changes, keep staging, WiX, and installer tests
consistent.

Update the current README references and release notes when changing versions.
The CI & Lint workflow skips push/PR changes limited to Markdown documentation,
the `docs/` directory, or the website's separately tested files. Mixed changes
that include code, build scripts, workflow configuration, or `VERSION` still
run the code checks. Manual CI runs and release-tag validation remain available.
The [Release workflow](../.github/workflows/release.yml) builds, tests, and packages
tagged commits before uploading assets. Pushing a tag is not proof of publication
or code signing; check the workflow and [signing policy](../CODE_SIGNING.md).
Release pages contain download links followed by only the matching version's
section from the release notes. The complete history remains in the repository.
[release_notes.ps1](../scripts/release_notes.ps1) generates this body from the
version, tag, and actual package filenames; missing or duplicate version sections
fail publication rather than including unrelated history.
Both download lists put all MSI installers before ZIP archives, sorting filenames
within each group and preserving the exact attachment names and tagged URLs.
This controls the body links, not GitHub's separate Assets list ordering.
Write the default Chinese notes first and add their translation under
`## English` within the same version section. Release pages show Chinese
downloads and notes by default, with English downloads and notes inside a
collapsed `English` section. Missing or empty translations fail generation.
Both languages include links to the project's standard GitHub Pages overview and
installation page, with `?lang=en` for English. These are live introductory pages;
the existing tag-specific installation guide and history links remain available
for version-specific instructions.

## Project website (GitHub Pages)

The lightweight [website](../site/index.html) has four content pages: overview,
installation, development, and feedback, plus a custom [404 page](../site/404.html).
It has no package dependencies or frontend
compilation step, and does not change the IME's build or release packaging.
Downloads remain on GitHub Releases.
`site/package.json` only declares ES-module syntax for shared JavaScript; the
website itself requires no `npm install`. Browser-test-only dependencies and their
lockfile live separately in `tests/site/` and are never uploaded with `site/`.

Before deploying, the Website workflow reads GitHub's latest stable release and
runs [site_release.mjs](../scripts/site_release.mjs) to generate ignored
`site/release.json`. The installation page shows the actual tag, publication date,
and x64/x86 Chinese/English MSI links from that metadata, not from `VERSION` or
guessed filenames. All four installers must be fully uploaded and match the
release's repository, tag, and version; missing or invalid data fails deployment
instead of publishing broken buttons. The browser independently validates the
metadata. Fetch/validation failures are visible and keep quick-download buttons
hidden; the ordinary official Release links remain usable, including without
JavaScript. Visitors fetch only the static metadata, not the GitHub API.

Content is Chinese by default. Put English translations on leaf elements using
`data-en` and on accessible labels using `data-en-label`. The shared script selects
English with `?lang=en` and carries it through internal `data-site-link` navigation.
All pages load `site.js` as an ES module; the download module imports its shared
language state and locale, so initialization runs once without ordering-dependent
globals. Keep shared page interactions there rather than adding inline scripts.
It does not store a language preference or collect analytics. Without JavaScript,
the Chinese content and navigation remain usable. Keep ordinary page URLs relative
so the site works under `/qiqiime/`. The 404 page is the exception: its assets and
recovery links must start with `/qiqiime/` because GitHub serves it at the missing
URL, including nested paths. It has no active navigation item, canonical URL, or
automatic redirect, and uses `noindex, follow`.
Detailed technical documentation stays in the
repository rather than being duplicated on the site.

The shared [display module](../site/assets/appearance.js) adds the same bilingual
display controls to all five pages; do not duplicate them in individual HTML files.
The original light style remains the default. Visitors can choose dark, high
contrast, or system-following colors and 100/125/150/200% text. Easy reading
initially raises smaller text to 150%, enlarges primary targets to at least 44 CSS
pixels, uses single-column layouts, and hides only decorative elements, not
functional content. Text size remains independently adjustable afterward.
The browser's forced-color mode and reduced-motion preference remain authoritative.
Only display preferences are saved in local storage (`qiqi-display-v1`), never
sent to the project. Storage/validation failures show a visible message and keep
the current page usable; the reset button removes only that preference key.
Default Chinese HTML/navigation still work without JavaScript; display controls
require it. Desktop appearance settings are separate and not changed by the site.

Each content page has an absolute production canonical URL, Open Graph summaries,
and a summary-card hint. Canonical and `og:url` values omit language queries;
the homepage canonical is the project root, not `index.html`.
Use `data-en-content` for English metadata, alongside the existing localized title.
English Open Graph titles and descriptions reuse the translated document title
and description instead of carrying duplicate translations. Keep their default
Chinese values in HTML for crawlers and visitors without JavaScript.
English requires JavaScript: crawlers and share previews that do not execute it see
the default Chinese metadata even for `?lang=en`. Do not advertise separate
`hreflang` pages until independently renderable translations exist.
Keep [sitemap.xml](../site/sitemap.xml) in sync with canonical content URLs;
exclude the 404 page and query variants. Each content page links to this sitemap,
which can also be submitted to a search engine's webmaster tools.
No project-level `robots.txt` is provided: crawlers read `/robots.txt` at the
origin root, not `/qiqiime/robots.txt`, and this project deployment does not own
that root.

The overview's software screenshots live in `site/assets/images/`: `candidate.png`,
`settings.png`, and `dict-manager.png`. They are actual application captures, not
the hero's separately labeled illustration. Only unused space below the candidate
window is cropped. Keep screenshots free of personal paths, private entries,
desktop content, and text/EXIF metadata. Use neutral examples, preserve the
displayed UI, and update image dimensions and bilingual captions/accessible link
labels when replacing them. Each image links to its full-size asset without
requiring JavaScript.

The developer page introduces Log Monitor with `log-monitor-zh.png`,
`log-monitor-en.png`, and `log-monitor-permission.png`. Keep its capture and
optional administrator-restart instructions aligned with [the logging guide](log.md):
start with ordinary permissions, elevate only after an ETW permission failure,
and restart capture explicitly in the new window. Use empty or sanitized
diagnostic screenshots; never publish unreviewed captured events.

To check or preview locally with Node.js 22+ and Python 3 available:

```powershell
node --check site\assets\site.js
node --check site\assets\appearance.js
node --check site\assets\downloads.js
node --check site\assets\release-data.js
node --check scripts\site_release.mjs
node --test tests\site\site.test.mjs tests\site\release.test.mjs tests\site\appearance.test.mjs
gh api repos/cloudfyy/qiqiime/releases/latest | Set-Content -Encoding utf8 "$env:TEMP\qiqi-latest-release.json"
node scripts\site_release.mjs "$env:TEMP\qiqi-latest-release.json" site\release.json
Remove-Item -LiteralPath "$env:TEMP\qiqi-latest-release.json"
python -m http.server 8000 --bind 127.0.0.1 --directory site
```

Open `http://127.0.0.1:8000/` for the preview; stop the server with Ctrl+C.
This simple server does not emulate GitHub's 404 fallback or `/qiqiime/` mount.
When changing the 404 page, also test a nested missing URL under that mount,
confirm the HTTP status stays 404, and check its assets, both languages, and
recovery links. The actual Pages fallback must be checked after deployment.
Check both languages, mobile navigation, and command-copy success/failure in a
browser when changing site behavior. Also check all four direct MSI links and the
visible failure state when `release.json` is missing or invalid. Generated release
metadata is not committed; regenerate it to preview current downloads locally.

For the automated Chromium checks, use Node.js 22+ and npm from the repository
root. Set up the locked test dependencies and browser on first use, after a
lockfile change, or when the chosen test command reports a missing dependency:

```powershell
npm ci --prefix tests\site
Push-Location tests\site
try { npx playwright install --only-shell chromium } finally { Pop-Location }
```

For subsequent runs, reuse the installed dependencies and browser:

```powershell
npm --prefix tests\site run test:browser
```

Playwright owns the loopback-only test server at `http://127.0.0.1:4173/qiqiime/`,
including nested custom 404 responses and JavaScript MIME types. Do not start it
manually for the test run; an occupied port fails instead of reusing an unrelated
server. It emulates Pages routing, not the actual deployed GitHub service.
Desktop (1440px) and mobile (320px) checks cover both languages, metadata,
navigation, screenshot loading, horizontal overflow, installer URLs and keyboard
order, failed/malformed/invalid/aborted downloads, clipboard success/failure, and
404 keyboard recovery. A JavaScript-disabled check preserves the Chinese fallback.
Release fixtures reuse the static tests' data and the production metadata generator;
tests do not query GitHub or download installers, and block external requests.
Clipboard tests use a stub and never change the real clipboard.
Display checks measure text contrast (at least 7:1 in high-contrast mode), 44px
primary targets in easy reading, and reflow at every supported text size on all
pages. They also cover preference persistence/reset, storage failures, system
color changes, forced colors, keyboard focus, and reduced motion.

The [Website workflow](../.github/workflows/pages.yml) checks site changes on pull
requests and uploads only `site` for deployment after changes reach `main`.
It runs the Chromium suite before generating production metadata or uploading the
website; failed browser checks block deployment. On failure it retains traces and
screenshots from `tests/site/test-results/` as an Actions artifact for seven days.
The separate [CI & Lint workflow](../.github/workflows/ci.yml) skips automatic
runs when all changed paths belong to `site/`, `tests/site/`, `docs/`, the two
root READMEs, `scripts/site_release.mjs`, or the Website workflow itself. Website
checks still run for website source, tests, generator, or workflow changes.
Any other changed path, including native source/tests, build scripts, dependencies,
and CI/Release workflows, retains the full x64/x86 build, tests, and lint.
Mixed changes also run full CI; `workflow_dispatch` always remains available.
Do not require this path-filtered workflow as a PR status check without adding
an always-running gate: a workflow skipped by path filtering can leave a
required check pending.
Before the first deployment, a maintainer must select **Settings → Pages →
Build and deployment → Source: GitHub Actions**. Push the workflow and site, then
check the Website Actions run; it can also be started manually on `main`.
After a successful stable publication, the Release workflow explicitly dispatches
Website on `main` to refresh download metadata. This uses a separate job with only
`actions: write`; normal website checks and IME packaging gain no extra permissions.
The dispatch is necessary because publication using `GITHUB_TOKEN` does not
normally trigger another workflow's release event. Prereleases and package-only
runs do not refresh stable downloads. If a release is created/edited manually or
the refresh dispatch fails, run Website manually on `main`; no rebuilding or
re-uploading installer assets is needed.
The expected public URL is `https://cloudfyy.github.io/qiqiime/`. Do not announce
it as live until the Pages deployment succeeds. GitHub Pages serves static files;
issue submissions and downloads continue to use GitHub.

## Design references

- [Architecture](arch.md): subsystem overview.
- [Composition lifecycle](composition.md): ownership, reentrancy, cleanup, and validation.
- [Logging](log.md) and [metrics](metrics.md): capture, protocols, and measurement limits.
- [Restricted input](restricted.md) and [AppContainer](uwp.md): host and permission boundaries.
- [Host tests](hosts.md): manual procedures and reported results.

Historical implementation batches and old coverage snapshots remain available
in Git history. Do not treat them as current test counts or compatibility claims.
