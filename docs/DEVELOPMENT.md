# 奇奇五笔 (QiQiIME) 开发与构建指南

本文档面向开发者，介绍 **奇奇五笔（QiQiIME）** 的源码目录结构、构建环境要求、双架构编译、单元测试、静态代码分析与日志调试规范。

---

## 1. 开发与编译环境要求

| 工具 / 组件 | 版本与配置要求 |
|---|---|
| **操作系统** | Windows 10（1809+）或 Windows 11 (`x64`) |
| **Visual Studio / MSVC** | Visual Studio 2026 或 Visual Studio 2022（17.10+），安装 **“使用 C++ 的桌面开发”** 工作负载，包含 MSVC `x64` 与 `x86` 编译器及 C++ 核心功能检查工具（`CppCoreCheck`，开启 `/std:c++latest` C++26 草案模式） |
| **Windows SDK** | Windows 10 / 11 SDK（`10.0.22621.0` 或 `10.0.26100.0` 及以上，提供 `<msctf.h>`、`<ctffunc.h>`、`<d2d1.h>`、`<dwrite.h>`） |
| **CMake** | CMake `3.25` 或更高版本 |
| **vcpkg** | Visual Studio 内置 vcpkg 或通过 `VCPKG_ROOT` 环境变量指定（Manifest 模式自动安装 `ms-gsl`） |
| **PowerShell** | Windows PowerShell 5.1 或 PowerShell 7+（用于运行构建、注册与打包脚本） |
| **WiX Toolset（可选）** | .NET SDK 8.0+ 与 WiX CLI v4/v7+（`dotnet tool install --global wix`，用于生成 `.msi` 安装包） |

---

## 2. 源码目录结构

```text
qiqiime/
├── CMakeLists.txt              # 根 CMake 构建配置（工具链探测、版本生成、C++26 标准与子目录调度）
├── CMakePresets.json           # CMake Presets (支持 x64 与 x86/Win32 双架构配置、构建与测试)
├── LICENSE                     # GNU General Public License v3.0 (GPL-3.0)
├── VERSION                     # 项目版本号单一事实源 (SemVer, 当前 0.8.4)
├── vcpkg.json                  # vcpkg 依赖清单 (ms-gsl)
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
│   ├── CMakeLists.txt          # 仅组织模块及公共版本资源依赖
│   ├── common/                # 公共基础模块（qiqi_common 静态库）
│   │   ├── CMakeLists.txt      # 公共模块构建与版本资源生成
│   │   ├── config.h/.cpp       # 配置持久化
│   │   ├── d2d_controls.h/.cpp # 可复用 Fluent D2D 控件
│   │   ├── d2d_util.h          # D2D / DirectWrite RAII 封装
│   │   ├── com_object.h        # COM 对象与 TSF Compartment RAII 封装
│   │   ├── guids.h             # CLSID、Profile GUID、窗口类名常量
│   │   ├── i18n.h              # 中英文及跟随系统的界面文案
│   │   ├── logger.h/.cpp       # Windows ETW TraceLogging 日志
│   │   ├── scoped_handle.h     # 泛型零开销 RAII 句柄模板
│   │   └── ...                 # 文件、DPI、音效、主题、版本模板等公共代码
│   ├── Ime/                   # 输入法
│   │   ├── CMakeLists.txt      # qiqi_engine 静态库及 qiqiime.dll 构建
│   │   ├── engine/             # 五笔状态机、词库、映射、动态调频与造词
│   │   ├── tsf/                # COM 服务、注册、显示属性与编辑会话
│   │   └── ui/                 # 候选窗、托盘按钮、浮动状态条与快捷菜单
│   └── tools/                 # 独立工具，每个工具拥有自己的子目录
│       ├── CMakeLists.txt      # 组织各工具
│       └── settings/
│           ├── CMakeLists.txt  # qiqi_settings_ui 静态库及 QiQiIMESettings.exe
│           ├── main.cpp # 独立设置程序入口
│           ├── window.h/.cpp # 设置窗口创建与消息处理
│           ├── controls.cpp # 控件布局、配置绑定、语言更新与卡片
│           ├── internal.h       # 设置窗口内部上下文与控件 ID
│           ├── about.cpp # 关于窗口
│           └── util.h   # 共享图标及模态消息循环
└── tests/
    ├── CMakeLists.txt          # 仅组织测试模块
    ├── common/
    │   ├── CMakeLists.txt      # qiqi_common_test
    │   ├── test_support.h      # 各测试模块共享的断言及辅助函数
    │   ├── common.cpp          # 配置、日志、版本、RAII、音效测试
    │   ├── common_main.cpp     # 公共模块测试入口
    │   └── ui_d2d_controls.cpp # 共享 D2D 控件测试
    ├── Ime/
    │   ├── CMakeLists.txt      # qiqi_engine_test 与 qiqi_tsf_ui_test
    │   ├── engine/             # 词库、输入与模式测试
    │   ├── tsf/                # COM Mock、注册及 TSF 服务测试
    │   ├── ui/                 # 候选窗、托盘菜单与状态条测试
    │   ├── engine_main.cpp     # 引擎测试入口
    │   └── tsf_ui_main.cpp      # TSF / IME UI 测试入口
    └── tools/
        ├── CMakeLists.txt      # 组织各工具测试
        └── settings/
            ├── CMakeLists.txt  # qiqi_settings_test
            ├── main.cpp # 设置工具测试入口
            ├── window.cpp # 设置 / 关于窗口测试
            └── edges.cpp # 配置及生命周期边界测试
```

源码与测试目录保持相同的模块布局。模块的 `CMakeLists.txt` 在顶层工程中定义独立目标，可通过 `cmake --build --preset x64-release --target <目标名>` 按目标构建。新增工具时，在 `src/tools/<工具名>` 与 `tests/tools/<工具名>` 分别添加自己的 `CMakeLists.txt`，并由各自的 `tools/CMakeLists.txt` 注册。

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
- `QiQiIMESettings.exe`：独立设置工具
- `qiqi_common_test.exe`：公共基础模块与共享 D2D 控件测试（`QiQiCommonTest`）
- `qiqi_engine_test.exe`：五笔核心引擎测试（`QiQiEngineTest`）
- `qiqi_tsf_ui_test.exe`：TSF COM 服务与 IME UI 测试（`QiQiTsfUiTest`）
- `qiqi_settings_test.exe`：设置 / 关于窗口及配置边界测试（`QiQiSettingsTest`）

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
