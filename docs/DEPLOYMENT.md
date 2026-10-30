# 奇奇五笔 (QiQiIME) 部署、安装与运维指南

本文档介绍如何在 Windows 10 / 11 环境中部署、安装（注册）、打包分发、升级与卸载 **奇奇五笔输入法（QiQiIME）**。

---

## 1. 部署架构说明（`x64` + `x86` WOW64 双架构）

在 64 位 Windows 操作系统上，应用程序分为 64 位原生进程（如记事本、资源管理器、Office、现代浏览器）和 32 位 WOW64 进程（如部分老软件、32 位全屏游戏）。Windows TSF 要求被注入的输入法 DLL 位数必须与宿主进程位数完全匹配：

| 组件路径 | 目标宿主进程 | 注册工具 | 注册表写入位置 |
|---|---|---|---|
| `x64\qiqiime.dll`（或 `build\x64\Release\qiqiime.dll`） | 64 位原生应用 | `System32\regsvr32.exe` | `HKCR\CLSID\{8E7B9D31-...}\InProcServer32` + TSF `LanguageProfile` |
| `x86\qiqiime.dll`（或 `build\x86\Release\qiqiime.dll`） | 32 位 (WOW64) 应用 | `SysWOW64\regsvr32.exe` | `HKCR\WOW6432Node\CLSID\{8E7B9D31-...}\InProcServer32` |

每个架构目录下均需包含同级的运行时数据文件：
- `qiqiime.dll`
- `config.ini`
- `wubi86_jidian.txt`、`wubi86_jidian_extra.txt`、`wubi86_haifeng.txt`

> **重要提示**：Windows TSF 会直接从注册时所在的路径加载 `qiqiime.dll` 及其同级目录下的词库文件。安装注册后请勿移动或删除该目录（若需移动目录，请先卸载再在新目录重新注册）。

---

## 2. 安装（注册）与自检

通过 [scripts/register_ime.ps1](../scripts/register_ime.ps1) 可一键完成 **产物完整性校验 -> 双架构注册 -> 注册表与 TSF Profile 核验**（脚本同时兼容源码 `build\` 目录与独立发行包目录）：

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

## 3. 生成独立发行包（`.msi` 安装包与 `.zip` 便携包）

在源码仓库中运行以下命令，可自动编译并测试 `x64` 与 `x86` 双架构，然后同时生成 **标准 Windows Installer `.msi` 安装包**（基于 WiX Toolset）与 **`.zip` 绿色压缩包**：

```powershell
# 若首次打包 MSI，可先全局安装 WiX CLI：dotnet tool install --global wix
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Package
```

生成的产物位于 `dist\` 目录：
- **`dist\QiQiIME-v0.8.0-win-x64-x86.msi`**：标准 Windows MSI 安装包（双击即可自动安装到 `C:\Program Files\QiQiIME\` 并自动完成 `x64` + `x86` TSF 注册；支持通过 Windows“设置 -> 已安装的应用”一键卸载或无缝版本升级）。
- **`dist\QiQiIME-v0.8.0-win-x64-x86.zip`**：绿色便携压缩包，解压后结构如下（`x64` 与 `x86` 共用同一份 `data/` 词库与配置）：

```text
QiQiIME-v0.8.0-win-x64-x86/
├── x64/
│   └── qiqiime.dll             # 64 位 TSF 输入法组件
├── x86/
│   └── qiqiime.dll             # 32 位 (WOW64) TSF 输入法组件
├── data/                       # x64 与 x86 共用的配置与 3 套五笔词库
│   ├── config.ini
│   ├── wubi86_jidian.txt
│   ├── wubi86_jidian_extra.txt
│   └── wubi86_haifeng.txt
├── scripts/
│   ├── common.ps1              # 公共脚本模块
│   ├── register_ime.ps1        # 安装注册脚本
│   └── unregister_ime.ps1      # 卸载清理脚本
├── LICENSE                     # GPL-3.0 许可证
├── README.md                   # 功能与使用说明
└── DEPLOYMENT.md               # 部署与安装说明
```

终端用户解压到固定目录（如 `C:\Program Files\QiQiIME`）后，直接运行以下命令即可完成安装：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\register_ime.ps1 -AutoElevate
```

---

## 4. 运行期用户数据与缓存目录

安装并激活输入法后，所有运行期用户数据均自动存储在当前登录用户的 `%LOCALAPPDATA%\QiQiIME` 目录下（无需管理员权限即可读写）：

```text
%LOCALAPPDATA%\QiQiIME\
├── config.ini              # 用户个性化配置文件（支持窗口焦点切换时自动热重载）
├── user_dict.txt           # 用户在线造词 (Ctrl+=) 自动追加的个人词库
├── cache\
│   ├── wubi86_jidian.qdict       # 极点主词库只读内存映射二进制索引缓存
│   ├── wubi86_jidian_extra.qdict # 极点扩展词库二进制索引缓存
│   └── wubi86_haifeng.qdict      # 海峰大字符集词库二进制索引缓存
└── logs\
    └── qiqiime.log         # 结构化运行与排障日志（单文件上限 5MB，保留 3 个滚动备份）
```

---

## 5. 升级与卸载指南

### 5.1 卸载输入法

1. 按 `Win + Space` 切换到系统自带的其他输入法；
2. 运行卸载脚本（支持 `-AutoElevate` 自动提权，并自动核验 64 位与 32 位注册表项已彻底移除）：
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\unregister_ime.ps1 -AutoElevate
   ```

### 5.2 升级或重新编译时的 DLL 文件占用处理

由于 Windows TSF 会将 `qiqiime.dll` 映射进当前激活过该输入法的宿主进程，若升级覆盖或重新编译时提示 **`无法打开文件 qiqiime.dll`**：
1. 先执行 `scripts\unregister_ime.ps1 -AutoElevate` 卸载注册；
2. 关闭曾激活过奇奇五笔的应用程序窗口（若 `explorer.exe` 占用了 DLL，可在任务管理器中重启“Windows 资源管理器”）；
3. 替换文件或重新执行 `scripts\build_all.ps1` 编译，再运行 `scripts\register_ime.ps1 -AutoElevate` 重新注册。

---

## 6. 故障排查（查看日志）

如果遇到词库未加载、配置未生效或特定软件兼容性问题，可查看日志文件定位问题：

```powershell
Get-Content "$env:LOCALAPPDATA\QiQiIME\logs\qiqiime.log" -Tail 50
```

每条日志均包含精确时间戳、**宿主进程文件名**（如 `[notepad.exe]`）、进程 `pid`、线程 `tid` 与详细原因：
```log
[2026-10-30 08:15:10.120] [notepad.exe] [pid:46364] [tid:45604] [info] Loaded user configuration from 'C:\Users\...\AppData\Local\QiQiIME\config.ini'
[2026-10-30 08:15:10.122] [notepad.exe] [pid:46364] [tid:45604] [info] Attached external dictionary '...\wubi86_jidian.txt' (77169 codes, 89254 words, mapped=true)
```
如需更详细的窗口坐标、DPI 与空码状态日志，可将 `%LOCALAPPDATA%\QiQiIME\config.ini` 中的 `[log] level` 设为 `debug`。
