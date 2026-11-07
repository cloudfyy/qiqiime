# P0 研究报告：禁输入准入、真实宿主生命周期与 R05 收口

日期：2026-11-07。范围：任务 36、47、R05。性质：官方契约复核、当前代码追踪与本地定向实验，**不是生产修复或真实宿主验收报告**。

## 1. 结论与任务状态

| 任务 | 研究结论 | 是否完成 |
|---|---|---|
| 36：密码框与禁输入上下文识别 | 普通按键已有部分检查，但 Shift、保留键、候选操作和排队编辑尚未共享准入；当前双架构诊断仍可复现缺口 | 否，需实施共同准入并补宿主矩阵 |
| 47：真实宿主生命周期验证 | 现有用例可继续使用；本次定向自动回归通过，不能替代实际宿主的光标、下划线和窗口观察 | 否，缺当前构建的逐用例人工证据 |
| R05：组合生命周期边界收口 | 保留既有代码与自动验收收口结论；没有证据要求继续拆分协调类，剩余重点是实际宿主验收 | 否，须审核 R05 专属清单 |

建议：**先实现任务 36 的最小共同准入和延迟执行屏障，同时准备合法 ETW 采集与真实宿主取证；再联合执行任务 47/R05 验收。** 不等待新增指标监控，不通过强制弹窗、封禁所有未知控件或继续抽象服务来代替修复。

本次未修改生产/测试源码、未注册或部署 DLL、未修改系统登录配置、未读取真实密码或剪贴板、未生成 MSI。研究完成不等于这三项 P0 已完成。

## 2. 环境与证据边界

