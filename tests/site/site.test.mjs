// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../../site/", import.meta.url));
const pages = (await readdir(root)).filter((name) => name.endsWith(".html"));
const contentPages = pages.filter((name) => name !== "404.html");
const origin = new URL("https://example.test/qiqiime/");
const documents = new Map(
  await Promise.all(pages.map(async (name) => [name, await readFile(path.join(root, name), "utf8")])),
);

test("local links and assets resolve below the project Pages path", async () => {
  for (const [name, html] of documents) {
    for (const [script] of html.matchAll(/<script\b[^>]*\bsrc="[^"]+"[^>]*>/g)) {
      assert.match(script, /\btype="module"/, `${name}: shared scripts must load as modules`);
    }
    const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
    assert.equal(new Set(ids).size, ids.length, `${name}: duplicate element ID`);
    for (const match of html.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
      const target = new URL(match[1], new URL(name, origin));
      if (target.origin !== origin.origin) {
        assert.equal(target.protocol, "https:", `${name}: insecure external link`);
        continue;
      }
      assert.ok(target.pathname.startsWith(origin.pathname), `${name}: escapes project path: ${match[1]}`);
      const relative = decodeURIComponent(target.pathname.slice(origin.pathname.length));
      assert.ok((await stat(path.join(root, relative))).isFile(), `${name}: missing target: ${match[1]}`);
      if (target.hash) {
        const document = documents.get(relative);
        assert.ok(document?.includes(`id="${target.hash.slice(1)}"`), `${name}: missing anchor: ${match[1]}`);
      }
    }
  }
});

test("translatable elements preserve markup and provide nonempty English text", () => {
  for (const [name, html] of documents) {
    const translations = [...html.matchAll(/<([a-z][\w-]*)\b[^>]*\bdata-en="([^"]*)"[^>]*>([\s\S]*?)<\/\1>/g)];
    assert.ok(translations.length > 0, `${name}: missing translations`);
    for (const [, , english, original] of translations) {
      assert.ok(english.trim(), `${name}: empty English translation`);
      assert.ok(original.trim(), `${name}: empty default text`);
      assert.ok(!/<[a-z][\s\S]*?>/i.test(original), `${name}: translation would remove child markup`);
    }
    for (const match of html.matchAll(/aria-label="[^"]*[\u3400-\u9fff][^"]*"/g)) {
      const tagStart = html.lastIndexOf("<", match.index);
      const tagEnd = html.indexOf(">", match.index);
      assert.match(html.slice(tagStart, tagEnd), /data-en-label=/, `${name}: untranslated accessible label`);
    }
  }
});

test("navigation remains usable without JavaScript", () => {
  for (const [name, html] of documents) {
    assert.match(html, /<html lang="zh-CN">/, `${name}: Chinese must be the default`);
    const navigation = html.match(/<nav\b[^>]*>([\s\S]*?)<\/nav>/)?.[1];
    assert.ok(navigation, `${name}: missing navigation`);
    const currentLinks = [...navigation.matchAll(/<a\b[^>]*aria-current="page"[^>]*>/g)];
    assert.equal(currentLinks.length, name === "404.html" ? 0 : 1, `${name}: incorrect current page count`);
    if (name !== "404.html") {
      assert.ok(currentLinks[0][0].includes(`href="${name}"`), `${name}: incorrect current page`);
    }
    const destinations = [...navigation.matchAll(/\bhref="([^"]+)"/g)]
      .map((match) => new URL(match[1], new URL(name, origin)).pathname);
    for (const page of contentPages) {
      assert.ok(destinations.includes(new URL(page, origin).pathname), `${name}: missing navigation to ${page}`);
    }
  }
});

test("indexed pages share canonical URLs, localized summaries, and an exact sitemap", async () => {
  const publicRoot = new URL("https://cloudfyy.github.io/qiqiime/");
  const expectedUrls = [];
  for (const name of contentPages) {
    const html = documents.get(name);
    const expected = new URL(name === "index.html" ? "./" : name, publicRoot).href;
    expectedUrls.push(expected);
    const canonicals = [...html.matchAll(/<link rel="canonical" href="([^"]+)"/g)];
    assert.deepEqual(canonicals.map((match) => match[1]), [expected], `${name}: incorrect canonical`);
    assert.doesNotMatch(html, /noindex/i, `${name}: content should be indexable`);
    assert.match(html, /<link rel="sitemap" type="application\/xml" href="sitemap\.xml">/);
    const meta = new Map([...html.matchAll(/<meta (?:name|property)="([^"]+)" content="([^"]+)"(?: data-en-content="([^"]+)")?/g)]
      .map(([, key, content, english]) => [key, { content, english }]));
    assert.equal(meta.get("og:url")?.content, expected);
    assert.equal(meta.get("og:type")?.content, "website");
    assert.equal(meta.get("og:locale")?.content, "zh_CN");
    assert.equal(meta.get("og:locale")?.english, "en_US");
    assert.equal(meta.get("twitter:card")?.content, "summary");
    const title = html.match(/<title data-en="([^"]+)">([^<]+)<\/title>/);
    assert.ok(title, `${name}: missing localized title`);
    assert.equal(meta.get("og:title")?.content, title[2]);
    const description = meta.get("description");
    assert.ok(description?.content.trim() && description?.english?.trim(), `${name}: missing localized description`);
    assert.equal(meta.get("og:description")?.content, description.content, `${name}: inconsistent sharing summary`);
  }
  const sitemap = await readFile(path.join(root, "sitemap.xml"), "utf8");
  assert.match(sitemap, /<urlset xmlns="http:\/\/www\.sitemaps\.org\/schemas\/sitemap\/0\.9">/);
  const locations = [...sitemap.matchAll(/<loc>([^<]+)<\/loc>/g)].map((match) => match[1]);
  assert.deepEqual(locations.sort(), expectedUrls.sort(), "sitemap must list canonical content pages exactly once");
});

