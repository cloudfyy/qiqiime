# QiQi 五笔输入法 (TSF / C++26 / CMake)

`QiQiIME` 是一个基于 Windows **TSF（Text Services Framework）** 编写的 86 五笔输入法基础工程，采用现代 **C++26** 与 **CMake** 构建，将“Windows TSF 前端（COM TIP）”与“五笔输入引擎”解耦。

---

## 1. 目录结构

```text
C:\sample\qiqiime
├── CMakeLists.txt              # CMake 构建配置 (qiqi_engine + qiqiime.dll + qiqi_engine_test)
├── VERSION                     # 项目版本号单一事实源 (SemVer, 如 0.1.0)
├── vcpkg.json                  # vcpkg 依赖清单 (Manifest 模式管理 spdlog 等依赖)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
│   └── wubi86.txt              # 外部可扩展 UTF-8 86 五笔码表（构建后自动复制到 DLL 同目录）
├── docs/
│   └── ARCHITECTURE.md         # 详细架构设计、TSF 接口说明与游戏/宿主兼容指南
├── scripts/
│   ├── register_ime.ps1        # 管理员权限注册输入法脚本
│   └── unregister_ime.ps1      # 管理员权限卸载输入法脚本
├── src/
│   ├── common/                 # 公共基础模块（GUID 常量、spdlog 日志、版本信息与资源模板）
│   │   ├── guids.h             # CLSID / Profile GUID / UIElement GUID 定义
│   │   ├── logger.h/.cpp       # 基于 spdlog 的日志模块（Rotating File + MSVC Debug 输出）
│   │   ├── version.h.in        # C++ 编译期版本号与 Git Commit 元信息头文件模板
│   │   └── version.rc.in       # Windows DLL 版本资源 (VERSIONINFO) 模板
│   ├── engine/                 # 五笔核心引擎模块（无 COM 依赖，可独立单测）
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
    └── engine_test.cpp         # 五笔引擎自动化单元测试
```

---

## 2. 已实现的核心功能

1. **86 五笔核心输入法规则（`WubiEngine`）**
   - **一级简码 / 词组 / 全码检索**：内置 25 个一级简码（`a` 工、`q` 我、`w` 人等）及常用字词，并支持启动时自动合并 DLL 同目录下的 `wubi86.txt` UTF-8 码表。
   - **四码唯一自动上屏**：输入第 4 码且无重码时立即自动上屏（如 `wqvb` -> `你好`、`ggtt` -> `五笔`）。
   - **第五码顶字上屏**：四码有重码时继续输入第 5 个字母，自动将当前首选词顶上屏，并用第 5 码开启新一轮输入。
   - **`z` 万能学习键**：支持含 `z` 的通配查询（如 `wqz`）。
   - **选词与翻页**：空格首选上屏、`1-5` 数字选词、`Enter` 编码原文上屏、`Backspace` 退格、`Esc` 取消、`-`/`=` 或 `PageUp`/`PageDown` 翻页、`↑`/`↓` 切换候选。
   - **中英文切换与中文标点**：支持单按 `Shift` 或 `Ctrl+Space`（Preserved Key）切换中英文模式，支持中文模式下常用全角标点直接上屏。

2. **标准 Windows TSF 接入（`QiQiTextService`）**
   - 实现 `ITfTextInputProcessorEx`、`ITfThreadMgrEventSink`、`ITfKeyEventSink`、`ITfCompositionSink`。
   - 所有文档读写与坐标获取均通过 `ITfContext::RequestEditSession`（`LambdaEditSession`）安全执行。
   - 处理宿主主动终止组合（`OnCompositionTerminated`）与焦点切换（`OnSetFocus`），兼容 Win32、Chromium/Electron 与 Office。
   - 实现 `ITfCandidateListUIElement` 并注册 `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`，同时支持**桌面无焦点弹出候选窗**与**全屏游戏 UILess 模式**。

3. **日志系统（`spdlog`）**
   - 集成 `spdlog`（启用 C++ 标准库 `<format>` 与宽字符文件名支持），同时输出到 `OutputDebugString`（`msvc_sink_mt`）与滚动日志文件（默认路径 `%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`，单文件上限 5MB，保留 3 个备份）。

4. **版本号机制（`VERSION` + `version.h` + `version.rc`）**
   - 以根目录 `VERSION` 文件作为单一事实源，CMake 配置期自动提取当前 Git 短提交哈希（`git rev-parse --short=7 HEAD`），生成 `version.h`（提供 `qiqi::version::kCurrentVersion`、`kVersion`、`kFullVersion`、`kGitCommit` 及三路比较 `SemanticVersion`）和 Windows PE 资源 `version.rc`（为 `qiqiime.dll` 嵌入文件属性版本信息）。

---

## 3. 编译与测试（CMake + vcpkg + Visual Studio 2026）

需要 CMake 3.25 或更高版本、vcpkg（Visual Studio 2026/2022 内置或通过 `VCPKG_ROOT` 指定），以及支持 C++26 草案模式的编译器。Visual Studio 2026 的 MSVC 使用 `/std:c++latest`，可用的 C++26 特性取决于编译器版本，并不代表完整实现 C++26。

项目使用 **vcpkg Manifest 模式**（`vcpkg.json`）管理依赖，CMake 配置时会自动检测 `VCPKG_ROOT` 或 Visual Studio 内置 vcpkg 工具链并安装依赖。

打开 **x64 Native Tools Command Prompt for VS 2026**（或 PowerShell），执行：

```powershell
cd C:\sample\qiqiime
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

构建产物位于：
- `C:\sample\qiqiime\build\Release\qiqiime.dll`（TSF 输入法组件）
- `C:\sample\qiqiime\build\Release\wubi86.txt`（五笔码表）
- `C:\sample\qiqiime\build\Release\qiqi_engine_test.exe`（引擎单元测试）

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
