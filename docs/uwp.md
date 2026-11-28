# UWP and AppContainer Compatibility

## Scope

QiQiIME is an in-process TSF COM component. Its DLL runs with the host's token
and cannot bypass file, registry, mapping, or notification access restrictions.
Microsoft Store origin, MSIX packaging, an application name, or low integrity
alone does not establish AppContainer identity; inspect the actual hosting token.

TSF immersive/COM-less category registration and successful synchronization tests
do not certify every UWP, browser sandbox, packaged desktop app, or game.

The taskbar indicator uses a host-specific menu contract. SearchHost and the
dedicated UWP host advertise QiQiIME as an AppContainer TSF menu item but still
deliver clicks through `ITfLangBarItemButton::OnClick`; they do not call
`InitMenu`. QiQi therefore handles left-click mode toggling and opens right-click
quick settings with `TrackPopupMenuEx` on the original TSF thread. The focused
TSF context window is preferred as the menu owner, with an internal owner
window fallback. This does not depend on a host pumping posted messages after
the click callback. `InitMenu` remains available for hosts that implement the
standard TSF menu contract. Desktop hosts retain the original custom-menu path.

Searchbox candidate integration exposes the complete candidate model through
`ITfCandidateListUIElement`, including every candidate string and the real page
starts/current page. The host renders and navigates that model; QiQi keeps global
selection indices and raw strings in this integration mode. Ordinary desktop and
UILess presentation use the same candidate/page state with display annotations.

## Deployment and permissions

Normal [ZIP/source registration](../scripts/register_ime.ps1) grants the resolved
DLL directories and shared `data` directory read/execute access with inheritable
file/subdirectory permissions for:

| SID | Group |
| --- | --- |
| `S-1-15-2-1` | ALL APPLICATION PACKAGES |
| `S-1-15-2-2` | ALL RESTRICTED APPLICATION PACKAGES |

These are broad application-package groups, not per-application IDs.
The script does not grant write access. `-VerifyOnly` and
`-EnableCurrentUserOnly` skip that ACL step. MSI installation also attempts
package-group read/execute access on the installation directory.
Check actual ACL results; location under Program Files does not prove access.

Runtime configuration is no longer read from the host package's virtualized
`HKCU` view. `QiQiIMEConfigBroker.exe` owns all configuration writes and the
real desktop-user skin catalog. Ordinary AppContainer, Full Trust, and LPAC
hosts receive a validated in-process snapshot through local DCOM when their
token carries the required COM capability. Broker registration grants the
package groups and `lpacCom` capability local DCOM access rights.

Taskbar Search requires one narrow exception. Its SearchHost LPAC token carries
`registryRead` but does not carry `lpacCom`, so its `CoCreateInstance` is denied
before Broker method dispatch even when package-specific DCOM ACEs exist. QiQi
detects `TokenCapabilities` before transport and therefore does not call
`CoCreateInstance` for this route.
Windows Search package identities are discovered from
`%WINDIR%\SystemApps`, and their package and package-capability SIDs receive
only `KEY_READ` on the config root plus the known skin schema:
`Skins`, `Catalogs`, the active catalog, built-in skin IDs, and `Light`/`Dark`.
SearchHost uses that direct read whenever its read-only route lacks a cache.
It decodes the same complete configuration, skin catalog,
sync-directory address, and commit metadata, then commits the validated result
to the normal cache. It never writes registry values or walks arbitrary
subtrees. A successful startup or refresh requires no further registry read on
subsequent focus/input operations.

An AppContainer with neither `lpacCom` nor `registryRead` uses a third route:
default `QiQiConfig`, the versioned built-in skin catalog, no sync-directory
address, and no user-registry snapshot. It does not attempt DCOM or open the
configuration registry. The engine still uses readable bundled dictionaries
and falls back to its compiled core dictionary if the package file ACLs do not
expose the bundled data. This route is intentionally read-only and does not
reflect desktop preference changes.

Search package and package-capability SIDs are also added to local-only AppID
DCOM ACLs for package hosts that do carry `lpacCom`. Access and Launch use
valid local-only DACLs: `0x03` for access and `0x0B` for launch/activation. The
Broker still rejects callers whose token does not match its user SID and
Windows session. Adding DCOM ACEs cannot grant a capability that is absent from
the caller token.

For an uncommon package identity that is not automatically discovered, use
[repair_appcontainer_access.ps1](../scripts/repair_appcontainer_access.ps1):

```powershell
# Inspect installed package identities.
.\scripts\repair_appcontainer_access.ps1 -Action List

# Read-only diagnosis for one package.
.\scripts\repair_appcontainer_access.ps1 `
  -Action Diagnose `
  -PackageFamilyName Contoso.App_1234567890abc

# Preview the exact package-only ACL changes.
.\scripts\repair_appcontainer_access.ps1 `
  -Action Grant `
  -PackageFamilyName Contoso.App_1234567890abc `
  -WhatIf

# For a nonstandard development path whose parent folders also deny traverse,
# explicitly include ancestor Traverse ACEs.
.\scripts\repair_appcontainer_access.ps1 `
  -Action Grant `
  -PackageFamilyName Contoso.App_1234567890abc `
  -IncludeAncestors
```

