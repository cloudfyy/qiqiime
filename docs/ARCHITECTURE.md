# QiQi 五笔输入法 (TSF) 架构与设计说明

## 1. 架构分层

```text
+-------------------------------------------------------------------+
| 宿主应用 (Win32 Edit / Chromium / Electron / Office / 全屏游戏)    |
+-------------------------------------------------------------------+
                                 ▲
                                 │ TSF COM 接口 (msctf.dll)
                                 ▼
+-------------------------------------------------------------------+
| TSF 前端层 (qiqiime.dll)                                          |
|  - QiQiTextService : ITfTextInputProcessorEx                      |
|                    , ITfThreadMgrEventSink                        |
|                    , ITfKeyEventSink                              |
|                    , ITfCompositionSink                           |
|  - LambdaEditSession : ITfEditSession                             |
|  - CandidateUI       : ITfCandidateListUIElement + Topmost Popup  |
+-------------------------------------------------------------------+
                                 ▲
                                 │ 纯 C++ 数据结构 (KeyProcessResult)
                                 ▼
+-------------------------------------------------------------------+
| 五笔核心引擎层 (qiqi_engine.lib)                                  |
|  - WubiEngine : 编码状态机、四码唯一上屏、第五码顶字、通配符 z    |
|  - 内置 86 五笔核心词库 + 外部 UTF-8 码表 (wubi86.txt)            |
+-------------------------------------------------------------------+
```

之所以将 `qiqi_engine` 拆分为独立的静态库：
1. **无 COM 依赖即可测试**：`qiqi_engine_test.exe` 可以直接验证所有按键与五笔上屏规则，无需先通过 `regsvr32` 注入宿主进程。
2. **宿主安全**：输入法 DLL 会被注入到资源管理器、浏览器、Office 等进程中，引擎层保持零全局锁、纯内存计算，避免阻塞宿主 UI 线程。

---

## 2. TSF 关键生命周期与接口职责

### 2.1 注册阶段（`src/tsf/register.cpp`）
- **COM InProcServer32（支持 `x64` + `x86` WOW64 双视图）**：注册 `kClsidQiQiTextService`，线程模型为 `Apartment`（STA）。在 64 位 Windows 上分别通过 `System32\regsvr32.exe` 与 `SysWOW64\regsvr32.exe` 注册 `x64` 与 `x86` DLL，其中 WOW64 进程自动检测已存在的 64 位注册项并避免覆盖全局共享的 TSF Language Profile。
- **Language Profile**：通过 `ITfInputProcessorProfiles::AddLanguageProfile` 注册到 `zh-CN` (`0x0804`)。
- **Categories（`ITfCategoryMgr`）**：
  - `GUID_TFCAT_TIP_KEYBOARD`：声明为键盘输入法。
  - `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`：声明支持 `ITfUIElementMgr`，使支持 TSF UILess 的全屏游戏（如 Dota 2）能够激活输入法并接管候选框渲染。
  - `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` / `GUID_TFCAT_TIPCAP_COMLESS` / `GUID_TFCAT_TIPCAP_SECUREMODE`：提高现代 Windows 与安全输入框下的兼容性。

### 2.2 激活与停用（`ActivateEx` / `Deactivate`）
- 实现 `ITfTextInputProcessorEx::ActivateEx`，记录 `dwFlags`（包含是否处于 `TF_TMAE_UIELEMENTENABLEDONLY` UILess 模式）。
- 挂载 `ITfThreadMgrEventSink`（监听焦点文档切换）与 `ITfKeyEventSink`（拦截键盘事件），并注册 `Ctrl+Space` 保留键（Preserved Key）。

### 2.3 按键处理与编辑会话（`OnTestKeyDown` -> `OnKeyDown` -> `RequestEditSession`）
TSF 的按键处理分为两步：
1. `OnTestKeyDown`：仅询问输入法**是否打算消费该按键**（`engine_.ShouldEatKey(...)`）。只消费当前状态下真正需要处理的键，绝不在空闲状态下吞掉 `Enter`、`Backspace` 或方向键。
2. `OnKeyDown`：推进 `WubiEngine` 状态机，得到 `KeyProcessResult`（`UpdateState` / `Commit` / `Cancel`），然后通过 `ITfContext::RequestEditSession` 提交 `LambdaEditSession`。

在 `DoEditSession(TfEditCookie ec)` 内完成：
- **新建或更新组合串**：`ITfInsertAtSelection::InsertTextAtSelection` + `ITfContextComposition::StartComposition` + `ITfRange::SetText`。
- **上屏并结束组合**：将范围文本替换为最终汉字，折叠选区到末尾，调用 `ITfComposition::EndComposition`。
- **查询光标坐标**：通过 `ITfContextView::GetTextExt` 获取组合范围的屏幕矩形，移动候选框并做多显示器工作区边界裁剪。

### 2.4 异常处理与宿主兼容（`OnCompositionTerminated` / `OnSetFocus`）
当用户在浏览器、Word 或 Excel 中用鼠标点击其他位置，或前端框架重绘 DOM 导致组合被宿主强行结束时，TSF 会回调：
- `ITfCompositionSink::OnCompositionTerminated`
- `ITfThreadMgrEventSink::OnSetFocus`

`QiQiTextService` 会立即清空 `WubiEngine` 缓冲、释放 `ITfComposition` 并关闭 `CandidateUI`，防止状态残留。

### 2.5 桌面弹窗与游戏 UILess 双模式（`src/ui/candidate_ui.cpp`）
每次需要显示候选词时，先调用 `ITfUIElementMgr::BeginUIElement(candidate_ui_, &show_own_ui, &id)`：
- 若宿主应用返回 `show_own_ui == TRUE`（绝大多数桌面应用）：显示自带的 `WS_EX_TOPMOST | WS_EX_NOACTIVATE` 候选窗口。
- 若宿主应用返回 `show_own_ui == FALSE`（全屏游戏 UILess 模式）：隐藏自带弹窗，仅通过 `UpdateUIElement` 通知游戏，由游戏调用 `ITfCandidateListUIElement::GetCount` / `GetString` / `GetSelection` 自行绘制。

---

## 3. 后续扩展建议

1. **完整词库与二进制索引**：将 `data/wubi86.txt` 扩充至完整 86 五笔八万词库，并在编译期或首次加载时生成内存映射（`CreateFileMapping`）前缀数组或 Double-Array Trie，多进程共享只读内存页。
2. **显示属性（Display Attributes）**：实现 `ITfDisplayAttributeProvider`，为组合串设置虚线下划线与配色。
3. **语言栏按钮（Language Bar）**：实现 `ITfLangBarItemButton`，在系统托盘显示“中/英”、“全/半角”状态图标。
