# 奇奇五笔输入法 (QiQiIME) UWP / AppContainer 当前实现与验证边界

## 1. 背景概述

Windows 10 / 11 的部分应用进程运行在 AppContainer 中。不能仅凭 Microsoft Store 来源、MSIX 打包、应用名称或低完整性级别认定某个进程属于 AppContainer；应检查实际承载输入法的进程令牌和宿主条件。

QiQiIME 是 TSF 进程内 COM 组件。在允许加载该输入法的宿主中，`qiqiime.dll` 遵循承载进程的访问权限；文件、注册表、内存映射和通知接口不能绕过宿主的安全边界。以下区分已实现机制、访问失败时的回退和仍需实测的兼容范围，不把机制存在等同于所有沙箱可用。

---

## 2. 当前架构与 UWP 兼容现状评估

### 2.1 已实现机制（不等于全面兼容）

| 模块 | 代码位置 | 当前机制与边界 |
| :--- | :--- | :--- |
| **TSF 类别声明** | [`register.cpp`](../src/ime/tsf/register.cpp) | 声明 `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` 与 `GUID_TFCAT_TIPCAP_COMLESS`；实际激活仍取决于宿主支持、注册状态和 DLL 访问权限。 |
| **只读匿名文件映射** | [`binary_dict.cpp`](../src/ime/engine/binary_dict.cpp) | 以 `GENERIC_READ`、`FILE_SHARE_READ \| FILE_SHARE_DELETE` 打开文件，创建 `PAGE_READONLY` 匿名映射；这些标志不授予额外文件访问权，也不保证映射一定成功。 |
| **内存缓冲回退** | [`binary_dict.cpp`](../src/ime/engine/binary_dict.cpp) | 文本读取及二进制编译成功、但缓存无法映射时，可保留堆内存中的 `fallback_buffer_`。源文件不可读、编译失败或内存不足时仍可能加载失败，不保证零堆占用或绝不丢失词库能力。 |
| **ETW 日志** | [`logger.cpp`](../src/common/logger.cpp) | 使用 `QiQiIME.TraceLogging`，不依赖 spdlog 或自动写入文本日志。注册/写入可能失败，按实际错误码诊断；采集控制权限与宿主写事件权限不能混为一谈。 |
| **注册期词库预编译** | [`register.cpp`](../src/ime/tsf/register.cpp) | 注册时尝试为三套随包词库生成同目录 `.qdict`，卸载注册时尝试清理；源文件或权限问题可能导致预编译失败。 |
| **开发目录读取权限** | [`register_ime.ps1`](../scripts/register_ime.ps1)、[`common.ps1`](../scripts/common.ps1) | 注册脚本已对所需产物及数据目录配置应用包读取/执行权限；结果应检查实际 ACL，不能因位于 `Program Files` 就假定任何宿主均可访问。 |

---

### 2.2 访问限制与失败边界

