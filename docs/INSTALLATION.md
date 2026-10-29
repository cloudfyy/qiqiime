# 奇奇五笔 (QiQiIME) 安装、配置与排障指南

本文档详细介绍如何在 Windows 10 / 11 上编译、安装（注册）、配置、升级与卸载 **奇奇五笔输入法（QiQiIME）**。

---

## 1. 环境要求

- **操作系统**：Windows 10 / Windows 11（64 位 `x64` 或 32 位 `x86`）
- **安装/卸载权限**：需要 **管理员权限（Administrator）**（用于向系统写入 COM `InProcServer32` 与 Windows TSF `LanguageProfile` 注册表项）
- **从源码编译（可选）**：
  - Visual Studio 2026（或 2022，含“使用 C++ 的桌面开发”工作负载与内置 vcpkg）
  - CMake 3.25 或更高版本

---

## 2. 第一步：准备或编译二进制产物

如果尚未编译生成 `qiqiime.dll`，在项目根目录执行一键双架构编译脚本：

```powershell
cd C:\sample\qiqiime
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1
```

编译完成后，会在以下两个目录生成运行所需的全部文件：
- **64 位目录（`build\x64\Release\`）**：供 Windows 桌面、64 位浏览器、Office、VS Code 等 64 位进程加载
- **32 位目录（`build\x86\Release\`）**：供 32 位（WOW64）应用与老游戏加载

每个目录均包含：
- `qiqiime.dll`：TSF 输入法核心组件
- `config.ini`：默认配置文件模板
- `wubi86_jidian.txt`：极点五笔 86 核心主词库（默认）
- `wubi86_jidian_extra.txt`：极点五笔 86 现代扩展词库
- `wubi86_haifeng.txt`：海峰五笔 86 超大字符集词库（含 CJK 扩展 A/B 区生僻字）

> **注意**：Windows TSF 会直接从注册时所在的路径加载 `qiqiime.dll` 及其同级目录下的词库文件。安装注册后请勿移动或删除该目录（若需移动目录，请先卸载再在新目录重新注册）。

---

## 3. 第二步：安装（注册）与自检

### 方式一：使用一键安装与校验脚本（推荐）

可以使用 [scripts/test_install.ps1](../scripts/test_install.ps1) 自动完成**产物完整性检查 -> 双架构注册 -> 注册表与 TSF Profile 校验**：

- **在普通 PowerShell 窗口执行（自动弹出 UAC 提权并打开记事本测试）**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\test_install.ps1 -AutoElevate -LaunchNotepad
  ```

- **在管理员 PowerShell 窗口直接执行**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\test_install.ps1
  ```

- **如果尚未编译，可加 `-BuildIfMissing` 自动先编译再安装**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\test_install.ps1 -BuildIfMissing -AutoElevate
  ```

### 方式二：仅执行注册脚本

以**管理员身份**打开 PowerShell，运行：

```powershell
powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\register_ime.ps1
```

该脚本会依次调用：
1. `%SystemRoot%\System32\regsvr32.exe /s build\x64\Release\qiqiime.dll`（注册 64 位组件与 TSF 语言配置项）
2. `%SystemRoot%\SysWOW64\regsvr32.exe /s build\x86\Release\qiqiime.dll`（注册 32 位 WOW64 组件）

---

## 4. 第三步：切换与日常使用

### 4.1 切换输入法与常用快捷键

安装完成后，按 **`Win + Space`**（或点击任务栏右下角输入法指示器）即可切换到 **「QiQi 五笔 86」**。

| 快捷键 / 操作 | 功能说明 |
|---|---|
| **`Win + Space`** | 在系统输入法列表切换到「QiQi 五笔 86」 |
| **单按 `Shift`** 或 **`Ctrl + Space`** | 切换 **中 / 英** 输入模式 |
| **`Shift + Space`** | 切换 **全角 / 半角** 字符模式 |
| **`Ctrl + .`** | 切换 **中文全角标点 / 英文半角标点** |
| **`Space`（空格键）** | 上屏当前高亮候选词（默认首选词） |
| **`1` ~ `9` 数字键 / 鼠标左键点击** | 直接上屏当前页对应序号的候选词 |
| **`Enter`（回车键）** | 将当前已输入的英文字母原文直接上屏 |
| **`-` / `=`** 或 **`PageUp` / `PageDown`** | 候选框上翻页 / 下翻页 |
| **`↑` / `↓` 方向键** | 在当前页上/下移动高亮候选词 |
| **`z` 键（第 2~4 码）** | 万能学习键通配查询，并在候选词旁显示完整五笔编码反查提示 |
| **组合态按 `Shift + 字母`** | 自动将当前首选词连同该大写英文字母一并上屏 |

### 4.2 任务栏托盘菜单与桌面浮动状态条