test("shared page module localizes metadata and preserves queries and anchors in navigation", async (t) => {
  const globals = Object.fromEntries(["document", "location"]
    .map((key) => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  t.after(() => {
    for (const [key, descriptor] of Object.entries(globals)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  });
  function element(properties) {
    return {
      getAttribute(name) { return this[name]; },
      setAttribute(name, value) { this[name] = value; },
      ...properties,
    };
  }
  const cases = [
    { language: "en", metadata: true },
    { language: "zh-CN", metadata: true },
    { language: "fr", metadata: true },
    { language: "en", metadata: false },
  ];
  for (const [index, { language, metadata }] of cases.entries()) {
    const english = language === "en";
    const title = element({ textContent: "中文标题", dataset: { en: "English title" } });
    const description = element({ content: "中文摘要", dataset: { enContent: "English summary" } });
    const label = element({ "aria-label": "主导航", dataset: { enLabel: "Main navigation" } });
    const ogTitle = element({ content: title.textContent });
    const ogDescription = element({ content: description.content });
    const navigation = element({ href: "/qiqiime/install.html?source=nav#choose-title" });
    const languageLink = element({});
    const selectors = {
      "[data-en]": [title],
      "[data-en-label]": [label],
      "meta[data-en-content]": metadata ? [description] : [],
      "a[data-site-link]": [navigation],
      "[data-copy-target]": [],
    };
    const meta = metadata ? {
      'meta[name="description"]': description,
      'meta[property="og:title"]': ogTitle,
      'meta[property="og:description"]': ogDescription,
    } : {};
    globalThis.location = new URL(`https://example.test/qiqiime/missing/deep/page?source=help&lang=${language}#main`);
    globalThis.document = {
      documentElement: {},
      get title() { return title.textContent; },
      querySelectorAll(selector) { return selectors[selector] ?? []; },
      querySelector(selector) { return meta[selector] ?? null; },
      getElementById(id) { return id === "language-switch" ? languageLink : null; },
    };
    const shared = await import(new URL(`../../site/assets/site.js?language-test=${index}`, import.meta.url));
    assert.equal(shared.english, english);
    assert.equal(shared.locale, english ? "en-US" : "zh-CN");
    assert.equal(document.documentElement.lang, english ? "en" : "zh-CN");
    assert.equal(title.textContent, english ? "English title" : "中文标题");
    assert.equal(label["aria-label"], english ? "Main navigation" : "主导航");
    if (metadata) {
      assert.equal(ogTitle.content, title.textContent);
      assert.equal(ogDescription.content, english ? "English summary" : "中文摘要");
    }
    const destination = new URL(navigation.href, location.href);
    assert.equal(destination.pathname, "/qiqiime/install.html");
    assert.equal(destination.searchParams.get("source"), "nav");
    assert.equal(destination.hash, "#choose-title");
    assert.equal(destination.searchParams.get("lang"), english ? "en" : null);
    const switched = new URL(languageLink.href);
    assert.equal(switched.pathname, location.pathname);
    assert.equal(switched.hash, location.hash);
    assert.equal(switched.searchParams.get("source"), "help");
    assert.equal(switched.searchParams.getAll("lang").length, 1);
    assert.equal(switched.searchParams.get("lang"), english ? "zh-CN" : "en");
  }
});

test("404 recovery links and assets work from nested missing URLs without redirects", () => {
  const html = documents.get("404.html");
  assert.ok(html, "missing custom 404 page");
  assert.match(html, /<meta name="robots" content="noindex, follow">/);
  assert.doesNotMatch(html, /rel="canonical"|http-equiv="refresh"|<base\b/i);
  const nested = new URL("missing/deep/page?lang=en", origin);
  for (const [, target] of html.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
    if (/^(?:https:|#|\?)/.test(target)) continue;
    const url = new URL(target, nested);
    assert.equal(url.origin, origin.origin);
    assert.ok(url.pathname.startsWith(origin.pathname), `404 escapes the project path: ${target}`);
    assert.equal(url.href, new URL(target, new URL("404.html", origin)).href, `404 target depends on missing URL depth: ${target}`);
  }
  assert.match(html, /href="\/qiqiime\/index\.html" data-site-link/);
  assert.match(html, /src="\/qiqiime\/assets\/site\.js"/);
});

test("software screenshots have full-size accessible links and accurate image dimensions", async () => {
  const screenshots = [...documents.values()].flatMap((html) =>
    [...html.matchAll(/<figure\b[^>]*class="[^"]*\bsoftware-shot\b[^"]*"[^>]*>([\s\S]*?)<\/figure>/g)],
  );
  assert.ok(screenshots.length > 0, "missing software screenshots");
  for (const [, figure] of screenshots) {
    const image = figure.match(/<img\b[^>]*src="([^"]+)"[^>]*width="(\d+)"[^>]*height="(\d+)"[^>]*>/);
    assert.ok(image, "screenshot must declare intrinsic dimensions");
    const [, source, width, height] = image;
    assert.ok(figure.includes(`href="${source}"`), `${source}: missing full-size link`);
    assert.match(figure, /<a\b[^>]*aria-label="[^"]+"[^>]*data-en-label="[^"]+"/, `${source}: missing localized link description`);
    const png = await readFile(path.join(root, source));
    assert.equal(png.subarray(0, 8).toString("hex"), "89504e470d0a1a0a", `${source}: not a PNG image`);
    assert.equal(png.readUInt32BE(16), Number(width), `${source}: incorrect width`);
    assert.equal(png.readUInt32BE(20), Number(height), `${source}: incorrect height`);
  }
});
