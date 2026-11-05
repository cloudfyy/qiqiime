# 奇奇五笔输入法 (QiQiIME) 架构与设计说明

## 1. 架构分层

```text
+-----------------------------------------------------------------------+
| 宿主应用 (Win32 Edit / Chromium / Electron / Office / 全屏游戏)        |
+-----------------------------------------------------------------------+
                                   ▲
                                   │ TSF COM 接口 (msctf.dll)
                                   ▼
+-----------------------------------------------------------------------+
| TSF 前端与 UI 交互层 (qiqiime.dll)                                    |
|  - QiQiTextService  : ITfTextInputProcessorEx, ITfThreadMgrEventSink  |
|                     , ITfKeyEventSink, ITfCompositionSink             |
|                     , ITfFunctionProvider, ITfFnConfigure             |
|                     , ITfDisplayAttributeProvider                     |
|  - LambdaEditSession: ITfEditSession                                  |
|  - CandidateUI      : ITfCandidateListUIElement + 无焦点候选弹窗      |
|  - LangBarButton    : ITfLangBarItemButton + ITfSource (托盘按钮/菜单)|
|  - StatusBarUI      : 4x4 超采样 ARGB 抗锯齿浮动状态条 + Tooltip      |
|  - Settings / About : 属性设置窗口 + 关于奇奇五笔窗口 (RAII 托管)     |
+-----------------------------------------------------------------------+
                                   ▲
                                   │ 纯 C++ 数据结构 (KeyProcessResult)
                                   ▼
+-----------------------------------------------------------------------+
| 五笔核心引擎与基础层 (qiqi_engine.lib)                                |
|  - WubiEngine       : 静态分派 + 运行时 FSM、四码上屏、空码清屏、顶字 |
|  - WubiMappedDict   : 4 字节紧凑编码索引 + 只读按需分页内存映射 (.qdict)|
|  - Common           : HKCU 注册表事务配置、ETW TraceLogging 日志、ScopedHandle RAII |
+-----------------------------------------------------------------------+
```

之所以将 `qiqi_engine` 拆分为独立的静态库：
1. **无 COM 依赖即可测试**：`qiqi_engine_test.exe` 可以直接验证所有按键与五笔上屏规则，无需先通过 `regsvr32` 注入宿主进程。
2. **宿主安全**：输入法 DLL 会被注入到资源管理器、浏览器、Office 等进程中，引擎层保持纯内存/只读映射计算，避免阻塞宿主 UI 线程。

源码按 [`src/common`](../src/common)、[`src/ime`](../src/ime)、[`src/tools/settings`](../src/tools/settings)、[`src/tools/log_monitor`](../src/tools/log_monitor)、[`src/tools/dict_manager`](../src/tools/dict_manager) 分层，测试采用对应布局。公共 D2D 控件、窗口辅助（[`window.h`](../src/common/window.h)）、菜单及工具条（[`command_ui.h`](../src/common/command_ui.h)）与关于窗口由 `qiqi_common` 统一编译；设置窗口由 `qiqi_settings_ui` 统一编译，供独立设置程序和 IME 的进程内设置回退路径复用。每个模块及对应测试目录均有自己的 `CMakeLists.txt`，顶层文件只负责组织，新增工具使用独立的 `tools/<工具名>` 子目录。

---

## 2. 关键子系统与安全设计

### 2.1 注册阶段（`src/ime/tsf/register.cpp`）
- **COM InProcServer32（支持 `x64` + `x86` WOW64 双视图）**：注册 `kClsidQiQiTextService`，线程模型为 `Apartment`（STA）。在 64 位 Windows 上分别通过 `System32\regsvr32.exe` 与 `SysWOW64\regsvr32.exe` 注册 `x64` 与 `x86` DLL，其中 WOW64 进程自动检测已存在的 64 位注册项并避免覆盖全局共享的 TSF Language Profile。
- **Language Profile**：通过 `ITfInputProcessorProfiles::AddLanguageProfile` 注册到 `zh-CN` (`0x0804`)。
- **Categories（`ITfCategoryMgr`）**：
  - `GUID_TFCAT_TIP_KEYBOARD`：声明为键盘输入法。
  - `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`：声明支持 `ITfUIElementMgr`，使支持 TSF UILess 的全屏游戏能够激活输入法并接管候选框渲染。
  - `GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER`：声明提供组合串虚线下划线显示属性。
  - `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` / `GUID_TFCAT_TIPCAP_COMLESS`：声明现代应用及无需 COM 激活的能力，不代表所有宿主已验收。
  - 不再声明 `GUID_TFCAT_TIPCAP_SECUREMODE`。注册及卸载时检查并撤销旧版遗留分类，包括保留原生 Profile 的 WOW64 路径；查询或撤销失败保留 HRESULT 并返回失败，卸载仍继续其余清理。安全桌面支持由任务 61 单独评估。

### 2.2 激活、生命周期与模块职责拆分（`src/ime/tsf/`）
- `QiQiTextService` 的实现按职责拆分为四个文件：
  - [`tip_service.cpp`](../src/ime/tsf/tip_service.cpp)：实例构造/析构、`ActivateEx`/`Deactivate`、TSF 事件接收器注册、`ITfThreadMgrEventSink`、`ITfFunctionProvider` 与 `ITfDisplayAttributeProvider`。
  - [`tip_keys.cpp`](../src/ime/tsf/tip_keys.cpp)：`ITfKeyEventSink` 按键测试与处理、保留快捷键（`OnPreservedKey`）、`Shift` 轻按中英切换防误触、`Caps Lock` 处理与按键诊断日志。
  - [`tip_composition.cpp`](../src/ime/tsf/tip_composition.cpp)：`ITfCompositionSink`、`ITfTextLayoutSink`、编辑会话（`LambdaEditSession`）内的组合串创建/更新/提交/取消，以及候选框位置跟随与 `ITfUIElementMgr` 生命周期。
  - [`tip_config.cpp`](../src/ime/tsf/tip_config.cpp)：词库文件加载、TSF Global/System Compartment 同步、语言栏菜单动作分派、`ITfFnConfigure::Show` 设置启动与配置热重载。
- 实现 `ITfTextInputProcessorEx::ActivateEx`，记录宿主进程名、`client_id` 与 `dwFlags`（包含是否处于 `TF_TMAE_UIELEMENTENABLEDONLY` UILess 模式）。
- 挂载 `ITfThreadMgrEventSink`、`ITfKeyEventSink`、`ITfFunctionProvider` 与 `ITfLangBarItemButton`，并注册保留热键（`Ctrl+Space` 中英切换、`Shift+Space` 全半角切换、`Ctrl+.` 中英标点切换）。

### 2.3 按键处理与编辑会话（`OnTestKeyDown` -> `OnKeyDown` -> `RequestEditSession`）
TSF 的按键处理分为两步：
1. `OnTestKeyDown`：仅询问输入法**是否打算消费该按键**。当前 context 使用 `engine_.ShouldEatKey(...)`；不同 context 使用只读的 `ShouldEatKeyAfterReset(...)`，按实际目标切换后将采用的状态预测，不把旧组合的选词/退格语义带到新输入框。空 context、未激活或重入生命周期转换不消费。预判不绑定 context、不清理宿主组合、不申请编辑会话，也不推进 FSM；原有 Shift 轻触跟踪独立保留。
2. `OnKeyDown`：Shift 与 `OnTestKeyDown` 共用轻触跟踪，不吞按下事件、不绑定上下文；宿主省略测试回调时仍可建立候选，同时收到两个回调时保留原始计时。其他按键先取消 Shift 候选，再通过 `PrepareInputContext` 准备目标、必要时重置旧引擎并失效旧周期，再推进 `WubiEngine` 的静态分派与运行时有限状态机（[`ime/fsm/fsm.h`](../src/ime/fsm/fsm.h) 为聚合入口；状态/事件定义、按键映射、分派规则和分页辅助分别位于同目录的 `types.h`、`key_mapping.h`、`transitions.h`、`pagination.h`），得到 `KeyProcessResult`（`UpdateState` / `Commit` / `Cancel`），触发音效（若启用了按键音或空码提示音），然后通过 `ITfContext::RequestEditSession` 提交 `LambdaEditSession`。预判成功不保证编辑权限，实际回调仍复核状态。

编译期生成的 7 状态 × 18 按键事件表只选择 `TransitionOp` 和吞键策略，不包含预设目标状态。运行时处理器根据词库和配置返回 `TransitionOutcome { next_state, result }`，公共执行入口统一应用最终状态；处理器只修改组合数据，不直接赋值 `state_` 或调用生命周期 `Reset`。四码唯一上屏、空码清除、五码顶字开关及无效选词等分支显式返回真实终态，不额外查词预测终态。若执行异常，RAII 清理复位组合、记录错误并继续传播原异常。`ShouldEatKey` / `ShouldEatKeyAfterReset` 仍只读分类和静态分派，不查词、不执行状态转移；引擎完成不等于宿主编辑会话已完成。

上下文切换、失焦、移除和停用是 FSM 外部的生命周期事件，不加入现有 7 状态 × 18 按键事件表。`Reset` 与只读预判共用 `StateAfterCompositionReset`：中文组合态（含临时英文）回到 `Idle`，`English` 保持不变；全半角及标点模式不被目标切换改写。旧宿主组合的同步/异步清理独立于新目标的引擎状态，不能用“仍有待清理宿主组合”推断 FSM 仍在组合态。

候选鼠标点击直接调用 `WubiEngine::CommitCandidate(index_in_page)`，不模拟数字按键；普通五笔的数字选词复用同一内部提交处理器，两条路径都只经公共执行入口应用一次最终状态，并保留动态词频调整。临时英文模式下，键盘数字仍追加到正文，点击候选则提交去掉引导 `z` 的正文（只有引导时提交 `z`），不追加候选序号，也不参与五笔词频调整。

Shift 轻触由独立的 [`ShiftTapTracker`](../src/ime/tsf/shift_tap.h) 管理候选标记、原始按下时间和诊断序号，不加入五笔 FSM。跟踪器只使用调用方传入的时刻与 Ctrl/Alt/鼠标状态，TSF 层负责读取系统状态和生命周期门禁；测试松键与实际松键共用判定入口，预判通过不消耗候选，实际松键先复位再执行模式切换。阈值仍为 350 ms（等于阈值有效），双按下回调和自动重复不延长原计时，自动重复不能独立建立候选。拒绝或超时可保留原时间供诊断，实际松键及生命周期重置清除计时但保留序号。

