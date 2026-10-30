# 奇奇五笔 (QiQiIME) 开发与构建指南

本文档面向开发者，介绍 **奇奇五笔（QiQiIME）** 的源码目录结构、构建环境要求、双架构编译、单元测试、静态代码分析与日志调试规范。

---

## 1. 开发环境要求

- **操作系统**：Windows 10 / Windows 11 (`x64`)
- **编译器与工具链**：
  - Visual Studio 2026（或 2022，安装“使用 C++ 的桌面开发”工作负载，支持 `/std:c++latest` C++26 草案模式）
  - CMake 3.25 或更高版本
  - vcpkg（使用 Visual Studio 内置 vcpkg 或通过 `VCPKG_ROOT` 环境变量指定，采用 Manifest 模式自动管理 `spdlog` 与 `ms-gsl`）

---

## 2. 源码目录结构

```text
qiqiime/
├── CMakeLists.txt              # 根 CMake 构建配置（工具链探测、版本生成、C++26 标准与子目录调度）
├── CMakePresets.json           # CMake Presets (支持 x64 与 x86/Win32 双架构配置、构建与测试)
├── LICENSE                     # GNU General Public License v3.0 (GPL-3.0)
├── VERSION                     # 项目版本号单一事实源 (SemVer, 当前 0.8.3)
├── vcpkg.json                  # vcpkg 依赖清单 (spdlog, ms-gsl)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
│   ├── config.ini              # 默认 UTF-8 INI 配置文件模板
│   ├── wubi86_jidian.txt       # 极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条）
│   ├── wubi86_jidian_extra.txt # 极点 86 五笔现代扩展词库（Apache-2.0，2,100 编码 / 2,216 词条）
│   └── wubi86_haifeng.txt      # 海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条）
├── docs/
│   ├── ARCHITECTURE.md         # 【架构文档】分层架构、TSF 生命周期、状态机、内存映射与安全模型
│   ├── DEVELOPMENT.md          # 【开发文档】源码结构、编译构建、单元测试、静态检查与日志规范
│   └── DEPLOYMENT.md           # 【部署文档】安装注册、WOW64 部署、MSI/ZIP 发行包打包、升级与卸载指南
├── installer/
│   ├── QiQiIME.wxs             # WiX Toolset (v4+) Windows Installer (.msi) 安装包配置
│   ├── zh-CN.wxl               # 简体中文 (LCID 2052) MSI 本地化词条
│   └── en-US.wxl               # 英文 (LCID 1033) MSI 本地化词条
├── scripts/
│   ├── common.ps1              # 脚本公共函数与常量模块
│   ├── build_all.ps1           # 一键构建、测试并可选打包 (-Package) x64 与 x86 双架构脚本
│   ├── register_ime.ps1        # 一键校验产物、注册输入法并核验注册表/TSF Profile 脚本
│   └── unregister_ime.ps1      # 一键卸载输入法并核验注册表清理脚本
├── src/
│   ├── CMakeLists.txt          # 源码目标构建配置（qiqi_engine 静态库 + qiqiime.dll）
│   ├── common/                 # 公共基础模块
│   │   ├── config.h/.cpp       # UTF-8 INI 配置解析与原子写持久化
│   │   ├── guids.h             # CLSID / Profile GUID / PreservedKey GUID / 窗口类名常量
│   │   ├── i18n.h              # 简体中文 (zh-CN) / 英文 (en-US) / 跟随系统 (Auto) 界面文案表
│   │   ├── logger.h/.cpp       # 基于 spdlog 的 noexcept 安全日志模块与最小权限文件读取
│   │   ├── monitor_dpi.h       # 多显示器工作区查询与 Per-Monitor DPI 缩放
│   │   ├── scoped_handle.h     # 泛型零开销 RAII 句柄模板 ScopedHandle<Traits> 与 COM/GDI/剪贴板包装器
│   │   ├── sound.h             # 零堆分配栈/静态数组 PCM WAV 音效合成器
│   │   ├── theme.h             # Windows 深色/浅色系统主题检测与暗色菜单/窗口适配
│   │   ├── version.h.in        # C++ 编译期版本号与 Git Commit 元信息头文件模板
│   │   └── version.rc.in       # Windows DLL 版本资源 (VERSIONINFO) 模板
│   ├── engine/                 # 五笔核心引擎模块（无 COM 依赖，可独立单测）
│   │   ├── wubi_binary_dict.h/.cpp # 4 字节紧凑编码索引 + CreateFileMappingW 只读内存映射词库 (.qdict)
│   │   ├── wubi_fsm.h          # 编译期 (consteval/constexpr) 7 状态 × 15 事件五笔有限状态机
│   │   ├── wubi_default_dict.h # 编译期 consteval 排序的内置核心 86 五笔码表 (.rdata)
│   │   └── wubi_engine.h/.cpp  # 五笔状态机、查词、渐进编码提示、动态词组调频与自动取码造词
│   ├── tsf/                    # Windows TSF 框架接入与 COM 服务模块
│   │   ├── tip_service.h/.cpp  # TSF 核心服务（ITfTextInputProcessorEx / KeyEvent / Composition / Configure）
│   │   ├── display_attribute.h/.cpp # ITfDisplayAttributeProvider 组合串虚线下划线显示属性
│   │   ├── edit_session.h      # 现代 C++ Lambda 封装的 ITfEditSession
│   │   ├── class_factory.h/.cpp# COM IClassFactory 实现
│   │   ├── register.h/.cpp     # COM 注册表、TSF Language Profile 与 Category 注册（支持 WOW64）
│   │   ├── dllmain.cpp         # DLL 入口、窗口类清理与导出函数
│   │   └── qiqiime.def         # COM DLL 模块定义文件
│   └── ui/                     # 界面交互模块
│       ├── candidate_ui.h/.cpp # Direct2D/DirectWrite 候选窗 + ITfCandidateListUIElement（支持游戏 UILess）
│       ├── langbar_button.h/.cpp # Windows 任务栏系统托盘“中/英”指示器与右键快捷菜单
│       ├── status_bar.h/.cpp   # 桌面浮动状态条（4x4 超采样 32 位 ARGB 抗锯齿矢量图标、Tooltip、右键菜单）
│       └── settings_dialog.h/.cpp # 属性设置对话框与“关于奇奇五笔”对话框
└── tests/
    ├── CMakeLists.txt          # 测试目标构建配置（qiqi_engine_test + qiqi_tsf_ui_test）
    ├── common.h/.cpp           # 公共测试断言宏、日志系统、版本号、INI 配置、RAII 与音效测试
    ├── engine_dict.cpp         # 词库测试（词条查询、内存映射最小权限保护、五笔自动取码与在线造词）
    ├── engine_input.cpp        # 输入状态机测试（简码、四码上屏、分号/单引号选词、动态调频、z 临时英文、标点）
    ├── engine_main.cpp         # qiqi_engine_test 测试入口
    ├── tsf_mocks.h             # TSF COM 接口 Mock 实现（ITfThreadMgr / ITfContext / ITfRange 等）
    ├── ui_candidate.cpp        # 候选窗 DirectWrite、托盘按钮、浮动状态条、设置对话框与关于对话框单测
    ├── tsf_com.cpp             # TSF COM 基础测试（LambdaEditSession、ClassFactory、DLL 导出与注册表）
    ├── tsf_service.cpp         # TSF 核心服务全链路测试（激活/停用、按键处理、组合串、菜单动作与热重载）
    └── tsf_ui_main.cpp         # qiqi_tsf_ui_test 测试入口
```

