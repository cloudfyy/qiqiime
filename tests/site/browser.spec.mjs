// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { test as base, expect } from "@playwright/test";
import { createReleaseData } from "../../scripts/site_release.mjs";
import { releaseFixture } from "./fixtures.mjs";

const release = createReleaseData(releaseFixture());
function contrastRatio(foreground, background) {
  const luminance = (color) => {
    const channels = color.match(/[\d.]+/g).slice(0, 3).map((value) => {
      const component = Number(value) / 255;
      return component <= 0.04045 ? component / 12.92 : ((component + 0.055) / 1.055) ** 2.4;
    });
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
  };
  const values = [luminance(foreground), luminance(background)].sort((a, b) => b - a);
  return (values[0] + 0.05) / (values[1] + 0.05);
}
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
    test("themes preserve readable contrast and follow system changes only when selected", async ({ page }) => {
      await page.goto(`index.html?lang=${language}`);
      await page.locator(".appearance-details summary").click();
      await expect(page.locator("#appearance-theme")).toHaveAccessibleName(english ? "Theme" : "界面主题");
      for (const theme of ["light", "dark", "contrast"]) {
        await page.locator("#appearance-theme").selectOption(theme);
        await expect(page.locator("html")).toHaveAttribute("data-theme", theme);
        await page.evaluate(() => Promise.all(document.getAnimations().map((animation) => animation.finished)));
        const samples = await page.evaluate(() => {
          const sample = (selector, background) => ({
            selector,
            foreground: getComputedStyle(document.querySelector(selector)).color,
            background: getComputedStyle(document.querySelector(background)).backgroundColor,
          });
          return [
            sample("body", "body"), sample(".hero-note", "body"),
            sample("nav a", ".header"), sample(".card p", ".card"),
            sample(".actions .button", ".actions .button"),
            sample(".band p", ".band"), sample(".band .button", ".band .button"),
          ];
        });
        for (const sample of samples) {
          expect(contrastRatio(sample.foreground, sample.background), `${theme}: ${sample.selector}`)
            .toBeGreaterThanOrEqual(theme === "contrast" ? 7 : 4.5);
        }
      }
      await page.emulateMedia({ colorScheme: "dark" });
      await expect(page.locator("html")).toHaveAttribute("data-theme", "contrast");
      await page.locator("#appearance-theme").selectOption("system");
      await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
      await page.emulateMedia({ colorScheme: "light" });
      await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
    });

    test("easy reading and 200 percent text keep controls usable and persist across pages", async ({ page }) => {
      await page.goto(`index.html?lang=${language}`);
      await page.locator("#easy-reading").focus();
      await page.keyboard.press("Space");
      await expect(page.locator("#easy-reading")).toBeChecked();
      await expect(page.locator("html")).toHaveAttribute("data-font-scale", "150");
      await expect(page.locator(".demo")).toBeHidden();
      for (const button of await page.locator("a.button:visible").all()) {
        const bounds = await button.boundingBox();
        expect(bounds.width).toBeGreaterThanOrEqual(44);
        expect(bounds.height).toBeGreaterThanOrEqual(44);
      }
      const focused = await page.locator("#easy-reading").evaluate((input) => {
        const style = getComputedStyle(input);
        return { width: parseFloat(style.outlineWidth), style: style.outlineStyle };
      });
      expect(focused.width).toBeGreaterThanOrEqual(3);
      expect(focused.style).toBe("solid");
      await page.locator(".appearance-details summary").click();
      await page.locator("#appearance-theme").selectOption("contrast");
      for (const scale of ["100", "125", "150", "200"]) {
        await page.locator("#appearance-scale").selectOption(scale);
        for (const name of ["index.html", "install.html", "develop.html", "feedback.html", "missing/deep/page"]) {
          await page.goto(`${name}?lang=${language}`);
          await expect(page.locator("html")).toHaveAttribute("data-font-scale", scale);
          expect(await page.locator("html").evaluate((root) => parseFloat(getComputedStyle(root).fontSize)))
            .toBe(Number(scale) * 0.16);
          await expect(page.locator("html")).toHaveAttribute("data-theme", "contrast");
          await expect(page.locator("#easy-reading")).toBeChecked();
          expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
        }
        await page.locator(".appearance-details summary").click();
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
        for (const control of await page.locator(".appearance-options select, .appearance-options button").all()) {
          const bounds = await control.boundingBox();
          expect(bounds.height).toBeGreaterThanOrEqual(44);
          expect(bounds.x).toBeGreaterThanOrEqual(0);
          expect(bounds.x + bounds.width).toBeLessThanOrEqual(page.viewportSize().width);
        }
      }
      await page.reload();
      await expect(page.locator("html")).toHaveAttribute("data-font-scale", "200");
      await page.locator(".appearance-details summary").click();
      await page.locator("#appearance-reset").click();
      await expect(page.locator("html")).toHaveAttribute("data-font-scale", "100");
      await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
      await expect(page.locator("#easy-reading")).not.toBeChecked();
      expect(await page.evaluate(() => localStorage.getItem("qiqi-display-v1"))).toBeNull();
    });

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

