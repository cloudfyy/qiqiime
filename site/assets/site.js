// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

"use strict";

const english = new URLSearchParams(location.search).get("lang") === "en";
document.documentElement.lang = english ? "en" : "zh-CN";

if (english) {
  document.querySelectorAll("[data-en]").forEach((element) => {
    element.textContent = element.dataset.en;
  });
  document.querySelectorAll("[data-en-label]").forEach((element) => {
    element.setAttribute("aria-label", element.dataset.enLabel);
  });
  document.querySelectorAll("a[data-site-link]").forEach((link) => {
    const url = new URL(link.getAttribute("href"), location.href);
    url.searchParams.set("lang", "en");
    link.href = url.href;
  });
}

const languageLink = document.getElementById("language-switch");
if (languageLink) {
  const url = new URL(location.href);
  url.searchParams.set("lang", english ? "zh-CN" : "en");
  languageLink.href = url.href;
  languageLink.textContent = english ? "简体中文" : "EN";
  languageLink.setAttribute("lang", english ? "zh-CN" : "en");
  languageLink.setAttribute("aria-label", english ? "切换到简体中文" : "Switch to English");
}

document.querySelectorAll("[data-copy-target]").forEach((button) => {
  button.addEventListener("click", async () => {
    const code = document.getElementById(button.dataset.copyTarget);
    const status = document.getElementById("copy-status");
    try {
      if (!navigator.clipboard) {
        throw new Error("Clipboard API is unavailable");
      }
      await navigator.clipboard.writeText(code.textContent);
      status.textContent = english ? "Command copied." : "命令已复制。";
    } catch (error) {
      console.warn("Could not copy the command:", error);
      status.textContent = english
        ? "Copy failed. Select the command and copy it manually."
        : "复制失败，请选中命令并手动复制。";
    }
  });
});