Run `Grant` or `Revoke` from an elevated PowerShell. The tool modifies only the
selected package SID: DCOM local access/activation and ReadAndExecute on the
registered Broker/proxy paths. It does not grant Everyone, FullControl, or a
new package capability. The package must already carry the capability needed
for COM/LPAC use.

Registry access for desktop maintenance remains separate from runtime access.
Synchronization data grants read/traverse access, not write access. Other
qualifying applications may therefore read shared dictionary phrases but do
not gain direct write access to the Broker's registry. DACL repair runs only
after registry/catalog transactions complete; child keys are not traversed or
modified during KTM transactions. Do not open the entire user profile to solve
an access problem.
See the [privacy policy](../PRIVACY.md) for the disclosure boundary.

## Dictionaries and synchronization

Desktop defaults place user files under `%LOCALAPPDATA%\QiQiIME`, but a container's
environment, redirection, and effective token can change its paths and access.
Do not derive the desktop user's sharing directory from the container's own
`LOCALAPPDATA`.

The user dictionary file is authoritative. Desktop writers ask the Broker to
publish the absolute read-only channel address. The Broker updates
`HKCU\Software\QiQiIME\UserDictSyncDirectory` and the configuration sequence in
its registry transaction, then returns the address in the configuration
snapshot. AppContainer readers with `lpacCom` obtain that snapshot through DCOM;
the `registryRead` route reads the published address from the granted schema;
the no-capability route has no shared dictionary address. Readers still consume
published increments rather than opening the desktop source file or creating a
separate local channel.
An explicitly empty address means no shared user dictionary; inaccessible or
invalid metadata/snapshots must be diagnosed, not silently treated as success.

[change_sync](../src/common/changesync.h) separates directory wakeups from the
persistent journal, version checks, and file-lock coordination. Notifications
alone are not reliable state. Global TSF compartments wake subscribers to read
once from the selected Broker/registry route; they do not carry configuration.
The no-capability route retains built-in defaults and has no remote refresh to
perform. Delivery failure or focus-dependent checks can delay observation. Do
not promise immediate cross-process consistency.

Registration attempts to precompile bundled dictionaries into adjacent `.qdict`
files. Runtime loading validates caches against format and source size/time.
It can probe or attempt local-cache writes; no blanket "sandbox never writes"
claim is made. Read, compile, write, allocation, and mapping can fail separately.

Read-only file mappings may share underlying file pages, while each process
retains its own objects and heap allocations. In-memory fallback is available
only when usable binary data was produced; it cannot recover an unreadable source
or guarantee dictionary availability.

Full-code practice uses the shared `data\full.idx` alongside the bundled
dictionaries. Normal input does not open this file. When practice is enabled,
the engine opens the installed file with read access, validates its version
and pinned payload, and retains a read-only mapping. MSI's existing inherited
read/execute access covers this file; no extra capabilities or write grants are
used. A missing or denied file leaves the host in its previous input mode and
reports the failure. The AppContainer sync fixture checks both readable and
denied cases with a real container token; it is not a real-app acceptance test.

The optional leading-`z` pinyin lookup uses the same policy for
`data\pinyin.idx`. It needs no network, extra capability or write access.
If the index cannot be read or verified, the host reports the error and keeps
the previous leading-`z` behavior. The AppContainer fixture also verifies this
fallback, read-only lookup, and denied writes for the pinyin index.

Optional Wubi output conversion uses the same shared read-only layout for
`data\conv.idx`. It is opened only when conversion is requested; the IME does
not write the index, grant new access, or modify dictionary files. If loading
fails, the previous effective output mode is retained and the error is reported.
The AppContainer fixture exercises both readable and denied index access.

## Diagnostics and input restrictions

Use the [log monitor or ETW tools](log.md), not the settings executable, to capture
diagnostics. Distinguish provider registration, event write failures, consumer
permissions, and capture loss. Do not elevate the tested host to obtain events.

The [restricted-input policy](restricted.md) is already implemented, including
password/PIN temporary English mode and private-scope persistence suppression.
It remains dependent on host signals; a working AppContainer sharing channel
does not validate password controls or secure-desktop support.

## Validation checklist

1. Record the actual host/version, Windows version, PID, architecture, and token.
2. Confirm the loaded DLL version/path, Broker registration, effective
   DLL/data/channel permissions, and whether the host can activate the Broker.
   Check registry maintenance and registration-time precompilation separately.
3. Exercise bundled dictionaries, Broker snapshot/address reads,
   snapshots/increments, dictionary path changes, configuration reads, and
   notifications. Separate readable data from successful write-back.
4. Test composition, focus, deactivation/reactivation, context removal, and
   restricted/ordinary fields in that application.
5. Retain actual HRESULT/Win32 failures and mark uncovered cases explicitly.
   General desktop success, mocks, or dedicated container processes are not
   substitutes for a real application loading the IME.

The [synchronization tests](../tests/common/sync.cpp) include controlled
AppContainer read/notify/locking and denied-write scenarios. Broader host evidence
belongs in the [host test records](hosts.md); do not infer universal compatibility.
Review paths and exported diagnostics for personal information before sharing.

## References

- [AppContainer isolation](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation)
- [Installation](install.md)
- [Configuration Broker](config-broker.md)
- [Configuration and sharing implementation](../src/common/config.cpp)
- [Dictionary synchronization implementation](../src/ime/engine/dictutil.cpp)
