// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import { defineConfig } from "@playwright/test";

const baseURL = "http://127.0.0.1:4173/qiqiime/";

export default defineConfig({
  testDir: ".",
  testMatch: "browser.spec.mjs",
  fullyParallel: true,
  forbidOnly: Boolean(process.env.CI),
  workers: 2,
  reporter: process.env.CI ? [["github"], ["list"]] : "list",
  use: {
    baseURL,
    browserName: "chromium",
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },
  projects: [
    { name: "desktop", use: { viewport: { width: 1440, height: 900 } } },
    { name: "mobile", use: { viewport: { width: 320, height: 800 } } },
  ],
  webServer: {
    command: `"${process.execPath}" server.mjs`,
    url: baseURL,
    timeout: 10000,
    reuseExistingServer: false,
  },
});