#### 用户文件与注册表镜像
- [`GetAppDataDirectory()`](../src/common/file_util.cpp) 从当前进程的 `LOCALAPPDATA` 解析用户数据路径。应用包、进程令牌和重定向机制可能影响实际路径与访问结果，不能固定推导为某个包的 `AC` 或 `LocalState` 目录。
- 普通桌面进程通常使用 `%LOCALAPPDATA%\QiQiIME\user_dict.txt`、`phrase_freq.txt` 与 `cache\`；这不意味着 AppContainer 能访问同一目录，也不应通过开放整个用户目录权限解决。
- [`config.cpp`](../src/common/config.cpp) 将设置保存在 `HKCU\Software\QiQiIME`，并尝试为 `S-1-15-2-1` 与 `S-1-15-2-2` 授予 `KEY_READ`。实际访问仍受令牌、有效 ACL 和系统策略约束；该读取授权不等于允许应用包写回共享设置。
- 在线造词写入默认用户词表后，会尝试编译二进制镜像并写入注册表 `UserDictQdict`，镜像上限为 **512 KiB**。文件追加、缓存生成与注册表事务不是一个跨资源事务，不能保证三者同时成功。
- 默认用户词表不存在时，[`LoadExternalDictionaryNextToDll`](../src/ime/tsf/tip_config.cpp) 才尝试读取注册表镜像；指定自定义用户词库路径时不走这一缺失文件回退。不能据此承诺所有沙箱中的用户词库实时双向同步。

#### 缓存选择与内存占用
- 加载器先尝试词库旁的 `.qdict`，再尝试当前进程用户缓存目录；还会校验缓存格式及源文件大小/时间戳，不是只要文件存在就复用。
- 注册期生成且可读取的有效缓存可减少运行时文本解析和缓存写出。加载器仍会探测、尝试创建本地缓存目录，因此不宣称“沙箱内没有任何写入尝试”。
- 缓存缺失或过期时，尝试解析文本、写出缓存并重新映射；无法映射时才使用可用的二进制内存缓冲。读取、写入、映射和缓冲分配都有各自失败条件。
- 同一模块内的引擎实例复用进程词库缓存；不同进程仍有独立对象和堆内存。只读文件映射可由系统按需分页并共享底层文件页，不等于零私有堆或零工作集。

#### 配置通知与跨进程文件同步
- 当前实现已经使用 `ITfThreadMgr::GetGlobalCompartment` 与 `ITfCompartmentEventSink`，不再以旧的状态栏窗口广播作为配置通知方案。
- 配置保存后递增注册表序列并发送 Global Compartment 通知，运行中的实例在通知或焦点检查时按需重载。通知可能失败，不能保证所有进程立即或零延迟更新。
- [任务 39](arch.md) 已提供 [`common/change_sync`](../src/common/change_sync.h)：目录通知仅负责唤醒，持久化增量日志、版本检查及文件锁负责恢复与写入协调。独立 x64/x86 测试及混合架构测试验证了真实 AppContainer 的读取/通知、共享读锁和拒绝写入。
- 通道使用普通发布进程显式提供的每用户绝对目录；不能让容器按自己的 `LOCALAPPDATA` 另建一个互不相通的通道。目录对 AppContainer 授予读取/遍历权限，不授予写入；日志可能包含用户词条或排序信息，不是无隐私数据。
- 这些是公共层、引擎同步适配层（任务 39、40）和专用测试进程的结果，不是所有 UWP/商店宿主加载 IME 后的验收。实际宿主环境仍受包路径、进程令牌和 ACL 约束，现有配置通知与通道同步不代表所有沙箱宿主均已兼容。

#### 日志权限与生命周期
- Provider 在正常入口显式初始化，不在 `DllMain` 初始化；服务停用不注销，按模块生命周期清理。日志宏及注册状态查询不隐式初始化。
- 没有接收方或相关级别关闭时跳过消息格式化，但仍有状态检查成本；没有“0 纳秒”或单条指令开销的测量依据。
- 使用 [`QiQiIMELogMonitor.exe` 或 ETW 工具](log.md)采集；`QiQiIMESettings.exe` 不是实时日志查看器。注册失败、事件写入失败、采集权限和丢失计数必须分别检查，不保证任意 AppContainer 都能输出或被采集。

---

## 3. 验证流程与尚未覆盖的范围

1. 记录实际宿主名称、版本、PID、进程架构及是否属于 AppContainer，不以 Microsoft Store 来源代替令牌验证。
2. 确认宿主实际加载的 DLL 版本与注册路径；检查 DLL、工具和词库目录的有效读取/执行权限，以及注册阶段预编译缓存的结果。
3. 分别验证主/扩展词库加载、默认用户词表缺失时的注册表镜像回退、配置读取与变更通知；区分读取成功与写回成功。
4. 按[真实宿主测试步骤](hosts.md)验证组合、失焦、停用、重新激活及候选清理，记录实际日志与未覆盖分支。没有事件不能判为正常，Mock 或私有 TSF 文档成功也不替代宿主测试。

2026-11-03 已记录部分桌面宿主及 Microsoft Store 应用的人工反馈，但商店应用身份、版本、控件类型和完整重复次数未补齐；详见[结果记录](hosts.md#8-2026-11-03-人工测试结果)。不据此宣布所有 UWP、MSIX、浏览器沙箱或游戏兼容。

密码框、只读控件及游戏非聊天场景的识别仍属于[任务 36](arch.md)。2026-11-05 已启动[官方契约研究与双架构本地实验](restricted.md)，尚未修改生产输入行为或完成 UWP 真实控件验证；现有 compartment 检查不代表这些场景已全面覆盖。词库镜像及日志路径也可能含有用户数据，分享诊断前须检查并脱敏，不记录密码或输入正文。

## 4. 参考资料

- [Microsoft：AppContainer isolation](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation)
- [部署与运行数据目录](deploy.md)
- [日志权限、生命周期及监控](log.md)
- [当前架构与后续任务](arch.md)
