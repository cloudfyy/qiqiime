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
| PowerShell | Windows PowerShell 5.1 or PowerShell 7+. Full script lint additionally requires PSScriptAnalyzer in the PowerShell edition selected by CMake. |
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
| [scripts](../scripts), [installer](../installer) | Build, lint, registration, and release packaging. |
| [data](../data), [licenses](../licenses) | Bundled dictionaries/assets and third-party license texts. |

Each module owns its CMake source list. Keep related test suites in short,
thematic files: common tests use `tests\common\core`, dictionary tests use
`tests\ime\engine\dict`, and TSF service tests use [tests/ime/svc](../tests/ime/svc).
TSF mocks live in `tests\ime\tsf\mock`, with `mocks.h` as the aggregate header.
Share helpers through headers; do not include test `.cpp` files.

## Build and test

From the repository root, build and test both architectures sequentially:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
```

The default configuration is Release. Use `-Config Debug` for Debug.
The command does not create installers unless `-Package` is supplied.

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

Additional ETW test modes appear in the CTest listing. Global ETW integration
tests can return skip code 77 when capture permissions are unavailable; a skip
is not a pass. Private-session ETW tests do not replace global or real-host tests.

Run the two architectures sequentially. GUI/TSF suites share desktop and
single-instance state; avoid keyboard/mouse interaction during those tests.
`QiQiCompositionLifecycleTest` omits standalone UI suites and supplements,
rather than replaces, the full `QiQiTsfUiTest`.

Tests must isolate configuration, files, and owned child processes. Do not run
production registration or data-removal commands as a substitute for tests.
For manual compatibility checks, use the [host test guide](hosts.md).

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

Install PSScriptAnalyzer in the PowerShell edition selected by CMake if needed,
then run lint from a configured Visual Studio build:

```powershell
Install-Module -Name PSScriptAnalyzer -Scope CurrentUser
cmake --build build\x64 --config Release --target lint
cmake --build build\x86 --config Release --target lint
```

[lint.ps1](../scripts/lint.ps1) rebuilds `ALL_BUILD` with CppCoreCheck. Repository
source/test warnings have a zero baseline, with the existing C26490 and
external-dependency exclusions. Normal compilation also enables `/W4`.

The PowerShell gate checks Error-level PSScriptAnalyzer findings in `scripts`
and `tests`. If the module is missing, the script warns and checks AST syntax
instead; that fallback is not a completed PSScriptAnalyzer run.

### Native coverage

Use Visual Studio Enterprise's `Microsoft.CodeCoverage.Console` with matching
PDBs and isolated binary copies. Static native instrumentation needs suitable
`/PROFILE /INCREMENTAL:NO` linking; do not change production release flags or
instrument installed binaries. Enable child-process collection when testing tools.

Run coverage tests serially. Include test translation units during collection
when they instantiate production templates; apply the `src` filter when reporting.
Optimized native line tables may attribute inlined work to callers. Keep the
boundary-test evidence and disclose unmapped checkpoints rather than claiming
complete helper coverage from a covered caller.
Merge duplicate
static-library lines by source path and line number, and list production files
missing from the report. State the configuration, source revision, and measured
scope. Line coverage is not branch coverage or real-host certification.
Verify that instrumentation restores the original binaries/PDBs, and never
package instrumented outputs.

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

Packaging replaces same-version output paths under `dist`. Preserve any previous
artifacts you need. Do not include test logs, dumps, or instrumented binaries.
For documentation/license payload changes, keep staging, WiX, and installer tests
consistent.

Update the current README references and release notes when changing versions.
The [Release workflow](../.github/workflows/release.yml) builds, tests, and packages
tagged commits before uploading assets. Pushing a tag is not proof of publication
or code signing; check the workflow and [signing policy](../CODE_SIGNING.md).
Release pages contain download links followed by only the matching version's
section from the release notes. The complete history remains in the repository.
[release_notes.ps1](../scripts/release_notes.ps1) generates this body from the
version, tag, and actual package filenames; missing or duplicate version sections
fail publication rather than including unrelated history.
Write the default Chinese notes first and add their translation under
`## English` within the same version section. Release pages show Chinese
downloads and notes by default, with English downloads and notes inside a
collapsed `English` section. Missing or empty translations fail generation.

## Design references

- [Architecture](arch.md): subsystem overview.
- [Composition lifecycle](composition.md): ownership, reentrancy, cleanup, and validation.
- [Logging](log.md) and [metrics](metrics.md): capture, protocols, and measurement limits.
- [Restricted input](restricted.md) and [AppContainer](uwp.md): host and permission boundaries.
- [Host tests](hosts.md): manual procedures and reported results.

Historical implementation batches and old coverage snapshots remain available
in Git history. Do not treat them as current test counts or compatibility claims.
