# Radiant ContentEditable WPT Status

Tracking matrix for the planned `test/test_wpt_contenteditable_gtest.exe`
against the editing-host slice of WPT
(`ref/wpt/selection/contenteditable/`, `ref/wpt/input-events/`,
`ref/wpt/focus/`, `ref/wpt/html/interaction/focus/`).

See [`Radiant_Design_Content_Editable.md`](Radiant_Design_Content_Editable.md)
for the full proposal and phase plan (CE-1 … CE-7). This document
tracks per-file conformance once the runner ships; until then it
records what has landed and the gap to the curated Tier-A list in
§11.1.

## Phase summary (so far)

| Phase | Status | Headline |
|---|---|---|
| **CE-1** — `EditingHost` core | ✅ Shipped | `radiant/editing_host.{hpp,cpp}`; central lookup with mode (`Rich` / `PlaintextOnly`) and `target_in_false_island` flag. Replaces the four ad-hoc `contenteditable` reads in `event.cpp` and the local `editing_host_of` in `dom_range.cpp`. |
| **CE-2** — Focus model | ✅ Shipped | `is_view_focusable()` admits editing hosts as implicit `tabindex=0`. Esc-doesn't-blur and focus-driven IME activation are inherited from the existing focus + `ime_mac.mm` infrastructure. |
| **CE-3** — `InputEvent` complete | 🟢 Shipped (key items) | Native factory `js_create_native_input_event` + C bridge `radiant_dispatch_input_event`. `dispatch_rich_beforeinput` fires JS `beforeinput`, then JS `input` (when not preventDefault'd), then Lambda template handler. Full §6.2 `inputType` enum coverage with `format*`/`selectAll` excluded per §9. **`getTargetRanges()` real snapshot** computed at dispatch time via `compute_target_ranges()` — selection range for inserts / cut / drag; about-to-delete range walked through `dom_boundary_move()` for delete\*Backward/Forward and Word/Line variants; empty for history\*/selectAll/insertReplacementText. **`dataTransfer` populated** with `text/plain` + `text/html` records for rich `insertFromPaste`/`insertFromPasteAsQuotation`/`insertFromDrop`/`deleteByDrag`/`deleteByCut` via `js_data_transfer_new_with_strings()`; text controls keep `dataTransfer === null` and expose paste/drop text through `data`. Rich transfer events now expose nullable `data === null`, matching the Input Events cut/paste WPT surface. **Remaining**: legacy `te_dispatch_beforeinput` fallback audit, `insertCompositionText` lowering from IME backend, and file/blob drops. |
| **CE-4** — `inputmode`/`enterkeyhint` | ✅ Shipped (stub for forwarding) | IDL reflection of `inputMode`/`enterKeyHint` with spec keyword validation. `contentEditable` setter validates per spec; `isContentEditable` walks ancestors honouring inheritance + `="false"` islands. Focus-time hint forwarding stub logs to `log.txt`; real platform IME forwarding (NSTextInputClient / TSF / IBus) reserved for editor3 §3.9. |
| **CE-5** — Drop into editable | ✅ Shipped (lowered) | Drop completion at `event.cpp` lowers to `beforeinput { insertFromDrop }` on the target with string payload carried by `dataTransfer` for rich hosts and by `data` for text controls, paired with `beforeinput { deleteByDrag }` on the source when source was also editable. File-backed drops remain deferred to clipboard Phase 9 transport. |
| **CE-6** — Reject legacy APIs | ✅ Shipped | `document.execCommand()` returns `false` (no shim path = no mutation, no `beforeinput`). `queryCommandSupported`/`Enabled`/`Indeterm`/`State` → `false`. `queryCommandValue` → `""`. `document.designMode` getter returns the literal `"off"`. Setter is a silent expando write that the getter ignores. The clipboard WPT `__lambda_execCommand_handler` shim path is preserved for the copy/cut/paste user-gesture surface. |
| **CE-7** — JS API surface + status doc | ✅ Shipped (this doc) | `HTMLElement.contentEditable` / `isContentEditable` / `inputMode` / `enterKeyHint` exposed (CE-1 + CE-4). `InputEvent` constructor accepts nullable `data`, `dataTransfer`, `isComposing`, `inputType`, and immutable `targetRanges` snapshots. `StaticRange` constructor registered (`JS_CTOR_STATIC_RANGE`) — stub copies the dictionary fields and computes `collapsed`. Status doc published here. |

## Headline Numbers

| Bucket | Count |
|---|---|
| Tier-A test files planned | ~12 (see §11.1 of the design doc) |
| Tier-A wired into CI | **0** (runner not built; deferred) |
| Sub-tests passing | n/a until runner ships |
| Tier-B (`editing/run/*`) gauge | not measured — expected to mostly fail per §9 |
| Documented skips | per §11.4 |

## Per-File Status

The Tier-A WPT runner under `test/wpt/` for contenteditable is **not yet
built**. The design doc §12 sequences it after CE-3 (the `InputEvent`
complete-surface exit criterion). Once the runner lands, this table will
populate with the same `Pass / Fail / Skip — sub-tests N/M` shape as
`Radiant_Clipboard_WPT_Status.md`.

### Tier-A planned (curated, must be 100 %)

| WPT area | Status |
|---|---|
| `selection/contenteditable/collapse.html` | not yet wired |
| `selection/contenteditable/cefalse-on-boundaries.html` | not yet wired — CE-1 false-island lookup ready |
| `selection/contenteditable/modify*` | not yet wired |
| `selection/textcontrols/selectionchange*` | not yet wired |
| `selection/textcontrols/focus.html` | not yet wired — CE-2 focus model ready |
| `input-events/input-events-typing.html` | not yet wired — CE-3 partial dispatch ready |
| `input-events/input-events-get-target-ranges*` (non-tentative) | not yet wired — real `StaticRange` snapshot shipped; WPT runner pending |
| `input-events/input-events-cut-paste.html` | not yet wired — native data/dataTransfer split now matches the text-control vs contenteditable WPT surface |
| `input-events/input-events-delete-selection.html` | not yet wired |
| `input-events/idlharness.window.js` (subset) | not yet wired — `InputEvent` / `StaticRange` ctors registered |
| `editing/event.html` | not yet wired |
| `focus/focus-event-targets` | not yet wired — CE-2 implicit-focusable ready |

### Tier-B gauge — `ref/wpt/editing/run/*`

Per §11.2 and §9, the entire `execCommand` quirk corpus is **expected
to mostly fail by design** since Radiant returns `false` from
`execCommand` for every command (CE-6). A subset incidentally tests
caret-navigation behaviour that the modern path *should* honour; track
those passes once the runner is wired. **Never gates CI.**

| Gauge | Value |
|---|---|
| `editing/run/*` files | not yet measured |

## Skip rationale (per §11.4)

| Bucket | Why skipped |
|---|---|
| `editing/run/*` (Tier B) | execCommand corpus — we reject the API (§9, CE-6); kept as a gauge only. |
| `editing/manual/*` | Manual gestures; out of automatable scope. |
| `editing/plaintext-only/*` cases that depend on legacy execCommand semantics | Replaced by `inputType` filtering tests authored against §4.1's `plaintext-only` mode. |
| `editing/edit-context/*` | Experimental `EditContext` successor API; revisit in a later stage (§11.2 informational). |
| `selection/shadow-dom/*` (subset) | Shadow DOM is a separate Radiant work-stream; partial defer per §14 Risks. |
| `uievents/keyboard/*` tests that depend on a specific OS layout | Documented per-test as platform-specific. |
| `input-events-exec-command.html` | Tests `beforeinput { inputType: "formatBold" }` etc. dispatched from `execCommand`. §6.2 + §9 explicitly do not dispatch these — keep as a documented skip with the negative-test counterpart from CE-6 covering the API rejection. |

## Negative tests (CE-6)

A future `test/wpt/test_wpt_contenteditable_negative_gtest.cpp` (per
§13 acceptance criteria) asserts the §9 contract:

- `typeof document.execCommand === 'function'` (feature detection works) **and** `document.execCommand('bold') === false` (no mutation, no `beforeinput`)
- `document.queryCommandSupported('bold') === false` (likewise for `Enabled`/`Indeterm`/`State`)
- `document.queryCommandValue('foreColor') === ""`
- `document.designMode === "off"`; assigning `"on"` and re-reading still returns `"off"`
- No keystroke ever produces `beforeinput { inputType: "formatBold" }` (verified by event-log inspection)

These are unit-test-shaped, not WPT files — kept alongside the
Tier-A runner once it lands.

## Known gaps (open work after CE-1 … CE-7)

| Gap | Tracked under |
|---|---|
| ~~`getTargetRanges()` real `StaticRange[]` snapshot~~ | ✅ Shipped (CE-3 follow-up) |
| ~~`dataTransfer` population on `insertFromPaste` / `insertFromDrop`~~ | ✅ Shipped — rich hosts expose text/plain + text/html via `js_data_transfer_new_with_strings()`; text controls keep `dataTransfer === null` |
| ~~`InputEvent` constructor `targetRanges` / nullable `data` surface~~ | ✅ Shipped (ED2-4 follow-up) |
| ~~Post-mutation `input` event firing~~ | ✅ Shipped (CE-3 follow-up) |
| `te_dispatch_beforeinput` (textarea/input) → JS bridge | CE-3 follow-up |
| `insertCompositionText` lowering from IME backend | CE-3 follow-up + editor3 §3.9 |
| Platform IME forwarding of `inputmode`/`enterkeyhint` | editor3 §3.9 `RdTextInputClient` |
| `DataTransfer.files` / `DataTransferItem` blobs on drop | CE-5 follow-up (only string items today; full multi-MIME / file items defer to clipboard Phase 9 reuse) |
| WPT runner + status table population | next CE-7 follow-up |
| `EditContext` API (`editing/edit-context/*`) | Tier-B informational, no commitment (§15) |

## References

- [Radiant_Design_Content_Editable.md](Radiant_Design_Content_Editable.md) — the proposal (CE-1 … CE-7 phase plan, §6 InputEvent surface, §9 legacy-API rejection)
- [Radiant_Clipboard_WPT_Status.md](Radiant_Clipboard_WPT_Status.md) — template + status pattern this doc follows
- [Radiant_Design_Selection.md](Radiant_Design_Selection.md), [Radiant_Design_Selection2.md](Radiant_Design_Selection2.md) — selection / range layer below
- [`../Radiant_Rich_Text_Editor3.md`](../Radiant_Rich_Text_Editor3.md) — the editor layer above
