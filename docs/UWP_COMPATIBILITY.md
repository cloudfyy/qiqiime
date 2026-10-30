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

#### 痛点 4：跨进程窗口枚举与消息广播在 AppContainer 下的限制
- **现状**：`NotifyAllStatusBarWindowsConfigChanged` 通过 `FindWindowExW` 枚举顶层窗口并发送 `PostMessageW(hwnd, kConfigChangedBroadcastMsg, ...)`。
- **问题**：AppContainer 受到 UIPI（用户界面特权隔离）限制：
  - 高/中完整性（Medium Integrity，如独立配置程序 `QiQiIMESettings.exe`）向 AppContainer 窗口发送 `PostMessageW` 是允许的；
  - 但 AppContainer 进程向普通 Medium Integrity 进程发送自定义窗口消息可能被 UIPI 拦截（除非目标进程调用 `ChangeWindowMessageFilterEx` 放行，或由宿主在 `OnSetFocus` 时主动拉取最新 `config.ini` 时间戳）。

---

## 3. 完整解决方案设计（对应 Todo：`uwp-appcontainer-compatibility`）

为了在不引入任何额外后台服务的前提下，让 **Win32 传统应用、UWP 沙箱应用、以及独立配置程序 `QiQiIMESettings.exe`** 100% 共享同一份配置与内存映射词库，采用以下四步方案：

### 3.1 穿透 AppContainer 定位真实用户的 `%LOCALAPPDATA%\QiQiIME`
在 [`src/common/logger.cpp`](../src/common/logger.cpp) 的 `GetAppDataDirectory()` 中：
1. 若 `LOCALAPPDATA` 路径包含 `\AppData\Local\Packages\`（典型 AppContainer 虚拟化路径），截取 `\AppData\Local\Packages\` 之前的前缀，还原出真实用户的 `C:\Users\<User>\AppData\Local\QiQiIME`。
2. 备选兜底：若环境变量被清空，通过当前进程 Token 的用户 SID 或 `userenv.dll!GetBasicProfileFolderPath` 拼接 `AppData\Local\QiQiIME`。

### 3.2 为 `%LOCALAPPDATA%\QiQiIME` 授予 `ALL APPLICATION PACKAGES` 继承权限
当非沙箱进程（如 `regsvr32 /i:user`、`QiQiIMESettings.exe` 或任意普通桌面宿主进程）创建或打开 `%LOCALAPPDATA%\QiQiIME` 目录时，设置目录 DACL：
- 保留当前用户完全控制权限（`Full Control`）；
- 显式添加两条允许继承（`OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE`）的 ACE：
  - **`S-1-15-2-1` (`ALL APPLICATION PACKAGES`)**：授予读取/写入权限（允许读取 `config.ini`、映射 `cache\*.qdict`、追加 `user_dict.txt`）；
  - **`S-1-15-2-2` (`ALL RESTRICTED APPLICATION PACKAGES`)**：授予只读权限（`GENERIC_READ | GENERIC_EXECUTE`）。
- 也可通过 SDDL / `SetNamedSecurityInfoW` 在目录创建时一次性完成，幂等且开销极小。

### 3.3 在构建/安装阶段预编译或共享 `.qdict` 二进制词库
1. **方案 A（共享缓存目录）**：完成 3.1 + 3.2 后，所有 UWP 进程均可直接以只读方式 `OpenMappedFile` 映射 `%LOCALAPPDATA%\QiQiIME\cache\*.qdict`，全系统所有进程（Win32 + UWP）在内核物理内存中共享唯一一份词库页缓存。
2. **方案 B（构建期预编译 `.qdict` 进 `data/`，可选优化）**：在打包阶段直接生成 `data\wubi86_jidian.qdict`、`wubi86_jidian_extra.qdict`、`wubi86_haifeng.qdict` 随 MSI 安装到 `C:\Program Files\QiQiIME\data\`。这样连首次启动编译 `.qdict` 的 ~20ms 开销都彻底消除，且天然具备 `Program Files` 的只读安全属性。

### 3.4 开发注册脚本与消息过滤增强
1. **开发脚本赋权**：在 [`scripts/register_ime.ps1`](../scripts/register_ime.ps1) 中，自动对构建产物目录及 `data\` 目录执行：
   ```powershell
   icacls $targetDir /grant "*S-1-15-2-1:(OI)(CI)(RX)" /Q
   ```
   使开发者无需打包安装 MSI 即可直接在 Windows 11 便笺、设置、UWP 应用中调试奇奇五笔。
2. **UIPI 消息放行 + `OnSetFocus` 双重保障**：
   - 在创建 `StatusBarUI` 窗口后调用 `ChangeWindowMessageFilterEx(hwnd, kConfigChangedBroadcastMsg, MSGFLT_ALLOW, nullptr)`，允许低完整性进程发送配置同步广播；
   - 继续保留 `QiQiTextService::OnSetFocus` 中的 `ReloadConfigIfModified()` 时间戳检查——一旦切换焦点进入或离开 UWP 应用，毫秒级 `stat` 检查会立即同步最新配置。
