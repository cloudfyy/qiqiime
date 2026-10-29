# 奇奇五笔输入法 QiQiIME (TSF / C++26 / CMake)

`QiQiIME`（**奇奇五笔**）是一个基于 Windows **TSF（Text Services Framework）** 编写的现代 86 五笔输入法工程，采用 **C++26** 与 **CMake** 构建，将“Windows TSF 前端（COM TIP）”与“五笔输入核心引擎”彻底解耦。

---

## 1. 目录结构

```text
C:\sample\qiqiime
├── CMakeLists.txt              # 根 CMake 构建配置（工具链、版本、C++26 标准与子目录调度）
├── CMakePresets.json           # CMake Presets (支持 x64 与 x86/Win32 双架构配置与构建)
├── VERSION                     # 项目版本号单一事实源 (SemVer, 当前 0.7.0)
├── vcpkg.json                  # vcpkg 依赖清单 (Manifest 模式管理 spdlog 与 ms-gsl)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
│   ├── config.ini              # 默认 UTF-8 INI 配置文件模板（支持切换词库、分页大小、自动上屏等）
│   ├── wubi86_jidian.txt       # 极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条）
│   ├── wubi86_jidian_extra.txt # 极点 86 五笔现代扩展词库（Apache-2.0，2,100 编码 / 2,216 词条）
│   └── wubi86_haifeng.txt      # 海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条，含 CJK 扩展 A/B 区生僻字）
├── docs/
│   └── ARCHITECTURE.md         # 详细架构设计、TSF 接口说明、安全模型与宿主兼容指南
├── scripts/
│   ├── build_all.ps1           # 一键构建并测试 x64 与 x86 双架构脚本
│   ├── register_ime.ps1        # 管理员权限注册输入法脚本（同时注册 x64 与 x86 WOW64）
│   └── unregister_ime.ps1      # 管理员权限卸载输入法脚本（同时卸载 x86 与 x64）
├── src/
│   ├── CMakeLists.txt          # 源码目标构建配置（qiqi_engine 静态库 + qiqiime.dll）
│   ├── common/                 # 公共基础模块（配置、GUID、多语言、日志、DPI、RAII 句柄、音效、主题、版本）
│   │   ├── config.h/.cpp       # UTF-8 INI 配置解析与用户目录 (%LOCALAPPDATA%\QiQiIME\config.ini) 原子持久化
│   │   ├── guids.h             # CLSID / Profile GUID / PreservedKey GUID / 窗口类名与常量定义
│   │   ├── i18n.h              # 简体中文 (zh-CN) / 英文 (en-US) / 跟随系统 (Auto) 界面文案表
│   │   ├── logger.h/.cpp       # 基于 spdlog 的 noexcept 安全日志模块与最小权限文件读取函数
│   │   ├── monitor_dpi.h       # 多显示器工作区查询 (MonitorFromPoint) 与 Per-Monitor DPI 缩放
│   │   ├── scoped_handle.h     # 泛型零开销 RAII 句柄模板 ScopedHandle<Traits>（管理文件、映射、注册表、GDI、HWND、HMENU、HMODULE）
│   │   ├── sound.h             # 零堆分配编译期/栈上 PCM WAV 合成（四码空码提示音与打字按键音）
│   │   ├── theme.h             # Windows 深色/浅色系统主题检测与暗色菜单/窗口适配
│   │   ├── version.h.in        # C++ 编译期版本号与 Git Commit 元信息头文件模板
│   │   └── version.rc.in       # Windows DLL 版本资源 (VERSIONINFO) 模板
│   ├── engine/                 # 五笔核心引擎模块（无 COM 依赖，可独立单测）
│   │   ├── wubi_binary_dict.h/.cpp # 4 字节紧凑编码索引 + CreateFileMappingW 只读内存映射词库缓存 (.qdict)
│   │   ├── wubi_fsm.h          # 编译期 (consteval/constexpr) 五笔有限状态机与转移表
│   │   ├── wubi_default_dict.h # 内置核心 86 五笔码表（一级简码、常用字词保底）
│   │   └── wubi_engine.h/.cpp  # 独立 86 五笔状态机与查词引擎
│   ├── tsf/                    # Windows TSF 框架接入与 COM 服务模块
│   │   ├── tip_service.h/.cpp  # TSF 核心服务（ITfTextInputProcessorEx / KeyEvent / Composition / Configure）
│   │   ├── display_attribute.h/.cpp # ITfDisplayAttributeProvider 组合串虚线下划线显示属性
│   │   ├── edit_session.h      # 现代 C++ Lambda 封装的 ITfEditSession
│   │   ├── class_factory.h/.cpp# COM IClassFactory 实现
│   │   ├── register.h/.cpp     # COM 注册表、TSF Language Profile 与 Category 注册（支持 WOW64）
│   │   ├── dllmain.cpp         # DLL 入口、窗口类清理与导出函数
│   │   └── qiqiime.def         # COM DLL 模块定义文件
│   └── ui/                     # 候选窗口、状态条、托盘菜单与设置/关于对话框模块
│       ├── candidate_ui.h/.cpp # 候选窗绘制 + ITfCandidateListUIElement（支持桌面弹窗与全屏游戏 UILess 模式）
│       ├── langbar_button.h/.cpp # Windows 任务栏系统托盘“中/英”指示器与右键快捷菜单
│       ├── status_bar.h/.cpp   # 桌面浮动状态条（4x4 超采样 32 位 ARGB 抗锯齿矢量图标、悬停提示、拖拽与右键菜单）
│       └── settings_dialog.h/.cpp # 属性设置对话框与“关于奇奇五笔”对话框
└── tests/
    ├── CMakeLists.txt          # 测试目标构建配置（qiqi_engine_test + qiqi_tsf_ui_test）
    ├── common.h                # 公共测试断言宏 (QIQI_TEST_CHECK) 与测试套件声明
    ├── common.cpp              # 公共模块测试（spdlog 日志、版本号、配置序列化、音效合成）
    ├── engine_dict.cpp         # 五笔引擎词库测试（词条增删、通配/前缀查询上限、外部码表与最小权限映射测试）
    ├── engine_input.cpp        # 五笔引擎输入状态机测试（简码、四码上屏、空码清屏/提示音、第五码顶字、翻页、全半角与标点）
    ├── engine_main.cpp         # qiqi_engine_test 测试入口
    ├── tsf_mocks.h             # TSF COM 接口 Mock 实现（ITfThreadMgr / ITfContext / ITfRange 等）
    ├── ui_candidate.cpp        # 候选窗口、托盘按钮、浮动状态条、设置对话框与关于对话框单元测试
    ├── tsf_com.cpp             # TSF COM 基础测试（LambdaEditSession、ClassFactory、DLL 导出与注册表）
    ├── tsf_service.cpp         # TSF 核心服务全链路测试（激活/停用、按键处理、组合串、菜单动作与热重载）
    └── tsf_ui_main.cpp         # qiqi_tsf_ui_test 测试入口
```