在 `DoEditSession(TfEditCookie ec)` 内完成：
- **新建或更新组合串**：`ITfInsertAtSelection::InsertTextAtSelection` + `ITfContextComposition::StartComposition` + `ITfRange::SetText` + 设置 `GUID_PROP_ATTRIBUTE` 下划线显示属性。
- **上屏并结束组合**：清除显示属性、将范围文本替换为最终汉字、折叠选区到末尾、调用 `ITfComposition::EndComposition`。
- **查询光标坐标**：通过 `ITfContextView::GetTextExt` 获取组合范围的屏幕矩形，移动候选框并做多显示器工作区边界裁剪。

### 2.4 异常处理与宿主兼容（`OnCompositionTerminated` / `OnSetFocus`）
宿主终止组合、焦点或文档目标变化时，可能通过以下接口通知服务；具体回调取决于宿主，不能假定每次鼠标点击或 DOM 重绘都会触发相同通知：
- `ITfCompositionSink::OnCompositionTerminated`
- `ITfThreadMgrEventSink::OnSetFocus`

当前组合被宿主终止、实际输入目标变化或失焦时，`QiQiTextService` 按生命周期规则重置引擎与本地 UI，并在适用时安排旧宿主组合清理；本地清理完成不等于宿主异步清理完成。同文档的模态上下文入栈和无关通知不应直接清掉主组合，详见[生命周期契约](composition.md)。配置保存在 `HKCU\Software\QiQiIME`；设置程序保存后递增注册表配置序列并发送 TSF Global Compartment 通知，实例收到通知或 `OnSetFocus` 时按需检查和重载，通知失败另行诊断。

### 2.5 桌面弹窗与游戏 UILess 双模式（`src/ime/ui/candidate_ui.cpp`）
首次注册候选 UI 时调用 `ITfUIElementMgr::BeginUIElement(candidate_ui_, &show_own_ui, &id)`，后续通过 `UpdateUIElement` 更新：
- 若宿主返回 `show_own_ui == TRUE`，按展示开关显示自带的 `WS_EX_TOPMOST | WS_EX_NOACTIVATE` 候选窗口。
- 若宿主返回 `show_own_ui == FALSE`，不显示自带弹窗，向宿主提供 `ITfCandidateListUIElement` 接口及更新通知。这是宿主接管 UI 的机制，不仅限于游戏，也不代表所有全屏游戏已兼容；实际验证范围见[宿主测试记录](hosts.md)。

