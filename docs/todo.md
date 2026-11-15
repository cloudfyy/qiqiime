# TODO

## IME skins

- [ ] Phase 1: Add six original color palettes inspired by the candidate-window
  samples in `C:\temp\imeui-sample`: gold/lime, gray/black, teal, charcoal/blue,
  mustard/pink, and light green/dark green. Adjust text colors for readability.
- [ ] Add a persisted IME skin option independent of theme mode and Comfort
  mode, preserving compatibility with existing configurations. Windows system
  high contrast must override the selected skin.
- [ ] Add skin selection and preview to display settings. Share palette
  resolution between the preview, candidate window, and IME custom context
  menu; do not recolor standalone tool windows.
- [ ] Validate configuration round trips, all six palettes, DPI, text scaling,
  Comfort mode, high contrast, clipping, and scrolling. Add focused tests and
  update related documentation while preserving candidate selection and paging.
- [ ] Phase 2, after Phase 1 validation: Add rounded corners, subtle separators,
  and ordinary shadows with consistent Direct2D and GDI fallback behavior.
  Validate window boundaries and hit testing. Colored glow is deferred.

Windows-hosted TSF menus remain system-controlled and are not guaranteed to
display custom skins.

## Tracked follow-up

- [ ] Replace the custom backup format and add standalone configuration export/import.
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

- [ ] High priority: add an independent prefix-completion setting.
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
