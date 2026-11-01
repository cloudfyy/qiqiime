# 奇奇五笔应用日志监控指南

本文说明如何监控输入法宿主及独立设置程序的日志。日志实现见 [logger.h](../src/common/logger.h) 和 [logger.cpp](../src/common/logger.cpp)，开发规范见 [DEVELOPMENT.md](DEVELOPMENT.md#52-结构化排障日志)。

计划中的独立实时日志监控工具已记录为[架构待办任务 56](ARCHITECTURE.md#p2轻量实时诊断工具)，尚未实现；其日志初始化与共享生命周期前置任务 55、51 已完成。目前请使用下述调试器或 ETW/WPA 流程。

## 1. 先了解日志在哪里

QiQiIME 使用 Windows ETW TraceLogging，不会自动生成文本日志文件，也没有注册“事件查看器 → Windows 日志 → 应用程序”中的专用事件源。

| 项目 | 值 |
|---|---|
| Provider 名称 | `QiQiIME.TraceLogging` |
| Provider GUID | `{8E7B9D31-5A42-4C81-9F11-202610290099}` |
| 事件负载 | `process`（进程名）、`message`（消息） |
| ETW 事件头 | 时间、级别、进程 ID、线程 ID |
| 调试输出 | 附加调试器时额外输出到 `OutputDebugString` |
| 离线采集文件 | 由采集工具生成的 `.etl`，不是输入法自动写入的 `.log` |

输入法 DLL 运行在使用它的应用进程内，不是独立的 `QiQiIME.exe`。排查浏览器、VS Code 或记事本时，需监控实际承载输入法的进程；多进程应用可能不是主窗口所属进程。独立设置程序的进程名是 `QiQiIMESettings.exe`。

### 初始化与共享生命周期

- IME 在服务构造入口、配置和词库初始化之前主动确保 Provider 注册，激活时再次幂等检查；独立设置程序使用 `InitLogger`。不依赖日志宏或注册状态查询触发初始化。
- 同一进程内的 IME 模块共享一次注册，同进程多个服务实例共用；不同进程的 Provider 状态、日志级别及锁互相独立，不使用跨进程日志引用计数或锁。多个宿主使用同一 Provider GUID，以 PID、线程 ID、时间区分事件。
- **ETW 采集不需要附加调试器**。可以先开始采集再启用输入法，也可以在宿主已经初始化后开始采集；只接收采集启用后的消息，不补发历史日志。
- 停用服务不注销 Provider，也不会重置共享日志级别；正常模块卸载后统一清理，要求模块调用者已经结束。`DllMain` 不初始化或格式化日志，清理不获取应用的初始化锁。
- 即使没有接收方或级别为 Off，正常初始化仍注册 Provider，以支持之后启用采集；没有接收方时跳过消息格式化，详细日志关闭或没有接收方时跳过焦点计时，不新增后台线程或日志文件。

任务 55、51 已由双架构真实 DLL、多进程、无调试器 ETW 采集验证。请先部署含修复的 DLL，再重启仍加载旧 DLL 的宿主；本指南不意味着旧版本也获得了修复。注册失败仍可能没有 ETW 事件，此时查看不递归的调试输出错误码，不能把没有日志当成没有错误。

## 2. 设置应用日志级别

应用级别和 ETW 会话级别是两道独立过滤：采集器选了 Verbose，也不能收到已被应用级别过滤掉的 Debug 消息。

配置存储在当前用户注册表 `HKCU\Software\QiQiIME` 的 DWORD 值 `LogLevel`。当前图形设置界面没有日志级别控件。

| LogLevel | 应用级别 | 建议用途 | ETW 事件 |
|---|---|---|---|
| `0` | Trace | 短时间详细复现 | `QiQiLogVerbose` |
| `1` | Debug | 常规开发排障 | `QiQiLogVerbose` |
| `2` | Info（默认） | 日常生命周期与状态消息 | `QiQiLogInfo` |
| `3` | Warn | 仅警告及更严重消息 | `QiQiLogWarning` |
| `4` | Error | 仅错误及严重错误 | `QiQiLogError` |
| `5` | Critical | 仅严重错误 | `QiQiLogError` |
| `6` | Off | 关闭常规日志 | 无 |

数值越小越详细。Trace 与 Debug 在 ETW 中都映射为 Verbose（级别 5）；Error 与 Critical 都映射为 Error（级别 2）。

1. 先打开奇奇五笔设置并保存一次，确保已建立有效配置；不要仅创建一个孤立的 `LogLevel` 值。
2. 在**被测用户自己的 PowerShell**中执行以下命令。管理员采集窗口如果属于另一个账号，不要在其中修改该账号的 HKCU。

   ```powershell
   $configKey = 'HKCU:\Software\QiQiIME'
   $config = Get-ItemProperty -Path $configKey -ErrorAction Stop
   if (-not $config.ConfigSequence) {
       throw '请先打开奇奇五笔设置并保存配置。'
   }
   $previousLogLevel = $config.PSObject.Properties['LogLevel']
   New-ItemProperty -Path $configKey -Name LogLevel -PropertyType DWord -Value 1 -Force -ErrorAction Stop
   ```

3. 保存工作后，关闭并重新启动被测应用或设置程序，使其读取新级别。手动修改注册表不会自动广播配置变更，不要假定仅切换输入法便能立即生效。
4. 排查结束后，在**同一 PowerShell 窗口**恢复之前的设置：

   ```powershell
   if ($null -ne $previousLogLevel) {
       Set-ItemProperty -Path $configKey -Name LogLevel -Value $previousLogLevel.Value -ErrorAction Stop
   } else {
       Remove-ItemProperty -Path $configKey -Name LogLevel -ErrorAction Stop
   }
   ```

   再次重启被测应用。如已关闭该 PowerShell 窗口且无法确认原值，可明确设置为 `2`（默认 Info），但这不是恢复原配置。

## 3. 实时查看：附加原生调试器

适合开发时查看单个宿主的操作顺序与 HRESULT，不需要生成日志文件。

1. 按第 2 节设置 Debug 或 Trace，并重新启动被测应用。
2. 打开 Visual Studio，选择 **调试 → 附加到进程**。
3. 按进程名和 PID 选择被测宿主；连接类型使用本地 Windows 调试，代码类型选择 **原生 / Native**。监控设置程序时选择 `QiQiIMESettings.exe`。
4. 打开 **视图 → 输出**，在“显示输出来源”中选择 **调试 / Debug**。
5. 返回被测输入框，切换到奇奇五笔，复现输入、失焦、停用或设置保存操作。
6. 查看形如 `[进程名] [级别] 消息` 的输出；完成后选择 **分离全部 / Detach All**，不要为了结束监控而终止用户的应用。

若未收到 IME 日志，检查是否附加到了真正承载输入法的进程；浏览器和 VS Code 通常存在多个同名进程。权限较高的宿主可能需要以管理员运行调试器。

**DebugView 单独打开不保证有效**：当前常规调试输出受 `IsDebuggerPresent()` 条件限制，仅打开 DebugView 不等于给目标进程附加调试器。日志基础设施自身的失败兜底与常规日志不同，不能据此判断普通日志通路已开启。

## 4. 保存复现过程：用 logman 采集 ETW

`logman.exe` 是 Windows 自带工具。建议使用管理员 PowerShell 启动采集，**被测应用仍按原来的普通用户权限运行**，尤其不要为了采集而提升浏览器权限。

本例只订阅 QiQiIME Provider，使用独立会话名和最大 64 MB 的环形文件；不订阅全系统键盘、网络或其他应用 Provider。

### 4.1 启动并确认采集

在管理员 PowerShell 中执行：

```powershell
$traceDirectory = Join-Path $env:TEMP 'QiQiIME-Traces'
New-Item -ItemType Directory -Path $traceDirectory -Force -ErrorAction Stop | Out-Null
$sessionName = 'QiQiIME-' + [guid]::NewGuid().ToString('N')
$traceFile = Join-Path $traceDirectory ($sessionName + '.etl')
$provider = '{8E7B9D31-5A42-4C81-9F11-202610290099}'

logman create trace $sessionName -p $provider 0xffffffffffffffff 5 -o $traceFile -f bincirc -max 64 -ets
if ($LASTEXITCODE -ne 0) {
    throw "启动 ETW 采集失败，logman 退出码：$LASTEXITCODE"
}
logman query $sessionName -ets
if ($LASTEXITCODE -ne 0) {
    throw "查询 ETW 会话失败，logman 退出码：$LASTEXITCODE；请使用下方停止命令清理本次会话。"
}
Write-Host "会话名：$sessionName"
Write-Host "输出文件：$traceFile"
```

- `5` 是 ETW Verbose，不是注册表中的应用 Critical；两者数值含义不同。
- `0xffffffffffffffff` 启用该 Provider 的全部关键字，不表示订阅所有 Provider。
- `-ets` 直接操作即时 ETW 会话，不建立持久化的数据收集器计划。
- 查询成功仅证明会话存在；是否实际收到应用事件，需要在复现后打开 ETL 检查。
- 环形文件达到上限后会覆盖较早事件，复现完成应及时停止。

### 4.2 复现并停止

1. 启动独立设置程序并操作设置，或按第 3 节附加 IME 宿主，然后在输入框复现问题。
2. 记录复现时间、应用版本、进程名/PID、x64 或 x86，以及具体操作步骤。
3. 完成后，在启动采集的同一管理员 PowerShell 中执行：

   ```powershell
   logman stop $sessionName -ets
   if ($LASTEXITCODE -ne 0) {
       throw "停止 ETW 采集失败，logman 退出码：$LASTEXITCODE"
   }
   Get-ChildItem -LiteralPath $traceDirectory -Filter "$sessionName*.etl"
   ```

4. 停止成功后再打开文件，确保缓冲区已刷新；实际文件名以查询信息和目录中的输出为准。

即使复现失败或提前结束，也应停止本次会话。若关闭了原 PowerShell 窗口，使用此前打印的完整会话名执行 `logman stop '本次会话名' -ets`；不要停止其他工具或用户的会话。即时会话不需要再执行 `logman delete`。

## 5. 在 WPA 中读取 ETL

安装 Windows ADK 中的 **Windows Performance Toolkit**，使用支持 TraceLogging 的 Windows 10/11 版本 **Windows Performance Analyzer（WPA）**。

1. 启动 WPA，选择 **File → Open**，打开第 4 节生成的 `.etl`。
2. 在 **Graph Explorer** 中展开 **System Activity**。
3. 双击 **Generic Events**，将事件表加入 **Analysis**。
4. 在 **Provider Name** 列找到 `QiQiIME.TraceLogging`，右键选择 **Filter to Selection**。
5. 展开事件，查看 `process`、`message`，结合进程 ID、线程 ID 和时间定位复现期间的操作。列布局随 WPA 版本不同，可在表格列选择器中添加相应字段。
6. 多个宿主同时使用输入法时，按 PID 进一步过滤；仅按 `Code.exe` 或浏览器进程名可能混入其他实例。

事件名分为 `QiQiLogVerbose`、`QiQiLogInfo`、`QiQiLogWarning`、`QiQiLogError`。查错误时先筛选 Warning/Error，再查看前后的 Info/Verbose，不要只保留最后一条失败消息。

ETW 采集不需要 PDB 才能显示这些自描述消息；调试调用栈或源码则可能需要匹配构建的符号文件。不要把未解码的 ETL 当成普通文本文件搜索。

## 6. 如何理解失败诊断

| 消息内容 | 含义 |
|---|---|
| `hr=0x80070005` | COM 操作返回 `E_ACCESSDENIED`，是实际 HRESULT |
| `error=5` | 支持错误码的 Win32/注册表操作返回访问被拒绝 |
| `expected=... read=... error=0` | API 返回成功，但实际字节数不足；不是陈旧的线程错误码 |
| `TS_E_NOLAYOUT` 或布局回退消息 | 宿主可能尚未完成布局；应结合后续事件判断，不一律视为输入失败 |
| `SetWindowSubclass failed` 等无错误码消息 | 对应 API 未约定可用的扩展错误码，不应补猜 `GetLastError` |
| C++ exception / logging failed | 异常或日志基础设施失败，没有可凭空填入的 HRESULT |

`S_FALSE`、`TF_S_ASYNC` 是成功状态，不应当成失败。上层“回退到 GDI”或“使用内存词库”等消息描述降级行为，原因可能在紧邻的底层错误事件中。

### 6.1 焦点处理耗时（任务 50）

1. 按第 2 节设为 Debug（`LogLevel=1`）或 Trace，并按第 3–5 节连接接收方；ETW 会话需允许 Verbose，确认宿主加载的是含任务 55 修复的 DLL。
2. 在真实宿主中反复切换输入框、切换前后台，再分别复现配置变化、词库文件变化及两者同时变化。
3. 在调试输出或 WPA 的 `message` 中筛选 `FocusPerformance(` 与 `ReloadConfigIfModified(focus_id=`。以事件头的 PID 和消息的 `id` / `focus_id` 关联一次处理；ID 在同一进程内递增，非焦点路径的重载记录使用 `focus_id=0`。

| 字段 | 解释 |
|---|---|
| `source` | `thread-manager` 为文档管理器焦点通知，`key-event` 为前台键盘焦点通知 |
| `has_focus`、`same_state` | 当前是否获得焦点、是否与该服务实例上一条已记录通知的布尔状态相同；状态相同不证明是同一个文档，也不自动认定通知可合并 |
| `reload_checked` | 本次获得焦点时进入检查路径；失去焦点不做配置/词库检查 |
| `reload_ns`、`position_ns`、`total_ns` | 配置/词库检查与重载、状态条位置同步、完整焦点处理耗时，均为单调时钟测量的整数纳秒；除以 1,000 得到微秒 |
| `position_changed` | 本次同步是否调用了位置更新；相同位置或没有可用的共享位置时为 `false` |
| `config_status` | `unchanged`、`loaded`、`load-failed`、`sequence-unavailable` 或尚未检查的 `not-checked` |
| `dictionary_modified`、`dictionary_reload_calls` | 是否检测到映射词库变化、进入词库重载函数的次数；仅配置变化通常为 1，仅词库变化为 1，两者同时变化保留现有两次调用并记录为 2 |
| `mapped_dicts`、`phrase_frequency_status` | 最终映射词库数量，以及词频加载的 `not-requested` / `loaded` / `load-failed` 状态；调用次数和数量不代表每个文件都加载成功，结合底层失败日志判断 |
| `completed` | 函数是否正常走完当前路径；`true` 不代表所有底层操作成功，失败仍看状态及实际错误码 |

无变化时仍记录检查耗时，失去焦点时两段耗时为 0，而总耗时包含组合/UI 清理。详细级别关闭或没有接收方时不读取计时时钟。测量包含该路径中的日志及同步开销，不是去除埋点开销的基准；配置热更新若降低日志级别，后续详细记录会被正常过滤。

双架构测试通过自有调试子进程捕获实际日志，覆盖上述变化组合、两类来源、相同/变化的位置、获得/失去焦点及 Info/Off 过滤；这不替代真实宿主测量。本项仅提供观测，不增加耗时阈值、不合并重载、不改变 TSF/UI 的线程或生命周期策略。

### 6.2 组合生命周期与停用清理（任务 43、44）

复用第 2–5 节的采集步骤，应用设为 Debug（`LogLevel=1`）或 Trace，ETW 会话允许 Verbose；筛选 `TsfLifecycle(` 和 `TsfCleanup(`。这些是现有 `message` 字符串中的字段，不是新增 ETW 负载。

- 按 PID、模块加载周期和 `service` 关联服务，进一步按 `cleanup` 关联一次旧组合清理；模块重载后编号可以重新开始。事件头 TID/时间用于判断回调发生在哪个线程、何时真正执行。
- `sync-result` / `async-result` 同时记录外层 `request_hr` 和内层 `session_hr`。`queued=true` 只表示异步请求获准排队；必须继续观察 `callback-enter`、`attribute-clear`、`end-composition` 和退出阶段。
- `callback-exit`、`finished=true` 或 `deactivate-local-complete` 都不表示宿主清理完整成功。属性清理和结束组合分别记录实际 HRESULT，失败为 Warn；`released` 只是独立状态开始析构。
- `host-terminated` 后出现 `skip-host-terminated` 是预期的重复处理屏障；`write-forced-async` 表明新写在等待哪一次旧清理。`stale-edit-skipped` 用快照/当前周期解释业务任务为什么被拒绝。
- 不记录输入文本、按键、候选、范围内容或裸 COM 地址。详细记录受 Info/Off 正常过滤，没有接收方时不格式化。

字段表、预期日志顺序、只排队不执行及“组合已结束但属性残留”等故障的判读，见[组合生命周期设计与排障](TSF_COMPOSITION_LIFECYCLE.md#7-log-protocol-and-troubleshooting)。不要只凭单条成功状态判断宿主组合已结束。

## 7. 无日志与共享日志的检查清单

| 现象 | 检查项 |
|---|---|
| 找不到文本日志/事件查看器条目 | 使用调试器或 ETW/WPA，本项目不自动写文本日志 |
| 有 Info，没有 Debug/Trace | 检查应用 `LogLevel`，并确认进程已读取新配置 |
| ETW 会话存在，但没有 IME 事件 | 确认 DLL 版本、宿主 PID、输入法是否已初始化、应用及采集级别；主动触发操作，注册失败时查看调试输出 |
| 设置程序有日志，浏览器没有 | Provider 注册按进程发生；核对浏览器的实际宿主 PID |
| 同进程停用后不再收到事件 | 停用的实例不再处理输入是正常现象；其他实例不应因此失去日志，核对是否使用旧 DLL、模块是否已卸载或级别变为 Off |
| 只有早期/晚期部分事件 | 检查环形文件是否覆盖，以及采集起止时间 |
| 启动或停止采集失败 | 查看 logman 退出码、管理员权限、会话名及输出目录权限 |
| ETL 有事件，但看不懂字段 | 使用支持 TraceLogging 的 WPA，展开 Generic Events 的负载列 |

只在复现时短期开启详细日志，结束后停止会话并恢复原级别。日志可能包含进程名、PID、窗口位置及部分路径等环境信息；分享前人工检查并脱敏，不提交用户日志、ETL 或包含敏感信息的调试输出到仓库。

## 8. 官方参考

- [logman create trace](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace)
- [logman start / stop](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-start-stop)
- [记录与查看 TraceLogging 事件](https://learn.microsoft.com/en-us/windows/win32/tracelogging/tracelogging-record-and-display-tracelogging-events)
- [Windows Performance Analyzer](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-analyzer)
