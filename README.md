# QiQi 五笔输入法 (TSF / C++26 / CMake)

`QiQiIME` 是一个基于 Windows **TSF（Text Services Framework）** 编写的 86 五笔输入法基础工程，采用现代 **C++26** 与 **CMake** 构建，将“Windows TSF 前端（COM TIP）”与“五笔输入引擎”解耦。

---

## 1. 目录结构

```text
C:\sample\qiqiime
├── CMakeLists.txt              # 根 CMake 构建配置（工具链、版本、C++26 标准与子目录调度）
├── CMakePresets.json           # CMake Presets (支持 x64 与 x86/Win32 双架构配置与构建)
├── VERSION                     # 项目版本号单一事实源 (SemVer, 如 0.1.0)
├── vcpkg.json                  # vcpkg 依赖清单 (Manifest 模式管理 spdlog 等依赖)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
│   ├── config.ini              # 默认 UTF-8 INI 配置文件模板（支持切换词库、分页大小、自动上屏等）
│   ├── wubi86_jidian.txt       # 极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条）
│   ├── wubi86_jidian_extra.txt # 极点 86 五笔现代扩展词库（Apache-2.0，2,100 编码 / 2,216 词条）
│   └── wubi86_haifeng.txt      # 海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条，含 CJK 扩展 A/B 区生僻字）
├── docs/
│   └── ARCHITECTURE.md         # 详细架构设计、TSF 接口说明与游戏/宿主兼容指南
├── scripts/
│   ├── build_all.ps1           # 一键构建并测试 x64 与 x86 双架构脚本
│   ├── register_ime.ps1        # 管理员权限注册输入法脚本（同时注册 x64 与 x86 WOW64）
│   └── unregister_ime.ps1      # 管理员权限卸载输入法脚本（同时卸载 x86 与 x64）
├── src/
│   ├── CMakeLists.txt          # 源码目标构建配置（qiqi_engine 静态库 + qiqiime.dll）
│   ├── common/                 # 公共基础模块（GUID 常量、INI 配置、spdlog 日志、版本信息与资源模板）
│   │   ├── config.h/.cpp       # UTF-8 INI 配置解析与用户目录 (%LOCALAPPDATA%\QiQiIME\config.ini) 自动初始化
│   │   ├── guids.h             # CLSID / Profile GUID / UIElement GUID 定义
│   │   ├── logger.h/.cpp       # 基于 spdlog 的日志模块（Rotating File + MSVC Debug 输出）
│   │   ├── version.h.in        # C++ 编译期版本号与 Git Commit 元信息头文件模板
│   │   └── version.rc.in       # Windows DLL 版本资源 (VERSIONINFO) 模板
│   ├── engine/                 # 五笔核心引擎模块（无 COM 依赖，可独立单测）
│   │   ├── wubi_fsm.h          # 编译期 (consteval/constexpr) 五笔有限状态机与转移表
│   │   ├── wubi_default_dict.h # 内置核心 86 五笔码表（一级简码、常用字词）
│   │   └── wubi_engine.h/.cpp  # 独立 86 五笔状态机与查词引擎
│   ├── tsf/                    # Windows TSF 框架接入与 COM 服务模块
│   │   ├── tip_service.h/.cpp  # TSF 核心服务（ITfTextInputProcessorEx / KeyEvent / Composition）
│   │   ├── edit_session.h      # 现代 C++ Lambda 封装的 ITfEditSession
│   │   ├── class_factory.h/.cpp# COM IClassFactory 实现
│   │   ├── register.h/.cpp     # COM 注册表、TSF Language Profile 与 Category 注册
│   │   ├── dllmain.cpp         # DLL 入口与导出函数
│   │   └── qiqiime.def         # COM DLL 模块定义文件
│   └── ui/                     # 候选窗口与界面交互模块
│       └── candidate_ui.h/.cpp # 候选窗绘制 + ITfCandidateListUIElement（支持游戏 UILess 模式）
└── tests/
    ├── CMakeLists.txt          # 测试目标构建配置（qiqi_engine_test + qiqi_tsf_ui_test）
    ├── common.h                # 公共测试断言宏 (QIQI_TEST_CHECK) 与测试套件声明
    ├── common.cpp              # 公共模块测试（spdlog 日志系统、版本号与架构元信息）
    ├── engine_dict.cpp         # 五笔引擎词库测试（词条增删、通配/前缀查询上限、外部码表加载）
    ├── engine_input.cpp        # 五笔引擎输入状态机测试（简码、四码上屏、第五码顶字、翻页、标点）
    ├── engine_main.cpp         # qiqi_engine_test 测试入口
    ├── tsf_mocks.h             # TSF COM 接口 Mock 实现（ITfThreadMgr / ITfContext / ITfRange 等）
    ├── ui_candidate.cpp        # 候选窗口 UI 测试（ITfCandidateListUIElement、弹窗绘制与边界裁剪）
    ├── tsf_com.cpp             # TSF COM 基础测试（LambdaEditSession、ClassFactory、DLL 导出）
    ├── tsf_service.cpp         # TSF 核心服务测试（激活/停用、按键处理、组合串与模式切换）
    └── tsf_ui_main.cpp         # qiqi_tsf_ui_test 测试入口
```

