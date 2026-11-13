# QiQi Wubi IME

English | [简体中文](README.zh-CN.md)

QiQiIME is an open-source **Wubi 86** input method for Windows, built in C++ on
the native Text Services Framework (TSF).

## Downloads and supported systems

Download installers from [GitHub Releases](https://github.com/cloudfyy/qiqiime/releases).
The current source version is **1.0.7**, with release tag
[`v1.0.7`](https://github.com/cloudfyy/qiqiime/releases/tag/v1.0.7).
The files actually attached to a release are authoritative; a tag alone does not
mean that its build or publication succeeded.

| System | Installer |
| --- | --- |
| Windows 11 x64 | `win-x64` MSI |
| Windows 10 22H2 x64 | `win-x64` MSI |
| Windows 10 22H2 x86 | `win-x86` MSI |

The x64 installer includes both native x64 and x86 components for WOW64
applications. Choose the installer by **operating-system architecture**, not by
the architecture of an individual application. Native ARM64 components are not
provided. Release builds statically link the MSVC runtime.

Read the [installation guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/install.md)
and [release notes](https://github.com/cloudfyy/qiqiime/blob/main/docs/release-notes.md)
before installing or upgrading. The development, logging, metrics, restricted-input,
and AppContainer guides are available in English.

## Features

- Jidian Wubi 86 and Haifeng dictionaries, an optional modern vocabulary
  extension, and custom UTF-8 text, supported Rime dictionary YAML, or compiled
  `.qdict` dictionaries.
- Custom user phrases, phrase pinning and suppression, optional recent-selection
  ordering, and a dictionary manager with import, export, and `.qbak` backup/restore.
- Optional four-code unique-candidate commit, fifth-key commit, empty-code
  clearing, wildcard lookup, code hints, and a `z`-prefixed temporary English mode.
- Chinese/English, full-width/half-width, and Chinese/English punctuation modes.
- Horizontal or vertical candidates, a language-bar/tray menu, and an optional
  floating status bar, with DPI, theme, and high-contrast support.
- Standalone settings, dictionary manager, and local ETW log-monitor tools.
  Settings and tool interfaces support Simplified Chinese and English.
- Uninstall keeps personal data by default. MSI uninstall offers an optional
  cleanup checkbox; ZIP unregistration supports explicit `-RemoveUserData`.
  Both preserve custom dictionaries, exports, backups, and other users' data.
- Local dictionary and configuration sharing between desktop and AppContainer
  instances; this is **not cloud synchronization**.

Some applications render their own TSF candidate UI. Compatibility and candidate
positioning depend on the host. See the
[host test records](https://github.com/cloudfyy/qiqiime/blob/main/docs/hosts.md)
for actual evidence and known limitations.

## Quick start

1. Back up existing settings and user dictionaries before upgrading.
2. Download the matching architecture and preferred installer language from the
   official release. Installation and removal require administrator permissions.
3. Install, reopen your application, and use **Win + Space** to select QiQi Wubi 86.
4. Type `wqiy` in Chinese mode to select or commit `你`.

The Start menu's **QiQiIME** folder provides settings, the dictionary manager,
and the log monitor. Daily typing normally does not require administrator rights.
If an application still uses an old DLL after an upgrade, save your work and
sign out and back in, or restart Windows.

| Shortcut | Action |
| --- | --- |
| `Shift` tap or `Ctrl + Space` | Toggle Chinese/English |
| `Shift + Space` | Toggle full-width/half-width |
| `Ctrl + .` | Toggle Chinese/English punctuation |
| `Space` | Commit the selected candidate |
| `1` through `9` | Select a candidate in Wubi composition |
| `Enter` | Commit the raw code or temporary English text |
| `-` / `=` or `PageUp` / `PageDown` | Previous/next candidate page |
| `Ctrl + =` | Read a suitable clipboard phrase and add it to the user dictionary |
| `Ctrl + 1` through `9` | Pin a candidate |
| `Ctrl + Delete` or `Ctrl + Shift + 1` through `9` | Remove a user phrase or suppress a dictionary entry |

Some shortcuts depend on settings and input context. Password/PIN recognition is
not guaranteed in every host. Use a system keyboard layout or another verified
input method for sensitive fields. Windows sign-in and the secure desktop are
outside the verified support scope.

## Privacy

Typing and dictionary lookup run locally. The current application does not
implement cloud conversion, automatic telemetry upload, or automatic update
downloads. User dictionaries, recent-selection records, caches, and backups can
contain sensitive information. AppContainer sharing also involves local read
permissions beyond the current process.

Read the [Privacy policy](PRIVACY.md) for storage, clipboard access, diagnostic
capture, local sharing, and data retained after uninstalling. Do not post raw
logs, dictionaries, or backups in public issues.

## Code signing policy

**SignPath signing is not currently configured. This repository does not claim
SignPath approval, sponsorship, or that its release files have been signed.**

See the [Code signing policy](CODE_SIGNING.md) for status, responsibilities,
intended scope, and requirements before enabling signing. A trusted signature
does not guarantee that SmartScreen will immediately recognize a new release.
Never disable security protection just to install the application.

## Building from source

Requirements include Windows, Visual Studio's C++ desktop workload and x64/x86
build tools, a Windows SDK, CMake 4.4.4 or newer, and vcpkg with `ms-gsl`.
The default presets target Visual Studio 2026 and use `/std:c++latest`; other
generators require separate validation.

From a PowerShell terminal in the repository root:

```powershell
# Build and test both architectures; does not create installers.
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1

# Explicitly create release packages (also requires .NET and WiX).
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
```

For MSI downloads, installation, upgrades, removal, and common questions, follow the
[installation guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/install.md).
Do not move a registered DLL or use manual registration scripts to uninstall an
MSI-managed installation. See the
[development guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/dev.md)
for tests and build details.

## License and third-party material

QiQiIME's original code is licensed under [GNU GPL version 3 or any later version](LICENSE)
(`GPL-3.0-or-later`). Original source files, tests, scripts, and comment-capable
build files identify their copyright and license with SPDX headers. JSON
configuration files remain covered by this project-level license without adding
invalid JSON comments. Dictionaries and third-party
code retain their own licenses; see [Third-party notices](THIRD_PARTY_NOTICES.md)
and the complete texts in [licenses](licenses/).
Both Jidian conversions exactly match a pinned Apache-2.0 upstream snapshot;
the notices record source hashes, conversion rules, and an offline verification
command. The Haifeng dictionary also retains its complete BSD notice in its header.

For bug reports, use [GitHub Issues](https://github.com/cloudfyy/qiqiime/issues).
Include the Windows version/architecture, application, release version, and
reproduction steps, without private input or unredacted diagnostic files.
