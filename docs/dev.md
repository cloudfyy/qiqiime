# 奇奇五笔 (QiQiIME) 开发与构建指南

本文档面向开发者，介绍 **奇奇五笔（QiQiIME）** 的源码目录结构、构建环境要求、双架构编译、单元测试、静态代码分析与日志调试规范。

---

## 1. 开发与编译环境要求

| 工具 / 组件 | 版本与配置要求 |
|---|---|
| **操作系统** | Windows 10（1809+）或 Windows 11 (`x64`) |
| **Visual Studio / MSVC** | 默认预设使用 Visual Studio 2026，安装 **“使用 C++ 的桌面开发”** 工作负载、MSVC `x64/x86` 编译器与 `CppCoreCheck`；项目使用 `/std:c++latest` 草案模式，不表示完整实现 C++26。VS 2022 等其他版本需选择相应生成器并验证实际代码兼容性 |
| **Windows SDK** | Windows 10 / 11 SDK（`10.0.22621.0` 或 `10.0.26100.0` 及以上，提供 `<msctf.h>`、`<ctffunc.h>`、`<d2d1.h>`、`<dwrite.h>`） |
| **CMake** | **`4.4.4` 或更高版本**；默认使用 VS 2026 预设 |
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
├── VERSION                     # 项目版本号单一事实源 (SemVer)
├── vcpkg.json                  # vcpkg 依赖清单 (ms-gsl)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
│   ├── wubi86_jidian.txt       # 极点 86 五笔核心主词库（Apache-2.0，77,169 编码 / 89,254 词条）
│   ├── wubi86_jidian_extra.txt # 极点 86 五笔现代扩展词库（Apache-2.0，2,098 编码 / 2,216 编码-词语条目）
│   └── wubi86_haifeng.txt      # 海峰 86 五笔超大字符集词库（BSD-3-Clause，100,051 编码 / 144,374 词条）
├── docs/
│   ├── arch.md         # 【架构文档】分层架构、TSF 生命周期、状态机、内存映射与安全模型
│   ├── dev.md          # 【开发文档】源码结构、编译构建、单元测试、静态检查与日志规范
│   └── deploy.md           # 【部署文档】安装注册、WOW64 部署、MSI/ZIP 发行包打包、升级与卸载指南
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
│   │   ├── about_dialog.h/.cpp # 共享关于窗口（供 IME、设置及日志监控复用）
│   │   ├── config.h/.cpp       # 配置持久化
│   │   ├── d2d_controls.h/.cpp # 可复用 Fluent D2D 控件
│   │   ├── command_ui.h/.cpp    # 词库管理器与日志监控器共用的原生菜单/图标工具条
│   │   ├── window.h            # 共享窗口生命周期、主题、布局与消息循环辅助
│   │   ├── d2d_util.h          # D2D / DirectWrite RAII 封装
│   │   ├── dialog_util.h       # 共享对话框辅助与模态消息循环
│   │   ├── com_object.h        # COM 对象与 TSF Compartment RAII 封装
│   │   ├── guids.h             # CLSID、Profile GUID、窗口类名常量
│   │   ├── i18n.h              # 中英文及跟随系统的界面文案
│   │   ├── logger.h/.cpp       # Windows ETW TraceLogging 日志
│   │   ├── scoped_handle.h     # 类型化 RAII 句柄模板
│   │   └── ...                 # 文件、DPI、音效、主题、版本模板等公共代码
│   ├── ime/                   # 输入法
│   │   ├── CMakeLists.txt      # qiqi_engine 静态库及 qiqiime.dll 构建
│   │   ├── comp/               # 组合编辑票据、开始准备、范围写入、显示属性与清理协调
│   │   ├── engine/             # 五笔状态机、词库、映射、动态调频与造词
│   │   ├── fsm/                # 状态/事件定义、按键映射、静态分派与分页辅助（fsm.h 为聚合入口）
│   │   ├── tsf/                # COM 服务、注册、显示属性与编辑会话
│   │   └── ui/                 # 候选窗、托盘按钮、浮动状态条与快捷菜单
│   └── tools/                 # 独立工具，每个工具拥有自己的子目录
│       ├── CMakeLists.txt      # 组织各工具
│       ├── settings/
│       │   ├── CMakeLists.txt  # qiqi_settings_ui 静态库及 QiQiIMESettings.exe
│       │   ├── main.cpp # 独立设置程序入口
│       │   ├── window.h/.cpp # 设置窗口创建与消息处理
│       │   ├── controls.cpp # 控件布局、配置绑定、语言更新与卡片
│       │   ├── internal.h       # 设置窗口内部上下文与控件 ID
│       │   └── message.cpp # 复用 D2D 控件的隐私确认、保存失败与通知失败提示窗口
│       ├── log_monitor/
│       │   ├── CMakeLists.txt  # qiqi_log_monitor 静态库及 QiQiIMELogMonitor.exe
│       │   ├── main.cpp        # 独立日志监控程序入口
│       │   ├── model.h/.cpp    # 事件缓冲、筛选、复制与导出
│       │   ├── collector.h/.cpp # ETW 会话与 TDH 解码
│       │   └── window.h/.cpp   # 日志监控窗口与详情分隔条
│       └── dict_manager/
│           ├── CMakeLists.txt  # qiqi_dict_manager 静态库及 QiQiIMEDictManager.exe
│           ├── main.cpp        # 独立词库与词频管理程序入口
│           ├── model.h/.cpp    # 多源词库/置顶/屏蔽/词频模型、五笔自动取码与导入导出
│           ├── internal.h      # 词库管理窗口内部状态与来源映射
│           └── window.h/.cpp   # 词库管理窗口、虚拟列表与跨进程同步交互
└── tests/
    ├── CMakeLists.txt          # 仅组织测试模块
    ├── common/
    │   ├── CMakeLists.txt      # qiqi_common_test
    │   ├── test_support.h      # 各测试模块共享的断言及辅助函数
    │   ├── common.cpp          # 配置、日志、版本、RAII、音效测试
    │   ├── common_main.cpp     # 公共模块测试入口
    │   └── ui_d2d_controls.cpp # 共享 D2D 控件测试
    ├── ime/
    │   ├── CMakeLists.txt      # qiqi_engine_test 与 qiqi_tsf_ui_test
    │   ├── comp/               # 对应组合组件的独立测试
    │   ├── engine/             # 词库、输入与模式测试
    │   ├── tsf/                # COM Mock、注册及 TSF 服务测试
    │   ├── ui/                 # 候选窗、托盘菜单与状态条测试
    │   ├── engine_main.cpp     # 引擎测试入口
    │   └── tsf_ui_main.cpp      # TSF / IME UI 测试入口
    └── tools/
        ├── CMakeLists.txt      # 组织各工具测试
        ├── settings/
        │   ├── CMakeLists.txt  # qiqi_settings_test
        │   ├── main.cpp # 设置工具测试入口
        │   ├── window.cpp # 设置 / 关于窗口测试
        │   ├── edges.cpp # 配置及生命周期边界测试
        │   ├── version.cpp # 设置 EXE 版本资源测试
        │   ├── save.cpp  # 保存失败、重试、取消及通知失败提示测试
        │   └── message.cpp # 各设置窗口的主题、语言、提示框键盘及模态行为测试
        ├── log_monitor/
        │   ├── CMakeLists.txt  # qiqi_log_monitor_test
        │   └── main.cpp        # 实时日志监控模型、GUI、版本与 ETW 集成测试
        └── dict_manager/
            ├── CMakeLists.txt  # qiqi_dict_manager_test
            └── main.cpp        # 词库与词频管理模型、跨进程同步、GUI 与版本测试
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
- `qiqiime.dll`：TSF 输入法核心组件；大小随版本、架构及构建选项变化
- `QiQiIMESettings.exe`：独立设置工具
- `QiQiIMELogMonitor.exe`：独立实时 ETW 日志监控工具
- `qiqi_common_test.exe`：公共基础模块与共享 D2D 控件测试（`QiQiCommonTest`）
- `qiqi_change_sync_test.exe`：公共跨进程日志、并发写入与真实 AppContainer 只读同步测试（`QiQiChangeSyncTest`）
- `qiqi_engine_test.exe`：五笔核心引擎测试（`QiQiEngineTest`）
- `qiqi_tsf_ui_test.exe`：TSF COM 服务与 IME UI 测试（`QiQiTsfUiTest`）；`--etw-integration` 模式另注册为 `QiQiEtwIntegrationTest`
- `qiqi_settings_test.exe`：设置 / 关于窗口及配置边界测试（`QiQiSettingsTest`）；直接编译并调用独立程序的 `wWinMain`，在隔离注册表下验证首次启动实际创建可见窗口、再次启动恢复已最小化窗口、关闭后可重新启动，以及取消不保存配置。
- `qiqi_log_monitor_test.exe`：监控器测试（`QiQiLogMonitorTest`）；`--integration` 模式另注册为 `QiQiLogMonitorIntegrationTest`

任务 36 的原生 TSF 作用域/只读/compartment 契约及原生 EDIT 样式检查已纳入 `QiQiTsfUiTest`。显式运行 `.\build\x64\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics`（x86 对应替换路径）还会观察现有禁用按键与异步编辑行为；`[OBSERVE]` 不是保护已实现的通过断言。实验边界、双架构复现步骤及真实宿主待验证矩阵见[禁输入场景研究](restricted.md)。不会读取密码或真实剪贴板，不修改生产输入策略。

设置窗口的单实例互斥量仅由 `ShowSettingsDialog` 持有，独立 EXE 和 IME 调用方不得提前创建同名互斥量，否则对话框会把自身误判为另一个实例并直接退出。创建失败记录真实 Win32 错误码并停止创建窗口；回归同时覆盖已存在的互斥量和同名异类内核对象，避免只检查快捷方式目标或进程退出码而遗漏实际启动失败。

`QiQiStandaloneToolsTest` 另直接启动当前架构的设置、词库管理器和日志监控器 EXE，核对真实可见窗口及窗口类、正常关闭和重新启动，并验证设置程序重复启动恢复原有最小化窗口。测试只关闭自己启动的进程，不修改快捷方式或安装目录；作为 `RUN_SERIAL` 测试运行，避免与其他 GUI 回归争用窗口或单实例互斥量。

公共文件工具回归覆盖包含 NUL 的 UTF-8/UTF-16 转换、64 MiB 读取上限及超过上限一个字节、缺失文件、原子替换失败时原文件保留及临时文件清理、解除锁定后重试，以及超过 `MAX_PATH` 的 `LOCALAPPDATA`、缺失变量时的临时目录回退。目录查询根据 API 返回的所需容量重试，不把有效长路径当作缺失或使用截断结果。共享断言仍输出失败位置并非零退出，但关闭 CRT 的交互式 abort/WER 报告，避免失败测试等待桌面错误对话框。

### 3.3 组合组件与生命周期测试组织

- [`src/ime/comp`](../src/ime/comp) 集中组合相关组件，文件使用 `attribute`、`cleanup`、`edit`、`range`、`start` 短名；服务仍在 [`src/ime/tsf`](../src/ime/tsf) 负责宿主接口、当前组合及 UI 协调，目录调整不改变 FSM 或生命周期边界。
- [`tests/ime/comp`](../tests/ime/comp) 镜像组件目录，分别保留编辑票据/任务、清理、范围写入及开始准备测试；共享 Mock 仍在 [`mocks.h`](../tests/ime/tsf/mocks.h)。
- [`service_lifecycle.cpp`](../tests/ime/tsf/service_lifecycle.cpp) 仅保留有序入口，依次调用[延迟会话](../tests/ime/tsf/service_deferred.cpp)、[上下文/文档](../tests/ime/tsf/service_context.cpp)、[窗口生命周期](../tests/ime/tsf/service_windows.cpp)、[重入](../tests/ime/tsf/service_reentrancy.cpp)、[服务清理](../tests/ime/tsf/service_cleanup.cpp)及[原生 TSF](../tests/ime/tsf/service_native.cpp)测试。组件测试仍在原窗口测试的配置/键盘快照之前运行；入口的 key/composition sink 强引用保留到全部子套件返回，各测试块的局部 COM/RAII 作用域不变。

---

## 4. 代码规范与静态代码检查（`lint`）