- 源码基线：`51cefcdc2f2524d9febf2259cadcda3aa8938231`，项目版本 `0.99.2`；工作区已有规划文档变更，不作为干净提交发布。
- Windows 内核版本：`10.0.26300.0`；Release，x64 与 x86/Win32；本次 x86 配置使用 Windows SDK `10.0.26100.0`。
- 构建 `qiqi_tsf_ui_test` 及其依赖的 IME DLL，没有将产物安装到注册位置。最终两架构 DLL 文件版本均为 `0.99.2+51cefcd`，不证明真实宿主已加载该版本。
- 首次 x86 增量构建的资源标记仍为 `0.99.2+f9388ac`。[CMakeLists.txt](../CMakeLists.txt#L44) 在配置时读取提交号；本次重新配置 x86、构建并复测后标记一致。旧资源标记不能单独证明所有代码来自旧提交；首次 x86 结果不作为最终同标记对照，未借此修改构建系统。

最终研究测试 EXE 的 SHA-256：

| 架构 | SHA-256 |
|---|---|
| x64 | `309FCED13F6806390961F526BFC8750AC4C62BAB8C411ADEB920CBF56021693E` |
| x86 | `6B5C84976C8759A3E97AA8836B5EB08861010DD964A333A4FCC456334794D75B` |

证据分层：

1. **官方契约**证明接口语义，不证明各宿主如何提供信号。
2. **静态路径**证明操作顺序，不证明真实密码宿主会派发相同回调。
3. **本地实验**使用系统 TSF 私有文档、空白隐藏 EDIT 与受控 Mock 队列，不代表浏览器、WPF、UWP 或游戏验收。
4. **历史反馈**保留在 [restricted.md](restricted.md) 和 [hosts.md](hosts.md)，不改写为本轮通过。
5. **未验证/跳过**明确记录，不用“未见异常”代替通过。

## 3. 官方契约复核

下列官方页面均在本次研究中重新访问。项目建议与官方保证必须区分。

| 主题 | 官方语义与实施约束 |
|---|---|
| 禁输入 compartment | [Predefined Compartments][compartments] 将 `KEYBOARD_DISABLED`、`EMPTYCONTEXT` 定义为 context 特定的 `VT_I4`；thread manager 同名值不是官方继承规则 |
| 未设置与失败 | [GetValue][compartment-value] 的 `S_FALSE/VT_EMPTY` 表示未设置，清理后的对象可能返回 `E_UNEXPECTED`；须区别值、类型与真实失败并释放 VARIANT |
| 动态只读 | [GetStatus][status]、[TS_STATUS][status-flags] 说明只读是动态状态，`TF_STATUS` 是 `TS_STATUS` 别名；只读不等于密码，同一 context 也可能原地变化 |
| 状态通知 | [OnStatusChange][status-change] 要求重新查询实际状态；通知不是永久许可缓存，也不能在通知中强求同步写锁 |
| InputScope | [InputScope][scope-values] 明确 `IS_PASSWORD` 无安全保证且不受支持，密码字段应禁用文本服务；`IS_PRIVATE` 是私密数据，不等于密码，密码/PIN 枚举只能作附加限制 |
| 应用属性 | [GetAppProperty][app-property] 可返回不支持或未实现；预期缺失不等于运行期查询失败，也不证明目标非敏感 |
| 属性值与锁 | [属性 GetValue][property-value] 需要合法读/写锁，`S_FALSE` 也可能表示范围跨越多个属性值；应查相关选择范围，不能总查文档开头 |
| 作用域资源 | [GetInputScopes][get-scopes] 的数组由调用方 `CoTaskMemFree`；处理全部元素并校验数量/指针，不只读第一个 |
| 原生窗口 | [EDIT 样式][edit-styles] 可动态改变；[GetWnd][view-window] 成功也可返回空 HWND。仅对确认属于当前目标的原生 EDIT 使用样式，不推广到网页或自绘控件 |
| 编辑请求 | [RequestEditSession][edit-session] 区分外层请求和内层会话结果；异步排队不是完成，特定通知中同步读写请求会失败 |
| 结束组合 | [EndComposition][end-composition] 要求写锁，不释放组合对象，会移除 `GUID_PROP_COMPOSING`；结束不是删除正文 |
| 候选许可 | [BeginUIElement][begin-ui] 返回 `pbShow=FALSE` 时文本服务不显示该元素的自绘 UI；不得强制弹窗绕过 UILess/Word Copilot 的宿主许可 |

**识别上限**：窗口无关、没有可靠 context/属性/通知的自绘控件，无法只靠通用 TSF 保证识别所有密码或游戏非聊天状态。不得读取正文、用全局键盘钩子、窗口标题或进程名黑名单猜测。

## 4. 当前路径与缺口

| 编号 | 代码证据 | 当前行为 | 证据性质 |
|---|---|---|---|
| Q01 | [禁用检查](../src/ime/tsf/tip_keys.cpp#L117)、[读取辅助](../src/common/com_object.h#L179) | 辅助已诊断失败 HRESULT，但调用方把失败与默认零值合并，且存在 thread manager 回退 | 静态及错误注入 |
| Q02 | [按键](../src/ime/tsf/tip_keys.cpp#L161)、[Shift 释放](../src/ime/tsf/tip_keys.cpp#L223) | Shift 先于普通禁用检查；普通字母放行也不会自动使旧任务失效或清理现有 UI | 受控回调 |
| Q03 | [保留键](../src/ime/tsf/tip_keys.cpp#L245) | 模式、全半角、标点及快捷造词没有共同准入；造词可进入剪贴板和词库写入 | 切换键已观察；造词仅静态检查，未执行 |
| Q04 | [候选回调](../src/ime/tsf/tip_service.cpp#L50) | 点击/翻页先调用引擎，再申请宿主编辑，没有统一的禁用/私密策略 | 静态，缺定向实验 |
| Q05 | [编辑执行](../src/ime/comp/edit.h#L137)、[票据](../src/ime/comp/state.h#L31) | 已有 epoch、目标和旧清理屏障，没有复核同 context 的禁用/只读/作用域语义 | 排队后禁用实验 |
| Q06 | [候选提交](../src/ime/engine/engine.cpp#L310)、[调频](../src/ime/engine/engine_dict.cpp#L1157) | 动态调频可早于最终宿主会话，单守 `ApplyEngineResult` 太晚；容器只读路径也可能改内存排序 | 静态，本次未启用学习 |
| Q07 | [通知](../src/ime/tsf/tip_config.cpp#L222) | 当前主要处理配置、位置和系统模式，没有统一的活动 context 权限修订 | 静态，缺动态集成矩阵 |
| Q08 | [模式切换](../src/ime/tsf/tip_keys.cpp#L299) | 切英文默认可提交原始编码；系统状态、按钮和快捷键相关路径都应覆盖 | 静态，敏感目标下各入口待测 |

这些缺口不等于本次证明了真实密码泄露、写盘或跨目标提交。没有使用真实密码，也不将 Mock 观察表述为所有宿主的实际行为。

## 5. 本次验证与可复现结果

### 5.1 命令

仓库根目录依次运行，避免双架构争用既有固定测试资源：

```powershell
cmake --build --preset x64-release --target qiqi_tsf_ui_test
cmake --preset x86
cmake --build --preset x86-release --target qiqi_tsf_ui_test
.\build\x64\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics
.\build\x86\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics
ctest --preset x64-release -R '^(QiQiCompositionLifecycleTest|QiQiEtwIntegrationTest)$' --output-on-failure
ctest --preset x86-release -R '^(QiQiCompositionLifecycleTest|QiQiEtwIntegrationTest)$' --output-on-failure
```

实际执行包括首次 x86 身份检查后的重建复测；以上是最终同标记的复现流程。

[诊断实现](../tests/ime/tsf/restricted_input.cpp#L77) 使用 `TF_TMAE_NOACTIVATETIP` 的系统 TSF 私有文档与受控 owner；EDIT 为空白隐藏窗口。行为观察使用 Mock 队列、PID 唯一的测试注册表子键并清理，只保存/恢复测试线程键盘状态，不开启动态学习或剪贴板造词。

### 5.2 最终结果

| 实验 | x64 | x86（刷新身份后） |
|---|---|---|
| 10 种明确作用域加未声明作用域，分别可写/只读 | 22 组合通过 | 相同 |
| 空 compartment 与显式 `VT_I4/1` | 通过 | 相同 |
| 普通/密码/只读/数字 EDIT 样式及动态切换 | 通过 | 相同 |
| 未声明作用域的属性值 | `S_OK, vt=0` | 相同 |
| 排队编辑后设置禁用再执行 | `composition_started=1` | 相同 |
| 禁用时普通 TestKeyDown / KeyDown | `eaten=0 / 0` | 相同 |
| 上述普通按键是否触发清理 | `cleanup_requested=0, candidate_ended=0` | 相同 |
| 禁用时 Shift TestKeyUp / KeyUp | `eaten=1 / 1, mode_changed=1` | 相同 |
| 禁用时保留键切换中英文 | `eaten=1` | 相同 |
| 注入 compartment 读取 `E_ACCESSDENIED` | `test_keydown_eaten=1` | 相同 |
| 未设置 `S_FALSE/VT_EMPTY` | `test_keydown_eaten=1` | 相同 |
| `QiQiCompositionLifecycleTest` | 通过，20.96 s | 通过，26.43 s |
| `QiQiEtwIntegrationTest` | 跳过：`StartTraceW error=5` | 同原因跳过 |

解释与限制：

- 诊断退出码 0 只表示观察流程执行完成，`[OBSERVE]` 不是保护已实现的断言。
- 系统 TSF 契约通过不证明真实宿主会提供信号；隐藏 EDIT 样式通过不证明 HWND 已与真实活动 context 正确关联。
- 未设置值应支持正常输入，但必须与查询失败分开；不能简单把二者都封禁或都视作安全普通框。
- 两架构生命周期定向回归通过，不等于真实宿主通过，也不能证明历史偶发挂起根因已消失。
- ETW 因缺少 trace-control 权限跳过，不是通过；没有修改组、ACL、提升被测宿主或尝试绕过权限。后续须在合法有权限的环境补测。
- 本次没有运行完整 `QiQiTsfUiTest`、全仓测试或新增静态分析，不宣称它们本轮通过。

本轮保留的本地生命周期日志：

- [x64 逐用例日志](../build/x64/tests/ime/test-logs/composition-lifecycle-20261107T013446848Z-19748-1179546.log)。
- [x86 最终复测逐用例日志](../build/x86/tests/ime/test-logs/composition-lifecycle-20261107T013741480Z-30224-1354171.log)。
- [x64 CTest 摘要](../build/x64/Testing/Temporary/LastTest.log)、[x86 CTest 摘要](../build/x86/Testing/Temporary/LastTest.log) 可核对跳过原因，但后续运行可覆盖摘要。

这些日志不随报告提交，也不保证在其他机器存在。禁输入 stdout 另存本次会话，关键观测值已列于上表，报告不依赖机器专属会话路径。

## 6. 任务 36 实施建议（尚未实现）

### 6.1 结果模型

保留来源、阶段、HRESULT、值类型、原因和目标修订，分开表达生命周期有效性与本次操作权限。可编辑、敏感/私密及可学习性应是独立事实；例如只读且 Private 不能因为 Private 就开放写入。

| 状态 | 文本/候选策略 | 学习与词库副作用 |
|---|---|---|
| Blocked | 明确禁用、空 context、无当前目标、只读或确认密码/PIN；放行按键，不创建/更新组合 | 禁止 |
| Private | 可编辑的私密目标允许正常输入/候选及非内容性模式选择 | 禁止本次内存学习、持久化、置顶/屏蔽及快捷造词 |
| Allowed | 当前信号允许输入，不代表识别所有控件 | 遵循已有用户配置 |
| Unknown | 可选信号不支持/未声明；不能一律封禁所有正常编辑器，也不视为已确认普通框 | 建议没有明确普通判定时先禁本次学习，兼容策略由首键实验校准 |
| Pending | 所需查询尚未完成，不与“宿主明确未提供可选属性”混同 | 不读剪贴板、不学习、不复用旧目标结论 |
| QueryError | 建议当次放行并失效旧输入状态，下一边界重试；保留真实失败 | 禁止，不伪装成未设置 |

明确 Blocked 不得被后续可选查询失败覆盖成 Allowed。诊断只记录分类、阶段、原因、错误码和修订，不记录按键、正文、候选、剪贴板、输入长度或对象地址；重复错误按状态变化诊断，避免逐键刷屏。

**首键/锁时序尚待实验**：作用域读取需要合法编辑锁，不能在锁尚未取得时先把按键交给引擎再声称拦截，也不能吞首键无限等待。建议焦点/状态变更时预取，在合法按键编辑会话复核；无法及时完成时本次放行、不修改引擎或学习并记 Pending。必须先验证普通宿主首键是否退化为英文、是否可在既有合法读写会话内完成查询，再确定最终实现；本报告不把此难点表述为已解决。

### 6.2 必须一起接入的表面

1. 四个按键入口在 Shift 跟踪和引擎操作前共同快检；实际 Key 仍复核，不能把先前 Test 结果当永久许可。
2. 所有保留键、候选点击/翻页、快捷置顶/屏蔽早于读取剪贴板或引擎操作检查；纯工具/设置查看与文本操作分开，不封死全部菜单。
3. 系统中英文 compartment、语言栏和状态栏覆盖相同规则。可同步必要模式状态，但不得在受限目标顺带执行“英文上屏”提交旧编码。
4. 延迟编辑、布局及候选回调先检查票据，再检查最新权限；可能重入的 COM 查询后再核对目标/修订。
5. 为活动 context 订阅禁用和动态状态；属性/选择变化使作用域缓存失效。缓存绑定激活、context、焦点和状态修订；无可靠通知时在合法边界复核。
6. 阻断时先失效旧任务、清除 Shift 和本地引擎/UI，复用既有清理队列处理捕获的旧组合；通知中不能强求同步写锁，需要时按合法方式排队。
7. 目标已移除时不再请求写入；宿主拒绝清理时保留两个 HRESULT。完成本地失效不等于宿主属性已清除，更不能删除正文掩盖问题。

### 6.3 学习时序与所有权

调频当前可能早于最终宿主会话，因此 Private 不能只在上屏末尾禁止写盘，也不能临时改全局配置再恢复，避免 COM 重入观察到临时状态。

建议引入每次操作的能力参数，同时约束内存学习和持久化；需要最终准入确认的学习副作用应延后到该操作获准执行，无法确认则不学习。普通目标排队期间变成 Private/Blocked 时不得补做旧学习。该效果边界需单独测试，不顺便改变候选排序规则，也不承诺跨资源事务。

遵循 C++ 检查要求：

- 复用 `ComPtr`、sink 连接及 `gsl::finally`；VARIANT、作用域数组在失败/异常/提前返回时释放。公共读取新增带结果的能力，但不把所有旧调用者一并改成新业务策略。
- 保持 [CompositionLifecycleState](../src/ime/comp/state.h#L15) 不可移动及成员销毁顺序；[重置作用域](../src/ime/comp/state.h#L65) 嵌套仍失效 epoch，仅外层负责解除重置标志。
- 查询与策略用小型值结果/纯判定供各入口复用，不新增万能协调器，不跨异步保存裸 context 指针。
- 以有限枚举表达结果，检查 HRESULT/VARIANT/数组一致性，不用强制转换掩盖未知宿主值，不制造成功形状的默认值。

## 7. 任务 47 与 R05：联合验证

[激活/停用](../src/ime/tsf/tip_service.cpp#L112) 已有过渡门禁、强引用与取消清理；[状态票据](../src/ime/comp/state.h)、[编辑任务](../src/ime/comp/edit.h) 和 [清理协调](../src/ime/comp/cleanup.cpp) 已隔离旧目标/旧组合。清理中的 `finished` 或 `callback-exit` 仅表示流程退出，不能代替属性清理和结束组合的实际结果。

本次不继续拆分包装函数。任务 47/R05 可共享同轮宿主证据，但应各自审核通过条件，不互相自动勾选。

沿用 [H01–H10 与 R05 清单](hosts.md)，本次状态如下：

| 目标 | 验证重点 | 本次状态 |
|---|---|---|
| VS Code 临时编辑器 A/B | 基线、H03 首键退格、H04 快速切目标、停用与重激活 | 未执行 |
| VS Code Copilot | 两种切英文策略、后续英文/退格、光标位置 | 未执行，保留历史 P01 |
| Edge 或 Chrome 临时页面 | textarea/contenteditable、地址栏、移除/刷新、快速切换 | 未执行 |
| 记事本与真实 x86 文本应用 | 普通输入对照、焦点、停用/重激活 | 未执行；x86 测试 EXE 不冒充宿主验收 |
| Word 正文/Copilot、Explorer | 显示许可、定位、滚动与 DPI 分别记录 | 未执行，不预设同一根因 |
| WPF、UWP、游戏聊天/非聊天 | 实际信号、只读与禁输入动态变化 | 未执行，属于任务 36 扩展矩阵 |

人工操作前需明确授权、保存工作、更新实际注册构建、重启被测宿主并准备有权限的 ETW 接收方。本次没有这些真实宿主操作与观察证据。

每条记录至少包括：宿主/版本、Windows、目标类型、架构、实际 `ActivateEx` 构建身份、PID/TID、配置、用例及重复次数、时间区间、实际结果、关联活动/清理结果、丢失计数、脱敏证据位置。

按既有要求每个正常用例每宿主重复 10 次，并在立即、2 秒、10 秒观察；这些不是 TSF 完成时限。停用分支必须有停用记录，失焦不能代替。原始编码留下普通文本不自动算失败；旧任务不得重显 UI、污染新目标或清掉新组合属性。

证据不足、未覆盖、跳过和无法复现分别记录。Word Copilot 的 `pbShow=FALSE` 不能通过强制弹窗验收。

## 8. 建议批次与验收门槛

| 批次 | 内容 | 门槛 |
|---|---|---|
| A | 有类型查询结果、快检、共同按键/Shift/保留键准入；原生最小宿主验证首键与锁时序 | 普通首键不误退化、禁用不进引擎、失败有诊断；不单独发布为完整保护 |
| B | 权限修订/通知、异步复核、候选/模式入口、Private 效果控制、旧组合清理 | 与 A 合并验收全部操作表面及下列回归 |
| C | 合法权限下 ETW 集成，真实宿主任务 36/47/R05 联合取证 | 各任务按证据分别判定，未覆盖不补写通过 |

需新增或扩展的测试：

1. 两个 context 标志的零/非零/未设置/错误/错误类型；thread manager 同名值不误禁其他 context。
2. 四个按键入口、直接派发与 Test/Key 配对、Shift 期间原地禁用、四种保留键。
3. 同 context 变只读/Private/Blocked、排队前后变化、通知缺失、查询重入切目标及旧通知迟到。
4. 点击、翻页、快捷选词、置顶/屏蔽、菜单/状态栏和系统模式通知不绕过准入。
5. Private/Pending 不改变本次内存调频、磁盘、注册表镜像和同步通道；使用隔离目录与伪造剪贴板接口，不打开真实剪贴板。
6. 多作用域、空数组、数量/指针不一致、接口不支持、跨值范围、合法空值、真实查询失败和资源释放。
7. 首键、只读恢复可写、无 HWND、普通数字/英文/邮箱、禁用解除、两种切英文策略及配置未被临时改写。
8. 清理写锁拒绝、属性清理失败、宿主主动结束/移除、新组合重入、异常与对象计数回基线。
9. 诊断中无合成敏感哨兵文本、编码、候选、长度或剪贴板内容。

当前诊断只有单作用域/统一属性的 owner 和有限回调观察，不能将上述新增策略、Private、真实选择范围及首键时序视为已有覆盖。新增测试应断言目标策略，不把当前 `[OBSERVE]` 的缺口固化为兼容要求。

## 9. 最终建议

- 可以进入任务 36 的受控实施，先解决首键/锁时序实验，再将入口与延迟副作用共同验收；仅加密码样式判断不足以完成任务。
- 任务 47/R05 下一步是真实宿主证据，不是继续重构。自动测试仍是回归基础，ETW 本轮跳过须补测。
- “无正文日志”不证明无学习，“按键放行”不证明旧任务失效，“没有再现”不证明历史问题已修复。
- 不承诺通用识别所有自绘/不合作宿主；未知、失败及兼容策略应显式记录。
- 登录/安全桌面仍属于任务 61，不在本次范围内恢复安全模式声明或修改登录配置。

## 官方参考

[compartments]: https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments
[compartment-value]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompartment-getvalue
[status]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getstatus
[status-flags]: https://learn.microsoft.com/en-us/windows/win32/api/textstor/ns-textstor-ts_status
[status-change]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfstatussink-onstatuschange
[scope-values]: https://learn.microsoft.com/en-us/windows/win32/api/inputscope/ne-inputscope-inputscope
[app-property]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getappproperty
[property-value]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue
[get-scopes]: https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-itfinputscope-getinputscopes
[edit-styles]: https://learn.microsoft.com/en-us/windows/win32/controls/edit-control-styles
[view-window]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextview-getwnd
[edit-session]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession
[end-composition]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcomposition-endcomposition
[begin-ui]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfuielementmgr-beginuielement
