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
|  - WubiEngine       : 编译期 FSM、四码唯一上屏、空码清屏/提示音、顶字 |
|  - WubiMappedDict   : 4 字节紧凑编码索引 + 只读按需分页内存映射 (.qdict)|
|  - Common           : INI 原子配置、ETW TraceLogging 日志、ScopedHandle RAII |
+-----------------------------------------------------------------------+
```

之所以将 `qiqi_engine` 拆分为独立的静态库：
1. **无 COM 依赖即可测试**：`qiqi_engine_test.exe` 可以直接验证所有按键与五笔上屏规则，无需先通过 `regsvr32` 注入宿主进程。
2. **宿主安全**：输入法 DLL 会被注入到资源管理器、浏览器、Office 等进程中，引擎层保持纯内存/只读映射计算，避免阻塞宿主 UI 线程。

源码按 [`src/common`](../src/common)、[`src/Ime`](../src/Ime)、[`src/tools/settings`](../src/tools/settings) 分层，测试采用对应布局。公共 D2D 控件由 `qiqi_common` 统一编译；设置及关于窗口由 `qiqi_settings_ui` 统一编译，供独立设置程序和 IME 的进程内设置回退路径复用。每个模块及对应测试目录均有自己的 `CMakeLists.txt`，顶层文件只负责组织，新增工具使用独立的 `tools/<工具名>` 子目录。

---

## 2. 关键子系统与安全设计

### 2.1 注册阶段（`src/Ime/tsf/register.cpp`）
- **COM InProcServer32（支持 `x64` + `x86` WOW64 双视图）**：注册 `kClsidQiQiTextService`，线程模型为 `Apartment`（STA）。在 64 位 Windows 上分别通过 `System32\regsvr32.exe` 与 `SysWOW64\regsvr32.exe` 注册 `x64` 与 `x86` DLL，其中 WOW64 进程自动检测已存在的 64 位注册项并避免覆盖全局共享的 TSF Language Profile。
- **Language Profile**：通过 `ITfInputProcessorProfiles::AddLanguageProfile` 注册到 `zh-CN` (`0x0804`)。
- **Categories（`ITfCategoryMgr`）**：
  - `GUID_TFCAT_TIP_KEYBOARD`：声明为键盘输入法。
  - `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`：声明支持 `ITfUIElementMgr`，使支持 TSF UILess 的全屏游戏能够激活输入法并接管候选框渲染。
  - `GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER`：声明提供组合串虚线下划线显示属性。
  - `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` / `GUID_TFCAT_TIPCAP_COMLESS` / `GUID_TFCAT_TIPCAP_SECUREMODE`：提高现代 Windows 与安全输入框下的兼容性。

### 2.2 激活与停用（`ActivateEx` / `Deactivate`）
- 实现 `ITfTextInputProcessorEx::ActivateEx`，记录宿主进程名、`client_id` 与 `dwFlags`（包含是否处于 `TF_TMAE_UIELEMENTENABLEDONLY` UILess 模式）。
- 挂载 `ITfThreadMgrEventSink`、`ITfKeyEventSink`、`ITfFunctionProvider` 与 `ITfLangBarItemButton`，并注册保留热键（`Ctrl+Space` 中英切换、`Shift+Space` 全半角切换、`Ctrl+.` 中英标点切换）。

### 2.3 按键处理与编辑会话（`OnTestKeyDown` -> `OnKeyDown` -> `RequestEditSession`）
TSF 的按键处理分为两步：
1. `OnTestKeyDown`：仅询问输入法**是否打算消费该按键**（`engine_.ShouldEatKey(...)`）。只消费当前状态下真正需要处理的键，绝不在空闲状态下吞掉 `Enter`、`Backspace` 或方向键。
2. `OnKeyDown`：推进 `WubiEngine` 编译期有限状态机（`fsm.h`），得到 `KeyProcessResult`（`UpdateState` / `Commit` / `Cancel`），触发音效（若启用了按键音或空码提示音），然后通过 `ITfContext::RequestEditSession` 提交 `LambdaEditSession`。

在 `DoEditSession(TfEditCookie ec)` 内完成：
- **新建或更新组合串**：`ITfInsertAtSelection::InsertTextAtSelection` + `ITfContextComposition::StartComposition` + `ITfRange::SetText` + 设置 `GUID_PROP_ATTRIBUTE` 下划线显示属性。
- **上屏并结束组合**：清除显示属性、将范围文本替换为最终汉字、折叠选区到末尾、调用 `ITfComposition::EndComposition`。
- **查询光标坐标**：通过 `ITfContextView::GetTextExt` 获取组合范围的屏幕矩形，移动候选框并做多显示器工作区边界裁剪。

### 2.4 异常处理与宿主兼容（`OnCompositionTerminated` / `OnSetFocus`）
当用户在浏览器、Word 或 Excel 中用鼠标点击其他位置，或前端框架重绘 DOM 导致组合被宿主强行结束时，TSF 会回调：
- `ITfCompositionSink::OnCompositionTerminated`
- `ITfThreadMgrEventSink::OnSetFocus`

`QiQiTextService` 会立即清空 `WubiEngine` 缓冲、释放 `ITfComposition` 并关闭 `CandidateUI`，同时在 `OnSetFocus` 时按需检查 `%LOCALAPPDATA%\QiQiIME\config.ini` 修改时间戳实现无感热重载。

### 2.5 桌面弹窗与游戏 UILess 双模式（`src/Ime/ui/candidate_ui.cpp`）
每次需要显示候选词时，先调用 `ITfUIElementMgr::BeginUIElement(candidate_ui_, &show_own_ui, &id)`：
- 若宿主应用返回 `show_own_ui == TRUE`（桌面应用）：显示自带的 `WS_EX_TOPMOST | WS_EX_NOACTIVATE` 候选窗口。
- 若宿主应用返回 `show_own_ui == FALSE`（全屏游戏 UILess 模式）：隐藏自带弹窗，仅通过 `UpdateUIElement` 通知游戏，由游戏调用 `ITfCandidateListUIElement` 接口自行渲染。

### 2.6 内存映射词库与最小权限安全模型（`src/Ime/engine/binary_dict.cpp` & `src/common/file_util.cpp`）
- **零隐私数据映射**：`.qdict` 映射文件仅包含静态五笔码表的只读索引（`DictBinaryHeader` + `DictCodeIndex` + `DictWordSlice` + `wchar_t` 字符池），不包含任何用户实时按键或输入状态；映射对象为匿名内核对象（`lpName == nullptr`）。
- **最小权限访问、防篡改锁与无锁热替换（`FILE_SHARE_READ | FILE_SHARE_DELETE`）**：
  - 映射读取 `.qdict` 时，通过 `CreateFileW` 仅申请 `GENERIC_READ` 权限与 `FILE_SHARE_READ | FILE_SHARE_DELETE` 共享模式：既禁止任何进程在映射期间原地写入或截断文件页（100% 防止 `STATUS_IN_PAGE_ERROR`），又允许其他进程在 `.txt` 词库更新后通过 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换目录项（POSIX Unlink 语义）。
  - 旧进程继续安全读取旧映射视图，并在下次 `OnSetFocus`（`HasModifiedMappedDictionary()`）时无缝切换至新 `.qdict` 映射；同时启用 `SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS` 与 `FILE_TYPE_DISK` 校验，阻断命名管道令牌模拟攻击。
  - 生成 `.qdict` 缓存与保存 `config.ini` 时，统一通过 `WriteFileAtomically` 以 `GENERIC_WRITE` + `dwShareMode = 0`（独占写）写入同目录临时文件 `.tmp.<pid>`（带 `gsl::finally` 异常/错误自动清理），完成后通过 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换目标文件。

### 2.7 全链路 RAII、多显示器 DPI 与矢量抗锯齿 UI（`src/common/` & `src/Ime/ui/`）
- **泛型零开销 RAII（`ScopedHandle<Traits>`）**：所有 Win32 内核句柄、文件映射视图、注册表键、GDI 对象（`HFONT`/`HBRUSH`/`HPEN`/`HBITMAP`/`HDC`）、菜单（`HMENU`）、窗口（`HWND`）与动态库（`HMODULE`）均由移动语义 RAII 包装器管理。
- **可复用 D2D 控件（[`d2d_controls.h`](../src/common/d2d_controls.h) / [`d2d_controls.cpp`](../src/common/d2d_controls.cpp)）**：头文件仅提供声明，实现集中管理 Fluent 卡片、主次按钮、开关、单选框与下拉框的绘制和子类化；设置页及关于页复用同一模块，不在控件层依赖业务控件 ID 或父窗口的 `GWLP_USERDATA` 类型。每个窗口组合一个 `FluentD2DThemeContext`，通过现有 RAII 包装器持有 D2D/DWrite/GDI 资源；控件创建失败时自动回收尚未移交的窗口并记录日志，成功后由父窗口销毁子控件。下拉框借用的上下文须保持地址稳定，且存活到子控件销毁之后；单选组由调用方通过 `MAKELONG(first_id, last_id)` 指定连续 ID 范围。父窗口将 `WM_DRAWITEM` 分派给相应绘制函数，主题变更时调用 `RefreshThemeBrushes()`；卡片绘制在调用方的 `ScopedD2DDcDraw` 会话内进行，其余控件自行管理绘制会话。
- **多显示器与 Per-Monitor DPI（`monitor_dpi.h`）**：候选窗、浮动状态栏、Tooltip、设置窗口与关于窗口均通过 `MonitorFromPoint` + `GetDpiForMonitor` 适配任意副屏坐标（含负坐标副屏）与高分屏缩放，并在跨屏拖拽（`WM_DPICHANGED` / `WM_EXITSIZEMOVE` / `WM_DISPLAYCHANGE`）时自动重算字体与边界裁剪。
- **4x4 超采样抗锯齿状态条图标（`status_bar.cpp`）**：使用有向距离场（SDF）+ 4x4 超采样实时生成 32 位预乘 ARGB `DIBSection` 位图，通过 `GdiAlphaBlend` 渲染半角月牙、全角圆环、中英文标点与八齿设置齿轮图标。

- **设置窗口职责拆分**：[`window.cpp`](../src/tools/settings/window.cpp) 负责窗口创建、消息分派与命令处理；[`controls.cpp`](../src/tools/settings/controls.cpp) 负责控件布局、配置绑定、语言更新与卡片绘制；[`about.cpp`](../src/tools/settings/about.cpp) 独立实现关于窗口。内部上下文和控件 ID 集中在 [`internal.h`](../src/tools/settings/internal.h)，共享图标及模态消息循环位于 [`util.h`](../src/tools/settings/util.h)。原有 [`window.h`](../src/tools/settings/window.h) 公共接口保持不变，窗口仍在 `WM_NCDESTROY` 中回收 RAII 上下文。

- **控件交互与退出边界**：开关和单选框仅在有效按下后的释放事件中切换，取消点击或孤立的释放事件不改变状态。下拉框在下游消息处理返回后重新确认子类化上下文仍有效，避免回调销毁控件后访问已释放资源。模态消息循环保留并重新投递 `WM_QUIT`，遇到消息读取错误或窗口被外部销毁时返回失败，不再访问已回收的窗口上下文。

### 2.8 多进程词库同步、动态字库切换与深层 TSF 健壮性设计

1. **多进程零延迟同步与状态共享（TSF Global Compartment `ITfCompartmentEventSink`，`tsf-global-compartment-bus`）**
   - 彻底移除基于 `FindWindowExW` + `kConfigChangedBroadcastMsg` 的 Win32 窗口广播。
   - 通过 `ITfThreadMgr::GetGlobalCompartment()` 订阅由系统 `ctfmon.exe` 维护的全局 Compartment：
     - **配置/词库版本号 (`kGuidCompartmentConfigVersion`)**：当发生配置修改、词库切换或在线造词（`Ctrl+=`）时，递增全局版本号，`ctfmon.exe` 立即向全系统所有已激活进程推送 `ITfCompartmentEventSink::OnChange` 回调，触发毫秒级热重载与浮动状态栏显隐同步。
     - **浮动状态栏坐标跨进程记忆 (`kGuidCompartmentStatusBarPos`)**：用户在进程 A 拖动 `StatusBarUI` 后，将坐标打包写入 Global Compartment；当焦点切换到进程 B 时，浮动状态条直接出现在用户刚刚拖到的位置，不再跳回右下角默认位置。
   - **系统 Compartment 双向监听 (`tsf-bidirectional-compartment-sync`)**：同时在 `ITfCompartmentEventSink` 中监听 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 与 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`，当外部（如 Windows 系统托盘指示器或宿主应用）修改输入模式时，反向同步回 `WubiEngine` 与状态栏。

