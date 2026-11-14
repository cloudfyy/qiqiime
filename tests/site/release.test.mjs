// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import test from "node:test";
import { createReleaseData } from "../../scripts/site_release.mjs";
import { validateReleaseData } from "../../site/assets/release-data.js";

function releaseFixture(tag = "v2.3.4") {
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

test("latest-release data uses the published version and all four actual MSI URLs", () => {
  for (const tag of ["v2.3.4", "2.3.4"]) {
    const release = releaseFixture(tag);
    const data = createReleaseData(release);
    assert.equal(data.tag, tag);
    assert.equal(data.version, "2.3.4");
    assert.deepEqual(data.installers.map((installer) => `${installer.architecture}/${installer.language}`).sort(),
      ["x64/en-US", "x64/zh-CN", "x86/en-US", "x86/zh-CN"]);
    for (const installer of data.installers) {
      assert.equal(installer.url, release.assets.find((asset) => asset.name === installer.name).browser_download_url);
    }
  }
});

test("drafts, prereleases, and incomplete or mismatched installers cannot replace website downloads", () => {
  for (const field of ["draft", "prerelease"]) {
    const release = releaseFixture();
    release[field] = true;
    assert.throws(() => createReleaseData(release), /published, stable/);
  }
  const invalidTag = releaseFixture();
  invalidTag.tag_name = "v2.3.4-rc.1";
  assert.throws(() => createReleaseData(invalidTag), /stable numeric release tag/);
  const missing = releaseFixture();
  missing.assets.shift();
  assert.throws(() => createReleaseData(missing), /all four/);
  const duplicate = createReleaseData(releaseFixture());
  duplicate.installers[1] = { ...duplicate.installers[0] };
  assert.throws(() => validateReleaseData(duplicate), /Duplicate installer/);
  for (const change of [
    { state: "new" }, { size: 0 }, { name: "QiQiIME-v2.3.3-win-x64-zh-CN.msi" },
  ]) {
    const release = releaseFixture();
    Object.assign(release.assets[0], change);
    assert.throws(() => createReleaseData(release), /mismatched or not fully uploaded/);
  }
});

test("download URLs must match the release repository, tag, and installer name exactly", () => {
  for (const url of [
    "https://example.test/installer.msi",
    "http://github.com/cloudfyy/qiqiime/releases/download/v2.3.4/QiQiIME-v2.3.4-win-x64-zh-CN.msi",
    "https://github.com/cloudfyy/qiqiime/releases/download/v2.3.3/QiQiIME-v2.3.4-win-x64-zh-CN.msi",
    "https://github.com/other/project/releases/download/v2.3.4/QiQiIME-v2.3.4-win-x64-zh-CN.msi",
    "https://github.com/cloudfyy/qiqiime/releases/download/v2.3.4/QiQiIME-v2.3.4-win-x64-zh-CN.msi?token=example",
  ]) {
    const release = releaseFixture();
    release.assets[0].browser_download_url = url;
    assert.throws(() => createReleaseData(release), /Installer URL/);
  }
  const wrongRelease = releaseFixture();
  wrongRelease.html_url = "https://example.test/releases/tag/v2.3.4";
  assert.throws(() => createReleaseData(wrongRelease), /HTTPS GitHub release URL/);
});

test("browser validation rejects incompatible metadata and invalid publication dates", () => {
  for (const change of [
    { schemaVersion: 2 }, { version: "2.3.3" }, { publishedAt: "not a date" },
  ]) {
    const data = { ...createReleaseData(releaseFixture()), ...change };
    assert.throws(() => validateReleaseData(data));
  }
});
