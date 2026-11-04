# 奇奇五笔 (QiQiIME) 部署、安装与运维指南

本文档介绍如何在 Windows 10 / 11 环境中部署、安装（注册）、打包分发、升级与卸载 **奇奇五笔输入法（QiQiIME）**。

---

## 1. 运行环境要求

- **操作系统与架构**：目标平台为 Windows 10 / 11；当前双架构发行包及 MSI 面向 `x64` Windows，`x86` DLL 用于其中的 32 位 WOW64 宿主。Windows 11 没有 32 位系统版本，项目也未提供原生 ARM64 DLL；这些构建目标不等于所有系统/宿主组合均已验证。
- **运行库**：Release 构建采用 `/MT` 静态链接 MSVC CRT，不需要额外安装 VC++ Redistributable；这不等于没有系统 DLL 或运行环境依赖。
- **系统组件**：依赖 Windows TSF（`msctf.dll`）、Direct2D / DirectWrite 等系统组件。
- **权限要求**：
  - **安装 / 卸载时**：需要 **管理员权限（Administrator）**（用于向 `HKLM\SOFTWARE\Classes\CLSID` 与 `HKLM\SOFTWARE\Microsoft\CTF\TIP` 注册 COM/TSF 组件）
  - **日常运行与修改设置时**：桌面标准用户通常不需提权；设置保存在 `HKCU\Software\QiQiIME`，用户文件及缓存位置见第 5 节。日志使用 ETW，不自动写本地日志文件，采集控制可能需要额外权限。AppContainer 的访问能力还取决于令牌和 ACL，见 [UWP / AppContainer 边界](https://github.com/cloudfyy/qiqiime/blob/main/docs/UWP_COMPATIBILITY.md)。

---

## 2. 部署架构说明（`x64` + `x86` WOW64 双架构）

在 64 位 Windows 操作系统上，应用程序分为 64 位原生进程（如记事本、资源管理器、Office、现代浏览器）和 32 位 WOW64 进程（如部分老软件、32 位全屏游戏）。Windows TSF 要求被注入的输入法 DLL 位数必须与宿主进程位数完全匹配：

| 组件路径 | 目标宿主进程 | 注册工具 | 注册表写入位置 |
|---|---|---|---|
| `x64\qiqiime.dll`（或 `build\x64\Release\qiqiime.dll`） | 64 位原生应用 | `System32\regsvr32.exe` | `HKCR\CLSID\{8E7B9D31-...}\InProcServer32` + TSF `LanguageProfile` |
| `x86\qiqiime.dll`（或 `build\x86\Release\qiqiime.dll`） | 32 位 (WOW64) 应用 | `SysWOW64\regsvr32.exe` | `HKCR\WOW6432Node\CLSID\{8E7B9D31-...}\InProcServer32` |

每个架构目录下需包含以下运行组件，注册脚本会检查它们：
- `qiqiime.dll`
- `QiQiIMESettings.exe`
- `QiQiIMELogMonitor.exe`
- `QiQiIMEDictManager.exe`

日志监控器也使用原生菜单和纯图标工具条：开始/停止、暂停/恢复滚动、清空、复制、导出、诊断/输入法日志；语言切换位于“语言”菜单，关于位于“帮助”菜单。两款工具共用菜单、工具条和矢量图标实现，支持实时语言切换、系统主题、高对比度及跨显示器 DPI。采集权限、筛选、详情和导出隐私提示保持不变，使用方法见 [应用日志监控指南](LOGGING.md)。

词库管理器通过“文件 / 编辑 / 语言 / 帮助”菜单提供完整功能，顶部纯图标工具条从左到右为刷新、新增词条、复制、导入、导出、置顶/取消置顶、删除/屏蔽/恢复及词组置前；保存图标移至正在编辑的行旁。悬停图标可查看当前界面语言的功能提示。界面语言从“语言”菜单实时切换（自动 / 简体中文 / English）；菜单和图标跟随系统主题、高对比度和显示器 DPI，工具条在窗口较窄时自动换行。

首次打开默认显示“用户词库 / User Dict”，也可从来源下拉框切换到所有来源或其他分类；刷新和语言切换保留当前来源。“极点扩展词库 / Jidian Extra Dict”仅指启用后的随包极点扩展词库，不是用户词库。来源名称不显示内部文件格式或规则标记；“词组排序 / Phrase Ordering”表示词组的优先顺序记录，而非使用次数统计。

上方统一“搜索”框只筛选列表，不修改词库：去除首尾空白后，纯英文字母按五笔编码片段搜索（不区分大小写），中文、数字、标点及混合内容按词条文字搜索；清空搜索恢复当前来源的全部词条。底部不再放置编辑框：点击每行最前面的铅笔图标进入行内编辑，默认聚焦并选中编码；也可双击编码或词条（或选中后按 F2），在该行同时编辑两项。点击行旁保存图标、按 Enter，或使用“编辑 / 保存用户词”提交，Esc 取消，失去焦点不自动保存。输入法组合输入期间 Enter/Esc 仍交由输入法处理。铅笔与保存图标随主题、高对比度及 DPI 更新，不依赖外部图片。

第二个工具条图标“新增词条”插入一行草稿，即使当前筛选没有结果也可新增；新词默认自动取码，手动修改编码后不再随词条变化覆盖编码，也可从菜单重新自动取码。用户词条可同时修改编码与文字；系统词库、置顶等来源的行编辑后另存为用户词，不修改原来源。编辑期间仅禁用会修改列表的其他操作；点击来源或搜索框会直接放弃草稿，控件保持焦点并立即可用，无需先按 Esc。语言、主题、DPI 切换保留草稿，滚动或列宽变化时编辑框跟随行定位，不可完整显示时暂时隐藏。未保存的改动在来源列及状态栏显示 `*`；双击另一行或关闭窗口直接放弃草稿，不弹保存确认、不自动保存。保存失败或检测到原词条被其他进程修改/删除时保留草稿并提示。

三套词库 `wubi86_jidian.txt`、`wubi86_jidian_extra.txt`、`wubi86_haifeng.txt` 在发行包中共用上级 `data` 目录。运行时依次查找 DLL 同目录、上级 `data`、上级目录中的随包数据文件；用户选择的外部词库另按配置路径访问。

> **重要提示**：TSF 从注册表记录的绝对路径加载 DLL。注册后不要移动或删除运行目录；需要迁移时先注销，再在新位置重新注册。ZIP 分发也不是免注册的便携运行方式。

---

## 3. 安装（注册）与自检

通过 [scripts/register_ime.ps1](https://github.com/cloudfyy/qiqiime/blob/main/scripts/register_ime.ps1) 可完成 **产物完整性校验 -> 双架构注册 -> 注册表与 TSF Profile 核验**（脚本同时兼容源码 `build\` 目录与独立发行包目录）：

脚本不编译源码，需先完成构建或解压完整发行包。注册时会尝试预编译随包词库缓存，并为部署目录授予 AppContainer 读取/执行权限；失败以诊断为准，不保证所有沙箱宿主均可访问。

- **在普通 PowerShell 窗口执行（自动弹出 UAC 管理员提权，安装后打开记事本试用）**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate -LaunchNotepad
  ```

- **在管理员 PowerShell 窗口直接执行**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1
  ```

- **仅验证产物与版本信息（不修改注册表，无需管理员权限）**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -VerifyOnly
  ```

---

### 当前用户启用与旧版本修复

MSI 先以系统身份注册双架构 DLL，再尝试以安装发起用户身份启用语言配置，覆盖旧版本卸载留下的用户级 `Enable=0`，但不主动切换当前输入法。卸载在 `InstallValidate` 前尝试按会话停用该配置，不主动持久化用户级禁用标记；同时配置 Restart Manager、禁用自动关闭宿主进程，并设置 `MSIFASTINSTALL=7` 以减少部分安装开销。这些设置不能保证没有文件占用、扫描等待、重启要求或升级失败。

如果以其他管理员账户安装，或通过 SYSTEM 静默部署，不能据此保证当前桌面用户已启用。安装后，请由实际使用输入法的账户在普通 PowerShell 窗口运行以下命令（无需管理员权限；使用包含此修复的新版本 DLL 和脚本）：

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Program Files\QiQiIME\scripts\register_ime.ps1" -EnableCurrentUserOnly
```

此命令只启用当前用户已有的系统级语言配置，不重新注册 COM、不删除用户数据，也不修改其他账户。完成后用 `Win + Space` 检查；若列表尚未刷新，先重新打开应用，必要时注销后登录。

---

## 4. 生成独立发行包（`.msi` 安装包与 `.zip` 压缩包）

在源码仓库中显式运行以下打包命令，会编译并测试双架构，然后生成 ZIP；仅在找到 WiX CLI 时另生成中英文 MSI，否则报告跳过 MSI 的警告。日常构建无需加 `-Package`：

```powershell
# 若首次打包 MSI，可先全局安装 WiX CLI：dotnet tool install --global wix
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
```

生成的产物位于 `dist\`，文件名使用项目版本号（以下以 `0.9.31` 为例）：

- **`dist\QiQiIME-v0.9.31-win-x64-x86-zh-CN.msi`**：简体中文版 Windows MSI 安装包。
- **`dist\QiQiIME-v0.9.31-win-x64-x86-en-US.msi`**：英文版 Windows MSI。两个 MSI 共享 `UpgradeCode`，默认安装根目录为 `C:\Program Files\QiQiIME`；DLL 分别位于其 `x64`、`x86` 子目录。安装器提供注册、升级与卸载流程，实际成功与否以安装结果为准，不保证无缝升级。
- **`dist\QiQiIME-v0.9.31-win-x64-x86.zip`**：需解压后注册的压缩包，结构如下。双架构共用 `data` 词库，用户配置仍在注册表，不存于 `data`。

MSI 在所有用户的开始菜单中创建 `QiQiIME` 文件夹，包含三个工具入口，均启动安装目录下的 x64 程序，快捷方式不主动请求管理员权限：

| 简体中文 MSI | 英文 MSI |
|---|---|
| 奇奇五笔设置 | QiQiIME Settings |
| 奇奇五笔词典管理器 | QiQiIME Dict Manager |
| 奇奇五笔日志监视器 | QiQiIME Log Monitor |

快捷方式名称和说明跟随所安装 MSI 的语言，不随之后的 Windows 显示语言或工具内语言设置自动改名。可按上述名称在开始菜单搜索；搜索索引更新可能需要时间。ZIP 解压或注册脚本不会创建这些入口。MSI 卸载时移除对应快捷方式，仅在文件夹为空时移除 `QiQiIME` 开始菜单文件夹。

日志采集若遇到权限不足，需按[日志监控指南](LOGGING.md)单独提升监控工具或配置 ETW 权限，不要提升输入法宿主。入口与中英文资源的静态回归检查已纳入 `QiQiInstallerShortcutsTest`；该检查不安装产品，不能替代实际安装、升级和卸载验证。

```text
QiQiIME-v0.9.31-win-x64-x86/
├── x64/
│   ├── qiqiime.dll             # 64 位 TSF 输入法组件
│   ├── QiQiIMESettings.exe     # 64 位独立设置程序
│   ├── QiQiIMELogMonitor.exe   # 64 位独立实时 ETW 日志监控工具
│   └── QiQiIMEDictManager.exe  # 64 位独立词库与词频管理工具
├── x86/
│   ├── qiqiime.dll             # 32 位 (WOW64) TSF 输入法组件
│   ├── QiQiIMESettings.exe     # 32 位独立设置程序
│   ├── QiQiIMELogMonitor.exe   # 32 位独立实时 ETW 日志监控工具
│   └── QiQiIMEDictManager.exe  # 32 位独立词库与词频管理工具
├── data/                       # 共用词库；注册时尝试预编译同名 .qdict 缓存
│   ├── wubi86_jidian.txt
│   ├── wubi86_jidian_extra.txt
│   └── wubi86_haifeng.txt
├── scripts/
│   ├── common.ps1              # 公共脚本模块
│   ├── register_ime.ps1        # 安装注册脚本
│   └── unregister_ime.ps1      # 注销 COM/TSF 注册的脚本，不删除安装目录
├── LICENSE                     # GPL-3.0 许可证
├── README.md                   # 功能与使用说明
├── DEPLOYMENT.md               # 部署与安装说明
└── LOGGING.md                  # 应用日志与实时监控工具指南
```

终端用户解压到固定目录后，从解压目录运行以下命令注册；解压到 `Program Files` 本身也需要管理员权限。请检查脚本结果，注册后保持目录位置不变：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate
```

---

## 5. 运行期用户数据与缓存目录

桌面进程默认使用其 `LOCALAPPDATA` 下的 `QiQiIME` 目录保存用户词库和词组排序数据；路径可受自定义配置、环境及权限影响。下图是默认布局示意，不表示每项必然存在：

```text
%LOCALAPPDATA%\QiQiIME\
├── user_dict.txt           # 用户在线造词 (Ctrl+=) 自动追加的个人词库
├── user_dict.qdict         # 用户词库编译缓存（成功生成时）
├── cache\                 # 无法使用词库同目录缓存时的后备缓存目录
└── phrase_freq.txt         # 最近选中词组的排序记录（启用动态调频时使用）
```

词库优先尝试有效的同目录 `.qdict`，再尝试后备缓存；缺失或过期时解析文本并尝试生成缓存。映射失败时可尝试内存缓冲，但读取、编译、分配及写入仍可能失败，不能保证零写盘或任意沙箱中均可加载。

设置保存在 `HKCU\Software\QiQiIME`；默认用户词库的二进制镜像另存为其中的 `UserDictQdict`（最大 512 KiB），在默认用户词表不存在时作为加载后备，不是所有用户文件的实时备份。文本、缓存和注册表镜像不是一个跨资源事务。

词典管理器保存用户词条时先写入用户词库 TXT，再尝试重建 QDICT 缓存及更新同步数据；编辑系统词库等其他来源的词条会另存为用户词，不改写随包词库。备份为用户选择位置的 `.qbak` 文件，不会把运行时词库迁移到备份目录。备份和还原由独立桌面工具执行，不要求 AppContainer 访问备份目录；当前用户仍需具有目标目录的创建、写入及重命名/替换权限。

日志通过 ETW 发出；只有显式采集或导出才产生相应文件。AppContainer 路径和访问权限不能直接套用桌面进程假设，详见 [UWP / AppContainer 说明](https://github.com/cloudfyy/qiqiime/blob/main/docs/UWP_COMPATIBILITY.md)。

---

## 6. 升级与卸载指南

### 6.1 注销与卸载输入法

1. 按 `Win + Space` 切换到系统自带的其他输入法；
2. ZIP 或源码目录注册的版本，运行注销脚本（支持 `-AutoElevate` 自动提权，并核验双架构注册项；失败以实际诊断为准）：
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
   ```

脚本不删除用户设置、用户词库或安装目录；注销并确认文件不再占用后，才可自行清理部署文件。MSI 安装的版本应通过 Windows“已安装的应用”或对应 MSI 卸载，不能用注销脚本代替 Windows Installer 的产品卸载。用户数据保留不等于有独立备份。

MSI 卸载会移除安装文件及开始菜单入口，并清理安装目录中的随包 QDICT 缓存；当前不主动清除 `HKCU\Software\QiQiIME` 中的个人设置、默认 `%LOCALAPPDATA%\QiQiIME` 用户数据或安装目录外自行保存的备份，静默卸载也不额外清除这些数据。自定义词库或备份应放在安装目录之外，避免与安装器管理的文件混用。

**当前没有检测随包词库是否被修改并询问删除的功能**，也没有“彻底清除个人数据”选项。不要依赖 MSI 自动保护手工改过的安装目录文件，升级或卸载前请先另行备份。后续若增加数据清除功能，应默认保留个人数据，仅在用户明确选择并确认后清除；修改过的随包词库则需另行设计内容检测与保留/备份机制，而不是只按修改时间判断或弹窗后直接删除。

### 6.2 升级或重新编译时的 DLL 文件占用处理

由于 Windows TSF 会将 `qiqiime.dll` 映射进当前激活过该输入法的宿主进程，若升级覆盖或重新编译时提示 **`无法打开文件 qiqiime.dll`**：
1. 先执行 `scripts\unregister_ime.ps1 -AutoElevate` 卸载注册；
2. 关闭曾激活过奇奇五笔的应用程序窗口（若 `explorer.exe` 占用了 DLL，可在任务管理器中重启“Windows 资源管理器”）；
3. 替换文件或重新执行 `scripts\build_all.ps1` 编译，再运行 `scripts\register_ime.ps1 -AutoElevate` 重新注册。

---

## 7. 故障排查（查看日志）

结构化排障事件通过 Windows ETW TraceLogging 提供程序 `QiQiIME.TraceLogging` 发出，不自动写本地日志文件；可使用 `QiQiIMELogMonitor.exe` 实时采集，或用 WPR 等工具采集后在 WPA 中分析。采集权限及限制见 [日志指南](LOGGING.md)。设置程序保存后尝试发送 TSF Global Compartment 通知，运行中的实例据通知或焦点检查按需重载注册表配置，不保证所有实例立即更新。
