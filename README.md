# 奇奇五笔输入法 (QiQi Wubi IME)

**奇奇五笔（QiQiIME）** 是一款面向 Windows 10 / 11 的轻量、高速、零堆内存常驻开销的 **86 版五笔输入法**，基于 Windows 原生 **TSF（Text Services Framework）** 与 **C++26** 构建，同时支持 64 位与 32 位桌面软件及全屏游戏。

---

## 1. 核心功能特性

### 1.1 多词库体系（极点五笔 & 海峰超大字符集）
- **极点五笔 86 核心词库（默认）**：收录 77,169 编码 / 89,254 词条，保持经典极点五笔盲打词序体验。
- **极点现代扩展词库（可选叠加）**：收录 2,100 编码 / 2,216 条现代互联网、科技、生活常用新词（当主词库设为极点五笔时可自由开关）。
- **海峰五笔 86 超大字符集词库**：收录 100,051 编码 / 144,374 词条，完整覆盖 GBK 与 Unicode CJK 扩展 A/B 区生僻字（如 `𠀀`）。
- **自定义用户词库与在线造词**：支持加载用户自定义词表（兼容 `编码 词1 词2` 与 Rime `.dict.yaml` 格式），并支持通过 **`Ctrl + =`** 将剪贴板中的双字词（2+2）、三字词（1+1+2）或多字词（1+1+1+末）按五笔 86 规则自动取码并持久化到用户词库。

### 1.2 纯正 86 五笔打字体验
- **四码唯一自动上屏（`auto_commit_4code`）**：输入满 4 码且无重码时立即自动上屏（如 `wqvb` -> `你好`）。
- **第五码顶字上屏（`push_commit_5th`）**：4 码有重码时继续输入第 5 个字母，自动将当前首选词顶上屏并开启下一轮输入。
- **分号 `;` / 单引号 `'` 快捷选重码（`semicolon_quote_quick_select`）**：在输入编码状态下，按 `;` 直接上屏第 2 候选词、按 `'` 直接上屏第 3 候选词。
- **渐进后续编码提示（`show_code_hint`）与 `z` 万能学习键**：
  - 输入 1~3 码时，在候选词旁自动提示剩余编码后缀（如输入 `wq` 显示 `你好(~vb)`）；
  - 第 2~4 码支持按 `z` 万能键通配查询，并显示完整五笔编码反查提示（如 `1.你(wqiy)`）。
- **首码 `z` 引导临时英文模式（`enable_z_temp_english`）**：在中文空闲态下首键按 `z` 即可直接输入含大小写字母、数字与符号的英文单词、网址或代码标识符（如 `zhttps://github.com`），按空格或回车直接上屏，无需来回切换中英模式。
- **空码自动清屏與提示音（`beep_on_empty_4code` / `beep_on_dup_4code` / `clear_on_empty_4code`）**：
  - 支持四码空码提示音、四码重码提示音与可选打字按键音效（`enable_key_sound`）；
  - 支持可选“空码时自动清空输入”（默认关闭）。
- **可选重码词组动态调频（`dynamic_phrase_freq`）**：默认严格保持五笔固定词序；开启后，单字位置始终固定不变，仅将高频选中的重码词组动态提升至前排。

### 1.3 快捷键一览

| 快捷键 / 操作 | 功能说明 |
|---|---|
| **`Win + Space`** | 在 Windows 输入法列表切换到「QiQi 五笔 86」 |
| **单按 `Shift`** 或 **`Ctrl + Space`** | 切换 **中 / 英** 输入模式 |
| **`Shift + Space`** | 切换 **全角 / 半角** 字符模式 |
| **`Ctrl + .`** | 切换 **中文全角标点 / 英文半角标点**（含智能配对引号 `“”` / `‘’`） |
| **`Ctrl + =`** | 将剪贴板中的词组自动按五笔 86 规则取码并加入个人词库 |
| **`Space`（空格键）** | 上屏当前高亮候选词（默认第 1 候选词） |
| **`;` / `'`** | 在组合态下快捷上屏 **第 2 / 第 3** 候选词 |
| **`1` ~ `9` 数字键 / 鼠标点击** | 直接上屏当前页对应序号的候选词 |
| **`Enter`（回车键）** | 将当前已输入的英文字母原文直接上屏 |
| **`-` / `=`** 或 **`PageUp` / `PageDown`** | 候选框上翻页 / 下翻页 |
| **`↑` / `↓` 方向键** | 在当前页上/下移动高亮候选词 |
| **组合态按 `Shift + 字母`** | 将当前首选词连同该大写英文字母一并上屏 |

