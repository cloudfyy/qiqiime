# 任务栏输入指示器与品牌图标研究

研究日期：2026-11-15。本文按阶段记录源码、资源和验证结果，当前品牌实现见第 13 节；
此前章节保留各阶段的研究结论，不代表全部兼容性验收已完成。
参考项目使用研究时的 `master` 或明确标注的版本标签，非固定提交链接可能随上游更新。

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

当前品牌按官方 `IME brand icons` 的白底黑字方框建议修正，不再把自动反色当作
品牌规范的承诺。“中/英”模式图标按官方定义固定为白字加黑描边；任务栏主题事件
只保留作为更新通知，不再选择图标颜色。
状态图标消失尚无足够运行证据，不能直接归因于资源、缓存或注册失效。

## 2. 研究时的奇奇实现

- [语言栏项](../src/ime/ui/fltbarbtn.cpp)使用 `GUID_LBI_INPUTMODE`，
  最初未启用 `TF_LBI_STYLE_TEXTCOLORICON`。`GetIcon` 生成“中/英”图标，
  [主题工具](../src/common/theme.h)按任务栏主题选择黑色或白色，高对比时使用系统颜色。
- `Show` 当时不保存可见状态，`GetStatus` 返回零；`UpdateState` 通知
  `TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP`。这些是待核查点，
  不是已经证明的消失根因。
- [注册实现](../src/ime/tsf/register.cpp)提供 DLL 中固定的品牌图标索引。
  当时的[品牌资源](../data/qiqiime.ico)是透明背景、白色字形和半透明黑色描边，
  浅色背景下依赖描边呈现；这与中/英状态图标的颜色选择是两回事。

## 3. 外部实现对照

- 研究时观察到的外部语言栏实现根据中文、英文和 CapsLock 状态选择固定 ICO，返回复制的图标，
  未使用 `TF_LBI_STYLE_TEXTCOLORICON`；不能据此称其具备官方式主题自动着色。
- 维护显示/隐藏状态，并按变化通知状态、图标及提示等信息。
- 输入指示器控制器有移除、重新添加语言栏项以及完整刷新机制。
  会话实现会在刷新失败时安排有上限的延迟重试。
  可参考其注册状态、失败诊断和回调生命周期管理，但不应盲目反复 Remove/Add。
- 注册实现还绑定 legacy IMM HKL，与奇奇纯 TSF 路径不同；不为图标问题引入该架构
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
  用户提供的 0.17.4 版本进一步核实为固定黑底白字设计，见第 12 节。

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

本节为此前尝试的历史记录；用户实机反馈及当前替代实现见第 8 节。

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

## 8. 实机反馈后调整：主动着色与主题刷新

用户反馈浅色任务栏上的“奇”不清晰、深色任务栏上的“中”颜色不正确，
并且切换系统主题后图标没有自动更新。因此第 7 节的单色格式测试通过，
不能视为宿主自动着色方案在实际 Shell 中可用。

经用户确认，本轮只修复“中/英”状态图标及主题刷新，“奇”品牌图标保持不变：

- 移除 `TF_LBI_STYLE_TEXTCOLORICON`，复用透明 ARGB 字形生成逻辑，
  按 `SystemUsesLightTheme` 主动选黑/白，不跟随应用主题或输入法自身的外观设置。
  系统高对比开启时继续使用 `COLOR_BTNTEXT`。
- sink 连接期间创建不显示、不激活的顶层工具窗口，接收
  `WM_SETTINGCHANGE`、`WM_THEMECHANGED` 和 `WM_SYSCOLORCHANGE`。
  不使用收不到系统广播的 message-only 窗口，也不依赖候选窗或状态栏是否显示。
- 合并相邻通知并投递到所属线程，避免在系统主题广播内同步回调宿主。
  随后以 `TF_LBI_ICON` 请求宿主重新取图；`GetIcon` 每次读取当前任务栏颜色，
  不需要切换中英文、重新激活输入法或重新注册语言栏项。
- 取消订阅、停用和最终释放时销毁监听窗口；回调期间保持项和 sink 存活，
  允许宿主在主题更新通知内取消订阅、停用或释放外部引用。
  窗口创建、消息投递及 sink 通知失败保留诊断，通知失败后仍可响应后续主题变化。