2. **“一库一 `.qdict`”与自定义字库（`user_dict.txt`）同步机制**
   - **静态主/扩展词库**：极点核心（`wubi86_jidian.qdict`）、极点扩展（`wubi86_jidian_extra.qdict`）与海峰超大字符集（`wubi86_haifeng.qdict`）各自独立编译为一个 `.qdict` 文件，支持按需组合挂载，切换词库无需重新合并编译（耗时 `< 1ms`）。
   - **动态用户自造词（`user_dict.txt`）**：在 `WubiEngine::AddUserPhrase` 向 `user_dict.txt` 追加新词后，同步调用 `WubiMappedDict::CompileTextFileToQdict` 原子替换 `user_dict.qdict`（利用 `FILE_SHARE_READ | FILE_SHARE_DELETE` 不阻塞任何已映射进程），并触发 Global Compartment 广播使所有进程立即生效。

3. **深层 TSF 输入体验与宿主兼容机制**
   - **异步排版光标跟随（`ITfTextLayoutSink` + Caret 兜底，`tsf-text-layout-sink-caret`）**：针对 Chromium / VS Code / WPF / Word 等异步排版宿主在首键插入组合串时 `GetTextExt` 返回 `TS_E_NOLAYOUT` 的情况，通过 `ITfTextLayoutSink::OnLayoutChange` 在宿主完成排版后立即刷新候选窗位置，并以 `GetGUIThreadInfo`（`rcCaret`）/ 焦点窗口坐标兜底，避免候选窗跳至 `(0, 0)`。
   - **Shift 键中英切换防误触（`shift-tap-anti-mistrigger`）**：记录 `VK_SHIFT` 按下时间戳（`GetTickCount64()`）并检测鼠标按键状态（`VK_LBUTTON` / `VK_RBUTTON` / `VK_MBUTTON`），仅当单按时长 `<= 350ms` 且期间无鼠标点击与其它按键时才触发中英切换，防止 `Shift+Click` 选区或长按 `Shift` 误切模式。
   - **数字小键盘支持（`numpad-and-composing-digits`）**：在编译期 FSM 与全角模式中支持 `VK_NUMPAD0..9` 与 `VK_DECIMAL`，防止组合态下按小键盘数字穿透破坏正在输入的编码串。
   - **禁用态与密码框上下文感知（`tsf-disabled-context-awareness`）**：感知 `GUID_COMPARTMENT_KEYBOARD_DISABLED` 与 `GUID_COMPARTMENT_EMPTYCONTEXT`，在密码框、只读控件及游戏非聊天状态下自动旁路放行按键。
   - **异步 EditSession 状态快照隔离（`async-editsession-snapshot`）**：在 `KeyProcessResult` 中携带自包含的候选/分页快照传入 `LambdaEditSession`，避免 `TF_ES_ASYNCDONTCARE` 异步执行时读取到超前或滞后的引擎状态。
   - **词组动态调频跨会话持久化（`persist-dynamic-phrase-freq`）**：当启用 `dynamic_phrase_freq` 时持久化 `promoted_phrases_`，使高频词组排序在跨进程和重启后保持一致。

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
- [x] **8. 组合态大写字母顶字上屏、四码空码自动清屏（`clear_on_empty_4code`）与零分配 WAV 音效（`sound.h`）**
- [x] **9. 配置文件与内存映射词库最小权限访问、原子写与带进程名的结构化排障日志**

