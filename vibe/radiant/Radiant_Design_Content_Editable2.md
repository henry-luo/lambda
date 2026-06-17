# Radiant `contenteditable` 2 — execCommand, the Chrome editing corpus, and a green WPT baseline

**Date:** 2026-06-15
**Status:** Active implementation — P0 complete; Phase SI keyboard insert/delete/selectionchange/click-direction/mouse-button/number-spin-button/simple-block-join/whitespace-boundary/inline-block-join slices landed; EC-1 native core-text execCommand bridge landed; EC-2 selected-range inline formatting and conservative whole-wrapper toggle-off landed; EC-3 block structure started with single-block `formatBlock`, current-block justify commands, single-block ordered/unordered list insertion, and current-block indent/outdent; EC-4 links/objects started with selected-range `createLink` and nearest-anchor `unlink`.
**Layer:** DOM editing host + a new built-in editing-command engine on top of it.
**Builds on:** [Radiant_Design_Content_Editable.md](Radiant_Design_Content_Editable.md) (the editing-host / `InputEvent` / focus / selection foundation, phases CE-1…CE-7). This document **extends and partially revises** it.
**Revises:** [Content_Editable.md §9](Radiant_Design_Content_Editable.md) — the "execCommand is rejected and never implemented" line. execCommand is now **in scope** (see §2). The rest of the original contract stands.

---

## 1. Objective

Three deliberate enhancements to Radiant's editing surface, in dependency order:

1. **Clear the WPT baseline first.** Drive the existing `test_wpt_selection_gtest` and `test_wpt_contenteditable_gtest` runners to green (fix the two engine crashes + the selection / IDL / focus gaps) *before* taking on new scope. A stable selection + editing-host base is the prerequisite for everything below.
2. **Implement `execCommand`** (`document.execCommand`, `queryCommand{Enabled,State,Value,Indeterm,Supported}`, `designMode`) — a built-in editing-command engine layered on the existing `InputEvent` plumbing. This reverses the original "reject execCommand" stance (§2 explains why and how it reconciles).
3. **Adopt the Chrome (Blink) `editing/` test corpus** (`caret`, `deleting`, `inserting`, `execCommand`, `selection`, `style`, …) as a structured, phased conformance target — imported into the sibling **`lambda-test/editing/`** repo and driven by a new runner in `lambda`.

These are sequenced: **(1) gates (2) and (3); synthetic input (§5) gates the interactive parts of both.**

### 1.1 Mandatory per-phase regression gate

Every implementation phase in this document (**P0, SI, EC-\*, and CET-\***)
must close with the global JavaScript regression gate green. A phase is not
considered complete until both commands pass:

| Gate | Required result |
|---|---|
| `make test262-baseline` | `0` regressions and `0` retries |
| `./test/test_js_gtest.exe --gtest_brief=1` | `0` failed tests |

This gate is in addition to each phase's local WPT/Chrome-editing exit
criteria. If either command cannot be run, or reports any regression, retry, or
failure, the phase remains **in progress** and the blocker must be recorded in
the phase progress notes.

---

## 2. The execCommand pivot — why, and how it reconciles with Content_Editable.md

### 2.1 What the original doc said

[Content_Editable.md §9](Radiant_Design_Content_Editable.md) is emphatic: `execCommand` / `queryCommand*` / `designMode` are **not** implemented; the editing host **never mutates the document on its own** — every change is issued by a *consumer* (a Lambda `edit <…>` template or a JS editor). The whole point was to pin the *modern* contract (selection + `beforeinput` + `InputEvent` + composition) and refuse the legacy quirk surface.

### 2.2 Why this changes now

The original stance was the right default for building a *modern editor framework*. But two goals make execCommand worth having:

- **The Chrome/WebKit `editing/` corpus is execCommand-based.** Direction 3 adopts that corpus as a conformance target. `deleting/`, `inserting/`, `execCommand/`, `style/` all drive editing through `document.execCommand(...)` and assert the resulting DOM. Without execCommand there is nothing to run.
- **execCommand is still the lowest-common-denominator editing API.** Plenty of in-the-wild content and simpler editors rely on it. Supporting it widens what "just works" on Radiant.

### 2.3 How it reconciles — execCommand as a built-in *consumer*, not a bypass

The original architecture is **not** discarded. execCommand is implemented as the **canonical built-in consumer** that sits *on top of* the §6 `InputEvent` plumbing and supplies the **default action** the original doc deferred:

```
document.execCommand("bold")
        │
        ▼
  EditingCommand registry (§6) ── resolves "bold" → { inputType:"formatBold", … }
        │
        ▼
  dispatch beforeinput { inputType:"formatBold", targetRanges } (cancelable)
        │  (consumer may preventDefault → abort, exactly as today)
        ▼
  default action: the new DOM mutation engine applies the edit (§6.2)
        │
        ▼
  dispatch input { inputType:"formatBold" } + update selection + push undo entry
        │
        ▼
  return true
```

So the modern contract is preserved — every execCommand edit still flows through cancelable `beforeinput` → mutation → `input`. What changes is that **Radiant now ships the default action** (the DOM mutation engine, §6.2), where the original doc said "the host never mutates." A JS editor that wants full control still cancels `beforeinput` and applies its own model edit; it just now *also* has a working `execCommand` if it wants the batteries-included path.

**Net delta vs. Content_Editable.md:** the editing host gains a *default-action mutation engine* and the execCommand/queryCommand/designMode JS surface. Everything else (EditingHost lookup §4, focus §5, the `InputEvent` shape §6.1, `inputmode`/`enterkeyhint` §7, drop-into-editable §8) is unchanged and reused.

---

## 3. Current baseline — the starting line for Phase 0

Verified 2026-06 (debug build), both runners live:

| Runner | Cases | Result | Notable failures |
|---|---|---|---|
| `test_wpt_selection_gtest` | 151 | 18 fail | `selection/contenteditable/initial-selection-on-focus.tentative` **segfaults** (SIGSEGV); `collapse` 0/3; `modify*` partial; `caret/*` 0/0; `textcontrols/*`; `bidi/modify`; 2 pre-existing `move-by-word` i18n |
| `test_wpt_contenteditable_gtest` | 194 | 139 pass / 41 skip / 14 fail | `editing/crashtests/insertparagraph-in-listitem-in-svg-…` **SIGABRT**; `contentEditable`/`designMode`/`spellcheck`/`autocapitalize`/`writingsuggestions` IDL; focus-fixup / tabindex / autofocus; 23/25 `input-events` skipped pending synthetic input |

Two **genuine engine crashes** sit at the top of the list. They must be root-caused (not skipped) per the project's "no work-arounds" rule. *(Update: both are now fixed — see §4.1. The numbers above are the pre-Phase-0 starting line; current numbers are in §4.1.)*

---

## 4. Phase 0 — Clear the WPT gtests to green (gating prerequisite)

