# Privacy policy / 隐私政策

This policy describes the current QiQiIME application and its bundled desktop
tools. It does not cover modified third-party builds or the independent behavior
of Windows, the application receiving input, your browser, or storage services.

## Local input and network use

- Wubi lookup, composition, and text commit run locally. The current application
  has no cloud input service, account service, automatic telemetry upload, or
  automatic update downloader.
- The project does not operate a server to receive typed text or user dictionaries.
  Text committed to another application is subsequently subject to that
  application's behavior and privacy policy.
- Clicking the GitHub button in About opens the project website in your browser.
  Downloads and issue submissions use GitHub; its
  [privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement)
  applies. This does not automatically attach typing history or diagnostic files.
- Choosing a network share or a cloud-synchronized folder for a dictionary,
  backup, or export can expose data to that storage provider. Such a location is
  not made private or encrypted by QiQiIME.

## Input, clipboard, and saved data

- The IME processes key events and composition text to provide input. It does not
  maintain a general transcript of everything typed.
- Adding a phrase with **Ctrl + =** explicitly reads Unicode text from the
  clipboard. A suitable phrase is saved in the user dictionary. The log monitor's
  Copy commands explicitly write selected diagnostics to the clipboard.
- User phrases, pinned/suppressed entries, and optional recent-selection records
  are persistent data, not anonymous statistics. Dynamic phrase ordering is
  optional and disabled by default.
- Settings are stored under `HKCU\Software\QiQiIME`. Desktop defaults place user
  data under `%LOCALAPPDATA%\QiQiIME`, including `user_dict.txt` and, when enabled,
  `phrase_freq.qfreq`. Custom paths and permissions can change the actual location.
- `.qdict` caches, `.sync` snapshots/journals, exported dictionaries, and `.qbak`
  backups may contain user phrases or selection information. They are not an
  encrypted vault and must be protected like the original data.
- Recognized private input scopes suppress learning/persistence; recognized
  password/PIN fields use temporary English mode. Host recognition is not
  universal. Do not rely on this as a guarantee for every sensitive field.

## Local sharing and permissions

Desktop instances publish dictionary/frequency snapshots and incremental data
for other local instances, including AppContainer hosts. This is local
cross-process sharing, not an upload.

To support AppContainer access, the application attempts to grant read access
to its configuration key and read/traverse access to synchronization data to
`ALL APPLICATION PACKAGES` and `ALL RESTRICTED APPLICATION PACKAGES`. These
permissions are broader than "only QiQiIME": other applications with qualifying
access may be able to read shared settings, paths, and phrases. Effective access
also depends on parent-directory permissions and the host token.

Installation registers COM/TSF components and attempts to grant the application
package groups read/execute access to the installation directory. See the
[installation guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/install.md)
for details and limitations.

## Diagnostics and voluntary sharing

Production diagnostics use local Windows ETW and/or debugger output, not an
automatically uploaded log file. The log monitor starts capture when requested;
metrics collection requires starting capture in the Metrics view. Export saves
to a user-selected file; it does not send the file to the project.

Ordinary diagnostic captures can identify processes, paths, timing, configuration,
and failure details. Treat logs as potentially sensitive and inspect/redact them
before sharing. Metrics JSON omits raw log messages and uses capture-local
instance aliases, but this does not sanitize a separately exported ordinary log.
Developer tests and debugger/watchdog workflows can additionally create retained
test logs and memory dumps containing sensitive process memory.

Do not attach private text, dictionaries, backups, logs, or dumps to public
issues. Share only information you have reviewed and explicitly chosen to disclose.
For capture controls, see the
[logging guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/log.md).

## Retention and removal

MSI uninstall removes the managed application files and input-method registration.
Personal settings and user data are **kept by default**. Interactive uninstall
in the unreleased wizard has two independent, initially unchecked choices:
delete the initiating user's settings under `HKCU\Software\QiQiIME`, and/or delete
their managed dictionary data. The latter includes default `user_dict.txt` /
`user_dict.qdict` / `phrase_freq.qfreq` files in the default profile's Local AppData
`QiQiIME` directory, known generated dictionary caches, and known snapshot,
journal, metadata and lock files in the default `.sync` directories. Selecting
and confirming authorizes permanent deletion at uninstall commit; upgrade removal
does not opt into either choice. The three bundled dictionaries in the program
directory are ordinary application payload and are always removed.