test("enlarged text also reflows without enabling easy reading", async ({ page }) => {
  await page.goto("index.html?lang=en");
  await page.locator(".appearance-details summary").click();
  for (const scale of ["125", "150", "200"]) {
    await page.locator("#appearance-scale").selectOption(scale);
    for (const name of ["index.html", "install.html", "develop.html", "feedback.html", "missing/deep/page"]) {
      await page.goto(`${name}?lang=en`);
      await expect(page.locator("#easy-reading")).not.toBeChecked();
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    }
    await page.locator(".appearance-details summary").click();
  }
});

test("OS forced colors and reduced motion take priority over display preferences", async ({ page }) => {
  await page.emulateMedia({ forcedColors: "active", reducedMotion: "reduce" });
  await page.goto("index.html?lang=en");
  await page.locator(".appearance-details summary").click();
  await page.locator("#appearance-theme").selectOption("dark");
  const state = await page.locator("body").evaluate((body) => {
    const sample = document.createElement("span");
    sample.style.backgroundColor = "Canvas";
    sample.style.color = "CanvasText";
    body.append(sample);
    const colors = {
      background: getComputedStyle(body).backgroundColor,
      expectedBackground: getComputedStyle(sample).backgroundColor,
      foreground: getComputedStyle(body).color,
      expectedForeground: getComputedStyle(sample).color,
      adjustment: getComputedStyle(body).forcedColorAdjust,
      transition: getComputedStyle(document.querySelector(".button")).transitionDuration,
    };
    sample.remove();
    return colors;
  });
  expect(state.background).toBe(state.expectedBackground);
  expect(state.foreground).toBe(state.expectedForeground);
  expect(state.adjustment).toBe("auto");
  expect(state.transition).toBe("0s");
});

test("corrupt stored display preferences produce a visible default-state warning", async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem("qiqi-display-v1", "{"));
  await page.goto("index.html?lang=en");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  await expect(page.locator("#appearance-status")).toContainText("could not be read");
  await page.locator("#easy-reading").check();
  await expect(page.locator("html")).toHaveAttribute("data-easy", "true");
  await expect(page.locator("#appearance-status")).toContainText("saved in this browser");
});

test("blocked storage does not prevent display changes and explicitly reports lost persistence", async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(window, "localStorage", {
    get() { throw new DOMException("Simulated storage denial", "SecurityError"); },
  }));
  await page.goto("index.html?lang=en");
  await expect(page.locator("#appearance-status")).toContainText("could not be read");
  await page.locator("#easy-reading").check();
  await expect(page.locator("html")).toHaveAttribute("data-font-scale", "150");
  await expect(page.locator("#appearance-status")).toContainText("could not be saved");
});

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
