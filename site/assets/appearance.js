// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

const storageKey = "qiqi-display-v1";
const defaults = Object.freeze({ theme: "light", scale: 100, easy: false });

export function validateAppearance(value) {
  if (!["light", "dark", "contrast", "system"].includes(value?.theme) ||
      ![100, 125, 150, 200].includes(value?.scale) || typeof value?.easy !== "boolean") {
    throw new Error("Invalid display preferences.");
  }
  return { theme: value.theme, scale: value.scale, easy: value.easy };
}

export function initializeAppearance(english) {
  const header = document.querySelector(".header");
  if (!header) return;
  const text = (zh, en) => english ? en : zh;
  let preferences = { ...defaults };
  let readFailed = false;
  try {
    const saved = localStorage.getItem(storageKey);
    if (saved !== null) preferences = validateAppearance(JSON.parse(saved));
  } catch (error) {
    console.warn("Could not read display preferences:", error instanceof Error ? error.name : "UnknownError");
    readFailed = true;
  }

  const panel = document.createElement("aside");
  panel.className = "appearance wrap";
  panel.setAttribute("aria-label", text("显示与易读设置", "Display and reading preferences"));
  panel.innerHTML = `
    <div class="appearance-quick">
      <label class="easy-toggle" for="easy-reading">
        <input id="easy-reading" type="checkbox">
        <span>${text("易读模式", "Easy reading")}</span>
      </label>
      <details class="appearance-details">
        <summary>${text("显示设置", "Display options")}</summary>
        <div class="appearance-options">
          <label for="appearance-theme">${text("界面主题", "Theme")}
            <select id="appearance-theme">
              <option value="light">${text("现有明亮样式", "Light (original)")}</option>
              <option value="dark">${text("深色", "Dark")}</option>
              <option value="contrast">${text("高对比度", "High contrast")}</option>
              <option value="system">${text("跟随系统", "Follow system")}</option>
            </select>
          </label>
          <label for="appearance-scale">${text("文字大小", "Text size")}
            <select id="appearance-scale" aria-describedby="appearance-help">
              <option value="100">100%</option>
              <option value="125">125%</option>
              <option value="150">150%</option>
              <option value="200">200%</option>
            </select>
          </label>
          <button id="appearance-reset" class="button secondary" type="button">${text("恢复默认显示", "Reset display")}</button>
        </div>
        <p id="appearance-help">${text(
          "易读模式会放大较小的文字至 150%、增大按钮、采用单列布局并减少装饰和动画。之后仍可单独调整文字大小。显示偏好仅保存在本浏览器，不会上传。",
          "Easy reading raises smaller text to 150%, enlarges controls, uses a single column, and reduces decoration and motion. You can still adjust text size separately. Display preferences stay in this browser and are not uploaded.")}</p>
      </details>
    </div>
    <p id="appearance-status" class="appearance-status" role="status" aria-live="polite"></p>`;
  header.insertAdjacentElement("afterend", panel);
  const theme = panel.querySelector("#appearance-theme");
  const scale = panel.querySelector("#appearance-scale");
  const easy = panel.querySelector("#easy-reading");
  const status = panel.querySelector("#appearance-status");
  const systemDark = matchMedia("(prefers-color-scheme: dark)");

  function apply() {
    const root = document.documentElement;
    root.dataset.theme = preferences.theme === "system" ? (systemDark.matches ? "dark" : "light") : preferences.theme;
    root.dataset.fontScale = String(preferences.scale);
    root.dataset.easy = String(preferences.easy);
    theme.value = preferences.theme;
    scale.value = String(preferences.scale);
    easy.checked = preferences.easy;
  }

  function save(reset = false) {
    apply();
    try {
      if (reset) localStorage.removeItem(storageKey);
      else localStorage.setItem(storageKey, JSON.stringify(preferences));
      status.textContent = reset
        ? text("已恢复默认显示。", "Default display restored.")
        : text("显示设置已保存在本浏览器。", "Display preferences saved in this browser.");
    } catch (error) {
      console.warn("Could not save display preferences:", error instanceof Error ? error.name : "UnknownError");
      status.textContent = text(
        "显示设置已生效，但无法保存；重新打开页面后可能丢失。",
        "Display preferences applied but could not be saved; they may be lost when you reopen the page.");
    }
  }

  theme.addEventListener("change", () => {
    preferences.theme = theme.value;
    save();
  });
  scale.addEventListener("change", () => {
    preferences.scale = Number(scale.value);
    save();
  });
  easy.addEventListener("change", () => {
    preferences.easy = easy.checked;
    if (preferences.easy) preferences.scale = Math.max(150, preferences.scale);
    save();
  });
  panel.querySelector("#appearance-reset").addEventListener("click", () => {
    preferences = { ...defaults };
    save(true);
  });
  systemDark.addEventListener("change", () => {
    if (preferences.theme === "system") apply();
  });
  apply();
  if (readFailed) {
    status.textContent = text(
      "无法读取已保存的显示设置，本次使用默认显示。",
      "Saved display preferences could not be read; using the default display for this visit.");
  }
}
