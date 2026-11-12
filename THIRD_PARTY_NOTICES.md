# Third-party notices / 第三方许可声明

QiQiIME's own code is distributed under [GNU GPL v3](LICENSE). This does not
replace the licenses or copyright notices of the materials below. The complete
third-party license texts accompany distributions in [licenses](licenses/).

## Jidian Wubi 86 dictionaries

- Upstream: [KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian).
- Imported files: `wubi86_jidian.dict.yaml` and `wubi86_jidian_extra.dict.yaml`.
- Distributed conversions: `data/wubi86_jidian.txt` and
  `data/wubi86_jidian_extra.txt`.
- Attribution: Kyle Bing and the upstream dictionary contributors.
- Changes: converted from Rime dictionary YAML into QiQiIME's UTF-8
  code-to-phrase text format; upstream weights determine phrase ordering.
  The files identify themselves as conversions, not unchanged upstream originals.
- License of the verified source snapshot: **Apache License 2.0**, consistent
  with the existing import attribution. Its full text is in
  [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt), checked against the
  [pinned upstream license](https://github.com/KyleBing/rime-wubi86-jidian/blob/18a6016c3d08a20f713f8f8e1791a2e79e0be081/LICENSE).

### Verified, reproducible source snapshot

Both converted dictionaries match the following fixed upstream revision
**line for line, including candidate order**, under the rules below:

- Repository revision: [`18a6016c3d08a20f713f8f8e1791a2e79e0be081`](https://github.com/KyleBing/rime-wubi86-jidian/tree/18a6016c3d08a20f713f8f8e1791a2e79e0be081),
  dated 2026-10-10.
- Core YAML version: `4.3`; Git blob `a22bd7717229b8884bf42cb50b113dba88dfef25`.
- Extension YAML version: `2019-09-06`; Git blob `696124619d0f3fa8f906d9974018f366ce898ccf`.
- The revision's root contains an Apache-2.0 `LICENSE`, with no root `NOTICE`
  or `COPYING` entry.

SHA-256 values of the original upstream bytes:

| File | SHA-256 |
| --- | --- |
| `wubi86_jidian.dict.yaml` | `89df3685e4a0c3d319a1c56a3a64e3df4ee3bfa3ba3892a6dadc77b21c989c5b` |
| `wubi86_jidian_extra.dict.yaml` | `3cf5f6110ab0df9157009bb88d3164c5405c970218e6888d08c5e67fc97a6e9c` |
| `LICENSE` | `43070e2d4e532684de521b885f385d0841030efa2b1a20bafb76133a5e1379c1` |

Reconstruction rules, preserving the existing conversions rather than changing
dictionary behavior:

1. Read tab-separated table rows after Rime's `...` delimiter, skipping blank
   and comment lines. Use text/code and optional numeric weight; missing weight is zero.
2. Trim text/code and lowercase the code. Exclude empty or whitespace-containing
   phrases and codes not matching `[a-y]{1,4}`. Thus the original conversions
   exclude multiword phrases, ideographic spaces, invalid codes, and `z` codes;
   this is not a claim that every upstream row is supported.
3. Group by code, deduplicating identical phrases within that code. Keep the
   highest-weight occurrence, retaining its source row for tie ordering.
4. Sort codes ordinally. Within each code, sort by descending weight and then
   source row. Emit `code phrase1 phrase2 ...`.

The core excludes 3 rows and removes 3 duplicate occurrences, yielding
**77,169 codes / 89,254 candidates**. The extension excludes 10 rows and removes
53 duplicate occurrences, yielding **2,098 codes / 2,216 candidates**.

Canonical converted-body hashes (exclude blank/comment lines; UTF-8, LF,
one final LF) are:

| Local file | SHA-256 |
| --- | --- |
| `data/wubi86_jidian.txt` | `6c284695b49fcd49d33b145f17f4b748ffdd198bb8db64f547435ba3ababb22e` |
| `data/wubi86_jidian_extra.txt` | `2aaf564d148ae402c44bab3e55f1538830c04ec79ac724fd9a6bd93433ff6515` |

To reproduce the check from a source checkout, clone into a **new sibling
directory**, check out the pinned revision, then run the offline verifier:

```powershell
git clone --no-checkout https://github.com/KyleBing/rime-wubi86-jidian.git ..\qiqi-jidian-upstream
git -C ..\qiqi-jidian-upstream checkout --detach 18a6016c3d08a20f713f8f8e1791a2e79e0be081
powershell -NoLogo -NoProfile -File scripts\verify_jidian_provenance.ps1 -UpstreamDirectory ..\qiqi-jidian-upstream
```

The verifier checks source/license hashes and every converted data row; it does
not download, rewrite, register, or install anything. Its script is available
from the source repository rather than bundled with the runtime installers.

### Historical identity and later license change

The original local import
[`1e3d079`](https://github.com/cloudfyy/qiqiime/commit/1e3d079de886b5197c21236bde3c89a03f42af3e)
did not record its download revision. The exact content match establishes a
reproducible Apache-2.0 source baseline, **not the unique original download
commit**: multiple revisions may contain identical dictionary blobs.

Upstream's later snapshot has an
[MIT license](https://github.com/KyleBing/rime-wubi86-jidian/blob/4d08110b8080f6577dad733ada7e5ae5c5e878fd/LICENSE);
the license-changing commit is
[`33a1f943`](https://github.com/KyleBing/rime-wubi86-jidian/commit/33a1f9430fa9bcb30b2f32b698e4e497f8864091).
The current MIT text, including `Copyright (c) Kyle Bing`, is reproduced in
[licenses/rime-wubi86-jidian-MIT.txt](licenses/rime-wubi86-jidian-MIT.txt)
for historical comparison, not as a replacement license for these conversions.
The dictionaries retain Apache-2.0 attribution. Future updates must record the
source revision, applicable license/notices, and conversion changes together.

## Haifeng Wubi 86 dictionary

- Upstream: [definite/ibus-table-chinese, tables/wubi-haifeng](https://github.com/definite/ibus-table-chinese/tree/master/tables/wubi-haifeng).
- Attribution: `Copyright (c) 2010, SUN Haifeng (孙海峰)`. The bundled header also
  acknowledges contributors from the Wubi enthusiasts forum and Shenzhen University.
- Distributed conversion: `data/wubi86_haifeng.txt`, converted to QiQiIME's
  code-to-phrase text format while retaining Haifeng ordering/weights.
- License: **BSD 3-Clause**, including the non-endorsement clause and full
  disclaimer, reproduced from
  [pinned upstream COPYING](https://github.com/definite/ibus-table-chinese/blob/bac13d268344311d48ad9bbc09c7df48b304066f/tables/wubi-haifeng/COPYING)
  in [licenses/wubi-haifeng-BSD-3-Clause.txt](licenses/wubi-haifeng-BSD-3-Clause.txt).
  The complete copyright, conditions, and disclaimer are also retained as
  comments in the converted dictionary's header.

## Microsoft Guidelines Support Library (GSL)

- Upstream: [microsoft/GSL](https://github.com/microsoft/GSL).
- Dependency: `ms-gsl` in the vcpkg manifest, used by the C++ application code.
- Attribution: `Copyright (c) 2015 Microsoft Corporation. All rights reserved.`
- License: **MIT**, reproduced in
  [licenses/microsoft-gsl-MIT.txt](licenses/microsoft-gsl-MIT.txt).
  The text was checked against the locally installed vcpkg package's copyright file.

## Build tools and platform components

Windows components, MSVC, CMake, vcpkg, WiX, GitHub Actions, and signing services
have their own terms. Listing these tools does not imply they are bundled as
third-party application binaries or covered by QiQiIME's GPL. Keep required
notices for any additional material actually redistributed in a future release.

## 中文说明

本项目 GPL 不替代第三方许可。两份极点词库已按固定上游 `18a6016c` 的 Apache-2.0
快照精确重建，词条及顺序完全一致；上文给出源文件/正文哈希、过滤规则和离线复核方法。
这确立可复现来源，不捏造当年未记录的原下载提交，也不改用上游后来的 MIT 许可。
海峰词库文件头和随包文件均保留完整 BSD 三条款及免责声明，GSL 附 MIT 全文。
源码和后续 ZIP/MSI 均应保留这些声明与许可文件；更新时需同步核对。
