# Restricted Input and Password Fields

## Current behavior

QiQiIME applies a shared input-access policy to keyboard callbacks, Shift mode
switching, preserved shortcuts, candidate actions, and queued edit/layout work.
The policy is implemented, not a pending research proposal.

When a password/PIN scope or a confirmed native password EDIT is recognized,
the IME temporarily switches to English and restores the previous Chinese/English
mode on exit. This does not save a new configuration or restore a cancelled
composition. Ordinary numeric fields, read-only fields, and private scopes are
not automatically classified as passwords.

Recognition depends on host-provided context, input scopes, and reliable native
focus information. It is not a guarantee for every browser, WPF, AppContainer,
or custom/game control. General host input success is not password-field
validation, and secure-desktop/login support is not claimed.

## Signals and policy

[ContextFacts and EvaluatePolicy](../src/ime/comp/access_policy.h) separate fact
collection from the pure policy decision:

| Condition | Input | Learning/persistence | Reset active state |
| --- | --- | --- | --- |
| Actual query error | No | No | Yes |
| No context, disabled/empty context, read-only, recognized password/PIN | No | No | Yes |
| Input-scope query pending | No | No | No |
| `IS_PRIVATE` | Yes | No | No |
| Optional scope absent, or no explicit restriction | Yes | Yes | No |

"No input" means the IME must not consume or edit on behalf of that action; it
does not prevent the host from handling keys. Missing optional information is
not a claim that the field is safe. A pending query must not reuse an old
context's permission.

Important TSF contracts:

- `GUID_COMPARTMENT_KEYBOARD_DISABLED` and `GUID_COMPARTMENT_EMPTYCONTEXT` are
  context-level signals. Query the actual context, not a thread-manager fallback.
  "Empty context" does not mean an edit box contains no text.
- `ITfCompartment::GetValue` can return `S_FALSE`/`VT_EMPTY` for unset values.
  Preserve HRESULT and type; distinguish absence from access failure.
- `TF_SD_READONLY` means noneditable, not sensitive, and can change dynamically.
- Read input scope through `GetAppProperty(GUID_PROP_INPUTSCOPE)` and a checked
  property value in a valid edit session. `S_OK` with `VT_EMPTY` provides no
  `ITfInputScope` interface.
- Inspect all scopes returned by `GetInputScopes`; release their array with
  `CoTaskMemFree`. Password/PIN takes precedence over `IS_PRIVATE`.
- `IS_PASSWORD`, `IS_NUMERIC_PASSWORD`, `IS_NUMERIC_PIN`, `IS_ALPHANUMERIC_PIN`,
  and `IS_ALPHANUMERIC_PIN_SET` are supplementary restriction signals, not a
  security guarantee that the host disables text services correctly.
- Native `ES_PASSWORD`/`ES_READONLY` styles are meaningful only for a confirmed,
  current-thread native EDIT target. Do not generalize a top-level HWND to
  windowless browser fields or WPF PasswordBox.

Detection must not call `GetText`, inspect passwords/clipboard data, scan other
processes, attach input queues, or guess from a process-name blacklist.

## Lifecycle and persistence boundaries

Fast checks collect context compartments, status, and reliable native-window
facts. Input scope is read in a legal read-only session; synchronous lock refusal
can require an asynchronous query. Bind results to context identity and revision,
then recheck after reentrant host calls and before queued work executes.

Password observation is independent of composition lifetime. It observes text
scope updates and confirmed native `EM_SETPASSWORDCHAR`/`WM_STYLECHANGED`
notifications, including changes while a control is focused. Focus/context
removal or deactivation disconnects old observation; stale callbacks cannot
restore an old mode.

Check policy before engine processing, clipboard shortcuts, pin/suppress actions,
or persistent learning, not only before final document writes. In a private
scope, normal composition/candidates remain available but dynamic learning and
user-dictionary mutations are suppressed without changing global settings.

When input becomes restricted, invalidate old work and reset local engine/UI
state through the existing lifecycle protocol. Cleanup may operate only on its
captured old context/composition; context removal is not a new edit opportunity.
Host refusal to clear attributes or end composition must retain its actual error,
not be reported as successful cleanup.

Implementation entry points are [key handling](../src/ime/tsf/tip_keys.cpp),
[composition edits](../src/ime/tsf/tip_composition.cpp),
[mode observation](../src/ime/tsf/tip_config.cpp), and
[service lifecycle](../src/ime/tsf/tip_service.cpp).
See the [composition contracts](composition.md) for reentrancy and cleanup.

## Validation

[Restricted-input tests](../tests/ime/tsf/restricted_input.cpp) combine native
TSF contract checks, controlled contexts, real native EDIT style changes, and
deterministic queued/reentrant callbacks. The standalone
`QiQiTsfTestHost.exe` supports manual control-level checks.

```powershell
cmake --build --preset x64-release --target qiqi_tsf_ui_test QiQiTsfTestHost
ctest --test-dir build\x64 -C Release -R '^QiQi(TsfUi|CompositionLifecycle|Engine)Test$' --output-on-failure
.\build\x64\Release\qiqi_tsf_ui_test.exe --restricted-input-diagnostics
```

Repeat sequentially with x86 paths/presets. Diagnostic `[OBSERVE]` output and a
zero process exit are not substitutes for behavior assertions or real-host
acceptance.

Cover ordinary/password/read-only/numeric/private transitions, absent and failed
queries, multiple scopes, pending queries, context push/pop, queued edits that
become restricted, repeated notifications, reentrancy, and mode restoration.
Private TSF owners supplying scopes prove API behavior, not automatic recognition
of an arbitrary application's password field.

For browsers, Electron/VS Code, WPF, AppContainer, and games, record application
and Windows version, architecture, loaded DLL, target type, supplied signals,
repetition count, and redacted diagnostics. Mark each scenario passed, failed,
uncovered, or blocked. A host that dispatches no TSF callbacks is a host-disabled
path, not an IME detection success. Use synthetic input, never real credentials.
The [host guide](hosts.md) retains actual reported evidence and gaps.

## References

- [Predefined compartments](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments)
- [ITfCompartment::GetValue](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcompartment-getvalue)
- [ITfContext::GetStatus](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getstatus)
- [ITfContext::GetAppProperty](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getappproperty)
- [InputScope values](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/ne-inputscope-inputscope)
- [EDIT control styles](https://learn.microsoft.com/en-us/windows/win32/controls/edit-control-styles)