- 语言栏 COM 对象显式禁止复制和移动，取消订阅与状态更新保持无异常契约。
  基类同样禁止赋值，派生类已删除的赋值声明采用与候选窗相同的 C26456 局部例外。
  宿主 `ITfLangBarItemSink::OnUpdate` 按 COM 约定以 HRESULT 报错；SDK 未声明
  `noexcept`，因此只在这一外部调用处沿用仓库的 C26447 单行分析例外，
  仍保留真实失败码和诊断，不扩大全局屏蔽规则。测试 sink 的 `std::function`
  回调可抛 C++ 异常，不为该测试方法声明 `noexcept`。
- 自动回归覆盖黑/白预乘 ARGB、透明边角、中英字形差异、应用/任务栏主题分离、
  主题消息合并、失败后的后续通知、取消订阅与重连，以及通知回调内释放对象。

仍需安装后的实机验收：系统浅色、深色、高对比之间切换，不输入、不切换中英文、
不重启宿主，观察任务栏“中/英”是否及时变色；同时验证中英文切换、失焦、
重新激活和不同 DPI 下的清晰度。自动测试不代替此项验收。

“奇”仍使用固定白字描边资源，浅色背景上的可读性问题尚未解决，不能随主题
自动变色；本轮不改资源、不改变品牌设计，也不承诺 `Win+Space` 列表已改善。

自动验证结果：x64、x86 Release 的 DLL 和 TSF/UI 测试目标构建通过，
两种架构的完整 `QiQiTsfUiTest` 均通过，`git diff --check` 通过。
未安装、注册输入法或修改系统主题；上述实机验收仍待更新后的实际宿主验证。

## 9. 品牌自动黑白适配：公开接口探测

用户安装后反馈：深色下状态图标颜色已正确，但笔画偏粗；浅色下“奇”仍不清晰。
当前优先目标是品牌“奇”浅色黑字、深色白字，不以固定蓝色或强化描边替代；
笔画粗细另行处理。本节研究未修改品牌资源或正式运行时代码。

### 9.1 注册资源与系统语言栏项不是同一契约

- 微软的 [IME 要求](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-branding-icon)
  明确区分从 DLL 注册资源读取的品牌图标，以及由 `GUID_LBI_INPUTMODE`
  提供的模式图标。
- [`RegisterProfile`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-registerprofile)
  提供单个图标路径和索引，没有公开的浅色/深色图标参数。
  `TEXTCOLORICON` 是语言栏项样式，不能直接套用于注册的资源图标。
- 另一公开入口是 `GetItem(GUID_TFCAT_TIP_KEYBOARD)` 后查询
  `ITfSystemLangBarItem`，调用
  [`SetIcon`](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nf-ctfutb-itfsystemlangbaritem-seticon)。
  其文档契约是修改系统语言栏菜单，不保证现代任务栏或 `Win+Space` 列表跟随。
- [`SetIconMode`](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/nf-ctfutb-itfsystemdevicetypelangbaritem-seticonmode)
  只能选择默认图标或 profile 图标，不是深浅主题选择器，宿主也可不支持。

### 9.2 隔离与真实任务栏探测结果

先在临时 x64 进程中创建独立 TSF 线程管理器，不获取焦点、不注册 profile：

- `GetItem`、两个系统项接口查询及 `GetIconMode` 均返回 `S_OK`，初始模式为 0。
- `GetIcon` 返回 `S_OK` 且图标为空；按其公开契约，这是“没有图标”，不是 API 失败。
- `SetIcon` 设置临时系统信息图标返回 `S_OK`，随后取回图标的实际绘制像素与
  测试图标一致；恢复原来的空图标同样返回 `S_OK`，取回结果也确认已还原。
  这只验证本地语言栏对象，不证明任务栏绘制成功。

经用户许可，再打开临时测试窗口，以 `TF_IPPMF_FORPROCESS` 激活已安装的奇奇。
日志确认活动 profile 是奇奇，`SetIcon`、`SetIconMode(0)`、
`SetIconMode(TF_DTLBI_USEPROFILEICON)` 均返回 `S_OK`。
用户分别将测试输入框置于前台观察，三种情况下任务栏都仍是原来的“奇”，没有
变成临时信息图标。交互过程中还出现过 `GetIcon` 成功但再次返回空图标的情况，
不据此推断具体覆盖时机或缓存根因。

因此，这条公开系统语言栏接口路径在本机本次实测中未实现任务栏品牌替换，
不接入正式代码，也不把成功 HRESULT 记为品牌适配通过。测试后恢复图标及模式，
恢复调用均返回 `S_OK`，并关闭了临时窗口。未更改系统主题、默认输入法或 profile
注册，没有安装新包或重启系统进程。以上结论不外推为所有 Windows 版本都不支持。