Cleanup does not read custom paths from settings, scan other Windows users, or
recursively delete directories. Custom dictionaries, exports, backups and
unrecognized files are preserved, including such files inside the default data
directory. Reparse points (including junctions/symlinks) at the data directory,
sync directories or targeted files are refused. Locked or inaccessible data
can remain; a cleanup error is reported rather than silently treated as success.
Mapped old caches can be moved into a protected same-volume pending-deletion
location and queued for deletion by Windows at restart. This retains the old
data locally until deletion actually occurs; it is not secure erasure. A
machine-protected operational record, separate from removable preferences,
retains the initiating user identity, isolated pending targets, cleanup outcome
and boot-session state. It prevents reinstall before the required restart and
verification. Registration of a deletion is not proof the file has been deleted.
Close applications using the IME and back up first; applications left running
can recreate data. Removal is not secure disk erasure and cannot remove external
copies. Quiet uninstall preserves data unless explicitly invoked with
`QIQI_REMOVE_CONFIGURATION=1` and/or `QIQI_REMOVE_DICTIONARIES=1`.
Legacy `QIQI_REMOVE_USER_DATA=1` selects both, unless overridden by an explicit
new option. Interactive full/basic/reduced entry points use the wizard.
ZIP/manual unregistration also keeps user data by
default. Newer `unregister_ime.ps1` scripts support an explicit `-RemoveUserData`
switch for the existing default-file cleanup after successful unregistration;
ZIP cleanup does not implement the MSI wizard or reboot scheduling. ZIP cleanup
runs as the initiating account. The MSI backend is elevated only for validated
initiating-user cleanup, protected state and reboot scheduling; it resolves the
initiating user's profile, not the administrator account supplied to UAC.
The switch authorizes deletion without another prompt. The deployment directory
is not deleted. Older release ZIPs may not support this switch.

After uninstalling and closing affected applications, you can separately remove
your own remaining settings, data, synchronization directories, caches, and
exports. Back up what you need first and check custom locations. Data stored in
the installation directory can be removed or replaced by installation maintenance;
keep personal dictionaries and backups outside it.

## Questions and changes

Policy questions can be filed in
[GitHub Issues](https://github.com/cloudfyy/qiqiime/issues) without including
personal data. Future networking or data-handling changes must update this policy
before release. This policy does not certify third-party services or operating-system settings.

## 中文摘要

- 当前输入、查词和上屏在本地完成，没有云输入、账号、自动遥测上传或自动下载更新服务。
  关于窗口的 GitHub 按钮由用户点击后交给浏览器打开。
- `Ctrl + =` 会主动读取剪贴板中的适合词组并保存；用户词、置顶/屏蔽、可选调频、
  缓存、同步快照和备份可能包含敏感内容，并非加密存储。
- 本机 AppContainer 共享使用较宽的应用包组读取权限，不等于“只有输入法能读取”。
  密码/PIN 和私密输入识别受宿主限制，敏感场景请使用系统键盘布局等已验证方式。
- 日志监控、导出和反馈由用户发起，不自动上传；日志、测试 dump、用户词库及备份
  分享前必须检查脱敏。用户选择网盘或网络路径产生的数据传输由相应服务处理。
- 卸载默认保留个人设置和用户数据；未发布的向导把配置、词典数据分成两个默认未选中
  的选项，词典数据包含默认用户词库、词频及已知编译缓存和同步文件。
  外部源词库、导出、备份、其他账户及未知文件保留；升级不清理，静默卸载也默认保留。
  被占用的旧缓存可隔离后登记重启删除；受保护的本机操作状态记录身份、待删目标及启动会话，
  在所需重启和清理核验前阻止重装，不上传这些信息。新版 ZIP 注销脚本可显式加 `-RemoveUserData`，
  在注销成功后以发起账户清理既有默认文件范围，不再询问，也不删除程序目录；
  ZIP 不包含 MSI 向导和重启删除机制。
  先关闭使用输入法的应用并备份；占用或权限问题会报告
  失败且可能留下部分数据，不等同于安全擦除。完整的数据类别和权限边界见上文。