---

## 2. 已实现的核心功能

1. **86 五笔核心输入法规则（`WubiEngine`）**
   - **极点五笔 & 海峰五笔多词库支持**：内置 25 个一级简码及高频字词保底，并在 `data/` 下独立提供三套合法开源词库：
     - `data/wubi86_jidian.txt`：极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条，默认主词库）
     - `data/wubi86_jidian_extra.txt`：极点 86 五笔现代扩展词库（Apache-2.0，2,100 编码 / 2,216 词条，仅在主词库为极点五笔时可叠加启用）
     - `data/wubi86_haifeng.txt`：孙海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条，涵盖 GBK 及 Unicode CJK 扩展 A/B 区全部汉字）
   - **按需分页只读内存映射与最小权限保护（`WubiMappedDict`）**：
     - 外部大词库首次加载时自动编译为 4 字节大端紧凑编码索引二进制缓存（`%LOCALAPPDATA%\QiQiIME\cache\*.qdict`），通过独占写临时文件 + `MoveFileExW` 原子替换生成。
     - 通过 `CreateFileW`（仅 `GENERIC_READ` + 仅 `FILE_SHARE_READ` + `SECURITY_ANONYMOUS`）与匿名 `CreateFileMappingW` / `MapViewOfFile`（`PAGE_READONLY` / `FILE_MAP_READ`）映射，由操作系统按 4 KB 页按需调入且跨进程共享只读物理页，进程私有堆占用接近 0 KB，同时防止映射期间文件被篡改或截断。
   - `LoadDictionaryFile` 同时支持 `编码 词1 词2` 格式与 Rime `.dict.yaml`（`词<TAB>编码<TAB>权重`）格式直接加载。
   - **四码唯一自动上屏（`auto_commit_4code`）**：输入第 4 码且无重码时立即自动上屏（如 `wqvb` -> `你好`、`ggtt` -> `五笔`）。
   - **第五码顶字上屏（`push_commit_5th`）**：四码有重码时继续输入第 5 个字母，自动将当前首选词顶上屏，并用第 5 码开启新一轮输入。
   - **空码提示音与空码自动清屏（`beep_on_empty_4code` / `clear_on_empty_4code`）**：
     - 支持四码空码时播放柔和提示音（默认开启），以及可选的打字按键音效（`enable_key_sound`，默认关闭），音效均由内存零分配 WAV 合成并通过 `SND_MEMORY | SND_ASYNC` 播放。
     - 支持开启“空码时自动清空输入”（`clear_on_empty_4code`，默认 `false`）：当第 4 码无匹配候选词（或 1~3 码无候选按空格）时自动清空组合串回到空闲态。
   - **`z` 万能学习键与编码反查**：支持含 `z` 的通配查询（如 `wqz`），并在候选框中显示完整五笔编码提示（如 `1.你(wqiy)`）。
   - **选词与翻页**：空格首选上屏、`1`~`9` 数字选词（或鼠标点击候选词上屏）、`Enter` 编码原文上屏、`Backspace` 退格、`Esc` 取消、`-`/`=` 或 `PageUp`/`PageDown` 翻页、`↑`/`↓` 切换候选。
   - **动态状态切换（中英 / 全半角 / 中英标点）**：
     - **中/英文模式切换**：单按 `Shift` 或 `Ctrl+Space`（Preserved Key），组合态下按 `Shift+字母` 自动将首选词连同大写字母一并上屏。
     - **全/半角字符切换**：`Shift+Space`（Preserved Key，全角模式下英文字母、数字、空格与符号自动转为全角 `Ａ`~`Ｚ`、`０`~`９`、`　`）。
     - **中/英文标点切换**：`Ctrl+.`（Preserved Key，动态开关中文全角标点映射，含智能配对双引号 `“”` 与单引号 `‘’`）。
     - 所有动态状态实时同步至 Windows TSF Compartments（`GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 与 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`）。

