# QiQiIME 1.0.0 正式版

数字版本：`1.0.0`；Git 标签：`1.0.0`（不带 `v`）。

## 主要更新

- 发布面向 Windows 11 x64 的五笔 86 TSF 输入法，包含 x64 和 x86（WOW64 应用）组件。
- 提供中英文 MSI、双架构运行组件 ZIP，以及同次构建的独立 PDB 符号 ZIP。
- 包含 R01–R06 重构、组合生命周期与激活重入处理、统一禁输入准入、独立设置/词典管理/日志监控工具及 Metrics 视图。
- 用户词库文件作为唯一权威来源：桌面端复用只读同步通道，注册表 `UserDictSyncDirectory` 只发布绝对目录地址；AppContainer 读取快照和增量，不再使用注册表词库镜像。
- 二进制词组排序文件改用 `.qfreq` 后缀；缺失或不可读的共享快照不再被当成空词库成功加载。
- 合并重复清理和初始化逻辑，补充共享读取失败与路径容量边界测试，修复 C++ Core Guidelines/GSL 静态检查发现的告警。

## 升级注意事项

1. 升级前使用旧版词典管理器导出 `.qbak`，并另外保留用户词库、调频文件与配置备份；关闭正在使用输入法的应用。
2. 默认排序文件由 `phrase_freq.txt` 改为 `phrase_freq.qfreq`，同步目录相应改为 `phrase_freq.qfreq.sync`。旧文件和目录不会自动读取、迁移或删除，因此旧排序不会自动恢复；可先用旧版导出备份，再在新版词典管理器恢复调频数据。
3. 注册表 `UserDictQdict` 旧值保留但不再读取或写入，不自动迁移仅存于镜像中的词条。升级前确认个人词条已经保存到用户词库文件或已导出备份。
4. AppContainer 的共享地址需要桌面 IME 或使用配置路径的词典管理器成功生成并发布。只有明确发布空地址才表示没有用户词库；配置、路径或快照不可读时记录实际失败，不回退到旧镜像。
5. 同步目录给应用包授予只读权限，而非仅给 IME 授权；其他符合访问条件的应用包也可能读到个人词条。父路径、ACL 和宿主令牌仍会影响访问结果。

## 验证与支持边界

- 发行前本地 x64/x86 的相关 10 项测试及混合架构真实 AppContainer 测试通过；两架构全量 CppCoreCheck 门禁告警为 0，PSScriptAnalyzer Error 为 0。
- 使用 Visual Studio 自带工具采集了实际行覆盖率；沙箱子进程的采集不完整，不把真实沙箱测试通过等同于分支覆盖率 100%。
- 正式发行附件由标签提交的 Release 工作流执行双架构完整构建、测试及打包后发布。流水线结果和实际附件以 [Release 页面](https://github.com/cloudfyy/qiqiime/releases/tag/1.0.0)为准；跳过测试不计作通过。
- 任务 36、47 与 R05 保留既有验证结论。全部 UWP、浏览器、WPF、游戏宿主、原生 ARM64、Windows 登录及安全桌面不在全面验证承诺内；密码/PIN 场景建议使用系统键盘布局或已验证的系统输入方式。
- Explorer 文件列表输入定位、历史偶发挂起/光标问题及更广泛 Metrics 性能预算保留在[已知限制](https://github.com/cloudfyy/qiqiime/blob/1.0.0/README.md#当前发布状态)与[宿主记录](https://github.com/cloudfyy/qiqiime/blob/1.0.0/docs/hosts.md)中。
- 真实发行包的全新安装、升级、卸载及用户数据保留需要独立环境验证；本次不在当前共享开发环境中自动执行这些操作，不将未执行的安装验收记为通过。

安装步骤见[安装指南](https://github.com/cloudfyy/qiqiime/blob/1.0.0/docs/install.md)，部署与故障排查见[部署指南](https://github.com/cloudfyy/qiqiime/blob/1.0.0/docs/deploy.md)。二进制与 PDB 必须来自同一次构建。
