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
|  - Common           : INI 原子配置、spdlog 日志、ScopedHandle RAII    |
+-----------------------------------------------------------------------+
```

之所以将 `qiqi_engine` 拆分为独立的静态库：
1. **无 COM 依赖即可测试**：`qiqi_engine_test.exe` 可以直接验证所有按键与五笔上屏规则，无需先通过 `regsvr32` 注入宿主进程。
2. **宿主安全**：输入法 DLL 会被注入到资源管理器、浏览器、Office 等进程中，引擎层保持纯内存/只读映射计算，避免阻塞宿主 UI 线程。

---

## 2. 关键子系统与安全设计

### 2.1 注册阶段（`src/tsf/register.cpp`）
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
2. `OnKeyDown`：推进 `WubiEngine` 编译期有限状态机（`wubi_fsm.h`），得到 `KeyProcessResult`（`UpdateState` / `Commit` / `Cancel`），触发音效（若启用了按键音或空码提示音），然后通过 `ITfContext::RequestEditSession` 提交 `LambdaEditSession`。

在 `DoEditSession(TfEditCookie ec)` 内完成：
- **新建或更新组合串**：`ITfInsertAtSelection::InsertTextAtSelection` + `ITfContextComposition::StartComposition` + `ITfRange::SetText` + 设置 `GUID_PROP_ATTRIBUTE` 下划线显示属性。
- **上屏并结束组合**：清除显示属性、将范围文本替换为最终汉字、折叠选区到末尾、调用 `ITfComposition::EndComposition`。
- **查询光标坐标**：通过 `ITfContextView::GetTextExt` 获取组合范围的屏幕矩形，移动候选框并做多显示器工作区边界裁剪。

### 2.4 异常处理与宿主兼容（`OnCompositionTerminated` / `OnSetFocus`）
当用户在浏览器、Word 或 Excel 中用鼠标点击其他位置，或前端框架重绘 DOM 导致组合被宿主强行结束时，TSF 会回调：
- `ITfCompositionSink::OnCompositionTerminated`
- `ITfThreadMgrEventSink::OnSetFocus`

`QiQiTextService` 会立即清空 `WubiEngine` 缓冲、释放 `ITfComposition` 并关闭 `CandidateUI`，同时在 `OnSetFocus` 时按需检查 `%LOCALAPPDATA%\QiQiIME\config.ini` 修改时间戳实现无感热重载。

### 2.5 桌面弹窗与游戏 UILess 双模式（`src/ui/candidate_ui.cpp`）
每次需要显示候选词时，先调用 `ITfUIElementMgr::BeginUIElement(candidate_ui_, &show_own_ui, &id)`：
- 若宿主应用返回 `show_own_ui == TRUE`（桌面应用）：显示自带的 `WS_EX_TOPMOST | WS_EX_NOACTIVATE` 候选窗口。
- 若宿主应用返回 `show_own_ui == FALSE`（全屏游戏 UILess 模式）：隐藏自带弹窗，仅通过 `UpdateUIElement` 通知游戏，由游戏调用 `ITfCandidateListUIElement` 接口自行渲染。

### 2.6 内存映射词库与最小权限安全模型（`src/engine/wubi_binary_dict.cpp` & `src/common/config.cpp`）
- **零隐私数据映射**：`.qdict` 映射文件仅包含静态五笔码表的只读索引（`DictBinaryHeader` + `DictCodeIndex` + `DictWordSlice` + `wchar_t` 字符池），不包含任何用户实时按键或输入状态；映射对象为匿名内核对象（`lpName == nullptr`）。
- **最小权限访问与防篡改锁**：
  - 映射读取 `.qdict` 与读取 `config.ini` 时，通过 `CreateFileW` 仅申请 `GENERIC_READ` 权限与 `FILE_SHARE_READ` 共享模式（禁止任何进程在映射期间写入、截断或删除文件，防止 `STATUS_IN_PAGE_ERROR`），并启用 `SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS` 与 `FILE_TYPE_DISK` 校验，阻断命名管道令牌模拟攻击。
  - 生成 `.qdict` 缓存与保存 `config.ini` 时，先以 `GENERIC_WRITE` + `dwShareMode = 0`（独占写）写入同目录临时文件 `.tmp.<pid>`，完成后通过 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换目标文件。

### 2.7 全链路 RAII、多显示器 DPI 与矢量抗锯齿 UI（`src/common/` & `src/ui/`）
- **泛型零开销 RAII（`ScopedHandle<Traits>`）**：所有 Win32 内核句柄、文件映射视图、注册表键、GDI 对象（`HFONT`/`HBRUSH`/`HPEN`/`HBITMAP`/`HDC`）、菜单（`HMENU`）、窗口（`HWND`）与动态库（`HMODULE`）均由移动语义 RAII 包装器管理。
- **多显示器与 Per-Monitor DPI（`monitor_dpi.h`）**：候选窗、浮动状态栏、Tooltip、设置窗口与关于窗口均通过 `MonitorFromPoint` + `GetDpiForMonitor` 适配任意副屏坐标（含负坐标副屏）与高分屏缩放，并在跨屏拖拽（`WM_DPICHANGED` / `WM_EXITSIZEMOVE` / `WM_DISPLAYCHANGE`）时自动重算字体与边界裁剪。
- **4x4 超采样抗锯齿状态条图标（`status_bar.cpp`）**：使用有向距离场（SDF）+ 4x4 超采样实时生成 32 位预乘 ARGB `DIBSection` 位图，通过 `GdiAlphaBlend` 渲染半角月牙、全角圆环、中英文标点与八齿设置齿轮图标。

---

## 3. 已完成特性清单与后续扩展

### 已完成（v0.8.1）
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
