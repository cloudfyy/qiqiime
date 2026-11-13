# UWP and AppContainer Compatibility

## Scope

QiQiIME is an in-process TSF COM component. Its DLL runs with the host's token
and cannot bypass file, registry, mapping, or notification access restrictions.
Microsoft Store origin, MSIX packaging, an application name, or low integrity
alone does not establish AppContainer identity; inspect the actual hosting token.

TSF immersive/COM-less category registration and successful synchronization tests
do not certify every UWP, browser sandbox, packaged desktop app, or game.

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

Settings under `HKCU\Software\QiQiIME` attempt to grant these groups `KEY_READ`.
Synchronization data grants read/traverse access, not write access. Other
qualifying applications may therefore read shared settings and phrases.
Do not open the entire user profile to solve an access problem.
See the [privacy policy](../PRIVACY.md) for the disclosure boundary.

## Dictionaries and synchronization

Desktop defaults place user files under `%LOCALAPPDATA%\QiQiIME`, but a container's
environment, redirection, and effective token can change its paths and access.
Do not derive the desktop user's sharing directory from the container's own
`LOCALAPPDATA`.

The user dictionary file is authoritative. Desktop writers publish the absolute
read-only channel address through
`HKCU\Software\QiQiIME\UserDictSyncDirectory`, with `ConfigSequence` detecting
address changes. AppContainer readers consume published snapshots/increments
rather than opening the desktop source file or creating a separate local channel.
An explicitly empty address means no shared user dictionary; inaccessible or
invalid metadata/snapshots must be diagnosed, not silently treated as success.

[change_sync](../src/common/change_sync.h) separates directory wakeups from the
persistent journal, version checks, and file-lock coordination. Notifications
alone are not reliable state. Global TSF compartments and registry sequences
trigger configuration reloads; delivery failure or focus-dependent checks can
delay observation. Do not promise immediate cross-process consistency.

Registration attempts to precompile bundled dictionaries into adjacent `.qdict`
files. Runtime loading validates caches against format and source size/time.
It can probe or attempt local-cache writes; no blanket "sandbox never writes"
claim is made. Read, compile, write, allocation, and mapping can fail separately.

Read-only file mappings may share underlying file pages, while each process
retains its own objects and heap allocations. In-memory fallback is available
only when usable binary data was produced; it cannot recover an unreadable source
or guarantee dictionary availability.

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
2. Confirm the loaded DLL version/path and effective DLL/data/registry/channel
   permissions. Check registration-time precompilation separately.
3. Exercise bundled dictionaries, shared address discovery, snapshots/increments,
   dictionary path changes, configuration reads, and notifications. Separate
   readable data from successful write-back.
4. Test composition, focus, deactivation/reactivation, context removal, and
   restricted/ordinary fields in that application.
5. Retain actual HRESULT/Win32 failures and mark uncovered cases explicitly.
   General desktop success, mocks, or dedicated container processes are not
   substitutes for a real application loading the IME.

The [synchronization tests](../tests/common/change_sync.cpp) include controlled
AppContainer read/notify/locking and denied-write scenarios. Broader host evidence
belongs in the [host test records](hosts.md); do not infer universal compatibility.
Review paths and exported diagnostics for personal information before sharing.

## References

- [AppContainer isolation](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation)
- [Installation](install.md)
- [Configuration and sharing implementation](../src/common/config.cpp)
- [Dictionary synchronization implementation](../src/ime/engine/engine_dict.cpp)
