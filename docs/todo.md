# TODO

## IME skins

### Confirmed design (complete; runtime implementation still pending)

- [x] Define all 21 bilingual skin names, each Chinese name having two characters,
  and all 42 Light/Dark palettes. See the [design catalog](ime-skins.md).
- [x] Complete the [per-skin colors and state diagrams](ime-skin-palettes.md):
  background, outer border, code title, candidate text, selected background/text/
  border, empty-message text, and enabled/disabled page text. Illustrate first
  page, last page, and empty candidates for both variants of every skin.
- [x] Provide the [registry design template](ime-skins.reg): 21 stable skin IDs,
  42 complete variants, and 13 explicit color fields per variant (546 values).
  Check data/diagram consistency and design contrast. This does not mean the
  registry template has been imported or that a skin loader exists.
- [x] Confirm [theme-combination rules](ime-skins.md#82-皮肤选项与主题模式):
  theme chooses brightness; skin chooses color family. Windows high contrast
  takes precedence, then app high contrast; both bypass skins without clearing
  the selection. Explicit Light/Dark selects that variant. The approved candidate
  automatic-source policy now uniformly uses App mode, including File Explorer
  and system search surfaces. Host detection and Windows-mode exceptions are
  deferred below. Default skin preserves existing colors.

### Phase 1: Implement the complete pure-color skin feature

Execute the following dependent steps in order; add focused regressions with
each behavior change rather than postponing all testing until the final gate.

- [ ] Implement the [registry catalog contract](ime-skins.md#84-注册表配置契约).
  Store `ImeSkinId` as REG_SZ, separate from versioned `Skins\Catalogs`
  definitions and `Skins\ActiveCatalog`. Use REG_SZ for names/identifiers and
  REG_DWORD for schema/order and all 13 RGB24 roles; no packed REG_BINARY
  structure or serialized whole-skin string. Colors are `0x00RRGGBB` with a
  zero high byte, not COLORREF's channel order; convert explicitly for rendering.
  Keep the documented stable IDs and bilingual names, not A/B figure numbers
  or translated display names, as persistence identities.
- [ ] Initialize the 21 complete Light/Dark definitions through the desktop
  settings flow, preserving existing catalogs and user preferences. IME reads
  must not create missing keys, change ACLs, or bypass restricted-host access.
  Validate schema, encoding, names/order, both variants, all required field
  types/ranges and contrast before publishing a value snapshot. Do not mix
  incomplete skins with fields from another palette. Diagnose real API failures
  and malformed configuration explicitly.
- [ ] Publish catalog updates by staging and validating a new complete catalog
  before switching `ActiveCatalog` and issuing the existing configuration
  notification. On write/validation failure, preserve the old catalog, skin
  selection and effective colors; do not automatically delete catalogs that
  existing readers may still use. Resolve/copy colors outside Paint and cache
  the validated snapshot instead of querying registry fields during drawing.
- [ ] Add `ImeSkinId` to the shared configuration field and synchronization
  mechanisms. Default missing legacy selections to `default`; reject invalid
  selections on save. Keep skin, theme, text scale and Comfort mode independent.
  Theme changes retain the skin ID; skin changes retain the theme. Preserve
  both preferences across high-contrast entry/exit. Reuse shared color resolution
  for candidate UI, the IME custom quick menu and candidate-style preview.
- [x] Use App mode for the candidate popup and candidate-style preview in all
  hosts, without adding Windows system-surface detection. Keep custom quick
  menus on their existing Windows-mode rule. Manual themes/high contrast stay
  unchanged. This source-policy change does not implement the skin loader.
- [ ] Apply every required candidate color in Direct2D and GDI: bottom surface,
  outer border, code title/page information, ordinary candidate text, selected
  row background/text/border, empty-message text, and enabled/disabled page text.
  Keep `EmptyText`, `PageEnabledText` and `PageDisabledText` explicitly defined
  even when values equal other roles. Keep candidate numbers the same color as
  their row in Phase 1. Empty candidates must have no fake selection or paging.
- [ ] Apply the same skin family to the IME custom quick menu: background,
  border, text, hover/current item, check marks, disabled state, separators and
  keyboard-focus cues. Keep hover, checked and focus states distinct. Do not
  reuse candidate-selection text or title accents indiscriminately for menus.
  Leave native scrollbars system-rendered in Phase 1.
- [ ] Add a default entry plus all 21 bilingual choices to display settings.
  Preview the edited selection without starting composition or recoloring the
  settings window. Preview must obey high-contrast priority, including when
  showing Light/Dark pairs. Save through the existing acceptance/notification
  flow; cancellation must not persist display edits, and failed saves must
  preserve stored values and report the actual error.
- [ ] Refresh on saved configuration and system theme/high-contrast changes on
  each window's owning thread. Color-only updates must not steal focus, clear
  composition, change selection, repaginate or move the input caret. Do not set
  the host process's preferred theme. Preserve Direct2D failure diagnostics
  and equivalent pure-color GDI fallback behavior.

### Phase 1 acceptance

- [ ] Verify all 42 variants and 546 explicit fields against the design data,
  including RGB24 conversion, isolated-registry round trips, missing/invalid
  IDs, wrong types/ranges/schema, missing Light/Dark roles, denied reads/writes,
  staged-update failure, and configuration notification/snapshot refresh.
- [ ] Verify first/last/empty candidate states, code title and paging colors,
  selected border, menu hover/check/disabled/focus states, and preview/save/
  cancel behavior. Check text roles at least 4.5:1 and selected-border versus
  ordinary background at least 3:1; do not substitute the decorative outer
  border for that measurement. Preserve the existing app high-contrast 7:1
  target and Windows system-color behavior.
- [ ] Cover all four Windows/App mode combinations, explicit Light/Dark,
  candidate/preview App-mode behavior independent of host identity, unchanged
  Windows-mode custom menus,
  system/app high-contrast entry/exit, horizontal/vertical layout, long
  candidates, paging, DPI/text scaling, Comfort mode, clipping and small-work-area
  scrolling. Run affected x64/x86 builds, focused suites and existing C++ quality
  checks; separately record real-host checks, including restricted hosts and
  host-rendered UILess UI. Do not log real input or candidate text.
- [ ] Update user/developer documentation to distinguish implemented surfaces
  from design-only features; automated color checks do not certify all hosts
  or accessibility. Do not generate MSI packages unless explicitly requested.

### Phase 2 and exclusions

- [ ] After Phase 1 acceptance, design/validate gradients, rounded corners,
  subtle separators and ordinary shadows with consistent Direct2D/GDI behavior,
  window bounds and hit testing. Colored glow remains deferred.
- [ ] Separately evaluate candidate-number/hint colors, candidate page-button
  hover/pressed states and native scrollbar adaptation; these require additional
  drawing/state/accessibility work, not just more palette entries.

### Deferred candidate host adaptation

- [ ] Evaluate Windows-mode exceptions for positively identified and real-host-
  validated taskbar/Start search or other specific system input surfaces.
  Current candidates and previews uniformly use App mode; no detection is
  implemented in this change. Classify the actual TSF context's UI, never an
  entire process solely by executable name, Microsoft signature, AppContainer
  identity or immersive flags. File Explorer address/search/rename fields and
  unknown/null contexts must remain on App mode. Document supported Windows
  versions, identification evidence and real-host results for every exception.
  Cache outside Paint and invalidate on context/host changes; reuse existing
  window information, bound probes, and avoid per-keystroke process/signature/
  UI Automation work. Diagnose actual detection failures without input text or
  window titles. App mode is not an application's private per-window theme.
  Validate source-switch correctness and detection counts/timings before enabling
  any exception.

Standalone tools and their menus, the floating status bar and its tooltips do
not adopt candidate skins; their automatic themes remain on App mode. Candidates
and their previews currently use App mode in all hosts. Custom quick menus
keep their existing Windows-mode rule. Do not recolor taskbar brand/mode icons, host composition text/
underlines, system dialogs, Windows-hosted TSF menus or host-rendered UILess
candidates. Skin editors, external images/scripts/skin packs, and additional
status-bar/tooltip skin support require separate design approval.

## Tracked follow-up

- [ ] High priority: fix intermittent Log Monitor status-text flicker.
  Reported on 2026-11-17; code inspection found that the 100 ms refresh timer
  unconditionally calls `UpdateStatus`, including while capture is stopped.
  The ordinary `STATIC` footer receives `SetWindowTextW` even when its text
  is unchanged; Metrics view also repeatedly sets its summary and footer.
  Confirm the repaint path at runtime, then update these controls only when
  their displayed text changes, without reducing capture/drain frequency.
  If changed-text updates still flicker, investigate local buffered painting
  rather than forcing whole-window redraws. Add focused regressions proving
  unchanged ticks send no redundant text updates and real status/error,
  language, and view changes remain visible. Verify idle, active and stopped
  capture in Logs, diagnostics and Metrics views, including theme/DPI changes.
  This is tracked work only; the reported flicker has not yet been reproduced
  at runtime or fixed.

- [ ] Fix MSI uninstall failure when optional current-user data cleanup encounters
  a mapped user-dictionary cache (reported in 1.1.1 on 2026-11-16).
  Main-branch implementation now includes the two-choice wizard, protected
  same-volume quarantine, boot-session gate, and isolated regressions.
  Keep this item open until real Windows Settings/full/basic/reduced entry,
  elevated completion and actual reboot/reinstall are validated on a dedicated
  machine. No MSI was generated or installed for the source/fixture checks.
  ETW recorded `Uninstall::RemoveDataFile` returning `0x80070005`; the
  `RemoveUserData` action exited with code 1, causing MSI error 1722, final
  status 1603, and rollback. Read-only inspection found the remaining
  `user_dict.qdict` in use by multiple hosts; an isolated reproduction of the
  current deletion API failed while mapped and succeeded after unmapping.
  This concerns the personal user dictionary/cache, not the three bundled
  default dictionaries (`wubi86_jidian.txt`, `wubi86_jidian_extra.txt`, and
  `wubi86_haifeng.txt`): normal MSI uninstall removes those bundled files
  regardless of the personal-data options. Keep that payload removal separate
  from two independent, unchecked-by-default uninstall choices:
  1. Delete the initiating user's configuration only; preserve dictionary data.
  2. Delete the initiating user's QiQi-managed dictionary data: the personal
     user dictionary, known compiled dictionary caches, phrase-frequency data,
     and known synchronization files; preserve configuration unless option 1
     is also selected.
  Compiled caches here include known QiQi-generated caches of bundled
  dictionaries in current-user managed locations, not just `user_dict.qdict`.
  Preserve external/custom-path source dictionaries, exports, backups,
  unrecognized files, and other users' data. Do not scan drives or delete by
  extension alone. Capture and validate cleanup locations before deleting
  configuration so either choice and their combination remain correct.
  Implement an interactive uninstall wizard with three stages:
  1. Data choices: show the two independent options above on the first page,
     explain the default retention and irreversible deletion scope, and let
     the user cancel before deactivation, unregistration, or data deletion.
  2. Uninstall progress: start only after explicit confirmation; remove the
     program and perform only the selected personal-data cleanup, retaining
     the choices throughout the operation.
  3. Completion: distinguish program removal failure from successful removal
     with complete, reboot-pending, or incomplete personal-data cleanup.
     When a restart is required, explain why and prompt the user to save work
     and restart Windows; do not force an immediate restart or require routine
     manual file deletion.
  Cover interactive entry from Windows Settings as well as MSI maintenance,
  not just the authored full-UI dialog. Provide Chinese/English text and
  verify the actual full/basic/reduced entry flows. Silent install/uninstall
  must not show a wizard; retain explicit choices, the same safety checks,
  accurate logs, and documented success/restart-required/failure exit statuses.
  Define backward compatibility for `QIQI_REMOVE_USER_DATA`. Upgrade removal
  must not opt in to either choice.
  Agreed plan: attempt immediate cleanup first. For a cache still mapped/in
  use, safely rename it to a unique pending-deletion name without replacing
  another file, then use an appropriately elevated operation to call
  [`MoveFileExW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
  with a null destination and `MOVEFILE_DELAY_UNTIL_REBOOT`. Schedule only
  that isolated old cache, never its reusable original pathname, so reinstall
  or resumed use before reboot cannot cause newly created user data to be
  deleted. Preserve initiating-user ownership and path/reparse-point checks
  across elevation; do not resolve personal data from the elevated account.
  No resident cleanup process or routine manual deletion should be required.
  Distinguish files actually deleted, deletion successfully scheduled, and
  incomplete cleanup; successful scheduling is not proof of boot-time deletion.
  Clearly tell the user when program removal is complete but personal-data
  cleanup requires a restart; do not force an immediate restart.
  Persist this product's pending-restart state separately from removable user
  preferences and MSI-owned files, so uninstall and option 1 cannot erase it.
  Keep the initiating-user identity, validated pending-deletion targets, and
  reliable boot identity sufficient for the next installer to check the state
  across elevation, architecture, and UI-language changes.
  Before a subsequent install changes files or registration, check pending
  state left by QiQi uninstall. If the required system restart has not occurred,
  explain why and block installation until the user restarts Windows; cover
  full/basic UI and silent installs with explicit diagnostics and a documented
  non-success exit status. Logging off is not a system restart, and unrelated
  software's generic pending-reboot flags must not alone block QiQi installation.
  After a verified restart, validate the isolated pending targets are gone
  before clearing the marker and permitting installation. If cleanup is still
  incomplete, report the actual problem and preserve the state rather than
  claiming success or repeatedly demanding restarts without diagnosis.
  Report rename/scheduling/state-persistence failures explicitly rather than
  claiming success or rolling back the entire uninstall solely because optional
  cache cleanup is incomplete. Do not treat every access-denied error as a
  mapped-file failure or weaken permission/path validation.
  Preserve actual HRESULT/Win32 diagnostics without logging dictionary contents
  or private paths. Add isolated immediate/mapped deletion, rename/scheduling
  failure, reboot-pending reporting, original-path reuse, partial-cleanup,
  all four configuration/dictionary option combinations, and preservation of
  external source dictionaries/backups and default-retained personal data.
  Cover same-boot installation refusal, verified-restart cleanup checks,
  pending-state survival after configuration deletion, unrelated pending-reboot
  flags, marker read/write failures, first-page defaults and pre-execution
  cancellation, choice preservation, completion/restart messages, and actual
  Settings/full/basic/reduced/silent and Chinese/English installer paths.
  Use fixtures or mocked scheduling/boot identity, not real reboot deletion
  of personal files; preserve upgrade guards. MSI rollback must not imply
  deleted personal data was restored. The implementation is unreleased and does
  not change any already installed `v1.1.1` package's cached uninstall sequence.

- [ ] Deferred: Investigate taskbar input-indicator disappearance and theme-aware icons.
  Follow the [research and validation record](input-indicator.md). The registered
  brand resource now uses the documented white box, black glyph, and 1px
  half-opaque black frame; its DLL resource ID and registration index are unchanged.
  Validate the updated brand in the actual taskbar and Win+Space switcher,
  including light/dark/high-contrast backgrounds, DPI, focus changes, and IME
  switching. Mode-icon theme refresh and disappearance remain separate checks.
  The prepared transparent black/white assets are not registered brand icons.
  Do not treat fixed-color contrast designs, resource generation, or the
  unsuccessful public `SetIcon`/`SetIconMode` probe as proof of automatic
  brand-color inversion.

- [ ] Audit and validate compatibility with the Windows-style Shell.
  Record manual results using the [offline interactive checklist](shell-checklist.html).
  Existing TSF, `UIELEMENTENABLED`, and `IMMERSIVESUPPORT` registration is
  a starting point, not proof of complete compatibility. Check the actual
  taskbar/Win+Space experience, focus and IME switching, desktop and
  AppContainer/restricted hosts, DPI, and supported Windows versions.
  Record real-host results separately from unit tests and category declarations.

- [ ] Harden candidate owned-window handling for missing or stale host HWNDs.
  The normal `ITfContextView::GetWnd` -> Win32 `::GetFocus()` fallback,
  root-window ownership, and owner-switch/lifetime tests already exist.
  Production presentation can still fall back to an unowned popup when no
  valid root owner is available or the owner is destroyed before creation.
  Define a safe hide/defer/retry policy instead, preserving composition and
  explicit diagnostics. Add focused tests for successful-but-null GetWnd,
  failed GetWnd with null focus, invalid root HWNDs, and owner destruction
  between lookup and window creation. Do not confuse Win32 `::GetFocus()`
  (HWND) with `ITfThreadMgr::GetFocus` (document manager).

- [x] Implement candidate-window notifications for
  [light-dismiss surfaces](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-candidate-window-interaction-with-light-dismiss-surfaces).
  The native popup sends `EVENT_OBJECT_IME_SHOW`, `EVENT_OBJECT_IME_HIDE`, and
  `EVENT_OBJECT_IME_CHANGE` through `NotifyWinEvent` with its HWND,
  `OBJID_CLIENT`, and `CHILDID_SELF`. Notifications follow actual visibility and
  geometry, including layout/DPI changes and native destruction; unchanged
  state and hidden staging do not generate duplicates. Scoped native-event
  regressions cover ordering, identifiers and teardown. UILess/blocked or
  uncreated native UI does not announce a visible popup.

- [ ] Validate candidate light-dismiss integration with real transient surfaces.
  Recheck SearchHost while composing and moving the taskbar between bottom,
  top, left and right, including mixed-DPI displays and without refocusing.
  Record the IME WinEvents, TSF text rectangles and actual popup rectangles.
  Do not treat the automated notification contract as proof that stale host
  caret coordinates or taskbar-search placement are fixed.
  The first-key stale-extent path now requests one guarded asynchronous
  confirmation for a visible native popup without a Win32 caret. Captured
  confirmation callbacks still returned stale whole-composition coordinates.
  Position queries now use a clone collapsed to the composition start, following
  the reviewed Weasel approach without modifying the original range/selection.
  A later marked capture showed that collapsed-start and selection/display
  extents can all remain stale before and after the first edit. One matching
  first-key release now triggers an additional guarded read, following Weasel's
  release-stage update boundary without modifying key handling or adding timers.
  Automated moved-work-area and stale-task regressions do not replace this host
  check.

- [ ] Audit IME DPI scaling against the
  [desktop-application DPI requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#dpi-scaling-support-for-desktop-applications).
  Existing monitor-DPI queries and scaling helpers are a baseline, not a
  completed conformance check. Ensure the IME DLL inherits the host's DPI
  awareness: audit its embedded manifest, process-level awareness setters,
  and reachable shared code. Review and justify any temporary thread-DPI
  overrides, restoring the host context on every exit and reentrant path.
  Keep standalone tool EXEs' DPI policies separate from the in-process IME.
  Verify candidate/status/menu UI, fonts, icons, geometry, and hit testing
  across DPI-unaware, system-aware, and per-monitor-aware v1/v2 hosts.
  Check TSF caret extents and Win32 fallback coordinate virtualization,
  GetDpiForMonitor/GetDpiForWindow selection, error/fallback diagnostics,
  and avoidance of double scaling. Cover mixed-DPI monitor transitions,
  dynamic DPI changes, negative coordinates, and 100/125/150/175/200% scales
  with focused regressions and real-host validation on x64/x86.

- [ ] Audit and complete
  [IME candidate-list accessibility for Narrator](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-accessibility).
  Set `UIA_AutomationIdPropertyId` to exactly `IME_Candidate_Window` for
  conversion lists or `IME_Prediction_Window` for prediction lists, according
  to the actual list type. Raise `UIA_MenuOpenedEventId` and
  `UIA_MenuClosedEventId` when the list actually appears and disappears.
  On selection changes, raise `UIA_SelectionItem_ElementSelectedEventId`
  for the selected item and expose its
  `UIA_SelectionItemIsSelectedPropertyId` as TRUE; clear stale selections.
  Each item's `UIA_NamePropertyId` must be its candidate text, with optional
  disambiguation in `UIA_HelpTextPropertyId`, not a replacement name.
  Cover keyboard selection, paging, candidate updates, hiding/destruction,
  and UILess coordination without announcing nonexistent native UI.
  These UIA events do not replace the IME light-dismiss WinEvents above.
  Add property/event regressions using synthetic candidates and manual
  Narrator validation; do not capture real input or candidate text in logs.

- [ ] Complete and validate TSF UILess-mode conformance.
  The current candidate UI implements basic `ITfCandidateListUIElement`
  and `ITfUIElementMgr` negotiation, but the full contract still needs review.
  Cover host-controlled visibility, Begin/Update/End lifecycle, initial and
  subsequent update flags, and association with the active document.
  Do not show an unnegotiated popup when UILess forbids it or when the
  manager/BeginUIElement negotiation fails. Preserve actual HRESULT diagnostics
  and add focused failure-path/reentrancy tests plus real UILess-host validation.

- [ ] Implement and validate the Windows IME search-integration APIs.
  Neither `ITfFnSearchCandidateProvider` nor
  `ITfIntegratableCandidateListUIElement` is currently implemented.
  Expose the search provider through `ITfFunctionProvider::GetFunction`;
  provide candidates without modifying live composition or generating input
  events, and explicitly define `SetResult` learning/privacy behavior.
  Expose the integratable interface on the candidate UI and support its
  integration style, selection, keyboard, numbering, and finalization contract;
  review the companion `ITfCandidateListUIElementBehavior` requirements.
  Add COM/interface, candidate-result, and lifecycle regressions, then validate
  supported integrated search controls. Ordinary typing into a search box is
  not proof of search integration; do not log real queries or candidate text.

- [ ] Add automated conformance tests for
  [Microsoft's IME icon requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-icons).
  Cover both brand and mode icons, not only the current theme assets:
  - Require ICO frames for at least 16, 20, 24, 32, 40, and 48px; every
    supplied resolution must be 32-bit with an alpha channel.
  - Require black/white artwork, permitting neutral grayscale antialiasing
    and alpha edges but rejecting colored pixels.
  - Brand icons: white box, black glyph, and a 1px black outer stroke at
    50% opacity. Clarify applicability of the guideline's "new" variant
    and test its upper-left rounded corner where applicable.
  - Mode icons: white glyph with a 1px black outer stroke at 50% opacity.
  - Measure geometry, stroke width/opacity, clipping, and contrast on light
    and dark backgrounds; do not substitute loose visibility counts for
    the required output shape or silently accept theme-aware deviations.
  - Check source ICOs and the icons actually embedded/returned by the IME
    DLL on x64/x86. Keep standalone tool icons outside these IME-only rules.

- [x] Add standalone configuration JSON export/import.
  Available on the existing Dictionary settings page, with an independent modal
  change preview and no new tab. Confirmed imports remain unsaved edits until
  Settings OK; cancellation or failed persistence preserves the saved configuration.
  Support partial imports, native scalar types and stable enum names; reject
  duplicate/unknown fields, invalid UTF-8/JSON, types, ranges, and format/version.
  Keep local paths and dictionary contents outside the JSON. See the
  [configuration transfer guide](accessibility.md#配置-json-导入与导出).

- [ ] Replace the custom backup format with ZIP complete backups.
  Use UTF-8 JSON (`.json`) for configuration, with explicit `format` and `version`,
  native boolean/numeric values, and stable string enum names. Use ZIP (`.zip`)
  for complete backups containing configuration JSON, user dictionary, phrase
  ordering data, and any selected custom primary dictionary. Do not export
  machine-specific absolute paths; restore bundled files using portable references.
  No compatibility with the existing backup format is required. Validate the
  entire input before applying changes, and explicitly report write failures
  or partial restoration rather than claiming success. Reuse shared configuration
  field definitions and file helpers, remove the superseded custom parser, and
  add focused round-trip, malformed-input, validation, and failure-path tests.
  Allow users to edit configuration JSON manually; do not require a checksum.
  Treat imports as untrusted input: strictly validate types, ranges, enum names,
  format/version, and reject unknown fields and duplicate keys with actionable
  field-specific errors. Missing settings retain their current values, allowing
  partial configuration imports. Only explicitly supported settings may be
  imported, never arbitrary registry keys or machine-specific paths. Show a
  change summary and require confirmation before saving. Validation failure or
  cancellation must leave the current configuration unchanged; failed saves must
  preserve the original configuration and report the actual error. Include focused
  tests for manual edits, partial imports, duplicate/unknown keys, cancellation,
  and preservation of existing settings on failure.

- [x] High priority: add an independent prefix-completion setting.
  Use "编码补全" / "Code completion" as the Chinese / English UI labels.
  Preserve the current enabled default. When disabled, show only exact code
  matches while retaining valid short-code candidates. Persist the setting,
  expose it in the settings UI, and add focused lookup/configuration tests.
  Prioritize this task over the short-code and single-character controls below.

- [ ] Lower priority: add short-code control / full-code practice mode.
  Confirm the intended behavior before implementation, including whether
  incomplete-code hints remain visible and how custom short codes are handled.
  If full-code-only mode is chosen, prevent short-code commits through every
  selection path and identify actual complete codes rather than requiring four
  letters universally. Keep this separate from prefix completion, preserve
  dictionary files, and add focused lookup and commit-path tests.

- [ ] Lower priority: add an optional single-character-only candidate mode.
  Keep the mode disabled by default. Filter multi-character Wubi dictionary
  candidates without modifying dictionary files or affecting punctuation and
  temporary English. Confirm how user-defined phrases are handled before
  implementation. Keep this independent of short-code control, prefix completion,
  dynamic phrase ordering, and automatic phrase creation. Persist the setting,
  expose it in the settings UI, and add focused filtering and commit tests,
  including the effect on unique full-code auto-commit.

- [ ] Add an optional CapsLock Chinese/English mode-switch setting.
  Preserve CapsLock English pass-through as the default. Reuse the shared mode
  switching path and intercept CapsLock in the optional mode so switching does
  not also toggle the OS uppercase lock. Confirm pending-composition behavior
  and handling of an already-enabled OS CapsLock state before implementation.
  Persist the setting and expose it in the settings UI. Keep TSF compartments
  and mode indicators synchronized, preserve restricted-input behavior, and
  add focused tests for prediction callbacks, key repeat, composition handling,
  and both CapsLock modes.

- [ ] Add a persisted three-option Space/Enter behavior setting for Wubi composition.
  Expose mutually exclusive options in the settings UI with these labels:
  - "空格选字，回车输出编码" (default): Space commits the highlighted
    candidate; Enter commits the raw code.
  - "空格选字，回车清码": Space commits the highlighted candidate;
    Enter cancels the current composition.
  - "回车选字，空格清码": Enter commits the highlighted candidate;
    Space cancels the current composition.
  Preserve existing defaults, including the current empty-code clearing option.
  Define no-candidate behavior and interaction with that option before
  implementation. Keep Esc cancellation, temporary English, and keys outside
  Wubi composition unchanged. Add focused configuration, prediction, and
  key-handling tests for all three options, including highlighted selection
  and no-candidate cases.

- [ ] Measure input-path allocation pressure and deferred edit-session retention.
  Count allocations and allocated bytes per key/edit request, including result
  snapshot copies and stored callbacks; measure snapshot size and requeue counts.
  Track pending edit-session count and retained-byte peaks under normal and slow
  host delivery. Measure heap usage after sustained input and queue drainage to
  distinguish temporary retention, leaks, allocator caching, and fragmentation.
  Compare metrics-disabled and metrics-enabled runs to quantify measurement
  overhead. Use evidence before changing snapshot ownership, callback storage,
  or introducing object pools; do not record input text or candidate contents.

- [ ] Add configurable hotkeys and selectable Haifeng/Jidian key presets.
  Verify the default bindings against documented versions before defining the
  presets, and confirm whether they cover shortcuts only or broader key behavior.
  Let users select a preset in settings and customize individual bindings.
  Validate conflicts, persist configuration, and safely replace registered TSF
  preserved keys when settings change. Preserve current defaults and restricted
  input behavior; add focused configuration, registration, and key-handling tests.

- [ ] Long-term, deferred: show candidate-specific Wubi root-code hints.
  Display the roots used to encode each candidate beside its label, including
  roots selected by phrase-coding rules rather than full decomposition of every
  character. This is not a keyboard root chart or stroke animation. Before
  implementation, verify redistribution licenses for decomposition data and
  root fonts, Wubi 86 consistency with active dictionaries, and handling of
  short codes and identification codes. Add an optional display setting and
  validate rendering and hint accuracy when this task is scheduled; do not
  import data or implement the feature yet.
