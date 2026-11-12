# Code signing policy

## Current status

**SignPath signing is not configured.** This project is preparing the public
documentation needed to apply for free open-source signing. This document does
not assert that an application has been submitted or approved, that SignPath
sponsors the project, or that any existing release artifact is signed.

Existing tags and release attachments are not retroactively signed by adding
this policy. Check each downloaded artifact's actual signature and its release
notes. A tag, successful build, checksum, or this document is not a signature.

## Responsibilities

| Role | Current status |
| --- | --- |
| Project maintainer | [cloudfyy](https://github.com/cloudfyy), as identified in the [Chinese README](README.zh-CN.md) |
| Authors with direct write access | Must be confirmed against repository permissions before signing is enabled |
| Reviewers of external contributions | Must be designated by the maintainer before signing is enabled |
| Release-signing approvers | Not configured; must be designated before signing is enabled |

This document does not grant GitHub permissions or assign unconfirmed signing
authority. The maintainer must publish the actual authors, reviewers, and
approvers here before activation. Team members must use multi-factor
authentication for both repository and SignPath access.

Changes from contributors without direct write access require review by a
designated reviewer. Each release-signing request must receive manual approval
from a designated approver trusted by the project team. Build scripts and CI
changes require the same attention as application source code.

## Intended signing scope

If approved and configured, signing is intended for QiQiIME's own release
artifacts built from this repository:

- The x64 and x86 `qiqiime.dll` TSF components.
- The x64 and x86 settings, dictionary manager, and log-monitor executables.
- The final architecture-specific, localized MSI installers containing the
  approved runtime files and license/policy documentation.

ZIP distributions are intended to contain the same signed runtime files; an
ordinary ZIP container is not itself an Authenticode-signed executable. PDB files
are matching debug symbols, not signed runtime programs. Third-party binaries
must not be signed as if they were authored by this project.

The planned sequence is: build and test from an identified source revision,
sign the project DLLs/EXEs, package those signed files, sign the final MSIs,
verify signatures and timestamp results, and publish. Signed content must not
be modified afterwards. Signing failures must block the signed-release path,
not silently fall back to unsigned artifacts labeled as signed.

## Requirements before enabling signing

- Obtain SignPath Foundation approval and configure the project, certificate,
  artifact rules, signing policy, and restricted CI credentials.
- Confirm actual GitHub/SignPath permissions, MFA, reviewers, and approvers.
- Verify dependency/dictionary licensing against
  [Third-party notices](THIRD_PARTY_NOTICES.md), including the pinned Jidian
  reconstruction; update provenance records whenever dependencies change.
- Connect a verifiable CI build to the signing service; do not treat arbitrary
  locally uploaded files as verified source builds.
- Enforce project product-name and consistent product-version restrictions on
  the signed binaries, using the existing shared version resources.
- Implement the signing/packaging sequence above in the release workflow and
  verify the resulting signatures before claiming a release is signed.
- Link this **Code signing policy** and the [Privacy policy](PRIVACY.md) from
  the project home and download/release pages.

See the [SignPath Foundation conditions](https://signpath.org/terms) and
[application page](https://signpath.org/apply). Approval is discretionary;
an open-source license alone does not guarantee acceptance.

Only after approval and activation should the current-status section be updated
to acknowledge the service with the required wording:

> Free code signing provided by [SignPath.io](https://about.signpath.io),
> certificate by [SignPath Foundation](https://signpath.org).

The quotation above is a future acknowledgment requirement, **not a statement of
current sponsorship**. Under that program the certificate is issued to SignPath
Foundation, not to the individual project maintainer.

## Download verification

Use the [official releases](https://github.com/cloudfyy/qiqiime/releases).
When signing is available, check the actual publisher, signature validity, and
timestamp through Windows file properties or `signtool verify /pa /all /v`.
Compare checksums only against a trusted release source.

Trusted signing identifies a publisher and detects changes to signed content;
it does not certify that software is bug-free or guarantee that Edge/SmartScreen
will stop warning about a new file. Do not disable security protection or bypass
organization policy to install QiQiIME.

## 中文摘要

当前尚未配置 SignPath，也不声明已提交申请、获批、获得资助或现有附件已签名。
维护者需先确认实际作者权限、审阅者、签名批准者及双因素认证；这些角色不能由本文
自动授予。获批后才接入可验证构建、逐次人工批准、先签运行组件再打包签 MSI 的流程，
并更新实际发布状态及服务致谢。隐私边界见 [隐私政策](PRIVACY.md)；
第三方许可、已核对的极点快照及复核方法见 [第三方声明](THIRD_PARTY_NOTICES.md)。
