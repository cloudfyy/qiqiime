# Packaged AppContainer search and IME test host

This directory is an opt-in integration host. It is not built or tested by the
normal QiQiIME CTest run.

The host:

- is a minimum-permission, x64 packaged AppContainer;
- uses a blue package and executable icon with a circular `U` badge to
  distinguish it from the other hosts;
- exposes one editable `TextBox` for normal TSF/IME input;
- registers the legacy `windows.search` contract and handles Search Pane
  query, suggestion, submit, and search activation events;
- reports AppContainer/LPAC token state, inferred LPAC status, package and
  capability SIDs, and broad `ALL APPLICATION PACKAGES` group membership;
- loads controlled records and synthetic linguistic details from
  [TestData.json](TestData.json);
- writes privacy-safe events to the existing `QiQiIME.TraceLogging` provider;
  and
- exports the same non-input diagnostic messages to package
  `LocalState\qiqi-appcontainer-host.log`.

The host never logs entered text, query contents, candidate contents,
clipboard data, window titles, or filesystem paths.

## Important scope

The Search Contract is a consumer-side compatibility probe. A Search Contract
event does not prove that `ITfFnSearchCandidateProvider` was queried directly.
Windows 10/11 may no longer expose the legacy pane; in that case the app records
the exact `HRESULT` and still supports ordinary IME input plus synthetic tests.

This manifest intentionally requests no capabilities. On that route, bundled
candidate order and a denied direct Broker probe are expected; it is not a
test of personalized dictionary synchronization. `QiQiChangeSyncTest` provides
the positive `registryRead` check using an isolated registry and a real
restricted AppContainer token. For other capability profiles, see the
[permission hosts](../grpperm/README.md).

## Package

From PowerShell at the repository root:

```powershell
tests\hosts\uwp\Package.ps1 -Configuration Release
```

The script:

1. validates the manifest and packaged test data;
2. configures a standalone CMake/MSVC build;
3. generates the C++/WinRT projection from the installed Windows SDK;
4. generates and embeds the executable window icon, then links an
   `/APPCONTAINER` executable;
5. generates package assets and a PRI;
6. creates a self-signed development certificate;
7. signs an installable MSIX; and
8. validates the package contents and signer.

Outputs:

- `dist\test-hosts\QiQiImeAppContainerSearchHost_<version>.msix`
- `cert\QiQiImeDevelopment.cer` (public certificate kept in source control)
- a generated development private key retained in the current user's
  `Cert:\CurrentUser\My` store for repeatable package updates

Signing private keys and PFX files are never copied into source control. The
checked-in `.cer` contains only the public certificate and is safe to share.
Subsequent package builds find the private key whose thumbprint matches this
file. This keeps package upgrades within one trusted signer identity.

To intentionally replace the development certificate, run the package script
with `-RotateDevelopmentCertificate`, import the updated `.cer` on test
machines, and include that public-certificate change in the same commit as the
affected package update. `-CertificatePath` remains available for an externally
managed PFX and does not replace the checked-in development certificate.

To use an existing code-signing PFX:

```powershell
tests\hosts\uwp\Package.ps1 `
  -CertificatePath C:\path\development.pfx `
  -CertificatePassword $securePassword
```

The certificate subject becomes the package Publisher. Its public trust
certificate must be installed in `Cert:\LocalMachine\TrustedPeople`.

## Install and remove

```powershell
tests\hosts\uwp\Install-Host.ps1
tests\hosts\uwp\Uninstall-Host.ps1
```

Run the install script from an elevated PowerShell. Microsoft MSIX sideloading
requires the checked-in self-signed development certificate in
`Cert:\LocalMachine\TrustedPeople`; a current-user `TrustedPeople` entry is not
sufficient for this deployment path. The script checks elevation, imports the
certificate from `cert\QiQiImeDevelopment.cer` into that exact store, and
installs only the host package.

After testing, the package and trust entry can be removed together with:

```powershell
tests\hosts\uwp\Uninstall-Host.ps1 `
  -RemoveDevelopmentCertificate
```

Launch **QiQiIME AppContainer Search Host** from Start, then:

1. confirm:
   - `TokenIsAppContainer: true`;
   - the broad package-group membership reported by this Windows build;
   - whether LPAC is inferred from group absence; and
   - the package SID is available;
2. type a Wubi code in the input box;
3. inspect the automatically run synthetic fixture result;
4. optionally click **Run synthetic fixtures** again;
5. optionally click **Open legacy Search Pane** and note the recorded `HRESULT`
   if the OS no longer supports it;
6. click **Probe Config Broker** to inspect package identity, proxy/DLL access,
   DCOM activation, and Broker method access; and
7. capture `QiQiIME.TraceLogging` with QiQi Log Monitor.

## Build prerequisites

- Visual Studio 2022 or 2026 with the MSVC desktop C++ workload;
- Windows 10/11 SDK 10.0.26100.0, including C++/WinRT, PRI, MakeAppx, and
  SignTool; and
- CMake 4.x.

The Visual Studio C++ UWP workload is intentionally not required.
