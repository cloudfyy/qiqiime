// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../../site/", import.meta.url));
const pages = (await readdir(root)).filter((name) => name.endsWith(".html"));
const origin = new URL("https://example.test/qiqiime/");
const documents = new Map(
  await Promise.all(pages.map(async (name) => [name, await readFile(path.join(root, name), "utf8")])),
);

test("local links and assets resolve below the project Pages path", async () => {
  for (const [name, html] of documents) {
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
    assert.equal(currentLinks.length, 1, `${name}: current page must be unambiguous`);
    assert.ok(currentLinks[0][0].includes(`href="${name}"`), `${name}: incorrect current page`);
    for (const page of pages) {
      assert.ok(navigation.includes(`href="${page}"`), `${name}: missing navigation to ${page}`);
    }
  }
});
