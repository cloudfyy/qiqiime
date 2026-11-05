# 奇奇五笔应用日志监控指南

本文说明如何监控输入法宿主及独立设置程序的日志。日志实现见 [logger.h](../src/common/logger.h) 和 [logger.cpp](../src/common/logger.cpp)，开发规范见 [dev.md](dev.md#52-结构化排障日志)。

独立 GUI 工具 `QiQiIMELogMonitor.exe` 已实现，日常复现可直接使用下方的实时监控流程；需要离线分析时仍可使用调试器或 ETW/WPA。

窗口实现按职责拆分：[window.cpp](../src/tools/log_monitor/window.cpp) 负责窗口生命周期与主消息分发，[ui.cpp](../src/tools/log_monitor/ui.cpp) 负责控件创建、布局、主题、语言与 DPI，[view.cpp](../src/tools/log_monitor/view.cpp) 负责筛选、列表选择、刷新及详情，[actions.cpp](../src/tools/log_monitor/actions.cpp) 负责快捷键、分隔条、命令、复制和导出。共享的私有 `Impl` 声明位于 [internal.h](../src/tools/log_monitor/internal.h)，公开接口仍为 [window.h](../src/tools/log_monitor/window.h)。

## GUI 实时监控

窗口使用“文件 / 编辑 / 查看 / 语言 / 帮助”菜单及纯图标工具条，不再放置独立操作按钮或语言下拉框。工具条从左到右为开始/停止、暂停/恢复滚动、清空、复制、导出、工具诊断/输入法日志；悬停显示当前语言的命令提示。采集、暂停及来源状态变化时同步更新菜单名称、图标和选中状态。“关于”位于帮助菜单，活动关联筛选同时可从查看菜单或列表右键菜单使用。菜单及工具条与词库管理器共用 [`ToolCommandUi`](../src/common/command_ui.h)，支持相同的主题、高对比度和 DPI 行为。

1. 构建 `QiQiIMELogMonitor`（例如 `cmake --build --preset x64-release --target QiQiIMELogMonitor`），运行 `build\x64\Release\QiQiIMELogMonitor.exe`；x86 构建位于对应的 `build\x86\Release`。标题、关于、文件版本资源及导出头共用项目版本。
2. 点击 **开始采集 / Start**。若提示 `StartTrace` 权限不足，只提升监控工具，或由管理员配置合适的 ETW 控制权限；不要提升被测宿主。错误在状态区保持显示，详细步骤和数值错误码见 **工具诊断 / Diagnostics**。
3. 在宿主中复现问题。工具仅订阅 `QiQiIME.TraceLogging`，不要求附加调试器；宿主可先于监控器启动，但不会补发历史事件。x64 工具也可接收 x86 宿主事件，反之亦然；工具架构不表示宿主架构。
4. 按进程名、精确 PID、实际 ETW 级别、**分类（Category / Keyword）**、**活动关联（ActivityId / RelatedActivityId）** 及消息文本筛选。进程名是可手动输入的动态下拉框，列出当前日志来源缓冲中已采集的进程名（忽略大小写去重），也支持手动输入子串。PID 与进程名联动：非空名称只匹配一个 PID 时自动显示并置灰；匹配多个 PID 时默认“全部 / All”，可从下拉框选择单个 PID；名称为空时列出全部已采集 PID，仍可单独筛选；无匹配 PID 时禁用下拉框。名称或 PID 选项不受级别、分类及消息筛选影响。新增、清空和缓冲淘汰会同步更新选项；已退出进程只要仍有缓冲记录就保留。手选 PID 若仍存在则保留，消失后退回全部或唯一 PID；单 PID 自动状态变为多 PID 时默认全部。**分类 / Category** 下拉框可筛选 `General`、`Lifecycle`、`Input`、`Composition`、`Dictionary`、`Sync`、`UI`、`Config`；列表的 **分类 / 阶段** 列展示分类、`Start/Stop` 阶段及简短活动 ID（如 `Composition:Start [12345678]`）。右键带有活动 ID 的记录可选择 **“按当前活动关联筛选 / Filter by selected activity”**（同时匹配该 `ActivityId` 及其 `RelatedActivityId` 衍生/父级事件），或选择 **“清除活动关联筛选 / Clear activity filter”**。切换日志来源重置名称/PID/分类/活动筛选，切换语言保留有效选择。应用侧已过滤的日志无法恢复，Trace/Debug 只能统一显示为 Verbose，Error/Critical 只能统一显示为 Error。
5. 选中单行或多行（支持 `Ctrl` / `Shift` 多选及 `Ctrl+A` 全选）可在底部查看完整详情（包含 `Category:Opcode` 及完整 `Activity` / `Related` GUID）；点击 **复制 / Copy**、列表右键菜单或按 `Ctrl+C` / `Ctrl+Insert` 可将选中日志项（含时间、进程、PID/TID、级别、分类、阶段、ActivityId、事件名与消息）复制到剪贴板。直接上下拖动列表与详情区之间的分隔条可调整两区高度，悬停显示上下调整光标；也可用 `Tab` 聚焦分隔条后按上下方向键调整，`Home` / `End` 切换到最小 / 最大详情高度。调整高度不改变日志级别筛选；Error、Verbose 等仍由 **级别 / Level** 下拉框选择。**暂停滚动 / Pause scroll** 只关闭自动滚动，采集、淘汰和显示刷新仍继续；**停止 / Stop** 保留已有记录。**清空 / Clear** 只清空当前来源，不重置累计丢失计数。
6. **导出 / Export** 保存点击时当前来源、当前筛选结果的快照。确认隐私提示后选择 `.log` 文件；格式为带版本/架构/来源头的 UTF-8 制表符分隔文本（列为 `TimeUTC`、`Process`、`PID`、`TID`、`Level`、`Category`、`Opcode`、`ActivityId`、`RelatedActivityId`、`Event`、`Message`），消息中的反斜杠、制表符、换行和 NUL 被转义。请检查进程名、路径和可能的输入内容并脱敏后分享。写入失败会报告错误，目标可能不完整，但不删除内存记录。

### 桌面集成与多显示器

- 窗口图标包含 16–256 像素的独立分辨率资源，随窗口 DPI 重新加载，不将小图标简单放大；界面不重复展示与标题栏、关于对话框相同的品牌大图标和副标题。
- 主题自动跟随 Windows，响应主题、系统颜色及设置变更；高对比度优先使用系统颜色与原生控件回退。没有独立的深浅色开关，也没有托盘或通知弹窗。
- **语言 / Language** 菜单可即时选择 **跟随系统、简体中文、English**，选项文案与设置程序共用（含 Auto / zh-CN / en-US 标识）。切换保留采集状态、筛选、选中记录和暂停滚动状态；仅影响当前窗口，不写入输入法配置。原始事件、自身技术诊断和导出格式不随界面翻译；Windows 原生对话框按钮仍可能使用系统语言。
- 支持 Per-Monitor V2 DPI；跨不同缩放比例的屏幕移动时更新字体、图标、列宽和布局。窄窗口自动分行显示工具栏及消息筛选，分隔条随窗口宽度伸缩。
- 初始窗口置于当前显示器工作区内，支持位于主屏左侧或上方的负坐标显示器。显示器移除、分辨率或任务栏工作区变化时，普通窗口重新适配可用工作区；最大化和最小化窗口的迁移由 Windows 管理。
- **关于 / About** 与设置程序共用 [`common/about_dialog`](https://github.com/cloudfyy/qiqiime/blob/main/src/common/about_dialog.h) 实现，展示相同的名称、版本（含提交标识）、架构、许可说明及 GitHub 按钮，并使用日志监控工具图标。对话框相对主窗口居中并限制在所在显示器工作区内；ETW 采集与级别合并说明见上方实时监控流程。

### 缓冲和监控器自身日志

- 进程名/PID 下拉框展开时暂缓替换选项，避免用户选择中途发生变化；收起后补偿刷新，即使之后没有新日志也会更新。

- 全选使用一次批量选择和详情刷新，避免逐行通知反复格式化全部日志。复制工具项、`Ctrl+C` 与 `Ctrl+Insert` 复制相同的完整选中记录，不受详情控件显示长度限制。采集期间新增日志不自动加入已有选择；需要包含新日志时再次全选。

- 后台线程负责 `ProcessTrace` 与 TDH 解码；GUI 每 100 ms 批量接收。主缓冲上限为 **10,000 条 / 16 MiB**，待处理队列为 **2,000 条 / 4 MiB**，任一上限到达时淘汰最旧记录；单条超过容量也会计入本地淘汰。字节预算保守计入事件及字符串容量，不包含控件、分配器和快照的全部进程内存，因此不是进程工作集上限。
- 状态区分别显示本地淘汰、ETW `EventsLost`、ETW `RealTimeBuffersLost`、TDH 解码失败和内存/诊断异常计数。没有日志不等于没有故障，应同时检查采集状态、宿主 PID、应用日志级别和工具诊断。
- 监控器使用独立 Provider **`QiQiIME.LogMonitor`**，GUID 为 `{6AB897B4-D148-4DF9-8EB7-4790A274951E}`，不初始化或复用 IME 的 Provider。它只记录启动、会话生命周期、API 错误、汇总解码错误和导出结果；不逐条重发收到的消息，不形成采集反馈循环。
- 工具诊断有独立的有界视图和导出来源：显示缓冲及待处理队列分别限制为 **512 条 / 1 MiB**。关键错误即使没有其他 ETW 接收器也能在 GUI 中看到；底层诊断格式化/分配失败另累计计数并使用调试输出兜底。默认不写文本日志、不常驻、不上传数据。
- 每个窗口创建带 PID 的唯一会话，只停止自身会话；停止失败保留所有权供重试，窗口不会将失败当成成功关闭。异常终止可能留下会话，可根据工具启动诊断中记录的精确会话名清理，不要停止其他采集工具的会话。
- 初版是 Win32/D2D 混合 GUI，没有 CLI 或 ETL 导入功能。原生虚拟列表承担选择、滚动和键盘交互；按钮、字体、主题及 DPI 帮助代码与设置程序共用，高对比度或 D2D 不可用时按钮走原生绘制。

### 候选窗口定位诊断

候选不跟随滚动或跳到左上角时，导出异常前后完整的 All/Verbose 日志，按 PID/TID/service/activation/context 关联 `CandidatePosition`。`layout-advise` 报告布局订阅，`layout-notification` 报告宿主通知与目标状态，`layout-request` 和 `layout-callback` 区分请求及实际执行；`query-result` 包含定位成功状态、宿主返回的矩形、裁剪标志及是否已有有效位置。`fallback` 表示 GUI/鼠标定位回退，`position-unchanged` 表示没有取得新位置。

`CandidatePositionPopup` 记录传入矩形、屏幕边界约束后的目标位置、DPI 和实际窗口矩形；这些诊断不记录输入文字或裸 COM 地址。没有布局通知不能单凭候选停留推断旧回调复活；成功返回的零坐标也需结合宿主和屏幕位置判断，不直接认定是输入法或宿主的问题。

布局回调保留宿主传入的 `ITfContextView`，在编辑会话内查询该 view；空 view 和普通输入更新仍通过 `GetActiveView` 查询。`query` 的 `notification_view` 标识查询来源；异步回调在上下文失效后不再查询旧 view。如果滚动时通知到达而该 view 持续成功返回相同矩形，应记录为坐标未更新，不能仅凭 API 成功判定真实宿主定位通过。

### FSM 状态转移诊断

`FSM transition` 在运行时处理器返回且最终状态统一应用后记录：`state_before` 是执行前状态，`op` 是静态表选择的操作，`state_after` 是处理完成后的真实状态。静态表只决定操作和吞键策略，不再提供预设目标状态，因此不再记录 `planned_state` / `actual_state` 两套结果；数字定义见 [types.h](../src/ime/fsm/types.h)。

例如四码唯一候选自动上屏时，可能记录：

```text
FSM transition: state_before=4 -> op=2 state_after=1
```

表示从三码组合态追加第四码，运行时查词确认唯一候选并自动提交，最终回到空闲态。关闭自动上屏则停留在四码组合态；禁止五码顶字或选词序号越界会保留原状态，空码清除及临时英文引导符退格取消会返回空闲态。这些条件分支都由处理器显式返回最终状态。

该日志使用 Trace 级别，在 ETW 中显示为 Verbose；调试器按 Trace 阈值输出。透传操作不记录转移，日志不包含虚拟键、编码、候选或提交文字。`state_after` 只表示引擎处理完成，不证明后续 TSF 编辑会话已执行或宿主已接受提交。若处理器异常退出，公共执行入口清空组合以避免编码与状态不一致，记录 Error 并继续传播原异常，不产生正常完成的转移日志。

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

ETW 写入失败通过不递归的 `OutputDebugStringA` 输出 `EventWriteTransfer failed (error=实际 Win32 错误码)`，不包含事件正文，并保留调用线程的 `GetLastError`。该诊断不再次写入 ETW；采集器的丢失计数仍需单独观察。错误码 8 表示 `ERROR_NOT_ENOUGH_MEMORY`，不能仅据此认定是采集缓冲配置或应用内存泄漏。

输入法 DLL 运行在使用它的应用进程内，不是独立的 `QiQiIME.exe`。排查浏览器、VS Code 或记事本时，需监控实际承载输入法的进程；多进程应用可能不是主窗口所属进程。独立设置程序的进程名是 `QiQiIMESettings.exe`。

### 初始化与共享生命周期

- IME 在服务构造入口、配置和词库初始化之前主动确保 Provider 注册，激活时再次幂等检查；独立设置程序使用 `InitLogger`。不依赖日志宏或注册状态查询触发初始化。
- 同一进程内的 IME 模块共享一次注册，同进程多个服务实例共用；不同进程的 Provider 状态、日志级别及锁互相独立，不使用跨进程日志引用计数或锁。多个宿主使用同一 Provider GUID，以 PID、线程 ID、时间区分事件。
- **ETW 采集不需要附加调试器**。可以先开始采集再启用输入法，也可以在宿主已经初始化后开始采集；只接收采集启用后的消息，不补发历史日志。
- 停用服务不注销 Provider，也不会重置共享日志级别；正常模块卸载后统一清理，要求模块调用者已经结束。`DllMain` 不初始化或格式化日志，清理不获取应用的初始化锁。
- 即使没有接收方或级别为 Off，正常初始化仍注册 Provider，以支持之后启用采集；没有接收方时跳过消息格式化，详细日志关闭或没有接收方时跳过焦点计时，不新增后台线程或日志文件。

任务 55、51 已由双架构真实 DLL、多进程、无调试器 ETW 采集验证。请先部署含修复的 DLL，再重启仍加载旧 DLL 的宿主；本指南不意味着旧版本也获得了修复。注册失败仍可能没有 ETW 事件，此时查看不递归的调试输出错误码，不能把没有日志当成没有错误。

## 2. 设置应用日志级别

当 ETW 会话（如 `QiQiIMELogMonitor.exe` 或 `logman`）启用 `QiQiIME.TraceLogging` Provider 时，只要应用未配置为 `Off`（`LogLevel=6`），输入法会自动按当前 ETW 会话请求的级别（例如 `TRACE_LEVEL_VERBOSE`）放行并输出对应级别的 ETW 事件。因此**使用 `QiQiIMELogMonitor.exe` 采集 `All` / `Verbose` 日志时，无需手工修改注册表 `LogLevel` 或重启被测应用**。

注册表 `HKCU\Software\QiQiIME` 下的 DWORD 值 `LogLevel` 主要用于：
1. 控制仅附加原生调试器（无 ETW 会话请求该级别）时的 `OutputDebugString` 输出阈值；
2. 当设为 `6`（`Off`）时，显式关闭全部 ETW 与调试器常规日志输出。

当前图形设置界面没有日志级别控件。

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

### Shift 切换无响应或图标不更新

1. 使用包含诊断代码的新 DLL（若刚更新 DLL，保存工作并重启被测应用以加载新 DLL）。在 `QiQiIMELogMonitor.exe` 中将级别筛选保持为 **全部 / All** 或 **详细 / Verbose** 并点击 **开始采集 / Start**（ETW 会话会自动启用 Verbose 级别事件，无需手工修改注册表 `LogLevel`，除非此前将 `LogLevel` 设为了 `6` 即 `Off`）。
2. 开始采集，聚焦可编辑区域，分别轻按左、右 Shift 各数次，记录无效操作的大致时间；同时测试点击任务栏中英文图标。先不要仅筛选 `Input mode toggled`，以免遗漏失败链路。
3. 导出这一时段的全部日志，按 PID/TID 和 `ShiftTap(service=…, attempt=…)` 关联同一服务内的操作。`attempt` 在非自动重复的 Shift 按下回调建立新候选时递增，不代表系统收到的所有物理按键。
   - 正常链路为 `OnTestKeyDown / armed` 或 `OnKeyDown / armed` → 可选的 `OnTestKeyUp / accepted` → `OnKeyUp / accepted` → `toggle-complete`。两种按下入口共用跟踪逻辑且不吞键；测试和实际回调同时到达时以 `press-preserved` 保留原始计时，不重复建立候选。宿主省略测试回调或实际按下回调本身不是故障。
   - `disabled` 表示配置关闭 Shift 切换；`ctrl`、`alt`、`mouse-button` 表示组合操作；`timeout` 表示超过 **350 ms**；`repeat-timeout` / `repeat-mouse-button` 表示自动重复期间失效。
   - `other-key-cancelled` 表示期间出现其他按键（不记录键值）；`context-reset-cancelled` 表示上下文清理取消候选；`inactive`、`resetting`、`preparing`、`keyboard-disabled` 或 `no-context` 说明相关入口状态。`no-candidate` 应结合前面的取消记录分析。
   - `ShiftTapContext` 额外报告两种按下入口的 `keyboard_disabled`，不改变现有 Shift 分支的行为。Shift 跟踪不绑定上下文，空上下文仍可切换；未激活、正在清理或准备上下文时不建立候选。每条 `ShiftTap` 包含候选状态、按住时长、配置、上下文与修饰键布尔状态，不包含输入文字、普通按键值或完整键盘状态。
4. 已出现 `toggle-complete` 但图标不变时，检查 `SyncTsfCompartments` 的服务/操作编号、组件是否存在、`openclose` / `conversion` 写入的真实 HRESULT，以及 `LangBarButton::UpdateState`、`OnUpdate`、`GetIcon` / `GetText`。`sink=false` 表示当时没有语言栏通知订阅；通知成功只说明 API 接受，不保证系统已经重绘。语言栏记录按 PID/TID 和相邻事件关联，不含服务编号。
5. 确认新版、Debug 级别、采集及 PID 均正确后，若无效操作附近连 `OnTestKeyDown` 都没有，只能说明这些操作未被本服务的该回调记录，不能仅凭缺失日志断定 Windows 丢键。采集结束恢复原日志级别。

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

### 6.2 组合生命周期与停用清理（任务 43、44、45）

复用第 2–5 节的采集步骤，应用设为 Debug（`LogLevel=1`）或 Trace，ETW 会话允许 Verbose；筛选 `TsfLifecycle(` 和 `TsfCleanup(`。这些是现有 `message` 字符串中的字段，不是新增 ETW 负载。

- 按 PID、模块加载周期和 `service` 关联服务，进一步按 `cleanup` 关联一次旧组合清理；模块重载后编号可以重新开始。事件头 TID/时间用于判断回调发生在哪个线程、何时真正执行。
- `sync-result` / `async-result` 同时记录外层 `request_hr` 和内层 `session_hr`。`queued=true` 只表示异步请求获准排队；必须继续观察 `callback-enter`、`attribute-clear`、`end-composition` 和退出阶段。
- `callback-exit`、`finished=true` 或 `deactivate-local-complete` 都不表示宿主清理完整成功。属性清理和结束组合分别记录实际 HRESULT，失败为 Warn；`released` 只是独立状态开始析构。
- `host-terminated` 后出现 `skip-host-terminated` 是预期的重复处理屏障；`write-forced-async` 表明新写在等待哪一次旧清理。`stale-edit-skipped` 用快照/当前周期解释业务任务为什么被拒绝。
- 不记录输入文本、按键、候选、范围内容或裸 COM 地址。详细记录受 Info/Off 正常过滤，没有接收方时不格式化。

字段表、预期日志顺序、只排队不执行及“组合已结束但属性残留”等故障的判读，见[组合生命周期设计与排障](composition.md#7-log-protocol-and-troubleshooting)。不要只凭单条成功状态判断宿主组合已结束。

任务 45 增加 `document-target`（`source`、`retained`、`covered_by_modal`）、`document-uninit`（`related`）、`context-push`（`related`）及 `context-pop`（`current`）。`context-reset` 的 `try_host_cleanup=false` 表示目标移除后的本地清理，不代表调用了 `EndComposition`；排队清理收到 `context-removed` 后应跳过，记录 `skip-removed-context` 或 `skip-end-after-context-removal`。字段含义和重入边界见[文档与上下文通知](composition.md#8-document-and-context-notifications)。

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