2. **标准 Windows TSF 接入与完整 UI 交互（`QiQiTextService` & `src/ui/`）**
   - 实现 `ITfTextInputProcessorEx`、`ITfThreadMgrEventSink`、`ITfKeyEventSink`、`ITfCompositionSink`、`ITfFunctionProvider`、`ITfFnConfigure`、`ITfDisplayAttributeProvider`。
   - 所有文档读写与坐标获取均通过 `ITfContext::RequestEditSession`（`LambdaEditSession`）安全执行，处理宿主主动终止组合（`OnCompositionTerminated`）与焦点切换（`OnSetFocus`），兼容 Win32、Chromium/Electron 与 Office。
   - **候选窗口（`CandidateUI`）**：实现 `ITfCandidateListUIElement` 并注册 `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`，同时支持**桌面无焦点弹出候选窗**与**全屏游戏 UILess 模式**。
   - **任务栏托盘按钮（`LangBarButton`）与桌面浮动状态条（`StatusBarUI`）**：
     - 任务栏托盘实时显示“中/英”图标，支持左键切换中英文、右键打开快捷菜单。
     - 浮动状态条采用 4x4 超采样抗锯齿 32 位预乘 ARGB 位图绘制（月牙/全圆、中英标点、八齿齿轮图标），支持左侧胶囊把手拖拽、悬停提示（Tooltip）与右键菜单（默认关闭，可通过菜单或设置开启）。
   - **属性设置与“关于奇奇五笔”对话框（`SettingsDialog` / `AboutDialog`）**：
     - 支持图形化配置主词库（极点/海峰/内置，非极点词库自动禁用极点扩展词库选项）、打字习惯、音效、每页候选数与中英文界面语言。
     - 内置“关于奇奇五笔”对话框，展示品牌徽标图标、版本号与架构、GitHub 仓库链接（`https://github.com/cloudfyy/qiqiime`）及开源许可协议信息。
   - **多显示器、Per-Monitor DPI 与系统深浅色主题**：所有弹窗与对话框均支持多显示器工作区边界裁剪（含负坐标副屏）、高分屏 DPI 动态缩放及 Windows 10/11 深色/浅色主题自动适配。