### 1.4 界面与个性化设置
- **Direct2D / DirectWrite 候选窗口**：支持彩色 Emoji（`D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`）与 CJK 扩展 B~G 区生僻字系统级字体回退，同时支持**全屏游戏 UILess 模式**。
- **任务栏托盘与 4x4 超采样抗锯齿浮动状态条**：
  - 任务栏托盘实时显示“中/英”状态图标，支持左键切换中英、右键打开快捷菜单；
  - 桌面浮动状态条（默认关闭）支持胶囊把手拖拽、悬停提示（Tooltip）、右键菜单，并自动跟随 Windows 深色/浅色主题与多显示器高分屏 DPI。
- **图形化属性设置与热重载配置文件**：
  - 支持通过图形界面或直接编辑 `%LOCALAPPDATA%\QiQiIME\config.ini` 调整所有功能开关（窗口焦点切换时自动无感热重载），并支持中/英文（`zh-CN` / `en-US`）双语界面切换。

---

## 2. 快速安装与卸载

在项目或发行包根目录下运行 PowerShell：

```powershell
# 一键校验产物、自动弹出 UAC 提权注册 x64+x86 输入法并打开记事本试用
powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate -LaunchNotepad

# 一键卸载输入法并清理注册表
powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
```

---

## 3. 技术文档索引（`docs/`）

本项目将功能文档与技术文档分离，详细技术设计、开发构建与部署运维请查阅 `docs/` 目录下的专项文档：

| 文档分类 | 文档链接 | 内容概述 |
|---|---|---|
| **架构文档** | [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) | 系统分层架构、TSF COM 生命周期、编译期 `consteval` 状态机、`.qdict` 按需分页只读内存映射与最小权限安全设计、Direct2D/DirectWrite 与 RAII 体系 |
| **开发文档** | [docs/DEVELOPMENT.md](./docs/DEVELOPMENT.md) | 源码目录结构、CMake + vcpkg + C++26 双架构构建、单元测试套件、`CppCoreCheck` 静态代码检查、版本号生成机制与结构化排障日志规范 |
| **部署文档** | [docs/DEPLOYMENT.md](./docs/DEPLOYMENT.md) | `x64` + `x86 (WOW64)` 双架构注册原理、独立发行包打包（`build_all.ps1 -Package`）、用户数据目录结构、升级 DLL 占用处理与故障排查 |

---

## 4. 开源许可协议与词库致谢（License & Third-Party Notices）

本项目（**奇奇五笔 / QiQiIME**，作者：`cloudfyy`）采用 **[GNU General Public License v3.0 (GPL-3.0)](./LICENSE)** 开源发布。

本项目在 `data/` 目录下收录并转换了以下合法开源的 86 五笔词库码表，特此向原作者及维护社区致谢，并严格遵循各自的开源许可协议：

### 4.1 极点五笔 86 词库（`wubi86_jidian.txt` / `wubi86_jidian_extra.txt`）
- **上游仓库**：[KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)
- **原始码表文件**：
  - 核心主词库：[`wubi86_jidian.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/wubi86_jidian.dict.yaml)（77,169 编码 / 89,254 词条）
  - 现代扩展词库：[`wubi86_jidian_extra.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/wubi86_jidian_extra.dict.yaml)（2,100 编码 / 2,216 词条）
- **开源协议**：[Apache License 2.0](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/LICENSE)

### 4.2 海峰五笔 86 超大字符集词库（`wubi86_haifeng.txt`）
- **原作者与贡献者**：孙海峰（SUN Haifeng）及《五笔爱好者论坛》网友、深圳大学中文系合作团队
- **上游仓库**：[definite/ibus-table-chinese (`tables/wubi-haifeng`)](https://github.com/definite/ibus-table-chinese/tree/master/tables/wubi-haifeng)
- **原始码表说明与授权文件**：
  - 码表说明：[`tables/wubi-haifeng/README`](https://github.com/definite/ibus-table-chinese/blob/master/tables/wubi-haifeng/README)
  - 授权文件：[`tables/wubi-haifeng/COPYING`](https://github.com/definite/ibus-table-chinese/blob/master/tables/wubi-haifeng/COPYING)（100,051 编码 / 144,374 词条，涵盖 GBK、常用词组及 Unicode CJK 扩展 A/B 区全部汉字）
- **开源协议**：**BSD 3-Clause License**
  > Copyright (c) 2010, SUN Haifeng (孙海峰). All rights reserved.
  >
  > Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
  > 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  > 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
  > 3. Neither the name of the "SunHaifeng.NET(海峰别墅)" nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

### 4.3 第三方 C++ 依赖库
- **[gabime/spdlog](https://github.com/gabime/spdlog)**：高性能 C++ 日志库，遵循 [MIT License](https://github.com/gabime/spdlog/blob/v1.x/LICENSE)。
- **[microsoft/GSL](https://github.com/microsoft/GSL)**：C++ Core Guidelines Support Library（`gsl::span` / `gsl::not_null` / `gsl::narrow_cast` / `gsl::at` / `gsl::finally`），遵循 [MIT License](https://github.com/microsoft/GSL/blob/main/LICENSE)。