---

## 2. 已实现的核心功能

1. **86 五笔核心输入法规则（`WubiEngine`）**
   - **极点五笔 & 海峰五笔多词库支持**：内置 25 个一级简码及高频字词保底，并在 `data/` 下独立提供三套合法开源词库：
     - `data/wubi86_jidian.txt`：极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条）
     - `data/wubi86_jidian_extra.txt`：极点 86 五笔现代扩展词库（Apache-2.0，2,100 编码 / 2,216 词条）
     - `data/wubi86_haifeng.txt`：孙海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条，涵盖 GBK 及 Unicode CJK 扩展 A/B 区全部汉字）
   - `LoadDictionaryFile` 同时支持 `编码 词1 词2` 格式与 Rime `.dict.yaml`（`词<TAB>编码<TAB>权重`）格式直接加载。
   - **四码唯一自动上屏**：输入第 4 码且无重码时立即自动上屏（如 `wqvb` -> `你好`、`ggtt` -> `五笔`）。
   - **第五码顶字上屏**：四码有重码时继续输入第 5 个字母，自动将当前首选词顶上屏，并用第 5 码开启新一轮输入。
   - **`z` 万能学习键**：支持含 `z` 的通配查询（如 `wqz`）。
   - **选词与翻页**：空格首选上屏、`1-5` 数字选词、`Enter` 编码原文上屏、`Backspace` 退格、`Esc` 取消、`-`/`=` 或 `PageUp`/`PageDown` 翻页、`↑`/`↓` 切换候选。
   - **动态状态切换（中英 / 全半角 / 中英标点）**：
     - **中/英文模式切换**：单按 `Shift` 或 `Ctrl+Space`（Preserved Key）
     - **全/半角字符切换**：`Shift+Space`（Preserved Key，全角模式下英文字母、数字、空格与符号自动转为全角 `Ａ`~`Ｚ`、`０`~`９`、`　`）
     - **中/英文标点切换**：`Ctrl+.`（Preserved Key，动态开关中文全角标点映射）
     - 所有动态状态实时同步至 Windows TSF Compartments（`GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 与 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`）。

2. **标准 Windows TSF 接入（`QiQiTextService`）**
   - 实现 `ITfTextInputProcessorEx`、`ITfThreadMgrEventSink`、`ITfKeyEventSink`、`ITfCompositionSink`。
   - 所有文档读写与坐标获取均通过 `ITfContext::RequestEditSession`（`LambdaEditSession`）安全执行。
   - 处理宿主主动终止组合（`OnCompositionTerminated`）与焦点切换（`OnSetFocus`），兼容 Win32、Chromium/Electron 与 Office。
   - 实现 `ITfCandidateListUIElement` 并注册 `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`，同时支持**桌面无焦点弹出候选窗**与**全屏游戏 UILess 模式**。

3. **日志系统（`spdlog`）**
   - 集成 `spdlog`（启用 C++ 标准库 `<format>` 与宽字符文件名支持），同时输出到 `OutputDebugString`（`msvc_sink_mt`）与滚动日志文件（默认路径 `%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`，单文件上限 5MB，保留 3 个备份）。

4. **版本号机制（`VERSION` + `version.h` + `version.rc`）**
   - 以根目录 `VERSION` 文件作为单一事实源，CMake 配置期自动提取当前 Git 短提交哈希（`git rev-parse --short=7 HEAD`）及目标架构（`x64` / `x86`），生成 `version.h`（提供 `qiqi::version::kCurrentVersion`、`kVersion`、`kFullVersion`、`kGitCommit`、`kArch` 及三路比较 `SemanticVersion`）和 Windows PE 资源 `version.rc`（为 `qiqiime.dll` 嵌入文件属性版本信息）。

5. **双架构（`x64` + `x86` / WOW64）完整支持**
   - 支持同时构建 64 位（`build\x64`）与 32 位（`build\x86`）`qiqiime.dll`。
   - `RegisterServer` / `UnregisterServer` 自动识别 WOW64 环境（`IsRunningUnderWow64()`）：在 64 位系统上通过 `SysWOW64\regsvr32.exe` 注册 32 位 DLL 时，精确写入 `HKCR\WOW6432Node\CLSID` 而不覆盖 64 位共享 TSF Language Profile，兼顾 64 位桌面应用与 32 位老应用/游戏。

6. **INI 配置文件系统与动态热更新（`config.ini`）**
   - 默认读取 `%LOCALAPPDATA%\QiQiIME\config.ini`（首次运行时若不存在会自动生成带中文注释的默认配置文件），并支持回退读取 `qiqiime.dll` 同目录的 `config.ini`。
   - 支持配置主词库选择（`primary = jidian | haifeng | builtin`）、是否启用现代扩展词库（`enable_jidian_extra`）、自定义词表路径（`user_dict`）、每页候选数（`page_size`）、四码唯一自动上屏（`auto_commit_4code`）、第五码顶字（`push_commit_5th`）、默认中英文模式（`default_chinese_mode`）、默认全半角模式（`default_full_width`）、默认中英文标点（`default_chinese_punct`）、Shift 切换开关（`shift_toggle_mode`）及日志级别（`level`）。
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