3. **结构化日志系统（`spdlog`）**
   - 日志文件默认位于 `%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`（单文件上限 5MB，保留 3 个备份），同时输出到 `OutputDebugString`。
   - 格式：`[%Y-%m-%d %H:%M:%S.%e] [%n] [pid:%P] [tid:%t] [%l] %v`（自动携带宿主进程名 `%n`，如 `[notepad.exe]`，便于多进程排障；所有日志宏均具备 `noexcept` 异常安全保护且不记录用户打字隐私内容）。

4. **版本号机制（`VERSION` + `version.h` + `version.rc`）**
   - 以根目录 `VERSION` 文件（当前 `0.7.0`）作为单一事实源，CMake 配置期自动提取当前 Git 短提交哈希（`git rev-parse --short=7 HEAD`）及目标架构（`x64` / `x86`），生成 `version.h`（提供 `qiqi::version::kCurrentVersion`、`kVersion`、`kFullVersion`、`kGitCommit`、`kArch` 及三路比较 `SemanticVersion`）和 Windows PE 资源 `version.rc`（为 `qiqiime.dll` 嵌入文件属性版本信息）。

5. **双架构（`x64` + `x86` / WOW64）完整支持**
   - 支持同时构建 64 位（`build\x64`）与 32 位（`build\x86`）`qiqiime.dll`。
   - `RegisterServer` / `UnregisterServer` 自动识别 WOW64 环境（`IsRunningUnderWow64()`）：在 64 位系统上通过 `SysWOW64\regsvr32.exe` 注册 32 位 DLL 时，精确写入 `HKCR\WOW6432Node\CLSID` 而不覆盖 64 位共享 TSF Language Profile，兼顾 64 位桌面应用与 32 位老应用/游戏。

6. **INI 配置文件系统与动态热更新（`config.ini`）**
   - 默认读取 `%LOCALAPPDATA%\QiQiIME\config.ini`（首次运行时若不存在会自动生成带中文注释的默认配置文件），并支持回退读取 `qiqiime.dll` 同目录的 `config.ini`。
   - 支持配置主词库选择（`primary = jidian | haifeng | builtin`）、是否启用现代扩展词库（`enable_jidian_extra`）、自定义词表路径（`user_dict`）、每页候选数（`page_size`）、四码唯一自动上屏（`auto_commit_4code`）、第五码顶字（`push_commit_5th`）、四码空码提示音（`beep_on_empty_4code`）、空码自动清空输入（`clear_on_empty_4code`）、打字按键音效（`enable_key_sound`）、浮动状态条（`show_status_bar`）、界面语言（`ui_language = auto | zh-CN | en-US`）、默认中英文模式（`default_chinese_mode`）、默认全半角模式（`default_full_width`）、默认中英文标点（`default_chinese_punct`）、Shift 切换开关（`shift_toggle_mode`）及日志级别（`level`）。
   - **动态热重载**：焦点切换（`OnSetFocus`）时自动检测 `%LOCALAPPDATA%\QiQiIME\config.ini` 的修改时间戳，修改保存后无需重启宿主程序即可生效。