---

## 3. 编译与单元测试

### 3.1 一键构建并测试双架构（`x64` + `x86`）

```powershell
# 编译并运行 x64 与 x86 全部单元测试
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1

# 编译、测试并生成独立发行包 (dist/QiQiIME-v<version>-win-x64-x86.zip)
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
```

### 3.2 使用 CMake Presets 单独构建与调试

```powershell
# 配置、编译并运行 x64 Release 测试
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure

# 配置、编译并运行 x86 (Win32) Release 测试
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release --output-on-failure
```

构建产物位于 `build\<x64|x86>\Release\`：
- `qiqiime.dll`：TSF 输入法核心组件（`x64` 约 652 KB，`x86` 约 580 KB）
- `qiqi_engine_test.exe`：五笔核心引擎与基础模块单元测试
- `qiqi_tsf_ui_test.exe`：TSF COM 服务与 UI 交互单元测试

---

## 4. 代码规范与静态代码检查（`CppCoreCheck`）

项目遵循 **C++ Core Guidelines**，全面采用 `gsl::span`、`gsl::not_null`、`gsl::narrow_cast`、`gsl::at`、`gsl::finally` 以及自定义零开销移动语义 `ScopedHandle<Traits>` RAII 模板。

可通过以下命令运行 MSVC `CppCoreCheck` 静态分析：

```powershell
cmake --build build\x64 --config Release --target qiqi_engine qiqiime -- /p:RunCodeAnalysis=true /p:CodeAnalysisRuleSet=CppCoreCheckRules.ruleset
```

---

## 5. 版本号管理与排障日志规范

### 5.1 版本号生成机制
- 根目录下的 [VERSION](../VERSION) 文件是版本号的单一事实源（SemVer 格式，如 `0.7.0`）。
- 修改 `VERSION` 后，CMake 会自动重新配置并提取当前 Git 短提交哈希（`git rev-parse --short=7 HEAD`）及目标架构（`x64` / `x86`），自动更新：
  - `build/<arch>/generated/common/version.h`（提供 `qiqi::version::kCurrentVersion`、`kFullVersion`、`kArch`）
  - `build/<arch>/generated/common/version.rc`（为 `qiqiime.dll` 嵌入 PE `VERSIONINFO` 资源）

### 5.2 结构化排障日志
- **日志路径**：`%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`（单文件上限 5 MB，保留 3 个滚动备份，同时输出至 `OutputDebugString`）。
- **单行格式**：
  ```text
  [%Y-%m-%d %H:%M:%S.%e] [%n] [pid:%P] [tid:%t] [%l] %v
  ```
  其中 `[%n]` 自动记录加载输入法的宿主可执行文件名（如 `[notepad.exe]`、`[chrome.exe]`）。
- **隐私与异常安全**：所有 `QIQI_LOG_*` 宏均内建 `try/catch` 异常隔离（可在 `noexcept` 函数中安全调用），且严禁在日志中记录用户的私密上屏文字内容。
