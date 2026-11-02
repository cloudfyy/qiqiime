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
├── VERSION                     # 项目版本号单一事实源 (SemVer)
├── vcpkg.json                  # vcpkg 依赖清单 (ms-gsl)
├── vcpkg-configuration.json    # vcpkg 注册表基线配置
├── data/
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
│           ├── message.cpp # 复用 D2D 控件的隐私确认、保存失败与通知失败提示窗口
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
            ├── edges.cpp # 配置及生命周期边界测试
            ├── version.cpp # 设置 EXE 版本资源测试
            ├── save.cpp  # 保存失败、重试、取消及通知失败提示测试
            └── message.cpp # 各设置窗口的主题、语言、提示框键盘及模态行为测试
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

## 4. 代码规范与静态代码检查（`lint`）

项目遵循 **C++ Core Guidelines**，使用 `gsl::span`、`gsl::not_null`、`gsl::narrow_cast`、`gsl::at`、`gsl::finally` 及 `ScopedHandle<Traits>` RAII 模板。`lint` target 使用 MSVC CppCoreCheck 检查 C++ Core Guidelines 与 GSL 规则，并使用 PSScriptAnalyzer 检查 `scripts/` 下的 PowerShell 脚本。C++ 检查覆盖公共库、引擎、设置界面、输入法、设置程序及测试目标；按既有规则排除 C26490，并以 435 条去重后的其他 CppCoreCheck 诊断作为当前基线，新增诊断会使 lint 失败。基线应在逐步修复旧告警后下调。

安装 PSScriptAnalyzer 后，可从已配置的 Visual Studio CMake 构建目录运行：

```powershell
Install-Module -Name PSScriptAnalyzer -Scope CurrentUser
cmake --build build\x64 --config Release --target lint
```

