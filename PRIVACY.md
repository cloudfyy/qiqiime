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
[deployment guide](https://github.com/cloudfyy/qiqiime/blob/main/docs/deploy.md)
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
offers one unchecked option to also delete the initiating user's settings under
`HKCU\Software\QiQiIME`, default `user_dict.txt` / `user_dict.qdict` /
`phrase_freq.qfreq` files under the Windows Local AppData `QiQiIME` directory,
and known snapshot, cache, journal, metadata and lock files in their `.sync`
directories. Selecting it and confirming uninstall authorizes permanent deletion
at successful uninstall commit; it is not offered during upgrade removal.

Cleanup does not read custom paths from settings, scan other Windows users, or
recursively delete directories. Custom dictionaries, exports, backups and
unrecognized files are preserved, including such files inside the default data
directory. Reparse points (including junctions/symlinks) at the data directory,
sync directories or targeted files are refused. Locked or inaccessible data
can remain; a cleanup error is reported rather than silently treated as success.
Close applications using the IME and back up first; applications left running
can recreate data. Removal is not secure disk erasure and cannot remove external
copies. Quiet/basic-UI uninstall preserves data unless explicitly invoked with
`QIQI_REMOVE_USER_DATA=1`. Manual unregistration does not delete the deployment
directory or user data.

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
- 卸载默认保留个人设置和用户数据；可勾选一个默认未选中的选项，同时清理当前用户配置、
  默认词库/词频及已知缓存和同步文件。自定义路径、导出、备份、其他账户及未知文件保留；
  升级不清理，静默卸载也默认保留。先关闭使用输入法的应用并备份；占用或权限问题会报告
  失败且可能留下部分数据，不等同于安全擦除。完整的数据类别和权限边界见上文。