### 9.3 动态资源 DLL：可生成，但刷新链路尚未验证

可以用有效的资源专用 PE 模板，通过
[`BeginUpdateResourceW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-beginupdateresourcew)、
[`UpdateResourceW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-updateresourcew)
和 `EndUpdateResourceW` 写入图标组与各尺寸图像。这修改的是磁盘资源文件，
不是向输入指示器提交内存 `HICON`；仅生成资源不能保证 Shell 重新读取。

该路线尚需验证注册路径、写权限、文件占用、AppContainer 可读性，以及无需重启
或重新选择输入法的可靠刷新机制。不直接改正在使用的输入法 DLL，不绕过权限，
也不把重新注册 profile 或反复重启宿主当作主题即时刷新方案。
本轮只核实了资源更新 API 的能力与边界，未修改已注册图标文件来进行这项实验；
“奇”自动黑白切换仍未解决。

## 10. 本机系统输入法图标资源对照

2026-11-15，在本机 x64 Windows 上只读核对注册信息和图标资源。
模块仅以数据/图像资源方式加载，不执行其入口；未切换主题、修改注册或安装软件。
以下是静态资源检查，不是实际 Shell 的主题切换验收。

### 10.1 微软拼音和五笔的注册图标

按注册的 `Description` 识别简体中文 profile，结果为：

| 输入法 | 注册的 IconFile | IconIndex | 提取后匹配的图标组 |
|---|---|---:|---:|
| Microsoft Pinyin | `%SystemRoot%\System32\InputMethod\Shared\ResourceDll.dll` | 0 | 资源 ID 1 |
| Microsoft Wubi | 同上 | 1 | 资源 ID 2 |

通过 `ExtractIconExW` 按注册索引提取，并与按资源 ID 加载的 16px 图标逐像素指纹
核对，确认这里的索引 0/1 对应资源 ID 1/2，不能把索引直接当作资源 ID。

该 `ResourceDll.dll` 版本为 `10.0.26100.1`，包含 5 组图标。
注册指向的两组分别是**白色方框背景上的黑色“拼”和“五”**，不是一对深浅主题版本。
每组都有 16、20、24、32、40、48px 的 8bpp 和 32bpp 图像。
16px 的两组图标均为带颜色位图的 HICON，不是纯 AND/XOR 单色 HICON。

### 10.2 系统状态图标确实另有黑白两套

本机 `%SystemRoot%\System32\InputMethod\CHS\ChsIME.exe`
版本为 `10.0.26100.9278`，包含 27 组图标。其中明确可见：

| 资源 ID | 字形 | 颜色 |
|---:|---|---|
| 41 / 42 | 中 | 透明背景黑字 / 白字 |
| 43 / 44 | 英 | 透明背景黑字 / 白字 |
| 45 / 46 | 全角实心圆 | 黑 / 白 |
| 47 / 48 | 半角月牙 | 黑 / 白 |
| 49 / 50、51 / 52 | 中文、英文标点 | 黑 / 白 |
| 53 / 54、55 / 56 | 简、繁 | 黑 / 白 |
| 57 / 58 | 设置齿轮 | 黑 / 白 |

这些组各提供 16、20、24、32、40、48px 的 32bpp 透明图像。
例如资源 41/42 的 16px 图像都覆盖 74 个像素，alpha 分布一致，
但字形颜色一黑一白；这是独立预制的颜色版本，不是“同一张白图自动变黑”的证据。
这些“中/英”等**模式图标**也不能当作已经找到了“拼/五”**品牌图标**的主题路径。

修正前奇奇的注册资源只有一组白色“奇”加半透明黑描边：16px 检查中有 55 个
完全不透明白像素、没有完全不透明黑像素。这解释了其浅色背景上的描边式外观，
但本轮不据此调整已要求暂缓的笔画粗细。

### 10.3 当前能得出的结论

- 可以借鉴系统为模式图标提供两套颜色、逐尺寸设计的资源做法。
- 系统品牌注册本身仍是单个固定路径和索引；本次没有找到可公开声明的
  “品牌图标按主题选择资源 ID”参数。
- 在已检查的两个模块中，注册的“拼/五”方框资源与“中/英”黑白资源分属不同路径。
  系统实际显示的品牌样式如何选择、是否还有其他系统资产或内部处理，仍未证实；
  不能仅凭资源存在就宣称内置输入法的主题机制已查清。
