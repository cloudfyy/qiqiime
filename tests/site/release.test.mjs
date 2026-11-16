// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { createReleaseData } from "../../scripts/site_release.mjs";
import { validateReleaseData } from "../../site/assets/release-data.js";
import { releaseFixture } from "./fixtures.mjs";

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

test("release metadata ignores non-installer assets without changing the four downloads", () => {
  const release = releaseFixture();
  const expected = createReleaseData(release);
  release.assets.push({ name: "debug_symbols.7z" }, { name: "source.zip" });
  assert.deepEqual(createReleaseData(release), expected);
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
  for (const change of [
    { architecture: "arm64" }, { language: "ja-JP" }, { name: "unrelated.msi" },
  ]) {
    const data = createReleaseData(releaseFixture());
    Object.assign(data.installers[0], change);
    assert.throws(() => validateReleaseData(data), /Unsupported installer|filename does not match/);
  }
});

test("release CLI accepts a UTF-8 BOM and writes the validated download metadata", async (t) => {
  const directory = await mkdtemp(path.join(tmpdir(), "qiqi-release-cli-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const input = path.join(directory, "release.json");
  const output = path.join(directory, "downloads.json");
  const release = releaseFixture();
  await writeFile(input, `\uFEFF${JSON.stringify(release)}`, "utf8");
  const cli = fileURLToPath(new URL("../../scripts/site_release.mjs", import.meta.url));
  const result = spawnSync(process.execPath, [cli, input, output], { encoding: "utf8" });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  const written = await readFile(output, "utf8");
  assert.equal(written, `${JSON.stringify(createReleaseData(release), null, 2)}\n`);
});

test("release CLI rejects bad usage or input without replacing the last good downloads", async (t) => {
  const directory = await mkdtemp(path.join(tmpdir(), "qiqi-release-cli-invalid-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const input = path.join(directory, "release.json");
  const output = path.join(directory, "downloads.json");
  const cli = fileURLToPath(new URL("../../scripts/site_release.mjs", import.meta.url));
  const previous = `${JSON.stringify(createReleaseData(releaseFixture()), null, 2)}\n`;
  await writeFile(output, previous, "utf8");
  await writeFile(input, JSON.stringify(releaseFixture()), "utf8");
  const incomplete = releaseFixture();
  incomplete.assets.shift();
  const cases = [
    { args: [], expected: /Usage:/ },
    { args: [input], expected: /Usage:/ },
    { args: [input, output, "extra"], expected: /Usage:/ },
    { args: [input, output], content: "{", expected: /SyntaxError/ },
    { args: [input, output], content: JSON.stringify(incomplete), expected: /all four/ },
  ];
  for (const { args, content, expected } of cases) {
    if (content !== undefined) await writeFile(input, content, "utf8");
    const result = spawnSync(process.execPath, [cli, ...args], { encoding: "utf8" });
    assert.ifError(result.error);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, expected);
    assert.equal(await readFile(output, "utf8"), previous);
  }
});