### 待办与后续扩展（TODO，按优先级排序）

#### P0：核心日常打字高频增强
- [x] **10. 分号 `;` / 单引号 `'` 二、三级候选快捷上屏（`semicolon-quote-quick-select`）**：在组合态（`Composing1..4`）下支持通过 `;` 键直接上屏第 2 候选词、`'` 键直接上屏第 3 候选词（由配置项开关控制）。
- [x] **11. 四码重码提示音（`dup-4code-beep`）**：当开启 `auto_commit_4code` 且输入满 4 码存在多个精确匹配重码时，提供可选的轻微提示音，提醒盲打用户当前有重码需选词。
- [x] **12. 编码渐进提示 / 简码反查提示（`prefix-code-hint`）**：在输入 1~3 码时，可选在前缀匹配的候选字词旁显示剩余后缀编码提示（如 `1.你(~i)`），辅助记忆简码与全码。

#### P1：进阶输入能力
- [x] **13. 在线快捷造词与自动取码（`online-user-word-creation`）**：按五笔 86 标准构词规则（双字词 2+2、三字词 1+1+2、多字词 1+1+1+末）自动计算新词编码并追加写入 `%LOCALAPPDATA%\QiQiIME\user_dict.txt`。
- [x] **14. 临时英文 / 网址 / 路径引导输入模式（`temp-english-guide-mode`）**：在中文空闲态下通过引导键直接输入一串包含大小写字母或符号的英文字符串（回车/空格上屏），无需频繁来回切换中英状态。

#### P2：渲染与词频体验打磨
- [x] **15. Direct2D / DirectWrite 渲染与生僻字/彩色 Emoji 字体回退（`directwrite-font-fallback`）**：将候选窗文字绘制升级为 `Direct2D` + `DirectWrite`（开启 `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`），利用系统级字体回退完美显示 CJK 扩展 B~G 区生僻字与彩色符号。
- [x] **16. 可选词组动态调频（`optional-dynamic-phrase-freq`）**：默认保持五笔固定词序以保障盲打肌肉记忆，同时提供可选的“仅重码词组动态调频（单字固定不调频）”开关。
- [x] **17. 独立发行包与 MSI 安装包打包（`release-packaging-script`）**：通过 `scripts/build_all.ps1 -Package` 配合 `installer/QiQiIME.wxs` 将 `x64`/`x86` 双架构 DLL、共享 `data/` 词库、注册/卸载脚本与许可证一键打包生成 `.msi` 安装包与 `.zip` 便携包到 `dist/` 目录。
