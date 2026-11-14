// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { test as base, expect } from "@playwright/test";
import { createReleaseData } from "../../scripts/site_release.mjs";
import { releaseFixture } from "./fixtures.mjs";

const release = createReleaseData(releaseFixture());
const test = base.extend({
  page: async ({ page, baseURL }, use) => {
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.route("**/*", (route) =>
      new URL(route.request().url()).origin === new URL(baseURL).origin
        ? route.continue()
        : route.abort("blockedbyclient"));
    await page.route(new URL("release.json", baseURL).href, (route) => route.fulfill({ json: release }));
    await use(page);
    expect(errors, "unexpected page JavaScript errors").toEqual([]);
  },
});

for (const language of ["zh-CN", "en"]) {
  const english = language === "en";
  test.describe(language, () => {
    test("navigation, translated metadata, screenshots, and viewport layout", async ({ page, baseURL }) => {
      for (const name of ["index.html", "install.html", "develop.html", "feedback.html"]) {
        const response = await page.goto(`${name}?source=test&lang=${language}#main`);
        expect(response.status()).toBe(200);
        await expect(page.locator("html")).toHaveAttribute("lang", language);
        await expect(page.locator('meta[property="og:title"]')).toHaveAttribute("content", await page.title());
        await expect(page.locator('meta[property="og:description"]')).toHaveAttribute(
          "content", await page.locator('meta[name="description"]').getAttribute("content"));
        await expect(page.locator("nav [aria-current=page]")).toHaveCount(1);
        const links = await page.locator("a[data-site-link]").evaluateAll((anchors) => anchors.map((a) => a.href));
        for (const href of links) {
          expect(href.startsWith(baseURL)).toBe(true);
          if (english) expect(new URL(href).searchParams.get("lang")).toBe("en");
        }
        for (const image of await page.locator(".software-shot img").all()) {
          await image.scrollIntoViewIfNeeded();
          await expect(image).toHaveJSProperty("complete", true);
          expect(await image.evaluate((img) => img.naturalWidth)).toBeGreaterThan(0);
        }
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
      }
      const current = new URL(page.url());
      current.searchParams.set("lang", english ? "zh-CN" : "en");
      await page.locator("#language-switch").click();
      await expect(page).toHaveURL(current.href);
      await expect(page.locator("html")).toHaveAttribute("lang", english ? "zh-CN" : "en");
      await page.getByRole("navigation").getByRole("link", {
        name: english ? "软件安装" : "Install", exact: true,
      }).click();
      const destination = new URL(page.url());
      expect(destination.pathname).toBe("/qiqiime/install.html");
      expect(destination.searchParams.get("lang")).toBe(english ? null : "en");
    });

    test("downloads retain exact URLs and put the preferred installer first", async ({ page }) => {
      await page.goto(`install.html?lang=${language}`);
      await expect(page.locator("#latest-release")).toHaveAttribute("aria-busy", "false");
      await expect(page.locator("#release-downloads")).toBeVisible();
      await expect(page.locator("#release-version")).toHaveText(release.tag);
      await expect(page.locator("#latest-release-link")).toHaveAttribute("href", release.releaseUrl);
      const locale = english ? "en-US" : "zh-CN";
      const date = new Intl.DateTimeFormat(locale, { dateStyle: "medium", timeZone: "UTC" })
        .format(new Date(release.publishedAt));
      await expect(page.locator("#release-status")).toHaveText(english ? `Published ${date}` : `发布于 ${date}`);
      for (const installer of release.installers) {
        await expect(page.locator(`[data-installer="${installer.architecture}/${installer.language}"]`))
          .toHaveAttribute("href", installer.url);
      }
      for (const group of await page.locator(".installer-links").all()) {
        const first = group.locator("[data-installer]").first();
        await expect(first).toHaveAttribute("data-installer", new RegExp(`/${locale}$`));
        await expect(first).not.toHaveClass(/\bsecondary\b/);
        await expect(group.locator("[data-installer]").last()).toHaveClass(/\bsecondary\b/);
        await first.focus();
        await page.keyboard.press("Tab");
        await expect(group.locator("[data-installer]").last()).toBeFocused();
      }
    });

    test("download failures keep a localized message and the official fallback", async ({ page, baseURL }) => {
      const invalid = createReleaseData(releaseFixture());
      invalid.installers[0].url = "https://example.test/installer.msi";
      const failures = [
        (route) => route.fulfill({ status: 503, body: "Unavailable" }),
        (route) => route.fulfill({ contentType: "application/json", body: "{" }),
        (route) => route.fulfill({ json: invalid }),
        (route) => route.abort("timedout"),
      ];
      const url = new URL("release.json", baseURL).href;
      for (const failure of failures) {
        await page.unroute(url);
        await page.route(url, failure);
        await page.goto(`install.html?lang=${language}`);
        await expect(page.locator("#latest-release")).toHaveAttribute("aria-busy", "false");
        await expect(page.locator("#release-downloads")).toBeHidden();
        await expect(page.locator("#release-status")).toHaveText(english
          ? "Quick downloads are unavailable. Use the official release page above to download the installer."
          : "快捷下载信息暂时不可用，请使用上方官方发布页下载安装包。");
        await expect(page.locator("#latest-release-link"))
          .toHaveAttribute("href", "https://github.com/cloudfyy/qiqiime/releases/latest");
        await expect(page.locator("[data-installer][href]")).toHaveCount(0);
      }
    });

    test("copy reports success and failure with one handler per click", async ({ page }) => {
      await page.addInitScript(() => {
        window.copyAttempts = [];
        window.rejectCopy = false;
        Object.defineProperty(navigator, "clipboard", {
          configurable: true,
          value: {
            writeText: async (text) => {
              window.copyAttempts.push(text);
              if (window.rejectCopy) throw new Error("Simulated clipboard permission denial");
            },
          },
        });
      });
      await page.goto(`develop.html?lang=${language}`);
      await page.locator('[data-copy-target="clone-command"]').click();
      await expect(page.locator("#copy-status")).toHaveText(english ? "Command copied." : "命令已复制。");
      expect(await page.evaluate(() => window.copyAttempts))
        .toEqual([await page.locator("#clone-command").textContent()]);
      await page.evaluate(() => { window.rejectCopy = true; });
      await page.locator('[data-copy-target="build-command"]').click();
      await expect(page.locator("#copy-status")).toHaveText(english
        ? "Copy failed. Select the command and copy it manually."
        : "复制失败，请选中命令并手动复制。");
      expect(await page.evaluate(() => window.copyAttempts.length)).toBe(2);
    });

    test("nested 404 preserves status, language, assets, and keyboard recovery", async ({ page, baseURL }) => {
      const response = await page.goto(`missing/deep/page?source=test&lang=${language}`);
      expect(response.status()).toBe(404);
      await expect(page.locator("html")).toHaveAttribute("lang", language);
      await expect(page.locator('meta[name="robots"]')).toHaveAttribute("content", "noindex, follow");
      await expect(page.locator("nav [aria-current]")).toHaveCount(0);
      expect(await page.locator('link[rel="stylesheet"]').evaluate((link) => Boolean(link.sheet))).toBe(true);
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
      await page.locator(".skip").focus();
      await page.keyboard.press("Enter");
      expect(new URL(page.url()).hash).toBe("#main");
      await page.getByRole("link", { name: english ? "Back to overview" : "返回项目首页", exact: true }).focus();
      await page.keyboard.press("Enter");
      await expect(page).toHaveURL(new URL(`index.html${english ? "?lang=en" : ""}`, baseURL).href);
      await expect(page.locator("html")).toHaveAttribute("lang", language);
    });
  });
}

test.describe("without JavaScript", () => {
  test.use({ javaScriptEnabled: false });
  test("Chinese navigation and official downloads remain usable", async ({ page, baseURL }) => {
    const response = await page.goto("missing/deep/page?lang=en");
    expect(response.status()).toBe(404);
    await expect(page.locator("html")).toHaveAttribute("lang", "zh-CN");
    await page.getByRole("link", { name: "返回项目首页", exact: true }).click();
    await expect(page).toHaveURL(new URL("index.html", baseURL).href);
    await page.getByRole("navigation").getByRole("link", { name: "软件安装", exact: true }).click();
    await expect(page.locator("#release-downloads")).toBeHidden();
    await expect(page.locator("#latest-release-link"))
      .toHaveAttribute("href", "https://github.com/cloudfyy/qiqiime/releases/latest");
    await expect(page.locator("noscript")).toBeVisible();
  });
});