- **任务栏托盘图标**：切换到奇奇五笔后，任务栏右下角会显示 **「中」/「英」** 图标：
  - **左键点击**：快速切换中/英文模式
  - **右键点击**：弹出快捷菜单（切换中英/全半角/中英标点、切换极点/海峰主词库、开关极点扩展词库、显示/隐藏桌面浮动状态条、切换中/英文界面语言、打开属性设置或编辑 `config.ini`、查看关于对话框）
- **桌面浮动状态条**：默认关闭；开启后可按住左侧竖条把手拖动到任意显示器位置，支持高分屏 DPI 自动缩放、深色/浅色系统主题自动适配与按钮悬停提示。

---

## 5. 用户数据目录与个性化配置

奇奇五笔的所有用户期生成文件均存放在当前用户的 `%LOCALAPPDATA%\QiQiIME` 目录下：

```text
%LOCALAPPDATA%\QiQiIME\
├── config.ini              # 用户个性化配置文件（支持焦点切换时自动热重载）
├── cache\
│   ├── wubi86_jidian.qdict       # 极点主词库只读内存映射二进制索引缓存
│   ├── wubi86_jidian_extra.qdict # 极点扩展词库二进制索引缓存
│   └── wubi86_haifeng.qdict      # 海峰大字符集词库二进制索引缓存
└── logs\
    └── qiqiime.log         # 结构化运行与排障日志（单文件上限 5MB，保留 3 个滚动备份）
```

可通过右键菜单点击 **「输入法属性设置...」** 在图形界面修改，或直接编辑 `%LOCALAPPDATA%\QiQiIME\config.ini`（保存后切换一下输入窗口焦点即可立即热重载生效）：

```ini
[dictionary]
primary = jidian              # jidian (极点86) | haifeng (海峰86超大字符集) | builtin (仅内置)
enable_jidian_extra = true    # 是否叠加极点现代扩展词库 (仅在 primary = jidian 时生效)
user_dict =                   # 自定义扩展码表路径 (可选)

[engine]
page_size = 5                 # 每页候选词数量 (1 - 9)
auto_commit_4code = true      # 四码唯一自动上屏
push_commit_5th = true        # 第五码顶字上屏
beep_on_empty_4code = true    # 四码空码时发出提示音
clear_on_empty_4code = false  # 空码时自动清空已输入编码
enable_key_sound = false      # 启用打字按键音效
show_status_bar = false       # 显示桌面浮动状态条
ui_language = auto            # 界面语言: auto | zh-CN | en-US

[log]
level = info                  # 日志级别: trace | debug | info | warn | error | off
```

---

## 6. 升级与卸载指南

### 6.1 卸载输入法

1. 按 `Win + Space` 切换到系统自带的其他输入法；
2. 以**管理员身份**运行卸载脚本：
   ```powershell
   powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\unregister_ime.ps1
   ```
3. 也可使用带卸载回环验证的测试脚本确认注册表项已彻底清除：
   ```powershell
   powershell -ExecutionPolicy Bypass -File C:\sample\qiqiime\scripts\test_install.ps1 -AutoElevate -UnregisterAfterTest
   ```

### 6.2 重新编译或升级时的文件占用处理

由于 Windows TSF 会将 `qiqiime.dll` 加载进当前激活过该输入法的宿主进程（如记事本、浏览器、资源管理器），如果重新编译时提示 **`LNK1104: 无法打开文件 qiqiime.dll`**：
1. 先执行 `scripts\unregister_ime.ps1` 卸载注册；
2. 关闭曾激活过奇奇五笔的应用程序窗口（若 `explorer.exe` 占用了 DLL，可在任务管理器中重启“Windows 资源管理器”）；
3. 重新执行 `scripts\build_all.ps1` 编译，再运行 `scripts\test_install.ps1 -AutoElevate` 重新安装。

---

## 7. 常见问题排查（查看日志）

如果遇到词库未加载、配置未生效或特定软件兼容性问题，可打开日志文件查看详细诊断记录：

```powershell
Get-Content "$env:LOCALAPPDATA\QiQiIME\logs\qiqiime.log" -Tail 50
```

每条日志均包含精确时间戳、**宿主进程文件名**（如 `[notepad.exe]`）、进程 `pid`、线程 `tid` 与详细原因：
```log
[2026-10-29 20:07:45.247] [notepad.exe] [pid:46364] [tid:45604] [info] Loaded user configuration from 'C:\Users\...\AppData\Local\QiQiIME\config.ini'
[2026-10-29 20:07:45.248] [notepad.exe] [pid:46364] [tid:45604] [info] Attached external dictionary '...\wubi86_jidian.txt' (77169 codes, 89254 words, mapped=true)
```
如需更详细的窗口坐标、DPI 与空码状态日志，可将 `%LOCALAPPDATA%\QiQiIME\config.ini` 中的 `[log] level` 设为 `debug`。