项目常规编译另启用 MSVC `/W4`；`lint` 会在指定配置下重建启用静态分析的目标。

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
- **监控步骤**：日志级别设置、原生调试器实时查看、`logman` 采集与 WPA 分析见[应用日志监控指南](LOGGING.md)。任务 55、51 已修复初始化及停用影响共享日志的问题；ETW 无需附加调试器。
- **初始化与生命周期**：正常入口调用 `EnsureInitialized`，幂等注册且不改变日志级别；`InitLogger` 额外设置级别。注册成功后才发布状态，查询及写日志不隐式注册；同进程 IME 实例共享，同名 Provider 在不同进程独立。`Deactivate` 不注销，模块清理在调用者结束后注销，不在 `DllMain` 注册、格式化日志或获取初始化锁。
- **焦点性能埋点**：任务 50 使用 `std::chrono::steady_clock` 记录纳秒耗时，仅在详细级别且有接收方时计时；按进程内递增编号关联焦点处理与重载详情，保留原检查/重载顺序。字段和真实宿主复现步骤见[焦点处理耗时](LOGGING.md#61-焦点处理耗时任务-50)，不能将状态相同或测试进程耗时当成可合并通知、需要后台化的依据。
- **日志提供程序**：Windows ETW TraceLogging 提供程序 `QiQiIME.TraceLogging`；仅在调试器附加时额外输出到 `OutputDebugString`，不会写入 `%LOCALAPPDATA%\QiQiIME\logs\qiqiime.log`。
- **日志字段**：结构化事件包含级别、宿主进程名、进程 ID、线程 ID 和消息。
- **隐私与异常安全**：所有 `QIQI_LOG_*` 宏均内建 `try/catch` 异常隔离（可在 `noexcept` 函数中安全调用），且严禁在日志中记录用户的私密上屏文字内容。
- **失败诊断**：保存实际返回值，记录操作名与错误码。COM 可复用 `log::CheckHResult`，输出 `hr=0x{:08x}`，只将失败 HRESULT 记为失败，`S_FALSE` 与 `TF_S_ASYNC` 不误报；该帮助函数保留调用线程的 `GetLastError`。
- **Win32 与注册表**：仅在 API 文档约定支持 `GetLastError` 时，于失败后立即捕获；注册表及 ACL API 使用其直接返回的错误码。读写成功但字节数不足时记录实际字节数和 `ERROR_SUCCESS`，不能输出陈旧的线程错误。`SetWindowSubclass`、部分 GDI 操作及输入校验失败只记录操作和条件，不编造错误码。
- **预期回退与资源所有权**：未配置的可选注册表值、未生成的缓存与正常无布局状态不是最终失败；探测采用调试/跟踪级别，已记录底层原因的上层消息仅说明回退或用户影响。函数提供程序与语言栏仅清理本激活周期成功注册/添加的项，真正注销失败保留诊断。
- **日志自身失败**：ETW 注册结果是 Win32 `ULONG`，不能按 HRESULT 判断；注册、格式化或编码失败使用不递归的调试输出兜底，日志内部不调用会再次产生日志的文本转换帮助函数。引擎日志不记录逐键键值、用户词编码或词语正文。

### 5.3 失败日志审计范围（任务 49）
- 已检查生产代码中的失败日志、COM 返回值、文件/注册表读写及异常兜底，覆盖 `common`、IME 引擎、TSF、候选窗/语言栏/状态条、设置程序及安装/构建脚本；测试与第三方依赖仅作为验证依据，未修改第三方代码。
- 修复实际 HRESULT 漏报、ETW 注册错误判断、注册表/文件读写及缓存映射错误码缺失、回调异常吞错，以及设置程序单实例互斥量失败；构建脚本补充阶段/架构/退出码诊断。保留可选能力失败后的原有回退，COM CLSID 必需值写入失败不再报告注册成功。
- 失败注入覆盖函数提供程序及语言栏部分成功、注册失败、注销失败、重复停用和再次激活；公共日志测试覆盖成功/异步 HRESULT 与错误码保留；配置、文件、词库、设置及绘制由现有回归测试覆盖。
- 任务 48 的保留键所有权策略、任务 52 的进程词库缓存策略、任务 53 的 DLL 生命周期与任务 42–47 的异步生命周期处理仍独立待办；审计阶段只补失败诊断，任务 51 的共享日志问题后来与任务 55 一并修复。
- 验证：x64/x86 Release 构建及各 4 组 CTest 全部通过，`lint` 的 CppCoreCheck 与 PSScriptAnalyzer 通过；模拟 CMake 返回 37，确认构建脚本输出阶段、架构和错误码并保留退出码。首次 x64 窗口交互测试失败且后续设置测试未结束，停止该测试进程后，TSF/UI 隔离重跑与最终全套重跑均通过；此记录不等同于已修复窗口交互测试的偶发性问题。

### 5.4 日志初始化与生命周期验证（任务 55、51）

- 公共测试覆盖失败注册的实际返回码、失败不发布已注册状态、成功重试、并发初始化只成功注册一次、注册完成前查询为 false、未初始化/重复关闭不注册，以及 Off 下的显式初始化。TSF/UI 测试覆盖停用一个实例保留 Provider、重新激活不重置 Off。
- `QiQiEtwIntegrationTest` 启动两个无调试器子进程加载真实 `qiqiime.dll`，覆盖采集先于/晚于初始化、一个进程退出后另一个继续输出、同进程多实例停用/重新激活、模块卸载重载及 Off 过滤。实际 ETL 经 TDH 解码并断言目标 PID、预期消息、级别和零丢失，不只检查会话存在。每个子进程隔离 HKCU 与 LOCALAPPDATA，不修改真实配置或词库。
- 采集仅启用自有测试 PID；唯一命名会话、句柄、子进程作业及临时文件按生命周期清理。普通权限缺少采集控制权限时显式报告 `StartTraceW error=5`，CTest 标记跳过（退出码 77），不能当作 ETW 验证通过。
- 定向运行：`ctest --test-dir build\x64 -C Release -R '^(QiQiCommonTest|QiQiTsfUiTest|QiQiEtwIntegrationTest)$' --output-on-failure`；x86 使用 `build\x86`。本次两个架构实际 ETW 测试均通过，没有因权限跳过；这些受控宿主验证不替代浏览器/UWP 的兼容性或真实输入体验测试。
- 验证：x64/x86 Release 完整构建及各 5 组 CTest 全部通过；CppCoreCheck 在既有 435 条基线以内，PSScriptAnalyzer 零错误。仅对 ETW SDK 必需的回调联合体局部说明并豁免 C26476，不替换 Windows ABI，也不扩大告警豁免范围。

### 5.5 延迟编辑会话测试（任务 42）

- [`MockTfContext` 与 `MockEditSessionQueue`](../tests/Ime/tsf/tsf_mocks.h) 提供确定性的编辑会话调度，不使用后台线程或 `Sleep`。默认上下文的 `TF_ES_ASYNCDONTCARE` 请求仍立即执行；传入外部 `std::shared_ptr<MockEditSessionQueue>` 后排队。显式 `TF_ES_ASYNC` 必须排队，无调度器时明确失败；`TF_ES_SYNC` 立即执行，优先于待处理异步请求。
- 测试持有调度器，并将同一调度器传给需要验证的上下文；`PendingCount`/`PendingRequests` 检查待执行请求与目标，`RunNext(cookie)` 按 FIFO 执行一个请求并返回真实回调 HRESULT，默认 cookie 为 1。`ClearForCleanup` 仅用于显式测试清理。入队、运行和清理须在创建线程进行，错误线程或空队列执行会返回错误并记录诊断。
- 调度器强引用待执行上下文与会话，上下文仅弱引用调度器，避免 Context→Session→Context 引用环。运行前先移出请求，局部强引用保证回调期间对象存活，并允许重入入队；清理或销毁调度器释放待处理请求。回调不得强捕获拥有它的调度器，避免另一个引用环；已过期的调度器不能静默退回立即执行。
- Windows SDK 的 `TF_ES_ASYNCDONTCARE` 为 **0**，不是独立位。Mock 校验同步/异步冲突、访问模式和未知位；成功异步请求返回外层 `S_OK`、内层 `TF_S_ASYNC`。同步拒绝和只读读写拒绝分别返回内层 `TF_E_SYNCHRONOUS`、`TS_E_READONLY`；只读上下文仍允许读请求。这是最小请求标志与结果模型，不是完整 TSF 锁、编辑权限或 cookie 校验器。
- [`tsf_com.cpp`](../tests/Ime/tsf/tsf_com.cpp) 覆盖默认兼容性、FIFO、请求元数据、同步优先、失败注入、回调失败后继续执行、重入及引用释放；[`tsf_service.cpp`](../tests/Ime/tsf/tsf_service.cpp) 调度真实生产写回调和布局读回调，覆盖停用/重新激活后执行、输入上下文变化及旧队列保留。
- Mock 不理解激活周期，不自动丢弃过期任务，也不将旧回调可能修改文档的行为定义为正确。任务 43 已在生产回调中实现并断言过期任务不得写文档、创建组合或恢复 UI；任务 44 处理主动结束宿主组合。任务 42 本身不修改生产代码。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，ETW 未跳过；定向运行使用 `ctest --test-dir build\x64 -C Release -R '^(QiQiTsfUiTest|QiQiEtwIntegrationTest)$' --output-on-failure --timeout 120`，x86 替换为 `build\x86`。x64 测试目标同时执行 CppCoreCheck，新增 Mock 与测试没有新增诊断。

### 5.6 过期编辑会话屏障（任务 43）

- [`QiQiTextService`](../src/Ime/tsf/tip_service.h) 为每个服务实例维护激活状态及激活/上下文周期；有效激活尝试和停用改变激活周期，停用在任何清理之前标记为无效。部分激活失败仍通过 `Deactivate` 回滚，不使旧任务重新有效。
- [`ApplyEngineResult` 与 `OnLayoutChange`](../src/Ime/tsf/tip_composition.cpp) 在提交请求前保存周期，回调开始处通过同一检查核对激活状态、两个周期和目标上下文。强引用只保证对象存活，不代表目标仍有效；同一 COM 指针重新激活或 A→B→A 后，旧快照也必须失效。预期丢弃返回 `S_OK`，调试日志仅记录周期与激活状态，不输出输入文本，也不伪造失败 HRESULT。
- `ResetContextAndUI` 在 COM 清理前使上下文周期失效并清空活动目标，统一注销布局订阅、释放本地组合和结束候选 UI；现有的焦点丢失、宿主终止和停用重置复用它。切换输入上下文也使用该清理，避免新目标沿用旧组合范围；处理已生成的引擎结果时不额外重置引擎。提交请求前再次检查目标切换过程中是否发生重入失效。
- 布局通知只接受当前活动上下文，执行过期读回调时不查询旧视图。调度器仍保留旧请求并实际调用其生产回调，屏障不依赖 Mock 过滤；同周期的多个写入快照保持 FIFO，不按每个按键递增周期。
- [`生命周期测试`](../tests/Ime/tsf/tsf_service.cpp) 通过实际文本、写入/组合创建/视图查询与 UI 调用次数验证：停用及失败激活后执行旧任务、同一上下文重新激活、焦点重置、A→B→A、宿主终止、过期提交/取消，以及旧布局读任务遇到新组合。另验证正常 FIFO 更新、提交、下一次组合和延迟布局读，不只检查回调返回成功。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，ETW 未跳过；x64 生产与测试目标执行 CppCoreCheck，修复新增的 const 正确性诊断后无新增告警。仍保留既有告警，不扩大豁免范围。
- 范围：本任务覆盖现有重置入口及输入上下文周期，不建立文档管理器列表。后续任务 45 补充文档/栈通知，并修正“生成引擎结果后才切换目标”的跨输入框携带问题，详见下文 5.8；任务 44 已补充停用时的同步清理及受控异步补救。本地释放组合不等于已正确结束宿主组合，受控测试也不替代浏览器/UWP 的真实宿主验证。

### 5.7 停用组合清理与受控异步补救（任务 44）

- 完整的状态定义、强弱所有权图、同步/异步与重新激活时序、重入规则、日志协议和排障步骤见[组合生命周期设计](TSF_COMPOSITION_LIFECYCLE.md)；采集入口见[日志指南](LOGGING.md#62-组合生命周期与停用清理任务-434445)。
- 按更新确认的策略保留预编辑文字，并允许受控异步补救。清理只清除旧范围的 `GUID_PROP_ATTRIBUTE` 并结束旧组合，不删除/替换文本或恢复 UI。
- [`Deactivate`](../src/Ime/tsf/tip_service.cpp) 仍同步返回：先拒绝旧业务任务并设置停用保护，再通过 [`ScheduleCompositionCleanup`](../src/Ime/tsf/tip_composition.cpp) 请求 `TF_ES_SYNC | TF_ES_READWRITE`。仅当请求成功但会话结果为 `TF_E_SYNCHRONOUS`、回调未执行、宿主未终止组合且上下文未被移除时，再申请 `TF_ES_ASYNC | TF_ES_READWRITE`；不等待异步回调便清理本地引擎、订阅、UI 和线程管理器引用。任务 45 将该路径复用到仍连接的目标切换和失焦。
- 每次清理独立保存旧上下文、组合及范围。编辑回调强引用该状态，不捕获线程管理器或服务；服务只保存弱引用，避免服务与清理任务强引用成环。组合自身可能持有其 sink，COM 引用计数仍保证服务和模块存活到请求被执行或释放。RAII 复位停用保护及清理完成标志，任务被宿主放弃时捕获资源也会释放。
- 同一上下文有待处理清理时，新业务写请求强制使用 `TF_ES_ASYNC`，避免默认同步优先越过旧清理；TSF 的异步 FIFO 先结束旧组合，再创建新组合。其他上下文不受此调度限制。原业务回调仍检查任务 43 的激活/上下文周期，只有独立旧组合清理不依赖当前激活周期。
- [`OnCompositionTerminated`](../src/Ime/tsf/tip_composition.cpp) 区分当前、待清理及旧组合。宿主提前终止待清理组合时，使用通知的写 cookie 清理旧范围并标记终止，之后旧回调直接退出，不能清除新组合属性；清理过程中发生终止也不重复结束。非当前组合通知不重置新组合/UI。停用期间重入停用不重复清理，重入激活记录并返回 `E_UNEXPECTED`，外层结束后正常激活可继续。
- 只读、断开、请求拒绝及宿主范围/属性/结束 API 失败都记录实际 HRESULT，不无限重试或强行越过权限。读会话内升级写权限也不能假定异步就会成功。属性或范围清理失败不阻止在有效权限下尝试结束组合；停用返回 `S_OK` 或排队返回 `TF_S_ASYNC` 都不等于宿主清理已成功。这些不可恢复情形可能保留宿主组合/属性，应从诊断识别，不宣称所有宿主都能结束。
- [`Mock 回归测试`](../tests/Ime/tsf/tsf_service.cpp) 覆盖同步和异步 flags、保留文字、失败注入、重复停用、部分激活失败回滚、清理期间重入、旧组合通知、异步请求失败/断开、同一上下文重新激活及宿主提前终止；断言旧回调不调用 UI、不误清新属性，并检查释放外部服务引用后清理仍可完成、COM 对象计数恢复。
- 同一测试建立真实 Windows TSF 管理器及私有文档，使用 `TF_TMAE_NOACTIVATETIP` 禁止激活系统 TIP，不切换用户焦点。通过真实布局通知触发同步拒绝，验证通知内组合仍存在、通知返回后异步清理完成；枚举宿主组合和属性范围，断言保留文本、`GUID_PROP_COMPOSING` 与 `GUID_PROP_ATTRIBUTE` 消失。另验证立即重新激活后旧清理先于新写入，新的组合范围仅含新文字且属性保留；以有界消息泵等待真实异步处理，不用 `Sleep`。
- 真实 TSF 文档验证不同于 ETW 日志采集，不替代浏览器/UWP 实际宿主验证；任务 41 的窗口销毁仍独立待办，任务 45 的文档/上下文栈处理见下文 5.8。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`（含上述真实 TSF 文档测试）、`QiQiEtwIntegrationTest` 均通过，未跳过；定向 CppCoreCheck 修复新增非空类型约束并复用现有 VARIANT 初始化帮助函数后无新增告警。清理请求完成和被调度器放弃两条路径均检查 COM 对象计数恢复。

### 5.8 文档及上下文生命周期（任务 45）

- 继续以活动 `ITfContext` 为核心，仅缓存其所属文档用于移除后的关联判断，不建立文档管理器列表。焦点与同文档入栈统一通过 `ReconcileDocumentTarget` 查询栈顶/主上下文；主上下文被正常模态栈顶覆盖时保留组合，无关文档通知不得重置当前输入。
- 出栈按 context 身份处理，解除初始化按缓存文档的 COM 身份处理；当前目标移除先失效旧写入/布局任务，再清订阅、引擎和 UI，不请求新的编辑会话。已排队的独立清理也标记为 context 已移除，迟到回调不得再清属性或结束组合；本地释放不等于宿主已结束。
- 一并修正任务 43 的两个相关缺口：新目标的按键在引擎处理前准备上下文，避免旧组合通过 Backspace 等按键带到新输入框；仍连接的目标切换/失焦复用任务 44 的宿主清理，而不只是释放本地引用。A→B→A 时同一 context 的新写仍强制排在旧异步清理之后。
- 重置/绑定以 RAII 防止重入输入，文档查询和布局订阅之后复核周期，过时焦点通知不再继续显示 UI；重入激活明确失败。新增文档/栈日志记录关联判断、模态覆盖和是否尝试宿主清理，不记录输入内容。完整英文说明见[组合生命周期设计](TSF_COMPOSITION_LIFECYCLE.md#8-document-and-context-notifications)。
- Mock 验证延迟写入/布局、非空文档切换、模态入栈/出栈、无关通知、移除前断开、范围查询失败与重入、A→B→A 异步顺序、已排队清理的 context 被移除及 COM 对象计数恢复。原测试中的伪文档地址改为实际 Mock 对象。自有调试子进程校验实际输出的模态保留和非当前出栈字段。
- 真实 Windows TSF 私有文档通过实际事件订阅转发 Push/Pop/Uninit 通知，验证主组合跨模态 Push/Pop 保留、非空文档切换时旧属性/组合清除且文字保留、新文档组合独立、全部出栈时结束 UI；不调用原生 `SetFocus` 或激活系统 TIP，不假定通知顺序。
- 另覆盖清理范围查询期间重入出栈/解除初始化：移除标记先于新编辑请求，既不清属性也不调用结束 API；焦点处理使用独立 revision，普通 context 移除不吞掉失焦隐藏动作，较新的重入焦点通知则取代旧动作。
- 验证：x64/x86 Release 的 `QiQiTsfUiTest`、`QiQiEtwIntegrationTest` 均通过，未跳过；定向 CppCoreCheck 修复新增构造分配/noexcept 诊断后无新增告警。首次运行遇到旧测试伪文档地址访问异常，替换为实际 Mock 文档后双架构全部通过。窗口销毁与真实浏览器/UWP 验证仍由任务 41、47 处理。

#### 0.9.19 提交后的覆盖复核

- 补测父文档查询期间目标出栈与无关目标出栈、布局订阅发布期间重入移除及后续正常输入、订阅被拒绝、不同接口指针的 COM 文档身份与身份查询失败、`GetBase` 失败、清属性过程中出栈后不得调用 `EndComposition`。事件连接及配置恢复采用 RAII，最后检查 COM 对象计数恢复。
- 新回归先复现了一个实际缺口：`GetDocumentMgr` 重入移除目标时，目标尚未写入 `active_context_`，原来的布尔绑定保护无法识别该目标，返回后仍会绑定并写入。绑定保护改为持有待绑定 context，匹配出栈立即失效周期；外层不消费按键、不订阅、不申请编辑权限，无关移除不受影响。
- 使用独立服务和隔离的状态栏配置检查真实窗口可见性：普通出栈不应吞掉失焦隐藏动作；较新的重入得焦优先于旧失焦；文档查询期间失焦不应被旧外层通知重新显示。
- 前次 x86 验证中 Shift 轻触断言曾偶发失败，隔离重跑通过；现将线程键盘状态复位移到该序列之前，减少此前消息泵带来的环境状态干扰。未改变生产的 350 ms 限制、未增加盲目重试，也未确认该次失败的唯一根因；测试仍依赖实际调度。
- 此次按行为/分支复核，不报告未测量的覆盖率，也不替代任务 41、46、47 的窗口销毁、完整矩阵及真实宿主验证。
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
- 生命周期契约、诊断字段与验证边界见英文[设计文档](TSF_COMPOSITION_LIFECYCLE.md)。完整回归矩阵见下节；任务 47 的真实宿主验证仍独立待办。

### 5.9 生命周期回归矩阵（任务 46）

- 先盘点已有延迟写入/提交/取消/布局、FIFO、重新激活、焦点与 A→B→A、模态及无关通知、移除后屏障、清理失败矩阵、引用/订阅释放，以及任务 41 的真实 HWND 和 UI 注册重入；不重复计为本轮新增测试。完整矩阵见英文[设计文档](TSF_COMPOSITION_LIFECYCLE.md#91-regression-inventory-task-46)。
- 新增 `TestReentrantCompositionEdits`：86 个执行中编辑/宿主终止边界用例、8 个迟到 `StartComposition` 用例、4 个清理期间属性查询用例，并显式断言各矩阵计数；另外覆盖同周期替换组合的 2 例及键盘状态重入的 2 例。按实际文本、写入/属性/选择/视图/UI 调用次数、组合身份及对象计数验证，不只检查返回成功，不报告未测量的覆盖率。
- 测试先复现 `GetRange` 内停用后旧编辑仍写文档，以及清理已进入但 `GetProperty` 内宿主终止发生在实际 `Clear` 前导致漏清属性。编辑强持有原组合，并在宿主调用之间检查 epoch、context 和组合身份；清理独立记录 `attribute_clear_started`，需要时用宿主通知的 write cookie 清一次，移除后不再调用宿主编辑 API。
- 迟到的组合创建结果先私有持有，临时弱跟踪使用 `id == 0`，不消耗正式清理诊断 ID。期间的移除/终止不会被返回结果覆盖；仍连接的迟到旧组合转入独立清理，捕获原 client ID。同 context 新写若早于迟到清理入队，执行入口再次检查，并将已捕获结果显式异步排到旧清理后，不重算按键、不推进 FSM。
- 同一 epoch 也可能重入提交并创建替代组合；新增测试先复现旧更新/布局返回后继续刷新新 UI。更新返回其原组合快照，布局查询及 UI 发布之间继续复核身份；提交/取消在旧 `EndComposition` 前先使共享组合槽与旧 caret 状态失效，保留重入创建的新组合。
- 确定性键盘测试在旧目标清理及 compartment 查询中注入新的 Ctrl 状态，先复现当前按键被错误透传。`OnTestKeyDown`/`OnKeyDown` 改为在宿主调用前捕获当前事件的修饰键和 CapsLock，预判还捕获 Shift 轻触所需鼠标状态；下一事件正常观察新状态，FSM 表及消费规则不变。普通按键矩阵每次显式建立中性键盘前置状态，整个用例保存/恢复原线程状态，不影响专门的修饰键测试。
- 不能撤销已开始的宿主写入；回归在可观察写入之后触发重入，要求失效后不再发后续操作。实际浏览器/VS Code 的焦点、下划线和窗口行为仍由任务 47 验证，不以 Mock 或私有 TSF 文档替代。
- 最终验证：x64/x86 Release 的 `QiQiTsfUiTest` 与 `QiQiEtwIntegrationTest` 各连续通过 2 次、均未跳过；`QiQiEngineTest` 双架构通过。双架构构建与定向 CppCoreCheck 通过，修正新增的取消路径 const 诊断后，修改的生产代码无分析告警，仅保留既有其他路径/测试告警；`git diff --check` 通过。RAII、组合快照复用、Core Guidelines/GSL 及缺失测试已复核，未放宽断言或生产 Shift 轻触时限。