日志监控器的筛选回归另覆盖匿名进程、PID 0/MAXDWORD，以及下拉框展开期间收到同名新进程、收起后无新增日志时的补偿刷新；真实多进程 ETW 测试验证 PID 从单选置灰转为“全部/多选项”，不只检查数据收集函数。

ETW 集成测试仍要求零丢失，不通过放宽断言掩盖采集失败；失败输出保留实际丢失计数及缓冲统计，区分解码失败与 ETW 事件丢失。

模态退出测试先移除较高优先级的排队消息再检查 `WM_QUIT`：该消息按低优先级合成，仅筛选 `WM_QUIT` 的 `PeekMessage` 可能暂时取不到；回归用例固定排入 `WM_APP` 后验证退出码。公共 ETW 写入测试注入成功、内存不足、事件过大及拒绝访问，验证实际错误码、非递归诊断及调用线程错误状态保留。

项目以 **C++ Core Guidelines** 为代码规范，使用 `std::span`、`gsl::not_null`、`gsl::narrow_cast`、`gsl::at`、`gsl::finally` 及 `ScopedHandle<Traits>` RAII 模板；不把采用这些工具等同于所有代码已经完全合规。`lint` target 使用 MSVC CppCoreCheck 检查 C++ Core Guidelines 与 GSL 规则，并使用 PSScriptAnalyzer 检查 `scripts/` 和 `tests/` 下的 PowerShell 脚本。C++ 检查通过 Visual Studio 的 `ALL_BUILD` 一次重建全部生产和测试目标，避免逐目标重复重建公共依赖，并自动纳入新增目标；按既有规则排除 C26490，其他仓库源码诊断的去重基线为 **0**，出现任何诊断即失败。PowerShell 门禁检查 Error 级诊断，不把通过门禁等同于没有 Warning 级风格或兼容性建议。

安装 PSScriptAnalyzer 后，可从已配置的 Visual Studio CMake 构建目录运行：

```powershell
Install-Module -Name PSScriptAnalyzer -Scope CurrentUser
cmake --build build\x64 --config Release --target lint
```

项目常规编译另启用 MSVC `/W4`；`lint` 会在指定配置下重建启用静态分析的目标。

### 4.1 原生执行覆盖率

使用本机 Visual Studio Enterprise 的 `Microsoft.CodeCoverage.Console` 收集原生 C++ 覆盖率；Debug 产物须保留 PDB 并以 `/PROFILE /INCREMENTAL:NO` 链接。覆盖率配置的 `ModulePaths/IncludeDirectories` 指向当前构建产物目录，`Sources/Include` 仅匹配仓库 `src`，启用 `EnableStaticNativeInstrumentation`、`EnableStaticNativeInstrumentationRestore` 和 `CollectFromChildProcesses`，以覆盖真实工具子进程；报告与插桩产物不用于安装。

收集时串行运行完整 `ctest --test-dir build\x64 -C Debug --output-on-failure --parallel 1`，并单独运行双架构 Release 全套回归。Cobertura 的模块汇总会重复计算静态库源码；源码行覆盖率应按文件路径和行号去重，任一模块命中即视为覆盖。同样必须列出未出现在报告中的实现文件，不能只看已有模块的百分比。该工具导出的 `branch-rate` 不代表已经测得所有 C++ 分支覆盖率；真实宿主矩阵、权限及资源耗尽等边界仍需独立验证。

词库管理器与日志监控器采用原生菜单和窄窗口自动换行的纯图标平面工具条，不使用独立 `BUTTON` 控件。两者共同使用 `qiqi_common` 中的 [`ToolCommandUi`](../src/common/command_ui.h)：应用提供命令 ID、本地化文案、语义图标、状态与菜单分组，共享层管理菜单所有权、菜单栏测量/绘制及助记键、工具项创建/更新、悬停提示、主题/高对比度和 DPI 尺寸。图标集中于 [`command_ui.cpp`](../src/common/command_ui.cpp)，不再分散到业务窗口中；工具项为 36 个逻辑像素的方形区域，居中绘制 20 个逻辑像素的 GDI 矢量图标，无外部图片或字体依赖，绘制对象使用现有 RAII 包装。工具条隐藏文字但保留原生命令名称及本地化提示。词库管理器的行旁保存使用同一组件的紧凑模式。业务状态、筛选、采集及行内编辑仍由各工具维护，未抽象整个窗口。共享层通过调用方错误回调报告失败，不直接初始化或写入 ETW；词库管理器使用原有日志，监控器仅写独立诊断 Provider，避免采集自身 IME 日志形成反馈。

两个 GUI 回归均覆盖中英文/自动语言、96/120/144/192 DPI、最小/默认/宽窗口的标签宽度、工具项尺寸、悬停提示、控件边界及重叠、语言单选状态、菜单助记键与无独立按钮；绘制测试检查共享图标非空且形状互异、浅色/深色/高对比度下的颜色，以及鼠标工具项和菜单命令。日志监控器另验证暂停/恢复、诊断/日志切换时菜单、图标、选中状态及提示同步，真实 ETW 集成仍验证开始/停止采集。

词库管理器首次打开默认选择用户词库，初始化时同步来源下拉框与 `DictFilter`；刷新、语言、主题及 DPI 切换保留当前来源，其他模型调用方的默认全来源筛选保持不变。来源名称不暴露 `^`、`!`、`QPF1` 等存储细节；词组优先顺序统一显示为“词组排序 / Phrase Ordering”，菜单、工具提示、状态和备份文案同步更新，底层格式与同步行为不变。GUI 回归验证空/非空用户词库的默认行数、切换全部来源、刷新及多语言后的选择保留，以及中英文来源和命令文案。

词库列表按 `ColumnId` 定义编辑、编码、词条、来源、顺位、操作六列。首列固定为 40 个逻辑像素，使用 `ToolCommandUi::DrawIcon` 的共享铅笔图标，在原生子项背景绘制后叠加，不为每行创建控件；图标为 16 个逻辑像素且受单元格宽高约束，尺寸、线宽、六列宽度随 DPI 更新，颜色沿用原生列表当前文字颜色以适配选择、主题和高对比度。编辑列通过鼠标命中及捕获处理单击，仅在同一行的编辑列内按下并释放才进入编码编辑；取消或失去捕获不触发。双击编码/词条使用点击位置命中子项，不依赖首列文字区域才有效的原生通知行号。回归覆盖真实鼠标单击、编码焦点、取消捕获、移出释放、双击/F2、切换已有及新增草稿，以及多语言/多 DPI/浅深色和高对比度下的原生绘制。

词库管理器顶部第二项为“新增词条”，保存图标仅放在当前编辑行的操作列。底部编辑区已移除；双击编码/词条或 F2 开始行内编辑，两个原生 EDIT 和一个小型图标工具条覆盖对应单元格，不创建独立 BUTTON。小工具条清除默认位图占位尺寸，避免小行高时原生工具项裁切。Enter/图标/菜单保存，Esc 取消，失焦不保存；编辑子类通过 `WM_GETDLGCODE` 保留键盘事件，组合输入期间不截获 Enter/Esc。新词采用 ID 0 的临时行，编辑身份保存来源、编码和词条快照，不依赖每次重建都会变化的模型 ID。修改列表的其他命令在编辑期间禁用；来源和搜索保持启用，获得焦点时复用取消编辑流程但不把焦点移回列表，直接放弃草稿并正常筛选。回归覆盖已有/新增草稿切到两个筛选控件后的焦点保留、临时行移除、下拉展开及筛选结果，确认不自动写入。定时同步延后到取消或提交，提交前检查外部变更，失败保留草稿。滚动、列宽、语言、主题和 DPI 更新重新定位编辑控件；行或列不可完整显示时隐藏覆盖控件而不丢弃草稿。

未保存状态按当前编码、词条与编辑开始时的原值比较；新词草稿以空内容为基线。仅有实际差异才在来源列及状态栏显示 `*`；还原原值会清除标记，未修改的已有词条保存时直接结束编辑，不写入词库。双击其他行或在另一行按 F2 会直接放弃当前草稿并切换，新建草稿的临时行同时移除；双击当前编辑行不丢弃内容。关闭窗口直接放弃草稿，不弹保存确认、不自动保存；只有主动执行保存命令才会写入更改。列表使用 `WS_CLIPSIBLINGS`，防止重绘覆盖同级的行内编辑控件和保存图标；回归测试同时覆盖原生工具条绘制及列表重绘后的实际可见图标。

`ReplaceUserEntry` 在同步通道排他锁内验证原词条仍存在，再一次写入文本词库完成编码和词条替换；原词条消失时明确失败，不重新创建旧记录。同码使用已有 Modify 增量格式，跨码编辑利用现有 `S_FALSE` 基线失效机制，令运行中的旧客户端通过快照加载完整更改，不发布无法兼容的增量正文。源文件、快照、缓存与元数据沿用已有写入路径，不承诺跨资源回滚。回归覆盖同码/跨码/无改动保存、无效输入、写入失败、磁盘重载、对端快照同步，以及 GUI 新增、双击/F2、Enter/Esc、组合输入、失焦不保存、外部冲突、非用户来源另存副本、滚动/列宽、双语/多 DPI、切换行放弃草稿和关闭无确认且不保存。

统一搜索由 `DictFilter::SetQuery` 在输入变化时一次性去除首尾空白、判断是否纯 ASCII 字母并规范编码大小写，逐条匹配不再重复分配规范化字符串；纯字母按编码包含匹配，其余按词条包含匹配，来源筛选同时生效。中英文搜索及编辑提示通过 cue banner 更新，不重置已有输入。回归保留工具条顺序、编码/中文/混合内容/空白搜索、来源组合筛选和搜索模式切换检查。

---

## 5. 版本号管理与排障日志规范

### 5.1 版本号生成机制
- 根目录下的 [VERSION](../VERSION) 文件是版本号的单一事实源（SemVer 格式，如 `0.7.0`）。
- 修改 `VERSION` 后，CMake 会自动重新配置并提取当前 Git 短提交哈希（`git rev-parse --short=7 HEAD`）及目标架构（`x64` / `x86`），自动更新：
  - `build/<arch>/generated/common/version.h`（提供 `qiqi::version::kCurrentVersion`、`kFullVersion`、`kArch`）
  - `build/<arch>/generated/common/version.rc`（为 `qiqiime.dll` 嵌入 PE `VERSIONINFO` 资源）
  - `build/<arch>/generated/tools/settings/version.rc`（为 `QiQiIMESettings.exe` 嵌入 PE `VERSIONINFO` 资源）
- DLL 与设置 EXE 复用 [`version.rc.in`](../src/common/version.rc.in) 模板及统一版本号，但分别设置文件类型、描述和原始文件名。设置 EXE 标记为应用程序（`VFT_APP`），描述包含目标架构；两者均保留中英文资源、公司信息、版权与产品信息。

