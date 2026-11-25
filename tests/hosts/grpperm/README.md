# QiQiIME AppContainer permission hosts

This standalone fixture launches a Win32 Rich Edit host inside explicit
AppContainer profiles. It is separate from the packaged AppContainer and
full-trust hosts and is not part of the normal CTest run, product build, or
QiQiIME MSI. The two COM hosts use distinct blue `B` and purple `P` icons.

## Profiles

Each run creates one temporary AppContainer profile and deletes it after the
child exits:

| Profile | Capability | Expected boundary |
| --- | --- | --- |
| `broadGroupCom` | `lpacCom` | broad-group token policy is tested with custom-COM capability |
| `packageOnlyCom` | `lpacCom` | all-packages matching is opted out; package/capability access is tested |
| `zero` | none | registry and COM initialization are denied |
| `registryRead` | `registryRead` | the QiQi registry key is readable; COM initialization is denied |
| `lpacCom` | `lpacCom` | compatibility alias for the restricted COM profile |

Profiles are deliberately separate. `broadGroupCom` and `packageOnlyCom` are the
two Broker permission fixtures. Neither grants unrestricted COM, TSF, UI, or
registry access.

The launcher always sets
`PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`. It sets
`PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` with
`PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT` only for restricted
profiles. The child displays:

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
cmake -S tests\hosts\grpperm -B build\hosts\grpperm -A x64
cmake --build build\hosts\grpperm --config Release
```

Select the Visual Studio CMake generator explicitly when CMake cannot detect
it, for example `-G "Visual Studio 18 2026"` or
`-G "Visual Studio 17 2022"`.

## Run

Interactive launch defaults to `packageOnlyCom`:

```powershell
tests\hosts\grpperm\Host.ps1
```

Select a profile, or run it without the UI:

```powershell
tests\hosts\grpperm\Host.ps1 -Profile broadGroupCom
tests\hosts\grpperm\Host.ps1 -Profile packageOnlyCom
tests\hosts\grpperm\Host.ps1 -Profile registryRead
tests\hosts\grpperm\Host.ps1 -Profile zero -Smoke
tests\hosts\grpperm\Host.ps1 -SmokeAll
```

You can also run the standalone smoke test through its own CTest tree:

```powershell
ctest --test-dir build\hosts\grpperm -C Release --output-on-failure
```

`-SmokeAll` validates all profiles, unsupported profile and excess
argument rejection, the expected registry/COM boundaries, and removal of each
temporary package profile.

`--broker-smoke` is an opt-in integration check. With the Broker registered, it
requires `broadGroupCom` or `packageOnlyCom` to activate the Broker and
complete a read successfully. Run it once with the Broker stopped to test
activation, and once with the Broker already running to isolate
AccessPermission and proxy-file authorization.

Broker smoke exit codes are `9` for apartment initialization, `10` for
`CoCreateInstance`, `11` for proxy security, and `12` for the Broker `Read`
method.

The executable can be invoked directly as:

```text
QiQiImePermissionLauncher.exe [broadGroupCom|packageOnlyCom|zero|registryRead|lpacCom] [--smoke]
QiQiImePermissionLauncher.exe packageOnlyCom --broker-smoke
```

No argument selects interactive `packageOnlyCom`.

## Interactive acceptance

The Rich Edit target is intended for a manual TSF/UI boundary check:

1. launch the packaged AppContainer fixture as the IME baseline;
2. compare `broadGroupCom` and `packageOnlyCom`, then use `zero` and `registryRead`
   as negative controls;
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
private `LOCALAPPDATA\QiQiImeAppContainerPermissions\qiqi-appcontainer-permission-host.log`. LPAC ETW registration
can itself be denied; that `HRESULT` is displayed rather than reported as a
successful trace write.
