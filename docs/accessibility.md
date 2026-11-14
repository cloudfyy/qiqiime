# 显示主题与关怀模式 / Display and Comfort mode

## 桌面端使用

打开开始菜单中的 **QiQiIME 设置**，在“显示与易读”中选择：

| 选项 | 行为 |
|---|---|
| 跟随系统 | 跟随 Windows 应用明暗设置；桌面端默认值 |
| 明亮 | 固定明亮主题 |
| 深色 | 固定深色主题 |
| 高对比度 | 应用自己的黑底、白字、黄色强调配色 |
| 文字大小 | 100%、125%、150%、200%，默认 100% |
| 关怀模式 | 开启时将小于 150% 的字号提高到 150%，增大操作区域及间距 |

主题是**可选项**，无需修改 Windows 设置。Windows 已启用高对比度时，
其系统颜色始终优先；关闭系统高对比度后恢复应用所选主题。
关怀模式开启时字号只能选择 150% 或 200%，100%/125% 灰显不可选。
关闭关怀模式后恢复所有字号选项，保留当前字号。文字区域不切换模式；
仅点击开关本身可切换，也可用键盘操作。
关怀模式开关位于文字大小上方；禁用字号使用明显的灰色文字。
字号与显示器 DPI 分开计算，移动到不同缩放比例的屏幕时重新布局。

更改先在设置窗口及其候选样式示例中预览：

- **确定**：保存并经现有配置通知更新输入法自绘候选窗。
- **取消**或关闭：丢弃尚未保存的显示修改。
- **恢复默认设置**：统一恢复显示、词库、输入习惯等设置，仍须确定保存。
- 保存失败会明确提示并保留当前编辑，可重试。

设置按“输入习惯、词库、候选窗口、显示与易读、声音与反馈”五个 Tab 分类。
Tab 栏的背景、文字、选中态、焦点及横向翻页箭头使用所选主题；
字号放大后空间不足时，使用箭头或 Ctrl+Tab / Ctrl+Shift+Tab 切换。
只有当前页显示并滚动；切换页保留未保存的修改和各页滚动位置。
页面中的标签和控件在空间不足时上下排列。
About、Shortcuts、恢复默认设置、确定/取消按此顺序固定在底栏，所有 Tab 可用；
常规宽度并列，字号放大或窗口较窄时底栏分两行。
下拉框采用固定基础宽度并右对齐；页大小与候选排列方式并排显示。
操作按钮紧凑排列，候选预览以横向布局节省空间，空间不足时换行。
Browse 和 Dictionary Manager 与自定义主词库选项并排，空间不足时移到其下一行。
仅选中自定义主词库时 Browse 可用，Dictionary Manager 不受该选项限制。
单击 Custom 仅选中该方案并启用 Browse，不自动打开文件对话框；
使用 Browse 选择文件后才能保存有效的自定义词库配置。
Extra 缩进显示在 Jidian Core 下，并注明依赖关系：只有选中 Core 时可用。
切换其他词库会自动取消 Extra 勾选并禁用；切回 Core 后须手动重新勾选。
禁用时 Browse 文字、Extra 标签及开关使用与禁用字号一致的灰色，高对比度下遵循对应配色。
高对比度下不只依靠颜色：禁用按钮和开关增加“不可用 / Unavailable”状态文字，
移除其外框；禁用字号项追加同样的状态提示，保持系统配色及文字可读性。
可使用 Tab/Shift+Tab 移动焦点，空格操作开关，方向键选择下拉项；
焦点进入屏外控件时自动滚动。易读主要操作目标不小于 44×44 DIP。

候选窗对长候选换行；横排空间不足时按候选项换行，不改变候选顺序、
编号、配置的横/竖排或引擎页大小。工作区过小时提供页内滚动，
选择移动时自动显示当前项；不通过偷偷缩小字号或丢弃候选适配屏幕。
候选窗不激活，不抢走正在输入的应用焦点。选中项同时使用背景/边框提示。

### 范围与限制

- 本批桌面显示偏好适用于**设置窗口和输入法自绘候选窗**。
  词库管理器、Log Monitor、浮动状态条及其他对话框尚未全部接入，
  不能视为全软件的独立主题改造已完成。
- 设置中的候选预览是本地样式示例，不启动真实输入组合。
- 宿主接管 TSF 无界面模式候选展示时，外观由宿主决定。
- 高对比度自动检查针对应用配色的正文、次要及选中文字与背景，
  目标至少 7:1；系统高对比度按系统颜色绘制，不擅自替换用户配色。
- 保留现有键盘选词及快捷键；这不等于全面读屏认证或所有宿主的人工验收。
- 网站主题独立保存在浏览器，不修改桌面配置，也不会上传。

## English

Open **QiQiIME Settings** and use **Display & easy reading**. Theme is an explicit
choice: **Follow Windows**, **Light**, **Dark**, or **High contrast**. The desktop
default remains Follow Windows with 100% text and Comfort mode off.
Windows high contrast always takes precedence using the user's system colors.
No Windows setting is changed.

Text sizes are 100%, 125%, 150%, and 200%. Enabling Comfort mode raises smaller
text to 150%, enlarges targets and spacing, and uses a single column. While enabled,
100%/125% are greyed out and unavailable. Disabling it restores those options
without reverting the current size. Click the switch, not its label, to toggle;
keyboard operation remains available.
Display scaling and monitor DPI are applied separately.

Changes preview only in Settings and its local candidate-style sample until
**OK** succeeds. **Cancel** discards display edits. **Restore defaults** resets
display, dictionary, input, and other settings together and still requires OK.
Save failures retain edits and show a message.

Settings has five themed tabs: Input, Dictionary, Candidates, Display, and
Feedback. The tab strip, selection, focus, and overflow arrows follow the selected
theme, with Windows high contrast taking precedence. Use the arrows or
Ctrl+Tab / Ctrl+Shift+Tab when enlarged labels do not fit. Only the active page
scrolls; switching preserves unsaved edits and each page's scroll position.
Keyboard focus scrolls into view while OK/Cancel remain visible.
Easy-reading primary targets are at least 44×44 DIP. Candidate text wraps and
overflow can scroll within a page, without changing candidate order, numbering,
engine page size, or the user's horizontal/vertical preference. The popup does
not activate or steal the host's input focus. Selection is not indicated by
text color alone.

This phase covers Settings and the IME's own candidate popup, **not all desktop
tools**. Dictionary Manager, Log Monitor, the floating status bar, and other
dialogs have not all adopted these independent preferences. Host-rendered TSF
candidate interfaces remain host-controlled. App high-contrast text targets
7:1 contrast; Windows high contrast uses system colors. This is not a claim of
complete screen-reader certification or real-host acceptance.

Website preferences remain browser-local and separate from desktop preferences.