### 5.2 结构化排障日志
- **监控步骤**：日志级别设置、原生调试器实时查看、`logman` 采集与 WPA 分析见[应用日志监控指南](log.md)。任务 55、51 已修复初始化及停用影响共享日志的问题；ETW 无需附加调试器。
- **初始化与生命周期**：正常入口调用 `EnsureInitialized`，幂等注册且不改变日志级别；`InitLogger` 额外设置级别。注册成功后才发布状态，查询及写日志不隐式注册；同进程 IME 实例共享，同名 Provider 在不同进程独立。`Deactivate` 不注销，模块清理在调用者结束后注销，不在 `DllMain` 注册、格式化日志或获取初始化锁。
- **焦点性能埋点**：任务 50 使用 `std::chrono::steady_clock` 记录纳秒耗时，仅在详细级别且有接收方时计时；按进程内递增编号关联焦点处理与重载详情，保留原检查/重载顺序。字段和真实宿主复现步骤见[焦点处理耗时](log.md#61-焦点处理耗时任务-50)，不能将状态相同或测试进程耗时当成可合并通知、需要后台化的依据。
- **日志提供程序**：Windows ETW TraceLogging 提供程序 `QiQiIME.TraceLogging`；仅在调试器附加时额外输出到 `OutputDebugString`，不会写入 `%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`。
- **日志字段与 ETW 元数据**：结构化事件包含级别（`Level`）、非零分类掩码（`Keyword`：`General`、`Lifecycle`、`Input`、`Composition`、`Dictionary`、`Sync`、`UI`、`Config`）、操作阶段（`Opcode`：`Info`、`Start`、`Stop`）、显式活动关联（`ActivityId` 与可选 `RelatedActivityId`）、宿主进程名、进程 ID、线程 ID 和消息。无活动事件显式写入 `GUID_NULL`，不修改或拾取宿主线程隐式 TLS `ActivityId`。`ShouldLog(Level, Keyword)` 在格式化前按会话级别与分类掩码短路。
- **隐私与异常安全**：所有 `QIQI_LOG_*` 宏均内建 `try/catch` 异常隔离（可在 `noexcept` 函数中安全调用），且严禁在日志中记录用户的私密上屏文字内容。
- **失败诊断**：保存实际返回值，记录操作名与错误码。COM 可复用 `log::CheckHResult`，输出 `hr=0x{:08x}`，只将失败 HRESULT 记为失败，`S_FALSE` 与 `TF_S_ASYNC` 不误报；该帮助函数保留调用线程的 `GetLastError`。
- **Win32 与注册表**：仅在 API 文档约定支持 `GetLastError` 时，于失败后立即捕获；注册表及 ACL API 使用其直接返回的错误码。读写成功但字节数不足时记录实际字节数和 `ERROR_SUCCESS`，不能输出陈旧的线程错误。`SetWindowSubclass`、部分 GDI 操作及输入校验失败只记录操作和条件，不编造错误码。
- **预期回退与资源所有权**：未配置的可选注册表值、未生成的缓存与正常无布局状态不是最终失败；探测采用调试/跟踪级别，已记录底层原因的上层消息仅说明回退或用户影响。函数提供程序与语言栏仅清理本激活周期成功注册/添加的项，真正注销失败保留诊断。
- **日志自身失败**：ETW 注册结果是 Win32 `ULONG`，不能按 HRESULT 判断；注册、格式化或编码失败使用不递归的调试输出兜底，日志内部不调用会再次产生日志的文本转换帮助函数。引擎日志不记录逐键键值、用户词编码或词语正文。

### 5.3 失败日志审计范围（任务 49）
- 已检查生产代码中的失败日志、COM 返回值、文件/注册表读写及异常兜底，覆盖 `common`、IME 引擎、TSF、候选窗/语言栏/状态条、设置程序及安装/构建脚本；测试与第三方依赖仅作为验证依据，未修改第三方代码。
- 修复实际 HRESULT 漏报、ETW 注册错误判断、注册表/文件读写及缓存映射错误码缺失、回调异常吞错，以及设置程序单实例互斥量失败；构建脚本补充阶段/架构/退出码诊断。保留可选能力失败后的原有回退，COM CLSID 必需值写入失败不再报告注册成功。
- 失败注入覆盖函数提供程序及语言栏部分成功、注册失败、注销失败、重复停用和再次激活；公共日志测试覆盖成功/异步 HRESULT 与错误码保留；配置、文件、词库、设置及绘制由现有回归测试覆盖。
- 本节记录任务 49 当时的审计范围，仅补失败诊断。任务 48、52、53 及 42–46 后续已完成对应实现与自动化验证，任务 51 的共享日志问题与任务 55 一并修复；任务 47 的真实宿主验收仍未完成，已有阶段性反馈见[实测记录](hosts.md)。
- 验证：x64/x86 Release 构建及各 4 组 CTest 全部通过，`lint` 的 CppCoreCheck 与 PSScriptAnalyzer 通过；模拟 CMake 返回 37，确认构建脚本输出阶段、架构和错误码并保留退出码。首次 x64 窗口交互测试失败且后续设置测试未结束，停止该测试进程后，TSF/UI 隔离重跑与最终全套重跑均通过；此记录不等同于已修复窗口交互测试的偶发性问题。

### 5.4 日志初始化与生命周期验证（任务 55、51）

- 公共测试覆盖失败注册的实际返回码、失败不发布已注册状态、成功重试、并发初始化只成功注册一次、注册完成前查询为 false、未初始化/重复关闭不注册，以及 Off 下的显式初始化。TSF/UI 测试覆盖停用一个实例保留 Provider、重新激活不重置 Off。
- `QiQiEtwIntegrationTest` 启动两个无调试器子进程加载真实 `qiqiime.dll`，覆盖采集先于/晚于初始化、一个进程退出后另一个继续输出、同进程多实例停用/重新激活、模块卸载重载及 Off 过滤。实际 ETL 经 TDH 解码并断言目标 PID、预期消息、级别和零丢失，不只检查会话存在。每个子进程隔离 HKCU 与 LOCALAPPDATA，不修改真实配置或词库。
- 采集仅启用自有测试 PID；唯一命名会话、句柄、子进程作业及临时文件按生命周期清理。普通权限缺少采集控制权限时显式报告 `StartTraceW error=5`，CTest 标记跳过（退出码 77），不能当作 ETW 验证通过。
- 定向运行：`ctest --test-dir build\x64 -C Release -R '^(QiQiCommonTest|QiQiTsfUiTest|QiQiEtwIntegrationTest)$' --output-on-failure`；x86 使用 `build\x86`。本次两个架构实际 ETW 测试均通过，没有因权限跳过；这些受控宿主验证不替代浏览器/UWP 的兼容性或真实输入体验测试。
- 验证：x64/x86 Release 完整构建及各 5 组 CTest 全部通过；CppCoreCheck 在既有 435 条基线以内，PSScriptAnalyzer 零错误。仅对 ETW SDK 必需的回调联合体局部说明并豁免 C26476，不替换 Windows ABI，也不扩大告警豁免范围。

### 5.5 延迟编辑会话测试（任务 42）

- [`MockTfContext` 与 `MockEditSessionQueue`](../tests/ime/tsf/mocks.h) 提供确定性的编辑会话调度，不使用后台线程或 `Sleep`。默认上下文的 `TF_ES_ASYNCDONTCARE` 请求仍立即执行；传入外部 `std::shared_ptr<MockEditSessionQueue>` 后排队。显式 `TF_ES_ASYNC` 必须排队，无调度器时明确失败；`TF_ES_SYNC` 立即执行，优先于待处理异步请求。
- 测试持有调度器，并将同一调度器传给需要验证的上下文；`PendingCount`/`PendingRequests` 检查待执行请求与目标，`RunNext(cookie)` 按 FIFO 执行一个请求并返回真实回调 HRESULT，默认 cookie 为 1。`ClearForCleanup` 仅用于显式测试清理。入队、运行和清理须在创建线程进行，错误线程或空队列执行会返回错误并记录诊断。
- 调度器强引用待执行上下文与会话，上下文仅弱引用调度器，避免 Context→Session→Context 引用环。运行前先移出请求，局部强引用保证回调期间对象存活，并允许重入入队；清理或销毁调度器释放待处理请求。回调不得强捕获拥有它的调度器，避免另一个引用环；已过期的调度器不能静默退回立即执行。
- Windows SDK 的 `TF_ES_ASYNCDONTCARE` 为 **0**，不是独立位。Mock 校验同步/异步冲突、访问模式和未知位；成功异步请求返回外层 `S_OK`、内层 `TF_S_ASYNC`。同步拒绝和只读读写拒绝分别返回内层 `TF_E_SYNCHRONOUS`、`TS_E_READONLY`；只读上下文仍允许读请求。这是最小请求标志与结果模型，不是完整 TSF 锁、编辑权限或 cookie 校验器。
- [`com.cpp`](../tests/ime/tsf/com.cpp) 覆盖默认兼容性、FIFO、请求元数据、同步优先、失败注入、回调失败后继续执行、重入及引用释放；[`service_lifecycle.cpp`](../tests/ime/tsf/service_lifecycle.cpp) 调度真实生产写回调和布局读回调，覆盖停用/重新激活后执行、输入上下文变化及旧队列保留。
- Mock 不理解激活周期，不自动丢弃过期任务，也不将旧回调可能修改文档的行为定义为正确。任务 43 已在生产回调中实现并断言过期任务不得写文档、创建组合或恢复 UI；任务 44 处理主动结束宿主组合。任务 42 本身不修改生产代码。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，ETW 未跳过；定向运行使用 `ctest --test-dir build\x64 -C Release -R '^(QiQiTsfUiTest|QiQiEtwIntegrationTest)$' --output-on-failure --timeout 120`，x86 替换为 `build\x86`。x64 测试目标同时执行 CppCoreCheck，新增 Mock 与测试没有新增诊断。

### 5.6 过期编辑会话屏障（任务 43）

- [`QiQiTextService`](../src/ime/tsf/tip_service.h) 为每个服务实例维护激活状态及激活/上下文周期；有效激活尝试和停用改变激活周期，停用在任何清理之前标记为无效。部分激活失败仍通过 `Deactivate` 回滚，不使旧任务重新有效。
- [`ApplyEngineResult` 与 `OnLayoutChange`](../src/ime/tsf/tip_composition.cpp) 在提交请求前保存周期，回调开始处通过同一检查核对激活状态、两个周期和目标上下文。强引用只保证对象存活，不代表目标仍有效；同一 COM 指针重新激活或 A→B→A 后，旧快照也必须失效。预期丢弃返回 `S_OK`，调试日志仅记录周期与激活状态，不输出输入文本，也不伪造失败 HRESULT。
- `ResetContextAndUI` 在 COM 清理前使上下文周期失效并清空活动目标，统一注销布局订阅、释放本地组合和结束候选 UI；现有的焦点丢失、宿主终止和停用重置复用它。切换输入上下文也使用该清理，避免新目标沿用旧组合范围；处理已生成的引擎结果时不额外重置引擎。提交请求前再次检查目标切换过程中是否发生重入失效。
- 布局通知只接受当前活动上下文，执行过期读回调时不查询旧视图。调度器仍保留旧请求并实际调用其生产回调，屏障不依赖 Mock 过滤；同周期的多个写入快照保持 FIFO，不按每个按键递增周期。
- [`生命周期测试`](../tests/ime/tsf/service_lifecycle.cpp) 通过实际文本、写入/组合创建/视图查询与 UI 调用次数验证：停用及失败激活后执行旧任务、同一上下文重新激活、焦点重置、A→B→A、宿主终止、过期提交/取消，以及旧布局读任务遇到新组合。另验证正常 FIFO 更新、提交、下一次组合和延迟布局读，不只检查回调返回成功。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，ETW 未跳过；x64 生产与测试目标执行 CppCoreCheck，修复新增的 const 正确性诊断后无新增告警。仍保留既有告警，不扩大豁免范围。
- 范围：本任务覆盖现有重置入口及输入上下文周期，不建立文档管理器列表。后续任务 45 补充文档/栈通知，并修正“生成引擎结果后才切换目标”的跨输入框携带问题，详见下文 5.8；任务 44 已补充停用时的同步清理及受控异步补救。本地释放组合不等于已正确结束宿主组合，受控测试也不替代浏览器/UWP 的真实宿主验证。

### 5.7 停用组合清理与受控异步补救（任务 44）

- 完整的状态定义、强弱所有权图、同步/异步与重新激活时序、重入规则、日志协议和排障步骤见[组合生命周期设计](composition.md)；采集入口见[日志指南](log.md#62-组合生命周期与停用清理任务-434445)。
- 按更新确认的策略保留预编辑文字，并允许受控异步补救。清理只清除旧范围的 `GUID_PROP_ATTRIBUTE` 并结束旧组合，不删除/替换文本或恢复 UI。
- [`Deactivate`](../src/ime/tsf/tip_service.cpp) 仍同步返回：先拒绝旧业务任务并设置停用保护，再通过 [`ScheduleCompositionCleanup`](../src/ime/tsf/tip_composition.cpp) 请求 `TF_ES_SYNC | TF_ES_READWRITE`。仅当请求成功但会话结果为 `TF_E_SYNCHRONOUS`、回调未执行、宿主未终止组合且上下文未被移除时，再申请 `TF_ES_ASYNC | TF_ES_READWRITE`；不等待异步回调便清理本地引擎、订阅、UI 和线程管理器引用。任务 45 将该路径复用到仍连接的目标切换和失焦。
- 每次清理独立保存旧上下文、组合及范围。编辑回调强引用该状态，不捕获线程管理器或服务；服务只保存弱引用，避免服务与清理任务强引用成环。组合自身可能持有其 sink，COM 引用计数仍保证服务和模块存活到请求被执行或释放。RAII 复位停用保护及清理完成标志，任务被宿主放弃时捕获资源也会释放。
- 同一上下文有待处理清理时，新业务写请求强制使用 `TF_ES_ASYNC`，避免默认同步优先越过旧清理；TSF 的异步 FIFO 先结束旧组合，再创建新组合。其他上下文不受此调度限制。原业务回调仍检查任务 43 的激活/上下文周期，只有独立旧组合清理不依赖当前激活周期。
- [`OnCompositionTerminated`](../src/ime/tsf/tip_composition.cpp) 区分当前、待清理及旧组合。宿主提前终止待清理组合时，使用通知的写 cookie 清理旧范围并标记终止，之后旧回调直接退出，不能清除新组合属性；清理过程中发生终止也不重复结束。非当前组合通知不重置新组合/UI。停用期间重入停用不重复清理，重入激活记录并返回 `E_UNEXPECTED`，外层结束后正常激活可继续。
- 只读、断开、请求拒绝及宿主范围/属性/结束 API 失败都记录实际 HRESULT，不无限重试或强行越过权限。读会话内升级写权限也不能假定异步就会成功。属性或范围清理失败不阻止在有效权限下尝试结束组合；停用返回 `S_OK` 或排队返回 `TF_S_ASYNC` 都不等于宿主清理已成功。这些不可恢复情形可能保留宿主组合/属性，应从诊断识别，不宣称所有宿主都能结束。
- [`Mock 回归测试`](../tests/ime/tsf/service_lifecycle.cpp) 覆盖同步和异步 flags、保留文字、失败注入、重复停用、部分激活失败回滚、清理期间重入、旧组合通知、异步请求失败/断开、同一上下文重新激活及宿主提前终止；断言旧回调不调用 UI、不误清新属性，并检查释放外部服务引用后清理仍可完成、COM 对象计数恢复。
- 同一测试建立真实 Windows TSF 管理器及私有文档，使用 `TF_TMAE_NOACTIVATETIP` 禁止激活系统 TIP，不切换用户焦点。通过真实布局通知触发同步拒绝，验证通知内组合仍存在、通知返回后异步清理完成；枚举宿主组合和属性范围，断言保留文本、`GUID_PROP_COMPOSING` 与 `GUID_PROP_ATTRIBUTE` 消失。另验证立即重新激活后旧清理先于新写入，新的组合范围仅含新文字且属性保留；以有界消息泵等待真实异步处理，不用 `Sleep`。
- 真实 TSF 文档验证不同于 ETW 日志采集，不替代浏览器/UWP 实际宿主验证；任务 41 的窗口销毁后续已由 5.10 完成，任务 45 的文档/上下文栈处理见下文 5.8。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`（含上述真实 TSF 文档测试）、`QiQiEtwIntegrationTest` 均通过，未跳过；定向 CppCoreCheck 修复新增非空类型约束并复用现有 VARIANT 初始化帮助函数后无新增告警。清理请求完成和被调度器放弃两条路径均检查 COM 对象计数恢复。

### 5.8 文档及上下文生命周期（任务 45）

- 继续以活动 `ITfContext` 为核心，仅缓存其所属文档用于移除后的关联判断，不建立文档管理器列表。焦点与同文档入栈统一通过 `ReconcileDocumentTarget` 查询栈顶/主上下文；主上下文被正常模态栈顶覆盖时保留组合，无关文档通知不得重置当前输入。
- 出栈按 context 身份处理，解除初始化按缓存文档的 COM 身份处理；当前目标移除先失效旧写入/布局任务，再清订阅、引擎和 UI，不请求新的编辑会话。已排队的独立清理也标记为 context 已移除，迟到回调不得再清属性或结束组合；本地释放不等于宿主已结束。
- 一并修正任务 43 的两个相关缺口：新目标的按键在引擎处理前准备上下文，避免旧组合通过 Backspace 等按键带到新输入框；仍连接的目标切换/失焦复用任务 44 的宿主清理，而不只是释放本地引用。A→B→A 时同一 context 的新写仍强制排在旧异步清理之后。
- 重置/绑定以 RAII 防止重入输入，文档查询和布局订阅之后复核周期，过时焦点通知不再继续显示 UI；重入激活明确失败。新增文档/栈日志记录关联判断、模态覆盖和是否尝试宿主清理，不记录输入内容。完整英文说明见[组合生命周期设计](composition.md#8-document-and-context-notifications)。
- Mock 验证延迟写入/布局、非空文档切换、模态入栈/出栈、无关通知、移除前断开、范围查询失败与重入、A→B→A 异步顺序、已排队清理的 context 被移除及 COM 对象计数恢复。原测试中的伪文档地址改为实际 Mock 对象。自有调试子进程校验实际输出的模态保留和非当前出栈字段。
- 真实 Windows TSF 私有文档通过实际事件订阅转发 Push/Pop/Uninit 通知，验证主组合跨模态 Push/Pop 保留、非空文档切换时旧属性/组合清除且文字保留、新文档组合独立、全部出栈时结束 UI；不调用原生 `SetFocus` 或激活系统 TIP，不假定通知顺序。
- 另覆盖清理范围查询期间重入出栈/解除初始化：移除标记先于新编辑请求，既不清属性也不调用结束 API；焦点处理使用独立 revision，普通 context 移除不吞掉失焦隐藏动作，较新的重入焦点通知则取代旧动作。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，未跳过；定向 CppCoreCheck 修复新增构造分配/noexcept 诊断后无新增告警。首次运行遇到旧测试伪文档地址访问异常，替换为实际 Mock 文档后双架构全部通过。窗口销毁后续由任务 41 完成；真实浏览器/UWP 验收仍由任务 47 跟进。

#### 0.9.19 提交后的覆盖复核

- 补测父文档查询期间目标出栈与无关目标出栈、布局订阅发布期间重入移除及后续正常输入、订阅被拒绝、不同接口指针的 COM 文档身份与身份查询失败、`GetBase` 失败、清属性过程中出栈后不得调用 `EndComposition`。事件连接及配置恢复采用 RAII，最后检查 COM 对象计数恢复。
- 新回归先复现了一个实际缺口：`GetDocumentMgr` 重入移除目标时，目标尚未写入 `active_context_`，原来的布尔绑定保护无法识别该目标，返回后仍会绑定并写入。绑定保护改为持有待绑定 context，匹配出栈立即失效周期；外层不消费按键、不订阅、不申请编辑权限，无关移除不受影响。
- 使用独立服务和隔离的状态栏配置检查真实窗口可见性：普通出栈不应吞掉失焦隐藏动作；较新的重入得焦优先于旧失焦；文档查询期间失焦不应被旧外层通知重新显示。
- 前次 x86 验证中 Shift 轻触断言曾偶发失败，隔离重跑通过；现将线程键盘状态复位移到该序列之前，减少此前消息泵带来的环境状态干扰。未改变生产的 350 ms 限制、未增加盲目重试，也未确认该次失败的唯一根因；测试仍依赖实际调度。
- 此次按行为/分支复核，不报告未测量的覆盖率。窗口销毁和完整回归矩阵后续分别由任务 41、46 补齐；自动化结果仍不替代任务 47 的真实宿主验证。
- 最终验证：x64/x86 Release 的 `QiQiTsfUiTest` 各连续通过 3 次，`QiQiEtwIntegrationTest` 双架构均通过且未跳过；生产/测试目标执行 CppCoreCheck，修复新增 const/noexcept 诊断后仅保留既有告警。

#### 生命周期与 FSM 边界

- 目标切换已在实际 `OnKeyDown` 前重置引擎，`OnTestKeyDown` 也必须按新目标的重置后状态预测。不同 context 使用只读 `ShouldEatKeyAfterReset`，当前目标继续使用 `ShouldEatKey`；预判不执行绑定、宿主清理或编辑请求。空目标、未激活和重入重置/绑定均不消费按键。
- 保持现有按键状态转移表；实际 `Reset` 与预测共用 `StateAfterCompositionReset`，中文组合及临时英文回到 `Idle`，英文模式保持 `English`，全半角与标点设置保留。异步宿主清理不反向改变新目标的 FSM。
- 引擎回归比较 7 个状态、2 种宽度、16 种修饰键组合及全部 256 个虚拟键，并检查预测不改变编码、候选、分页和选中项。TSF 回归先复现新目标 Backspace 仍按旧组合预测的问题，再覆盖临时英文、Shift/CapsLock、英文/全角保持及生命周期重入，并以实际文本、组合结束及编辑请求计数验证无副作用。
- 验证：x64/x86 Release 的 `QiQiEngineTest`、`QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过且未跳过；定向 CppCoreCheck 修正新增的 string-view 引用诊断后仅保留既有告警。

#### 停用时提前销毁 UI（任务 41）

- 先复核停用、组合清理、TSF UI 注册以及窗口 RAII 的顺序，再将候选窗、状态栏和提示窗的销毁提前到 `Deactivate`；保留 UI 对象及回调供再次激活使用，析构仅作幂等兜底。独立异步组合清理仍可晚于原生窗口销毁完成，不阻塞消息泵。
- 展示开关与 UI 对象同寿命，停用期间宿主持有的候选接口调用 `Show(TRUE)` 返回 `S_FALSE`，不得重建窗口。迟到的 `BeginUIElement` 成功结果仅通过旧 manager 注销其临时 ID；结束注册时先使共享 ID 失效，防止重入重复注销或覆盖新 UI。
- 新回归先复现 `GetTextExt` 内停用并重新激活后，旧位置结果仍移动新候选窗的问题；位置查询改为强持有旧组合，在宿主调用之间及移动窗口前复核周期与组合身份，丢弃失效结果。
- 共用所属线程校验和可报告失败的窗口销毁 helper；拒绝跨线程销毁并保留 HWND，允许所属线程重试。`WM_NCDESTROY` 清除匹配句柄，覆盖外部原生销毁及句柄复用。状态栏销毁同时结束捕获/拖动、提示与悬停状态。原生窗口销毁失败时停用返回 `E_FAIL`，不跳过其余 TSF 本地清理；成功返回也不等于宿主组合或 UI 注销已成功。
- 回归使用真实 HWND 与销毁消息验证顺序和线程，不以可见性代替销毁：覆盖外部引用、重复停用、激活失败回滚、再次激活后的菜单/选词回调、旧异步写入/布局/清理、UILess、宿主 Begin/End 重入、最终服务释放后保留的 UI，以及独立 UI 的跨线程拒绝与销毁重建。
- Begin/End 重入时切换到另一个 manager，并故意复用相同数值的 UI ID，验证旧注销仅作用于旧 manager、新窗口不被隐藏。独立用例显式保存、清理并恢复线程键盘状态，避免先前消息泵带入的环境状态影响普通按键断言；未改变生产按键规则。
- 最终验证：x64/x86 Release 的 `QiQiTsfUiTest` 与 `QiQiEtwIntegrationTest` 各连续通过 2 次、均未跳过；双架构构建与定向 CppCoreCheck 通过，修正新增向下转换诊断后仅保留既有告警，`git diff --check` 通过。
- 生命周期契约、诊断字段与验证边界见英文[设计文档](composition.md)。完整回归矩阵见下节；任务 47 已有[阶段性人工反馈](hosts.md)，但完整验收仍未完成。

### 5.9 生命周期回归矩阵（任务 46）

- 先盘点已有延迟写入/提交/取消/布局、FIFO、重新激活、焦点与 A→B→A、模态及无关通知、移除后屏障、清理失败矩阵、引用/订阅释放，以及任务 41 的真实 HWND 和 UI 注册重入；不重复计为本轮新增测试。完整矩阵见英文[设计文档](composition.md#91-regression-inventory-task-46)。
- 新增 `TestReentrantCompositionEdits`：86 个执行中编辑/宿主终止边界用例、8 个迟到 `StartComposition` 用例、4 个清理期间属性查询用例，并显式断言各矩阵计数；另外覆盖同周期替换组合的 2 例及键盘状态重入的 2 例。按实际文本、写入/属性/选择/视图/UI 调用次数、组合身份及对象计数验证，不只检查返回成功，不报告未测量的覆盖率。
- 测试先复现 `GetRange` 内停用后旧编辑仍写文档，以及清理已进入但 `GetProperty` 内宿主终止发生在实际 `Clear` 前导致漏清属性。编辑强持有原组合，并在宿主调用之间检查 epoch、context 和组合身份；清理独立记录 `attribute_clear_started`，需要时用宿主通知的 write cookie 清一次，移除后不再调用宿主编辑 API。
- 迟到的组合创建结果先私有持有，临时弱跟踪使用 `id == 0`，不消耗正式清理诊断 ID。期间的移除/终止不会被返回结果覆盖；仍连接的迟到旧组合转入独立清理，捕获原 client ID。同 context 新写若早于迟到清理入队，执行入口再次检查，并将已捕获结果显式异步排到旧清理后，不重算按键、不推进 FSM。
- 同一 epoch 也可能重入提交并创建替代组合；新增测试先复现旧更新/布局返回后继续刷新新 UI。更新返回其原组合快照，布局查询及 UI 发布之间继续复核身份；提交/取消在旧 `EndComposition` 前先使共享组合槽与旧 caret 状态失效，保留重入创建的新组合。
- 确定性键盘测试在旧目标清理及 compartment 查询中注入新的 Ctrl 状态，先复现当前按键被错误透传。`OnTestKeyDown`/`OnKeyDown` 改为在宿主调用前捕获当前事件的修饰键和 CapsLock，预判还捕获 Shift 轻触所需鼠标状态；下一事件正常观察新状态，FSM 表及消费规则不变。普通按键矩阵每次显式建立中性键盘前置状态，整个用例保存/恢复原线程状态，不影响专门的修饰键测试。
- 不能撤销已开始的宿主写入；回归在可观察写入之后触发重入，要求失效后不再发后续操作。实际浏览器/VS Code 的焦点、下划线和窗口行为仍由任务 47 验证，不以 Mock 或私有 TSF 文档替代。
- 最终验证：x64/x86 Release 的 `QiQiTsfUiTest` 与 `QiQiEtwIntegrationTest` 各连续通过 2 次、均未跳过；`QiQiEngineTest` 双架构通过。双架构构建与定向 CppCoreCheck 通过，修正新增的取消路径 const 诊断后，修改的生产代码无分析告警，仅保留既有其他路径/测试告警；`git diff --check` 通过。RAII、组合快照复用、Core Guidelines/GSL 及缺失测试已复核，未放宽断言或生产 Shift 轻触时限。

### 5.10 保留键与键盘监听所有权生命周期（任务 48）

- 复核微软 `SampleIME` 与 `Weasel`：`SampleIME` 在 `InitPreservedKey`/`UninitPreservedKey` 中忽略 `PreserveKey`/`UnpreserveKey` 的返回值并按表全量注销，`Weasel` 的 `_InitPreservedKey` 位于 `#if 0` 且注销为空；两者均不适合作为所有权与失败处理模型。
- [`QiQiTextService`](../src/ime/tsf/tip_service.h) 新增 `key_event_sink_registered_` 与 `registered_preserved_keys_`：[`InitKeyEventSink`](../src/ime/tsf/tip_service.cpp) 在 `AdviseKeyEventSink` 成功后标记键盘监听已注册，并仅将返回成功（`SUCCEEDED`）的 `(GUID, TF_PRESERVEDKEY)` 记入本激活周期持有列表。
- 区分 `TF_E_ALREADY_EXISTS`（警告记录已注册并跳过本次所有权）与其他失败（记录操作与实际 `HRESULT`），不先注销或覆盖已有注册。[`UninitKeyEventSink`](../src/ime/tsf/tip_service.cpp) 先移出并清空本周期持有状态，仅对持有项调用 `UnpreserveKey`（失败记录 `vkey`/`mod` 与 `HRESULT`）、仅在监听已注册时调用 `UnadviseKeyEventSink`，保证部分激活回滚、重复停用、注销失败或 `ITfKeystrokeMgr` 查询失败后的状态一致与可再次激活。
- [`service.cpp`](../tests/ime/tsf/service.cpp) 覆盖全部成功、查询/监听注册失败回滚、`TF_E_ALREADY_EXISTS` 与 `E_OUTOFMEMORY` 部分成功、全部保留键失败、重复停用、注销失败、`ITfKeystrokeMgr` 注销查询失败后重新激活，以及自有调试子进程对实际 `OutputDebugString` 警告消息的校验；现有快捷键行为保持不变。

### 5.11 进程词库缓存生命周期（任务 52）

- 评估结论：`g_process_dict_cache` 以规范化路径缓存 `(src_size, src_mtime, std::shared_ptr<const WubiMappedDict>)`。虽然清空该映射不会使其他实例已持有的 `shared_ptr` 悬空，但在每个实例 `Deactivate` 时调用 `WubiMappedDict::ClearProcessCache()` 会使同进程后续激活、实例重载或新实例无法命中内存缓存，被迫重新打开并映射 `.qdict` 文件。
- 调整策略：[`QiQiTextService::Deactivate`](../src/ime/tsf/tip_service.cpp) 仅调用 `engine_.ClearExternalDictionaries()` 释放当前服务实例持有的词库引用，不再清空进程级缓存；进程缓存随模块生命周期在 [`DllMain`](../src/ime/tsf/dllmain.cpp) 动态卸载（`DLL_PROCESS_DETACH` 且 `lpReserved == nullptr`）时统一清理，运行期词库文件修改则由 `LoadFromTextOrCache` 的文件大小与最后修改时间比对自动更新。
- 关联修正：`ActivateEx` 原先仅在 `config_.primary_dict != PrimaryDict::BuiltinOnly` 时恢复被 `Deactivate` 清空的外部词库，导致 `BuiltinOnly` 配置下停用后重新激活丢失已配置的外部用户词库；现统一在 `engine_.MappedDictionaryCount() == 0` 时调用 `LoadExternalDictionaryNextToDll()`（直接命中进程级缓存）。
- 验证：[`dict.cpp`](../tests/ime/engine/dict.cpp) 与 [`service.cpp`](../tests/ime/tsf/service.cpp) 验证多实例指针级复用、停用一个实例后进程缓存保留且另一实例及再次激活实例（含 `BuiltinOnly` 用户词库）候选查询正常、显式 `ClearProcessCache` 不影响在用 `shared_ptr`，以及所有实例与缓存释放后 `weak_ptr` 过期（底层映射资源释放）。

### 5.12 类别管理器创建与模块引用（任务 53）

- 保留“导出函数优先、COM 回退”的既有顺序。[`TF_CreateCategoryMgr`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-tf_createcategorymgr) 支持无需初始化 COM 的创建路径；不能仅因普通 STA 宿主中 `CoCreateInstance` 成功就删掉它。生产代码不调用 `CoInitializeEx` 或 `CoUninitialize`，不改变宿主线程的 COM 状态。
- [`category_manager.h`](../src/ime/tsf/category_manager.h) 将创建与样式 Atom 注册保持在同一个作用域：使用 [`GetModuleHandleExW`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) 的 `0` 标志取得已加载 `msctf.dll` 的持有引用，由现有 `ScopedModule` 释放；不使用 `PIN`、`UNCHANGED_REFCOUNT` 或主动加载 DLL 的新生产回退。
- 模块守卫先于 `ComPtr<ITfCategoryMgr>` 声明，所以成功、注册失败及创建失败路径都先释放接口，再释放持有的模块引用。此改动消除借用句柄路径缺乏显式引用保障的问题，不宣称此前已复现 DLL 卸载故障。
- 无模块或无导出时继续尝试 COM；模块/导出查询失败立即捕获 `GetLastError`，记录 Debug 回退诊断。导出创建失败使用实际 HRESULT 记录 Debug，COM 创建及 `RegisterGUID` 失败使用现有 `CheckHResult` 记录警告。成功却无接口按 `E_UNEXPECTED` 诊断；导出返回失败时不沿用其输出接口。
- 仅在 `RegisterGUID` 成功时发布 Atom，失败返回 `TF_INVALID_GUIDATOM`，保持输入法激活不因此失败的原有行为。调用方仍为 [`RegisterCompositionDisplayAttributeAtom`](../src/ime/tsf/tip_service.cpp)，没有将类别管理器或函数指针缓存到模块引用之外。
- [`category_manager.cpp`](../tests/ime/tsf/category_manager.cpp) 通过局部 API 参数测试导出成功、模块/导出不可用、导出失败后回退、两种创建均失败、成功但无接口、异常失败输出和两条路径的 GUID 注册失败；在每条路径断言接口释放先于模块引用释放，不新增全局测试钩子。
- 原生测试在未初始化 COM 的新线程中预加载系统 `msctf.dll`，验证真实导出创建与 GUID/Atom 映射，并检查调用前后仍为未初始化 COM；另在测试 STA 中强制导出失败，验证真实 COM 回退与 GUID 映射。x64/x86 Release 的 `QiQiTsfUiTest` 与 `QiQiEtwIntegrationTest` 均通过，未跳过；VS Code、浏览器及容器类宿主的交互矩阵仍由任务 47 跟进，不将本测试等同于全部宿主兼容性验证。
- 定向 CppCoreCheck 已修正本次新增的异常声明与测试资源类型诊断；动态导出所需的函数指针转换沿用仓库排除 C26490 的规则，其余仅保留既有诊断。复核 RAII 释放顺序、共享日志/句柄封装复用、Core Guidelines/GSL 及缺失测试，未扩大告警豁免范围；最终双架构回归与 `git diff --check` 通过。

### 5.13 实时日志监控工具（任务 56）

- [`model`](../src/tools/log_monitor/model.h) 实现条数/字节双上限、单调事件 ID、过滤与 UTF-8 导出；字节预算计入字符串容量。队列在锁内转交拥有数据的快照，GUI 不保留 ETW 回调指针。导出使用严格 UTF-16 转换，非法文本或写入失败不能变成成功的空字段；写入器直接返回 Win32 错误，不使用会向 IME Provider 写日志且只返回 bool 的共享原子写入接口。目标可能部分写入，错误提示明确保留内存记录。
- [`collector`](../src/tools/log_monitor/collector.h) 创建带 PID/计时/序号的自有实时会话，仅订阅 IME Provider；在后台用 TDH 有界解析自描述元数据、事件名及计数字符串，保留计数字符串内的 NUL。使用系统时间时钟，验证回调时间确为 UTC FILETIME，而非未经转换的原始时钟值。
- `EnableTraceEx2` 使用有界同步等待，使已注册宿主在开始采集返回后发出的首条消息不会因异步启用竞态而漏收。启动后的每条失败/异常路径由作用域守卫停止自有会话、关闭消费者并等待线程退出；停止失败不丢失会话句柄，也不创建替代会话掩盖问题。生命周期方法仅由 GUI 线程调用，不与消费者或其他控制线程并发执行。
- 自身诊断为独立的进程级 `QiQiIME.LogMonitor` 注册；消费者停止并 join 后，队列和诊断对象才能释放。关键错误保留在 GUI 状态，另有有界诊断视图；常规采集记录不进入自身日志。只操作自有会话，不修改宿主权限或系统日志设置。
- [`window`](../src/tools/log_monitor/window.h) 使用 Win32 `LVS_OWNERDATA` 多选虚拟列表，列表回调按稳定 ID 查找已拥有数据；重建时抑制选择通知重入，保留仍可见的多选集合并清除已淘汰选择。支持工具栏 **复制 / Copy**、右键菜单（复制/全选）及 `Ctrl+C` / `Ctrl+Insert` / `Ctrl+A`，共用 [`CopyTextToClipboard`](../src/common/win32_ui_util.h) 与 `ScopedGlobalAlloc`/`ScopedClipboard` 将选中完整日志项写入剪贴板。原生窗口销毁时同步清空 RAII HWND，避免句柄复用后的重复销毁。详情提供键盘可用滑块，DPI 变更同步字体/布局/列宽；不重复放置与标题栏和关于对话框重叠的左上品牌图标及副标题控件。
- [`d2d_controls`](../src/common/d2d_controls.h) 提取共享颜色和高对比度检测，由设置程序和监控器共同使用；设置程序的按钮保留原有回退。监控器与词库管理器的原生菜单及纯图标工具条共用 [`command_ui`](../src/common/command_ui.h)，不依赖 D2D 绘制按钮，使用作用域守卫恢复 DC/字体。没有抽取整个设置窗口，也未新增 UI 框架。
- GUI 可执行文件沿用根 `VERSION`、生成版本头、图标与 `version.rc.in`，资源类型为 `VFT_APP`；不维护工具私有版本常量。构建目标 `QiQiIMELogMonitor`，测试目标 `qiqi_log_monitor_test`，两者已纳入现有 lint 目标集合。无需安装新依赖或生成 MSI。
- 监控器通过资源模板参数单独使用 [`qiqiime-monitor.ico`](../data/qiqiime-monitor.ico)（深色实时波形监控面板图标），设置程序使用 [`qiqiime-settings.ico`](../data/qiqiime-settings.ico)（蓝色“奇”字品牌高清图标），IME DLL 保留 [`qiqiime.ico`](../data/qiqiime.ico)（供深色语言栏/输入法指示器使用的白色“奇”字图标）。[`generate_monitor_icon.ps1`](../scripts/generate_monitor_icon.ps1) 分别生成 16、20、24、32、40、48、64、96、128、256 像素 PNG 帧；同环境重新生成的文件字节一致。窗口拥有大小图标，DPI 切换时先替换窗口引用，再释放旧资源。
- 系统主题、颜色和设置通知统一刷新共享画刷及原生控件主题；高对比度下共享按钮/组合框回退到系统颜色和 GDI 绘制，并恢复 DC/字体。原生深色表头在部分系统上保留黑色文字，监控器通过列表子类的表头 custom draw 修正，保留原生列宽调整及命中测试；高对比度和浅色仍使用原生表头。
- 语言选择复用 `UiLanguage`/`ResolveUiLanguage`，只更新当前窗口文案，不重建采集器或重置筛选、选择、暂停状态，不持久化或修改 IME 配置。技术事件与导出协议保持不变。
- 下拉框内容重建由 [`ReplaceComboBoxItems`](../src/common/win32_ui_util.h) 统一处理，设置程序的初始化/实时翻译与监控器的级别/分类共用：按索引保留选择，索引失效时选择第一项，拒绝无效控件、排序/非字符串组合框及空字符串指针，失败返回 `HRESULT` 交由各自日志通道报告。监控器的语言菜单直接使用现有 `UiStrings`，不维护第二份语言名称翻译。严格导出编码及 ETW 解码不改用宽松的字符串转换，独立诊断不改用 IME 日志写入器。
- 多显示器复用 [`monitor_dpi.h`](../src/common/monitor_dpi.h) 的工作区/DPI 查询及矩形适配；响应 `WM_DPICHANGED`、`WM_DISPLAYCHANGE`、`SPI_SETWORKAREA`，支持负坐标及较小工作区。相同 DPI 不重复缩放列宽，普通窗口保持在目标工作区；最大化/最小化迁移交给 Windows。

验证命令（x86 将 `x64` 替换为 `x86`，GUI 回归按架构顺序运行）：

```powershell
cmake --build --preset x64-release --target qiqi_log_monitor_test qiqi_common_test qiqi_settings_test qiqi_tsf_ui_test --parallel 2
ctest --test-dir build\x64 -C Release -R '^(QiQiLogMonitorTest|QiQiLogMonitorIntegrationTest|QiQiCommonTest|QiQiSettingsTest|QiQiTsfUiTest|QiQiEtwIntegrationTest)$' --output-on-failure --timeout 120
```

- [`main.cpp`](../tests/tools/log_monitor/main.cpp) 覆盖条数/字节精确边界、预留字符串容量、超大记录、ID/淘汰/清空、PID 溢出、过滤、转义/严格编码、真实文件写入及共享冲突、版本资源、原生虚拟列表、选择、暂停/清空、详情高度、DPI 和销毁。
- 桌面集成测试覆盖中英/自动语言及状态保留、四类主题/颜色通知、高对比度优先级、实际表头绘制像素和亮色文字、256 像素图标资源、DPI 图标尺寸、重复 DPI 通知、负坐标、模拟拔屏及缩小工作区；共享控件测试检查高对比度组合框选中色和 DC 恢复。
- 公共下拉框测试覆盖首次选择、翻译后保留索引、列表缩短/清空、无效控件/样式/字符串指针，以及注入的内存不足、添加项和恢复选择失败；监控器另外验证所选语言文案直接来自共享 `UiStrings`。
- 实时集成测试不附加调试器，覆盖宿主先注册后开始采集、后注册子进程、Provider 注销/重新注册、中文/换行/NUL 精确负载、PID/TID/事件名/级别/UTC 时间、诊断隔离、1,024 条突发消息的本地有界淘汰、停止后会话不存在及再次启动。权限不足时仅此集成测试返回 CTest skip code 77，不能让普通模型/GUI 测试随之跳过。
- 双架构上述 6 项测试通过且未跳过；通过测试程序的 `--integration <另一架构测试程序完整路径>` 参数验证两个方向跨架构采集（这是测试入口，不是 GUI 工具的 CLI）。真实 GUI 启动、显示子进程事件、零解码/ETW 丢失状态及正常退出另行检查。
- 定向 MSVC CppCoreCheck 修正新增告警，Windows ABI 转换沿用仓库既有 C26490 排除；分配型默认构造器的异常传播、满足前置条件的消费者 join 只作逐处说明，不将其伪造为不会失败的分配。共享文件其余旧告警未扩大修复范围。复核 RAII、复用、Core Guidelines/GSL 与缺失测试。
- 真实 GUI 已在双屏间移动，验证 150%→100% DPI 切换、工作区边界及已有事件保留；浅色、深色和模拟高对比度窗口均接收子进程事件并正常退出。测试宿主提供 `--preview-dark` / `--preview-contrast` 供视觉检查，不是生产 GUI 的 CLI；后者仅切换应用回退分支、使用当前系统色，不修改 Windows 高对比度设置。完整真实高对比度主题、热插拔/最大化/最小化矩阵及浏览器/UWP 交互仍需另行人工验证。

### 5.14 公共跨进程变更同步（任务 39）

- [`Channel`](../src/common/change_sync.h) 不依赖 TSF 广播、不创建常驻服务或后台线程。每个数据源使用独立的绝对目录，绑定数据类型与稳定源 GUID；适配层须向桌面和容器实例提供同一路径，不能各自从进程环境推导不同目录。词库/词频的业务接入属于任务 40。
- `Create` 仅允许非 AppContainer 发布进程调用，为专用通道目录配置受保护 ACL：所有者/系统可写，AppContainer 可读及遍历。不要传入安装根目录或含无关私密文件的目录；增量正文可能含用户词条和排序记录，不能当作公开或加密数据。测试覆盖已有目录重新配置权限及容器不能写入。
- `journal.bin` 使用固定宽度小端字段，保存格式版本、数据类型、源/基线 GUID、递增序号、操作和不透明增量正文，并作内容校验。正文由业务层解释；最多保留 256 条、总文件不超过 2 MiB，单条正文上限 64 KiB，超限参数明确失败。记录/字节保留窗口过期后要求全量快照，不逐次读取完整业务数据。
- `Watch` 的目录通知可能由临时文件、源文件或最终日志替换触发，不能等同于提交完成；唤醒后先 `Rearm`，再检查版本。漏通知或晚启动仍能通过 `Read`/`Synchronize` 检查持久化版本；实际焦点补偿入口在任务 40 接入。
- `Publish` 使用非阻塞排他文件锁和预期版本检查；只有持锁后才执行源数据读改写回调。先保存未完成标记，再写源并提交增量；回调失败/异常使基线失效，写进程退出或最终提交失败则保留可识别的未完成状态。下一读者持共享锁要求快照恢复，不把未提交增量当作成功。锁冲突/旧版本保留实际 HRESULT，调用者应在合适边界重新读取并重试，不能丢弃操作或无界阻塞输入线程。
- 源文件应自行原子替换；此协议不能撤销业务回调已经产生的外部副作用，也不是跨文件或断电持久性事务。日志校验/格式失败显式报错，不信任损坏版本；不得静默继续应用旧增量。`WriteFileAtomicallyResult` 复用已有原子写实现并保留 HRESULT，原有布尔接口继续可用。
- `Synchronize` 在 `Composing` 时不更新本地状态；`NewInput` 时在共享锁内调用快照或增量适配器。仅回调返回 `S_OK` 才确认版本，失败或 `S_FALSE` 不推进。适配器须在成功时一次性发布本地状态，不从持读锁的回调重入发布接口。
- [`回归测试`](../tests/common/change_sync.cpp) 覆盖新增/修改/删除/调序、组合期暂缓、漏通知、晚启动、基线/保留窗口变化、并发读改写、回调失败/异常、损坏校验、提交失败和持锁写进程退出。真实 AppContainer 子进程验证通知、只读增量与写入拒绝；x64/x86 均通过，并验证两个方向跨架构写入及容器读取。测试使用独立目录、唯一临时容器配置与拥有的子进程，不修改真实词库。
- 读端干扰回归覆盖真实 AppContainer 拒绝共享写、持有共享字节锁，以及拒绝日志文件共享删除三种情况。每种情况重复发布两次，验证失败发生在源写回调之前、源文件/日志/版本不变、无遗留临时文件；释放句柄后发布恢复，重复同步只应用一次增量。这些是当前协议限制的复现与恢复测试，不代表已经实现不可信读端隔离；当前暂缓协议修复并保留回归，后续修复须相应更新预期。
- 多写端回归同时核对最终计数、基线、全部 60 条增量的连续序号、操作和载荷。新增干扰用例在 x64、x86 及两个跨架构方向共 12 组全部通过，正常 AppContainer 同步及多写端回归一并通过；定向 CppCoreCheck 无非 C26490 的自有代码诊断。测试复用容器配置管理，子进程由专属 Job 管理，并在关闭 Job 后等待退出，避免失败清理时残留持锁进程。
- 复核 RAII、共享帮助函数复用和 GSL/Core Guidelines；定向分析修正新增代码告警，保留仓库既有 Windows ABI 转换排除及第三方 GSL 头诊断。既有回归首轮 Common/LogMonitor 的剪贴板用例失败，隔离重跑通过，未修改或放宽断言；不据此声称已修复剪贴板环境的偶发失败。

```powershell
cmake --build --preset x64-release --target qiqi_change_sync_test
ctest --preset x64-release -R '^QiQiChangeSyncTest$' --output-on-failure
# 两个架构的测试程序均已构建后；反向测试交换 x64/x86
.\build\x64\Release\qiqi_change_sync_test.exe --peer "$PWD\build\x86\Release\qiqi_change_sync_test.exe"
```

接口依据：[文件变更通知](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstchangenotificationw)、[文件锁](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)、[AppContainer 隔离](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation)。真实测试只证明当前环境的公共协议路径，不等同于所有宿主或沙箱策略均已验收。

### 5.15 词库与词频文件接入跨进程同步（任务 40）

- **通道路径与源标识**：[`GetSyncChannelDirectoryForFile`](../src/common/config.h) 为目标数据文件分配 `<绝对路径>.sync` 专用通道目录，词库与词频分别使用 [`kGuidSyncSourceDictionary` 与 `kGuidSyncSourcePhraseFrequency`](../src/common/guids.h)。
- **词库增量与外部编辑检测**：[`WubiEngine`](../src/ime/engine/engine.h) 的 `AddUserPhrase`、`AddUserEntry`、`RemoveUserEntry`、`ModifyUserEntry` 在排他锁下先合并远端增量，再写入词表、通道快照、`source.meta`（大小、修改时间、FNV-1a 哈希）与 `.qdict`，并通过 `WubiMappedDict::InvalidateProcessCacheForPath` 失效旧进程缓存。`CheckAndPublishExternalUserDictChanges` 在检测到外部编辑器直接修改词表时，对 `<= 16` 条且未打乱幸存词顺序的差异发布 `Add`/`Remove` 增量，其余情况旋转基线触发全量快照重载。
- **二进制词频（`QPF1`）与调序同步**：`SavePhraseFrequencies` 输出小端二进制格式 `QPF1`（含魔数、版本、编码/词条表与尾部 64 位 FNV-1a 校验），`LoadPhraseFrequencies` 同时兼容 `QPF1` 与旧版 UTF-8 文本。`PromotePhraseForCode` 在普通桌面进程中先同步远端未应用调序，再发布 `Operation::Reorder` 并更新 `snapshot.qfreq`；在 AppContainer 中仅更新内存且拒绝写盘。
- **输入边界隔离**：[`tip_keys.cpp`](../src/ime/tsf/tip_keys.cpp) 与 [`tip_config.cpp`](../src/ime/tsf/tip_config.cpp) 在焦点切换及非组合态按键入口触发 `SynchronizeCrossProcessChanges(InputBoundary::NewInput)`，组合态下返回 `S_FALSE` 暂缓，保证打字中途候选不重排。

### 5.16 恢复结果与同步适配器失败一致性（R01、R06）

- **恢复先校验后应用**：[`RestoreBackupBundle`](../src/tools/dict_manager/model.cpp) 完整读取、校验备份并生成计划后才修改目标；返回失败、部分完成或完整成功，保留首次失败阶段与 HRESULT、操作计数及可能的持久化影响。前序失败后不继续保存配置；兼容 bool 入口只接受完整成功。
- **刷新必须检查**：[`ReloadAllResult`](../src/tools/dict_manager/model.h) 以新引擎重建视图，检查文件读取、映射加载与同步结果。可选源不存在是正常情况，存在但不可读是失败；Replace 准备后的刷新和最终刷新均纳入结果，独立 `refresh_error` 保留刷新失败而不覆盖首次应用错误。界面不将刷新失败显示为恢复成功，也不声称其他进程已同步。
- **不承诺跨资源回滚**：恢复可能已经修改部分文件；包括源写入成功、必要镜像失败而返回 false 的情况。零个确认成功操作不代表没有持久化影响，必须通过 `Partial`/`effects_possible` 和提示说明；不通过盲目重写或删除来模拟事务回滚。
- **同步先暂存后发布**：[`WubiEngine`](../src/ime/engine/engine_dict.cpp) 在候选状态应用完整词库/词频批次，成功后交换发布。词库候选包含用户词、置顶、屏蔽及调序，快照同时暂存映射引用；失败保留本适配器原有状态、已应用版本和成功计数。词库与词频分别提交，不构成通道间的全局事务，组合态同步边界和 FSM 不变。
- **必要词频镜像错误不吞掉**：`SavePhraseFrequencies` 检查源写入、通道创建及 `snapshot.qfreq` 写入；保留真实失败诊断，调用方不得将源写入成功视为整个保存成功。
- **回归入口**：[`引擎测试`](../tests/ime/engine/dict.cpp) 比较失败前后的四类词条、版本和计数，覆盖非法后项、重复失败、快照读取失败与恢复，以及必要镜像/通道写入失败；[`词库管理器测试`](../tests/tools/dict_manager/main.cpp) 覆盖非法备份、配置拒绝、锁定文件/通道、部分结果、可选文件缺失、刷新失败与中英文提示。测试使用隔离目录，不以用户词库试错；不代表已完成真实宿主、崩溃恢复、分配失败注入或性能基准验收。

### 5.17 配置字段投影与自带词库加载计划（R03、R04）

- **新增标量字段先检查投影**：[`kConfigFields`](../src/common/config.h) 显式列出成员指针、稳定备份键、注册表键、数值范围与设置可编辑标志。注册表和备份共用这份类型化描述；键名不能用大小写转换推导。默认值仍由 `QiQiConfig` 初始化，路径等特殊字段不自动加入备份。
- **格式边界不变**：布尔读取兼容非零值；注册表保存页大小时钳制到 1–9，加载缺失/非法标量时保留默认值；备份恢复拒绝已知字段的非法数值，忽略未知数字键。备份键顺序及校验格式不变。
- **只合并可编辑字段**：[`MergeSettingsConfig`](../src/common/config.cpp) 在外部配置副本上覆盖相对基线确已修改的可编辑标量；不分配内存、不复制路径。窗口实际控件绑定、读写、布局及语言刷新继续由 [`controls.cpp`](../src/tools/settings/controls.cpp) 维护。增加控件时同步检查投影标志和真实窗口矩阵，不扩大隐藏字段的编辑范围。
- **选择与执行分离**：[`GetBundledDictionaryLoadPlan`](../src/common/config.h) 只返回自带主词库、扩展词库的角色、文件名及加载顺序；三种主词库与扩展开关共有六种组合。IME 和工具分别执行，继续复用 `ResolveBundledDataFile` 的路径查找和现有映射缓存；用户来源及 IME 注册表回退不搬入纯计划。
- **验证入口**：[`公共测试`](../tests/common/common.cpp) 检查格式键集合、注册表策略、逐字段合并和计划矩阵；[`设置测试`](../tests/tools/settings/save.cpp) 覆盖 19 个可编辑字段各自的干净/已编辑窗口；[`词库工具测试`](../tests/tools/dict_manager/main.cpp) 覆盖备份投影及来源/顺序、相对用户路径、缺失文件和缓存复用；[`TSF 测试`](../tests/ime/tsf/service.cpp) 保留用户来源回退与生命周期验证，不替代真实 AppContainer 或宿主验收。

### 5.18 词库编辑草稿与窗口边界（R02）

- **草稿唯一所有者**：[`DictEditor`](../src/tools/dict_manager/editor.h) 以 variant 表达 Browsing / NewDraft / ExistingDraft，拥有词条、编码和原项副本。不要在 View 新增与其重复的编辑、脏状态或手动编码标志；脏状态由文本是否恢复原值决定。
- **先决策再执行**：[`PrepareSave`](../src/tools/dict_manager/editor.cpp) 区分未编辑、无修改、原项缺失、添加和替换；原项身份按来源/编码/词条核对，不依赖刷新后可能变动的行 ID。窗口先同步外部记录，再执行需要写入的计划；`CompleteSave(false)` 保留草稿，成功才退出。词条/编码规范化和落盘校验仍由已有模型处理。
- **编码接管**：只有未手动改码的新建草稿随词条变化自动取码；已有项不自动覆盖编码。主动“自动取码”可更新当前编码，但不清除已经接管的手动模式。取消或重新开始新建草稿时才重置该状态。
- **窗口保留控件职责**：[`window.cpp`](../src/tools/dict_manager/window.cpp) 将控件变化送入草稿，菜单、工具栏、右键菜单与命令分派复用命令许可。IME 组合态、通知更新守卫、鼠标捕获、焦点与编辑行位置留在 View；组合态 Enter/Esc 不提交或取消草稿。
- **测试**：[`词库工具测试`](../tests/tools/dict_manager/main.cpp) 同时包含不创建窗口的状态/保存动作矩阵和真实窗口回归，检查失败保留、原项外部删除后不复活、行 ID 重排、编码接管、筛选切换、主题/DPI 与组合态按键。未引入 Undo、跨资源事务或新的 TSF 生命周期抽象。

### 5.19 TSF 生命周期票据与独立清理（R05 首步）

- **票据是非拥有快照**：[`EditSessionTicket`](../src/ime/tsf/edit_session.h) 配对激活/上下文 epoch 与原始目标身份，只比较值，不执行 COM 调用。异步回调仍必须捕获原有 context/service/view 强引用；会话内操作继续保留组合强引用和组合身份复核。不要把裸目标指针当成所有者，也不要在宿主重入后重新捕获 epoch 来“更新”旧任务的票据。
- **验证不只是指针相等**：[`IsEditSessionCurrent`](../src/ime/tsf/tip_composition.cpp) 还要求服务激活且不在重置中。保持原有宿主调用前后的检查位置，不把检查缩减到回调入口。A-B-A 必须因 epoch 不同而拒绝；空目标票据仅用于输入目标准备/文档切换后的内部重置检查，不授予编辑权限。
- **清理不能按当前票据取消**：[`CompositionCleanup`](../src/ime/comp/cleanup.h) 独立拥有旧组合及其资源，保留移除、宿主终止、开始/完成状态和同 context 的写入屏障。Deactivate 使旧编辑失效，但旧清理仍可完成；未执行请求被丢弃时保留原诊断并释放引用，不宣称宿主清理成功。不要复用旧编辑 cookie，也不要把 STA 操作转移到后台线程。
- **回归与范围**：[`生命周期测试`](../tests/ime/tsf/service_lifecycle.cpp) 覆盖票据匹配矩阵、旧布局/提交/取消回调与跨激活清理/新写入交错，以及未执行队列释放后的对象计数。继续保留同步/异步清理、宿主终止和 UI 注册重入测试；宿主 `BeginUIElement` 许可、FSM 与定位策略不变，完整协调器提取尚未实施。

### 5.20 TSF 清理登记边界（R05 第二步）

- **记录拥有，登记不拥有**：[`CompositionCleanupRegistry`](../src/ime/comp/cleanup.h) 只保存弱引用；编辑会话回调继续强持有 `CompositionCleanup`，记录继续拥有 context、composition、range。登记器析构不取消外部记录，也不调用 COM 清理；它只在所属 STA 使用，不引入锁、线程切换或通用事件队列。
- **不要统一不同的裁剪时机**：`Track` 只追加；`FindComposition` 不裁剪已完成记录，且空组合不匹配未发布结果。`FindPending` 先移除全部已完成/过期弱记录，再按原顺序返回同 context 的首项；`TrackedCount` 保留原始弱槽位诊断含义，不代表尚未完成的任务数。
- **移除标记覆盖全部匹配项**：`MarkContextRemoved` 同时设置 `context_removed` 和 `finished`，包括已完成项及 id 为零的未发布结果；不影响别的 context，也不在此时裁剪或代替宿主清理。按原阶段及真实 HRESULT 记录诊断，不能把标记完成或释放记录解释为宿主组合已结束。
- **发起时机留在服务**：[`tip_composition.cpp`](../src/ime/tsf/tip_composition.cpp) 决定清理时机并接收宿主终止通知；记录创建/登记、待清理判断及清理模块入口由专用协调器统一管理，见 5.25。当前组合的终止/引擎/UI 重置仍在服务。不能为了提取登记逻辑而挪动重入检查或让新写入越过旧清理屏障；[`登记与生命周期测试`](../tests/ime/comp/cleanup.cpp) 同时固定弱所有权、查询/裁剪差异、移除范围及现有宿主执行顺序。

### 5.21 TSF 写入任务快照（R05 第三步）

- **任务与目标不可分离**：[`CompositionEditTask`](../src/ime/comp/edit.h) 拥有 context、完整结果副本、原票据和活动 ID，构造时以 GSL 前置条件要求非空 context 且目标匹配。通过只读访问器消费，复制/移动构造传递，不在已发布任务上赋值改写；移动后的源对象不再执行。任务不持有服务或编辑 cookie，回调仍单独强持有服务。
- **先捕获再请求**：[`ApplyEngineResult`](../src/ime/tsf/tip_composition.cpp) 保留原有强引用获取、epoch 捕获和结果复制时机；请求标志仍在原位置根据清理屏障决定。只把原回调体移入 `ApplyCompositionEditInSession`，不挪动票据验证、清理判断、提交/取消区别或宿主调用后的复核。
- **再排队保留快照**：清理仍未完成时，不执行当前写入，而是将该任务的结果重新排队；不要重新读取引擎来构造替代结果，也不能把旧编辑 cookie 带给后续会话。活动 ID 仍按原作用域恢复，失效任务仍按既有诊断跳过。
- **验证与范围**：[`任务与生命周期测试`](../tests/ime/tsf/service_lifecycle.cpp) 比较四类动作的 12 个结果字段，覆盖复制/移动、生产者结束后的延迟访问及清理中重入再排队的 FIFO 快照。布局读取、独立清理回调、宿主 UI 许可及 FSM 不变；本步不等于已完成整个生命周期协调器。

### 5.22 TSF 独立清理执行与属性复核（R05 第四步）

- **执行期间固定强所有权**：[`ExecuteCompositionCleanup`](../src/ime/comp/cleanup.h) 按值接收记录，只在对应 context 允许写入的编辑会话内调用，不保存 cookie 或改用后台线程。作用域守卫保证每个返回分支都设置 `finished`；宿主释放调用方引用不影响正在执行的记录，登记器仍只弱持有它。
- **共用重入复核**：[`属性清除 helper`](../src/ime/comp/attribute.h) 先验证目标，查询属性后再验证，只有仍有效才设置清除开始标志并调用宿主。当前组合编辑、宿主终止和独立清理共用此实现，但使用各自的有效性条件：独立清理在移除/终止后停止，宿主终止分支的属性移除规则见 5.24。
- **执行结束不等于宿主成功**：属性查询/清除失败按真实 HRESULT 诊断，成功却无属性视为 `E_UNEXPECTED`；尽力结束组合的既有策略不变，若调用 `EndComposition` 则返回其实际结果。不能用 `finished`、`attribute_clear_started` 或单个返回值推断所有清理操作均成功，也不借本次拆分增加重复结束或自动重试。
- **验证与范围**：[`清理执行及生命周期测试`](../tests/ime/comp/cleanup.cpp) 检查 13 组执行/重入/失败边界、helper 的真实错误码和入口失效，以及调用方释放引用后的完成与销毁。服务仍负责发起时机、当前组合终止及激活/上下文/UI 协调；完整组合生命周期协调器保持未完成状态。

### 5.23 TSF 清理请求与受限回退（R05 第五步）

- **先登记后调用宿主**：[`RequestCompositionCleanup`](../src/ime/comp/cleanup.h) 要求非空 context/组合和有效 client ID；生产路径由专用协调器先登记记录，再获取 range、请求编辑会话。`GetRange` 和 `RequestEditSession` 都可能重入，不能让移除/终止通知找不到进行中的记录。
- **区分两个 HRESULT 和回调是否开始**：只有请求 API 成功且会话返回 `TF_E_SYNCHRONOUS`，并确认尚未开始、未终止、未移除，才回退到异步。请求失败或已执行回调返回同一码均不重试；只读等拒绝也不改变策略。保持同步/异步各自的真实 HRESULT 诊断，`async_requested` 只表示曾尝试请求，不表示入队成功。
- **保持原有尽力与释放边界**：range 查询失败或返回空 range 仍诊断并尝试结束组合，查询中失效则跳过请求。回调按原规则强持有记录及 COM 资源，使用其本次编辑 cookie；宿主丢弃未执行回调后允许弱登记过期，不把本地释放当成宿主组合已结束。
- **回归**：[`请求调度测试`](../tests/ime/comp/cleanup.cpp) 的 17 组矩阵固定请求次数、标志、client ID、回退条件、登记可见性、cookie、错误/失效状态及丢弃释放；既有清理屏障、重入、宿主终止及私有文档原生 TSF 测试继续保留。整体生命周期协调器仍未全部提取。

### 5.24 TSF 已登记组合的宿主终止（R05 第六步）

- **分离通知归属与清理成功**：[`HandleCompositionCleanupTermination`](../src/ime/comp/cleanup.h) 查找并强持有已登记记录，返回值仅表示通知是否已处理；空组合、未匹配组合及过期记录返回未处理，由服务保留原有当前组合/无关组合路径。登记器仍只负责弱引用记账，不执行宿主调用，也不在终止查询中裁剪已完成记录。
- **保留未发布与重复通知语义**：id 为零的未发布结果只标记终止/完成并记录生命周期阶段。普通记录先标记终止，再调用属性接口，重复通知直接退出；已移除上下文不再访问宿主，已开始清除的属性不重复清除。尚未执行的清理标记完成，正在执行的清理仍由原作用域守卫结束；本函数不请求新编辑会话，也不再次调用 `EndComposition`。
- **属性清除只以移除为失效**：终止标志此时已经设置，不能拿它否定本次必要的属性清除。查询属性后仍复核上下文是否移除；失败保留实际 HRESULT，成功却无属性保留 `E_UNEXPECTED` 诊断，不把“已处理”当成清除成功或新增重试策略。
- **回归与边界**：[`宿主终止测试`](../tests/ime/comp/cleanup.cpp) 的 17 组矩阵覆盖未发布、运行/完成、重复、移除、缺少 range、已开始清除、真实属性错误/空属性，以及属性查询/清除中的移除和重复通知；另验证空/未匹配/过期记录，以及重入裁剪登记并释放调用方引用后的记录存活与最终销毁。请求测试的 range 查询中终止也改为经过实际处理入口。当前组合终止、引擎/UI 重置、FSM 和宿主许可不变；x64/x86 构建、CppCoreCheck 及串行 TSF/UI 测试通过，完整协调器仍后置。

### 5.25 TSF 专用清理协调器（R05 第七步）

- **收拢清理工作流而非整个服务**：[`CompositionCleanupCoordinator`](../src/ime/comp/cleanup.h) 持有固定服务日志身份、服务生命周期内递增的清理编号及弱登记器，统一未发布记录创建、清理记录创建/登记、待清理判断、移除及宿主终止入口；请求、执行和终止继续复用已有 helper。它不拥有服务、引擎、活动目标或 UI，也不在 Deactivate 时重置编号或丢弃旧清理。
- **保持创建与登记顺序**：`TrackStarting` 返回调用方强持有的 id 为零记录，仍在 `StartComposition` 前登记；发布、失效判断及未发布结果的完成标记留在服务。`Schedule` 接收强引用目标、原 client ID 及创建日志上下文，先创建/诊断可用性，再裁剪旧槽位、登记并调用宿主；缺少 context/client ID 仍按原策略诊断并返回，不顺便裁剪已有槽位。传入 epoch 仅用于创建诊断，不是旧清理的取消票据。
- **屏障不与服务激活绑定**：两个写入屏障检查点继续在原位置调用 `HasPending`，包括回调执行中再次检查及保留原结果重排队；`HasPending` 仍使用原查询/裁剪和阶段日志。焦点/上下文移除入口转交协调器，但当前组合终止、激活、上下文与引擎/UI 重置不迁移。
- **销毁不取消宿主回调**：协调器只间接持有弱记录，队列回调继续只强持有独立记录及其 COM 资源，不捕获协调器或服务。协调器析构不执行 COM 清理、不改写外部未发布记录，也不取消已入队任务；真实 HRESULT 和尽力清理策略保持不变。
- **验证**：[`协调器回归`](../tests/ime/comp/cleanup.cpp) 的 9 组调度矩阵检查登记可见性、同步/异步请求、range 查询中失效、拒绝、丢弃、入队后移除/重复终止及 cookie/调用次数；另验证不可用目标不访问宿主或提前裁剪、未发布记录的弱所有权与终止、同 context 的全部移除、跨 context 隔离、完成槽位裁剪及协调器销毁后的回调执行/释放。x64/x86 IME DLL/TSF 测试构建和 CppCoreCheck、串行 `QiQiTsfUiTest` 通过；完整组合生命周期协调器仍未完成。

### 5.26 TSF 范围写入与光标折叠（R05 第八步）

- **只提取同步宿主操作**：[`WriteCompositionTextAndCaret`](../src/ime/comp/range.h) 执行原有 `SetText`、`Clone`、`Collapse(TF_ANCHOR_END)`、`SetSelection` 序列。服务的 `SetRangeTextAndCollapseCaret` 仍在原时点捕获票据和组合强引用，传入绑定该原组合的检查器；helper 不保存 cookie、目标或检查器，也不授予编辑会话权限。context/range 及检查器依赖的对象由调用方强持有，caret clone 则由 helper 的局部 COM 强引用持有至调用结束。
- **保持复核位置而非另建入口**：生产调用方先验证目标，helper 在写入文本、成功取得 caret clone、折叠后分别复核；首次复核之前仍会执行 `SetText`，不可将 helper 当作自行验证入口的写入 API。selection 返回后的验证仍由原服务调用链负责，不在 helper 中新增检查、请求或重排队。
- **保留原尽力错误策略**：各失败保留实际 HRESULT；`SetText` 或 `Collapse` 失败后，若目标仍有效，仍按原策略继续后续步骤，不能将 helper 返回或光标更新解释成写入成功。`Clone` 失败或成功却无 range 时停止 caret 操作；后者新增明确警告并记录原 HRESULT，不再只有静默跳过，但不增加自动重试。
- **固定文本与 selection 形状**：传递完整 `wstring_view` 的 UTF-16 码元长度，不按零终止字符串截断，也不改成字节数或 Unicode 码点数。selection 只含一个已折叠的 clone，使用本次 cookie、`TF_AE_NONE` 和非 interim 标志；不复用原 composition range 作为 caret。
- **验证**：[`独立范围编辑回归`](../tests/ime/comp/range.cpp) 将 13 组正常/各边界失效/失败/空 clone 条件分别应用于 ASCII、空文本及含内嵌 NUL、中文和代理对的 UTF-16 文本，共 39 组。检查精确调用/复核顺序、HRESULT 对应的继续/停止策略、文本与长度、cookie/flags、折叠锚点、selection 形状及 helper 退出后的 clone 引用释放。既有真实服务重入矩阵和原生私有文档 TSF 回归继续通过；x64/x86 构建、CppCoreCheck 及串行 TSF/UI 测试通过。FSM、候选定位算法和宿主 UI 许可不变，未据此宣称修复偶发光标偏移。

### 5.27 TSF 组合开始前的资源准备（R05 第九步）

- **返回拥有资源的准备值**：[`PrepareCompositionStart`](../src/ime/comp/start.h) 按原顺序查询 `ITfInsertAtSelection`、以 `TF_IAS_QUERYONLY` 获取插入范围，再查询 `ITfContextComposition`；每步成功且返回非空资源后才调用原票据/组合身份检查器。返回值的 `ready` 仅表示三步准备和复核通过，不代表组合已开始，也不是之后宿主调用的免检许可。
- **未就绪不等于没有资源**：[`CompositionStartPreparation`](../src/ime/comp/start.h) 保留此前取得的全部 COM 强引用，包括部分失败或复核失效的结果。特别是最后一次复核失败时三个指针仍可非空，必须检查 `ready`，不能以指针齐全代替它。字段顺序保留原接口、range、组合接口的逆序释放关系；服务将准备值保留到原作用域结束，不在 helper 返回时提前释放。
- **创建与发布仍在服务**：[`StartOrUpdateCompositionInSession`](../src/ime/tsf/tip_composition.cpp) 仍在原时点捕获票据/client ID，仅在准备就绪后登记未发布结果并调用 `StartComposition`；完成标记、失效判断、旧结果清理、终止后的重置及当前组合发布均留在原位置。helper 不登记清理、不开始组合、不写文本、不更新 UI、不请求编辑会话，也不保存 cookie 或检查器。
- **错误保持真实且可见**：接口或范围获取失败保留原 HRESULT 诊断和停止策略；成功却返回空接口/范围时仍停止，新增包含原 HRESULT 的明确警告。检查器失效仍由服务原有生命周期诊断记录，不额外查询 COM 身份或重试。
- **验证**：[`独立开始准备回归`](../tests/ime/comp/start.cpp) 的 12 组条件固定正常流程、入口/三处宿主边界失效、三步失败及成功却返回空资源的行为。检查精确调用/复核顺序、query-only 的 cookie/flags/空文本/零长度、各部分资源与 `ready`、准备值存活及退出后的真实 COM 引用计数，并确认无文本/selection/组合/属性/会话副作用。既有服务开始/发布/重入与原生私有文档 TSF 回归继续通过；x64/x86 构建、CppCoreCheck 和串行 TSF/UI 测试通过。完整 R05 仍未完成，FSM、当前组合及 UI 协调不变。

### 5.28 TSF 未发布组合的结果裁决（R05 第十步）

- **沿用清理协调器**：[`ResolveStarting`](../src/ime/comp/cleanup.cpp) 返回 `Publish`、`Discard` 或 `Terminated`，收拢未发布结果的失效判断及旧组合补救清理，不新增队列或协调器。仅成功、非空、仍有效且未移除/终止的结果允许发布；过期但仍连接的活组合即使开始调用失败也继续按原策略清理。
- **保持检查与宿主顺序**：服务仍依次执行 `StartComposition`、原 HRESULT 诊断、`finished = true`、原票据/组合身份检查，再进入裁决；准备资源和未发布记录保留在原作用域。协调器以强引用持有记录，先调用已有 `Schedule`，返回前再读取终止标志，不能缓存重入前的判断。
- **重置与发布仍在服务**：`Terminated` 不代表已重置或清理成功；服务再次检查原票据后才重置引擎/UI，或仅在 `Publish` 时移动组合引用。开始失败的原诊断不变；成功却返回空组合时新增携带原 HRESULT 的警告，不记录输入内容。
- **验证**：[`开始结果回归`](../tests/ime/comp/start.cpp) 新增 16 种身份/移除/终止/空结果 × 3 种 HRESULT × 同步/异步，共 96 组矩阵；另有 4 组重入/释放和 2 组协调器销毁后队列执行/丢弃场景。验证结果类型、移动与保留的强引用、登记先于宿主调用、client ID、flags、cookie、待清理屏障、清除/结束次数及对象计数恢复。原 12 组准备测试和真实服务重入/原生 TSF 测试保留；x64/x86 构建、CppCoreCheck 及串行 TSF/UI 测试通过。完整 R05 仍未完成，FSM、宿主 UI 许可及定位算法不变。