---

## 3. 编译与测试（CMake + vcpkg + Visual Studio 2026）

需要 CMake 3.25 或更高版本、vcpkg（Visual Studio 2026/2022 内置或通过 `VCPKG_ROOT` 指定），以及支持 C++26 草案模式的编译器。Visual Studio 2026 的 MSVC 使用 `/std:c++latest`，可用的 C++26 特性取决于编译器版本，并不代表完整实现 C++26。

项目使用 **vcpkg Manifest 模式**（`vcpkg.json`）管理依赖，CMake 配置时会自动检测 `VCPKG_ROOT` 或 Visual Studio 内置 vcpkg 工具链并安装依赖。

### 方式一：一键编译并测试 `x64` + `x86` 双架构（推荐）

```powershell
cd C:\sample\qiqiime
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
```

### 方式二：使用 CMake Presets 或手动指定架构

```powershell
# 编译并测试 x64
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release

# 编译并测试 x86 (Win32)
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

双架构构建产物位于：
- `C:\sample\qiqiime\build\x64\Release\qiqiime.dll`（64 位 TSF 输入法组件）
- `C:\sample\qiqiime\build\x86\Release\qiqiime.dll`（32 位 TSF 输入法组件，供 32 位/WOW64 进程加载）
- `C:\sample\qiqiime\build\<x64|x86>\Release\wubi86_jidian.txt`（极点 86 五笔核心主词库）
- `C:\sample\qiqiime\build\<x64|x86>\Release\wubi86_jidian_extra.txt`（极点 86 五笔现代扩展词库）
- `C:\sample\qiqiime\build\<x64|x86>\Release\wubi86_haifeng.txt`（海峰 86 五笔超大字符集词库）
- `C:\sample\qiqiime\build\<x64|x86>\Release\qiqi_engine_test.exe`（引擎与基础组件单元测试）
- `C:\sample\qiqiime\build\<x64|x86>\Release\qiqi_tsf_ui_test.exe`（TSF 服务与候选窗 UI 单元测试）

---

## 4. 注册与卸载（需管理员权限）

以**管理员身份**运行 PowerShell：

```powershell
# 注册输入法
powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\register_ime.ps1

# 卸载输入法
powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\unregister_ime.ps1
```

> **提示**：由于 TSF DLL 会被加载进当前焦点宿主进程，重新编译覆盖 `qiqiime.dll` 前，请先切换到其他输入法并执行卸载脚本（或关闭正在使用该输入法的程序）。

---

## 5. 开源词库致谢与许可证声明（Attribution & Third-Party Notices）

本项目在 `data/` 目录下收录并转换了以下合法开源的 86 五笔词库码表，特此向原作者及维护社区致谢，并严格遵循各自的开源许可协议：

### 5.1 极点五笔 86 词库（`wubi86_jidian.txt` / `wubi86_jidian_extra.txt`）
- **上游仓库**：[KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)
- **原始码表文件**：
  - 核心主词库：[`wubi86_jidian.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/wubi86_jidian.dict.yaml)（77,169 编码 / 89,254 词条）
  - 现代扩展词库：[`wubi86_jidian_extra.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/wubi86_jidian_extra.dict.yaml)（2,100 编码 / 2,216 词条）
- **开源协议**：[Apache License 2.0](https://github.com/KyleBing/rime-wubi86-jidian/blob/master/LICENSE)

### 5.2 海峰五笔 86 超大字符集词库（`wubi86_haifeng.txt`）
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

### 5.3 第三方 C++ 依赖库
- **[gabime/spdlog](https://github.com/gabime/spdlog)**：高性能 C++ 日志库，遵循 [MIT License](https://github.com/gabime/spdlog/blob/v1.x/LICENSE)。
- **[microsoft/GSL](https://github.com/microsoft/GSL)**：C++ Core Guidelines Support Library（`gsl::span` / `gsl::not_null` / `gsl::narrow_cast` / `gsl::at` / `gsl::finally`），遵循 [MIT License](https://github.com/microsoft/GSL/blob/main/LICENSE)。