- 因此不把“额外嵌入两套资源”或“动态生成资源 DLL”直接视作自动适配已实现，
  也不复制系统专用资源、改写微软文件或加入私有 Shell 钩子。

临时检查程序以 `/W4 /WX` 编译通过，资源枚举和按注册索引提取均完成。
研究源文件、像素统计及静态对照图保留在会话目录，不加入产品安装包。

## 11. 透明黑白“奇”实验资产

按“先把图标做出来”的要求，当时先准备两份原创透明字形资源，未改变品牌注册。
这两份不是第 13 节采用的白底黑字品牌方案：

| 文件 | 使用背景 | 字形颜色 |
|---|---|---|
| [qiqiime-light.ico](../data/qiqiime-light.ico) | 浅色 | 纯黑 |
| [qiqiime-dark.ico](../data/qiqiime-dark.ico) | 深色 | 纯白 |

两份均为透明背景、无描边的 32bpp 图标，包含
16、20、24、32、40、48、64、96、128、256px 十种尺寸。
复用现有“奇”的字体、Bold 字重及 80% 字形比例，本轮不另调笔画粗细。
两份图标共享覆盖率，显式写入黑/白颜色通道，避免 GDI+ 混合造成白色边缘通道舍入；
每个尺寸的 alpha 平面完全一致。

