# TODO

## IME skins

Design catalog: [skin appearance, bilingual names, and illustrations](ime-skins.md).
Phase 1 is now explicitly scoped to all 21 skins with 42 Light/Dark variants.
The [complete palette data and state diagrams](ime-skin-palettes.md) and
[registry design template](ime-skins.reg) define all required candidate colors.
The [element scope and refresh design](ime-skins.md#7-哪些界面元素可以支持皮肤)
recommends candidate regions, the IME quick menu, and preview first; additional
hover/pressed states, native scrollbar adaptation, and status-bar/tooltip skins
remain separate design decisions.

- [ ] Phase 1: Add all 21 named skins with Light/Dark variants (42 palettes).
  Explicitly configure background, border, code title, candidate text,
  selection background/text/border, empty text, and enabled/disabled page text.
  Theme chooses brightness; skin chooses color family.
- [ ] Add a persisted IME skin option independent of theme mode and Comfort
  mode, preserving compatibility with existing configurations. Windows system
  high contrast must override the selected skin; app high contrast bypasses it too.
  Explicit Light/Dark chooses the corresponding skin variant; automatic candidate
  surfaces follow Windows mode. Keep the same skin ID across theme changes and
  retain both preferences when entering/exiting high contrast. Default skin keeps
  existing colors. See the [confirmed combination rules](ime-skins.md#82-皮肤选项与主题模式).
- [ ] Implement the [registry catalog contract](ime-skins.md#84-注册表配置契约):
  stable skin ID separate from versioned paired definitions; validate all required
  REG_DWORD RGB24 fields; stage a complete catalog before switching ActiveCatalog.
  Reuse configuration notifications, preserve old state on failure, and keep IME
  reads side-effect-free. The design template is not yet imported or implemented.
- [ ] Add skin selection and preview to display settings. Share palette
  resolution between the preview, candidate window, and IME custom context
  menu; do not recolor standalone tool windows.
- [ ] Validate configuration round trips, all 42 variants and their complete
  first/last/empty candidate states, DPI, text scaling,
  Comfort mode, high contrast, clipping, and scrolling. Add focused tests and
  update related documentation while preserving candidate selection and paging.
- [ ] Phase 2, after Phase 1 validation: Add rounded corners, subtle separators,
  and ordinary shadows with consistent Direct2D and GDI fallback behavior.
  Validate window boundaries and hit testing. Colored glow is deferred.

Windows-hosted TSF menus remain system-controlled and are not guaranteed to
display custom skins.

Preserve the system-following split when adding skins: candidate UI, IME custom
menus, and candidate-style previews use Windows mode; standalone tool windows,
the floating status bar, and its tooltips use App mode. Explicit themes and
Windows high contrast retain their existing precedence.

## Tracked follow-up

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

- [ ] Implement candidate-window integration with
  [light-dismiss surfaces](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#ime-candidate-window-interaction-with-light-dismiss-surfaces).
  Send the required WinEvents via `NotifyWinEvent` or an equivalent supported
  mechanism: `EVENT_OBJECT_IME_SHOW` when the candidate UI becomes visible,
  `EVENT_OBJECT_IME_HIDE` when it is hidden, and `EVENT_OBJECT_IME_CHANGE`
  when it moves or changes size. Tie notifications to actual window transitions,
  including DPI/layout changes and destruction; avoid duplicates and do not
  announce visible UI in UILess mode or after failed window creation.
  Validate HWND/object/child identifiers, event ordering, and lifetime with
  scoped automated event hooks, then test interaction with real transient
  surfaces. Hiding on focus loss alone does not satisfy this contract.

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
