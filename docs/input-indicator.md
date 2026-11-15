# 任务栏输入指示器与品牌图标研究

研究日期：2026-11-15。本文记录源码对比和后续验证顺序，不代表修复已完成。
参考项目以研究时的 `master` 分支为准，链接内容可能随上游更新。

## 1. 问题与边界

用户反馈浅色任务栏中的奇奇图标不如微软五笔清晰；另一次出现仅剩“奇”
品牌图标、没有“中/英”状态图标的情况，`Win+Space` 切换列表中的品牌图标
也存在浅色背景下的可读性问题。

必须区分四条路径：

| 路径 | 用途 | 处理边界 |
|---|---|---|
| TSF 中/英状态图标 | 显示当前输入模式 | 由语言栏项提供图标、状态和更新通知 |
| 主题颜色适配 | 深浅主题下保持可读性 | 不等于中英文模式切换 |
| 注册的“奇”品牌图标 | Windows 输入法识别及切换列表 | 注册时提供固定图标文件路径和索引 |
| 输入法独立托盘图标 | 独立程序的状态入口 | 不等于 Windows 的 TSF 输入指示器 |

当前方向是继续研究官方式自动黑白适配，暂不改变品牌配色。
状态图标消失尚无足够运行证据，不能直接归因于资源、缓存或注册失效。

## 2. 研究时的奇奇实现

- [语言栏项](../src/ime/ui/fltbarbtn.cpp)使用 `GUID_LBI_INPUTMODE`，
  未启用 `TF_LBI_STYLE_TEXTCOLORICON`。`GetIcon` 生成“中/英”图标，
  [主题工具](../src/common/theme.h)按任务栏主题选择黑色或白色，高对比时使用系统颜色。
- `Show` 当前不保存可见状态，`GetStatus` 返回零；`UpdateState` 通知
  `TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP`。这些是待核查点，
  不是已经证明的消失根因。
- [注册实现](../src/ime/tsf/register.cpp)提供 DLL 中固定的品牌图标索引。
  [品牌资源](../data/qiqiime.ico)是透明背景、白色字形和半透明黑色描边，
  浅色背景下依赖描边呈现；这与中/英状态图标的颜色选择是两回事。

## 3. CxxIME 对比