通过[现有生成脚本](../scripts/generate_monitor_icon.ps1)重建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\generate_monitor_icon.ps1 -ImeOnly
```

新增 `QiQiImeIconAssetsTest`，验证全部尺寸、图像格式、纯黑/白通道、透明外边缘、
可见且抗锯齿的字形，以及两份图标逐像素相同的 alpha。
CTest 与 Windows PowerShell 直接运行均通过。
该资产准备阶段未改变 `qiqiime.ico` 和三个工具图标的内容。

**这两份透明 ICO 仍仅为实验资产。** 未嵌入 DLL、未由运行时选择，
不能据此声称品牌图标已随主题自动切换。已注册资源的后续修正见第 13 节。

## 12. 小狼毫 0.17.4：固定黑底白字的实证

用户提供该版本在浅色、深色任务栏上都清晰的截图后，按标签 `0.17.4` 对照，
固定到提交 `9cc96e20dc71b80876b12f689bb5863c76c2a7ed`，而非继续只看 `master`：

- [Globals.h](https://github.com/rime/weasel/blob/9cc96e20dc71b80876b12f689bb5863c76c2a7ed/WeaselTSF/Globals.h#L20)
  定义品牌图标索引为 0；
  [Register.cpp](https://github.com/rime/weasel/blob/9cc96e20dc71b80876b12f689bb5863c76c2a7ed/WeaselTSF/Register.cpp#L68)
  以模块路径和该固定索引调用 `RegisterProfile`，不是传递主题图标回调。
- [资源声明](https://github.com/rime/weasel/blob/9cc96e20dc71b80876b12f689bb5863c76c2a7ed/WeaselTSF/WeaselTSF.rc#L213)
  将 `IDI_WEASEL` 绑定到
  [resource/weasel.ico](https://github.com/rime/weasel/blob/9cc96e20dc71b80876b12f689bb5863c76c2a7ed/resource/weasel.ico)。
  `IDI_WEASEL` 的资源 ID 为 100，不是索引 100。
- 本机 Weasel profile 的实际注册为 `%SystemRoot%\System32\weasel.dll`、索引 0。
  只读提取后，对应资源 ID 100；16px 和 32px 图像的像素指纹均与该标签下的
  官方 ICO 相同。因此本次既核对源码，也核对了用户机器上的实际品牌资源。
- 该资源是**固定的黑色底板加白色字形**，不是透明背景白字加半透明描边。
  本机 16px 图像有 116 个完全不透明黑像素；奇奇修正前的 16px 品牌图像为 0 个。
  两者分别合成到浅色、深色背景，能重现用户截图中品牌边界清晰度的差别。
- [LanguageBar.cpp](https://github.com/rime/weasel/blob/9cc96e20dc71b80876b12f689bb5863c76c2a7ed/WeaselTSF/LanguageBar.cpp#L204)
  的模式 `GetIcon` 按中英文状态加载资源或当前 style 指定的图标路径；
  `GetInfo` 没有启用 `TEXTCOLORICON`。这条模式接口不能当作品牌自动变色的证据。

这解释了该品牌图标为何无需先解决动态资源刷新，也能兼顾深浅背景：
浅色时有黑底板提供边界，深色时白字保持对比。
**“两种主题都清楚”不等于“同一品牌字形会自动反色”。**
此处只说明对比度，不代表该设计符合官方的白底黑字品牌建议。
用户随后指出其浅色外观也不协调，因此不采用它作为品牌配色基线，
也没有复制小狼毫图标到产品资源。按官方建议的修正见下节。

## 13. 品牌修正：白底黑字方框

官方的 [IME icons 设计指南](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-icons)
分别描述品牌和模式图标：品牌是白色方框内的黑字，外框为 1px、50% 不透明度黑边；
白色字形加半透明黑描边则是模式图标的样式。此前将后者用于“奇”品牌并不准确。

经用户确认，当前[已注册品牌资源](../data/qiqiime.ico)改为：

- 不透明白色方框内绘制黑色“奇”，保留原有字体、Bold 字重及 80% 字形比例。
- 每个尺寸的最外圈恰好 1px，像素为黑色、alpha 128；内侧为不透明底色和
  黑色/灰阶抗锯齿字形，不再依赖透明背景上的字形描边。
- 保留 16、20、24、32、40、48、64、96、128、256px 的 32bpp 图像。
- 资源 ID 1、注册索引 0、DLL 路径和注册接口均不变；不引入动态资源改写、
  缓存强刷、品牌主题切换或进程重启逻辑。
- 两份透明黑/白实验 ICO 和三个工具图标的 SHA256 均保持不变。

通过[原有生成脚本](../scripts/generate_monitor_icon.ps1)重建。
`QiQiImeIconAssetsTest` 直接读取源码 ICO，`TestImeBrandIcon` 枚举实际 DLL 的
图标组资源；两者均逐帧检查 16、20、24、32、40、48px 及附加尺寸、32bpp alpha、
1px/50% 黑边、中性灰阶、不透明白盒、黑色字形、深浅背景对比及不裁切。
本项目注册的是标准方形品牌变体，没有注册上左圆角的“new”变体；测试明确锁定
标准四角像素，并在未来改用新变体时要求同步改测试。

验证状态：

- x64、x86 Release 均重建通过；`QiQiImeIconAssetsTest` 验证源码 ICO，
  `QiQiImeRuntimeIconTest` 验证实际 DLL 品牌组和运行时 mode HICON。
- 本轮未生成或安装 MSI，也未修改已安装输入法；这些独立合规测试不替代实际
  Win+Space、任务栏和 Secure Desktop 的人工视觉验收。

## 14. 模式指示图标合规

用户确认品牌“奇”保持现有外观，本轮将任务栏“中/英”模式指示图标的笔画
恢复为与官方指示器一致的半粗体：

- 字重由 `FW_NORMAL`（400）恢复为 `FW_SEMIBOLD`（600）。描边路径在四周预留
  1px 空间并相应内缩字形，不通过降低白字 alpha 来模拟细笔画。
- 按官方模式图标定义固定使用白色字形，并在外侧增加恰好 1px、50% opacity 的
  黑色描边；浅色任务栏由黑边保证边界，深色任务栏由白色主体保证可见性。
- 不再按任务栏主题切换成纯黑或纯白。现有 TSF/theme 通知仍保留，但 `GetIcon`
  在 app mode、任务栏 mode 任意组合下返回逐像素一致的 ARGB。
- 透明字形生成器保留默认的无描边接口；模式图标显式请求描边，其他品牌、工具
  或对话框调用不受影响。
- 独立 `QiQiImeRuntimeIconTest` 读取真实 `GUID_LBI_INPUTMODE` 的 `GetIcon`
  结果，根据白色覆盖重算八邻域 1px 描边及 premultiplied alpha，逐像素检查
  16px 默认、20px UAC 路径、透明边缘、白字/黑描边、深浅背景对比和主题不变性。

本轮未生成 MSI 或修改已安装输入法；本地合规以源码 ICO、实际 DLL、运行时
HICON 三层自动测试为准，具体 Windows Shell 的人工视觉验收边界仍保留。

验证结果：x64、x86 Release 均通过 `QiQiImeIconAssetsTest` 与
`QiQiImeRuntimeIconTest`；品牌 ICO 未重生成或改内容，mode 图标结果
不随 app/taskbar 主题变化。
