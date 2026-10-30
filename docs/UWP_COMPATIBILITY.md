# 奇奇五笔输入法 (QiQiIME) UWP / AppContainer 兼容性分析与设计方案

## 1. 背景概述

Windows 10 / 11 中的现代应用（UWP 应用、部分 MSIX 打包应用、Windows 系统内置组件如搜索框/设置/便笺、以及开启了 AppContainer 沙箱的浏览器子进程）运行在 **AppContainer 低完整性沙箱（Low Integrity / AppContainer Token）** 中。

由于 TSF（Text Services Framework）采用**进程内 COM 组件（In-Process Server）**架构，当用户在 UWP 应用中切换到奇奇五笔时，Windows 会将 `qiqiime.dll` 直接加载进该 UWP 应用的沙箱进程空间中执行。因此，`qiqiime.dll` 中的所有文件 I/O、内存映射、配置读取、词库加载与进程间通知都必须遵循 AppContainer 的安全边界。

---

## 2. 当前架构与 UWP 兼容现状评估

### 2.1 已兼容的部分 ✅

| 模块 | 代码位置 | 兼容状态 | 技术原因 |
| :--- | :--- | :--- | :--- |
| **TSF 类别声明** | [`src/tsf/register.cpp`](../src/tsf/register.cpp) | ✅ 已兼容 | 注册了 `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` 与 `GUID_TFCAT_TIPCAP_COMLESS`，允许 Windows 在 Immersive / UWP 进程中激活输入法。 |
| **只读匿名内存映射 (`MapViewOfFile`)** | [`src/engine/wubi_binary_dict.cpp`](../src/engine/wubi_binary_dict.cpp) | ✅ 已兼容 | `WubiMappedDict::OpenMappedFile` 使用 `GENERIC_READ`、`FILE_SHARE_READ`、`PAGE_READONLY` 且 `lpName = nullptr`（匿名内核映射对象）。AppContainer 仅禁止跨沙箱访问命名内核对象，对已打开文件句柄创建匿名只读映射完全放行，且多个进程仍可在 Windows 内核物理页缓存（Section Object）层面共享同一份物理内存页。 |
| **内存映射失败兜底** | [`src/engine/wubi_binary_dict.cpp`](../src/engine/wubi_binary_dict.cpp) | ✅ 已兼容 | 若极端受限沙箱拒绝 `CreateFileMappingW`，会自动退化到进程内的 `fallback_buffer_`（`std::vector<uint8_t>`），保证输入法绝不崩溃或丢词。 |
| **日志降级保护** | [`src/common/logger.cpp`](../src/common/logger.cpp) | ✅ 已兼容 | `CreateLoggerInternal` 对日志文件打开失败做了 `try/catch` 静默忽略并退化到 `null_sink_mt`，不会因沙箱无写权限而抛出未捕获异常。 |
| **MSI 安装目录读取** | [`installer/QiQiIME.wxs`](../installer/QiQiIME.wxs) | ✅ 已兼容 | 正式安装路径 `C:\Program Files\QiQiIME` 继承了系统 `Program Files` 默认的 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 读取与执行（`RX`）权限，UWP 进程可正常加载 `qiqiime.dll` 并读取 `data\*.txt` 原始词库。 |

---

### 2.2 当前存在的不兼容痛点 ⚠️

#### 痛点 1：UWP 进程的 `%LOCALAPPDATA%` 被重定向且无权访问真实用户的 `%LOCALAPPDATA%\QiQiIME`
- **现状**：[`log::GetAppDataDirectory()`](../src/common/logger.cpp) 通过 `GetEnvironmentVariableW(L"LOCALAPPDATA", ...)` 解析路径，用于存放：
  - 用户配置文件：`%LOCALAPPDATA%\QiQiIME\config.ini`
  - 用户自造词库：`%LOCALAPPDATA%\QiQiIME\user_dict.txt`
  - 编译后的二进制词库缓存：`%LOCALAPPDATA%\QiQiIME\cache\*.qdict`
- **UWP 沙箱行为**：
  1. **环境变量隔离**：在 AppContainer 进程内，`LOCALAPPDATA` 被重定向至该 UWP 包的私有目录：
     `C:\Users\<User>\AppData\Local\Packages\<PackageFamilyName>\AC\...` 或 `LocalState`。
  2. **DACL 权限拦截**：即使计算出真实用户的 `C:\Users\<User>\AppData\Local\QiQiIME` 路径，Windows 默认的用户目录 ACL **不包含 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 的访问控制项（ACE）**。UWP 进程调用 `CreateFileW` 打开真实 `config.ini` 会直接返回 `ERROR_ACCESS_DENIED (5)`。
- **实际影响**：
  - 用户在普通 Win32 应用或未来的独立设置程序 `QiQiIMESettings.exe` 中修改了配置（如候选词个数、词库选择、四码唯一自动上屏等），**在 UWP 应用中完全不生效**（UWP 只能读到自己沙箱内的空白默认配置）。
  - 用户在 Win32 应用中通过在线造词写入 `user_dict.txt` 的词条，在 UWP 应用中无法读取；反之亦然。

