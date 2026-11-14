// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { initializeAppearance } from "./appearance.js";

export const english = new URLSearchParams(location.search).get("lang") === "en";
export const locale = english ? "en-US" : "zh-CN";

document.documentElement.lang = english ? "en" : "zh-CN";

function withLanguage(href, language) {
  const url = new URL(href, location.href);
  url.searchParams.set("lang", language);
  return url.href;
}

if (english) {
  document.querySelectorAll("[data-en]").forEach((element) => {
    element.textContent = element.dataset.en;
  });
  document.querySelectorAll("[data-en-label]").forEach((element) => {
    element.setAttribute("aria-label", element.dataset.enLabel);
  });
  document.querySelectorAll("meta[data-en-content]").forEach((element) => {
    element.setAttribute("content", element.dataset.enContent);
  });
  for (const [property, content] of [
    ["og:title", document.title],
    ["og:description", document.querySelector('meta[name="description"]')?.content],
  ]) {
    const meta = document.querySelector(`meta[property="${property}"]`);
    if (meta) {
      meta.content = content;
    }
  }
  document.querySelectorAll("a[data-site-link]").forEach((link) => {
    link.href = withLanguage(link.getAttribute("href"), "en");
  });
}

const languageLink = document.getElementById("language-switch");
if (languageLink) {
  languageLink.href = withLanguage(location.href, english ? "zh-CN" : "en");
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

initializeAppearance(english);
