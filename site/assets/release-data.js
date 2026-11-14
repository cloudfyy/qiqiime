// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

export function validateReleaseData(data) {
  const tag = /^v?(\d+\.\d+\.\d+)$/.exec(data?.tag ?? "");
  if (data?.schemaVersion !== 1 || !tag || data.version !== tag[1]) {
    throw new Error("Expected versioned metadata for a stable release.");
  }
  if (typeof data.publishedAt !== "string" ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(data.publishedAt) ||
      !Number.isFinite(Date.parse(data.publishedAt))) {
    throw new Error("Invalid release publication date.");
  }
  const releaseUrl = new URL(data.releaseUrl);
  const releasePath = /^\/([\w.-]+\/[\w.-]+)\/releases\/tag\/(v?\d+\.\d+\.\d+)$/.exec(releaseUrl.pathname);
  if (releaseUrl.origin !== "https://github.com" || releaseUrl.username || releaseUrl.password ||
      releaseUrl.search || releaseUrl.hash || !releasePath || releasePath[2] !== data.tag) {
    throw new Error("Expected an HTTPS GitHub release URL matching the release tag.");
  }
  if (!Array.isArray(data.installers) || data.installers.length !== 4) {
    throw new Error("Expected all four x64/x86 Chinese/English MSI installers.");
  }
  const slots = new Set();
  for (const installer of data.installers) {
    if (!["x64", "x86"].includes(installer?.architecture) ||
        !["zh-CN", "en-US"].includes(installer?.language)) {
      throw new Error("Unsupported installer architecture or language.");
    }
    const slot = `${installer.architecture}/${installer.language}`;
    if (slots.has(slot)) {
      throw new Error(`Duplicate installer: ${slot}`);
    }
    slots.add(slot);
    const suffix = `-win-${installer.architecture}-${installer.language}.msi`;
    if (![`QiQiIME-v${data.version}${suffix}`, `QiQiIME-${data.version}${suffix}`].includes(installer.name)) {
      throw new Error(`Installer filename does not match the release: ${slot}`);
    }
    const expectedUrl = `${releaseUrl.origin}/${releasePath[1]}/releases/download/${data.tag}/${installer.name}`;
    if (installer.url !== expectedUrl) {
      throw new Error(`Installer URL does not match its repository, tag, and filename: ${slot}`);
    }
  }
  return data;
}
