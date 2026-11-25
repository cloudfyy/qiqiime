# TODO

## 五笔功能核对与排期（2026-11-19）

本节对照常用五笔功能清单核实当前源码，不把规划、词库覆盖范围或相似功能算作已实现。
“已做”表示已有实际处理路径；“部分”表示基础能力已有，但所列开关或扩展仍缺失；
“未做”表示当前引擎、配置、按键及数据中未见对应实现。本次只整理文档，
未运行功能测试，也不代表新增真实宿主验收。

核对依据：[配置与默认值](../src/common/config.h)、
[设置控件](../src/tools/settings/controls.cpp)、
[引擎处理](../src/ime/engine/engine.cpp)、
[查词与造词](../src/ime/engine/dictutil.cpp)、
[按键映射](../src/ime/fsm/keymap.h)、
[状态转移](../src/ime/fsm/transitions.h)、
[保留热键注册](../src/ime/svc/service.cpp)及
[服务按键处理](../src/ime/svc/keyhandler.cpp)。
已有回归依据包括[简码、上屏、补全和选词测试](../tests/ime/engine/input.cpp)、
[临时英文及标点测试](../tests/ime/engine/modes.cpp)、
[词组取码与扩展字符测试](../tests/ime/engine/dict/external.cpp)。

### 核心功能矩阵

| 功能 | 状态 | 当前能力与缺口 |
| --- | --- | --- |
| 一级、二级、三级简码；关闭后仅全码 | 部分 | 已有各级简码；缺独立的简码禁用/全码练习模式，见 W03。关闭“编码补全”只取消更长编码的前缀候选，仍能提交简码。 |
| 四码唯一自动上屏 | 已做 | `auto_commit_4code` 默认开启；不含 `z` 通配符且只有一个精确候选时提交，可为单字或词组。 |
| 词语联想 | 未做 | 提交后没有关联词候选。现有 `dynamic_phrase_freq` 仅调整同编码词组的最近选择顺序，不是联想，见 W12。 |
| 二字、三字、四字及多字词组输入 | 已做 | 支持码表词组；造词取码规则已有 2+2、1+1+2、前三字及末字首码。 |
| 单字模式 | 未做 | 没有屏蔽词组的候选过滤开关，见 W02。 |
| 临时拼音、拼音反查五笔码 | 未做 | 已有“字/词查五笔码”的内部接口和词库管理器入口，但没有拼音检索数据及输入路径；首码 `z` 默认是临时英文，编码中的 `z` 是通配，见 W05。 |
| 编码提示、字根拆分提示 | 部分 | 已有可关闭的后续编码提示及通配结果编码提示；没有字根拆分数据、字形展示和开关，见 W13。编码提示也未绑定 `Ctrl+Shift+J`。 |
| 以形查音 | 未做 | 五笔候选没有拼音/多音字读音标注，`~` 不是查音入口，见 W09。 |
| 简入繁出、繁入简出 | 未做 | 没有繁简转换模式或转换数据；码表中含繁体字不等于支持转换，见 W10。 |
| 五笔拼音混输、纯五笔切换 | 部分 | 当前就是五笔 86 输入；没有拼音候选源、混输消歧及模式切换，见 W11。临时拼音反查不等于混输。 |
| 造词、删自造词 | 已做 | `Ctrl+=` 对符合长度/取码条件的剪贴板词组造词；支持删除用户词、屏蔽系统词、固定候选及词库管理器编辑。 |
| 全角/半角、中英文标点 | 已做 | 已有模式、默认配置及 `Shift+Space`、`Ctrl+.` 热键。 |
| 翻页、二三候选 | 部分 | 已有 `-`/`=`、PageUp/PageDown、可选上下键翻页及 `;`/`'` 二三候选；缺自定义/逗号句号翻页（W07）和 `0` 选第十候选（W14）。 |
| 以词定字 | 未做 | 不能从当前词组候选单独提交首字/末字，见 W04。 |
| GB2312 / GBK / Unicode 生僻字集开关 | 部分 | 可切换极点/海峰等主词库，海峰已有补充平面字符测试；没有按字符集统一过滤的开关，见 W08。词库切换不等于字符集切换，也不保证覆盖全部 Unicode 汉字。 |

另外已做：第五码顶字、独立编码补全、可选词组动态调频、横/竖排候选。
这些能力保留，不作为上述缺项的替代。

### 按键与输入流派核对

以下绑定来自不同输入法或用户配置，并非 Windows / Rime / Fcitx5 的统一标准。
分别核对“功能是否存在”和“该键是否绑定”，不直接照搬可能冲突的默认值。

| 按键 / 操作 | 当前行为或缺口 |
| --- | --- |
| Space / Enter | 当前固定为空格提交高亮候选、回车输出编码；既不是“回车清码”，也不是“回车选字、空格清码”。已有三选一设置 TODO，见 W01，保留当前默认。 |
| Esc / Backspace | 已有：取消组合 / 删除最后一个编码；删到空时结束组合。 |
| `1`～`9` / `0` | 已有当前页第 1～9 候选选择；每页最多 9 个，`0` 不选第十个，见 W14。 |
| `;` / `'` | 已有当前页第二/第三候选选择，默认开启；对应候选不存在时不提交。 |
| `-` / `=`、PageUp / PageDown | 已有上一页/下一页；上下键可配置为翻页。 |
| `,` / `.` 翻页及自定义翻页键 | 未做；当前逗号句号走标点路径，不能直接改默认，见 W07。 |
| 轻按 Shift / `Ctrl+Space` | 已有中英文切换；可关闭 Shift 轻按切换，Shift 组合键不是切换操作。 |
| CapsLock 切换中英文 | 没有可选模式开关；当前保留系统大写锁定/英文直通，五笔组合中按下会提交编码并放行系统切换，见 W06。 |
| `Shift+Space` / `Ctrl+.` | 已有全半角 / 中英文标点切换。 |
| `Ctrl+Shift+F` | 未绑定，繁简转换本身也未做，见 W10、W07。 |
| `z` 拼音反查 | 未做；已有临时英文/通配用途，不能无兼容方案直接抢占，见 W05。 |
| Ctrl + Backtick（反引号）方案菜单 | 未做；当前只支持五笔 86，切换主词库不等于切换 86/98/新世纪方案，见 W15。 |
| `Ctrl+Shift+H` | 未绑定；字根拆分功能待 W13，之后才能绑定。 |
| `Ctrl+Shift+J` | 未绑定；编码提示已有设置项，快捷切换待 W07。 |
| `Ctrl+Shift+D` / `Ctrl+Shift+U` | 未绑定；分别依赖 W02 单字模式、W08 字符集开关，再由 W07 接入。 |
| `Shift+Delete` | 未作为删词键；已有 `Ctrl+Delete` 删除当前高亮候选对应的用户词或屏蔽系统词，别名待 W07。 |
| `~` 以形查音 | 未做；当前中文标点模式下是 `～`，新入口需处理冲突，见 W09、W07。 |
| `Ctrl+Shift+M` 打开设置 | 未绑定；已有菜单打开设置的入口，快捷键待 W07。 |
| `Ctrl+Shift+X` / `Ctrl+Shift+W` | 未绑定；当前只有纯五笔，混输切换依赖 W11，再由 W07 接入。 |
| `Ctrl+=` | 已有剪贴板快捷造词，不是新的待实现功能。 |
| `Ctrl+Shift+1`～`9` | 已有删除对应用户词或屏蔽对应系统词；另有 `Ctrl+1`～`9` 固定置顶。 |

### 建议执行顺序

本排期只针对本次功能清单，不覆盖原有稳定性任务；已有的未完成输入丢弃、
宿主兼容性及生命周期回归优先处理。P1 优先补齐常用纯五笔能力，
P2 补充可配置性及数据能力，P3 为较大扩展或暂缓项。
同级按序推进，但没有依赖的调研可并行，不要求机械串行。
本表是任务索引，不重复创建下方已有 TODO，也不表示功能已获运行时验收。

