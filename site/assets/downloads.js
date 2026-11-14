// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { validateReleaseData } from "./release-data.js";

const english = new URLSearchParams(location.search).get("lang") === "en";
const panel = document.getElementById("latest-release");
const status = document.getElementById("release-status");

async function loadDownloads() {
  let release;
  try {
    const response = await fetch(new URL("../release.json", import.meta.url), {
      cache: "no-cache",
      signal: AbortSignal.timeout(10000),
    });
    if (!response.ok) {
      throw new Error(`Release metadata request failed: HTTP ${response.status}`);
    }
    release = validateReleaseData(await response.json());
  } catch (error) {
    console.error("Could not load verified release downloads:", error);
    status.textContent = english
      ? "Quick downloads are unavailable. Use the official release page above to download the installer."
      : "快捷下载信息暂时不可用，请使用上方官方发布页下载安装包。";
    panel.setAttribute("aria-busy", "false");
    return;
  }

  const installers = new Map(release.installers.map((installer) => [
    `${installer.architecture}/${installer.language}`, installer,
  ]));
  const preferredLanguage = english ? "en-US" : "zh-CN";
  document.querySelectorAll("[data-installer]").forEach((link) => {
    const installer = installers.get(link.dataset.installer);
    link.href = installer.url;
    link.classList.toggle("secondary", installer.language !== preferredLanguage);
    if (installer.language === preferredLanguage) {
      link.parentElement.prepend(link);
    }
  });
  document.getElementById("release-version").textContent = release.tag;
  document.getElementById("latest-release-link").href = release.releaseUrl;
  const date = new Intl.DateTimeFormat(english ? "en-US" : "zh-CN", {
    dateStyle: "medium", timeZone: "UTC",
  }).format(new Date(release.publishedAt));
  status.textContent = english ? `Published ${date}` : `发布于 ${date}`;
  document.getElementById("release-downloads").hidden = false;
  panel.setAttribute("aria-busy", "false");
}

await loadDownloads();