### 2.6 内存映射词库与最小权限安全模型（`src/ime/engine/binary_dict.cpp` & `src/common/file_util.cpp`）
- **映射内容与隐私边界**：`.qdict` 包含所编译词库的索引（`DictBinaryHeader` + `DictCodeIndex` + `DictWordSlice` + `wchar_t` 字符池），不保存逐键输入状态；但用户词库生成的 `.qdict` 会包含用户词条，不能当作不含隐私的数据。映射对象未命名（`lpName == nullptr`），不等于绕过文件权限或加密内容。
- **只读访问与文件替换（`FILE_SHARE_READ | FILE_SHARE_DELETE`）**：
  - 映射读取 `.qdict` 时，仅申请 `GENERIC_READ`，共享模式不允许普通并发原地写入，同时允许兼容的目录项替换。`MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 用于替换缓存文件；这些机制不保证消除存储故障或所有 `STATUS_IN_PAGE_ERROR`。
  - 已建立的映射保留旧文件视图；焦点检查通过 `HasModifiedMappedDictionary()` 检测源文件大小/时间戳变化后尝试重载，不保证任意文件变更立即生效。打开文件还使用 `SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS` 与 `FILE_TYPE_DISK` 校验，限制非磁盘文件误用；这不是对所有攻击或宿主条件的安全保证。
  - 生成 `.qdict` 缓存时，通过 `WriteFileAtomically` 以 `GENERIC_WRITE` + `dwShareMode = 0`（独占写）写入同目录临时文件 `.tmp.<pid>`（带 `gsl::finally` 异常/错误自动清理），完成后通过 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换目标文件。用户配置通过注册表事务保存，不写入 INI 文件。

### 2.7 全链路 RAII、多显示器 DPI 与矢量抗锯齿 UI（`src/common/` & `src/ime/ui/`）
- **类型化 RAII（`ScopedHandle<Traits>`）**：使用移动语义包装器管理拥有的 Win32 内核句柄、KTM 注册表事务、文件映射视图、注册表键、GDI 对象、菜单、窗口与动态库；借用句柄和父窗口拥有的子控件遵循各自的所有权规则。`ScopedKernelTransaction` 未提交时在析构中回滚并关闭句柄。配置及用户词库注册表镜像分别以事务保存；用户词表追加、缓存替换和镜像保存不是一个跨文件/注册表事务，不能保证同时成功。
- **可复用 D2D 控件（[`d2d_controls.h`](../src/common/d2d_controls.h) / [`d2d_controls.cpp`](../src/common/d2d_controls.cpp)）**：头文件仅提供声明，实现集中管理 Fluent 卡片、主次按钮、开关、单选框与下拉框的绘制和子类化；设置页及关于页复用同一模块，不在控件层依赖业务控件 ID 或父窗口的 `GWLP_USERDATA` 类型。每个窗口组合一个 `FluentD2DThemeContext`，通过现有 RAII 包装器持有 D2D/DWrite/GDI 资源；控件创建失败时自动回收尚未移交的窗口并记录日志，成功后由父窗口销毁子控件。下拉框借用的上下文须保持地址稳定，且存活到子控件销毁之后；单选组由调用方通过 `MAKELONG(first_id, last_id)` 指定连续 ID 范围。父窗口将 `WM_DRAWITEM` 分派给相应绘制函数，主题变更时调用 `RefreshThemeBrushes()`；卡片绘制在调用方的 `ScopedD2DDcDraw` 会话内进行，其余控件自行管理绘制会话。
- **多显示器与 Per-Monitor DPI（`monitor_dpi.h`）**：候选窗、浮动状态栏、Tooltip、设置及关于窗口复用显示器、DPI 与工作区帮助代码，按各自的窗口消息更新字体、布局和边界，支持负坐标显示器。实现这些分支不等于所有缩放、热插拔或宿主组合都已实测；保留具体测试范围与未覆盖项。
- **4x4 超采样抗锯齿状态条图标与模块拆分（[`statusbar_icons.cpp`](../src/ime/ui/statusbar_icons.cpp) / [`statusbar_tooltip.cpp`](../src/ime/ui/statusbar_tooltip.cpp) / [`statusbar.cpp`](../src/ime/ui/statusbar.cpp)）**：使用有向距离场（SDF）+ 4x4 超采样实时生成 32 位预乘 ARGB `DIBSection` 位图，通过 `GdiAlphaBlend` 渲染半角月牙、全角圆环、中英文标点与八齿设置齿轮图标；悬停提示弹窗与状态栏主窗口及拖拽交互按职责分文件维护。

- **设置窗口职责拆分**：[`window.cpp`](../src/tools/settings/window.cpp) 负责窗口创建、消息分派与命令处理；[`controls.cpp`](../src/tools/settings/controls.cpp) 负责控件布局、配置绑定、语言更新与卡片绘制；共享关于窗口由 [`common/about_dialog.cpp`](../src/common/about_dialog.cpp) 实现。内部上下文和控件 ID 集中在 [`internal.h`](../src/tools/settings/internal.h)，共享图标帮助代码及模态消息循环位于 [`common/dialog_util.h`](../src/common/dialog_util.h)。[`window.h`](../src/tools/settings/window.h) 提供公共接口，窗口在 `WM_NCDESTROY` 中回收 RAII 上下文。
- **设置保存与通知**：设置程序与进程内设置回退共享单实例互斥与窗口唤醒逻辑：若设置窗口已打开，再次触发设置仅恢复并切到已有设置窗口（或其当前活动的子模态窗口）前台，不会启动第二个设置实例。用户在设置窗口确认时先持久化配置，再关闭窗口并发送 Global Compartment 通知。保存失败会保留当前编辑内容并显示本地化错误，取消不会将窗口中的编辑应用到绑定配置；通知失败则明确提示配置已保存但需要重新激活输入法，不回滚保存。语言栏快捷设置先保存配置副本；失败时保留原运行时状态且不广播，以非模态 D2D Toast 短暂提示，不抢焦点或阻断菜单操作。设置窗口打开期间同时订阅 `kGuidCompartmentConfigVersion` 并在 `WM_ACTIVATE` 时校验注册表配置序列号：外部配置变更按字段合并刷新窗口控件与界面语言，同时保留用户在窗口内尚未保存的修改。
- **设置界面一致性**：设置、关于、保存失败及通知失败窗口均支持明暗主题刷新及中英文/跟随系统语言。提示窗口由 [`message.cpp`](../src/tools/settings/message.cpp) 统一实现，复用公共 D2D 卡片、按钮和控件创建函数，不再依赖原生 `MessageBox` 的主题和按钮语言。关于及提示窗口随所属设置窗口的语言预览刷新；启用保存模式时，预览不提前修改绑定配置。共享模态循环恢复所属窗口原有启用状态，并支持 Tab、Enter、Esc。
- **本地化文字布局**：关于窗口的许可证区域与提示正文复用 [`common/dialog_util.h`](../src/common/dialog_util.h) 中的文本测量函数，按中英文测量结果预留空间，并在语言变更时刷新布局。

- **控件交互与退出边界**：开关和单选框仅在有效按下后的释放事件中切换，取消点击或孤立的释放事件不改变状态。下拉框在下游消息处理返回后重新确认子类化上下文仍有效，避免回调销毁控件后访问已释放资源。模态消息循环保留并重新投递 `WM_QUIT`，遇到消息读取错误或窗口被外部销毁时返回失败，不再访问已回收的窗口上下文。

### 2.8 多进程词库同步、动态字库切换与深层 TSF 健壮性设计

1. **配置变更通知与状态共享（TSF Global Compartment `ITfCompartmentEventSink`，`tsf-global-compartment-bus`）**
   - 彻底移除基于 `FindWindowExW` + `kConfigChangedBroadcastMsg` 的 Win32 窗口广播。
   - 通过 `ITfThreadMgr::GetGlobalCompartment()` 订阅 TSF 全局 Compartment：
     - **配置/词库版本号 (`kGuidCompartmentConfigVersion`)**：配置修改、词库切换或在线造词（`Ctrl+=`）后尝试递增版本号，通过 `ITfCompartmentEventSink::OnChange` 通知订阅实例检查重载。失败需诊断，不能承诺所有进程立即或毫秒级更新；焦点检查仍用于按需发现变化。
     - **浮动状态栏坐标跨进程记忆 (`kGuidCompartmentStatusBarPos`)**：用户在进程 A 拖动 `StatusBarUI` 后，将坐标打包写入 Global Compartment；当焦点切换到进程 B 时，浮动状态条直接出现在用户刚刚拖到的位置，不再跳回右下角默认位置。
   - **配置作用域**：`QiQiConfig` 中保存到 `HKCU\Software\QiQiIME` 的词库选择、输入行为、默认中英/全半角/标点模式、界面语言、状态栏显示、声音及日志选项均为当前用户的全局持久设置。独立设置程序、进程内设置窗口和语言栏快捷设置在保存后都会递增配置版本并广播；其他活动输入法实例据此重载。配置中的模式项是新会话默认值，不会覆盖其他会话当前正在使用的输入模式。
   - **会话状态**：当前中英模式由 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 表示，全半角和标点模式由 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION` 表示，按 TSF 线程同步；组合串、候选列表、页码和选择位置属于当前输入会话的运行时状态，不写入用户全局配置。状态栏坐标则是单独的全局 Compartment 状态。
   - **系统 Compartment 双向监听 (`tsf-bidirectional-compartment-sync`)**：同时在 `ITfCompartmentEventSink` 中监听 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 与 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`，当外部（如 Windows 系统托盘指示器或宿主应用）修改输入模式时，反向同步回 `WubiEngine` 与状态栏。

2. **“一库一 `.qdict`”与自定义字库（`user_dict.txt`）同步机制**
   - **静态主/扩展词库**：三套随包词库各自编译为 `.qdict`，支持按需组合挂载。有效缓存可复用；缺失或过期时仍需解析编译，不给出未经测量的切换时限。
   - **动态用户自造词与跨进程同步（`user_dict.txt`）**：受控编辑（`AddUserPhrase`、`AddUserEntry`、`RemoveUserEntry`、`ModifyUserEntry`）通过 [`common/change_sync`](../src/common/change_sync.h) 协调排他写入、发布词条级增量，并更新 `user_dict.qdict`、通道快照及默认路径下的注册表镜像。外部编辑器直接修改词库时，对比源文件元数据与内容哈希：`<= 16` 条且保持幸存词顺序的差异转换为增量记录发布，大规模替换或重排则旋转基线 GUID 触发全量快照重载。
   - **安全输入边界应用（任务 39、40）**：实例在获得焦点或新一轮输入开始（`InputBoundary::NewInput`）时应用词库与词频更新，组合期间（`Composing`）暂缓，避免正在打字时候选重排。

3. **深层 TSF 输入体验与宿主兼容机制**
   - **异步排版光标跟随（`ITfTextLayoutSink` + Caret 兜底，`tsf-text-layout-sink-caret`）**：针对 Chromium / VS Code / WPF / Word 等异步排版宿主在首键插入组合串时 `GetTextExt` 返回 `TS_E_NOLAYOUT` 的情况，通过 `ITfTextLayoutSink::OnLayoutChange` 在宿主完成排版后立即刷新候选窗位置，并以 `GetGUIThreadInfo`（`rcCaret`）/ 焦点窗口坐标兜底，避免候选窗跳至 `(0, 0)`。
   - **Shift 键中英切换防误触（`shift-tap-anti-mistrigger`）**：记录 `VK_SHIFT` 按下时间戳（`GetTickCount64()`）并检测鼠标按键状态（`VK_LBUTTON` / `VK_RBUTTON` / `VK_MBUTTON`），仅当单按时长 `<= 350ms` 且期间无鼠标点击与其它按键时才触发中英切换，防止 `Shift+Click` 选区或长按 `Shift` 误切模式。
   - **数字小键盘支持（`numpad-and-composing-digits`）**：在静态按键分类、运行时 FSM 与全角模式中支持 `VK_NUMPAD0..9` 与 `VK_DECIMAL`，防止组合态下按小键盘数字穿透破坏正在输入的编码串。
   - **禁用标志检查及识别边界（`tsf-disabled-context-awareness`）**：普通按键按下入口检查 `GUID_COMPARTMENT_KEYBOARD_DISABLED` 与 `GUID_COMPARTMENT_EMPTYCONTEXT`；这不等于识别所有密码框、只读控件或游戏非聊天状态，也不代表 Shift 释放、保留键及异步回调已统一执行该检查。完整方案仍由任务 36 研究验证。
   - **异步 EditSession 状态快照隔离（`async-editsession-snapshot`）**：在 `KeyProcessResult` 中携带自包含的候选/分页快照传入 `LambdaEditSession`，避免 `TF_ES_ASYNCDONTCARE` 异步执行时读取到超前或滞后的引擎状态。
   - **词组排序持久化与跨进程同步（`persist-dynamic-phrase-freq`）**：启用 `dynamic_phrase_freq` 后，显式选中的词组按最近选择顺序提升，单字候选保留原位置，不是累计次数排名。排序采用带 FNV-1a 校验的 `QPF1` 二进制格式保存（兼容读取旧版 UTF-8 文本格式），并通过任务 39、40 的公共同步通道协调跨进程写入与 `Operation::Reorder` 增量同步；AppContainer 实例保持只读同步。

---

## 3. 已完成特性清单与后续扩展

### 已完成（v0.8.4）
- [x] **1. 完整中文标点与智能配对引号（`paired-quotes-punct`）**
- [x] **2. 候选框自适应宽度、多显示器 Per-Monitor DPI 缩放与鼠标点击选词（`candidate-dpi-autosize`）**
- [x] **3. 语言栏托盘按钮与右键菜单（`langbar-tray-menu`）**
- [x] **4. 系统设置接入、属性设置窗口与“关于奇奇五笔”对话框（`system-settings-config-ui`）**
- [x] **5. 4x4 超采样抗锯齿浮动状态条、悬停 Tooltip 与深浅色系统主题适配（`status-bar-ui`）**
- [x] **6. 组合串虚线下划线显示属性（`tsf-display-attributes`）**
- [x] **7. `z` 万能键编码反查提示（`wildcard-code-hint`）**
- [x] **8. 组合态大写字母顶字上屏、四码空码自动清屏（`clear_on_empty_4code`）与内置 WAV 音效（`sound.h`）**
- [x] **9. 注册表配置与内存映射词库最小权限访问、事务/文件替换及带进程名的结构化排障日志**

### 待办与后续扩展（TODO，按优先级排序）

#### P0：核心日常打字高频增强
- [x] **10. 分号 `;` / 单引号 `'` 二、三级候选快捷上屏（`semicolon-quote-quick-select`）**：在组合态（`Composing1..4`）下支持通过 `;` 键直接上屏第 2 候选词、`'` 键直接上屏第 3 候选词（由配置项开关控制）。
- [x] **11. 四码重码提示音（`dup-4code-beep`）**：当开启 `auto_commit_4code` 且输入满 4 码存在多个精确匹配重码时，提供可选的轻微提示音，提醒盲打用户当前有重码需选词。
- [x] **12. 编码渐进提示 / 简码反查提示（`prefix-code-hint`）**：在输入 1~3 码时，可选在前缀匹配的候选字词旁显示剩余后缀编码提示（如 `1.你(~i)`），辅助记忆简码与全码。

#### P1：进阶输入能力
- [x] **13. 在线快捷造词与自动取码（`online-user-word-creation`）**：按五笔 86 标准构词规则（双字词 2+2、三字词 1+1+2、多字词 1+1+1+末）自动计算新词编码并追加写入 `%LOCALAPPDATA%\QiQiIME\user_dict.txt`。
- [x] **14. 临时英文 / 网址 / 路径引导输入模式（`temp-english-guide-mode`）**：在中文空闲态下通过引导键直接输入一串包含大小写字母或符号的英文字符串（回车/空格上屏），无需频繁来回切换中英状态。

#### P2：渲染与词频体验打磨
- [x] **15. Direct2D / DirectWrite 渲染与生僻字/彩色 Emoji 字体回退（`directwrite-font-fallback`）**：候选窗使用 `Direct2D` + `DirectWrite` 并开启 `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`。实际生僻字覆盖、彩色字形和 Emoji 展示取决于系统及可用字体，不保证 CJK 扩展区或所有彩色符号完整显示。
- [x] **16. 可选词组动态调频（`optional-dynamic-phrase-freq`）**：默认保持五笔固定词序以保障盲打肌肉记忆，同时提供可选的“仅重码词组动态调频（单字固定不调频）”开关。
- [x] **17. 独立发行包与 MSI 安装包打包（`release-packaging-script`）**：通过 `scripts/build_all.ps1 -Package` 配合 `installer/QiQiIME.wxs` 将 `x64`/`x86` 双架构 DLL、共享 `data/` 词库、注册/卸载脚本与许可证一键打包生成 `.msi` 安装包与 `.zip` 便携包到 `dist/` 目录。

### 功能差距

以下为待开发或待评估能力，不代表已经实现。优先补齐轻量纯五笔的日常体验；竞品历史资料中的功能仅作为需求参考，不作为当前版本可用性或性能结论。

#### 首要代码审查待办：TSF 生命周期、诊断与性能

以下待办由代码阅读发现的问题及待验证风险整理，未勾选项尚未实施；静态风险不等于真实宿主中已经复现的故障。保存于本架构文档，供后续会话继续执行。与下方任务 41 协调 UI 清理方案，不重复实现，也不在存在过期回调时仅提前销毁窗口。

**进度说明：任务 39、40 的跨进程同步闭环及任务 41–46、48–59 的对应代码或工具工作已完成；原任务 38（独立词频查看/编辑工具）已合并入任务 18/19。任务 36 已于 2026-11-05 启动，完成官方契约研究和双架构本地实验，生产方案及真实宿主矩阵待验证。历史验证记录不等于所有真实宿主已验收。任务 47 已记录 2026-11-03 阶段性人工反馈，逐用例次数、宿主条件及部分分支仍未补齐，保留未完成状态。现有禁用标志检查也不等同于完整密码框识别。**

**首要执行：全仓库失败诊断**

- [x] **49. 全仓库失败日志审计（`audit-failure-log-error-codes`）**
  - 全面扫描生产代码，不限当前文件或 TSF：覆盖 IME 引擎/UI、公共组件、配置/文件操作、安装注册和各工具程序；测试及第三方依赖用于理解与验证，不修改第三方代码。
  - 查找已有失败日志缺错误码、返回值被忽略及失败未记录的路径；先区分应修复、预期可选探测/回退、无需错误码和需进一步确认，不机械添加日志。
  - 已知案例：[`tip_service.cpp`](../src/ime/tsf/tip_service.cpp) 的 `RegisterGUID`、`AdviseSingleSink`、`UnadviseSingleSink`、语言栏 `AddItem`/`RemoveItem` 及相应接口查询。跟踪成功注册/添加状态，区分无需清理与真正注销失败。
  - 保存真实返回值，记录操作名、必要的非敏感上下文及错误码。COM 沿用 `hr=0x{:08x}` 与 `gsl::narrow_cast<unsigned int>`；仅在 Win32 API 约定使用 `GetLastError` 时，于失败后立即捕获，不能替代 HRESULT 或输出陈旧错误。
  - 合理区分回退诊断与最终失败级别，不记录用户输入或敏感数据，不造成按键日志刷屏，不改变既有业务行为。
  - 分批修复并记录覆盖范围与剩余项，补充失败注入测试，验证错误码来源、操作上下文及正常行为；不能仅修复当前文件就宣布完成。与任务 48、53 协调避免重复实现。
  - 审计范围、修复项、验证方式及独立生命周期剩余项见[开发文档](dev.md#53-失败日志审计范围任务-49)；后续代码沿用该失败诊断规范。

**P0：先建立测试能力，再修复过期任务**

- [x] **55. 修复 IME 日志初始化循环依赖（`ime-etw-logger-bootstrap`）**
  - 原 [`Log`](../src/common/logger.h) 在 Provider 尚未注册时通过 `IsLogSinkActive` 提前返回，而注册发生在尚未进入的 `WriteLogMessage` 路径中；IME 未主动初始化，导致仅开启 ETW 采集、未附加调试器时可能始终没有日志。任务 49 遗漏了这一初始化缺陷。
  - 在正常服务激活路径、首条需要记录的消息之前明确初始化日志，不依赖调试器启动日志通路；避免在 `DllMain` 的加载器锁内注册或执行复杂初始化。
  - 覆盖首次激活，以及停用后重新激活；与任务 51 的进程共享日志生命周期协调，停用一个服务实例不得破坏其他实例的日志能力，不重复实现关闭策略。
  - 保留应用日志级别、无接收方时的低开销、真实注册错误码及不递归失败兜底，不记录用户输入或敏感数据。
  - 补充无调试器且 ETW 会话已启动、应用启动后才启用采集、停用/重新激活、多实例、注册失败及日志 Off 的测试；实际采集验证应确认包含目标宿主 PID 和预期消息，不能仅检查会话存在。
  - 已在服务构造入口（配置/词库初始化之前）与激活路径主动幂等注册，分离注册与设置日志级别，移除日志写入/查询的隐式注册及 `DllMain` 的格式化日志；与任务 51 一并完成模块生命周期清理。
  - 双架构普通测试及真实 DLL、无调试器、多进程 ETW 采集通过，包含晚启动采集、停用/重新激活、模块重载、Off 及目标 PID/预期消息/零丢失校验；同步核心注入注册失败与并发初始化。详见[开发文档](dev.md#54-日志初始化与生命周期验证任务-5551)和[日志指南](log.md#初始化与共享生命周期)，不宣称浏览器/UWP 全面兼容性已验证。

- [x] **42. 延迟编辑会话测试能力（`tsf-deferred-session-test-support`）**
  - 扩展现有立即执行回调的编辑会话 Mock，支持排队及手动执行；模拟停用、重新激活和上下文变化后才执行旧任务的时序。
  - 先建立可暴露问题的测试，再实施任务 43、44；不要用同步 Mock 的成功结果代替异步生命周期验证。
  - 已增加外部拥有的 FIFO 调度器，保留默认立即执行，覆盖同步优先、异步返回值、请求/回调失败、只读限制、重入及 COM 引用释放；停用/重新激活和上下文变化后的真实写回调、布局读回调可手动运行。
  - x64/x86 的 TSF/UI 与真实 ETW 集成测试通过；仅扩展测试能力，不修改生产代码或自动过滤旧任务。调度器所有权与使用边界见[开发文档](dev.md#55-延迟编辑会话测试任务-42)。
- [x] **43. 阻止过期异步编辑任务（`tsf-stale-edit-sessions`）**
  - 对 [`ApplyEngineResult` 与 `OnLayoutChange`](../src/ime/tsf/tip_composition.cpp) 的编辑会话校验激活周期及输入上下文周期；停用或输入目标失效后，旧任务不得写文档、创建组合或恢复 UI。
  - 仅检查是否激活或 `ComPtr` 非空不够：重新激活后旧任务仍属于旧周期，对象存活也不代表仍是有效输入目标。
  - 依赖任务 42；确保新周期的正常输入不受影响。
  - 已为写入及布局读回调捕获服务实例的激活/上下文周期，在回调开始处统一校验；停用先关闭有效激活状态，现有重置入口及输入目标切换先使旧任务失效，再清理本地状态。过期任务返回 `S_OK` 并记录不含输入内容的调试诊断，不访问文档或恢复 UI。
  - 上下文切换不再沿用旧组合范围；x64/x86 的 TSF/UI、真实 ETW 测试通过，覆盖同一 COM 上下文重新激活、焦点重置、A→B→A、宿主终止、旧提交/取消/布局任务及正常 FIFO。验证范围与任务 44、45 的边界见[开发文档](dev.md#56-过期编辑会话屏障任务-43)。

**P1：组合、上下文与真实宿主验证**

- [x] **44. 停用时正确结束宿主组合（`tsf-end-composition-on-deactivate`）**
  - 状态机、所有权、关键时序及可关联的详细调试日志见[组合生命周期设计与排障](composition.md)。
  - 原 [`Deactivate`](../src/ime/tsf/tip_service.cpp) 调用的本地重置仅释放 `composition_` 引用，不等于调用 `EndComposition`。
  - 区分宿主已经终止组合与 IME 主动取消；在有效编辑会话中处理需要主动结束的组合，再清理相关状态和显示属性。
  - 处理编辑权限不可用、请求失败、回调重入、部分初始化失败及重复停用；不在无有效编辑权限时强行修改文档。
  - 依赖任务 42，与任务 41 的窗口销毁流程协调。
  - 已按确认策略保留预编辑文字，在同步读写编辑会话中清除显示属性并结束捕获的宿主组合；拒绝重入激活，重复停用不重复结束。宿主终止当前组合时使用其提供的写 cookie 清理属性，不再主动结束；旧组合通知不重置新组合。
  - 已按更新确认的策略，在 `TF_E_SYNCHRONOUS` 且回调未执行时申请受控异步清理；独立状态只操作旧组合和范围，同一上下文的新写请求强制异步排在旧清理之后，不恢复 UI、不保留线程管理器引用。宿主提前终止旧组合时后续回调退出，避免误清新组合属性。
  - 真实私有 TSF 文档验证布局通知内同步拒绝、返回后异步清理、组合枚举及组合/显示属性移除、保留文本和重新激活后的新组合隔离。只读/断开/请求拒绝等仍明确记录并安全清理本地资源；不将 `S_OK` 或排队成功当成所有宿主均能结束的保证。详见[开发文档](dev.md#57-停用组合清理与受控异步补救任务-44)。
  - x64/x86 的 TSF/UI（包含真实 TSF 文档测试）及 ETW 集成测试均通过，定向 CppCoreCheck 无新增告警；实际浏览器/UWP 宿主兼容性仍需单独验证。
- [x] **45. 文档及上下文生命周期处理（`tsf-document-context-lifecycle`）**
  - 继续以 `ITfContext` 为核心，不建立不必要的文档管理器列表。`OnInitDocumentMgr` 无文档级资源时可保持空实现。
  - `OnUninitDocumentMgr` 判断是否关联当前上下文；`OnSetFocus` 比较实际输入目标，而不只是判断文档指针非空。
  - `OnPushContext` 判断上下文用途及关联关系，不能无条件取消主组合；`OnPopContext` 判断弹出的是不是当前上下文，非当前上下文通知不得清理当前输入。
  - 统一旧任务失效、订阅注销及组合/UI 清理流程；出栈或文档解除初始化时不假定仍能取得编辑权限。
  - **官方背景**：上下文栈最多两个；第一个为主文档上下文，通常只有它。文本服务可为模态 UI（官方例子为候选列表）压入第二个上下文，结束后弹出并恢复主上下文。普通窗口切换或候选窗显示不必然触发 `Push`/`Pop`，正常模态入栈不等于主组合失效。
  - 首次入栈会触发 `OnInitDocumentMgr` 及 `OnPushContext`；移除最后一个上下文会触发 `OnUninitDocumentMgr`。实现及 Mock 应核对具体通知时机，不将此描述当作回调顺序，也不把解除初始化等同于 COM 对象析构。
  - `Pop(0)` 只弹出栈顶，不能移除唯一的主上下文；`Pop(TF_POPF_ALL)` 仅用于文档解除初始化，清空上下文并终止文本服务 UI。`Push`/`Pop` 必须同线程，不能在 `Pop` 期间重入 `Push`/`Pop`。
  - 依赖任务 43、44。官方依据：[Push](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-push)、[Pop](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfdocumentmgr-pop)。
  - 已按活动 context 缓存单个所属文档并实现焦点/栈关联判断；正常模态入栈保留主组合，无关通知不清理当前输入。当前 context 移除只清本地资源，不请求写权限，并使匹配的已排队旧清理失效。
  - 同时修正任务 43/44 的关联缺口：在引擎处理新目标按键之前重置旧目标；连接中的目标切换/失焦复用宿主组合清理，A→B→A 的新写仍排在旧清理之后；范围查询重入移除后不再提交清理请求。
  - x64/x86 TSF/UI、真实私有 TSF 文档通知及 ETW 集成测试均通过，定向 CppCoreCheck 无新增诊断。实现、日志和验证边界见[开发文档](dev.md#58-文档及上下文生命周期任务-45)及[英文设计](composition.md#8-document-and-context-notifications)；不替代浏览器/UWP 实际宿主验证。
- [x] **46. 生命周期回归测试（`tsf-lifecycle-regression-tests`）**
  - 依赖任务 43、44、45，利用任务 42 的延迟执行能力验证：提交后停用、停用后重新激活、输入目标变化及过期任务执行。
  - 覆盖停用时仍有组合、宿主提前终止、重复停用、激活失败回滚、文档解除初始化及上下文出入栈。
  - 验证正常模态入栈不误取消主组合，非当前文档/上下文通知不干扰当前输入，旧任务不写文档、不恢复 UI，引用及订阅正确释放。
  - 按官方 API 语义模拟通知，不能仅用普通窗口切换替代上下文栈测试。
  - 已盘点既有回归并建立[覆盖矩阵](composition.md#91-regression-inventory-task-46)：新增 86 个执行中编辑/宿主终止、8 个迟到组合创建、4 个清理属性查询用例，另补同周期替换组合及按键状态重入各 2 例。
  - 先用失败测试复现再修正旧编辑在宿主调用返回后继续执行、宿主终止发生在属性查询中导致漏清、同周期替换组合后旧 UI 继续刷新，以及目标清理/compartment 查询改变当前按键修饰状态。迟到组合复用独立清理，新写必要时重新排到旧清理后，不改 FSM 表或同周期 FIFO。
  - x64/x86 Release TSF/UI 与 ETW 各连续通过 2 次，引擎回归通过；双架构定向 CppCoreCheck 修正新增 const 诊断后仅保留既有告警。验证边界见[开发文档](dev.md#59-生命周期回归矩阵任务-46)，真实宿主显示验证仍由任务 47 完成。

**P1：注册所有权、全仓库日志与性能**

- [x] **48. 保留键注册与注销生命周期（`tsf-preserved-key-registration-lifetime`）**
  - 保留现有快捷键方案，逐键记录本激活周期成功注册的 GUID 与 `TF_PRESERVEDKEY`；停用或初始化失败回滚时只注销本周期成功注册项，同时跟踪键盘监听注册状态。
  - 区分 `TF_E_ALREADY_EXISTS` 与其他失败，记录实际 `HRESULT`；不强行覆盖、先注销已有注册，或将重复注册视为本次取得所有权。注销失败不得静默丢失状态和诊断。
  - `PreserveKey` 是 TSF 保留键机制，不是 `RegisterHotKey` 全局热键。重复注册返回值不能单独证明其他输入法占用同一组合键；注册成功也不代表没有应用/系统快捷键语义冲突，后者需另行验证，不能擅自新增抢键策略。
  - 测试全部成功、单项重复/其他失败、部分成功回滚、重复停用、重新激活及注销失败，保持原快捷键行为。
  - 参考源码：微软 [SampleIME](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/IME/cpp/SampleIME/CompositionProcessorEngine.cpp) 未逐次检查注册返回值，注销按表进行，不宜照搬错误处理；[Weasel](https://github.com/rime/weasel/blob/master/WeaselTSF/KeyEventSink.cpp) 当前注册代码位于 `#if 0`，初始化直接成功，注销为空。实施前复核源码，不能据此泛化所有输入法行为。
  - 官方依据：[PreserveKey](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-preservekey)、[UnpreserveKey](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-unpreservekey)。
  - 已在 `QiQiTextService` 逐键记录本激活周期成功注册的 `(GUID, TF_PRESERVEDKEY)` 并独立跟踪 `key_event_sink_registered_`；`Deactivate` 或初始化失败回滚时只对本周期成功项调用 `UnpreserveKey`，只在 `AdviseKeyEventSink` 成功后调用 `UnadviseKeyEventSink`。
  - 复核了微软 SampleIME（忽略 `PreserveKey`/`UnpreserveKey` 返回值且按静态表全量注销）与 Weasel（保留键注册位于 `#if 0`、注销为空），未照搬其实现；显式区分 `TF_E_ALREADY_EXISTS`（记录跳过所有权）与其他失败，注销时逐键记录 `vkey`/`mod` 与真实 `HRESULT`，并在操作前清空本地持有表保证重复停用幂等。
  - x64/x86 Release TSF/UI 与 ETW 集成测试通过，自有调试子进程验证重复注册、其他失败与注销失败日志；定向 CppCoreCheck 无新增告警。详见[开发文档](dev.md#510-保留键与键盘监听所有权生命周期任务-48)。
- [x] **50. 焦点处理性能埋点（`tsf-focus-performance-instrumentation`）**
  - 对 [`HandleFocusChanged`](../src/ime/tsf/tip_service.cpp) 的 `ReloadConfigIfModified`、`SyncStatusBarPositionFromGlobalCompartment` 分别测量耗时，并记录焦点处理总耗时及两类通知来源。
  - 区分配置/词库无变化检查与实际重载，识别重复通知、同次词库与配置变化导致的重复加载；记录重载结果及非敏感计数。
  - 复用现有计时/日志机制；如无则用单调高精度时钟（如 `std::chrono::steady_clock`），单位明确，精度能观察轻量检查。
  - 详细埋点采用调试/跟踪级别，减少日志关闭时开销及生产刷屏，不记录用户文本或敏感路径，不新增吞异常或成功形状的失败结果。
  - 验证无变化、配置变化、词库变化、两者同时变化、位置相同/变化及两种焦点通知；在真实宿主测量后再决定优化。不在本项擅自增加阈值、改变重载策略或把 TSF/UI 对象移到后台线程。
  - 已实现 Debug 级别 `FocusPerformance` 与关联的重载详情，采用单调时钟整数纳秒，保留同次双重载，并记录配置/词频结果、词库调用次数及映射数量。详细日志关闭或没有接收方时不读取计时时钟；失去焦点仅测量既有清理路径。
  - 双架构构建与 TSF/UI 测试通过；自有调试子进程直接验证实际日志、计时形状、所需变化分支及 Info/Off 过滤。真实宿主测量留作后续优化依据，操作步骤见[日志指南](log.md#61-焦点处理耗时任务-50)；不宣称已完成宿主性能优化。

**P2：共享资源与命名**

- [x] **51. 共享日志关闭时机（`tsf-shared-logger-lifetime`）**
  - 已移除 `Deactivate` 的 `log::Shutdown`，Provider 按进程内模块生命周期共享、正常卸载时统一清理；不同进程独立，不使用跨进程锁或引用计数。
  - 初始化序列化且成功后才发布注册状态；稳定日志路径不获取应用锁，模块清理要求调用者结束，不在加载器锁内初始化或获取初始化锁。未初始化和重复清理不触发注册。
  - 与任务 55 同批验证：普通测试断言停用/重新激活保留共享 Provider 与 Off；真实 DLL 多实例、多进程 ETW 采集验证停用一个实例或退出一个进程不影响其他实例/进程，详见[开发文档](dev.md#54-日志初始化与生命周期验证任务-5551)。
- [x] **52. 进程词库缓存清理时机（`tsf-process-dict-cache-lifetime`）**
  - 评估每个 `Deactivate` 调用 `ClearProcessCache` 是否造成多实例后续重复加载，按共享资源生命周期决定清理策略。
  - 其他实例持有的 `shared_ptr` 不因清空缓存而悬空；重点验证共享复用、内存释放及停用一个实例后其他实例查询正常，不将性能风险误报成已确认的悬空引用。
  - 已移除 [`QiQiTextService::Deactivate`](../src/ime/tsf/tip_service.cpp) 中的 `WubiMappedDict::ClearProcessCache()`：停用仅通过 `engine_.ClearExternalDictionaries()` 释放本实例的 `shared_ptr<const WubiMappedDict>`，进程级 `g_process_dict_cache` 由模块生命周期管理（[`DllMain`](../src/ime/tsf/dllmain.cpp) 动态卸载时清理，文件修改时按大小/时间戳自动更新替换）。
  - 同时修正 `ActivateEx` 在 `PrimaryDict::BuiltinOnly` 模式下停用后重新激活漏重新挂载用户词库的问题（统一在 `engine_.MappedDictionaryCount() == 0` 时调用 `LoadExternalDictionaryNextToDll` 复用进程缓存）。
  - 引擎与 TSF 测试共同验证：同进程多实例共享同一 `WubiMappedDict` 实例、停用一个实例不清空进程缓存且其他实例与重新激活实例查询正常（含 `BuiltinOnly` + 用户词库）、显式清空缓存不会使已持有的 `shared_ptr` 悬空，且所有持有者与缓存释放后底层对象归零销毁。详见[开发文档](dev.md#511-进程词库缓存生命周期任务-52)。
- [x] **53. 类别管理器创建及 DLL 生命周期（`tsf-category-manager-creation-lifetime`）**
  - 背景：先用 `GetModuleHandleW`/`GetProcAddress` 动态调用 `TF_CreateCategoryMgr`，未取得接口则回退 `CoCreateInstance`；两条创建路径都忽略 HRESULT，仅检查指针，`RegisterGUID` 日志也缺错误码。
  - 官方说明导出函数可在不初始化 COM 时创建类别管理器，调用线程必须维护对象引用计数；局部 `ComPtr` 管理接口是合理的。文档没有要求导出优先、COM 回退的固定顺序。
  - `GetModuleHandleW` 不增加 DLL 引用计数，官方建议 `GetModuleHandleEx` 避免卸载竞态；尚未证明当前激活场景实际会发生，应先核实宿主兼容性和模块生命周期依据。
  - 不凭推测删回退。若保留动态路径，复用 RAII 模块资源机制，保障调用及接口使用期间模块存活，接口先释放、持有的模块引用后释放；借用的 `GetModuleHandleW` 句柄不得 `FreeLibrary`。
  - 保存并诊断创建、回退及注册的实际 HRESULT；不能在宿主线程擅自初始化/反初始化 COM。只有证实 COM 路径足够可靠后再评估简化。
  - 覆盖导出成功/不可用、导出失败后 COM 成功、全部创建失败及注册失败；保留失败不终止激活的既有行为，验证目标宿主兼容性。
  - 官方依据：[TF_CreateCategoryMgr](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-tf_createcategorymgr)、[GetModuleHandleW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew)、[ITfCategoryMgr](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfcategorymgr)。
  - 已保留导出优先/COM 回退路径，改用 `GetModuleHandleExW(0, ...)` 与现有 `ScopedModule` 持有引用，局部 COM 接口先于模块释放；各失败阶段记录对应错误码，不修改宿主 COM 初始化状态，注册失败仍不阻止激活。
  - 双架构 TSF/UI 与 ETW 测试通过；新增失败分支、释放顺序、未初始化 COM 线程上的原生导出及真实 COM 回退测试。详见[开发文档](dev.md#512-类别管理器创建与模块引用任务-53)，真实宿主交互验证继续由任务 47 跟进。
- [x] **56. 独立实时日志监控工具（`realtime-etw-log-monitor`）**
  - 执行顺序：紧接任务 53；依赖任务 55、51（均已完成）。
  - 为日常开发和用户问题复现提供按需启动的轻量工具，避免每次附加调试器或打开 WPA；作为可选诊断程序，不随 IME 常驻，不替代 WPA 的完整离线分析能力。
  - 沿用无调试器正常注册和按模块清理的策略，工具不能靠附加调试器掩盖初始化问题。
  - 直接消费 `QiQiIME.TraceLogging` 的实时 ETW 事件，并按自描述元数据解码 `process`、`message` 和事件头；复用现有 Provider，不新增文本文件轮询、宿主注入或第二套日志传输。
  - 已确定使用 GUI，独立程序必须带版本号。复用根 `VERSION`、生成的 `common/version.h` 与 `version.rc.in`，沿用设置程序的 CMake 资源生成方式，设置独立 EXE 名称/描述及 `VFT_APP`；标题显示版本，关于信息和导出头包含完整版本、构建标识及工具架构。不复制维护第二套版本常量，也不以工具架构推断事件来源架构。
  - 推荐界面采用原生 Win32 窗口与现有 D2D 皮肤混合：工具栏按钮、筛选框复用 `CreateFluentChildControl`、`FluentD2DThemeContext`，主题、DPI、字体及句柄管理复用 `common` 帮助代码；大量日志行用原生 `ListView` 的 `LVS_OWNERDATA` 虚拟列表，不重写 D2D 表格、滚动条、选择与键盘导航。高对比度优先系统可读性，不引入新 UI 框架，也不直接依赖设置窗口的内部状态。
  - 界面设计：顶部开始/停止、暂停滚动、清空、导出、关于；第二行进程名、PID、级别与消息文本筛选；中部列出时间、进程、PID、TID、级别、消息；底部可调整高度的只读详情区和采集状态、缓冲用量、丢失/淘汰及解码失败计数。暂停滚动不停止采集，停止后保留已有记录；导出明确针对当前筛选快照并先提示环境信息检查。
  - 区分显示过滤与应用日志级别，不承诺显示已在应用侧被过滤的消息。当前 Provider 将 Trace/Debug 合并为 ETW Verbose、Error/Critical 合并为 ETW Error；第一版按实际 ETW 级别显示和筛选，不虚构原始级别。
  - 尽量复用 [`logging.cpp`](../tests/ime/tsf/logging.cpp) 已验证的 Provider 过滤、TDH 解码与会话清理经验；目前它是离线 ETL 测试，不可直接当实时采集器。只在实际复用时提取无测试断言的最小公共逻辑，生产程序不依赖测试代码。
  - 采集与解码在后台执行，使用按条数及字节数有界的缓冲，UI 批量刷新；筛选和详情基于稳定事件标识，淘汰时不留下悬空引用。监控器自身诊断显示在状态区，避免逐事件日志或解码错误日志回流导致自激。第一版不同时实现 CLI。
  - 使用有界事件缓冲和批量显示，明确报告丢失事件、解码失败、权限不足及会话创建/停止错误；按采集 API 的权限要求运行工具，不提升被测宿主权限，只清理本工具拥有的会话与资源。
  - 验证多宿主与 x64/x86 事件、无调试器、晚启动监控、进程退出/重新启动、高频事件、缓冲溢出、暂停/恢复及退出清理；导出前提供敏感环境信息检查，不默认长期写盘或上传日志。
  - 完成后更新[日志监控指南](log.md)，提供操作步骤。
  - 已实现 [`QiQiIMELogMonitor`](../src/tools/log_monitor/CMakeLists.txt)：共享版本资源、后台实时采集/TDH 解码、原生多选虚拟列表、稳定 ID、筛选、详情高度滑块、暂停滚动、有界缓冲、单选/多选完整日志项剪贴板复制和带隐私确认的 UTF-8 快照导出。提取并在设置程序共用控件颜色及高对比度/原生按钮回退，不依赖设置窗口内部状态。
  - 自身诊断使用独立的 `QiQiIME.LogMonitor` Provider、有界视图与导出来源，不重发采集消息；状态区分别显示 ETW 丢失、本地淘汰、解码和内存异常。启动异常使用 RAII 回滚，停止失败保留自有会话供重试；同步等待 Provider 启用，避免晚启动采集丢失首条消息。
  - 桌面增强：独立 16–256 像素品牌窗口图标（去除与标题栏/关于框重复的客户区图标与说明文字）、系统深浅色/高对比度及变更通知、运行时中英/自动语言；不修改 IME 配置，不增加托盘通知。共享控件回退及工作区矩形帮助代码，支持跨屏 DPI、负坐标、普通窗口拔屏恢复和窄窗口布局。
  - x64/x86 的模型/GUI/版本/导出、实时多进程 ETW、Common、Settings、TSF/UI 及原有 ETW 回归通过，新增 ETW 测试未跳过；两个方向的跨架构采集与真实 GUI 收到事件、正常退出均验证通过。覆盖中文、内嵌 NUL、时间/级别、Provider 重注册、高频溢出、会话清理、主题/语言状态保留、深色表头像素及模拟显示器变化；真实双屏 150%→100% 移动另行通过。定向 CppCoreCheck 无新增监控器告警，详见[开发文档](dev.md#513-实时日志监控工具任务-56)。完整真实高对比度、热插拔/最大化/最小化及浏览器/UWP 矩阵不据此宣称全面验证。
- [x] **57. 日志监控器默认级别与输入法输出级别脱节修复（`log-monitor-effective-level-sync`）**
  - 问题现象：`QiQiIMELogMonitor` 界面默认 `Level` 显示为 `All`（且 ETW 会话启用了 `TRACE_LEVEL_VERBOSE`），但输入法侧 [`Log`](../src/common/logger.h) 被进程内 `g_log_level`（默认注册表 `LogLevel=2` 即 `Info`）提前拦截，导致用户在监控器看到 `Level = All` 却完全收不到 `Debug`/`Trace`（`Verbose`）级别日志。
  - 修复方案：新增 `log::ShouldLog(Level)` 并在 `Log`、`WriteLogMessage`、焦点计时、配置热重载及 `LogShiftTap` 中统一复用；当 `LogLevel != Off` 时，按活跃 ETW 会话请求的级别（`TraceLoggingProviderEnabled(g_qiqi_etw_provider, etw_level, 0)`）直接放行 ETW 事件，同时保留 `LogLevel` 对附加调试器 `OutputDebugString` 的阈值控制及 `LogLevel=Off` 的总开关语义，使监控器开始采集即可实时收到 `Debug`/`Trace`（`Verbose`）诊断日志而无需手改注册表或重启宿主。
- [x] **58. 修复英文模式下 Shift 无法切回中文及 Ctrl+Space Compartment 写入失败（`tsf-shift-toggle-and-compartment-sync`）**
  - 问题现象：
    1. 按 `Ctrl+Space` 触发外部 TSF Compartment 变更后，[`SyncEngineFromSystemCompartments`](../src/ime/tsf/tip_config.cpp) 在 `OnChange` 回调栈中反调 [`SyncTsfCompartments`](../src/ime/tsf/tip_config.cpp)，导致 `ITfCompartment::SetValue failed (hr=0x8000ffff)`（`E_UNEXPECTED`）。
    2. 最初怀疑英文模式下 `OPENCLOSE=0` 影响 Shift 派发；实际 VS Code 日志显示宿主直接调用 `OnKeyDown` / `OnKeyUp`，省略测试回调，而 Shift 候选仅在 `OnTestKeyDown` 建立，导致 `attempt=0`、释放时始终 `no-candidate`。两种按下入口已共用跟踪逻辑，重复回调保留原始计时。
  - 修复目标：
    1. 收到外部 `OnChange` 时避免在通知回调内对同一 Compartment 重复回写相同值，消除 `0x8000ffff` 警告。
    2. 核实并修复 TSF 下中英文切换与 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` / `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION` 及 `Shift` 按键监听的协同关系，确保在中文与英文模式下按 `Shift` 或 `Ctrl+Space` 均可双向稳定切换且任务栏状态同步。
  - 验证结果：双架构构建及回归测试通过；2026-11-03 用户确认任务 58 已正常，按上述目标完成验收。
- [x] **59. ETW 结构化分类（Keywords）、操作阶段（Opcode）与异步活动关联（ActivityId）（`etw-keywords-opcode-activity-correlation`）**
  - 在 [`logger.h`](../src/common/logger.h) / [`logger.cpp`](../src/common/logger.cpp) 中定义非零 `Keyword` 位掩码（`General`、`Lifecycle`、`Input`、`Composition`、`Dictionary`、`Sync`、`UI`、`Config`）、`Opcode`（`Info`、`Start`、`Stop`）、`LogContext` 与 `CreateActivityId()`。
  - 使用 `TraceLoggingWriteActivity` 显式写入 `ActivityId` 与可选 `RelatedActivityId`；无活动事件显式写入 `GUID_NULL`，不修改或拾取宿主线程隐式 TLS `ActivityId`。`ShouldLog` 按 `(Level, Keyword)` 短路过滤。
  - 为 TSF 组合清理（`CompositionCleanup`）、编辑会话（`RequestEditSession` / `LambdaEditSession`）、服务激活/停用/焦点切换、跨进程同步（`change_sync::Channel`）与词库加载接入分类、起止阶段及跨异步回调保持的 `ActivityId` / `RelatedActivityId`。
  - 升级 `QiQiIMELogMonitor` 解码 `Keyword`、`Opcode`、`ActivityId` 与扩展头中的 `RelatedActivityId`，支持按分类与活动关联筛选、显示、复制及导出；x64/x86 单元测试与真实无调试器 ETW 集成测试验证通过。
- [ ] **47. 真实宿主生命周期验证（`tsf-real-host-lifecycle-tests`）**
  - 执行步骤、矩阵及通过标准见[任务 47 测试文档](hosts.md)；第 8 节已记录 2026-11-03 人工反馈，尚缺逐用例次数、完整环境/日志及部分分支的验收证据，不据此自动勾选完成。
  - 在 VS Code 与浏览器中验证组合期间失焦、停用、重新激活后的组合文本、下划线及候选窗口清理。
  - 检查 UI 是否残留或被旧任务重新显示，记录可复现步骤、宿主条件和验证结果；配合单元测试，不以 Mock 代替真实显示验证。
  - 依赖任务 43、44、45。
- [x] **54. 明确显示属性注册命名（`tsf-display-attribute-registration-name`）**
  - 已统一为 `RegisterCompositionDisplayAttributeAtom`，同步声明、定义、调用、日志及直接相关文档引用；双架构构建与 TSF/UI 测试验证既有行为。类别管理器创建/回退及模块引用随后由任务 53 完成，保留注册失败不终止激活的策略；真实宿主边界仍由任务 47 跟进。
  - 保留 [`display_attribute.cpp`](../src/ime/tsf/display_attribute.cpp)、[`display_attribute.h`](../src/ime/tsf/display_attribute.h) 及其类名；它们对应 TSF 正式接口，暂不进行收益有限的批量改名。

#### 设计模式优化待办（2026-11-05）

来源：[设计模式优化扫描报告](review.md)。R01、R06 已完成有界一致性修正；R03、R04 已完成配置投影与共享加载计划；R02 已分离编辑草稿状态，均通过双架构定向回归。R05 已完成票据与清理记录登记提取，编辑任务准备及完整协调器仍后置。P1 表示可靠性优先级，不是安全漏洞评级，不据此宣称复现过生产数据丢失。

**P1：先验证并修复失败一致性**

- [x] **R01. 分阶段备份恢复与显式恢复结果**：已分离完整解析校验、恢复计划和实际写入，区分完整成功、部分完成与失败，保留首次错误及独立刷新错误；检查 Replace 中间刷新和最终刷新，避免 UI 将部分写入或不完整视图提示为成功。配置拒绝、锁定文件/通道、逐项写入及刷新失败回归在 x64/x86 通过；跨资源回滚仍单独评估，不承诺原子性。
- [x] **R06. 同步批次采用候选快照事务**：已在候选状态应用完整批次，成功后交换发布四类词条及所需映射；七类非法后项、重复失败、快照失败/恢复及正常同步回归在 x64/x86 通过，比较状态、已应用版本和成功计数。必要词频镜像错误不再被忽略；整体 Store/Facade 拆分后置，不承诺通道间全局事务。

**P2：按边界逐步重构**

- [x] **R02. 词库编辑采用 Presentation Model 与显式草稿状态**：已用独立 `DictEditor` 表达浏览、新建及已有词条草稿，集中命令可用性、脏状态、自动/手动编码和保存动作；保留原记录来源/编码/词条身份，窗口继续管理 HWND、布局、通知重入及 IME 组合态。纯状态与真实窗口回归覆盖切换、未修改保存、失败保留、外部删除不复活、手动改码和组合态 Enter/Esc，x64/x86 通过；未增加撤销/重做。
- [x] **R03. 统一类型化配置描述与投影**：已用成员指针描述统一 24 个标量的注册表、备份键和范围，设置仅合并 19 个可编辑字段；保留现有格式、默认值、布尔非零兼容、页大小钳制和路径排除范围。字段集合、注册表/备份往返及 38 组真实窗口合并矩阵在 x64/x86 通过；未引入反射框架或迁移控件布局。
- [x] **R04. 提取共享的纯词库加载计划**：IME 与工具共用自带主词库/扩展的角色及顺序计划，用户来源、相对路径、诊断和 IME 注册表回退仍由执行端处理。六种组合、来源展示、缺失词库、缓存复用及默认/显式用户路径回退差异回归在 x64/x86 通过；未改授权策略或宣称性能改善。
- [ ] **R05. 谨慎提取 TSF 组合生命周期协调器（票据与清理登记完成，整体后置）**：`EditSessionTicket` 统一激活/上下文 epoch 与目标身份；[`CompositionCleanupRegistry`](../src/ime/tsf/composition_cleanup.h) 收拢弱引用登记、组合查找、待清理裁剪及上下文移除标记，回调仍强持有清理记录。票据、交错执行/丢弃、登记顺序、已完成/过期记录及释放边界回归通过 x64/x86 TSF/UI 测试。任务准备、COM 请求/回调执行和完整协调器提取仍后置；原有重入检查、清理屏障、宿主 UI 许可及 FSM 不变。

#### 已知兼容性问题：Explorer 文件列表输入定位
- 2026-11-05 在 Explorer 文件列表获得焦点、使用奇奇中文输入时，观察到字母组合小窗及候选窗位于屏幕左上角；移动 Explorer 窗口后仍可复现。用户报告微软输入法也有同类现象，但不据此直接认定宿主根因。
- 已确认 Explorer 加载 `0.9.32+7556636`。本地时间 07:26:17 的定位日志先返回零高度文本矩形 `(1919,1127,1920,1127)`，随后持续成功返回 `(13,9,24,38)` 等左上角矩形，宽度随输入变化；奇奇候选实际位于 `(13,42)`。布局通知与回调正常执行，直接原因是候选跟随 TSF 提供的左上角组合范围，而非仅有全零矩形或缺少布局通知。TSF 为何提供该位置及字母组合小窗的归属尚未确认。
- 暂时保留现状，不做 Explorer 特判，不禁用文件列表的中文键入定位。不能仅凭左上角坐标判断异常；鼠标锚定、选中文件项锚定或单独移动候选都存在交互及兼容性代价，尚无已验证的通用整体方案。本问题与安全模式能力声明无关。
- 零高度矩形可另行评估通用定位健壮性处理，但不作为本问题的完整修复；任何后续方案均需分别验证字母组合小窗与奇奇自绘候选窗。日志采集与定位字段见[日志指南](log.md#候选窗口定位诊断)。

#### 已完成的代码质量检查
- [x] **0. 全量代码质量检查与测试补全（`code-audit-and-test-completion`）**：对全仓（`common`、`engine`、`tsf`、`ui`、`settings`）执行代码质量扫描（RAII 合规、代码复用/DRY、C++ Core Guidelines & GSL 最佳实践、异常与边界安全），并补全遗漏的单元测试分支。

#### P0：TSF 停用阶段资源清理
- [x] **41. 提前销毁停用服务的 UI 窗口（`tsf-deactivate-ui-cleanup`）**：将状态栏与候选窗口的销毁提前到 `QiQiTextService::Deactivate()`，不再仅隐藏窗口并等待 COM 引用计数归零后析构。
  - 保留 UI 对象与回调以支持再次激活；先结束组合和 TSF UI 元素，再销毁窗口，并在所属 UI 线程完成清理。
  - 析构保留幂等兜底，复用现有 RAII 清理机制，移除无意义的空 `catch (...)`，避免静默吞错。
  - 补充停用后对象仍被引用、重复停用、激活失败回滚、再次激活及异步 EditSession 的生命周期回归测试。
  - 已补展示开关、Begin/End 注册重入及旧位置查询屏障、`WM_NCDESTROY` 句柄清理与可报告失败的所属线程销毁；实际 HWND 回归检查销毁顺序、提示窗/捕获清理、不同 manager 复用相同 UI ID 的隔离及最终外部引用安全。双架构 Release 的 TSF/UI 与 ETW 测试各连续通过 2 次，定向 CppCoreCheck 保留既有告警、无新增诊断。详见[英文生命周期文档](composition.md)；完整矩阵与真实宿主仍由任务 46、47 验证。

#### 进行中：禁输入场景识别研究
- [ ] **36. 密码框与禁输入上下文识别研究（`restricted-input-context-research`）**：先研究并验证方案，再决定实现；现有 TSF 禁用标志检查不等同于所有密码框、只读框和游戏场景已覆盖。
  - 2026-11-05 阶段性结果见[研究结论与验证矩阵](restricted.md)：核实 context 级 compartment、只读状态、输入作用域及原生 EDIT 样式；新增 x64/x86 原生契约测试与显式诊断入口，复现 Shift/保留键、禁用后排队编辑及查询失败处理的缺口。本轮未改变生产输入行为，真实宿主矩阵未完成，不勾选完成。
  - 核实 `GUID_COMPARTMENT_KEYBOARD_DISABLED`、`GUID_COMPARTMENT_EMPTYCONTEXT` 的官方语义、上下文/线程作用域及宿主设置行为，研究标志缺失或查询失败时的处理。
  - 评估 Win32 `ES_PASSWORD`、TSF 输入作用域及宿主提供的其他禁输入信号的适用范围和局限，不读取密码内容，不将只读、数字或英文输入框一律判定为密码框。
  - 检查按键按下、释放、保留快捷键、焦点切换及异步 EditSession 的一致性；研究进入禁输入场景时组合串、候选窗口和 Shift 切换状态的安全清理，避免旧组合内容提交到新控件。
  - 验证禁输入时按键放行、按键日志/造词/词频学习不处理敏感输入；定义可诊断但不记录输入内容的异常处理。
  - 建立 Win32 密码/只读控件、浏览器密码框、Chromium/VS Code、WPF、UWP 和游戏聊天/非聊天场景的测试矩阵，覆盖焦点切换、标志变化、查询失败与普通输入回归。
  - 产出带官方资料依据、可复现测试结果、检测边界和实施建议的研究结论；明确无法可靠识别的自绘控件，不承诺所有宿主都可检测。

#### 待办：Windows 登录与安全桌面输入
- [ ] **61. Windows 登录与安全桌面输入支持评估（`windows-logon-secure-desktop-input`）**：独立于任务 36 的普通宿主密码框研究，先核实安全桌面契约和支持边界，再决定是否实现及恢复安全模式能力声明。
  - 已先撤下 `GUID_TFCAT_TIPCAP_SECUREMODE` 声明，并补充旧版残留分类的注册迁移及卸载清理；本次没有修改本机注册状态。后续仍需复核 `ActivateEx` 对安全模式的处理，能力声明不等于已通过登录界面验收，不仅靠注册标志宣称支持。
  - 区分用户名等普通文本目标与密码/PIN 目标；尊重宿主禁输入信号，敏感目标放行按键，不读取正文或剪贴板，不造词、不学习、不记录输入。
  - 若决定支持，设计独立受限运行模式：仅使用可信只读词库，不加载个人词库、不启动设置等外部工具、不进行跨桌面同步；验证初始化、组合清理、停用与异常处理。
  - 在具备快照及恢复通道的虚拟机中，分别验证首次登录、锁屏解锁、密码/PIN 和 UAC 凭据提示；记录 Windows 版本、实际架构、激活标志、宿主条件及脱敏结果，不在日常主力机器上先修改登录默认输入法。
  - 始终保留系统键盘布局、微软输入法或 Windows Hello 等可用的系统登录/恢复方式；未验收前不推荐将奇奇设为登录默认输入法，不将 Windows 登录支持扩展为 BitLocker 等启动前界面支持。

#### P0：词库管理与候选控制
- [x] **39. 公共跨进程变更同步机制（`cross-process-change-sync`）**：公共层及独立验证已完成；任务 40 再依次接入词库和词频文件，保留各自的数据处理逻辑，不把公共层完成等同于实际文件已经同步。
  - 普通应用可发布更新；容器类应用只接收通知并只读同步，验证通知对象及数据访问权限，避免依赖仅进程范围的 TSF Global Compartment 广播。
  - 支持按数据类型、数据源和递增版本识别变更，并通过增量记录传递新增、修改、删除或调序操作，使已同步实例仅应用变化，不必每次读取全量数据；通知唤醒与可追溯的增量记录分离，具体传输和存储方案在实现前验证。
  - 检测版本缺口、增量记录过期和基线不匹配；启动或无法增量恢复时使用全量快照，不能仅靠可能丢失的通知保证一致性。
  - 获得焦点时补偿检查版本，在新一轮输入时应用更新，避免正在输入时候选突然重排；验证并发写入、漏通知、晚启动、进程退出及容器只读同步。
  - 实现为 [`common/change_sync`](../src/common/change_sync.h)：按类型与源 GUID 绑定通道，基线 GUID 加递增序号识别变更；有界日志与目录唤醒分离，文件锁及预期版本检查防止旧实例盲写。源数据读改写回调与日志提交处于同一个排他锁内，未完成写入通过新基线要求全量恢复。
  - `Synchronize` 在组合期间返回暂缓，仅在新输入边界调用业务快照/增量回调；只有回调返回 `S_OK` 才推进本地版本。全量快照、增量正文校验和焦点/按键入口由任务 40 的适配层提供。
  - x64/x86 独立回归及两个方向的混合架构进程验证通过，包含真实 AppContainer 的通知、读取、共享读锁及拒绝写入，不只使用 Mock。测试同时覆盖记录/字节过期、漏通知、晚启动、回调失败/异常、日志损坏、源写入后提交失败与写进程中途退出。协议限制、权限及失败边界见[开发说明](dev.md#514-公共跨进程变更同步任务-39)。
- [x] **40. 词库与词频文件接入跨进程同步（`cross-process-dictionary-phrase-frequency-sync`）**：依赖任务 39，已完成词库文件与词频文件跨进程同步接入及验证。
  - 词库文件：
    - 受控编辑（`AddUserPhrase`、`AddUserEntry`、`RemoveUserEntry`、`ModifyUserEntry`）发布词条级增量（`Add` / `Remove` / `Modify`），同步更新内存覆盖层、`.qdict` 并使同路径进程缓存失效。
    - 外部编辑器修改文件时，通过 `source.meta`（文件大小、时间戳与 FNV-1a 内容哈希）检测变化；`<= 16` 条且未改变幸存词顺序的差异生成增量记录发布，否则旋转通道基线 GUID 明确触发全量快照重载。
  - 词频文件：普通应用中的 IME 动态协调写盘，容器类应用中的 IME 只接收变更并只读同步，不写盘。
    - 词频文件升级为带 64 位 FNV-1a 校验的 `QPF1` 二进制格式，同时兼容读取旧版 UTF-8 文本格式。
    - 跨进程写入前先同步远端未应用增量再发布 `Operation::Reorder` 并更新快照，避免旧实例覆盖其他实例的新调序记录。
    - 已验证普通应用之间、跨架构进程之间及普通应用到真实 AppContainer 子进程的词库与词频增量/快照同步。
- [x] **18. 图形化词库管理（`dictionary-management-ui`）**：已实现独立词库与词频管理工具 [`QiQiIMEDictManager`](../src/tools/dict_manager/CMakeLists.txt)，并支持从 `QiQiIMESettings` 一键启动；复用现有 `common`（Fluent D2D 主题、DPI、多语言、关于弹窗、ETW 结构化日志）与 `qiqi_engine`（用户词表、五笔 86 自动取码、`QPF1` 动态词频），提供词条搜索/编码反查、新增、编码/词条编辑、删除、动态词频置顶/清空、批量导入导出与分类词库筛选，所有变更通过 `change_sync` 实时同步到本机输入法实例。
- [x] **19. 候选词手动调序（`manual-candidate-order`）**：支持在组合输入态按 `Ctrl+1..9` 将对应候选词固定置顶（或在 `QiQiIMEDictManager` 中一键固定置顶/取消置顶），以 `^词条` 规则持久化于 `user_dict.txt` 并通过 `change_sync` 跨进程增量同步；手动置顶词始终排在同码候选首位，与可选动态调频（`QPF1`）明确区分且互不覆盖，默认继续保持固定词序。
- [x] **20. 候选词删除与屏蔽（`candidate-delete-blocklist`）**：支持在组合输入态按 `Ctrl+Delete`（针对当前选中候选）或 `Ctrl+Shift+1..9`（针对指定序号候选）执行快捷删除/屏蔽：自动区分删除用户词与屏蔽系统词（以 `!词条` 持久化于 `user_dict.txt` 且不编译入正向 `.qdict` 词表），并在 `QiQiIMEDictManager` 中支持筛选查看「已屏蔽词 (!)」与批量恢复/删除。
- [x] **21. 本地备份与恢复（`local-config-dictionary-backup`）**：在 `QiQiIMEDictManager` 中提供配置（`[config]`）、用户词库（`[user_dict]`，含普通词、`^置顶词`、`!屏蔽词`）和动态词频（`[phrase_freq]`）的单文件可读备份包（`.qbak`，带 64 位 FNV-1a 校验头）导出与恢复；恢复前校验文件头与校验和，并支持选择「合并模式（保留现有词条）」或「覆盖模式（完全替换）」，恢复后自动通过 `change_sync` 同步本机实例。

#### P1：拼音辅助与汉字筛选
- [ ] **22. 临时拼音输入（`temp-pinyin-input`）**：在纯五笔模式下按需进入拼音辅助输入，完成后返回五笔；与现有首码 `z` 临时英文及后续 `z` 通配查询保持兼容。
- [ ] **23. 拼音反查与五笔查询工具（`pinyin-wubi-lookup`）**：通过拼音或汉字查询五笔编码，复用现有编码查询能力，并提供字根表、口诀和可选拆分辅助。
- [ ] **24. 简繁输出切换（`simplified-traditional-output`）**：支持简体编码输出繁体及简繁切换，明确一对多转换的候选处理；不将词库含繁体字等同于转换能力。
- [ ] **25. 单字模式与字符集筛选（`single-character-charset-filter`）**：提供仅单字、常用字优先和大字符集检索选项，保持已有生僻字输入能力。
- [ ] **26. 候选匹配与排序策略（`candidate-ranking-options`）**：补充全码才显示词组、字词优先、简码置后/隐藏、累计频次或最近输入优先等可选策略；默认不改变现有盲打词序。

#### P2：输入习惯与界面设置
- [ ] **27. 自定义快捷键与标点映射（`custom-hotkeys-punctuation`）**：允许编辑模式切换、翻页、候选操作和工具快捷键及标点映射，检测冲突并提供恢复默认。
- [ ] **28. 自定义短语与便捷输入（`custom-phrases-quick-input`）**：提供独立短语编辑及快捷编码，支持多行、空格、日期/时间、数字与金额等按需扩展的输入能力。
- [ ] **29. 候选窗口个性化（`candidate-appearance-options`）**：提供字体、字号、颜色和横排/竖排设置，继续兼容系统深浅色、高对比度、多显示器 DPI 和字体回退。
- [ ] **30. 符号输入面板（`symbol-input-panel`）**：提供分类符号和常用字符选择，复用现有 Unicode 与彩色 Emoji 渲染能力。

#### P3：按产品定位评估的扩展
- [ ] **31. 智能造词与词语联想（`smart-phrase-learning`）**：评估连续输入生成临时词、用户确认后保存、误造词撤销和后续词语联想；默认关闭，避免影响固定词序及采集敏感输入。
- [ ] **32. 五笔拼音混输与纯拼音模式（`wubi-pinyin-mixed-mode`）**：在临时拼音能力基础上评估完整混输、纯拼音、简拼及模糊音，明确候选优先级与性能预算。
- [ ] **33. 98 版与其他编码方案（`additional-code-schemes`）**：评估码表、造词规则和用户词库隔离，避免仅替换主词库导致编码方案混用。
- [ ] **34. 跨设备配置与词库同步（`cross-device-sync`）**：本地备份成熟后再评估账号或用户指定存储的同步，明确隐私授权、离线行为、冲突解决和服务维护成本；与现有本机跨进程同步区分。
- [ ] **35. 皮肤扩展与附加工具（`optional-skins-tools`）**：评估可选皮肤、软键盘、输入统计及更新提示；截图、账号等级等非核心功能暂不列为必做，避免偏离轻量纯五笔定位。
- [x] **60. 调试模式与高频测试下的剪贴板打开失败专项排查（`clipboard-debug-failure-investigation`）**：已定位并修复三个根因：
  1. 远程桌面客户端（`msrdc.exe`）与 `explorer.exe` 监听 `WM_CLIPBOARDUPDATE` 并异步短暂打开剪贴板，导致紧接 `SetClipboardData` 后的下一次 `OpenClipboard` 偶发返回 `ERROR_ACCESS_DENIED (5)`；
  2. `tests/tools/log_monitor/main.cpp` 为填充诊断队列连续调用了 450 次真实剪贴板写入（已改为通过诊断通知填充，不再连续轰炸系统剪贴板）；
  3. 测试销毁窗口后未排空线程队列中的 `WM_QUIT`，导致后续弹窗立即中止返回。
  - 修复保持 `ScopedClipboard` 单次无阻塞语义，仅在高层复制入口 [`CopyTextToClipboard`](../src/common/win32_ui_util.h) 与测试读取辅助中对跨线程/跨进程 `ERROR_ACCESS_DENIED` 做有界短退避（同线程持有时立即失败返回，避免自锁等待）。