**Rationale (Direction 3's stated precondition):** do not build an execCommand engine and import 1000+ Chrome tests on top of an unstable selection + editing-host base. Get the existing runners green first; every fix here also advances CE-1/CE-2 from the original doc.

| Workstream | Target failures | Maps to |
|---|---|---|
| **P0-A — Engine crashes ✅ DONE (2026-06-15)** | `initial-selection-on-focus` (SIGSEGV) + `insertparagraph-in-listitem-in-svg` (SIGABRT) — both root-caused & fixed, no regressions (see §4.1) | stability |
| **P0-B — Selection ops** | `selection/contenteditable/collapse` (0/3), `modify` / `modify-around-*`, `textcontrols/*` selectionchange/focus | CE-1, §4.3 |
| **P0-C — Editing-host IDL** | `contentEditable` / `isContentEditable` enumerated+case-insensitive + slotted-inherit; `designMode`; `spellcheck`; `autocapitalize`; `writingsuggestions` | CE-1, §4.2 |
| **P0-D — Focus model** | focus-fixup-rule, tabindex getter / focus-flag, autofocus supported-elements | CE-2, §5 |
| **P0-E — Caret** | `selection/caret/*` (collapse-pre-linestart, move-around-cE-false, generated-content, designMode-off, invisible-br) | CE-1, §5 |

**Exit criterion:** both gtests **green** — zero unexpected failures; the only non-passing cases are *documented capability skips* (testdriver synthetic input, Shadow DOM, reftests, the 2 pre-existing `move-by-word` i18n cases tracked separately). Note: the 23 `input-events` + selection mouse-button skips are **not** gated here — they unblock in Phase SI (§5). "Green" means *every runnable assertion passes.*

### 4.1 Progress

**P0-A — engine crashes: DONE (2026-06-15).** Both root-caused and fixed; zero test regressions.

- **`initial-selection-on-focus` (SIGSEGV)** — `setAttribute`'s success check in
  `lambda/input/css/dom_element.cpp` (`if (result.element)`) treated a failed
  `elmt_update_attr` return (`ITEM_ERROR`, whose raw bits are non-null) as a
  valid pointer and wrote it into `native_element`; a later `get_attribute` →
  `ElementReader` then dereferenced it. **Fix:** guard on the runtime type
  (`get_type_id(result) == LMD_TYPE_ELEMENT && result.element`), matching the
  already-correct delete-attr path. *(This fix was committed alongside the
  parallel "layout fixes" commits.)*
- **`insertparagraph-in-listitem-in-svg` (SIGABRT)** — a debug `DocState`
  invariant *("non-collapsed selection has missing endpoints")*. Root cause was
  the **zero-initialized legacy `SelectionState`** (`is_collapsed = false`
  default — itself the invalid "non-collapsed with null endpoints" combo),
  latent until the first invariant check (here triggered by
  `selectAllChildren(<input>)` → `tc_ensure_init` → `state_set`, *not* the
  337M-char whitespace, which is a red herring). **Fix** in `radiant/state_store.cpp`:
  (a) default an empty selection to `is_collapsed = true`; (b) degrade the
  legacy projection to a clean empty/collapsed state when a non-collapsed
  selection has an endpoint that can't map to a rendered view — the canonical
  `EditingSelection` still holds the true range (the "clean collapse/none +
  reset legacy projection" approach).

**P0-B/P0-E — selection ops + caret movement: DONE (2026-06-16).** The
selection runner now has WPT variant support, documented capability skips
(reftest visual comparisons, `testdriver` synthetic input, Shadow DOM/manual
cases, and the two pre-existing i18n word-boundary cases), and browser-shaped
`Selection.modify` movement for:

- inline editing-host line boundaries, including bidi `left`/`right`;
- character movement across inline element boundaries with collapsed whitespace;
- `contenteditable=false` islands as atomic non-editable caret stops;
- `<br>` as the character step rather than consuming the adjacent text.

**P0-C/P0-D — editing-host IDL + focus model: DONE (2026-06-16).** The
`editing-0` IDL/focus cases in the contenteditable runner are now passing under
the same documented skip policy; `selectionchange` content attributes and text
control focus/selection semantics are covered by the selection runner.

**Current baseline (2026-06-16):**

| Runner | Cases | Result | Δ from §3 start |
|---|---|---|---|
| `test_wpt_selection_gtest` | 159 | 97 pass / 62 skip / 0 fail | SI-3/SI-5 converted nine `testdriver` selectionchange/click-direction/mouse-button cases from skip to pass |
| `test_wpt_contenteditable_gtest` | 196 | 165 pass / 31 skip / 0 fail | SI-1/SI-2/SI-7/SI-8/SI-9 converted ten `testdriver` input/pointer cases from skip to pass |

Regression guards green: `dom_range` 66, `source_pos_bridge` 22, `cmdedit` 82,
plus the live WPT guards listed in §5.1.

**P0 green status:** complete. No unexpected failures remain in the two WPT
gtest runners. The remaining non-pass cases are documented capability skips
that belong to later infrastructure phases: `testdriver`/synthetic input
(Phase SI), Shadow DOM coverage, reftest/manual visual comparisons, spelling
marker platform behavior, and the two separately tracked i18n word-boundary
cases.

---

## 5. Phase SI — Synthetic input (shared infrastructure)

Both the WPT `input-events` tests **and** the Chrome `editing/` corpus need synthetic keyboard/pointer input that Lambda's headless `js` runtime does not yet deliver (the long-noted "Phase 8F" gap; before SI it auto-skipped 23/25 `input-events` tests and the selection mouse-button tests).

This is a **shared dependency** of Directions 2 and 3, so it gets its own phase:

- **Headless input injection** in `lambda.exe js`: key down/press/up with `key`/`code`/modifiers, text/IME commit, and pointer down/move/up — delivered into the editing host's event pipeline so they dispatch real DOM events and drive selection/editing.
- **Two API frontings over the same injector:**
  - **WPT `test_driver`** (`Actions`, `send_keys`, `click`) — unblocks the WPT `input-events` + selection mouse-button tests.
  - **Blink/WebKit `eventSender`** + the relevant `testRunner` hooks — required by the Chrome `editing/` corpus (§7).
- **Unblocks:** 23 `input-events` WPT tests, the selection pointer tests, and essentially the entire *interactive* Chrome editing corpus (typing, caret navigation by key, selection by drag).

Sequencing SI early is high-leverage: it is the single capability that most of Directions 2 and 3 wait on.

### 5.1 Progress

**SI-1 — WPT keyboard insertion slice: STARTED / FIRST PASS (2026-06-16).**

- Added a WPT `test_driver.Actions().keyDown(<printable>).send()` path for
  focused text controls and document selections inside editable hosts. The
  path dispatches cancelable `beforeinput`, applies the headless default text
  insertion, updates the selection, then dispatches `input`.
- The shim honors editability: a nearest `contenteditable="false"` boundary
  blocks mutation, while an inner `contenteditable="true"` host inside a false
  island remains editable; `designMode="on"` is used only when there is no
  nearer contenteditable state.
- Enabled the first formerly skipped WPT `testdriver` case:
  `html/editing/editing-0/.../contenteditable-false-in-design-mode.html`
  now runs and passes `2/2`.
- While opening this path, fixed the `InputEvent` constructor's
  `targetRanges` handling: script-created `InputEventInit.targetRanges` are
  validated as live DOM `Range` objects and snapshotted when
  `getTargetRanges()` is called, matching
  `input-events-range-exceptions.tentative.html`.

**SI-2 — Delete/Backspace default action slice: LANDED (2026-06-16).**

- Added a native headless testdriver key hook for WPT Backspace/Delete. It
  targets the focused rich editing surface, maps plain Backspace/Delete and
  word-modified variants to Radiant `InputIntent`s, dispatches cancelable
  `beforeinput`, runs the rich default mutation, updates selection, then
  dispatches `input`.
- Implemented rich default deletion for `deleteContentBackward`,
  `deleteContentForward`, `deleteWordBackward`, and `deleteWordForward`.
  Non-collapsed selections delete the selected range; collapsed deletions use
  the existing UTF-8/word boundary helpers.
- Target ranges now follow the Input Events contract for this path:
  `beforeinput.getTargetRanges()` returns the pre-mutation static range, while
  the follow-up `input` event returns an empty range list.
- While enabling the WPT cases, fixed two root causes that would otherwise
  keep the headless path flaky:
  `innerHTML` now keeps DOM children and the backing Lambda `Element::items`
  tree in sync for parsed fragments, and top-level JS `let`/`const` module
  bindings captured by event-listener closures remain live instead of being
  copied into stale closure environments.
- The standalone JS `InputEvent` constructor test now uses explicit
  `StaticRange` objects for non-DOM snapshots, while DOM-backed
  `StaticRange`s still validate offsets through the live range path.

**SI-3 — selectionchange-on-Backspace slice: LANDED (2026-06-16).**

- Enabled the WPT Backspace selectionchange files:
  `fire-selectionchange-event-on-deleting-single-character-inside-inline-element`,
  `fire-selectionchange-event-on-pressing-backspace`, and
  `fire-selectionchange-event-on-textcontrol-element-on-pressing-backspace`.
  They now pass all `6/6` runnable assertions.
- Fixed the DOM-backed text empty-string root cause: rich Backspace can now
  replace the final character with a real zero-length Lambda `String` instead
  of hitting `MarkBuilder::createStringItem("")`'s empty-as-null behavior.
  The regression is covered by `DomText_EmptyString_Backed`.
- The native WPT key hook now queues document `selectionchange` after successful
  rich text mutations, and reports a key as handled only when `beforeinput` was
  prevented or the native mutation actually changed the DOM. Unsupported rich
  boundary deletes therefore fall back to the JS shim instead of being swallowed.

**SI-4 — click-direction selection slice: LANDED (2026-06-16).**

- Enabled the WPT selection-direction click files:
  `selection-direction-on-single-click`,
  `selection-direction-on-double-click.tentative`, and
  `selection-direction-on-triple-click.tentative`.
- These ride the existing WPT `Actions` pointer shim: single click collapses at
  the hit text node, double click selects the first word, triple click selects
  the containing block, and click-driven selections force
  `Selection.direction` to `none`.
- The trio now passes all `3/3` runnable assertions and moves the full
  selection runner to 94 pass / 65 skip / 0 fail.

**SI-5 — pointer mouse-button selection slice: LANDED (2026-06-16).**

- Extended the WPT `test_driver.Actions` pointer shim to dispatch real
  cancelable `pointerdown`/`mousedown` and `pointerup`/`mouseup` events with
  `button`/`buttons` and modifier state, then run the selection default action
  only when the down events are not canceled.
- Primary clicks now focus the editing host and collapse the caret at the hit
  text node; canceled `pointerdown`/`mousedown` keeps the old selection;
  Shift+click extends selection except for link/right-click cases that should
  collapse under the shim's no-`contextmenu` model.
- Primary drag extends selection before `pointerup`/`mouseup` listeners run,
  while middle/right drags preserve the collapsed down-position selection.
- Enabled the WPT mouse-button files:
  `selection/contenteditable/modifying-selection-with-primary-mouse-button`
  and both `modifying-selection-with-non-primary-mouse-button` variants. They
  now pass all `21/21` runnable assertions and move the full selection runner
  to 97 pass / 62 skip / 0 fail.

**SI probe — Backspace target-range matrix: MEASURED / NOT ENABLED (2026-06-16).**

- Temporarily allowed
  `input-events-get-target-ranges-backspace.tentative.html` to measure the
  next deletion frontier. It currently passes only `26/163` assertions, with
  root failures around block joins, empty inline cleanup, atomic element
  deletion, table-cell selections, and modifier/whitespace target-range
  semantics. The file stays skipped until those mutation/range gaps are fixed
  rather than broadening the allowlist to a known-red case.

**SI-6 — stale editing-surface cleanup after DOM replacement: LANDED (2026-06-16).**

- Fixed a debug-only `DocState` invariant crash in the enabled
  `fire-selectionchange-event-on-pressing-backspace` WPT: one promise test
  replaced an editing host's `innerHTML`, but `editing.active_surface` still
  pointed at the removed subtree, so the next rich edit failed preflight with
  "active editing surface no longer resolves."
- `view_state_prune_orphans()` now clears active editing/composition surface
  state when the stored owner/view is detached or no longer resolves to the
  same editing host. The fix is in the shared DOM mutation cleanup path, so it
  applies to `innerHTML` replacements and other subtree-removal paths rather
  than special-casing the WPT.

**SI-7 — number-input ArrowUp synthetic input slice: LANDED (2026-06-16).**

- Enabled the WPT number-input ArrowUp files:
  `input-events-arrow-key-on-number-input`,
  `input-events-arrow-key-on-number-input-prevent-default`, and
  `input-events-arrow-key-on-number-input-delete-document`. They now pass all
  `3/3` runnable assertions.
- Extended the WPT key shim so ArrowUp/ArrowDown on focused
  `<input type="number">` dispatch cancelable `beforeinput` with
  `inputType="insertReplacementText"`, apply the numeric step only when the
  event is not canceled and the control is still connected, then dispatch
  `input` and `change`.
- Fixed the iframe `srcdoc` property setter root cause exposed by the
  delete-document variant: setting `frame.srcdoc = ...` now reflects the
  `srcdoc` attribute and schedules the synthetic iframe load path, so the WPT
  frame document exists before the test sends the key.
- While closing the global JS gate, fixed an unrelated `lib_joi` fixture
  root cause: its local `URL` shim defined `href`/`hostname` but not `host`,
  while Joi's domain normalizer reads `new URL(...).host`.

**SI-8 — number-input spin-button pointer slice: LANDED (2026-06-16).**

- Enabled the WPT number-input spin-button pointer files:
  `input-events-spin-button-click-on-number-input`,
  `input-events-spin-button-click-on-number-input-prevent-default`, and
  `input-events-spin-button-click-on-number-input-delete-document`. They now
  pass all `3/3` runnable assertions.
- Extended the WPT `test_driver.Actions` shim so `pointerMove(..., {origin:
  "viewport"}).pointerDown().pointerUp()` over an `<input type="number">`
  dispatches pointer/mouse events, then runs the same cancelable
  `beforeinput` → numeric step → `input` → `change` default action used by the
  ArrowUp path. If `beforeinput` is canceled or removes the document, the
  later events are suppressed.
- Added `Actions.setContext(frame.contentWindow)` support for the iframe
  delete-document variant. The shim deliberately avoids relying on
  `typeof document.querySelectorAll === "function"` because Lambda's native DOM
  dispatcher exposes callable DOM methods through the property bridge even
  when `typeof` reports a non-function value.

**SI-9 — text-control drag-select pointer slice: LANDED (2026-06-16).**

- Enabled `input-events/select-event-drag-remove.html`; the drag-select case
  now passes its single assertion and no longer remains behind the Phase SI
  synthetic-input skip.
- Extended the WPT `test_driver.Actions` shim for left-button drags over
  `<input>` and `<textarea>` controls: pointer down focuses the control and
  anchors its native selection; pointer move maps the x coordinate to a
  text-control selection range, calls `setSelectionRange()`, and dispatches a
  `select` event. The target may be removed by that `select` handler, matching
  the WPT crash-regression shape.
- Relaxed the plain-event helper so it calls the native DOM bridge's
  `dispatchEvent` directly instead of depending on `typeof` reporting a
  JavaScript function.

**SI-10 — Backspace fallback event-envelope probe: LANDED (2026-06-17).**

- Tightened the WPT `Actions` keyboard shim so Backspace/Delete fallback
  mutation does not call the native editing hook a second time after the
  native path has already dispatched `beforeinput` but declined to mutate.
  This removes duplicate `beforeinput` events from the measured structural
  deletion frontier.
- When the JS fallback path performs a temporary DOM deletion after a native
  `beforeinput`, it now dispatches the matching post-mutation `input` event.
  The fallback previous-text search is also clamped to the active editing host
  so Backspace at the start of an editor cannot mutate unrelated document text.
- The broad Backspace target-range file remains skipped because the remaining
  failures are real engine gaps: block joins, empty inline cleanup, atomic
  element deletion, table-cell selections, whitespace target-range semantics,
  and modifier behavior. The probe improved from `26/163` to `44/163`.

**SI-11 — empty inline deletion cleanup probe: LANDED (2026-06-17).**

- Added a narrow post-delete cleanup to the native rich default transaction:
  if Backspace/Delete empties a text node whose parent is an ordinary inline
  formatting wrapper, the wrapper is removed via the existing live-range
  mutation envelope and the caret collapses to the wrapper's former parent
  boundary. Editing-host/contenteditable boundaries and non-text children are
  deliberately preserved.
- Mirrored the same cleanup in the WPT `Actions` keyboard fallback path for
  fallback DOM mutations. The broad skipped Backspace target-range probe now
  measures `45/163` instead of `44/163`: it clears the caret-after-inline
  wrapper innerHTML case, while caret-inside-inline target ranges and many
  structural deletion cases remain real blockers.
- The skipped Backspace file is still not enabled globally. Remaining failures
  continue to cluster around block joins, in-inline target range calculation,
  atomic element deletion, table-cell selections, whitespace semantics, and
  modifier behavior.

**SI-12 — simple Backspace block-join target range + mutation: LANDED
(2026-06-17).**

- Added the first native structural Backspace join for adjacent simple text
  blocks. In the intentionally narrow shape
  `<p>abc</p><p>[]def</p>`, Backspace now runs through the normal
  `beforeinput` transaction and mutates the host to `<p>abcdef</p>`, collapsing
  the caret to offset `3` in the surviving text node.
- Extended that join to the WPT collapsible-whitespace boundary shape:
  `<p>abc   </p><p>   def</p>` now joins to `<p>abcdef</p>` when the caret is
  anywhere in the second block's leading whitespace. The target range covers
  the trailing whitespace in the previous block and the leading whitespace in
  the current block.
- `beforeinput.getTargetRanges()` now reports the cross-block deletion range:
  for the plain join it starts at the end of the previous block's text node
  and ends at the start of the current block's text node; for the whitespace
  join it starts after the previous block's visible text and ends after the
  current block's leading whitespace. The post-mutation `input` event keeps
  the Input Events Level 2 contract and returns an empty target-range list.
- The native headless rich-edit surface is pinned to the editing host before
  the transaction runs. This keeps `input` dispatch stable after the originally
  focused block is removed by the join.
- Scope remains deliberately conservative: only adjacent `div`/`p`/`pre`
  blocks whose sole child is a text node are joined. Lists, tables, atomic
  content, whitespace-only block boundaries, nested inline wrappers, and
  modifier variants remain in the skipped Backspace matrix.

**SI-13 — simple inline Backspace block-join target range + mutation: LANDED
(2026-06-17).**

- Extended the SI-12 block join from direct text-only blocks to the next
  intentionally narrow WPT shape: adjacent `div`/`p`/`pre` blocks whose sole
  child is either a text node or a simple inline formatting wrapper containing
  one text node.
- The current block's inline wrapper is moved into the previous block rather
  than string-concatenated. This preserves wrapper identity/attributes and
  matches the WPT-accepted outputs:
  `<p>abc</p><p><b>[]def</b></p>` becomes
  `<p>abc<b>def</b></p>`, while
  `<p><b>abc</b></p><p><b>[]def</b></p>` becomes
  `<p><b>abc</b><b>def</b></p>`.
- `beforeinput.getTargetRanges()` mirrors the mutation path: it starts at the
  end of the previous block's last text leaf and ends at offset `0` in the
  current block's inline text leaf. The follow-up `input` event still returns
  an empty range list.
- Scope is still deliberately conservative. Arbitrary nested inline fragments,
  list/table joins, atomic nodes, modifier variants, and the rest of the broad
  skipped Backspace matrix remain future SI work.

**Current SI verification (2026-06-17):**

| Check | Result |
|---|---|
| `contenteditable-false-in-design-mode` | 2/2 passed |
| `input-events-delete-selection` | 6/6 passed |
| `input-events-get-target-ranges-during-and-after-dispatch.tentative` | 3/3 passed |
| `input-events-range-exceptions.tentative` | 4/4 passed |
| `fire-selectionchange-event-on-deleting-single-character-inside-inline-element` | 3/3 passed |
| `fire-selectionchange-event-on-pressing-backspace` | 2/2 passed |
| `fire-selectionchange-event-on-textcontrol-element-on-pressing-backspace` | 1/1 passed |
| `selection-direction-on-single-click` | 1/1 passed |
| `selection-direction-on-double-click.tentative` | 1/1 passed |
| `selection-direction-on-triple-click.tentative` | 1/1 passed |
| `modifying-selection-with-primary-mouse-button.tentative` | 7/7 passed |
| `modifying-selection-with-non-primary-mouse-button.tentative?middle` | 7/7 passed |
| `modifying-selection-with-non-primary-mouse-button.tentative?secondary` | 7/7 passed |
| `input-events-arrow-key-on-number-input` | 1/1 passed |
| `input-events-arrow-key-on-number-input-prevent-default` | 1/1 passed |
| `input-events-arrow-key-on-number-input-delete-document` | 1/1 passed |
| `input-events-spin-button-click-on-number-input` | 1/1 passed |
| `input-events-spin-button-click-on-number-input-prevent-default` | 1/1 passed |
| `input-events-spin-button-click-on-number-input-delete-document` | 1/1 passed |
| `select-event-drag-remove` | 1/1 passed |
| Direct simple block-join DOM regression | `ok=true`, `html=<p>abcdef</p>`, `before=deleteContentBackward:1:true,3,true,0`, `input=deleteContentBackward:0` |
| Direct whitespace block-join DOM regression | offsets 3/2/1/0 all join to `<p>abcdef</p>` with `before=deleteContentBackward:1:true,3,true,3` and `input=deleteContentBackward:0` |
| Direct inline block-join DOM regression | text/bold, bold/bold, and italic/bold WPT shapes all pass with matching HTML and `before=deleteContentBackward:1:true,3,true,0` |
| `input-events-get-target-ranges-backspace.tentative` | last measured 45/163 before SI-12/SI-13; remains skipped |
| `DomText_EmptyString_Backed` | passed |
| focused WPT test rebuilds | `test_wpt_contenteditable_gtest` rebuilt |
| `test_wpt_contenteditable_gtest` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_wpt_dom_events_gtest` | 96 cases: 43 pass / 53 skip / 0 fail |
| `test_js_gtest` | 199 passed / 0 failed; existing memtrack leak diagnostics printed |
| `make test262-baseline` | not rerun for SI-13; previous SI-9 result was regressions 0; 40261 / 40261 fully passing; retry 0.0s |

**Global gate note:** SI-13's local WPT guards and `test_js_gtest` are green.
The full `make test262-baseline` gate was not rerun for this block-join slice,
so the broader SI phase should still run the mandatory §1.1 gate before being
closed. The phase can keep advancing from the remaining skipped
deletion/pointer matrices without carrying a local WPT blocker.

**Next SI slice:** broaden synthetic input beyond the enabled Backspace/Delete,
number spin-key, number spin-button, and text-control drag-select pointer
subset: remaining `getTargetRanges` deletion matrices (in-inline target ranges,
deeper inline fragments, atomic deletion, list/table joins, whitespace
semantics, and modifier variants),
broader text-control delete coverage, and richer pointer drag/hit-test
injection outside the newly enabled text-control and contenteditable
mouse-button files.

---

## 6. Direction 1 — execCommand architecture

> **Design constraint (per directive): route through the existing eventstore.**
> execCommand must **not** open a parallel mutation path. Radiant already owns
> a canonical state store + editing-transaction pipeline; execCommand becomes
> one more *producer of editing intents* into it — indistinguishable from a
> keystroke, paste, drop, or IME commit. The transaction envelope (cancelable
> `beforeinput` → mutate → selection write → `input` → undo/history →
> state-machine validation → event-state log) is reused wholesale.

### 6.1 What already exists (reuse, do not reinvent)

The eventstore pipeline is in place and was, in part, *pre-wired for this*:

| Existing piece | Where | Reused for execCommand |
|---|---|---|
| `StateStore` / `DocState` (canonical state + selection authority `DocState.sel`) | `radiant/state_store.{hpp,cpp}` | command state/value reads; the one place selection lives |
| `editing_run_transaction(evcon, tx, …)` — the transaction runner | `radiant/editing_dispatch.{hpp,cpp}` | **the entry point execCommand calls** |
| `EditingIntent` / `InputIntentType` | `radiant/editing_intent.hpp` | the command → intent mapping |
| `editing_dispatch_beforeinput_ex` / `editing_dispatch_input` | `radiant/editing_dispatch.cpp` | cancelable `beforeinput`, then `input` |
| `state_store_set_selection(...)` — single selection writer | `radiant/state_store.hpp` | post-edit selection, no manual sync |
| `editing_rich_default_replace(...)` — rich text insert/delete mutation | `radiant/editing_rich_transaction.{hpp,cpp}` | the EC‑1 text mutations |
| undo recordability + history boundaries; `SM_EV_EDIT_*` state machine | `editing_dispatch.cpp`, `state_schema.hpp` | `undo`/`redo` and per-command transaction grouping |

Crucially, `InputIntentType` **already reserves the format intents**
(`INPUT_INTENT_FORMAT_BOLD/ITALIC/UNDERLINE/INDENT/OUTDENT`, `SELECT_ALL`,
`HISTORY_UNDO/REDO`) with the header note *"consumer-issued only … so consumers
can emit them through the same dispatcher."* **execCommand is that consumer.**

### 6.2 The execCommand flow (a thin producer over `editing_run_transaction`)

```
document.execCommand(name, showUI, value)            // new JS binding
        │   js_dom_execcommand.cpp
        ▼
  resolve EditingSurface         editing_surface_from_focus(DocState)
        ▼
  map name+value → EditingIntent (e.g. "bold" → INPUT_INTENT_FORMAT_BOLD;
        │                              "createLink" → INSERT_LINK, data=href)
        ▼
  editing_run_transaction(evcon, { surface, intent, hooks, mutate=<per-command> })
        │      ── reused envelope, identical to a keystroke ──
        ├─ snapshot selection from DocState.sel; compute getTargetRanges()
        ├─ editing_dispatch_beforeinput_ex(...)   // cancelable
        ├─ if not prevented → call the command's `mutate` callback (§6.3)
        ├─ state_store_set_selection(...)         // single selection writer
        ├─ editing_dispatch_input(...)
        └─ history record (if recordable) + SM_EV_EDIT_* validation + event log
        ▼
  return (mutated && !prevented)
```

So execCommand inherits cancelable `beforeinput`, correct `getTargetRanges()`,
selection reconciliation, undo eligibility, and deterministic event logging
**for free** — the only per-command code is the intent mapping and the
`mutate` callback.

### 6.3 The per-command mutations (the genuinely new work)

What does *not* exist yet are the rich default-action mutations themselves —
the DOM operations each command's `mutate` callback performs. `editing_rich_default_replace`
covers text insert/delete (EC‑1); the rest are new `editing_rich_*` mutators,
all operating through `dom_range` + the editing-host walk, GC-safe:

- **Inline format:** wrap/unwrap/toggle bold, italic, underline, strikethrough, sub/sup; colors; font name/size.
- **Block structure:** `formatBlock`, ordered/unordered lists, indent/outdent, justify.
- **Links / objects / cleanup:** createLink/unlink, insertImage, insertHorizontalRule, `insertHTML`, removeFormat.

Each is invoked **only** as the transaction's `mutate` callback — never called
directly from the JS binding — so the eventstore stays the single authority.

### 6.4 queryCommand* and designMode

- `queryCommandEnabled / State / Value / Indeterm / Supported` are **reads** off
  `DocState` + the canonical `EditingSelection` (e.g. `queryCommandState("bold")`
  walks the selection's inline context) — no mutation, no transaction.
- `designMode` ("on"/"off") promotes the whole `Document` to an editing host —
  replacing the original doc's "getter returns 'off', setter is a no-op" stub
  (Content_Editable.md §4.1).

### 6.5 Intent-enum extension

`InputIntentType` covers EC‑1 + the bold/italic/underline/indent/outdent
formats today. The broader command set needs new intent values
(strikethrough, sub/sup, justify\*, ordered/unordered list, formatBlock,
createLink/unlink, fore/back/hilite color, fontName/fontSize, removeFormat,
insertImage) — added to `editing_intent.hpp` as **consumer-issued** entries,
consistent with the existing pattern. `insertHorizontalRule`/`insertLink`/
`insertHTML`(via paste-shaped `html_data`) already exist.

### 6.6 Command set — tiered by corpus demand

Implemented in tiers, each = {intent mapping + `mutate` callback + queryCommand
hooks}, each unlocking a slice of the Chrome corpus (§7):

| Tier | Commands | Unlocks |
|---|---|---|
| **EC-1 Core text** | `insertText`, `insertParagraph`, `insertLineBreak`, `delete`, `forwardDelete`, `insertHTML` | `inserting/`, `deleting/` |
| **EC-2 Inline format** | `bold`, `italic`, `underline`, `strikethrough`, `subscript`, `superscript` | `style/` |
| **EC-3 Block structure** | `formatBlock`, `insert{Ordered,Unordered}List`, `indent`, `outdent`, `justify*` | `style/`, `execCommand/` |
| **EC-4 Links / objects** | `createLink`, `unlink`, `insertImage`, `insertHorizontalRule` | `execCommand/` |
| **EC-5 Color / font** | `foreColor`, `backColor`, `hiliteColor`, `fontName`, `fontSize` | `execCommand/` |
| **EC-6 Cleanup / history** | `removeFormat`, `selectAll`, `undo`, `redo`, clipboard `cut`/`copy`/`paste` (reuse `radiant/clipboard.cpp`) | `execCommand/`, `undo/`, `pasteboard/` |

`queryCommand*` and `designMode` land alongside EC-1 and grow per tier.

### 6.7 Progress

**EC-1a — native core-text execCommand bridge: LANDED (2026-06-17).**

- `document.execCommand(...)` now handles the EC-1 core text commands natively:
  `insertText`, `insertParagraph`, `insertLineBreak`, `delete`,
  `forwardDelete`, and `insertHTML`. These commands map to `InputIntent`s and
  run through `editing_run_transaction`, reusing the same cancelable
  `beforeinput` → rich default mutation → `input` envelope as synthetic key
  input.
- `queryCommandSupported(...)` reports the native EC-1 surface plus the
  clipboard commands already served by the WPT shim. `queryCommandEnabled(...)`
  enables core text commands for an active rich editing target, and preserves
  the HTML `designMode="on"` document-scope behavior required by WPT's
  `user-interaction-editing-designMode.html` case.
- Clipboard commands (`copy`, `cut`, `paste`) still delegate to the JS-side WPT
  helper so page clipboard event handlers continue to populate the synthetic
  clipboard store.
- `insertHTML` is currently paste-shaped: it carries `html_data` and the
  `text/html` MIME marker through the intent, but the EC-1 rich default
  mutation still inserts textual content. Full HTML fragment parsing/insertion
  remains part of the broader `insertHTML` mutation work.
- `queryCommandState`, `queryCommandValue`, and `queryCommandIndeterm` remain
  stubbed for later EC tiers.

**Current EC verification (2026-06-17):**

| Check | Result |
|---|---|
| Direct `execCommand("insertText")` DOM smoke | `supported=true`, `enabled=true`, `ok=true`, `innerHTML=aXb`, `beforeinput/input=1:insertText` |
| Focused designMode WPT regression | `user-interaction-editing-designMode`: 3/3 passed |
| `test_wpt_contenteditable_gtest` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest` | 196 passed / 0 failed; existing memtrack leak diagnostics printed |
| `make test262-baseline` | not rerun for EC-1a; previous SI-9 result was regressions 0; 40261 / 40261 fully passing; retry 0.0s |

**Global gate note:** EC-1a's local WPT guards and `test_js_gtest` are green.
The full `make test262-baseline` gate still needs to run before declaring the
whole EC-1 tier complete.

**EC-1b — rich transfer payload parity for execCommand/testdriver events:
LANDED (2026-06-17).**

- The DOM-backed JS dispatch path used by native `execCommand` and headless
  synthetic editing input now matches the main Radiant UI dispatcher for
  transfer-shaped rich editing intents. `insertFromPaste`,
  `insertFromPasteAsQuotation`, `insertFromDrop`, `deleteByDrag`, and
  `deleteByCut` expose a populated `InputEvent.dataTransfer` and keep
  `InputEvent.data` null for rich editing surfaces.
- This directly strengthens `execCommand("insertHTML", false, html)`, which
  maps to `insertFromPaste`: both `beforeinput` and `input` now carry
  `text/plain` and `text/html` entries through `DataTransfer`.
- The broad WPT `input-events-exec-command.html` remains skipped. It tests the
  whole legacy command surface (`insertOrderedList`, formatting, color/font,
  block justification, clipboard, links) and also expects no `beforeinput`,
  whereas this design intentionally routes execCommand through the cancelable
  `beforeinput` transaction envelope described in §2.3/§6.2.

**Current EC verification after EC-1b (2026-06-17):**

| Check | Result |
|---|---|
| Direct `execCommand("insertHTML")` DOM smoke | `ok=true`, `inputType=insertFromPaste`, `data=null`, `dataTransfer text/plain=<b>X</b>`, `dataTransfer text/html=<b>X</b>` |
| `make -C build/premake config=debug_native lambda -j10 ...` | passed; existing macOS-version linker warnings printed |

**EC-2a — selected-range bold/italic/underline wrapper: LANDED (2026-06-17).**

- `document.execCommand("bold"|"italic"|"underline", ...)` now maps to the
  existing consumer-issued `INPUT_INTENT_FORMAT_*` intents and runs through
  `editing_run_transaction`, sharing the same eventstore transaction log and
  state-machine envelope as EC-1. As designed in `editing_dispatch.cpp`,
  these format intents are non-`InputEvent` commands: they log the editing
  intent/transaction but do not synthesize `beforeinput {formatBold}` or
  `input`.
- Added the first native rich formatting mutator:
  `editing_rich_default_format(...)`. The initial scope is deliberately narrow:
  a non-collapsed rich DOM selection whose contents can be handled by
  `Range.surroundContents()` is wrapped in `<b>`, `<i>`, or `<u>`, and the
  canonical selection is restored to the wrapper's contents.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include the
  three inline format commands. `queryCommandState(...)` returns true for
  bold/italic/underline when the focus boundary sits inside the matching
  inline wrapper.
- Still out of scope for this slice: collapsed typing-state toggles,
  unwrap/toggle-off behavior, partial non-text node formatting, normalization
  of nested/sibling wrappers, `strong`/`em` alias state checks,
  strikethrough/subscript/superscript, and undo history for format commands.

**Current EC verification after EC-2a (2026-06-17):**

| Check | Result |
|---|---|
| Direct `execCommand("bold")` DOM smoke | `supported=true`, `enabled=true`, `ok=true`, `state=true`, `innerHTML=a<b>bc</b>d` |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 196 passed / 0 failed; existing memtrack leak diagnostics printed |
| `make test262-baseline` | not rerun for EC-2a; previous SI-9 result was regressions 0; 40261 / 40261 fully passing; retry 0.0s |

**Global gate note:** EC-2a's local WPT guards and `test_js_gtest` are green.
The full `make test262-baseline` gate still needs to run before declaring the
whole EC-2 tier complete.

**EC-2b — strikethrough/subscript/superscript wrapper + alias query state:
LANDED (2026-06-17).**

- Extended the EC-2 native inline format surface from bold/italic/underline to
  include `execCommand("strikethrough"|"subscript"|"superscript")`. These
  commands map to new consumer-issued format intents and reuse the same
  `editing_rich_default_format(...)` surround-contents mutator, producing
  `<s>`, `<sub>`, and `<sup>` wrappers for the current selected rich DOM range.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include the
  three added format commands, and `queryCommandState(...)` recognizes them
  when the focus boundary is inside the matching wrapper.
- Inline query state now also treats semantic aliases as active state:
  `bold` matches `<strong>`, `italic` matches `<em>`, and `strikethrough`
  matches `<strike>`.
- Scope remains the same as EC-2a: selected-range `Range.surroundContents()`
  shapes only. Collapsed typing-state toggles, unwrap/toggle-off behavior,
  partial non-text-node formatting, wrapper normalization, and undo history for
  format commands remain future EC-2 work.

**Current EC verification after EC-2b (2026-06-17):**

| Check | Result |
|---|---|
| Direct `execCommand("strikethrough"|"subscript"|"superscript")` DOM regression | all commands supported/enabled; selected `bc` wraps as `<s>bc</s>`, `<sub>bc</sub>`, `<sup>bc</sup>`; query state true |
| Inline alias query-state regression | `bold` in `<strong>`, `italic` in `<em>`, and `strikethrough` in `<strike>` all return true |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_inline_format' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 202 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-2b's local WPT guards and `test_js_gtest` are green.
The full `make test262-baseline` gate still needs to run before declaring the
whole EC-2 tier complete.

**EC-2c — whole-wrapper inline format toggle-off: LANDED (2026-06-17).**

- Repeated inline `execCommand(...)` now unwraps a fully selected matching
  wrapper instead of nesting a second wrapper. This covers direct selections
  of the wrapper contents and full selections of a wrapper's single text child.
- The matcher accepts the canonical and semantic alias forms used by query
  state: `bold` unwraps `<b>` / `<strong>`, `italic` unwraps `<i>` / `<em>`,
  `strikethrough` unwraps `<s>` / `<strike>`, and subscript/superscript unwrap
  `<sub>` / `<sup>`.
- The unwrap path preserves the selected contents after moving the children out
  of the wrapper and logs the transaction as `format-toggle`.
- Scope remains conservative: only whole-wrapper selections are toggled off.
  Collapsed typing-state toggles, partial non-text-node formatting, wrapper
  splitting/normalization, and undo history for format commands remain future
  EC-2 work.

**Current EC verification after EC-2c (2026-06-17):**

| Check | Result |
|---|---|
| Direct inline toggle-off DOM regression | `bold` over `<b>bc</b>` and `<strong>bc</strong>`, `italic` over `<em>bc</em>`, and `strikethrough` over `<strike>bc</strike>` all unwrap to `bc`; query state false |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_inline_format' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 202 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-2c's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-2 tier complete.

**EC-3a — single-block `formatBlock`: LANDED (2026-06-17).**

- Added `document.execCommand("formatBlock", false, value)` to the native EC
  command surface. The command maps to a new consumer-issued
  `INPUT_INTENT_FORMAT_BLOCK` intent and runs through the same
  `editing_run_transaction(...)` envelope as EC-1/EC-2.
- Added `editing_rich_default_format_block(...)` for the conservative first
  block-structure mutation: when the selection focus is inside one supported
  block, replace that block's tag while preserving its children and restoring
  the selected text range. Supported values are `p`, `div`, `h1`-`h6`,
  `blockquote`, and `pre`, accepting both bare names and angle-bracket input
  such as `<blockquote>`.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include
  `formatBlock`; `queryCommandValue("formatBlock")` returns the current
  supported block tag at the focus boundary.
- Scope remains intentionally narrow: no multi-block selection transform, no
  implicit wrapping of bare text under the editing host, no list conversion,
  no indent/outdent, no justify commands, and no undo history for block format
  commands yet.

**Current EC verification after EC-3a (2026-06-17):**

| Check | Result |
|---|---|
| Direct `formatBlock` DOM regression | `p -> h1` and `div -> blockquote` both preserve text and update `queryCommandValue`; unsupported `span` returns false without mutation |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_format_block' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 203 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-3a's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-3 tier complete.

**EC-3b — current-block justify commands: LANDED (2026-06-17).**

- Added native `execCommand("justifyLeft"|"justifyCenter"|"justifyRight"|
  "justifyFull")` support. The commands map to new consumer-issued justify
  intents and run through the same editing transaction envelope as the rest of
  EC-3.
- Added `editing_rich_default_justify(...)` for the conservative first justify
  mutation: find the supported block containing the selection focus and set its
  legacy HTML `align` attribute to `left`, `center`, `right`, or `justify`.
  Radiant's existing HTML resolver already maps this attribute into
  `text-align`, so the DOM mutation connects to layout without inventing a
  parallel style path.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include the
  four justify commands. `queryCommandState(...)` returns true when the current
  block's `align` attribute matches the queried command.
- Scope remains single-current-block only. Multi-block justification, implicit
  wrapping of bare host text, style-attribute normalization, list commands,
  indent/outdent, and undo history remain future EC-3 work.

**Current EC verification after EC-3b (2026-06-17):**

| Check | Result |
|---|---|
| Direct justify DOM regression | all four justify commands supported/enabled; `<p>` gains the expected `align` value; exactly one matching justify query state is true |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_justify' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 204 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-3b's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-3 tier complete.

**EC-3c — single-block ordered/unordered list insertion: LANDED (2026-06-17).**

- Added native `execCommand("insertOrderedList"|"insertUnorderedList")`
  support. The commands map to new non-dispatchable, non-recordable list
  intents and run through the same transaction envelope as the other EC-3
  block commands.
- Added `editing_rich_default_list(...)` for the conservative first list
  mutation: replace one focused supported block with
  `<ol><li>...</li></ol>` or `<ul><li>...</li></ul>`, moving the original
  block children into the list item and restoring the selected text range when
  possible.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include the
  two list commands. `queryCommandState(...)` returns true when the selection
  focus is inside a matching ancestor list.
- Scope remains intentionally narrow: no existing-list toggle, no adjacent
  list merge, no list split, no multi-block list conversion, no list-item
  indent/outdent, no undo history, and the raw DOM list nodes are not
  MarkEditor-backed for later attribute edits yet.

**Current EC verification after EC-3c (2026-06-17):**

| Check | Result |
|---|---|
| Direct list DOM regression | `insertOrderedList` converts `<p>abc</p>` to `<ol><li>abc</li></ol>` and reports ordered state true; `insertUnorderedList` converts it to `<ul><li>abc</li></ul>` and reports unordered state true |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_list' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 205 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-3c's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-3 tier complete.

**EC-3d — current-block indent/outdent: LANDED (2026-06-17).**

- Added native `execCommand("indent"|"outdent")` support. The commands map to
  the existing non-dispatchable, non-recordable indent/outdent intents and run
  through the same EC-3 transaction envelope.
- Added `editing_rich_default_indent(...)` for the conservative first
  indentation mutation. `indent` wraps the focused supported block in a
  `<blockquote>` and restores the current selection; `outdent` unwraps the
  nearest ancestor `<blockquote>`.
- `queryCommandSupported(...)` and `queryCommandEnabled(...)` now include both
  commands. `queryCommandState(...)` remains false for these command-style
  operations.
- Scope remains current-block only: no multi-block indentation, no list item
  nesting/outdenting, no margin/style-based indentation, no adjacent
  blockquote normalization, and no undo history for block structure commands
  yet.

**Current EC verification after EC-3d (2026-06-17):**

| Check | Result |
|---|---|
| Direct indent/outdent DOM regression | `indent` converts `<p>abc</p><p>def</p>` to `<blockquote><p>abc</p></blockquote><p>def</p>`; `outdent` unwraps it back to `<p>abc</p><p>def</p>` |
| `make -C build/premake config=debug_native lambda -j10` | passed; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_indent' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 206 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-3d's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-3 tier complete.

**EC-4a — selected-range createLink + nearest-anchor unlink: LANDED (2026-06-17).**

- Added native `execCommand("createLink"|"unlink")` support. `createLink`
  maps to the existing dispatchable and recordable `INPUT_INTENT_INSERT_LINK`
  path (`inputType="insertLink"`) and wraps a non-collapsed selected range in
  `<a href="...">...</a>`.
- Added `INPUT_INTENT_FORMAT_UNLINK` for the command-only unlink operation.
  It is non-dispatchable and non-recordable, and unwraps the nearest ancestor
  `<a>` at the selection focus.
- Link creation uses a native-backed `MarkBuilder::createElement("a")`
  element before setting `href`, so `dom_element_set_attribute(...)` updates
  the backing element and HTML serialization.
- Scope remains intentionally narrow: no collapsed-link insertion, no
  multi-range support, no partial non-text node shapes beyond the existing
  `Range.surroundContents(...)` constraints, no URL normalization or
  sanitization, no unlink of multiple anchors across a selection, no undo
  history for command-only unlink, and no `insertImage` or
  `insertHorizontalRule` yet.

**Current EC verification after EC-4a (2026-06-17):**

| Check | Result |
|---|---|
| Direct link DOM regression | `createLink` converts selected `bc` in `abcd` to `a<a href="https://example.test/">bc</a>d`; `unlink` unwraps `<a href="https://example.test/">bc</a>` back to `bc` |
| `make -C build/premake config=debug_native lambda -B -j10` | passed; forced rebuild cleared a stale `js_exec_profile.o` profile-define mismatch; existing warnings only |
| `test_js_gtest --gtest_filter='JavaScriptTests/JsFileTest.Run/dom_exec_command_link' --gtest_brief=1` | passed; existing memtrack leak diagnostics printed |
| `test_wpt_contenteditable_gtest --gtest_brief=1` | 194 cases: 163 pass / 31 skip / 0 fail |
| `test_wpt_selection_gtest --gtest_brief=1` | 159 cases: 97 pass / 62 skip / 0 fail |
| `test_js_gtest --gtest_brief=1` | 207 passed / 0 failed; existing memtrack leak diagnostics printed |

**Global gate note:** EC-4a's focused JS regression, full JS suite, and local
WPT guards are green. The full `make test262-baseline` gate still needs to run
before declaring the whole EC-4 tier complete.

---

## 7. Direction 2 — Import the Chrome `editing/` corpus

### 7.1 Source

Chromium `third_party/blink/web_tests/editing/` — subdirs `caret`, `deleting`, `editability`, `execCommand`, `input`, `inserting`, `pasteboard`, `selection`, `shadow`, `spelling`, `style`, `text-iterator`, `undo`, `unsupported-content` (~1000+ files across them). BSD-3-licensed.

### 7.2 The harness problem (why this is not "just copy the files")

These are **not** WPT testharness tests; they use Blink-vendor primitives that our `lambda.exe js --document` + `wpt_testharness_shim.js` path does not provide:

| Primitive | What it is | Our plan |
|---|---|---|
| `eventSender` | synthetic key/pointer input | **Phase SI** (§5) backs it |
| `testRunner` | dump controls (`dumpAsMarkup`, `dumpEditingCallbacks`, `notifyDone`) | shim subset in the editing runner |
| `assert_selection.js` | Blink's self-contained selection/markup assertion helper (parses `"foo^bar|baz"` carets, asserts resulting markup) | **import as-is** — mostly portable, no dump file needed |
| dump-as-markup (`-expected.txt`) | serialized DOM markup compared to a baseline | markup serializer + baseline comparison in the runner |
| `js-test.js` | older Blink assertion harness | small shim |
| render-tree / pixel dumps | layout/paint baselines | **defer** — not editing-model conformance |

Test flavours, in import-priority order: **(a)** `assert_selection.js`-based (most portable, modern) → **(b)** dump-as-markup → **(c)** `js-test.js` → **(d)** render-tree/pixel (deferred).

### 7.3 Where it lives — the `lambda-test/editing/` corpus + a symlink

Mirroring the existing **`test/js262 → ../lambda-test/js262`** pattern (the js262 runner reads the symlinked corpus and records the `lambda-test` commit for provenance):

- **Corpus:** new `lambda-test/editing/` in the sibling [`henry-luo/lambda-test`](https://github.com/henry-luo/lambda-test) repo, mirroring Blink's `editing/` subdir layout, plus the adapted shared helpers (`assert_selection.js`, `dump-as-markup.js`, `editing.js`) and a `MANIFEST` recording the source Chromium commit + per-file import/defer status + BSD license.
- **Symlink (in the `lambda` repo, pointing at the sibling corpus):** the link
  *lives at* `lambda/test/editing` and *points to* `../lambda-test/editing` —
  i.e. run from the `lambda` repo root: `ln -s ../lambda-test/editing test/editing`.
  Identical direction to the existing `test/js262 → ../lambda-test/js262`. The
  symlink itself is git-tracked in `lambda`; the corpus content is versioned in
  `lambda-test`. (Until `lambda-test/editing/` is populated this would be a
  dangling link, so it is created together with the first import — CET‑1.)
- **Runner:** new `lambda/test/test_chrome_editing_gtest.cpp` — **not** under the
  symlinked `test/editing/` (that path is corpus, not lambda code); it lives in
  the lambda test tree exactly like `test/test_js_test262_gtest.cpp`. Registered
  in `build_lambda_config.json` (`extended`), modelled on the WPT runners:
  per-file, parallel, recursive discovery over `test/editing/`, drives
  `lambda.exe` with the editing harness shim, compares `assert_selection`
  assertions or markup dumps vs `-expected.txt`, and records the `lambda-test`
  commit for provenance.

### 7.4 Phased import (gated on the matching execCommand tier + Phase SI)

| Phase | Import | Needs | Notes |
|---|---|---|---|
| **CET-1** | `caret/`, `selection/` | P0 + SI | caret navigation + selection; little/no execCommand |
| **CET-2** | `deleting/` | EC-1, SI | delete / forwardDelete |
| **CET-3** | `inserting/` | EC-1, SI | insertText / insertParagraph / insertHTML |
| **CET-4** | `execCommand/` | EC-2…EC-5 | the broad command corpus |
| **CET-5** | `style/` | EC-2, EC-3 | inline + block formatting |
| **CET-6** | `input/` | SI | `beforeinput`/`input` via real typing |
| **CET-7** | `undo/`, `editability/`, `spelling/`, `pasteboard/` | EC-6 | history, editability flags, clipboard |
| **defer** | `shadow/`, `text-iterator/`, `unsupported-content/` | — | Shadow-DOM workstream / internal / N/A |

Each CET phase: import the subdir into `lambda-test/editing/`, implement the commands it exercises, drive to a **tracked pass rate** in the status doc (§ below) — not a hard 100% gate initially (see Risk on quirks).

---

## 8. Consolidated phased plan & dependencies

```
P0  (clear WPT gtests) ──┬─────────────► EC-1 ─► EC-2 ─► EC-3 ─► EC-4 ─► EC-5 ─► EC-6
                         │                 │       │               │
   SI (synthetic input) ─┤                 ▼       ▼               ▼
   (unblocks input-events,│             CET-2   CET-5 / CET-3   CET-4 / CET-7
    selection pointer,    │
    Chrome interactive)   └─► CET-1 (caret/selection) ─► CET-6 (input)
```

| Phase | Scope | Depends on | Exit |
|---|---|---|---|
| **P0** | Fix 2 crashes + selection/IDL/focus/caret gaps | — | both WPT gtests green |
| **SI** | Headless key/pointer injection; `test_driver` + `eventSender` | P0 | `input-events` WPT unblocked; `eventSender` available |
| **EC-1…6** | execCommand engine + default-action mutations + queryCommand* + designMode, tiered | P0 (SI for interactive verification) | each tier's WPT `editing/run` + Chrome `execCommand` subset passes to a tracked rate |
| **CET-1…7** | Import Chrome `editing/` subdirs into `lambda-test/editing/` + runner | SI + matching EC tier | per-subdir tracked pass rate; runner wired into `make` |

All phase exits above also require the global JavaScript regression gate in
§1.1: `make test262-baseline` with `0` regressions / `0` retries, and
`./test/test_js_gtest.exe --gtest_brief=1` with `0` failures.

A new **`Radiant_ContentEditable_WPT_Status.md`** (planned in Content_Editable.md §11.5) carries the headline numbers for all three runners (selection, contenteditable, chrome-editing) plus the EC command-coverage matrix and the CET per-subdir gauge.

---

## 9. Risks & mitigations

| Risk | Mitigation |
|---|---|
| **execCommand is an enormous surface.** Blink's editing engine is tens of kLOC; full parity is a multi-quarter effort. | Phase strictly by command tier (EC-1…6); gate each tier on the corpus slice it unlocks; accept *incremental* pass rates rather than a big-bang 100%. |
| **Re-importing the quirk surface.** The original doc rejected execCommand precisely to avoid browser quirks. | Implement **spec-aligned** behaviour; treat the Chrome corpus as a **gauge** (tracked, not a hard CI gate) wherever it encodes Chrome-specific markup quirks; document divergences per-file in the MANIFEST. |
| **dump-as-markup brittleness.** Markup serialization must match Chrome's byte-for-byte. | Prefer `assert_selection.js` tests first (no dump file); normalize whitespace/attribute order in the markup comparator; support per-file rebaselining; keep dump tests in the gauge tier. |
| **Synthetic input is a prerequisite for most value.** | Sequence SI immediately after P0; it is the shared unlock for input-events, selection pointer, and the Chrome corpus. |
| **Corpus drift from upstream.** | MANIFEST pins the source Chromium commit; periodic re-sync script; imports are curated (not a blind mirror) so local adaptations are tracked. |
| **License / provenance.** | Blink tests are BSD-3; carry the license + per-file provenance in `lambda-test/editing/`; mirror the js262 commit-recording pattern. |
| **Undo/redo correctness** underpins many `execCommand`/`undo/` tests. | Build the transaction log with EC-1 (not bolted on later); every command is one undoable transaction from the start. |

---

## 10. Open questions

1. **execCommand: gate or gauge?** Recommend **gauge** initially (tracked pass rate, never fails CI), promoting individual commands/subdirs to must-pass as they stabilise — same tier discipline as Content_Editable.md §11.
2. **Markup-dump comparison policy** — exact-match vs normalized, and the rebaseline workflow when Radiant's serialization legitimately differs.
3. **Curated import vs re-syncable mirror.** "Phased import" implies curation; confirm we accept local edits to imported tests (tracked in MANIFEST) vs keeping them pristine for upstream re-sync.
4. **WebKit `editing/` too?** This proposal is Chrome-only (per the directive). WebKit's `LayoutTests/editing/` is a later option once the harness exists.
5. **How far does designMode go?** Whole-document editing pulls in document-level selection/focus edge cases; scope the first cut to what `editing/` actually exercises.

---

## 11. Acceptance criteria

- **Global per-phase gate:** every phase's change set must pass
  `make test262-baseline` with `0` regressions / `0` retries and
  `./test/test_js_gtest.exe --gtest_brief=1` with `0` failures before that
  phase is marked complete.
- **Phase 0:** `test_wpt_selection_gtest` and `test_wpt_contenteditable_gtest` both green (only documented capability skips remain); the two engine crashes root-caused and fixed.
- **Phase SI:** `input-events` WPT tests no longer auto-skipped; `eventSender` / `test_driver` injection works headless.
- **Direction 1:** `document.execCommand` + `queryCommand*` + `designMode` implemented; the EC-tiered command set passes its WPT `editing/run` + Chrome `execCommand` subset to a tracked, rising rate; every edit still flows through cancelable `beforeinput` → mutation → `input` (the §6 contract holds).
- **Direction 2:** `lambda-test/editing/` populated per CET phase with provenance + license; `test/editing → ../lambda-test/editing` symlink; `test_chrome_editing_gtest` runner wired into `make`; per-subdir pass rate tracked in the status doc.
- **Docs:** Content_Editable.md §9 carries a forward-note that execCommand is now in scope per this document.

---

## 12. Summary

This proposal takes Radiant's editing surface from "modern contract, no legacy" to "modern contract **plus** a batteries-included execCommand engine, measured against the industry's largest editing test corpus." It is gated on first making the existing selection + contenteditable WPT runners green (Phase 0), then building the one shared capability everything waits on (synthetic input, Phase SI), then growing execCommand command-by-command (EC tiers) while importing the matching Chrome `editing/` subdirs (CET phases) into the sibling `lambda-test/editing/` corpus. execCommand is added **without** discarding Content_Editable.md's architecture: it is the canonical built-in consumer that finally supplies the default-action mutation engine, still flowing through the cancelable `InputEvent` pipeline. The Chrome corpus is adopted as a tracked gauge, not a quirk-for-quirk mandate.
