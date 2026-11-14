// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

export function releaseFixture(tag = "v2.3.4") {
  const version = tag.replace(/^v/, "");
  const names = ["x64", "x86"].flatMap((architecture) =>
    ["zh-CN", "en-US"].map((language) => `QiQiIME-v${version}-win-${architecture}-${language}.msi`));
  names.push(`QiQiIME-v${version}-win-x64-x86.zip`, `QiQiIME-v${version}-win-x64-x86-symbols.zip`);
  return {
    tag_name: tag,
    html_url: `https://github.com/cloudfyy/qiqiime/releases/tag/${tag}`,
    published_at: "2026-11-14T01:28:20Z",
    draft: false,
    prerelease: false,
    assets: names.map((name) => ({
      name, state: "uploaded", size: 1024,
      browser_download_url: `https://github.com/cloudfyy/qiqiime/releases/download/${tag}/${name}`,
    })),
  };
}
