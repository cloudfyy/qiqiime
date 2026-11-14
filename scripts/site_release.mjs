// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { readFile, writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import { validateReleaseData } from "../site/assets/release-data.js";

export function createReleaseData(release) {
  if (release?.draft !== false || release.prerelease !== false || !Array.isArray(release.assets)) {
    throw new Error("Only a published, stable GitHub release can supply website downloads.");
  }
  const tag = /^v?(\d+\.\d+\.\d+)$/.exec(release.tag_name ?? "");
  if (!tag) {
    throw new Error("Expected a stable numeric release tag.");
  }
  const installers = [];
  for (const asset of release.assets) {
    const match = /^QiQiIME-v?(\d+\.\d+\.\d+)-win-(x64|x86)-(zh-CN|en-US)\.msi$/.exec(asset.name ?? "");
    if (!match) {
      continue;
    }
    if (match[1] !== tag[1] || asset.state !== "uploaded" || !Number.isSafeInteger(asset.size) || asset.size <= 0) {
      throw new Error(`Installer is mismatched or not fully uploaded: ${asset.name}`);
    }
    installers.push({
      architecture: match[2],
      language: match[3],
      name: asset.name,
      url: asset.browser_download_url,
    });
  }
  installers.sort((left, right) => left.name.localeCompare(right.name, "en"));
  return validateReleaseData({
    schemaVersion: 1,
    version: tag[1],
    tag: release.tag_name,
    publishedAt: release.published_at,
    releaseUrl: release.html_url,
    installers,
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [inputPath, outputPath, ...extra] = process.argv.slice(2);
  if (!inputPath || !outputPath || extra.length) {
    throw new Error("Usage: node scripts/site_release.mjs <GitHub-release.json> <site/release.json>");
  }
  const release = JSON.parse((await readFile(inputPath, "utf8")).replace(/^\uFEFF/, ""));
  await writeFile(outputPath, `${JSON.stringify(createReleaseData(release), null, 2)}\n`, "utf8");
}