| 顺序 | 优先级 | 任务 | 主要依赖 / 排序理由 |
| --- | --- | --- | --- |
| 1 | P1 | [W01 空格/回车三种模式](#wubi-w01) | 已有明确三选一需求；优先解决输入习惯兼容，不改旧默认。 |
| 2 | P1 | [W02 单字模式](#wubi-w02) | 高频、边界较清楚；同时核对过滤后的四码唯一判断。 |
| 3 | P1 | [W03 简码开关 / 全码练习](#wubi-w03) | 与编码补全独立；先确认真实全码、自定义简码及提示策略。 |
| 4 | P1 | [W04 以词定字](#wubi-w04) | 复用现有候选选择/上屏；先定义入口及首末字语义。 |
| 5 | P1 | [W05 离线拼音反查五笔码](#wubi-w05) | 高频补盲能力；先解决数据许可和入口，不被字根素材阻塞。 |
| 6 | P2 | [W06 可选 CapsLock 中英切换](#wubi-w06) | 复用统一模式切换与未完成输入策略，保留默认英文直通。 |
| 7 | P2 | [W07 快捷键配置及兼容预设](#wubi-w07) | 统一处理快捷键、翻页别名和冲突；功能不存在时不注册空操作。 |
| 8 | P2 | [W08 字符集 / 生僻字过滤](#wubi-w08) | 先定义字符集口径，复用 W02 的候选过滤接入点。 |
| 9 | P2 | [W09 以形查音 / 拼音提示](#wubi-w09) | 复用 W05 的读音数据阶段；不依赖混输或字根绘制。 |
| 10 | P2 | [W10 繁简输出转换](#wubi-w10) | 需可再分发数据、词组歧义及候选身份处理，不能只做逐字替换。 |
| 11 | P3 | [W11 五笔拼音混输](#wubi-w11) | 在 W05 数据基础上另做拼音词库、歧义与排序；不是打开反查开关即可完成。 |
| 12 | P3 | [W12 词语联想](#wubi-w12) | 独立于动态调频和混输；需定义提交后候选及隐私边界。 |
| 13 | P3 / 数据许可门槛 | [W13 字根拆分提示](#wubi-w13) | 复用已有调研 TODO；许可未厘清前不导入拆分数据或字形。 |
| 14 | P3 | [W14 第十候选与 `0` 选词](#wubi-w14) | 当前五笔高频选择已有 `;`/`'`；扩到十项需同步配置、UI、TSF 和索引边界。 |
| 15 | P3 / 暂缓、待确认 | [W15 98 / 新世纪及方案菜单评估](#wubi-w15) | 超出当前 86 版定位；先确认是否扩展产品范围，不默认承诺实现。 |

### 新增缺项 TODO

W01、W02、W03、W05、W06、W07、W13 复用下方既有条目及验收要求；
本次只新增尚未单独跟踪的缺项。既有简码/单字任务的优先级以上表为准。

- [ ] <a id="wubi-w04"></a> **W04 / P1：以词定字。**
  先确认首字/末字的按键，不抢占现有标点或二三候选键。以当前高亮的原始词组
  候选为对象，分别只提交首字或末字，不截取编码提示等显示后缀。
  复用现有提交和组合结束路径，明确调频是否作用于原词。
  按 Unicode 字符而非 UTF-16 单元取字；覆盖跨页选择、二字/长词、单字、
  补充平面字符、无候选和取消，不产生额外文本或拆开的代理对。

- [ ] <a id="wubi-w08"></a> **W08 / P2：字符集 / 生僻字过滤。**
  提供独立于主词库的 GB2312 / GBK / 全部已加载 Unicode 字符集选项；
  先定义精确成员判定，以及词组中部分字符不符合时、用户词和置顶词的处理。
  不把海峰词库选择或 BMP 范围判断当作字符集过滤，也不承诺码表外字符可输入。
  在候选数量截断、分页和唯一上屏判断前一致应用过滤，覆盖精确、补全、
  通配及所有提交入口；验证补充平面字符、空结果和切换后的索引。

- [ ] <a id="wubi-w09"></a> **W09 / P2：以形查音 / 拼音提示。**
  复用 W05 经许可核实的字音数据及索引，为五笔候选提供可选读音标注或查询入口；
  不再引入一套重复数据加载器，也不依赖字根拆分。
  先确认是否采用 `~`、其他按键或候选注释，并保留原有标点用途。
  定义声调、多音字、词组读音及缺失记录的显示规则，不猜测读音。
  覆盖同形多音、补充平面字符、缺失数据、提示开关和候选显示/提交分离。

- [ ] <a id="wubi-w10"></a> **W10 / P2：繁简输出转换。**
  明确“不转换 / 简入繁出 / 繁入简出”的范围、词组上下文规则和多义转换；
  选择并核实转换库/数据的分发许可，不能以系统存在繁体字形代替转换数据。
  候选预览与最终输出一致，同时保留原始编码/词条身份供删词、置顶和调频使用，
  不改写源词库。接入持久化、设置和通知；`Ctrl+Shift+F` 由 W07 管理。
  覆盖一对多、词组转换、输出去重与唯一上屏、用户词、关闭模式及转换失败诊断。

- [ ] <a id="wubi-w11"></a> **W11 / P3：五笔拼音混输及纯五笔切换。**
  在 W05 的单字反查之外单独确认拼音词组输入范围、拼写规则、同串编码消歧、
  候选来源排序及四码自动上屏条件，不能让拼音候选意外改变五笔盲打行为。
  补齐相应词库许可和本地索引，保留纯五笔为现有默认。
  模式切换入口及组合中切换策略先确认；相关快捷键由 W07 管理。
  覆盖纯五笔回归、同码冲突、翻页/选词、用户词、临时英文及受限/私密上下文。

- [ ] <a id="wubi-w12"></a> **W12 / P3：词语联想。**
  作为独立开关设计，建议默认关闭；不把前缀编码补全或最近选词排序当作联想。
  明确根据刚提交内容产生关联词还是待补后缀，以及选择时是否补写、如何避免
  重复输出。先确认数据来源/许可和是否学习；不读取无关宿主文本。
  定义 Esc、继续编码、切英文、离开输入框时的联想清理规则，
  保持现有候选消失策略及私密上下文不学习约束。
  覆盖连续提交、取消、跨输入框、无联想结果及关闭后完全恢复旧输入行为。

- [ ] <a id="wubi-w14"></a> **W14 / P3：第十候选与 `0` 选词。**
  保留现有默认每页 5 个；如扩展上限到 10，同步修改配置范围、设置选项、
  候选编号/点击、引擎索引、TSF/UILess 候选及相关页大小边界，不能只放宽配置。
  明确主键盘/数字小键盘 `0` 的选词范围，保持临时英文、非组合态和不足十项时
  的既有行为。覆盖首/末页、恰好十项、第十项提交和鼠标/键盘的一致性。

- [ ] <a id="wubi-w15"></a> **W15 / P3，暂缓：评估 98 / 新世纪五笔及方案菜单。**
  先确认是否扩展五笔 86 的项目定位；如果仅需在极点/海峰之间切换，应继续使用
  词库设置，不宣称支持另一套编码方案。
  若批准多方案，再单独核实码表许可、取码/造词规则、方案专属用户词与调频隔离、
  配置迁移及 TSF 注册/展示方式，最后考虑 Ctrl + Backtick 等菜单入口。
  未确认前不导入码表，也不把热键或菜单本身当成多方案实现。

### 后续实现的共同完成条件

- 新增设置复用共享配置字段、设置草稿/保存/通知及导入导出机制，补齐中英双语；
  不靠只改注册表或只有菜单入口宣称完成。
- 保持当前默认行为，尤其是 `z` 临时英文/通配、二三候选、空格/回车和 CapsLock。
  上表标注“先确认”的语义留到对应实现前决定，不在本次整理中擅自定案。
- 候选过滤/转换与精确查词、补全、通配、唯一上屏、分页及各提交入口保持一致；
  复用现有 UTF-16 字符拆分及提交路径，显示提示不进入实际上屏文本。
- 每项附针对新增行为的 x64/x86 回归；真实宿主专项验证按受影响路径执行。
  完成 C++ 任务时核对 RAII、复用/DRY、Core Guidelines/GSL 及缺失的边界测试。
  按[现有验证规则](dev.md#required-validation-by-change-scope)使用云端 lint，
  保留真实失败码，不记录真实输入、候选内容或剪贴板文本。

## Code review follow-up (2026-11-18)

These are tracked findings and proposals. Completed code changes are marked
below; dedicated runtime reproduction,
especially MSI uninstall/reinstall, is still required. Fix boundary regressions
first, then consolidate exit policy/test isolation, and only afterward consider
layout/painting refactors. Keep changes small and independently verifiable.

### High priority: boundary regressions

- [x] Allow service SIDs for default-retention machine uninstall without
  authorizing personal-data deletion. The automated installer test covers
  LocalSystem with both options off, including reboot bookkeeping, and rejects
  service SIDs when either deletion option is selected.
- [x] Verify recorded quarantine targets independently of the current
  ProfileList entry. The pending check uses the protected job's original volume
  location and still validates volume, path, ACL, reparse and file identities.
  Tests cover remaining and removed targets after profile relocation/deletion;
  `Complete=1` alone does not clear pending work.
- [x] Keep unpublished Settings language changes out of the caller config.
  [Transfer tests](../tests/tools/settings/transfer.cpp) cover Import and
  Restore Defaults, both refresh messages, and Cancel without changing the
  caller's configuration or saved registry values.
- [ ] On a dedicated test machine, verify real SYSTEM-initiated silent
  uninstall with both options off, and real reboot/reinstall with quarantined
  files and a removed or relocated profile. The x64/x86 automated installer
  tests do not replace these checks.

### Targeted design consolidation

- [ ] High priority: centralize composition exit policy as a small immutable
  value plus the existing coordinator, rather than adding a class hierarchy.
  Review [current termination](../src/ime/comp/finish.h),
  [captured cleanup](../src/ime/comp/cleanup.cpp), and
  [service exit entries](../src/ime/svc/composition.cpp). Make exit reason,
  preserve/discard policy, write authority, and operation results explicit.
  Preserve three distinct paths: active cleanup may request a write session
  and call `EndComposition`; host termination may erase only within its supplied
  callback cookie and must not end again; removed contexts permit no edits.
  Retain epoch/composition guards, at-most-once operations, actual HRESULTs,
  deferred-record ownership, and COM release/reentrancy ordering. Do not force
  all three mechanisms into one unconditional cleanup pipeline.
- [x] High priority: separate Settings baseline loading, draft rendering, and
  acceptance/persistence. Add a narrowly scoped draft-render operation shared
  by import, Restore Defaults, and DPI recreation, with a non-mutating language
  renderer also used by theme refresh. Draft rendering changes neither
  `baseline_config` nor the caller's bound configuration; only explicit
  load/accept operations may replace their respective state. Remove duplicated
  baseline save/restore around control population in
  [transfer](../src/tools/settings/transfer.cpp) and
  [window handling](../src/tools/settings/window.cpp). Preserve validation,
  confirmation, cancellation, save-failure, preview, and notification behavior.
- [ ] High priority: keep installer cleanup policy/plan, execution outcome,
  diagnostic history, and remaining work separate, using plain records and
  functions. Validate a deletable user identity only for selected per-user
  cleanup; machine reboot bookkeeping is independent. Persist self-contained
  quarantine identities sufficient for later pending checks without requiring
  a live original profile. Use the two installer findings above as acceptance
  cases; preserve all existing consent, identity, path, and upgrade protections.
- [x] Isolate the [service fixture](../tests/ime/svc/flow.cpp) with per-run
  registry keys and temporary directories, restoring prior overrides and
  environment values while keeping the borrowed subkey string alive.
- [ ] Medium priority: consolidate repeated test registry/environment setup
  using the existing [ConfigRegistryFixture](../tests/tools/dictmgr/helpers.cpp)
  approach where appropriate. Isolate every module accessing configuration
  before constructing services or loading the tested DLL. Aborting assertions
  bypass destructors: safety must come from isolation, never from restoring
  real preferences.
- [ ] Medium priority: build on the existing
  [CompositionLayoutTask and query/result split](../src/ime/comp/layout.h) to
  clarify first-edit and matching-key-release rechecks. Separate bounded
  diagnostic probing from [presentation orchestration](../src/ime/svc/composition.cpp).
  Retain one-shot scheduling, target/composition/epoch checks, and diagnostics
  inside valid TSF read sessions. Do not retain edit cookies, move host COM
  queries to background threads, or add input delays/debounce timers.
- [ ] Lower priority: extend the existing
  [BuildPaintItems model](../src/ime/ui/candobj.cpp) to share text-line geometry,
  padding, and color-role selection between Direct2D and GDI. Preserve complete
  offscreen frames, renderer fallback diagnostics, clipping, focus, selection,
  and paging. Prefer small shared traversal/model helpers over renderer class
  hierarchies. Measure before adding persistent buffers, caches, or object pools.

Refactoring constraints: do not expand the engine's key-processing FSM for TSF
lifecycle concerns, collapse legitimately overlapping lifecycle flags into one
flat state, or introduce a global event bus. Preserve `CompositionLifecycleState`
member destruction order and CandidateUI's distinct object/HWND teardown rules.
Add focused regressions with each change; use affected x64/x86 builds/tests and
the existing cloud lint workflow, not a new local full-lint pass. Static review
and modeled tests do not certify real hosts or installer upgrade/reboot flows.

## IME skins

### Confirmed design and implemented first-phase source

- [x] Define all 21 bilingual skin names, each Chinese name having two characters,
  and all 42 Light/Dark palettes. See the [design catalog](ime-skins.md).
- [x] Complete the [per-skin colors and state diagrams](ime-skin-palettes.md):
  background, outer border, code title, candidate text, selected background/text/
  border, empty-message text, and enabled/disabled page text. Illustrate first
  page, last page, and empty candidates for both variants of every skin.
- [x] Provide the [registry design template](ime-skins.reg): 21 stable skin IDs,
  42 complete variants, and 13 explicit color fields per variant (546 values).
  Check data/diagram consistency and design contrast. This does not mean the
  reference template has been imported into the real user configuration.
  The source loader and desktop initialization now implement this contract.
- [x] Confirm [theme-combination rules](ime-skins.md#82-皮肤选项与主题模式):
  theme chooses brightness; skin chooses color family. Windows high contrast
  takes precedence, then app high contrast; both bypass skins without clearing
  the selection. Explicit Light/Dark selects that variant. The approved candidate
  automatic-source policy now uniformly uses App mode, including File Explorer
  and system search surfaces. Host detection and Windows-mode exceptions are
  deferred below. Default skin preserves existing colors.

### Phase 1: Implement the complete pure-color skin feature

Execute the following dependent steps in order; add focused regressions with
each behavior change rather than postponing all testing until the final gate.

- [x] Implement the [registry catalog contract](ime-skins.md#84-注册表配置契约).
  Store `ImeSkinId` as REG_SZ, separate from versioned `Skins\Catalogs`
  definitions and `Skins\ActiveCatalog`. Use REG_SZ for names/identifiers and
  REG_DWORD for schema/order and all 13 RGB24 roles; no packed REG_BINARY
  structure or serialized whole-skin string. Colors are `0x00RRGGBB` with a
  zero high byte, not COLORREF's channel order; convert explicitly for rendering.
  Keep the documented stable IDs and bilingual names, not A/B figure numbers
  or translated display names, as persistence identities.
- [x] Initialize the 21 complete Light/Dark definitions through the desktop
  settings flow, preserving existing catalogs and user preferences. IME reads
  must not create missing keys, change ACLs, or bypass restricted-host access.
  Validate schema, encoding, names/order, both variants, all required field
  types/ranges and contrast before publishing a value snapshot. Do not mix
  incomplete skins with fields from another palette. Diagnose real API failures
  and malformed configuration explicitly.
- [x] Publish catalog updates by staging and validating a new complete catalog
  before switching `ActiveCatalog` and updating the existing configuration
  sequence. Callers retain the established configuration-notification flow.
  On write/validation failure, preserve the old catalog, skin
  selection and effective colors; do not automatically delete catalogs that
  existing readers may still use. Resolve/copy colors outside Paint and cache
  the validated snapshot instead of querying registry fields during drawing.
- [x] Add `ImeSkinId` to the shared configuration field and synchronization
  mechanisms. Default missing legacy selections to `default`; reject invalid
  selections on save. Keep skin, theme, text scale and Comfort mode independent.
  Theme changes retain the skin ID; skin changes retain the theme. Preserve
  both preferences across high-contrast entry/exit. Reuse shared color resolution
  for candidate UI, the IME custom quick menu and candidate-style preview.
- [x] Use App mode for the candidate popup and candidate-style preview in all
  hosts, without adding Windows system-surface detection. Keep custom quick
  menus on their existing Windows-mode rule. Manual themes/high contrast stay
  unchanged; the source-policy boundary remains separate from skin loading.
- [x] Apply every required candidate color in Direct2D and GDI: bottom surface,
  outer border, code title/page information, ordinary candidate text, selected
  row background/text/border, empty-message text, and enabled/disabled page text.
  Keep `EmptyText`, `PageEnabledText` and `PageDisabledText` explicitly defined
  even when values equal other roles. Keep candidate numbers the same color as
  their row in Phase 1. Empty candidates must have no fake selection or paging.
- [x] Apply the same skin family to the IME custom quick menu: background,
  border, text, hover/current item, check marks, disabled state, separators and
  keyboard-focus cues. Keep hover, checked and focus states distinct. Do not
  reuse candidate-selection text or title accents indiscriminately for menus.
  Leave native scrollbars system-rendered in Phase 1.
- [x] Add Default plus five common choices and More to candidate settings.
  More opens an owned window with all 21 skins and current-theme sample cards;
  preserve a non-common current selection in the quick dropdown.
  Preview the edited selection without starting composition or recoloring the
  settings window. Preview must obey high-contrast priority, including when
  showing Light/Dark pairs. Save through the existing acceptance/notification
  flow; cancellation must not persist display edits, and failed saves must
  preserve stored values and report the actual error.
- [x] Refresh on saved configuration and system theme/high-contrast changes on
  each window's owning thread. Color-only updates must not steal focus, clear
  composition, change selection, repaginate or move the input caret. Do not set
  the host process's preferred theme. Preserve Direct2D failure diagnostics
  and equivalent pure-color GDI fallback behavior.

### Phase 1 acceptance

- [x] Isolate skin-only regressions in **QiQiSkinTest**, reusing the existing
  runner, retained traces, watchdog and private registry/LOCALAPPDATA fixture.
  Run the collection on x64/x86; default UI/Common/Settings no longer execute
  skin-specific cases. General configuration projections remain in place.
- [x] Verify all 42 variants and 546 explicit fields against the design data,
  including RGB24 conversion, isolated-registry round trips, missing/invalid
  IDs, wrong types/ranges/schema, missing Light/Dark roles, denied reads/writes,
  staged-update failure, and configuration notification/snapshot refresh.
  `QiQiSkinTest` first compares the design table, C++ built-in source and UTF-16LE
  registry template by stable ID, display name, order, variant and all 546
  explicit fields. Native cases verify RGB24 round trips, strict registry shape
  and malformed data, preserve the last-good snapshot on failures, exercise
  denied read/write and transactional publication failure, and assert that a
  configuration notification updates the live candidate appearance without
  changing page, selection or composition. The expanded collection passed
  serial Release runs on x64 and x86.
- [x] Verify first/last/empty candidate states, code title and paging colors,
  selected border, menu hover/check/disabled/focus states, and preview/save/
  cancel behavior. Check text roles at least 4.5:1 and selected-border versus
  ordinary background at least 3:1; do not substitute the decorative outer
  border for that measurement. Preserve the existing app high-contrast 7:1
  target and Windows system-color behavior.
  `QiQiSkinTest` now inspects the candidate paint model and real Direct2D/GDI
  frames for first, last and empty states, including title, candidate, selected
  background/text/border, enabled/disabled paging and empty-message roles.
  The custom-menu test paints hover, checked gutter, disabled text and keyboard
  focus, then verifies Windows system high-contrast colors. Settings coverage
  checks preview pixels plus cancel/save persistence boundaries. Every Light
  and Dark palette is checked explicitly at 4.5:1 for text and 3:1 for the
  selected border; skin-resolved app high contrast is checked at 7:1 and the
  Windows path is checked against system colors. Serial Release runs passed on
  x64 and x86.
- [ ] Complete the environment and host matrix:
  - [x] Automated: cover all four Windows/App mode combinations, explicit
    Light/Dark, candidate/preview App-mode behavior with and without a host
    relationship, unchanged Windows-mode custom menus, system/app high-contrast
    entry/exit, horizontal/vertical layout, long candidates, paging, DPI/text
    scaling, Comfort mode, clipping and small-work-area scrolling. `QiQiSkinTest`
    reuses the existing display, layout and constrained-viewport cases with a
    real skin snapshot; the settings preview and Windows-mode menu are checked
    across the same source matrix. Serial x64/x86 `QiQiSkinTest`,
    `QiQiSettingsTest`, `QiQiUilessTest`, `QiQiCompositionLifecycleTest`, and
    restricted-input diagnostics passed.
  - [ ] Real hosts: separately record Notepad or another ordinary editor, VS
    Code, Explorer address/search/rename, Start/taskbar search, a restricted
    input host, and a host-rendered UILess surface. Record only environment,
    architecture, DPI, theme, skin and pass/fail observations; do not log real
    input, candidate text, window titles or screenshots containing private
    content. Simulated callbacks and native TSF test hosts do not satisfy this
    sub-gate.
    - [ ] **Feedback Hub: candidate skin not applied** (reported 2026-11-21;
      target fix 2026-11-22). In QiQi Settings > Candidates, `Follow Windows` with
      `Peach Blossom` shows a pink preview, but Wubi candidates in Feedback Hub's
      **Summarize your feedback** field still show the default blue highlight.
      The retained log confirms `show_own_ui=true`, so Feedback Hub uses QiQi's
      own popup rather than host-rendered UILess UI. SearchHost hot-reloaded the
      skin while Feedback Hub had been constructed after the saved skin and only
      saw an unchanged config sequence; its `ime_skin_snapshot_` was never
      initialized. Constructor initialization now reuses dynamic settings setup.
      Search/taskbar user retest reads the latest saved skin; recheck Feedback Hub
      after loading the new DLL, saving with **OK**, and opening a fresh process.
      Local log export: `C:\temp\qiqi-ime-events.log` (last modified 2026-11-21
      21:13 local time).
      - [x] **Runtime configuration Broker (implemented 2026-11-23):**
        the signed `QiQiIMEConfigBroker.exe` is the runtime authority for the
        real desktop-user configuration, skin catalog, revision, and user
        dictionary synchronization directory. Production clients no longer
        choose a registry path by readability and never fall back to package-
        private HKCU. They keep a process-local last-valid snapshot; key,
        focus, layout, and painting paths perform no DCOM. Native asynchronous
        COM calls refresh after the existing
        `kGuidCompartmentConfigVersion` wake-up, and explicit Settings, Quick
        Settings, backup, and dictionary writes use expected-revision saves.
        A failed Broker refresh retains the prior snapshot and returns the real
        HRESULT. Product registration is HKLM; x64 installs register both
        native and WOW64 interfaces/proxies. The implementation and validation
        matrix are documented in
        [config-broker.md](config-broker.md). Repeated real-host Broker
        activation remains the final Feedback Hub/UWP/Full Trust/LPAC acceptance
        gate; it is not considered complete merely because unit, x86/x64,
        synchronous, and native-async DCOM tests pass.
- [x] Update user/developer documentation to distinguish implemented surfaces
  from design-only features; automated color checks do not certify all hosts
  or accessibility. Do not generate MSI packages unless explicitly requested.

### Phase 2 and exclusions

- [ ] Add tool-window skins in a later phase, with tool-specific color-role
  mappings rather than applying candidate colors indiscriminately.
- [ ] After Phase 1 acceptance, design/validate gradients, rounded corners,
  subtle separators and ordinary shadows with consistent Direct2D/GDI behavior,
  window bounds and hit testing. Colored glow remains deferred.
- [ ] Separately evaluate candidate-number/hint colors, candidate page-button
  hover/pressed states and native scrollbar adaptation; these require additional
  drawing/state/accessibility work, not just more palette entries.

### Deferred candidate host adaptation

- [ ] Evaluate Windows-mode exceptions for positively identified and real-host-
  validated taskbar/Start search or other specific system input surfaces.
  Current candidates and previews uniformly use App mode; no detection is
  implemented in this change. Classify the actual TSF context's UI, never an
  entire process solely by executable name, Microsoft signature, AppContainer
  identity or immersive flags. File Explorer address/search/rename fields and
  unknown/null contexts must remain on App mode. Document supported Windows
  versions, identification evidence and real-host results for every exception.
  Cache outside Paint and invalidate on context/host changes; reuse existing
  window information, bound probes, and avoid per-keystroke process/signature/
  UI Automation work. Diagnose actual detection failures without input text or
  window titles. App mode is not an application's private per-window theme.
  Validate source-switch correctness and detection counts/timings before enabling
  any exception.

Standalone tools and their menus retain App-mode themes for now; tool skins are
planned for a later phase. The floating status bar and its tooltips do not adopt
candidate skins; their automatic themes remain on App mode. Candidates
and their previews currently use App mode in all hosts. Custom quick menus
keep their existing Windows-mode rule. Do not recolor taskbar brand/mode icons, host composition text/
underlines, system dialogs, Windows-hosted TSF menus or host-rendered UILess
candidates. Skin editors, external images/scripts/skin packs, and additional
status-bar/tooltip skin support require separate design approval.

## Tracked follow-up

- [ ] <a id="wubi-w05"></a> **W05 / P1:** Plan offline Pinyin-to-Wubi code
  lookup (2026-11-19). Proposed first phase:
  full-pinyin single-character queries, showing matching characters and their
  longest available Wubi codes through the
  [existing reverse lookup](../src/ime/engine/engine.h). Report missing codes
  explicitly; do not assume every active dictionary supplies standard full codes.
  Confirm the query entry and selection behavior (commit a character versus
  query/copy a code) before implementation. Preserve existing `z` temporary
  English/wildcard behavior and composition cancellation/unfinished-input policy.
  Evaluate [Unicode Unihan](https://www.unicode.org/reports/tr38/) reading fields
  or [pinyin-data](https://github.com/mozillazg/pinyin-data); retain polyphonic
  readings rather than relying solely on `kMandarin`. Audit the selected files'
  upstream licenses, pin the data version/hash, and distribute required notices;
  a repository's MIT declaration alone does not settle third-party data licensing.
  Generate a read-only reverse index ahead of runtime, with no network or Python
  runtime dependency and no full-dictionary scan per keystroke. Define case/tone
  normalization and `v` for u-diaeresis without merging it with plain `u`.
  Cover homophones, polyphones, supplementary Unicode characters, missing codes,
  cancellation and existing input behavior; measure binary/data size and lookup
  cost. Defer abbreviations, fuzzy spelling, phrase and sentence input; evaluate
  [phrase-pinyin-data](https://github.com/mozillazg/phrase-pinyin-data) separately
  if phrase lookup is later approved. This records a proposal, not implementation.
  Keep Pinyin lookup independent from the optional root-decomposition work below:
  Pinyin supplies matching characters and the existing dictionary supplies Wubi
  codes; root data is not needed to ship code lookup. Share only an extensible
  candidate-annotation model (code, reading, roots) and the selected-candidate
  rendering path, not the data loaders. Implement Pinyin-to-code annotation
  first without waiting for licensed root splits, then enable root artwork as a
  separate capability. Never derive root components from a Wubi key code: one
  key represents multiple roots, and identification codes are not components.
  - [ ] <a id="wubi-w13"></a> **W13 / P3 (data/license-gated):**
    Track a separate optional Pinyin/Wubi candidate annotation for Wubi 86
    root decomposition (research 2026-11-19). Weasel itself renders only Rime
    candidate comments. The available example pipeline stores one character's
    roots, full code, readings and character-set marker in a compiled reverse
    dictionary, maps the root sequence to Private Use Area glyphs with a custom
    font, then writes the result to each candidate comment from a Lua filter.
    That example's `wb_spelling` data says its split data came from unspecified
    online material and names no original author; its root font lacks an embedded
    copyright/license field, and the repository has no declared license. Treat
    the architecture as reference only, not as redistributable source data.
    For QiQi, first obtain a complete per-character Wubi 86 split set with an
    auditable source/provenance and compatible redistribution license. Reuse
    neither general IDS decomposition nor code-to-key guesses as substitute
    evidence. Prefer generated, read-only SVG root glyphs/paths over a required
    installed PUA font so DPI, fallback and high-contrast behavior stay controlled;
    cache the validated snapshot outside Paint. First phase may show root sequence
    and code beside a selected/candidate character only; do not guess absent
    splits, and distinguish valid Wubi splits from simplified-code or
    identification-code explanations. Verify root-name variants, component
    order, simplified/full-code behavior, supplementary characters, missing
    records and ordinary font fallback. Measure data and binary size, cover the
    visible list geometry/clip/paging/accessibility behavior, and record separate
    licenses/notices for the split data and glyph artwork. Phrase/sentence split
    rendering, PUA-font installation, stroke coloring and animation remain later
    work. This is research tracking only, not approved implementation or data.

- [ ] High priority: investigate unfinished code becoming ordinary text when
  switching between VS Code's chat input and code editor in either direction
  with **Discard unfinished input** enabled. Switching to another application
  cleared the preedit in the same retest. The VS Code trace shows termination
  of the current composition while the window still has focus and a successful
  `SetText` call; success does not prove that the original input field changed.
  1. Reproduce both in-window directions and compare them with switching to
     another application. Use one unfinished key in a scratch field and note
     whether it remains as ordinary text after returning.
  2. At [the termination callback](../src/ime/comp/finish.h), correlate focus
     and context events and check whether the owned range is already empty
     before deletion and whether it is empty afterward. Record only range
     state and HRESULTs, not input text. If the range clears but the field
     does not, trace when the host confirms the composition and applies the
     TSF edit to the original input field.
  3. If a safe fix is possible while the composition is still owned, add a
     focused regression and retest both settings in VS Code and Notepad.
     Preserve candidate dismissal and never simulate Backspace or delete
     already committed text.

- [ ] High priority: fix intermittent Log Monitor status-text flicker.
  Reported on 2026-11-17; code inspection found that the 100 ms refresh timer
  unconditionally calls `UpdateStatus`, including while capture is stopped.
  The ordinary `STATIC` footer receives `SetWindowTextW` even when its text
  is unchanged; Metrics view also repeatedly sets its summary and footer.
  Confirm the repaint path at runtime, then update these controls only when
  their displayed text changes, without reducing capture/drain frequency.
  If changed-text updates still flicker, investigate local buffered painting
  rather than forcing whole-window redraws. Add focused regressions proving
  unchanged ticks send no redundant text updates and real status/error,
  language, and view changes remain visible. Verify idle, active and stopped
  capture in Logs, diagnostics and Metrics views, including theme/DPI changes.
  This is tracked work only; the reported flicker has not yet been reproduced
  at runtime or fixed.

- [ ] Fix MSI uninstall failure when optional current-user data cleanup encounters
  a mapped user-dictionary cache (reported in 1.1.1 on 2026-11-16).
  Main-branch implementation now includes the two-choice wizard, protected
  same-volume quarantine, boot-session gate, and isolated regressions.
  Keep this item open until real Windows Settings/full/basic/reduced entry,
  elevated completion and actual reboot/reinstall are validated on a dedicated
  machine. No MSI was generated or installed for the source/fixture checks.
  ETW recorded `Uninstall::RemoveDataFile` returning `0x80070005`; the
  `RemoveUserData` action exited with code 1, causing MSI error 1722, final
  status 1603, and rollback. Read-only inspection found the remaining
  `user_dict.qdict` in use by multiple hosts; an isolated reproduction of the
  current deletion API failed while mapped and succeeded after unmapping.
  This concerns the personal user dictionary/cache, not the three bundled
  default dictionaries (`wubi86_jidian.txt`, `wubi86_jidian_extra.txt`, and
  `wubi86_haifeng.txt`): normal MSI uninstall removes those bundled files
  regardless of the personal-data options. Keep that payload removal separate
  from two independent, unchecked-by-default uninstall choices:
  1. Delete the initiating user's configuration only; preserve dictionary data.
  2. Delete the initiating user's QiQi-managed dictionary data: the personal
     user dictionary, known compiled dictionary caches, phrase-frequency data,
     and known synchronization files; preserve configuration unless option 1
     is also selected.
  Compiled caches here include known QiQi-generated caches of bundled
  dictionaries in current-user managed locations, not just `user_dict.qdict`.
  Preserve external/custom-path source dictionaries, exports, backups,
  unrecognized files, and other users' data. Do not scan drives or delete by
  extension alone. Capture and validate cleanup locations before deleting
  configuration so either choice and their combination remain correct.
  Implement an interactive uninstall wizard with three stages:
  1. Data choices: show the two independent options above on the first page,
     explain the default retention and irreversible deletion scope, and let
     the user cancel before deactivation, unregistration, or data deletion.
  2. Uninstall progress: start only after explicit confirmation; remove the
     program and perform only the selected personal-data cleanup, retaining
     the choices throughout the operation.
  3. Completion: distinguish program removal failure from successful removal
     with complete, reboot-pending, or incomplete personal-data cleanup.
     When a restart is required, explain why and prompt the user to save work
     and restart Windows; do not force an immediate restart or require routine
     manual file deletion.
  Cover interactive entry from Windows Settings as well as MSI maintenance,
  not just the authored full-UI dialog. Provide Chinese/English text and
  verify the actual full/basic/reduced entry flows. Silent install/uninstall
  must not show a wizard; retain explicit choices, the same safety checks,
  accurate logs, and documented success/restart-required/failure exit statuses.
  Define backward compatibility for `QIQI_REMOVE_USER_DATA`. Upgrade removal
  must not opt in to either choice.
  Agreed plan: attempt immediate cleanup first. For a cache still mapped/in
  use, safely rename it to a unique pending-deletion name without replacing
  another file, then use an appropriately elevated operation to call
  [`MoveFileExW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
  with a null destination and `MOVEFILE_DELAY_UNTIL_REBOOT`. Schedule only
  that isolated old cache, never its reusable original pathname, so reinstall
  or resumed use before reboot cannot cause newly created user data to be
  deleted. Preserve initiating-user ownership and path/reparse-point checks
  across elevation; do not resolve personal data from the elevated account.
  No resident cleanup process or routine manual deletion should be required.
  Distinguish files actually deleted, deletion successfully scheduled, and
  incomplete cleanup; successful scheduling is not proof of boot-time deletion.
  Clearly tell the user when program removal is complete but personal-data
  cleanup requires a restart; do not force an immediate restart.
  Persist this product's pending-restart state separately from removable user
  preferences and MSI-owned files, so uninstall and option 1 cannot erase it.
  Keep the initiating-user identity, validated pending-deletion targets, and
  reliable boot identity sufficient for the next installer to check the state
  across elevation, architecture, and UI-language changes.
  Before a subsequent install changes files or registration, check pending
  state left by QiQi uninstall. If the required system restart has not occurred,
  explain why and block installation until the user restarts Windows; cover
  full/basic UI and silent installs with explicit diagnostics and a documented
  non-success exit status. Logging off is not a system restart, and unrelated
  software's generic pending-reboot flags must not alone block QiQi installation.
  After a verified restart, validate the isolated pending targets are gone
  before clearing the marker and permitting installation. If cleanup is still
  incomplete, report the actual problem and preserve the state rather than
  claiming success or repeatedly demanding restarts without diagnosis.
  Report rename/scheduling/state-persistence failures explicitly rather than
  claiming success or rolling back the entire uninstall solely because optional
  cache cleanup is incomplete. Do not treat every access-denied error as a
  mapped-file failure or weaken permission/path validation.
  Preserve actual HRESULT/Win32 diagnostics without logging dictionary contents
  or private paths. Add isolated immediate/mapped deletion, rename/scheduling
  failure, reboot-pending reporting, original-path reuse, partial-cleanup,
  all four configuration/dictionary option combinations, and preservation of
  external source dictionaries/backups and default-retained personal data.
  Cover same-boot installation refusal, verified-restart cleanup checks,
  pending-state survival after configuration deletion, unrelated pending-reboot
  flags, marker read/write failures, first-page defaults and pre-execution
  cancellation, choice preservation, completion/restart messages, and actual
  Settings/full/basic/reduced/silent and Chinese/English installer paths.
  Use fixtures or mocked scheduling/boot identity, not real reboot deletion
  of personal files; preserve upgrade guards. MSI rollback must not imply
  deleted personal data was restored. The implementation is unreleased and does
  not change any already installed `v1.1.1` package's cached uninstall sequence.

- [ ] Deferred: Investigate taskbar input-indicator disappearance and theme-aware icons.
  Follow the [research and validation record](input-indicator.md). The registered
  brand resource now uses the documented white box, black glyph, and 1px
  half-opaque black frame; its DLL resource ID and registration index are unchanged.
  Validate the updated brand in the actual taskbar and Win+Space switcher,
  including light/dark/high-contrast backgrounds, DPI, focus changes, and IME
  switching. Mode-icon theme refresh and disappearance remain separate checks.
  The prepared transparent black/white assets are not registered brand icons.
  Do not treat fixed-color contrast designs, resource generation, or the
  unsuccessful public `SetIcon`/`SetIconMode` probe as proof of automatic
  brand-color inversion.

- [ ] Audit and validate compatibility with the Windows-style Shell.
  Record manual results using the [offline interactive checklist](shell-checklist.html).
  Existing TSF, `UIELEMENTENABLED`, and `IMMERSIVESUPPORT` registration is
  a starting point, not proof of complete compatibility. Check the actual
  taskbar/Win+Space experience, focus and IME switching, desktop and
  AppContainer/restricted hosts, DPI, and supported Windows versions.
  Record real-host results separately from unit tests and category declarations.

- [ ] Immediately after the current source-file split is complete, build two
  dedicated installable IME host fixtures for restricted/package validation.
  Keep these as opt-in integration hosts, not default CTest dependencies:
  - **UWP / AppContainer host:** create the smallest C++/WinRT XAML package
    that contains an editable `TextBox`. Do not declare `runFullTrust`, broad
    filesystem/registry access, networking, process or device capabilities.
    Display and log `TokenIsAppContainer`, package identity and package scope.
  - **Packaged full-trust host:** create a Win32/WinUI packaged host with a
    normal editable control, package identity and `runFullTrust`, similar to
    Feedback Hub. Do not disable MSIX registry virtualization. Its own test
    action may seed a known QiQiIME value in the package-private registry to
    reproduce stale/shadowed configuration while leaving all real user settings
    untouched; log that `TokenIsAppContainer=false` and package identity is
    present.
  Both hosts must expose TraceLogging/ETW and a persistent diagnostic export
  suitable for Log Monitor, without recording entered text, candidate contents,
  clipboard data, window titles or filesystem/user paths. Record startup,
  package/token state, TSF activation/deactivation, focus changes, compartment
  notification values, preserved-key registration/replacement, config/skin
  sequence observations, selected skin ID, and actual HRESULT/API errors.
  Exercise first-load skin, hot skin change, host restart, UWP-supported
  compartments/preserved keys, and the desktop-only status/thread/language-bar
  sinks separately so unsupported contracts are distinguishable from failures.
  Produce separately installable, side-by-side and uninstallable development
  MSIX packages for both hosts, with repeatable packaging scripts and a
  documented test-certificate installation flow. Do not commit signing private
  keys or include these test hosts in the QiQiIME product MSI.
  The UWP/AppContainer host is now implemented at
  [tests/hosts/uwp](../tests/hosts/uwp/README.md), including
  synthetic Search Contract fixtures, privacy-safe TraceLogging/LocalState
  diagnostics, and a repeatable signed-MSIX/installer flow. The packaged
  full-trust host is implemented at
  [tests/hosts/fulltrust](../tests/hosts/fulltrust/README.md), with
  `Windows.FullTrustApplication`/medium-IL identity, default registry
  virtualization, a real Rich Edit input target, TSF search-provider probing,
  package-private shadow injection, desktop-state comparison, and installable
  MSIX packaging. The parent TODO remains open until real-host acceptance.

- [x] Extend the restricted-host matrix with a dedicated LPAC launcher and
  matching packaged-AppContainer upgrades. Keep the existing
  `tests/hosts/uwp` fixture as
  the ordinary, capability-free UWP/AppContainer baseline; do not change its
  package into LPAC. Add an opt-in launcher that creates an explicit
  AppContainer profile/package SID, configures
  `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`, and sets
  `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` to
  `PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT`. Add a launcher mode
  that verifies `TokenIsAppContainer=true`, attempts
  `TokenIsLessPrivilegedAppContainer`, and verifies absence of `S-1-15-2-1`.
  Some supported Windows builds return `ERROR_INVALID_PARAMETER` for
  `TokenIsLessPrivilegedAppContainer`; in that case the launcher must retain
  the explicit all-packages opt-out proof and the child must expose it as an
  inferred LPAC fact. Do not require `S-1-15-2-2` to appear in `TokenGroups`;
  it is an ACL/group concept and may be enforced without appearing in the
  enumerated group array. Upgrade the UWP fixture
  or a close LPAC-compatible Win32 fixture to report those token facts, plus
  its capability SIDs, without logging user/package-sensitive paths. Cover
  distinct broad-group, package-only, zero-capability, `registryRead`, and
  legacy `lpacCom` runs (never combine capabilities by default). The standalone fixture is
  implemented at [tests/hosts/grpperm](../tests/hosts/grpperm/README.md). Smoke
  coverage verifies access denial for the zero-capability profile, the registry
  read granted only to `registryRead`, and COM initialization (but not ordinary
  TSF thread-manager activation) for `lpacCom`. A real interactive `lpacCom`
  run accepted ordinary text while `CoCreateInstance(CLSID_TF_ThreadMgr)`
  returned `ERROR_ACCESS_DENIED`, confirming that `lpacCom` must not be treated
  as broad TSF/UI access. Signed QiQi DCOM-broker activation remains an
  explicit real-host acceptance item. The launcher owns child lifetime and temporary
  profile cleanup; unsupported profiles and extra arguments are rejected with
  stable exit codes. The fixture is tested only by its standalone CTest tree
  and is not included in the normal QiQiIME CTest run or product MSI.

- [ ] Wrap `SafeArrayCreate` ownership in
  [src/ime/ui/canduia.cpp](../src/ime/ui/canduia.cpp) with a scoped SAFEARRAY
  deleter. The current UIA argument construction is correct but manually
  destroys the array; retain the same `UIA_VARIANT` ownership transfer and add
  focused failure tests when changing it.

- [x] Harden candidate owned-window handling for missing or stale host HWNDs.
  Production presentation now requires a valid root owner, hides any stale
  popup, retains the display request and composition, and automatically retries
  after a later layout update supplies a valid host. A window that becomes
  invalid between owner lookup and creation is deferred rather than recreated
  unowned. Diagnostics distinguish missing, invalid, and destroyed owners
  without recording input text. Focused x64/x86 tests cover successful-but-null
  and failed `GetWnd` with null Win32 `::GetFocus()`, invalid/stale HWNDs,
  owner-switch and owner destruction, plus automatic recovery after a valid
  owner returns. Keep Win32 `::GetFocus()` (HWND) distinct from
  `ITfThreadMgr::GetFocus` (document manager).

- [x] Implement candidate-window notifications for
  [light-dismiss surfaces](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-candidate-window-interaction-with-light-dismiss-surfaces).
  The native popup sends `EVENT_OBJECT_IME_SHOW`, `EVENT_OBJECT_IME_HIDE`, and
  `EVENT_OBJECT_IME_CHANGE` through `NotifyWinEvent` with its HWND,
  `OBJID_CLIENT`, and `CHILDID_SELF`. Notifications follow actual visibility and
  geometry, including layout/DPI changes and native destruction; unchanged
  state and hidden staging do not generate duplicates. Scoped native-event
  regressions cover ordering, identifiers and teardown. UILess/blocked or
  uncreated native UI does not announce a visible popup.

- [ ] Validate candidate light-dismiss integration with real transient surfaces.
  Recheck SearchHost while composing and moving the taskbar between bottom,
  top, left and right, including mixed-DPI displays and without refocusing.
  Record the IME WinEvents, TSF text rectangles and actual popup rectangles.
  Do not treat the automated notification contract as proof that stale host
  caret coordinates or taskbar-search placement are fixed.
  The first-key stale-extent path now requests one guarded asynchronous
  confirmation for a visible native popup without a Win32 caret. Captured
  confirmation callbacks still returned stale whole-composition coordinates.
  Position queries now use a clone collapsed to the composition start, following
  the reviewed Weasel approach without modifying the original range/selection.
  A later marked capture showed that collapsed-start and selection/display
  extents can all remain stale before and after the first edit. One matching
  first-key release now triggers an additional guarded read, following Weasel's
  release-stage update boundary without modifying key handling or adding timers.
  Automated moved-work-area and stale-task regressions do not replace this host
  check.

- [ ] Audit IME DPI scaling against the
  [desktop-application DPI requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#dpi-scaling-support-for-desktop-applications).
  Existing monitor-DPI queries and scaling helpers are a baseline, not a
  completed conformance check. Ensure the IME DLL inherits the host's DPI
  awareness: audit its embedded manifest, process-level awareness setters,
  and reachable shared code. Review and justify any temporary thread-DPI
  overrides, restoring the host context on every exit and reentrant path.
  Keep standalone tool EXEs' DPI policies separate from the in-process IME.
  Verify candidate/status/menu UI, fonts, icons, geometry, and hit testing
  across DPI-unaware, system-aware, and per-monitor-aware v1/v2 hosts.
  Check TSF caret extents and Win32 fallback coordinate virtualization,
  GetDpiForMonitor/GetDpiForWindow selection, error/fallback diagnostics,
  and avoidance of double scaling. Cover mixed-DPI monitor transitions,
  dynamic DPI changes, negative coordinates, and 100/125/150/175/200% scales
  with focused regressions and real-host validation on x64/x86.

- [x] Audit and complete
  [IME candidate-list accessibility for Narrator](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-accessibility).
  The native candidate popup now exposes an exact
  `UIA_AutomationIdPropertyId` value of `IME_Candidate_Window`, a fragment-root
  list with current-page item providers and `UIA_NamePropertyId` candidate text.
  Visual code annotations are intentionally omitted from `UIA_HelpTextPropertyId`
  so Narrator announces the selected character or phrase rather than its code.
  Selection items expose
  `UIA_SelectionItemIsSelectedPropertyId` and support selection through the
  existing candidate callback. The popup raises `UIA_MenuOpenedEventId`,
  `UIA_MenuClosedEventId`, and `UIA_SelectionItem_ElementSelectedEventId` only
  for actual native visibility; candidate/selection changes clear stale item
  state, and UILess presentation does not announce a nonexistent HWND.
  Events originate from the actual root/current item provider so clients can
  correlate them with the exposed tree, and are skipped when no UIA client is
  listening. Candidate-tree changes raise structure invalidation before the
  new selection event. Focused x64/x86
  regressions cover provider properties, sibling navigation, hit testing,
  runtime IDs, selection, `WM_GETOBJECT`, open/close and UILess gating.
  Real Narrator output remains a separate manual-host check because unit
  providers and event observers do not certify speech behavior.
  These UIA events do not replace the IME light-dismiss WinEvents above.
  Add property/event regressions using synthetic candidates and manual
  Narrator validation; do not capture real input or candidate text in logs.

- [x] Implement UI Automation support for candidate-window paging controls.
  Previous/next controls are stable UIA Button providers with localized names,
  stable Automation IDs/runtime IDs, enabled/offscreen state, hit testing,
  `IInvokeProvider`, and fragment sibling navigation alongside candidate items.
  The candidate list exposes current/total page status, and paging raises status/
  enabled property changes after structure invalidation. Hidden controls are not
  present on single-page lists; boundary controls remain visible but disabled.
  Focused x64/x86 regressions cover first, middle, last, and single-page states.
  Real control discovery and speech remain subject to separate Inspect/Narrator
  validation; automated providers and events do not certify the spoken output.

- [ ] Audit and complete UI Automation and Narrator support for the standalone
  tools: Settings, Dictionary Manager, and Log Monitor. Keep native
  `BUTTON`, `EDIT`, `COMBOBOX`, `ListView`, `TreeView`, tab, toolbar, and dialog
  providers intact; do not replace a usable Windows provider solely to add
  presentation metadata. Inventory owner-drawn/custom controls and list rows,
  including Fluent toggles, combo items, toolbars, inline dictionary editing,
  splitters, menus, status text, and changing Metrics/log views. Provide stable
  localized names, roles, values, enabled/checked/selected/offscreen states,
  item selection and keyboard focus behavior, and required property or structure
  events where the native provider cannot represent the displayed state.
  Preserve `WM_GETOBJECT` and subclass forwarding so custom handling does not
  break the host UIA provider. Add synthetic-data UIA regressions for x64/x86
  across language, theme/high contrast, DPI/text scaling, dynamic recreation,
  empty/error states, paging/filtering, and focus changes. Validate the resulting
  trees and announcements with Inspect/Accessibility Insights and real
  Narrator; do not log user dictionary entries, log messages, paths, clipboard
  content, or other private tool data. The candidate-window provider is not a
  substitute for this standalone-tool work.

- [x] Complete and validate TSF UILess-mode conformance.
  Candidate UI now publishes the active `ITfDocumentMgr`; the first update
  reports all candidate fields and subsequent updates report only changed
  document/count/string/selection/page flags. Empty lists return `S_FALSE` from
  `GetSelection`. The host's `pbShow` result controls native visibility: a false
  result remains hidden across updates and receives the required initial
  `UpdateUIElement`, while own-UI approval follows the ordinary popup path.
  Manager queries, `BeginUIElement` failures and invalid IDs cannot fall back
  to an unnegotiated popup. Begin/Update/End preserve actual HRESULTs, stale
  callback guards and reentrant document ownership; End clears the association
  only if no replacement presentation has begun. Failed retirement keeps its
  exact manager/ID and old snapshot as pending teardown, blocks replacement
  Begin, and is retried until End succeeds rather than abandoning the
  registration. Mock regressions cover manager/Begin/update/end failures,
  invalid IDs, host hide/reshow, exact update flags, manager replacement and
  stale/reentrant compensation. A native TSF test activates a real
  `ITfThreadMgr2` UILess thread and verifies `ITfUIElementMgr`/Sink IDs, all-first
  flags, subsequent flags, document identity, hidden visibility and lifecycle
  end on x64/x86. External full-screen hosts may still need separate rendering
  validation, but the Windows UILess contract is no longer backed only by mocks.
  Candidate registration ownership is now driven by a dedicated
  `Idle`/`Beginning`/`Active`/`Updating`/`Retiring`/`Ending` FSM rather than
  scattered service, UI, and presentation booleans; direct transition-table
  tests cover blocked nested Begin/Update/End and failed-retirement recovery.

- [ ] Implement and validate the Windows IME search-integration APIs.
  **Deferred.** API implementation and automated contract coverage are in place,
  but real Windows taskbar-search acceptance is still open. In a Win+S retest on
  Windows 11 build 26300.9911, the diagnostic DLL loaded in SearchHost and
  registered its function provider (`S_OK`). The only `GetFunction` request
  was for a different interface; no candidate query or integration-style call
  was observed. TSF allowed the ordinary popup and search still used the
  uncommitted raw code. The user reported the same Win+S behavior with a built-in
  Microsoft IME on that system. This suggests a possible host or system
  limitation, but the cause is unconfirmed. Keep this item open until real
  pre-commit results use the conversion candidates.
  `ITfFnSearchCandidateProvider` is exposed through
  `ITfFunctionProvider::GetFunction`. `GetSearchCandidates` performs a read-only
  Wubi lookup and returns immutable `ITfCandidateList` objects without changing
  live composition or emitting TSF/input events. Search application IDs and
  queries are not retained or logged; `SetResult` deliberately returns
  `E_NOTIMPL` so search selection never silently learns sensitive query history.
  Candidate UI implements both `ITfCandidateListUIElementBehavior` and
  `ITfIntegratableCandidateListUIElement`: Searchbox integration hides the TIP
  popup, advertises active selection and candidate numbers, accepts host
  selection, routes supported keydown operations through the ordinary service
  state machine, and supports selected/final-exact finalize plus abort.
  Integrated `GetString` exposes raw values without display-only code hints;
  ordinary UILess hosts retain their annotated display strings, and both modes
  preserve the global provider/UI index mapping. The independent
  `QiQiSearchIntegrationTest` covers COM discovery, candidate enumeration/clone,
  provider no-side-effect behavior, privacy feedback, all integration methods,
  cross-page selection, exact/raw finalization, and cancellation. The diagnostic
  regression checks provider identity after mock-manager activation and uses
  real private in-process ETW with synthetic canaries to verify log redaction.
  Native provider registration does not establish discovery of the search
  interface, and passing mocks does not replace host acceptance. Native UILess
  acceptance also verifies Behavior and Integratable QueryInterface support.
  Privacy-safe `SearchIntegration` diagnostics distinguish provider registration,
  function requests, candidate queries, style negotiation, and feedback;
  [capture instructions](log.md#search-integration) require activation/discovery
  coverage before drawing conclusions about missing calls.
  This remains Windows TSF contract conformance, not a claim that every external
  search host or Secure Desktop control has been manually exercised.

- [x] Add automated conformance tests for
  [Microsoft's IME icon requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-icons).
  [icons.ps1](../tests/ime/icons.ps1) validates the registered
  [qiqiime.ico](../data/qiqiime.ico) as a source asset, including every supplied
  32-bit alpha frame, required 16/20/24/32/40/48px sizes, neutral black/white
  artwork, opaque white box, black glyph, exact 1px 50%-black outer stroke and
  unclipped standard square variant. QiQiIME uses the standard brand variant;
  no upper-left-rounded "new" asset is registered, so rounded-corner checks do
  not apply and the square corners are asserted intentionally.
  `QiQiImeRuntimeIconTest` loads the actual x64/x86 IME DLL group-icon directory
  and verifies all embedded frames rather than relying on Windows scaling a
  missing size. It also validates every runtime mode icon returned through
  `GUID_LBI_INPUTMODE`: fixed white glyph, exact 1px 50%-black outer stroke,
  neutral antialiasing, transparent unclipped margins, light/dark compositing
  contrast, and identical pixels across app/taskbar theme changes. Direct
  16px (indicator) and 20px (UAC) render paths are both covered. Standalone tool
  icons remain outside this IME-only contract.

- [x] Replace standalone Settings transfer and the custom Dictionary Manager
  backup format with one complete ZIP backup format.
  Settings exposes “Restore backup...” / “Create backup...” on the existing
  Dictionary page. A restore requires Merge or Replace, applies immediately
  after full validation, refreshes Settings, and does not roll back if the
  Settings window is later cancelled. Creation exports the saved consistent
  state, not unsaved draft edits. Bare `.json` and the former `.qbak` format are
  intentionally unsupported.
  Use UTF-8 JSON (`.json`) for configuration, with explicit `format` and `version`,
  native boolean/numeric values, and stable string enum names. Use ZIP (`.zip`)
  for complete backups containing configuration JSON, user dictionary, phrase
  ordering data, and any selected custom primary dictionary. Do not export
  machine-specific absolute paths; restore bundled files using portable references.
  Validate the
  entire input before applying changes, and explicitly report write failures
  or partial restoration rather than claiming success. Reuse shared configuration
  field definitions and file helpers, remove the superseded custom parser, and
  add focused round-trip, malformed-input, validation, and failure-path tests.
  Treat imports as untrusted input: strictly validate types, ranges, enum names,
  format/version, and reject unknown fields and duplicate keys with actionable
  field-specific errors. Missing settings retain their current values. Only explicitly supported settings may be
  imported, never arbitrary registry keys or machine-specific paths. Show a
  conflict-mode choice before restoring. Validation failure or cancellation must
  leave the current state unchanged; failed or partial writes retain the actual
  HRESULT and explicitly disclose that effects may already exist.
  The dictionary manager now uses miniz to write a Deflate-compressed,
  CRC-checked portable ZIP with
  `manifest.json`, `config.json`, user dictionary and phrase-order text files,
  plus `custom-primary.txt` or `custom-primary.qdict` when a custom primary
  dictionary is selected. `.qbak` input is intentionally rejected. Restore
  fully validates archive structure, manifest references, configuration JSON
  and every dictionary entry before applying changes; partial write/refresh
  failures retain the actual HRESULT. Common code owns the archive contract and
  UI/result helpers; engine code owns `WubiEngine` state and custom dictionary
  validation so `qiqi_common` never depends on `ime` or either tool. Settings and
  Dictionary Manager share that layer without referencing each other.

- [x] High priority: add an independent prefix-completion setting.
  Use "编码补全" / "Code completion" as the Chinese / English UI labels.
  Preserve the current enabled default. When disabled, show only exact code
  matches while retaining valid short-code candidates. Persist the setting,
  expose it in the settings UI, and add focused lookup/configuration tests.
  Prioritize this task over the short-code and single-character controls below.

- [ ] <a id="wubi-w03"></a> **W03 / P1:** Add short-code control / full-code practice mode.
  Confirm the intended behavior before implementation, including whether
  incomplete-code hints remain visible and how custom short codes are handled.
  If full-code-only mode is chosen, prevent short-code commits through every
  selection path and identify actual complete codes rather than requiring four
  letters universally. Keep this separate from prefix completion, preserve
  dictionary files, and add focused lookup and commit-path tests.

- [ ] <a id="wubi-w02"></a> **W02 / P1:** Add an optional single-character-only candidate mode.
  Keep the mode disabled by default. Filter multi-character Wubi dictionary
  candidates without modifying dictionary files or affecting punctuation and
  temporary English. Confirm how user-defined phrases are handled before
  implementation. Keep this independent of short-code control, prefix completion,
  dynamic phrase ordering, and automatic phrase creation. Persist the setting,
  expose it in the settings UI, and add focused filtering and commit tests,
  including the effect on unique full-code auto-commit.
  Count Unicode characters, not UTF-16 code units; reuse the existing splitter.
  Apply filtering consistently to exact, prefix and wildcard candidates before
  candidate limits/paging, including user and pinned entries and every commit path.

- [ ] <a id="wubi-w06"></a> **W06 / P2:** Add an optional CapsLock Chinese/English mode-switch setting.
  Preserve CapsLock English pass-through as the default. Reuse the shared mode
  switching path and intercept CapsLock in the optional mode so switching does
  not also toggle the OS uppercase lock. Confirm pending-composition behavior
  and handling of an already-enabled OS CapsLock state before implementation.
  Persist the setting and expose it in the settings UI. Keep TSF compartments
  and mode indicators synchronized, preserve restricted-input behavior, and
  add focused tests for prediction callbacks, key repeat, composition handling,
  and both CapsLock modes.

- [ ] <a id="wubi-w01"></a> **W01 / P1:** Add a persisted three-option Space/Enter
  behavior setting for Wubi composition.
  Expose mutually exclusive options in the settings UI with these labels:
  - "空格选字，回车输出编码" (default): Space commits the highlighted
    candidate; Enter commits the raw code.
  - "空格选字，回车清码": Space commits the highlighted candidate;
    Enter cancels the current composition.
  - "回车选字，空格清码": Enter commits the highlighted candidate;
    Space cancels the current composition.
  Preserve existing defaults, including the current empty-code clearing option.
  Define no-candidate behavior and interaction with that option before
  implementation. Keep Esc cancellation, temporary English, and keys outside
  Wubi composition unchanged. Add focused configuration, prediction, and
  key-handling tests for all three options, including highlighted selection
  and no-candidate cases.

- [ ] Measure input-path allocation pressure and deferred edit-session retention.
  Count allocations and allocated bytes per key/edit request, including result
  snapshot copies and stored callbacks; measure snapshot size and requeue counts.
  Track pending edit-session count and retained-byte peaks under normal and slow
  host delivery. Measure heap usage after sustained input and queue drainage to
  distinguish temporary retention, leaks, allocator caching, and fragmentation.
  Compare metrics-disabled and metrics-enabled runs to quantify measurement
  overhead. Use evidence before changing snapshot ownership, callback storage,
  or introducing object pools; do not record input text or candidate contents.

- [ ] <a id="wubi-w07"></a> **W07 / P2:** Add configurable hotkeys and selectable
  Haifeng/Jidian key presets.
  Verify the default bindings against documented versions before defining the
  presets, and confirm whether they cover shortcuts only or broader key behavior.
  Let users select a preset in settings and customize individual bindings.
  Validate conflicts, persist configuration, and safely replace registered TSF
  preserved keys when settings change. Preserve current defaults and restricted
  input behavior; add focused configuration, registration, and key-handling tests.
  Replace a binding with `ITfKeystrokeMgr::PreserveKey` for the new key first,
  then `UnpreserveKey` for the old key only after registration succeeds. Keep
  the previous binding and report a conflict when registration returns
  `TF_E_ALREADY_EXISTS`. Implement `ITfPreservedKeyNotifySink::OnUpdated` as an
  optional conflict-state observer for preserved keys becoming available or
  unavailable; it reports only `TF_PRESERVEDKEY`, so requery with
  `IsPreservedKey`. Do not use the notify sink to trigger commands or as the
  hotkey update mechanism; command dispatch remains in `OnPreservedKey`.
  Include optional comma/period paging, a code-hint toggle, an open-settings
  command and a Shift+Delete alias, with explicit punctuation/application-conflict
  handling. Add bindings for single-character mode, character sets, conversion,
  mixed input, readings and root hints only as those capabilities become available.
  Do not advertise one Rime/QQ/Sogou binding set as a cross-platform standard.

- [ ] Long-term, deferred (W13 / P3 extension): show candidate-specific Wubi root-code hints.
  Display the roots used to encode each candidate beside its label, including
  roots selected by phrase-coding rules rather than full decomposition of every
  character. This is not a keyboard root chart or stroke animation. Before
  implementation, verify redistribution licenses for decomposition data and
  root fonts, Wubi 86 consistency with active dictionaries, and handling of
  short codes and identification codes. Add an optional display setting and
  validate rendering and hint accuracy when this task is scheduled; do not
  import data or implement the feature yet.