#### 痛点 2：`.qdict` 二进制词库缓存无法被 UWP 直接复用
- **现状**：[`GetCacheFilePath()`](../src/engine/wubi_binary_dict.cpp) 优先查找词库文本文件同级目录的 `.qdict`（即 `C:\Program Files\QiQiIME\data\wubi86_jidian.qdict`）；若不存在，则退回到 `%LOCALAPPDATA%\QiQiIME\cache\wubi86_jidian.qdict`。
- **问题**：
  - 当前安装包只打包了 `.txt` 码表，未预生成 `data\*.qdict`；
  - 普通进程无权往 `C:\Program Files\QiQiIME\data\` 写入 `.qdict`，因此缓存生成在真实用户的 `%LOCALAPPDATA%\QiQiIME\cache\` 下；
  - 由于痛点 1 的隔离，每个 UWP 应用既没有现成的 `Program Files\...\*.qdict` 可映射，也读不到真实用户的 `%LOCALAPPDATA%\QiQiIME\cache\*.qdict`，只能在各自沙箱内各自重新解析 `.txt` 并编译一份私有 `.qdict`。

#### 痛点 3：开发目录注册（`scripts/register_ime.ps1`）缺少 AppContainer 读取权限
- **现状**：开发调试阶段直接从 `C:\sample\qiqiime\build\x64\Release\qiqiime.dll` 注册 COM 组件。
- **问题**：普通工作区目录默认没有 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 的 `RX` 权限，导致开发模式下 UWP 应用无法加载 `qiqiime.dll`（表现为在 UWP 应用中切到奇奇五笔无响应或回退英文）。

#### 痛点 4：基于 `FindWindowExW` 的 Win32 窗口广播受限于 UIPI 与窗口创建状态
- **现状**：`NotifyAllStatusBarWindowsConfigChanged` 通过 `FindWindowExW` 枚举顶层窗口并发送 `PostMessageW(hwnd, kConfigChangedBroadcastMsg, ...)`。
- **问题**：不仅未开启浮动状态条的进程收不到通知，而且 AppContainer 进程向普通 Medium Integrity 进程发送自定义窗口消息会被 UIPI（用户界面特权隔离）拦截。
- **正统替代**：改用 Windows TSF 原生的 **`ITfThreadMgr::GetGlobalCompartment()` + `ITfCompartmentEventSink`**（由系统 `ctfmon.exe` 跨完整性级别中转），彻底移除自定义 Win32 窗口广播与 UIPI 依赖。

---

## 3. 完整解决方案设计（Windows 官方正统架构）

遵循微软官方 TSF 输入法（如 Microsoft Wubi / Pinyin）在 Windows 10/11 下的安全最佳实践，**绝不使用“字符串截取路径硬穿 AppContainer 沙箱目录”或“放宽用户 `%LOCALAPPDATA%` 文件夹 ACL”的非标做法**，而是采用以下 4 个正统、零解耦依赖的子任务：

### 3.1 子任务 1 (`uwp-etw-tracelogging`)：基于 Windows ETW (`TraceLoggingProvider.h`) 的跨沙箱内核级诊断
- **原理**：使用 Windows SDK 原生无清单 ETW（`<TraceLoggingProvider.h>`）注册 `QiQiIME.TraceLogging` Provider。
- **优势**：
  1. **全沙箱免权限**：ETW 直接通过内核态句柄写入事件缓冲，在任何 UWP AppContainer、浏览器低完整性进程（Low Integrity）中 100% 正常工作，无需任何文件写入权限。
  2. **未开启时零开销**：`TraceLoggingWrite` 先检查内核共享页中的 `TraceLoggingProviderEnabled` 位，未开启收集时仅有一条 `cmp` 指令（0 纳秒开销，不执行格式化）。
  3. **统一汇总**：所有 Win32 与 UWP 进程的日志与 `[KeyTrace]` 按键诊断可通过 `wpr` / `PerfView` 或 `QiQiIMESettings.exe` 统一实时捕获。

### 3.2 子任务 2 (`uwp-prebuild-qdict-mmap`)：预编译 `data\*.qdict` 实现 AppContainer 零写入只读内存映射
- **原理**：
  - 在 `RegisterServerWithManagers` 注册阶段直接将 `data\wubi86_jidian.txt`、`wubi86_jidian_extra.txt`、`wubi86_haifeng.txt` 预编译为同目录下的 `.qdict` 二进制映像（`UnregisterServerWithManagers` 卸载时自动清理）。
  - `C:\Program Files` 天然具备 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 的只读映射权限，UWP AppContainer 进程通过 `GetModuleFileNameW(g_dll_instance)` 直接 `MapViewOfFile` 只读映射 `data\*.qdict`，无需向 `%LOCALAPPDATA%` 写任何缓存文件（关于多进程词库并发热替换与实时同步机制详见 [`docs/ARCHITECTURE.md`](./ARCHITECTURE.md) 2.6 与 2.8 节）。

### 3.3 子任务 3 (`uwp-hkcu-registry-config`)：通过 `HKCU\Software\QiQiIME` 注册表镜像向 UWP 沙箱共享配置与自造词
- **原理**：
  - 将 `QiQiConfig` 与用户自造词数据镜像存储在 `HKCU\Software\QiQiIME` 注册表项下，并为该注册表项设置 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 与 `S-1-15-2-2` 的只读（`KEY_READ`）DACL。
  - 普通桌面进程或 `QiQiIMESettings.exe` 保存配置/造词时，同时更新本地文件与 `HKCU\Software\QiQiIME`（并通过 TSF Global Compartment 推送变更通知）。
- **优势**：
  - UWP AppContainer 进程无需访问 `%LOCALAPPDATA%` 文件夹，直接通过 `RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\QiQiIME", 0, KEY_READ, ...)` 在微秒级（~5 μs）内读取最新配置与用户自造词。

### 3.4 子任务 4 (`uwp-dev-script-acl`)：开发注册脚本自动授予 AppContainer 读取执行权限
- 在 [`scripts/register_ime.ps1`](../scripts/register_ime.ps1) 中自动对构建产物目录及 `data\` 目录执行：
  ```powershell
  icacls $targetDir /grant "*S-1-15-2-1:(OI)(CI)(RX)" /Q
  ```
  使开发者无需打包安装 MSI 即可直接在 Windows 11 便笺、设置、UWP 应用中调试奇奇五笔。
