# Packaged UWP test hosts

The three x64 AppContainer applications share the XAML input box, legacy Search
Contract probe, synthetic fixtures, and diagnostics. They can be installed
side by side and are not part of the production installer or ordinary CTest.

| Profile | Manifest capabilities | Expected QiQi configuration route | Icon |
|---|---|---|---|
| [zero](zero/app.psd1) | Neither `lpacCom` nor `registryRead` | Built-in defaults and bundled candidate ordering | Lock |
| [com](com/app.psd1) | `lpacCom` only | Broker, when the installed Broker permits access | Linked nodes |
| [reg](reg/app.psd1) | `registryRead` only | Read-only registry, when the key is readable | Registry tree |

The shared manifest uses the same legacy `windows.search` extension in every
profile. Its events do not prove a direct TSF search-candidate query. When the
legacy pane is unavailable, the host still accepts ordinary IME input and
provides its synthetic checks.

## Build and install

From the repository root:

```powershell
tests\hosts\uwp\Package.ps1
tests\hosts\uwp\Install.ps1
```

`Package.ps1` builds all three by default; `-Profile com` builds only that profile.
It reuses one C++/WinRT projection and shared compiled host sources, generates
each icon and manifest, and verifies the signed MSIX with [Check.ps1](Check.ps1).
Outputs are `dist\test-hosts\uwp-zero-<version>.msix`,
`uwp-com-<version>.msix`, and `uwp-reg-<version>.msix`. These package identities
are separate from the older single-profile test host.

Use VS 2022 or 2026 with the desktop C++ workload, Windows SDK 10.0.26100.0
with C++/WinRT, MakePri, MakeAppx and SignTool, and CMake 4.x. The Visual Studio
C++ UWP workload is not required.

The shared public certificate is [dev.cer](dev.cer). The matching signing key
must already be present in `Cert:\CurrentUser\My`. An external PFX can be used
with `-CertificatePath` and `-CertificatePassword`; keep it out of the
repository. `-SkipSigning` produces validated but non-installable MSIX files.
`-RotateDevelopmentCertificate` deliberately replaces the shared developer
certificate, including the one used by the Full Trust host.

Installation needs elevation only to add the public certificate to
`Cert:\LocalMachine\TrustedPeople` when it is not already trusted. It does not
change production registry, directory, or Broker permissions. `Install.ps1`
installs all three profiles by default, validating every selected package before
trusting the certificate or installing any app. Installation is sequential, not
transactional: an OS installation failure stops the script, but does not remove
profiles already installed successfully.

Use `-Profile zero`, `-Profile com`, or `-Profile reg` for one application.
An explicit `-PackagePath` also requires one of these individual profiles.
To remove an individual app while retaining the certificate and the other profiles:

```powershell
tests\hosts\uwp\Remove.ps1 -Profile com
```

The isolated `QiQiUwpInstallScriptTest` checks defaults, single-profile/custom-path
installation and all-package preflight failure without trusting certificates or
registering real apps.

## Runtime acceptance

For each application launched from Start:

1. Check `AppContainer=true` and `token match=true`. The host reads the actual
   token for `lpacCom` and `registryRead`; successful packaging does not prove
   that Windows granted them.
2. Click **Probe Config Access**. It reports direct Broker HRESULTs and the
   read-only registry probe separately. Named capabilities select a route but
   do not grant access to a particular Broker or registry key. A denial on an
   allowed route is not reported as success.
3. Use a synthetic pinned phrase. The zero profile should retain bundled order;
   the other profiles should use the pinned order only if their respective
   configuration and shared snapshot reads succeed. Do not grant broader rights
   to make a no-capability test pass.

The UI and logs report only diagnostics, not entered text, queries, candidates,
clipboard contents, window titles, or filesystem paths. Each app has its own
`LocalState\qiqi-appcontainer-host.log`. The independent
[`registryRead` regression](../../../docs/uwp.md#dictionaries-and-synchronization)
and [permission hosts](../grpperm/README.md) remain available for comparison.
