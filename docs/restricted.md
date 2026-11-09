# 任务 36：密码框与禁输入上下文识别研究

## 1. 状态与结论

**当前状态（2026-11-08 核对）**：任务 36 的研究与统一准入实施已按双架构自动回归及独立 `QiQiTsfTestHost.exe` 手工验证收口，见[任务记录](arch.md#当前执行优先级2026-11-07)。不外推未提供的真实浏览器、WPF、UWP 和游戏矩阵均已通过，不承诺所有密码控件可识别；这些边界保留供后续兼容性问题验证。

**动态密码模式修正（2026-11-10，1.0.2 源码）**：`v1.0.1` 标签之后补充密码/PIN 场景的临时英文模式，纳入 `1.0.2`；不能把 `v1.0.1` 的旧二进制视为已包含此修正。原有准入只清理组合、放行按键，并不改变中英文模式；现在明确识别原生 `EDIT` 密码样式或密码/PIN 输入作用域后，同步英文状态到引擎、TSF compartment 和界面，离开后恢复进入前的中英文状态，不写入用户配置。普通数字、只读、禁用及 `IS_PRIVATE` 仍按原有准入规则处理，不一概强制切英文。

焦点 context 的模式观察独立于组合生命周期：监听 `ITfTextEditSink` 的作用域更新，并仅对当前线程、已确认的原生编辑窗口监听 `EM_SETPASSWORDCHAR` / `WM_STYLECHANGED`，支持 11 号框运行中切换密码样式，不等待下一次按键。作用域读取只请求只读编辑会话，不读取正文；同步锁不可用时异步重试，陈旧回调通过 context 与修订号作废。失焦、context 移除和停用时解除观察；真实宿主是否提供有效作用域/焦点仍属于宿主契约，不能承诺任意自绘密码控件均可识别。

复用检查后，观察修订有效性统一由专用辅助函数判断；`OnPushContext` 的密码观察与组合协调共享一次所属文档查询，但仍各自校验观察修订和组合票据，避免重入后使用旧目标。回归同时验证普通 context 入栈不取消已有组合，以及后续密码作用域切换和出栈恢复。

本轮 x64/x86 Release 的 `QiQiTsfUiTest` 与 `QiQiCompositionLifecycleTest` 均通过；定向 CppCoreCheck 按既有规则排除 C26490 后，仓库源码诊断为 0。新增回归使用真实原生 `EDIT` 消息及模拟 TSF context，验证组合期间改为密码样式、五种密码/PIN 作用域、原中文/英文状态恢复、重复通知、作用域查询失败、context 栈切换、原生密码处理中的失焦、过期异步查询及订阅期间重入停用；同时检查 open/close 与 conversion compartment。未自动更新已安装 DLL、制作 MSI 或宣称真实宿主复测完成。

**2026-11-07 实施更新（批次 A + 批次 B 已落地）**：已在 [`access_policy.h`](../src/ime/comp/access_policy.h)、[`tip_keys.cpp`](../src/ime/tsf/tip_keys.cpp)、[`tip_composition.cpp`](../src/ime/tsf/tip_composition.cpp)、[`tip_config.cpp`](../src/ime/tsf/tip_config.cpp)、[`tip_service.cpp`](../src/ime/tsf/tip_service.cpp) 与 [`engine.cpp`](../src/ime/engine/engine.cpp) 中实现两层漏斗准入模型（`ContextFacts` → `AccessPolicy`）：统一覆盖四个按键入口、Shift 单击切换、全部四个保留快捷键、候选点击/翻页、模式切换、异步 `EditSession` / 布局回调复核、活动 context 的禁用 compartment 与 `ITfStatusSink` 动态通知，以及 `IS_PRIVATE` 无痕状态下的动态调频/置顶/屏蔽/剪贴板造词抑制。x64/x86 Release 下 `QiQiTsfUiTest`、`QiQiCompositionLifecycleTest`、`QiQiEngineTest` 与 `--restricted-input-diagnostics` 全部通过（排队后禁用 `composition_started=0`、禁用态 Shift/保留键/错误注入 `eaten=0`、未设置 `S_FALSE` `eaten=1`）。下方 2026-11-05 与研究阶段记录作为实施前基线保留，不将当时“未实现”的结论当作当前状态。

2026-11-05 完成官方契约核实、当前路径检查及 x64/x86 本地实验。本轮只新增测试和诊断入口，**未修改生产输入行为**。任务 36 不再暂缓，但保留未完成状态：真实浏览器、WPF、UWP 和游戏的矩阵仍需执行，再决定并验收生产实现。

同日后续已先撤下注册层的 `GUID_TFCAT_TIPCAP_SECUREMODE` 声明，并补充旧分类迁移/卸载清理；这不改变下文记录的按键实验结果，也不表示任务 36 的准入策略或任务 61 的安全桌面支持已实现。新分类状态需实际使用新构建重新注册后才应用，未在本轮自动部署。

**2026-11-06 架构同步**：R05 已把状态、票据及目标/编辑/清理协议移入 [`ime/comp`](../src/ime/comp)，COM 入口仍在服务中；下方静态路径以当前适配与组件共同理解，2026-11-05 实验数字仍为历史结果，不是本次重跑。此重构及激活取消修复**未接入统一禁输入准入**，不据此关闭任务 36。后续实施须同时覆盖[模块职责](composition.md#current-module-map)中的入口和延迟执行，不只修改原来的服务文件。

主要结论：

1. `GUID_COMPARTMENT_KEYBOARD_DISABLED` 和 `GUID_COMPARTMENT_EMPTYCONTEXT` 是 **context 级**信号，不是密码框分类器，也不是 thread manager 的标准禁输入标志。
2. `TF_SD_READONLY` 表示不可编辑，不表示敏感输入。`IS_PRIVATE` 表示私密数据，不等于密码框。数字、英文、邮箱、网址作用域不能一律禁用中文输入。
3. 宿主应在密码字段禁用文本服务。`IS_PASSWORD` 本身不提供保护，官方还明确其不受支持、可能变动；不能依赖宿主一定提供它。密码/PIN 作用域只能作为附加的明确限制信号。
4. 已知为真实原生 `EDIT` 的当前输入目标，可从窗口样式识别 `ES_PASSWORD` / `ES_READONLY`。无法把这个办法推广到网页、WPF PasswordBox、窗口无关控件或游戏自绘控件。
5. 研究阶段的旧实现仅在部分按键入口放行禁用上下文，Shift、保留快捷键、候选点击和排队编辑未共享准入策略；该缺口已由首节记录的统一准入实施覆盖，但仍不能据此承诺所有宿主敏感场景均可识别。
6. 检测结果至少要区分明确限制、正常可编辑、未声明/不支持、实际查询失败；不能把它们压成“失败即 0”的布尔值，也不能把缺少作用域当成密码或安全证明。

## 2. 官方契约与适用范围

| 信号 / 接口 | 已核实的含义 | 实现约束 |
|---|---|---|
| [预定义 Compartments][compartments] | 两个禁输入标志均为 context 特定的 `VT_I4`，非零分别表示键盘禁用、空上下文 | 从实际 `ITfContext` 查询。thread manager 上的同名 compartment 不能作为标准继承或兜底规则 |
| [ITfCompartment::GetValue][compartment-value] | 未设置返回 `S_FALSE`、`VT_EMPTY`；被清理的旧对象可返回 `E_UNEXPECTED` | 未设置不是 API 失败。保留 HRESULT 和类型，所有 VARIANT 都要释放 |
| [ITfContext::GetStatus][status] / [TS_STATUS][status-flags] | `TF_SD_READONLY` 是可在运行时改变的只读状态 | 只读时不写组合，放行宿主的选择、复制、导航等操作，不将其标记为密码 |
| [ITfInputScope][input-scope] | 通过 `GetAppProperty(GUID_PROP_INPUTSCOPE)`、属性值的 `VT_UNKNOWN` 取得接口 | 不应直接假设 `ITfContext::QueryInterface(ITfInputScope)`；不能抄用官方示例中未经类型检查的 `punkVal` 访问 |
| [GetAppProperty][app-property] / [属性 GetValue][property-value] | 支持 `S_FALSE`、未实现、不在文档栈及无有效锁等结果 | 属性读取应在合法只读/读写编辑会话、针对当前选择范围执行；不得为检测调用 `GetText` |
| [GetInputScopes][get-scopes] | 返回一个作用域数组，调用者用 `CoTaskMemFree` 释放 | 检查 HRESULT、数量与指针的一致性；处理所有作用域，不只第一个 |
| [InputScope 枚举][scope-values] | `IS_PASSWORD` 无安全保证；`IS_NUMERIC_PASSWORD`、`IS_NUMERIC_PIN`、`IS_ALPHANUMERIC_PIN`、`IS_ALPHANUMERIC_PIN_SET` 为明确密码/PIN 提示；`IS_PRIVATE` 为私密数据 | 前者建议限制输入处理；后者建议允许普通输入但禁止学习和造词，不能混为同一个“禁输入”分支 |
| [EDIT 样式][edit-styles] | `ES_PASSWORD` 为单行密码显示样式，`ES_READONLY` 为禁止编辑，`ES_NUMBER` 为数字限制 | 样式可动态改变；不能只在控件首次获得焦点时检测 |
| [ITfContextView::GetWnd][view-window] / [GetFocus][focus-window] | view HWND 可为空；`GetFocus` 对应当前线程消息队列 | 只有确认 HWND 属于当前 TSF 目标且确实是原生编辑控件时，样式才有分类意义；不要扫描别的进程、按进程名猜控件、附加别的线程输入队列或读取窗口文字 |

两个 context 标志按宿主给出的值解释；“空上下文”不等于输入框当前没有文字。不得读取或按正文长度判断，否则会误禁用正常的空白编辑框。

特别注意：本机原生 TSF 对未声明作用域的 owner 返回了 **`GetAppProperty == S_OK`，属性 `GetValue == S_OK`，`vt == VT_EMPTY`**。这是实际实验结果，不是将失败解释为成功。`SUCCEEDED(hr)` 不代表一定获得 `ITfInputScope`。未声明时应明确记录为“无作用域信息”，既不是密码，也不能证明不是密码。

游戏 UILess 激活只表示 UI 协作方式，不表示聊天目标正在接收文本。若游戏没有提供 context、禁用标志、输入作用域或可靠的焦点通知，通用 TSF 代码无法可靠区分聊天和游戏操作；不得通过截取聊天内容、全局键盘钩子或进程名黑名单猜测。

## 3. 当前实现路径与实验结果

本节描述 2026-11-05 的研究基线与实验，不是 RC7 的当前实现清单；生产实施及收口状态以第 1 节为准。

### 3.1 静态路径证据

- [`tip_keys.cpp`](../src/ime/tsf/tip_keys.cpp)：普通 `OnTestKeyDown` / `OnKeyDown` 检查禁用标志；查询还回退到 thread manager，查询失败最终按 0 处理。
- 同一文件中 Shift 路径先于普通禁用检查返回；`OnTestKeyUp` / `OnKeyUp` 依赖 Shift 跟踪状态；`OnPreservedKey` 的四种操作没有共同禁输入检查。其中快捷造词可进入剪贴板读取和词库写入路径。本轮不调用快捷造词，不接触真实剪贴板。
- [`tip_service.cpp`](../src/ime/tsf/tip_service.cpp)：候选点击先调用引擎提交；分页同样直接进入引擎。焦点/上下文移除已有生命周期处理，但不等于同一 context 原地变成只读或禁用时必然收到清理。
- [`tip_composition.cpp`](../src/ime/tsf/tip_composition.cpp) 适配 [`state.h`](../src/ime/comp/state.h) / [`edit.h`](../src/ime/comp/edit.h) 的 epoch、组合身份及任务执行屏障，防止旧目标/旧组合回调；没有在每次排队编辑执行前重新检查当前禁输入条件。生命周期有效不等于可编辑或非敏感；拒绝普通按键本身也不递增 epoch。
- [`engine.cpp`](../src/ime/engine/engine.cpp)：候选提交在 `ResolveCandidateCommit` 内已调用 `PromotePhraseForCode`。因此仅在 TSF 最终写入处检查，无法阻止此前发生的学习；准入应早于引擎处理。
- [`tip_config.cpp`](../src/ime/tsf/tip_config.cpp)：当前 compartment 事件主要处理模式与配置通知，没有为每个活动 context 连接两个禁输入标志的变更通知。

这些是待实现的路径缺口，不等于真实密码宿主一定会把相同回调派发给输入法。

### 3.2 可重复实验

代码：[`restricted_input.cpp`](../tests/ime/tsf/restricted_input.cpp)。原生契约检查进入现有 `QiQiTsfUiTest`；当前行为观察只在显式诊断模式执行，不将缺陷写成必须保留的回归断言。

```powershell
cmake --build --preset x64-release --target qiqi_tsf_ui_test
.\build\x64\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics

cmake --build --preset x86-release --target qiqi_tsf_ui_test
.\build\x86\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics

# 既有部分测试使用固定注册表测试子键，双架构回归顺序执行
ctest --preset x64-release -R '^QiQiTsfUiTest$'
ctest --preset x86-release -R '^QiQiTsfUiTest$'
```

原生契约实验使用系统 TSF 的私有文档及显式连接的 `ITfContextOwner`；通过 `TF_TMAE_NOACTIVATETIP` 不激活系统安装的 TIP。作用域和只读状态由受控 owner 提供，不代表 Windows 自动识别了某个真实密码框。范围为空，不读取文本。窗口实验仅创建隐藏、空白的标准 `EDIT`，不抢焦点，不发送真实键盘输入，不观察用户窗口。

行为观察使用现有 Mock manager/context 和受控异步队列。配置写入 PID 唯一的测试子键，退出时清理；只修改并恢复测试线程键盘状态，不改变全局键盘或用户输入法配置。测试不启用动态学习，不调用剪贴板造词。

### 3.3 2026-11-05 实际结果

环境：Windows 11 Pro，`10.0.26300`，64 位操作系统；仓库 `0.9.31` / 基线提交 `cc28be5` 加本次测试改动；Release，分别编译并运行 x64、x86 测试程序。不是已安装 DLL 的真实宿主验收。

| 实验 | x64 / x86 结果 | 证明范围 |
|---|---|---|
| 10 种明确作用域 × 可写/只读状态 | 通过，20 组合 | 原生 TSF 只读编辑会话能取回 owner 提供的作用域，`GetStatus` 独立报告只读 |
| 未声明作用域 × 可写/只读状态 | 通过，2 组合；均观察到 `S_OK + VT_EMPTY` | 未声明属性值不能强解引用为接口 |
| 每个私有 context 的两个 compartment | 通过；未设置为 `S_FALSE/VT_EMPTY`，显式设为 1 后为 `VT_I4/1` | compartment 的实际读取与设置契约；不证明宿主一定设置它 |
| 原生 EDIT 普通、密码、只读、数字四种初始样式及动态切换 | 通过 | `EM_SETREADONLY` / `EM_SETPASSWORDCHAR` 可改变样式；未验证与真实 TSF 焦点的映射 |
| 写入已排队，再设置 keyboard-disabled，执行排队回调 | `composition_started=1` | 当前异步入口不会因同一 context 新的禁用标志自动拒绝旧写入 |
| 已有组合后，禁用状态下执行普通字母 Test/KeyDown | 均 `eaten=0`；`cleanup_requested=0`、`candidate_ended=0` | 普通按键放行，但本次调用没有触发组合/UI 清理 |
| 禁用状态下 Shift 按下、TestKeyUp、KeyUp | 两种 key-up 均 `eaten=1`，实际 `mode_changed=1` | 直接派发的 Shift 路径未遵循普通按键的限制 |
| 禁用状态下保留键“切换中英文” | `eaten=1` | 当前保留键入口没有共同限制 |
| 注入 compartment 读取 `E_ACCESSDENIED (0x80070005)` | TestKeyDown 仍 `eaten=1` | 实际查询失败与 0 被合并处理；不等于真实宿主出现过该错误 |
| 注入未设置 compartment `S_FALSE/VT_EMPTY` | TestKeyDown `eaten=1` | 缺少可选值仍可正常输入；与失败应在结果模型中区分 |

诊断退出码 0 仅表示实验执行完成；输出中的 `[OBSERVE]` **不表示保护已经实现或验收通过**。本轮契约测试核查了 COM 对象计数回到基线，接口、sink、窗口、VARIANT 和作用域数组均通过现有 RAII / 作用域清理机制释放。

验证记录：x64/x86 Release 构建、上述显式诊断及 `QiQiTsfUiTest` 均通过。对新增源文件执行 CppCoreCheck，并以关闭增量重编译的直接编译分析复核，新增源文件无诊断；现有头文件、SDK 和第三方 GSL 头的告警未在本任务修改。没有制作 MSI，也没有注册/部署新 DLL。

## 4. 研究阶段推荐实施方案

以下保留实施前的设计建议，当前已落实的两层漏斗及入口覆盖见第 1 节；不再作为“尚未接入生产”的状态声明。

### 4.1 分类与错误策略

建议在 TSF 层收敛一份带原因、查询阶段、HRESULT 的结果，区分：

- **Blocked**：明确禁用、空 context、无当前目标、只读或已确认的原生密码目标、明确密码/PIN 作用域。原因中必须区分“不可编辑”和“敏感”，不读取任何正文。
- **Private**：`IS_PRIVATE`。允许组合和提交，但禁止动态学习、快捷造词、候选置顶/删除等持久化操作；不把私密框一律变成英文框。
- **Allowed**：当前可编辑且没有明确限制；只表示当前信号允许输入，不承诺安全识别了所有控件。
- **Unknown**：宿主没有提供作用域、窗口为空或不支持接口。对于可选信号不能一律封禁，否则许多正常编辑器会失去中文输入；也不能把 Unknown 记录为“确认普通框”。
- **QueryError**：必需的 context/禁用状态查询发生实际错误。建议当次事件放行、清理本地旧状态并使排队任务失效；保留真实错误码，不沿用上一个目标的 Allowed 结果。可选作用域的 `S_FALSE` / `E_NOTIMPL` / `VT_EMPTY` 与运行期访问失败应分开。

此策略是实施建议，不是本轮已经改变的行为。对于可选属性的实际运行期错误，建议暂停当次输入处理并在下一输入边界重试；与预期“不支持”区分，避免永久禁用正常宿主或静默带错继续处理。

不要每个测试回调都同步申请编辑锁。context compartment / `GetStatus` / 可确认的本线程原生窗口信号可先做快速检查；作用域通过合法的只读会话获取。缓存必须绑定 activation、context、焦点及状态修订，首次尚未完成检查时不要把前一个 context 的结论套到新目标。写入会话内仍需复核；context 身份未变化不代表语义未变化。

工程落地时采用**“两层漏斗”**收敛复杂度（详见 [P0 研究报告 6.1 节](p0-research.md#61-结果模型)）：底层采集函数将 4 类信号与错误码汇总为 `ContextFacts`（保留供脱敏日志诊断），顶层纯函数 `EvaluatePolicy(const ContextFacts&)` 按 `QueryError → Blocked → Pending → Allowed/Private` 优先级短路归约为 `AccessPolicy { can_input, can_learn, should_reset_active }`，使按键、异步编辑和引擎学习入口只需判断布尔权限。

### 4.2 需要同时接入的表面

1. **按键前置准入**：覆盖 TestKeyDown、KeyDown、TestKeyUp、KeyUp；Shift 不再在检查前建立跟踪状态。受限状态四个入口均返回 `eaten=FALSE`。
2. **保留快捷键与候选 UI**：所有保留键检查早于剪贴板打开、引擎处理、造词和配置变更；鼠标选词、分页及候选快捷置顶/屏蔽使用相同策略。不要只在最终 `ApplyEngineResult` 拦截。
3. **焦点与同目标状态变更**：为活动 context 连接禁用标志事件，处理只读/作用域变更通知；焦点、push/pop、失活时断开旧 sink。处理没有通知的宿主，在实际输入边界重新检查。
4. **组合和排队任务**：先失效 epoch、清除 Shift 和本地引擎/UI，再按既有生命周期规则结束旧组合。只清理原 context/原 composition，不把旧文本提交到新控件，不主动删除宿主已保留为普通文本的内容。context 已移除时不再请求编辑；宿主拒绝清理写锁时保留两个 HRESULT，不假装宿主清理成功。
5. **异步执行与重入**：排队写入、布局、候选回调执行前复核最新分类；COM 调用可能重入，查询完成后再次检查 epoch/目标身份。不要仅比较指针后继续写入。
6. **学习和日志**：准入早于 `ProcessKey` / `CommitCandidate` 及频次持久化。Private 状态需要独立的“本次禁止学习/变更词库”策略，不能临时改全局配置。诊断只记录分类、阶段、HRESULT、身份修订与活动关联，不记录按键值、原始编码、候选正文、剪贴板、密码或输入长度；避免每个按键重复刷屏。

建议先实现共同准入与生命周期屏障，再增加原生窗口和作用域补充识别；同一批回归必须同时验证敏感路径被阻止以及普通输入没有被误禁用。原有 thread manager 同名 compartment 兜底的删除属于行为变化，应有“thread 上设置同名值不会禁用另一个正常 context”的定向测试。

## 5. 验证矩阵与未覆盖范围

下表保留研究阶段证据与后续真实宿主验证计划，**不是当前实现或已通过清单**；任务 36 的实施与独立手工验证已按第 1 节收口。所有输入均使用临时空白页面/控件和固定非敏感测试串，不使用实际账号、密码、剪贴板或业务文档。

| 场景 | 本轮证据 | 仍需真实宿主验证 |
|---|---|---|
| Win32 EDIT 普通 / 密码 / 只读 / 数字 | 原生样式与动态变更通过；独立原生 TSF 属性契约通过 | 将实际焦点 HWND、context、compartment 关联后验证，不能拼接两种独立实验当作集成通过；x64/x86 |
| 浏览器 password / readonly / disabled / 普通 input、textarea、contenteditable | 未覆盖 | Edge、Chrome 版本/架构；同页目标切换、动态 type/readonly 变化、页面导航销毁 |
| Chromium / VS Code | 仅有任务 47 的一般生命周期反馈，不能复用为密码验证 | 普通编辑器不误禁；受限目标有无派发；在同一 context 或同一 HWND 中切换输入目标 |
| WPF TextBox / PasswordBox / IsReadOnly | 未覆盖 | 实际支持的禁用信号、作用域及焦点路径，不按顶层 HWND 推断 PasswordBox |
| UWP / AppContainer | 未覆盖；已有同步通道容器测试不算本任务证据 | 记录实际应用、版本、架构及令牌条件；属性查询权限/缺失、密码和普通控件回归 |
| 游戏聊天 / 非聊天 / UILess | 未覆盖 | 指定实际游戏与控件，检查聊天开关和禁用信号；无法提供信号的自绘场景记录为不可可靠识别 |
| 缺失标志、无作用域、查询失败 | 私有 TSF 的未声明值与合成 `E_ACCESSDENIED` 已覆盖 | 实施分类后补齐错误类型、空接口、多个/未知作用域、类型不匹配、断栈与无锁路径，不能用 Unknown 假装安全 |
| 已排队编辑、同目标变成受限、焦点重入 | 当前合成实验确认一条排队写入缺口 | 实施后对所有键盘/候选入口、布局回调、清理失败及状态恢复建立确定性时序回归，再执行人工矩阵 |

真实验收至少记录宿主名称/版本/架构、操作系统、实际 DLL 版本、目标类型、各信号/查询 HRESULT、重复次数、时间区间及脱敏日志。每项分别标记通过、失败、未覆盖或阻塞。密码框若完全不派发 TSF 回调，应记录为宿主禁用路径，不能伪造一次 IME 检测成功。

**研究阶段完成边界（2026-11-05）：**当时只交付可重复实验和实施建议，未实现新的密码保护，不能据此勾选实施完成。后续生产实施与既定验证已按第 1 节收口；无论任务状态如何，仍不宣称“所有密码框已全面支持”。

[compartments]: https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments
[compartment-value]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompartment-getvalue
[status]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getstatus
[status-flags]: https://learn.microsoft.com/en-us/windows/win32/api/textstor/ns-textstor-ts_status
[input-scope]: https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nn-inputscope-itfinputscope
[app-property]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getappproperty
[property-value]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue
[get-scopes]: https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-itfinputscope-getinputscopes
[scope-values]: https://learn.microsoft.com/en-us/windows/win32/api/inputscope/ne-inputscope-inputscope
[edit-styles]: https://learn.microsoft.com/en-us/windows/win32/controls/edit-control-styles
[view-window]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextview-getwnd
[focus-window]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus
