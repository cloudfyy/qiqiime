// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import test from "node:test";
import { validateAppearance } from "../../site/assets/appearance.js";

test("saved display preferences retain independent theme, scale, and easy-reading choices", () => {
  const saved = { theme: "system", scale: 200, easy: true, obsolete: "ignored" };
  assert.deepEqual(validateAppearance(saved), { theme: "system", scale: 200, easy: true });
  assert.deepEqual(validateAppearance({ theme: "contrast", scale: 125, easy: false }),
    { theme: "contrast", scale: 125, easy: false });
});

test("corrupt or unsupported saved display preferences are rejected rather than applied", () => {
  for (const value of [
    null, [], "dark", {},
    { theme: "unknown", scale: 100, easy: false },
    { theme: "dark", scale: "150", easy: true },
    { theme: "dark", scale: 99, easy: true },
    { theme: "light", scale: 201, easy: false },
    { theme: "light", scale: 100, easy: "true" },
  ]) {
    assert.throws(() => validateAppearance(value), /Invalid display preferences/);
  }
});
