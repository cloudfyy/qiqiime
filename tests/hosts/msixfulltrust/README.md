# Packaged full-trust IME test host

This host intentionally matches the trust and package-identity model of
Feedback Hub:

- MSIX package with a `Windows.FullTrustApplication` entry point;
- `runFullTrust`, medium integrity, and `TokenIsAppContainer == false`;
- package identity enabled;
- no `RegistryWriteVirtualization`, `unvirtualizedResources`, or excluded-key
  declaration, so HKCU/AppData writes use normal MSIX virtualization;
- a real `RichEdit` input target for QiQiIME; and
- no request for networking, file, device, registry, or process capabilities
  beyond `runFullTrust`.

It is an opt-in test fixture, independent of normal CTest and the QiQiIME
installer. Its package and executable use an orange icon with a square `F`
badge, including the native title-bar icon and taskbar icon.

## What it tests

The host displays and records only non-input diagnostic facts:

- package identity and package full name;
- `TokenIsAppContainer`;
- token integrity level;
- visible `HKCU\Software\QiQiIME` values:
  `ConfigSequence`, `ImeSkinId`, `ActiveCatalog`, sync-directory presence, and
  package-shadow marker presence;
- focus and input-length changes, without input or candidate contents; and
- direct `ITfFnSearchCandidateProvider` acquisition and candidate count.

The **Seed package-private shadow** button writes a marker, `ImeSkinId=default`,
and a deliberately stale `ConfigSequence` through the packaged process's
normal virtualized HKCU view. It is blocked unless package identity is present
and the token is non-AppContainer.

This action models the Feedback Hub symptom: the unpackaged desktop registry is
unchanged, while the packaged IME service sees a package-private sequence and
skin. Uninstalling the package removes its private shadow state. Do not click it
in an unpackaged launch of the EXE.

## Build and package

From the repository root:

```powershell
tests\hosts\msixfulltrust\Package.ps1 `
  -Configuration Release `
  -PackageVersion 0.1.0.0
```

The script:

1. validates the default-virtualization manifest;
2. builds the standalone x64 CMake project with the generated `F` window icon;
3. verifies that the PE does not have the AppContainer flag;
4. generates package visual assets and a PRI;
5. signs with the shared public/private development certificate pair; and
6. unpacks and verifies the final MSIX.

Public certificate:

`tests\hosts\uwp\cert\QiQiImeDevelopment.cer`

The matching private key must exist in the build user's
`Cert:\CurrentUser\My`. Private keys and PFX files are never placed in this
host directory or committed.

## Install, launch, and remove

```powershell
tests\hosts\msixfulltrust\Install-Host.ps1 -Launch
```

If the shared certificate is already in
`Cert:\LocalMachine\TrustedPeople`, installation does not require elevation.
On the first machine, rerun the install script from an elevated PowerShell so
it can add that one trust entry.

```powershell
tests\hosts\msixfulltrust\Uninstall-Host.ps1
```

Uninstalling removes the package and its virtualized HKCU/AppData state. The
shared certificate trust remains so the UWP host and subsequent builds continue
to install. To remove that shared machine trust explicitly:

```powershell
tests\hosts\msixfulltrust\Uninstall-Host.ps1 `
  -RemoveDevelopmentCertificate
```

## Suggested test

1. Record desktop configuration:

   ```powershell
   tests\hosts\msixfulltrust\Read-DesktopQiQiState.ps1
   ```

2. Launch the packaged host and confirm:
   - package identity is true;
   - `TokenIsAppContainer` is false;
   - integrity is medium; and
   - QiQi configuration is initially visible.
3. Type a code and observe the real candidate window. Save another skin in
   QiQi Settings and verify the host can hot-reload it.
4. Click **Seed package-private shadow**.
5. Change the desktop skin again and confirm the host's displayed sequence/skin
   no longer follows the desktop configuration.
6. Run `Read-DesktopQiQiState.ps1` again. Its `ConfigSequence`, `ImeSkinId`,
   and `PackagedHostShadow` must remain unchanged from step 1.
7. Restart the host and confirm the package-private values persist.
8. With a composition active, click **Probe TSF search provider** and
   record provider/candidate HRESULTs without recording candidate text.
9. Uninstall and reinstall to confirm the package-private shadow is removed.

The host emits the normal `QiQiIME.TraceLogging` provider and appends UTF-8
diagnostics to a package-private LocalAppData
`qiqi-msixfulltrust-host.log`. It never writes entered text, candidate
contents, registry paths, clipboard data, or window titles.

