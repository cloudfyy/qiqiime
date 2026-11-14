// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { readFileSync, readdirSync } from "node:fs";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../../site/", import.meta.url));
const files = new Map();
function collect(directory) {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) collect(file);
    else files.set("/qiqiime/" + path.relative(root, file).split(path.sep).join("/"), file);
  }
}
collect(root);
files.set("/qiqiime/", path.join(root, "index.html"));
const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
  ".xml": "application/xml",
  ".svg": "image/svg+xml",
  ".png": "image/png",
};
createServer((request, response) => {
  const pathname = new URL(request.url, "http://127.0.0.1").pathname;
  const matched = files.get(pathname);
  const file = matched || path.join(root, "404.html");
  response.writeHead(matched ? 200 : 404, {
    "Content-Type": types[path.extname(file)] || "application/octet-stream",
    "Cache-Control": "no-store",
  });
  response.end(readFileSync(file));
}).listen(4173, "127.0.0.1");
