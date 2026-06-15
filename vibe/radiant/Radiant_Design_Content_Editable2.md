# Radiant `contenteditable` 2 — execCommand, the Chrome editing corpus, and a green WPT baseline

**Date:** 2026-06-15
**Status:** Proposal
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

**Current baseline (2026-06-15):**

| Runner | Cases | Result | Δ from §3 start |
|---|---|---|---|
| `test_wpt_selection_gtest` | 151 | 87 pass / 46 skip / 18 fail | SIGSEGV eliminated (`initial-selection-on-focus` now a logical fail, not a crash) |
| `test_wpt_contenteditable_gtest` | 194 | 140 pass / 41 skip / 13 fail | +1 pass; SIGABRT eliminated (`insertparagraph-…` crashtest now passes) |

Regression guards green: `dom_range` 66, `source_pos_bridge` 22, `cmdedit` 82.

**Remaining for P0 green:** P0-B (selection ops), **P0-C (editing-host IDL — the
`editing-0` failures, completes CE-1)**, P0-D (focus model), P0-E (caret). Plus a
**harness decision**: `initial-selection-on-focus` (and other `?div`/`?span`
variant tests) need WPT **variant** support in the runner, or a documented
skip — its crash is fixed but it stays `0/100` headless until variants are
applied (it does `createElement(location.search.substr(1))` → `createElement("")`).

---

## 5. Phase SI — Synthetic input (shared infrastructure)

Both the WPT `input-events` tests **and** the Chrome `editing/` corpus need synthetic keyboard/pointer input that Lambda's headless `js` runtime does not yet deliver (the long-noted "Phase 8F" gap; today it auto-skips 23/25 `input-events` tests and the selection mouse-button tests).

This is a **shared dependency** of Directions 2 and 3, so it gets its own phase:

- **Headless input injection** in `lambda.exe js`: key down/press/up with `key`/`code`/modifiers, text/IME commit, and pointer down/move/up — delivered into the editing host's event pipeline so they dispatch real DOM events and drive selection/editing.
- **Two API frontings over the same injector:**
  - **WPT `test_driver`** (`Actions`, `send_keys`, `click`) — unblocks the WPT `input-events` + selection mouse-button tests.
  - **Blink/WebKit `eventSender`** + the relevant `testRunner` hooks — required by the Chrome `editing/` corpus (§7).
- **Unblocks:** 23 `input-events` WPT tests, the selection pointer tests, and essentially the entire *interactive* Chrome editing corpus (typing, caret navigation by key, selection by drag).

Sequencing SI early is high-leverage: it is the single capability that most of Directions 2 and 3 wait on.

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

- **Phase 0:** `test_wpt_selection_gtest` and `test_wpt_contenteditable_gtest` both green (only documented capability skips remain); the two engine crashes root-caused and fixed.
- **Phase SI:** `input-events` WPT tests no longer auto-skipped; `eventSender` / `test_driver` injection works headless.
- **Direction 1:** `document.execCommand` + `queryCommand*` + `designMode` implemented; the EC-tiered command set passes its WPT `editing/run` + Chrome `execCommand` subset to a tracked, rising rate; every edit still flows through cancelable `beforeinput` → mutation → `input` (the §6 contract holds).
- **Direction 2:** `lambda-test/editing/` populated per CET phase with provenance + license; `test/editing → ../lambda-test/editing` symlink; `test_chrome_editing_gtest` runner wired into `make`; per-subdir pass rate tracked in the status doc.
- **Docs:** Content_Editable.md §9 carries a forward-note that execCommand is now in scope per this document.

---

## 12. Summary

This proposal takes Radiant's editing surface from "modern contract, no legacy" to "modern contract **plus** a batteries-included execCommand engine, measured against the industry's largest editing test corpus." It is gated on first making the existing selection + contenteditable WPT runners green (Phase 0), then building the one shared capability everything waits on (synthetic input, Phase SI), then growing execCommand command-by-command (EC tiers) while importing the matching Chrome `editing/` subdirs (CET phases) into the sibling `lambda-test/editing/` corpus. execCommand is added **without** discarding Content_Editable.md's architecture: it is the canonical built-in consumer that finally supplies the default-action mutation engine, still flowing through the cancelable `InputEvent` pipeline. The Chrome corpus is adopted as a tracked gauge, not a quirk-for-quirk mandate.