- [语言栏实现](https://github.com/deanxyuan/cxx-ime/blob/master/tsf/src/language_bar.cpp)
  根据中文、英文和 CapsLock 状态选择固定 ICO，返回复制的图标，
  未使用 `TF_LBI_STYLE_TEXTCOLORICON`；不能据此称其具备官方式主题自动着色。
- 维护显示/隐藏状态，并按变化通知状态、图标及提示等信息。
- [输入指示器控制器](https://github.com/deanxyuan/cxx-ime/blob/master/tsf/src/input_indicator_controller.cpp)
  有移除、重新添加语言栏项以及完整刷新机制。
  [会话实现](https://github.com/deanxyuan/cxx-ime/blob/master/tsf/src/text_service_session.cpp)
  在刷新失败时安排有上限的延迟重试。
  可参考其注册状态、失败诊断和回调生命周期管理，但不应盲目反复 Remove/Add。
- [注册实现](https://github.com/deanxyuan/cxx-ime/blob/master/tsf/src/register.cpp)
  还绑定 legacy IMM HKL，与奇奇纯 TSF 路径不同；不为图标问题引入该架构
  或私有宿主兼容逻辑，也不照搬全部 capability 声明。

## 4. Rime / 小狼毫对比

此处研究的是 Rime 的 Windows 前端小狼毫，而非 Rime 核心引擎。

- [语言栏实现](https://github.com/rime/weasel/blob/master/WeaselTSF/LanguageBar.cpp)
  根据中英文模式选择固定资源或配置指定的图标；未使用
  `TF_LBI_STYLE_TEXTCOLORICON`，维护隐藏/禁用状态并发送更新通知。
- [配置加载](https://github.com/rime/weasel/blob/master/RimeWithWeasel/RimeWithWeasel.cpp)
  按明暗主题选择 `style/color_scheme` 或 `style/color_scheme_dark`。
  图标另从 `schema/icon`（或 `schema/zhung_icon`）、`schema/ascii_icon`
  等键加载；该加载分支没有按明暗主题选择两套图标。
  候选窗口配色切换不能视为任务栏图标自动黑白切换的证据。
- [独立服务托盘图标](https://github.com/rime/weasel/blob/master/WeaselServer/WeaselTrayIcon.cpp)
  按禁用、中英文状态和图标路径刷新，合并异步更新；它不是 TSF 输入指示器。
- [品牌注册](https://github.com/rime/weasel/blob/master/WeaselTSF/Register.cpp)
  同样提供固定资源索引，未发现明暗品牌图标的独立注册参数。

## 5. 后续顺序与验收

1. **先定位中/英状态图标消失。** 核对 AddItem/RemoveItem、sink 连接与断开、
   显示状态、更新通知及实际 HRESULT；复现失焦、切换输入法和重新激活。
   只记录生命周期及失败信息，不记录用户输入。按证据修复，保持所有权和回调安全。
2. **再验证状态图标自动黑白。** 微软公开的
   [`TF_LBI_STYLE_TEXTCOLORICON`](https://learn.microsoft.com/en-us/windows/win32/tsf/tf-lbi-style--constants)
   要求单色图标，黑色像素由宿主转换成主题文字颜色；
   [`GetIcon`](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nf-ctfutb-itflangbaritembutton-geticon)
   的返回图标由调用方释放。不能只给当前 ARGB 图标加标志就宣布完成。
   需验证真正单色格式，以及真实 Windows 任务栏上的浅色、深色、高对比、
   主题即时切换、DPI 和中英文切换效果；像素测试不替代 Shell 实机验收。
3. **最后单独验证品牌图标。** 公开 RegisterProfile/AddLanguageProfile 参数
   提供固定图标路径和索引，目前未确认能让品牌图标获得微软式自动着色的公开机制。
   分别验收任务栏和 `Win+Space` 列表，不把状态图标修复外推为品牌图标已修复。
   若公开方案不能满足要求，再讨论替代视觉设计；本轮不改变品牌配色。

研究阶段仅做源码对比；后续第一阶段实现和验证记录见下节。

## 6. 第一阶段实现（2026-11-15）

已针对可从代码确认的问题补充实现，尚未据此确认截图中的消失根因：

- `Show` 保存可见状态，`GetStatus` 返回对应的 `TF_LBI_STATUS_HIDDEN`；
  可见状态变化通知 `TF_LBI_STATUS`，重复相同请求不重复通知。
  更新失败保留实际 HRESULT，已更新的状态仍可通过 `GetStatus` 查询。
- 每次激活创建新的语言栏项。停用时先断开旧项的服务回调和 sink，再尝试移除；
  即使宿主移除失败并继续持有旧项，旧项也不能再调用已经停用或销毁的服务。
  菜单通过语言栏项转发当前回调，避免菜单持有过期的服务回调。
- 通知期间以 COM RAII 保持语言栏项及 sink 存活，允许回调中取消订阅、
  停用服务或释放外部引用。查询 sink 接口失败时不再改写原始 HRESULT。
- 日志增加语言栏项编号，以及注册/移除、状态查询、显示请求、sink 和通知标志。
  见[任务栏输入指示器诊断](log.md#taskbar-input-indicator)。
  语言栏注册仍是可选功能：失败会记录诊断，不改变现有输入服务激活结果。

回归覆盖显示/隐藏、中英文状态更新、通知失败、通知期间取消订阅和释放引用、
注册失败后重新激活、移除失败时旧项隔离及真实 ETW 失败码输出。
真实任务栏上的消失问题仍待复现日志确认；本阶段未改品牌资源、
未启用 `TEXTCOLORICON`，也未加入自动重新注册重试。

验证结果：x64 与 x86 Release 构建均通过；两种架构的 `QiQiTsfUiTest`、
`QiQiCompositionLifecycleTest` 均通过。x64 首轮完整回归曾在既有 Shift
松键断言失败，复核未再次出现；保留失败日志，不据此断言根因。
x64 `QiQiEtwIntegrationTest` 因 `StartTraceW error=5` 跳过，不记为通过；
完整回归中的 `service.focus-performance`（含新增语言栏 ETW 诊断断言）已通过。

## 7. 第二阶段实现：单色状态图标

经用户确认，接入单色状态图标并运行自动测试；安装后的真实任务栏效果由用户验收。
这不是品牌图标变更，也不代表已经获得与微软输入法完全相同的绘制效果。

- 语言栏项启用 `TF_LBI_STYLE_TEXTCOLORICON`，不再根据注册表主题为状态图标着色。
  黑色字形由 Windows 按所选主题文字颜色处理。
- `GetIcon` 返回真正的单色 HICON：`hbmColor == nullptr`，
  一位 `hbmMask` 为图标高度的两倍，上半为 AND，下半为 XOR。
  透明区域为 AND=1/XOR=0，字形为 AND=0/XOR=0；掩码完整初始化，
  不产生白色背景或反相区域。继续复用字体创建和 GDI/COM RAII。
- 单色字形使用非抗锯齿字体光栅化，这是格式切换的视觉取舍；
  不把二值掩码当作仍有 ARGB 抗锯齿效果。
- 自动回归验证单色格式、系统小图标尺寸、透明边角、可见字形、
  全零 XOR、中文与英文图形不同，以及不同应用/任务栏主题下生成相同掩码。

用户实机验收：在浅色、深色、高对比下分别观察中/英状态图标，切换主题后
不重启宿主检查是否及时变色；切换输入法、失焦和重新激活后检查可见性。
在不同 DPI 显示器上检查清晰度及裁切。若宿主不按契约着色或效果不理想，
保留第一阶段日志定位，不将自动测试结果记为真实 Shell 验收通过。
“奇”品牌图标及 `Win+Space` 列表仍待第三阶段独立研究，本轮不改资源和注册。

自动验证结果：x64、x86 Release 均构建通过，两种架构的完整 `QiQiTsfUiTest`
均通过，`git diff --check` 通过。上述结果包含单色掩码与原有品牌资源回归，
不替代用户安装后的任务栏主题着色验收。
