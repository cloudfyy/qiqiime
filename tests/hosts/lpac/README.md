# QiQiIME LPAC input host

This standalone fixture launches a Win32 Rich Edit host inside an explicit
LPAC AppContainer profile. It is separate from the ordinary UWP and packaged
full-trust hosts and is not part of the normal CTest run, product build, or
QiQiIME MSI.

## Profiles

Each run creates one temporary AppContainer profile and deletes it after the
child exits:

| Profile | Capability | Expected boundary |
| --- | --- | --- |
| `zero` | none | registry and COM initialization are denied |
| `registryRead` | `registryRead` | the QiQi registry key is readable; COM initialization is denied |
| `lpacCom` | `lpacCom` | COM initialization succeeds; ordinary TSF thread-manager activation remains denied |

Profiles are deliberately separate. `lpacCom` is a capability probe for the
future signed QiQi COM broker; it does not grant unrestricted COM, TSF, UI, or
registry access.

The launcher sets both
`PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` and
`PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` with
`PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT`. The child displays:

- `TokenIsAppContainer`;
- `TokenIsLessPrivilegedAppContainer`;
- inferred LPAC state when `ALL APPLICATION PACKAGES` is absent;
- package SID;
- capability SIDs; and
- presence of `S-1-15-2-1` and `S-1-15-2-2`.

Some Windows builds report `ERROR_INVALID_PARAMETER` for
`TokenIsLessPrivilegedAppContainer`. The explicit all-packages opt-out and token
group result remain visible as the fallback proof. Presence of
`S-1-15-2-2` is not required because that principal may be enforced through ACL
evaluation without appearing in `TokenGroups`.

## Build

Use Visual Studio 2022 or 2026 with the desktop C++ workload, Windows 10/11
SDK 10.0.26100.0, and CMake 4.x. From the repository root:

```powershell
cmake -S tests\hosts\lpac -B build\hosts\lpac -A x64
cmake --build build\hosts\lpac --config Release
```

Select the Visual Studio CMake generator explicitly when CMake cannot detect
it, for example `-G "Visual Studio 18 2026"` or
`-G "Visual Studio 17 2022"`.

## Run

Interactive launch defaults to `lpacCom`:

```powershell
tests\hosts\lpac\Run-Host.ps1
```

Select a profile, or run it without the UI:

```powershell
tests\hosts\lpac\Run-Host.ps1 -Profile registryRead
tests\hosts\lpac\Run-Host.ps1 -Profile zero -Smoke
tests\hosts\lpac\Run-Host.ps1 -SmokeAll
```

You can also run the standalone smoke test through its own CTest tree:

```powershell
ctest --test-dir build\hosts\lpac -C Release --output-on-failure
```

`-SmokeAll` validates all three profiles, unsupported profile and excess
argument rejection, the expected registry/COM boundaries, and removal of each
temporary package profile.

The executable can be invoked directly as:

```text
QiQiImeLpacLauncher.exe [zero|registryRead|lpacCom] [--smoke]
```

No argument selects interactive `lpacCom`.

## Interactive acceptance

The Rich Edit target is intended for a manual TSF/UI boundary check:

1. launch the ordinary UWP fixture as the positive IME baseline;
2. launch this host with `zero`, `registryRead`, and `lpacCom`;
3. confirm the token and capability facts;
4. verify ordinary text entry remains possible; and
5. record only IME composition/candidate pass, denial, or HRESULT results.

An LPAC process is not expected to support all ordinary TSF activation and
UI paths. The real `lpacCom` smoke intentionally expects
`CoCreateInstance(CLSID_TF_ThreadMgr)` to return access denied. Direct
candidate-input acceptance in that profile must not be interpreted as a
failure-free ordinary UWP guarantee.

## Privacy and diagnostics

The host never logs entered text, candidate contents, clipboard data, window
titles, or filesystem paths. It writes privacy-safe status text to the
`QiQiIME.TraceLogging` provider when ETW registration is permitted and to a
private `LOCALAPPDATA\QiQiImeLpacHost\qiqi-lpac-host.log`. LPAC ETW registration
can itself be denied; that `HRESULT` is displayed rather than reported as a
successful trace write.
