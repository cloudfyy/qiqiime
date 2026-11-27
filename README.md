# 奇奇五笔输入法 (QiQi Wubi IME)

简体中文 | [English](README.en.md)

[项目官网](https://cloudfyy.github.io/qiqiime/) |
[下载安装](https://cloudfyy.github.io/qiqiime/install.html)

**奇奇五笔（QiQiIME）** 是一款面向 Windows 10 22H2（x86 / x64）及 Windows 11 x64 的 **86 版五笔输入法**，基于 Windows 原生 **TSF（Text Services Framework）** 与 **C++** 构建，提供 `x64` 与 `x86`（32 位系统及 WOW64 应用）组件。

外部词库优先通过只读内存映射文件加载；不同进程映射同一词库文件时，可由 Windows 共享底层文件的物理内存页，减少词库数据的重复内存占用。进程内另有缓存，用于复用词库对象和映射视图；各进程的引擎、候选列表和界面仍会使用各自的堆内存。当前宿主验证情况及边界见[真实宿主测试记录](./docs/hosts.md#8-2026-11-03-人工测试结果)。

## 隐私与 Code signing policy

- [隐私政策 / Privacy policy](PRIVACY.md)：说明输入、剪贴板、用户词库、诊断、本机 AppContainer 共享权限及卸载后的数据保留。
- [Code signing policy / 代码签名政策](CODE_SIGNING.md)：当前尚未配置 SignPath，不声明已申请、获批或现有发行附件已签名；维护者角色确认、人工批准及获批后的接入要求见该政策。
- [第三方许可声明](THIRD_PARTY_NOTICES.md)与 [完整许可文本](licenses/)随后续发行包分发；两份极点词库已精确匹配固定 Apache-2.0 上游快照，来源、转换规则及复核方法见声明。

---

## 当前发布状态

当前源码数字版本为 **`1.3.5`**，发行标签为 `v1.3.5`，见[发布说明与版本历史](./docs/release-notes.md)。标签提交由 Release 工作流重新构建、测试和打包；安装包、运行组件和匹配符号以[Release 页面](https://github.com/cloudfyy/qiqiime/releases)实际提供的附件为准。标签存在不等于构建或发布成功；在 `v1.3.5` 发布完成前，仍可下载 `v1.3.4` 的正式附件。已有标签及附件不随主分支更新。

窗口和日志显示 `1.3.5+提交号`，MSI 与发行文件名使用数字版本。本机不制作或安装 MSI。任务栏搜索框候选显示的宿主关联修正已获 Win11 x64 和 Win10 x86 实机正常反馈，详见[测试记录](./docs/hosts.md#2026-11-11-任务栏搜索框候选不可见)；不外推所有排列、操作及宿主均已逐项验收。历史正式标签 `1.0.0`（不带 `v`）、候选标签及附件固定于各自提交，不随主分支升级或文档更新而改变。同一提交的本地构建与 GitHub 构建不保证文件哈希相同，二进制和 PDB 必须来自同一次构建。安装、升级和卸载步骤见[安装指南](./docs/install.md)。

- 显式打包流程生成 x64、x86 两种架构的中英文 MSI，以及双架构 ZIP；x64 MSI 包含 x64/WOW64 组件，x86 MSI 仅用于 32 位 Windows。历史附件名称和内容保持不变，安装及升级前请[备份用户数据并阅读安装指南](./docs/install.md)。
- [禁输入上下文的统一准入](./docs/restricted.md)已接入按键、保留快捷键、候选操作和异步执行；私密作用域允许普通输入但抑制学习及词库持久化。任务 36 已按双架构自动回归及独立测试宿主验证收口，不外推所有网页、WPF、UWP 或游戏控件均已验证；密码/PIN 识别依赖宿主提供的信号。
- [任务 47 与 R05](./docs/hosts.md#12-2026-11-07-用户确认任务-47-与-r05-完成)已按用户实际测试正常的确认收口，保留历史异常作为后续问题复现参考。发行包的安装、升级、卸载和用户数据保留仍须单独核对，自动回归不替代实际安装验证。
- Windows 登录与安全桌面不在已验证支持范围内，已撤下安全模式能力声明。Explorer 文件列表输入可能将组合小窗及候选窗放到屏幕左上角，暂保留现状；详见[已知兼容性记录](./docs/arch.md#explorer-file-list-positioning)。

## 1. 核心功能特性

### 1.3.5 五笔输入与设置改进（2026-11-27）

空格／回车支持三种处理方式，并新增严格单字模式，默认输入行为不变。
界面语言设置适用于独立工具；候选字体可从已安装的常用字体中选择和预览。
设置保存使用草稿的基线版本，发生并发修改时保留本地编辑，合并后重新确认。

### 1.3.4 AppContainer 配置路由补丁（2026-11-26）

1.3.4 在任何配置 DCOM 激活前检查 AppContainer token capability：
`lpacCom` 使用 Broker，仅有 `registryRead` 时读取只读注册表，二者都没有时
使用内置配置、内置皮肤与可用的随包/编译词典。三个只读来源共享同一个本地
缓存；注册表与内置路由拒绝写入，也不改变输入法 FSM 状态转换。

### 1.3.3 Shell 兼容补丁（2026-11-26，待发布）

1.3.3 修复 AppContainer 指示器菜单与 Searchbox 候选集成，并消除日志监视器
停止采集后状态栏的重复重绘：Searchbox 只公开当前候选页，列表选择会映射回
全局索引；桌面候选及非 Searchbox UILess 保留完整分页模型。日志监视器仅在
状态文字变化时更新状态栏，并明确区分本地缓冲淘汰、ETW 丢失与内存分配错误。

### 1.3.2 AppContainer Broker 访问补丁（2026-11-25，待发布）

1.3.2 为 Broker DCOM 增加 package-aware 的 Access/Launch 和文件访问授权：
注册时动态发现 Windows Search package SID，并为 Broker、x64/x86 Proxy
及所在目录配置只读执行权限。测试中的同一用户 AppContainer 调用可执行
Read、Save、Reset 与 Publish，不同用户仍被拒绝；独立 `grpperm`
权限矩阵和 `uwp` MSIX 宿主用于真实宿主验证。正式发布仍须完成
实际 AppContainer 激活与 Feedback Hub 皮肤刷新验收。

### 1.3.1 紧急补丁版（2026-11-24）

1.3.1 修复 1.3.0 配置 Broker 的 DCOM 注册权限：AccessPermission 仅授予
本地执行权，LaunchPermission 才包含本地激活权，避免 Windows DCOM 拒绝
`QiQiIMEConfigBroker.exe`。设置窗口、词库管理器和 IME 会保留 Broker、线程池、
缓存及配置保存返回的真实 HRESULT；皮肤不可用提示不再将
`0x80040153` 改写为通用 `0x8000FFFF`。原生同步/异步 DCOM、CAS、多
subscriber 与 x86/x64 回归均纳入验证。

### 1.3.0 功能版（2026-11-24）

1.3.0 将桌面用户配置统一交给独立 `QiQiIMEConfigBroker.exe`：
桌面、设置工具、UWP/Full Trust/LPAC 宿主不再各自读取包私有 `HKCU`
副本，皮肤与配置修改可由同一 Broker 提交并通知所有进程。新增配置
CAS、进程/用户会话校验、x86/x64 代理、事务持久化及安全默认缓存。
同步和原生异步调用现携带跨进程 trace/activity 上下文；日志监视器支持
Trace 筛选、关联树和提交血缘，且事务中持久化最后一次提交的 trace、
Broker activity 与 revision。

### 1.2.1 紧急补丁版（2026-11-22）

1.2.1 修复 1.2.0 包中设置窗口和词库管理器动态依赖 `miniz.dll`、
但 ZIP/MSI 未携带该 DLL，导致工具无法启动的问题。双架构构建现固定使用
静态 vcpkg triplet，CMake 会拒绝动态 Miniz，发布脚本也会修正旧缓存的
动态链接配置。输入行为、配置和个人词库格式不变。

### 1.2.0 功能版（2026-11-22）

1.2.0 提供统一的完整 ZIP 备份、候选皮肤与候选字体选择、
Shortcuts/About 独立设置页，以及输入法与设置 UI 的进程边界解耦。
设置窗口与词库管理器共用合并/覆盖恢复；皮肤和字体只影响候选窗/预览；
输入法打开设置时启动或激活独立 `QiQiIMESettings.exe`，不再把设置窗口代码
链接进宿主进程。MSI/ZIP/PDB 附件和网站下载元数据以发布工作流完成结果为准。

### 1.1.3 补丁版（2026-11-19）

1.1.3 补齐候选窗宿主所有权、DPI 宿主感知、Narrator UI Automation 及 TSF
UILess 合约：缺失或失效 HWND 时隐藏并延迟恢复；按宿主 DPI awareness
避免双重缩放；候选列表公开标准 UIA 属性与选择/显隐事件；UILess 失败不再
回退到未协商 popup，并保留文档关联、增量更新、重入序列化及失败重试。
Windows 原生 `ITfUIElementMgr` x64/x86 验收与现有候选、生命周期回归覆盖
见[发布说明](./docs/release-notes.md)。

### 1.1.2 补丁版（2026-11-18）

1.1.2 改善候选窗重绘，候选窗及预览统一跟随 App mode，统一切英文、切换输入法及离开输入框时的未完成输入策略，新增 JSON 配置导入导出，并修复设置草稿和回归测试的配置隔离，详见[发布说明](./docs/release-notes.md)。这些改动不属于原有 `v1.1.1` 附件，已知宿主问题与真机验收边界仍保留。1.1.1 的语言栏 COM 生命周期及异常声明修正、1.1.0 的主题、字号、关怀模式、菜单尺寸与焦点提示修复，以及无副作用的注册表只读查询均保留。1.0.9 的 ETW 普通日志、组合生命周期改进，以及更早的重入修复和卸载数据可选清理能力均保留。

- **卸载数据可选清理**：默认保留个人配置和词库；1.1.2 MSI 卸载向导将配置、词典数据分开选择，并提示待重启清理、在重装前检查状态。外部源词库、导出、备份和其他用户数据保留。ZIP 仍使用既有 `-RemoveUserData` 清理方式，操作与版本边界见[安装指南](./docs/install.md)。

- **竖排候选宽度**：按编码标题、翻页按钮及当前页最宽候选自适应，不再沿用横排的最小宽度；横排布局保持不变。
- **任务栏搜索框候选显示**：候选 popup 关联输入宿主，并补充可见性诊断；已获 Win11 x64 和 Win10 x86 实机正常反馈，不代表所有宿主及操作均已逐项验收。

- **Windows 10 与独立 x86 MSI**：新增 Windows 10 22H2（x86 / x64）支持目标，修复 32 位系统的产物检查与注册路径；显式打包生成 x64/x86 各自的中英文 MSI，x64 包仍包含 WOW64 支持。安装包选择见[安装指南](./docs/install.md#1-下载)。
- **日志监控多屏提权**：提权重启后在原窗口所在屏幕居中，不受 UAC 后鼠标位置影响；原屏幕断开时使用最近的可用屏幕。启动参数、错误诊断与真实工具子进程回归见[日志监控指南](./docs/log.md)。
- **密码/PIN 临时英文模式**：明确识别密码样式或密码/PIN 作用域后切换英文，离开后恢复进入前的中英文状态；支持原生编辑框运行中切换密码样式，不恢复已取消的组合，也不修改用户配置。识别与宿主边界见[专项说明](./docs/restricted.md)。
- **设置体验**：新增可取消的“恢复默认值”，仅点击确定后保存并通知；未保存修改在标题栏显示 `*`。用户词库路径、词库文件和调频数据不会因恢复默认值而删除。
- **声音与图标**：增强按键音的持续时间及能量并补充播放失败诊断；品牌图标改用透明背景、白色字形和半透明黑色描边，兼顾深浅任务栏。
- **R01–R06 重构**：恢复结果、同步批次发布、词库编辑草稿、配置投影和共享加载计划已完成；R05 的组合模块、COM 门面边界、双架构自动验收及用户宿主测试确认已收口。
- **组合期间切英文**：默认原始编码上屏，可改为清除当前输入文字；配置对各中英切换入口一致生效。详细规则见下方快捷键与[模式切换复测](./docs/hosts.md#p02组合期间-shift-切英文删除编码代码已修复宿主待复测)。
- **激活重入**：嵌套激活被拒绝；激活期间停用会取消当前尝试，登记返回后清理已取得的资源，外层返回 `E_ABORT`，不再继续报告成功。
- **独立 Metrics 视图与 JSON 导出**：日志监控器默认显示输入法日志；输入法日志、指标、工具诊断为一组互斥视图。共用开始/停止采集按钮，在指标视图启动才采集 Logs + Metrics，运行中切换视图不改变采集模式。支持筛选、累计/最近 60 秒统计及独立版本化 JSON，默认不混入原始日志或真实实例身份。当前业务埋点以三段焦点指标为试点，更广链路和完整性能预算仍在建设中，见[Metrics 指南](./docs/log.md#metrics-capture)。
- **测试诊断**：完整 TSF/UI 与组合生命周期测试保留逐函数耗时；连续 60 秒无测试完成结果时自动收集并分析本地 dump，90 秒限制测试进程，150 秒为 CTest 外层预算。日志、dump 与匹配符号不自动清理，见[开发指南](./docs/dev.md#tsf-hang-diagnostics)。
- **用户词库单一权威来源**：词条以用户词库文件为准，注册表只发布只读同步目录的绝对地址。AppContainer 通过该地址读取快照/增量，不再读取 `UserDictQdict` 镜像；权限与宿主边界见[隐私政策](./PRIVACY.md)和 [AppContainer 说明](./docs/uwp.md)。
- **调频文件格式命名**：二进制排序文件使用 `phrase_freq.qfreq`，不再默认读取旧 `phrase_freq.txt`；升级前请阅读[迁移说明](https://github.com/cloudfyy/qiqiime/blob/1.0.0/docs/release-notes.md#升级注意事项)。

已有用户正常使用确认不等于所有宿主/架构均有逐项证据，也不表示历史偶发挂起、Copilot 光标偏移或 Explorer 定位问题的根因已定位。当前职责与未完成扩展见[架构说明](./docs/arch.md)，历史用例保留在[宿主记录](./docs/hosts.md)，不为已收口任务重复要求整套人工验收。

### 1.1 多词库体系（极点五笔 & 海峰超大字符集）
- **极点五笔 86 核心词库（默认）**：收录 77,169 编码 / 89,254 词条，保持经典极点五笔盲打词序体验。
- **极点现代扩展词库（可选叠加）**：收录 2,098 编码 / 2,216 词条，包含现代互联网、科技、生活用语（当主词库设为极点五笔时可自由开关）。
- **海峰五笔 86 超大字符集词库**：收录 100,051 编码 / 144,374 词条，包含 GBK 汉字与 Unicode CJK 扩展 A/B 区生僻字（如 `𠀀`）。
- **自定义五笔 86 主词库（`PrimaryDict::Custom`）**：支持在属性设置窗口（`QiQiIMESettings.exe`）或词库管理器（`QiQiIMEDictManager.exe`「文件」菜单）中直接选择本地 UTF-8 五笔 86 码表（`.txt` / `.yaml`）或编译后的 `.qdict` 文件作为主词库，并可随时一键恢复默认极点 86 主词库。选择与保存前自动校验格式有效性，不修改用户原文件；若在 UWP AppContainer 沙盒宿主中因用户目录未授予 `ALL APPLICATION PACKAGES` 读取权限而无法打开自定义主词库，输入法会自动记录诊断日志并回退至随包极点 86 主词库。
- **自定义用户词库、在线造词与独立词库管理器（`QiQiIMEDictManager.exe`）**：
  - 支持加载用户自定义词表（兼容 `编码 词1 词2` 与 Rime `.dict.yaml` 格式）。对剪贴板中长度符合要求、所需汉字能查到编码的词组，可通过 **`Ctrl + =`** 按五笔 86 规则取码并追加到用户词库：双字词取 2+2，三字词取 1+1+2，多字词取前三字及末字的首码。
  - 在五笔组合输入态支持通过 **`Ctrl + 1..9`** 将候选词**固定置顶**（写入 `^词条`），通过 **`Ctrl + Delete`**（或 **`Ctrl + Shift + 1..9`**）**删除误造的用户词或屏蔽系统词**（写入 `!词条`）。
  - 提供独立词库与词频管理程序 **`QiQiIMEDictManager.exe`**（可从设置窗口一键打开），支持多源筛选（用户词库、手动置顶、已屏蔽词、动态词频 `QPF1`、主词库、扩展词库）、五笔编码反查、新增/修改/删除/恢复词条、动态词频置顶/清空和词库批量导入导出；所有修改通过跨进程通道实时同步到运行中的输入法实例。
  - 设置窗口与词库管理器共享包含配置、用户词库、置顶/屏蔽、词组排序和可选自定义主词库的完整 `.zip` 备份与合并/覆盖恢复；裸配置 JSON 与旧 `.qbak` 不再读取。设置中的恢复会立即生效，之后取消窗口不会回滚。

词库统计以当前仓库文件为准：编码去重，词条按“编码 + 词条”去重；同一词条的不同编码分别计数。

自定义主词库选择与设置保存会直接检查所选源文件，不让旧缓存掩盖文件问题：非法 UTF-8、损坏的 `.qdict` 或没有有效词条时拒绝；部分文本行无效时显示有效/无效行数、前 8 个错误的行号与原因，确认后仅加载有效词条，取消保留当前选择。文本编码限 1～4 个英文字母；Rime 行支持可选的非负数字权重，不支持任意 YAML 语法。格式正确不证明码表确实属于五笔 86。自定义文本主词库校验结果在内存中编译，不改写源文件或依赖旁边的缓存；随包词库仍沿用原有缓存加载路径。

### 1.2 纯正 86 五笔打字体验
- **四码唯一自动上屏（`auto_commit_4code`）**：启用后，输入不含通配符的 4 码且只有一个精确候选时自动上屏。
- **第五码顶字上屏（`push_commit_5th`）**：启用后，在已有 4 码且尚未上屏时继续输入字母，将当前选中候选上屏并开启下一轮编码；无候选时上屏原始编码。
- **分号 `;` / 单引号 `'` 快捷选重码（`semicolon_quote_quick_select`）**：启用后，在五笔组合态下按 `;` 上屏当前页第 2 候选词，按 `'` 上屏当前页第 3 候选词；对应候选不存在时不提交。
- **空格／回车三模式**：在设置的「输入习惯」页选择「空格选字，回车输出编码」（默认）、「空格选字，回车清码」或「回车选字，空格清码」。只改变五笔组合时单独按这两个键的行为。清码等同 `Esc`，不输出编码；选字键上屏高亮候选，无候选时仍按空码设置决定清除还是输出原始编码。
- **编码补全（`enable_code_completion`）**：默认开启，输入部分编码时追加以该编码开头的更长编码候选；可在设置的「候选窗口」页关闭。关闭后只显示精确编码匹配，保留有效的一、二、三级简码，不影响 `z` 通配查询或临时英文；与后续编码提示开关独立。
- **全码练习（`full_code_practice`）**：在设置的「候选窗口」页开启，默认关闭。只保留现有词库中可核验的精确全码，例如「好」可用 `vbg`，不能用 `vb`。练习期间不显示简码、补全或 `z` 通配结果；未经确认的别名只在普通模式可用。词组按可信单字码和五笔 86 取码规则核对；不改写词库或补全设置。临时英文、标点和原码上屏规则不变。参考表的覆盖范围见[第三方许可说明](THIRD_PARTY_NOTICES.md#wubi-86-full-code-practice-reference)。
  x64 和 x86 共用 `data\full.idx`；只在开启练习时读取和校验。文件缺失、损坏或没有读取权限时会提示，普通输入不依赖该文件。
- **单字模式（`single_character_mode`）**：在设置的「候选窗口」页开启，默认关闭。精确、补全和 `z` 通配候选都只保留单字；多字系统词、自造词和置顶词只隐藏，关闭后恢复。四码唯一上屏按筛选后的精确候选判断；不影响词库管理、造词、临时英文或标点键。
- **渐进后续编码提示（`show_code_hint`）与 `z` 万能学习键**：
  - 输入 1~3 码时，在候选词旁自动提示剩余编码后缀（如输入 `wq` 显示 `你好(~vb)`）；
  - 第 2~4 码支持按 `z` 万能键通配查询，并显示完整五笔编码反查提示（如 `1.你(wqiy)`）。
- **首码 `z` 引导临时英文模式（`enable_z_temp_english`）**：启用后，在中文空闲态下按小写 `z`，可继续输入 ASCII 字母、数字和常用符号组成的英文单词、网址或代码标识符。按空格或回车上屏后续内容，不包含引导 `z`（如输入 `zhttps://github.com`，上屏 `https://github.com`）；仅输入引导键时提交 `z`。
- **空码自动清屏与提示音（`beep_on_empty_4code` / `beep_on_dup_4code` / `clear_on_empty_4code`）**：
  - 支持四码空码提示音、四码重码提示音与可选打字按键音效（`enable_key_sound`）；
  - 支持可选“空码时自动清空输入”（默认关闭）。
- **可选重码词组动态调频（`dynamic_phrase_freq`）**：默认关闭，保持固定候选顺序；开启后，显式选中的词组按最近选择顺序调整同编码下的词组排序，单字候选保留原位置。这是最近选择优先，不是累计使用次数排名。

### 1.3 快捷键一览

| 快捷键 / 操作 | 功能说明 |
|---|---|
| **`Win + Space`** | 切换系统输入法，在列表中选择「QiQi 五笔 86」 |
| **轻按 `Shift`** 或 **`Ctrl + Space`** | 切换 **中 / 英** 输入模式；切换英文时默认将未提交编码原样上屏，可在设置中改为清除输入文字；中/英按钮及系统中英文状态切换遵循同一配置；Shift 轻按切换可在设置中关闭 |
| **`Shift + Space`** | 切换 **全角 / 半角** 字符模式 |
| **`Ctrl + .`** | 切换 **中文全角标点 / 英文半角标点**（含智能配对引号 `“”` / `‘’`） |
| **`Ctrl + =`** | 对长度和取码条件满足要求的剪贴板词组取码并追加到用户词库 |
| **五笔组合态下的 `Ctrl + 1` ~ `9`** | 将当前页对应序号的候选词**固定置顶**（排在同码首位并持久化同步） |
| **五笔组合态下的 `Ctrl + Delete` 或 `Ctrl + Shift + 1` ~ `9`** | **删除**对应用户自造词，或**屏蔽**对应系统词条（可在词库管理器中恢复） |
| **`Space`（空格键）** | 五笔组合时默认上屏高亮候选，也可改为清码；临时英文模式下仍提交英文内容 |
| **`;` / `'`** | 开启快捷选词时，在五笔组合态上屏当前页 **第 2 / 第 3** 候选词 |
| **`1` ~ `9` 数字键 / 鼠标点击** | 五笔组合态下选择当前页有效序号对应的候选；临时英文模式下数字键追加数字，鼠标点击上屏所点候选 |
| **`Enter`（回车键）** | 五笔组合时默认输出原始编码，也可改为清码或选字；临时英文模式下仍提交英文内容 |
| **五笔组合态下未按 `Shift` 的 `-` / `=`，或 `PageUp` / `PageDown`** | 候选框翻至上一页 / 下一页；临时英文中的 `-` / `=` 作为普通符号输入 |
| **五笔组合态下的 `↑` / `↓` 方向键** | 默认移动到前一个 / 后一个候选词（跨页）；启用 `up_down_page_turn` 后改为翻页 |
| **五笔组合态按 `Shift + 字母`（Caps Lock 关闭时）** | 将当前选中候选连同该大写英文字母一并上屏；无候选时使用原始编码 |

### 1.4 界面与个性化设置
- **候选皮肤与字体**：Candidates 页位于“皮肤/字体 + 预览”同一组区域，
  快捷下拉保留常用皮肤，完整窗口展示二十一套明暗配套皮肤并提供明确选中提示。
  候选字体只影响候选窗与预览；字体选择过滤横排、可缩放的简体中文字体，
  包括当前用户安装的字体，缺失字体保留配置并回退系统 UI 字体。
- **设置七页布局**：输入习惯、词库、候选窗口、显示、声音与反馈、快捷键、关于；
  Shortcuts/About 位于 Tab 栏最右侧，底栏只保留恢复默认设置与确定/取消。
- **可选主题与关怀模式**：设置窗口、输入法自绘候选窗、词库管理器和日志监控器的主窗口及自绘菜单支持跟随系统、明亮、深色、高对比度四选一，提供 100%～200% 字号及关怀大目标模式；Windows 高对比度优先。设置内预览，确定后保存，两个工具读取已保存的偏好；系统和宿主接管的界面仍由其控制。详见[显示与易读指南](docs/accessibility.md)。
  候选窗及候选预览在跟随系统时统一使用 App mode，当前不探测 Windows 系统界面；输入法自绘快捷菜单继续使用 Windows mode。
- **Direct2D / DirectWrite 候选窗口**：启用彩色 Emoji 绘制（`D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`）与系统字体回退；实际字形覆盖和彩色显示取决于系统及可用字体。为支持 TSF UILess 的宿主提供候选接口，不保证所有全屏游戏兼容。
- **任务栏托盘与 4x4 超采样抗锯齿浮动状态条**：
  - 任务栏托盘实时显示“中/英”状态图标，支持左键切换中英、右键打开快捷菜单；
  - “中/英”图标由系统按任务栏主题着色，兼容应用与任务栏使用不同深浅配色；快捷菜单在跟随系统时使用 Windows mode，保留手选主题及高对比度优先规则，切换开关和界面语言后保持展开并即时刷新，点击外部或按 Esc 关闭，打开设置或关于时自动关闭；词库选择与扩展词库开关统一在设置窗口操作，不再占用右键菜单；
  - 桌面浮动状态条（默认关闭）支持胶囊把手拖拽、悬停提示（Tooltip）、右键菜单与多显示器高分屏 DPI；状态条本体及 Tooltip 跟随 App mode，其自绘右键菜单跟随 Windows mode。
- **图形化属性设置与运行时同步**：
  - 设置窗口按输入习惯、词库、候选窗口、显示、声音与反馈、快捷键、关于分成七个 Tab；底栏仅保留恢复默认设置、确定和取消。
  - 输入法只激活或启动独立 `QiQiIMESettings.exe`，不把设置 UI 链接进宿主进程；配置仍存储在注册表 `HKCU\Software\QiQiIME`，保存后通过 TSF 全局通知同步运行中的输入法实例，并支持中/英文（`zh-CN` / `en-US`）双语界面切换。
  - `up_down_page_turn` 默认关闭。
  - **候选排列**：在“外观”的“每页候选 / 排列”中选择横排（默认）或竖排。两种排列共用选词、翻页及绘制逻辑；仅影响输入法自绘候选窗，宿主接管的 UILess 候选界面由宿主决定。
  - **未完成输入的处理**可选“原始编码上屏”（默认）或“清除未完成输入”，适用于离开输入框或窗口、切换英文模式，以及通过 `Win+Space` 等入口切换到其他输入法/键盘布局。不拦截系统快捷键；只操作仍由本输入法拥有的当前组合范围。宿主先终止当前组合时，在终止回调授予的写权限内执行同一策略；回调结束后不追删普通文本，写入失败保留真实诊断。配置字段仍为 `commit_on_english_switch`，注册表 DWORD 仍为 `CommitOnEnglishSwitch`（1 保留原始编码 / 0 清除）；旧配置默认上屏，备份和 JSON 导出保持字段兼容。

---

## 2. 运行环境与开发环境要求

### 2.1 运行环境要求（终端用户）

| 项目 | 最低 / 推荐要求 |
|---|---|
| **操作系统** | Windows 10 22H2（x86 / x64）或 Windows 11 x64；32 位系统使用 x86 MSI，64 位系统使用 x64 MSI（内含 32 位应用支持）；未提供原生 ARM64 组件。安装包选择见[安装指南](./docs/install.md#1-下载) |
| **运行库依赖** | Release 构建静态链接 MSVC 运行库（`/MT`），无需另装 VC++ Redistributable；仍依赖 Windows 系统组件 |
| **图形与文本组件** | Windows 原生 TSF（`msctf.dll`）、GDI / Direct2D / DirectWrite（系统自带，无需额外安装任何第三方框架） |
| **磁盘占用** | 随发行包所含架构、工具、词库及生成的 `.qdict` 缓存变化，以实际安装和缓存目录大小为准 |
| **内存占用** | 词库映射页由系统按需载入；引擎、缓存、候选与界面仍占用内存，不等于零私有堆或零工作集 |
| **安装权限** | 安装/卸载时需管理员权限（Administrator，用于注册系统 COM 与 TSF 语言项）；日常打字与修改配置仅需普通用户权限 |

### 2.2 开发与编译环境要求（开发者）

| 工具 / 组件 | 版本与配置要求 |
|---|---|
| **操作系统** | Windows 11 (`x64`) |
| **C++ 编译器** | 默认 CMake 预设使用 Visual Studio 2026，需安装 **“使用 C++ 的桌面开发”** 工作负载及 MSVC `x64/x86` 生成工具；项目使用 `/std:c++latest` 草案模式，不表示完整实现 C++26。使用 VS 2022 等其他版本时需选择相应生成器并验证编译兼容性 |
| **Windows SDK** | Windows SDK（`10.0.22621.0` 或 `10.0.26100.0` 及以上） |
| **CMake** | **`4.4.4` 或更高版本**；默认使用 VS 2026 预设 |
| **依赖包管理器** | `vcpkg`（Visual Studio 内置 vcpkg 或通过环境变量 `VCPKG_ROOT` 指定，Manifest 模式自动安装 `ms-gsl`、`nlohmann-json` 和 `miniz`） |
| **MSI 打包工具（可选）** | .NET SDK 8.0+ 与 **WiX Toolset CLI v4/v7+**（通过 `dotnet tool install --global wix` 安装，用于构建 `.msi` 安装包） |

---

## 3. 快速安装与卸载

从 GitHub 下载 MSI 的用户请先阅读[安装指南（含 SmartScreen 截图与操作步骤）](./docs/install.md)。

从源码安装时，须先按[开发指南](./docs/dev.md)完成 Release 构建（32 位目标系统只需 x86）；注册脚本只检查和注册已有产物，不会自动编译。使用 MSI 时按操作系统位数选择 `win-x64` 或 `win-x86`，不是按某个应用的位数选择；64 位系统所需的 32 位应用支持已包含在 x64 MSI 中。ZIP 包须完整解压并保留目录结构，历史发布附件不会随源码更新，旧的 `win-x64-x86-*.msi` 仍属于 x64 安装器。

以下命令在已构建的项目根目录或解压后的发行包根目录运行：

```powershell
# 按系统位数校验并注册输入法（64 位系统为 x64+x86，32 位系统仅 x86）
powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate -LaunchNotepad

# 注销输入法的 COM 与 TSF 注册；此脚本不删除用户配置、词库或安装文件
powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
```

---

## 4. 技术文档索引（`docs/`）

本项目将功能文档与技术文档分离，详细技术设计、开发构建与部署运维请查阅 `docs/` 目录下的专项文档：

| 文档分类 | 文档链接 | 内容概述 |
|---|---|---|
| **架构文档** | [docs/arch.md](./docs/arch.md) | 系统分层架构、TSF COM 生命周期、静态分派与运行时 FSM、`.qdict` 按需分页只读内存映射、Direct2D/DirectWrite 与 RAII 体系 |
| **开发文档** | [docs/dev.md](./docs/dev.md) | 源码目录结构、CMake + vcpkg + C++26 双架构构建、单元测试套件、`CppCoreCheck` 静态代码检查、版本号生成机制与结构化排障日志规范 |
| **组合生命周期** | [docs/composition.md](./docs/composition.md) | 组合模块职责、生命周期票据、独立清理、宿主重入、激活取消、候选呈现/布局及验证边界 |
| **日志监控** | [docs/log.md](./docs/log.md) | 生产 ETW 与测试保留日志的区别、实时监控、错误码解释、挂起 dump 与符号排查 |
| **安装指南** | [docs/install.md](./docs/install.md) | 面向最终用户的 MSI 下载、安装、升级、卸载与常见问题 |
| **真实宿主测试** | [docs/hosts.md](./docs/hosts.md) | 生命周期测试步骤、已有人工反馈及尚未覆盖的宿主与验证边界 |
| **实施与历史评审** | [docs/review.md](./docs/review.md) | R01–R06 当前状态、R05 分步实施与真实失败记录；原始初评保留为历史依据 |
| **受限输入** | [docs/restricted.md](./docs/restricted.md) | 统一准入、密码/PIN 临时英文、私密作用域持久化限制及宿主识别边界 |
| **UWP / AppContainer** | [docs/uwp.md](./docs/uwp.md) | 实际进程令牌、读取/同步/日志权限及未完成的宿主兼容性验收 |

---

## 5. 开源许可协议与词库致谢（License & Third-Party Notices）

本项目（**奇奇五笔 / QiQiIME**，作者：`cloudfyy`）的原创代码采用 **[GNU General Public License 第 3 版或任何后续版本](./LICENSE)**（`GPL-3.0-or-later`）开源发布。原创源文件、测试、脚本及支持注释的构建文件使用 SPDX 头部声明版权与许可；JSON 配置由项目级许可覆盖，不插入非法注释。第三方材料仍遵循各自许可。

本项目在 `data/` 目录下收录并转换了以下合法开源的 86 五笔词库码表，特此向原作者及维护社区致谢，并严格遵循各自的开源许可协议：

### 5.1 极点五笔 86 词库（`wubi86_jidian.txt` / `wubi86_jidian_extra.txt`）
- **上游仓库**：[KyleBing/rime-wubi86-jidian](https://github.com/KyleBing/rime-wubi86-jidian)
- **原始码表文件**：
  - 核心主词库：[`wubi86_jidian.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/18a6016c3d08a20f713f8f8e1791a2e79e0be081/wubi86_jidian.dict.yaml)（本仓库转换后：77,169 编码 / 89,254 词条）
  - 现代扩展词库：[`wubi86_jidian_extra.dict.yaml`](https://github.com/KyleBing/rime-wubi86-jidian/blob/18a6016c3d08a20f713f8f8e1791a2e79e0be081/wubi86_jidian_extra.dict.yaml)（本仓库转换后：2,098 编码 / 2,216 词条）
- **协议与来源**：[Apache License 2.0](licenses/Apache-2.0.txt)。两份转换词库的全部词条及顺序均精确匹配固定上游 `18a6016c`；这确立可复现来源，不声称原导入记录包含该提交。上游后来的 MIT 变更不用于改写本次许可；哈希、过滤/排序规则及离线复核见[第三方许可声明](THIRD_PARTY_NOTICES.md)。

### 5.2 海峰五笔 86 超大字符集词库（`wubi86_haifeng.txt`）
- **原作者与贡献者**：孙海峰（SUN Haifeng）及《五笔爱好者论坛》网友、深圳大学中文系合作团队
- **上游仓库**：[definite/ibus-table-chinese (`tables/wubi-haifeng`)](https://github.com/definite/ibus-table-chinese/tree/master/tables/wubi-haifeng)
- **原始码表说明与授权文件**：
  - 码表说明：[`tables/wubi-haifeng/README`](https://github.com/definite/ibus-table-chinese/blob/master/tables/wubi-haifeng/README)
  - 授权文件：[`tables/wubi-haifeng/COPYING`](https://github.com/definite/ibus-table-chinese/blob/master/tables/wubi-haifeng/COPYING)（本仓库转换后：100,051 编码 / 144,374 词条，包含 GBK 汉字、常用词组及 Unicode CJK 扩展 A/B 区汉字）
- **开源协议**：**BSD 3-Clause License**
  > Copyright (c) 2010, SUN Haifeng (孙海峰). All rights reserved.
  >
  > Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
  > 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  > 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
  > 3. Neither the name of the "SunHaifeng.NET(海峰别墅)" nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

### 5.3 第三方 C++ 依赖库
- **[microsoft/GSL](https://github.com/microsoft/GSL)**：C++ Core Guidelines Support Library（`gsl::span` / `gsl::not_null` / `gsl::narrow_cast` / `gsl::at` / `gsl::finally`），遵循 [MIT License](https://github.com/microsoft/GSL/blob/main/LICENSE)。

完整的第三方许可及免责声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [licenses/](licenses/)，以上摘要不替代完整文本。
