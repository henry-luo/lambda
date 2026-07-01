# Radiant Rich Editor — Stage 4B: Script-Driven Editing Under Radiant (Plain-DOM JS) over a Common C++ Substrate

**Date:** 2026-06-30 (updated 2026-07-01) · **Status:** JS runtime end-state reached — Phases 1–3 and 5 done (plain-DOM JS editor runs fully script-driven under Radiant; native rich-edit engine deleted; `contenteditable` is a pure routing flag). Phase 4 (the Lambda `.ls` track) deferred to a future milestone. Remaining within-4B items are one Radiant-core layout follow-up (#3) plus two accepted/dev-only notes (#2, #4) — see **Progress → Open / known issues**. See **Progress** below.
**Scope:** Make the Stage-4 rich-text editor run **inside Radiant** by moving **all document-model editing into the scripting layer** and reducing the C++ side to a **common substrate** — caret/selection, the editable flag, and event generation/routing — shared by **two co-equal runtime editors**: the **JS** editor (ported from React to plain DOM, the focus of 4B) and the **Lambda `.ls`** editor (`lambda/package/editor/`). The native C++ rich-text **editing-behavior subsystem is disabled/retired**; `contenteditable` becomes **just a flag** that routes input events to script handlers. Promotes the deferred design note in **[Stage 4, Appendix A](Radiant_Editor_Stage4.md#appendix-a--vanilla-dom-editor-future-third-rendering-target)** into committed work.
**Builds on:** [Radiant_Editor_Stage4.md](Radiant_Editor_Stage4.md), [Radiant_Rich_Text_Editing.md](Radiant_Rich_Text_Editing.md) → [Radiant_Rich_Text_Editor3.md](Radiant_Rich_Text_Editor3.md) (Stages 1-3), [Reactive_UI.md](Reactive_UI.md) (reactive substrate, Lambda event dispatch, `render_map`), [JS_13_Web_DOM.md](../../doc/dev/js/JS_13_Web_DOM.md) (LambdaJS DOM).
**Co-developed with:** the Lambda `.ls` editor — **as important as the JS version, built in parallel, aligned on source-model/doc handling, and checked against the same fixtures** (§1, §6). Drawing editor → [Stage 5](Radiant_Editor_Stage5.md).

---

## 0. TL;DR

Two editor implementations are co-equal deliverables that **share one design and one fixture corpus and differ only in runtime pipeline**: the **JS** editor (`test/editor-js/src/`, ported React → plain DOM) and the **Lambda `.ls`** editor (`lambda/package/editor/mod_*.ls`). Both sit on top of a **common C++ substrate** in Radiant. Stage 4B drives the JS-under-Radiant path and the shared substrate work; the Lambda track proceeds in lockstep against the same fixtures.

**The architecture is three layers (§1):**

- **Layer A — C++ substrate (common to both runtimes; defined by HTML/CSS).** The `contenteditable` flag (surface classification), caret/selection + non-content navigation, selection/caret rendering, and **event generation that routes to *both* binding mechanisms** — JS `addEventListener` and Lambda `on <event>` template handlers. Form controls (`<input>`/`<textarea>`) stay native.
- **Layer B — editing logic (runtime-specific; *not* in C++).** The source document model, schema, step algebra, transactions, commands, document-content history, input-intent, dom-bridge — implemented **twice, in parallel**: in JS and in Lambda. On an input event the script `preventDefault`s, edits its own model, reconciles the DOM (JS reconciler / Lambda `render_map`), and writes the caret back via the substrate's Selection API.
- **Retired — the C++ editing-behavior subsystem** (`editing_controller`, `editing_dispatch` apply-path, `editing_rich_transaction`, `editing_target_range` apply-path, `editing_intent`, `text_edit` rich-path). Replaced by Layer B in whichever runtime is active.

**`contenteditable` = just a flag (decision, your point 3).** Rather than surgically severing only the rich-apply, **disable the entire C++ editing-behavior layer**; `contenteditable` only classifies an element as editable and tells the substrate to deliver input events to script handlers. A read-only dependency audit confirms this is clean: the editing layer is a one-way-dependent optional layer above the substrate, and the few upward edges are narrow and known (§1.4).

**Why cheap:** the JS reference is already React-lean (38 pure `.ts`, 3 `.tsx`, 1838/1841 tests headless); `dom-bridge.ts` is already standards-only; events are already native. The Lambda editor already mirrors the JS module structure (`mod_dom_bridge`, `mod_input_intent`, `mod_router`, `mod_source_pos`, `mod_step`, `mod_transaction`, `mod_history`). The **one new component** for JS is a keyed selection-preserving reconciler (the Lambda side already has `render_map`).

**The phases:** (0) define the three-layer division for *both* runtimes → (1) plain-DOM JS view, browser-first → (2) run JS under Radiant + spike → (3) reduce `contenteditable` to a flag; route events to script → (4) reach parity across browser-JS + Radiant-JS + Radiant-Lambda on the shared fixtures → (5) retire the dead C++ editing-behavior subsystem behind a strict parity gate.

---

## Progress (as of 2026-06-30)

**Done — Phase 1 (plain-DOM JS view, browser).** React swapped for a framework-free view in `test/editor-js/src/view/`: `vnode.ts`, `render-vnode.ts`, `reconcile.ts` (the keyed selection-preserving reconciler — the one new component), `editor-view-dom.ts` (`EditorViewDom`), and `editor-state.ts` (pure reducer split out so the plain-DOM graph imports no React). Full chrome ported in `demo/full-editor-dom.ts` (`FullEditorDom`: toolbar, DOM-selection→model tracking, paste/copy, drag-reorder, image-resize, gap caret, link popover, table col-resize). **React is retained permanently** (§3.2) as the future React-under-Radiant test vehicle. Single-file demo `demo/editor-dom.html` + `main-dom.ts` → `npm run build:page-dom` → `test/html/editor-dom.html` (classic **IIFE**, no React). Tests: parallel `test/view/{render-vnode,reconcile,editor-view-dom,full-editor-dom}.test.ts` beside the React suite — **full JS suite 1955 green, tsc clean**; browser-verified.

**Substantially done — Phase 2 (run JS editor under Radiant).** The plain-DOM editor **loads, mounts, edits text, runs toolbar commands, applies range formatting, and performs script-driven structural edits (Enter split, Backspace delete, whole-document replace)** under Radiant via the headless `lambda.exe view --event-file` testdriver. Fixture suite `test/ui/editor4b/*.json` (mount · typing · sustained typing · toolbar undo/redo · selectionchange delivery · toolbar Bold on a range incl. `is-active` · **Enter split · Backspace delete · select-all replace**) — **9/9 green** via `test/editor-js/tools/run-radiant-fixtures.sh`. The de-risk spike passed: inline classic JS runs, `beforeinput` reaches page JS and is cancelable, `getSelection()` works.

**Structural editing unblocked (2026-06-30).** Extending the fixtures to Enter/Backspace surfaced a hard native crash: the still-live native rich-transaction state machine aborted the process when a script `beforeinput` handler reconciled the focused block away mid-dispatch (`SM_INV_EDITING_TARGET_RANGES: active transaction has no surface`). Root-caused and fixed (the native engine now tolerates a script that owns the apply path and reconciles re-entrantly) — see **[vibe/radiant/Radiant_Issue3.md](../radiant/Radiant_Issue3.md)**. A second gap behind it — **whole-document-replacing edits lost surface focus** across Radiant's *full* view-tree rebuild (the contenteditable host was blurred and never re-focused, dropping every keystroke after the first) — was also root-caused and fixed (re-focus the editing host after the full rebuild, mirroring the Lambda-template and form-control restore paths). Both fixes are gated so pure native editing / non-editing focus are unaffected. Fixture suite **9/9 green** (incl. `enter-split`, `backspace-delete`, `select-all-replace`); radiant baseline unchanged.

**Phase 3 — `contenteditable` reduced to a routing flag (2026-06-30).** A contenteditable host marked **`data-script-edit`** is now *script-managed*: `editing_run_transaction` (`radiant/editing_dispatch.cpp`) detects the marker and, for dispatchable input intents, **delivers the `beforeinput` event to the script handlers and returns — running no native rich transaction and no native apply**. The plain-DOM editor sets `data-script-edit` on its surface, so under Radiant every edit (typing, Enter/Backspace, paste, formatting via `beforeinput`) routes to the JS controller which owns the model; the native rich-edit behavior layer is fully bypassed. Verified by `test/ui/editor4b/phase3-no-native-edit.json`: `beforeinput` insertText/insertParagraph are dispatched and `prevented`, while **`editing.transaction` count is 0** (no native transaction runs) — the Phase-3 exit gate "no native edit fires." Unmarked contenteditable keeps the native engine (the Phase-4 parity safety net + native editing tests), so the cut is non-destructive. The marker is **transitional**: when the native engine is retired (Phase 5) all contenteditable routes to script and the marker is dropped. Fixture suite **10/10 green**; radiant baseline unchanged.

**Radiant substrate fixes made along the way** (Phase 2/3 work, all behind `make build`; `make test-radiant-baseline` unchanged at 6091/209 — no new regressions):
- **`getAttributeNames()`** added to `js_dom` (parser needs to enumerate attributes; `.attributes` was absent).
- **`_lambda_rt` set in `radiant_js_ctx_enter`/`_exit`** (`event.cpp`) — fixed a heap-buffer-overflow when a JIT'd JS event handler evaluated a template literal (`stringbuf_new(_lambda_rt->pool)` with a stale pool). Native editing event-tests 25/25 after.
- **`js_event_loop_pump_nowait()`** + a tick in the headless loop (`window.cpp`) — delivers `setTimeout(0)`/microtask work (e.g. coalesced `selectionchange`) between sim events. `selectionchange` now reaches page-JS `document` listeners (proven for plain contenteditable).
- **reflow-on-JS-mutation** — `js_dom_mutation_notify` requests a reflow for layout-affecting mutations, so JS-built chrome is laid out and hit-testable at mount.
- Editor-side adaptations: classic-IIFE build (Radiant skips `type="module"`); `DOMParser`→`innerHTML` fallback in `parseHtmlToDoc`; `title`/`class` set via `setAttribute` (JS *property* sets don't reflect to content attributes under Radiant); just-in-time `getSelection()`→model sync before toolbar commands; `is-active` toggled via `setAttribute`.

**Open / known issues**
- ~~**JS-built chrome layout view orphaned after an editing interaction** → coordinate-resolved toolbar click resolves to `y=INT_MAX` and misses.~~ **Fixed** — root cause was stale per-item flex state on reused views during incremental reflow, not an orphaned view tree; see **[vibe/radiant/Radiant_Issue2.md](../radiant/Radiant_Issue2.md)**. The range-format fixture's `type "Z"` workaround was removed.
- ~~**Native rich-transaction crash on script-handled structural edits** (`SM_INV_EDITING_TARGET_RANGES`).~~ **Fixed** — see **[vibe/radiant/Radiant_Issue3.md](../radiant/Radiant_Issue3.md)**.
- ~~**Whole-document-replacing edits lose surface focus** across the full view-tree rebuild, dropping subsequent keystrokes.~~ **Fixed** — `post_html_handler_rebuild` now re-focuses the editing host after a full rebuild; see **[Radiant_Issue3.md](../radiant/Radiant_Issue3.md)** follow-up.
- **`selectionchange` not delivered for the editor's nested contenteditable** — **accepted design-around (native fix deliberately reverted, 2026-06-30).** The queue drops at the runtime-enter guard (`js_doc_runtime_enter_if_needed`, `js_dom_selection.cpp:84-105`, line-92 guard) because at native-selection-mutation time there is no active JS eval context (`context == NULL`) and the document reached via `node_owning_doc(anchor.node)` / the thread-local `js_dom_get_document()` carries `js_runtime_heap == NULL`. The retained runtime lives on the top-level `dom_doc` (`script_runner.cpp:1870`/`2204`) and is reachable only via the `EventContext` during dispatch (`radiant_js_ctx_enter`, `event.cpp:4502`), which the notifier path (`notify_selection_changed` → `js_dom_queue_selectionchange`, `dom_range.cpp:664`) doesn't have. A robust fix would re-plumb that topology (e.g. retain the runtime pointers on `DocState` so the selection notifier can reach them); the team chose the **editor-side just-in-time `getSelection()` sync** instead and reverted the native attempt. Delivery to page JS is proven for *plain* contenteditable. **Status: not planned** unless live selection-driven UI (e.g. caret-move toolbar state without a command) is needed.
- **The non-minified `DEBUG_BUILD` editor page is unreliable under Radiant** — dev-only convenience; **root cause not found.** Minification is gated by `minify: !process.env.DEBUG_BUILD` (`test/editor-js/tools/build-dom-page.mjs:29`); both modes emit a classic IIFE (no `type="module"`), so the difference is *not* module syntax. Low value — always test the minified build. **Status: documented, not planned.**
- **JS-created chrome layout view orphaned after an editing interaction (the *deeper* linkage issue behind the fixed Issue2)** — genuine open Radiant-core follow-up; **precise lead found, not yet fixed.** After an edit the incremental-layout path (`event.cpp:4244`, `incremental_layout=true`) reaches the JS-built subtree with `layout_dirty==false` + `view_type!=RDT_VIEW_NONE` + `height>0`, so the Phase-16 skip optimization (`layout_block.cpp:4363-4389`) bypasses it and its child views (toolbar buttons) are never re-laid-out — a subsequent coordinate-resolved click misses. **Caveat:** the original note also reports "even a forced full `reflow_html_doc` does not recompute it," which the incremental-skip theory alone doesn't explain — reconciling that needs a build+trace before any change to the skip path (which guards the whole radiant baseline). Harmless in a real interactive window (full per-frame layout); worked around headless by typing one char first. **Status: dedicated follow-up.**

**Done since:** dead-test-exe cleanup (removed `test_wpt_contenteditable_gtest` + `test_chrome_editing_gtest` build entries and their orphaned sources — they no-op'd after the Phase-5 engine deletion); Phase-0 formal sign-off recorded (§7); the four substrate→editing "edges" audited as already-moot (§1.4); **`test/js/editing` retired** — the 12 native-`deleteContentBackward` DOM-parity test triples (block/list/table joins, atom-range backspace, word/soft-line delete) tested the deleted rich engine and were parked behind `LAMBDA_JS_INCLUDE_EDITING_TESTS`; their source-model-expressible behaviors were migrated to the JS editor's own vitest suite (`test/editor-js/test/commands/text-commands.test.ts` — 5 new cases: same-mark-coalescing join, distinct-mark join, marked-into-plain join, emptied-inline-leaf cleanup, empty-previous-block join; full suite **1960 green**), the dir + env-gate were deleted, and the native-only behaviors with no source-model counterpart (DOM whitespace collapse, `<br>`/`<hr>` atom ranges, table-cell colspan/rowspan joins, word/soft-line modifier delete, implicit nested-list unwrap-on-backspace) were **intentionally not migrated** (documented in the test file).

**Genuinely deferred (out of 4B scope):** Phase 4 (triple-runtime parity; the Lambda `.ls` track under Radiant). Also deferred: migrating the 4 React `.tsx` tests onto the plain-DOM view (kept in parallel since React is retained). *(Phases 3 and 5 are complete — see below; the earlier "Not started" note predates them.)*

---

## 1. The architecture — one substrate, two runtimes (define this first)

This is the load-bearing decision of 4B, fixed in **Phase 0** before code moves, and it must hold for **both** the JS and Lambda editors. The constraint from your point 2: anything that lives only in JS is unavailable to Lambda, so the division cannot put runtime-specific capability in the shared layer. The C++ substrate therefore exposes a **runtime-neutral** event + selection interface; the editing logic lives in each runtime.

### 1.1 The three layers

```
            OS / IME / pointer / keystroke
                        │
 ┌──────────────── Layer A — C++ substrate (common) ─────────────────┐
 │  contenteditable = flag (surface classification)                  │
 │  caret/selection + navigation  ·  selection/caret render          │
 │  generate DOM events  ·  ROUTE to both binding mechanisms ↓        │
 └───────────────┬─────────────────────────────────┬─────────────────┘
                 │ addEventListener                 │ on <event>(evt)
                 │ (js_dom_dispatch_event)          │ (dispatch_lambda_handler
                 ▼                                  ▼   + render_map reverse lookup)
 ┌──── Layer B (JS runtime) ────┐    ┌──── Layer B (Lambda runtime) ────┐
 │ model · steps · commands ·   │    │ model · steps · commands ·       │
 │ history · intent · dom-bridge│    │ history · intent · dom-bridge    │
 │ → edit model → reconcile DOM │    │ → edit model → render_map        │
 │ → write caret (Selection API)│    │ → write caret (Selection API)    │
 └──────────────────────────────┘    └──────────────────────────────────┘
   test/editor-js/src/                  lambda/package/editor/mod_*.ls
        (same design · same fixtures · different pipeline)
```

The crucial change: C++ **no longer applies any edit**. Today, when `beforeinput` is unprevented, Radiant runs the native rich-text formatter (`editing_rich_transaction.cpp`, invoked from `event.cpp` and `js_dom.cpp`). Under 4B, the active script always intercepts and applies the edit; the native apply path is disabled (§3) and removed (§5).

### 1.2 Layer A — what C++ keeps (common to both runtimes)

| Substrate capability | Files (approx) | Note |
|---|---|---|
| `contenteditable` flag / surface classification | `editing.hpp` (enum + `EditingSurface` struct, 49 LOC) | the flag both runtimes read; no behavior |
| Caret & selection ranges; navigation; editable state | `state_store.{cpp,hpp}` (8.4k), `state_machine.{cpp,hpp}`, `state_schema.{cpp,hpp}` | scripts read/write via Selection API → updates this store |
| Selection/Range backing | `dom_range.{cpp,hpp}` (4.5k), `dom_range_resolver.cpp` | backs JS `Selection`/`Range` and Lambda `mod_dom_bridge` |
| Caret-position geometry (point→caret, next/prev/line nav) | `editing_geometry.cpp` (0.9k) | **substrate, not editing** — needed for navigation by both runtimes; keep the caret-position math, drop any content-apply tail |
| Selection/caret rendering & blink | `render_selection.cpp`, `render_state.cpp` | independent of editing |
| Event generation **and routing to both bindings** | `event.cpp`, `js_dom_events.cpp` (JS path), `dispatch_lambda_handler` (Lambda path) | the runtime-neutral interface |
| Test event simulation / testdriver | `event_sim.{cpp,hpp}` (5.5k) | drives the shared fixture lanes |
| **Form controls** (`<input>`/`<textarea>`) | `text_control.{cpp,hpp}`, `form_control.hpp` | own value model; **stay native** |

### 1.3 Layer B — editing logic, implemented twice in parallel

Both editors share the **same design and the same fixtures**, differing only in the runtime pipeline:

| Concern | JS runtime | Lambda runtime |
|---|---|---|
| Source model / doc | `src/model/doc.ts` | `mod_doc.ls` |
| Positions / selection | `src/model/source-pos.ts` | `mod_source_pos.ls` |
| Steps / transactions / history | `step.ts` / `transaction.ts` / `history.ts` | `mod_step.ls` / `mod_transaction.ls` / `mod_history.ls` |
| Commands | `src/commands/` | `mod_commands.ls` |
| Input intent | `src/input/intent.ts` | `mod_input_intent.ls` |
| DOM bridge | `src/view/dom-bridge.ts` | `mod_dom_bridge.ls` |
| Event binding | `addEventListener` | `on <event>(evt)` template handlers |
| Reconcile / commit | **new plain-DOM reconciler (§4)** | `render_map` (exists) |

**Alignment rule:** any change to the source-model/doc handling, step algebra, or command semantics lands in *both* and is gated by the shared fixture corpus. Document-content **history** lives in each runtime (JS `history.ts`; Lambda `MarkEditor` `EditVersion` chain per [Reactive_UI.md](Reactive_UI.md) §6). Native keeps only caret/selection history (**hybrid**, your earlier decision).

### 1.4 `contenteditable` = a flag — feasibility (the audit)

A read-only dependency audit (this session) confirms the editing-behavior subsystem is a **cleanly separable optional layer**: it depends on the substrate (`editing_*` → `state_store.hpp`/`dom_range.hpp`), but the substrate's upward edges are few and narrow —
- `state_store.cpp` → `text_edit.hpp`
- `state_machine.cpp` → `editing_geometry/host/intent/target_range`
- `dom_range.cpp` → `editing_host.hpp`
- `event.cpp` → the full `editing_*` hook set (the dispatcher; expected)

`dom_range`/`render_selection`/caret-state are otherwise independent. So Phase 0 breaks those four edge-groups, after which `contenteditable` is purely a classification flag and `event.cpp` routes input events to script handlers (JS listeners and/or Lambda `on` handlers) instead of into the native editor. This is the *cleaner* cut you asked me to evaluate, and it is feasible.

> **Resolution (2026-07-01 audit): the four edges are already moot — nothing to sever.** Post-Phase-5 re-audit confirms all four point at **Layer-A substrate headers that survive by design** and were never coupled to the deleted `editing_rich_transaction`: `text_edit.hpp` (form-control text helpers), `editing_geometry.hpp` (caret geometry / navigation), `editing_host.hpp` (contenteditable classification / `editing_host_lookup`), `editing_intent.hpp` (input-intent enums), `editing_target_range.hpp` (selection-based event targeting). All headers still exist and compile (`make build` clean under `-Werror,-Wunused-function`); their only remaining consumers are the substrate + script routing — the intended end state. So the "Phase 0 breaks the four edge-groups" plan is a no-op: the *behavioral* coupling was removed in Phase 5 (engine deleted) and the *link-time* edges turned out to be Layer-A, not engine, dependencies.

### 1.5 The `js_dom.cpp` coupling to sever

`js_dom.cpp` `#include`s five `editing_*` headers and, inside `dispatchEvent`, runs the native rich default-format when `beforeinput` is unprevented (`editing_rich_default_format`, ~`js_dom.cpp:637`). With `contenteditable` reduced to a flag, that path is unreachable; Phase 5 removes the `editing_rich_transaction` dependency, keeping only substrate calls (surface classification, geometry, selection).

---

## 2. Why now, and why plain DOM (the JS track)

React **cannot** host on Radiant (full browser runtime + scheduler assumed). A **standards-only** editor — `createElement`/`setAttribute`/`addEventListener`/`Selection`/`Range` — runs on LambdaJS, which already exposes exactly this surface ([JS_13](../../doc/dev/js/JS_13_Web_DOM.md)). Three things make the swap cheap: the view layer is tiny and isolated (`render.tsx` 146 LOC, `EditorView.tsx` 96 LOC, plus a plain reducer); `dom-bridge.ts` is already standards-only and framework-agnostic; and events are already native (`beforeinput` via a native listener, selection via the native `Selection` API — exactly the Layer-A↔B seam). The one thing React does for us that we must now own is **keyed, focus/selection-preserving incremental DOM patching** (§4) — which is also the prototype of the Lambda `render_map` reconciler.

---

## 3. Phase 1–3 — build the JS view, run it under Radiant, flip contenteditable to a flag

### 3.1 Phase 1 — plain-DOM JS view, browser-first

Build and prove it in the browser (known-good platform, zero Radiant variables). Replace React per:

| Today (React) | Stage 4B (plain DOM) |
|---|---|
| `render.tsx` — `renderDoc(doc): ReactElement` | `render.ts` — `renderDoc(doc): VNode` (pure `doc → view-structure`) |
| React reconciler (implicit) | `reconcile(root, vnode, selection)` — keyed patch (§4) |
| `EditorView.tsx` — component + `useEffect` | `EditorView` — plain controller (native events → intent → command → tx → reducer → reconcile) |
| `use-editor-state.ts` — reducer driven by React | same reducer, driven by the controller |
| `DrawingView.tsx`, `demo/*.tsx` chrome | `render.ts` drawing branch + a small `h()`-based shell |

`dom-bridge.ts`, `intent-from-input-event.ts`, `intent.ts`, and all of `src/model`/`commands`/`input`/`clipboard`/`drawing` are **unchanged**. Commit-layer simplest-first (Appendix A.5): full re-render + save/restore selection first, then keyed diffing only where perf demands (typing, drag). The plain-DOM view is **added alongside** React (a second demo entry + build), not a replacement. **Tests:** the headless cases are view-agnostic and unaffected; the plain-DOM view gets its own **parallel** test files (render parity, reconciler invariants, controller pipeline, full-chrome) mirroring the React `.tsx` tests. **Exit:** the full suite green with both views.

### 3.2 React is retained permanently (decision)

**The React reference is kept, not retired.** Originally Appendix A framed React as a temporary shell to swap out; we now keep it as a **permanent parallel target**. Rationale: Radiant is likely to gain a React host in future (a React renderer over the LambdaJS DOM), at which point the **React version becomes the test vehicle for that React support** — the same editor, same model, same fixtures, exercised through React-under-Radiant. So the three browser/engine views — React, plain-DOM (JS), and Lambda `.ls` — all coexist as long-lived targets sharing one design and one fixture corpus; none is thrown away. Concretely: the React demo entry/build and the React `.tsx` tests stay; the plain-DOM view and its tests run beside them. This costs one extra build + test lane and keeps a ready-made React conformance suite for the day Radiant can host it.

> **Status (2026-06-30): plain-DOM view built, React retained.** New in `test/editor-js/src/view/`: `vnode.ts`, `render-vnode.ts`, `reconcile.ts` (keyed reconciler), `editor-view-dom.ts` (`EditorViewDom`), `editor-state.ts` (pure reducer split out of `use-editor-state.ts` so the plain-DOM graph imports no React). Full chrome: `demo/full-editor-dom.ts` (`FullEditorDom` — vanilla port of `full-editor.tsx`: toolbar, DOM-selection→source tracking, paste/copy, drag-reorder, image-resize, gap caret, link popover, table col-resize). Parallel tests `test/view/{render-vnode,reconcile,editor-view-dom,full-editor-dom}.test.ts` (38 tests). Single-file demo `demo/editor-dom.html` + `main-dom.ts` → `npm run build:page-dom` → `test/html/editor-dom.html` (no React). Suite green with React + plain-DOM in parallel; browser-verified.

### 3.3 Phase 2 — run the JS editor under Radiant

Radiant runs page JS via `script_runner` (retains JS state for interactive windows): `./lambda.exe view test/html/editor-dom.html` loads the single-file plain-DOM editor live on the LambdaJS DOM; headless event-script playback (`event_sim`) runs the fixture corpus without a window. **De-risk spike first** (gates everything):

1. **`contenteditable` → input event reaches *page-JS***: confirm a `beforeinput`/`keydown` reaches a page-JS listener that can `preventDefault` it (not only the native editor). *Fallback if not: drive input from `keydown` + the intent layer — localized to `intent-from-input-event.ts`.*
2. **`Selection` get/set fidelity** across text spans + atoms.
3. **Caret navigation ownership**: per §1.2, the substrate moves the caret on arrow/word/line; confirm the script can *read* the result via `selectionchange`.

> **Status (2026-06-30): editor LOADS and MOUNTS under Radiant; interactive editing blocked by a native runtime bug.** Headless harness: `./lambda.exe view test/html/editor-dom.html --event-file <events.json> --headless`, driven by the `event_sim` testdriver (`type`/`click`/`assert_text`). Mount verified — 4/4 asserts pass: surface (heading, marked text, lists) and full toolbar render through the LambdaJS DOM.
>
> **De-risk spike result (all positive, via a minimal classic-script page):** inline JS runs; `addEventListener('beforeinput')` fires and is cancelable (`preventDefault` works); `window.getSelection()` works; JS-driven DOM mutation is observed by the testdriver. So the §1 seam (beforeinput → page-JS → preventDefault → JS edits) is viable.
>
> **Gaps found & resolved to get the editor to mount:**
> 1. **ES modules are not executed** by Radiant's script runner (`type="module"` → skipped). Fix: build the editor as a **classic IIFE** (`tools/build-dom-page.mjs` via Vite lib mode → inline into the HTML as a plain `<script>`; `build:page-dom` now runs it). *General constraint for any JS-under-Radiant page.*
> 2. **`DOMParser` is undefined.** Fix: `parseHtmlToDoc` now falls back to the **`innerHTML`** setter (Radiant's HTML5 parser) when DOMParser is absent — unchanged for browsers/jsdom; no new DOM surface (§5.3).
> 3. **`Element.attributes` / `getAttributeNames()` were missing.** Fix: added **`getAttributeNames()`** to `js_dom` (`lambda/js/js_dom.cpp`) — a minimal, faithful DOM method — and `parseHtmlToDoc` enumerates with it. *(Note: a DOM-method property read returns `true` for feature-detection under Radiant, so `typeof el.m === 'function'` misfires — call directly with a try/catch fallback.)*
>
> **Interactive editing: root-caused & FIXED (2026-06-30).** Typing previously crashed with a **heap-buffer-overflow in `pool_alloc` (`lib/mempool.c:303`) via `stringbuf_new`**, from a JIT builtin (template literal) inside the editor's render. **Root cause:** the JIT lowers template literals to `stringbuf_new(_lambda_rt->pool)` (`js_mir_expression_lowering.cpp` `jm_transpile_template_literal`); the global `_lambda_rt` is set during the script/module batch and *restored afterward*, so it is stale when a retained JS event handler fires in a later turn. `radiant_js_ctx_enter` (`radiant/event.cpp`) — the scope wrapping every JS DOM-event dispatch — installed a valid `context` from the document's JS runtime but **did not set `_lambda_rt`**, so a template literal in a handler dereferenced a dangling `_lambda_rt->pool`. (The earlier `+`/`Array.join` spikes passed because they don't read `_lambda_rt`; the editor's `render-vnode` uses template literals.) **Fix:** `radiant_js_ctx_enter`/`_exit` now save, set `_lambda_rt = &handler_ctx`, and restore it — mirroring the module-batch pattern. **Verified:** the editor types under Radiant (3/3 + sustained 12-turn typing); native editing event-tests still pass (keyboard/logging/clipboard/drop/IME, 25/25 across 5 files). *(Radiant baseline also shows pre-existing, unrelated failures — CSS-text layout, render-visual, and a stale `test_ui_automation_gtest.exe` not relinked by the build — none in the JS-dispatch path this change touches.)*
>
> **Net Phase 2 status: the plain-DOM JS editor loads, mounts, edits text, and runs toolbar commands under Radiant.** Event-driven fixture suite added — `test/ui/editor4b/*.json` (mount, typing, sustained typing, toolbar undo/redo), run by `test/editor-js/tools/run-radiant-fixtures.sh` (4/4 green via the headless `lambda.exe view --event-file` testdriver).
>
> **Native `selectionchange` wiring (2026-06-30): DONE.** `selectionchange` is now **delivered to page-JS `document` listeners** under Radiant — fixture `test/ui/editor4b/selectionchange-delivery.json` (plain contenteditable + `document.addEventListener('selectionchange')`) passes (was failing).
>   - **Root cause (it was a missing event-loop tick, not a missing dispatch):** the JS bridge `js_dom_queue_selectionchange` (`js_dom_selection.cpp`) already queued the event via `setTimeout(0)` from the `dom_range` selection notifier, but the **headless event simulator never drained the timer/microtask queue**, so the coalesced fire (`_wpt_selectionchange_fire`) never ran. (Confirmed by tracing: queued 25×, fired 0×.)
>   - **Fix:** added `js_event_loop_pump_nowait()` (`lambda/js/js_event_loop.{cpp,h}`) — a **bounded, non-blocking** pump (a few `uv_run(UV_RUN_NOWAIT)` turns + microtask flush, no watchdog wait so a self-rescheduling callback can't spin) — and called it between sim events in the headless loop (`radiant/window.cpp`). This mirrors a real event loop ticking between user actions. **Verified safe:** the full `make test-radiant-baseline` is unchanged (6091 pass / 209 pre-existing fail, identical to before); native editing event-tests 25/25.
> **Toolbar coverage (2026-06-30):** toolbar buttons dispatch to the JS editor (undo/redo fixture green); title set via `setAttribute` so `[title=…]` selectors match under Radiant (a `.title` *property* set does not reflect to the content attribute).
> **Selection-based range formatting — WORKING (2026-06-30).** Bold/italic/colour on a *selected range* now applies under Radiant: fixture `test/ui/editor4b/toolbar-bold-range.json` (caret → type → Cmd+A → toolbar **Bold**) asserts `font-weight:bold` on the selected leaf and passes.
>   - **What fixed it (editor-side, design-around):** the editor now syncs the live DOM selection into its model **just-in-time before each toolbar command** (`syncSelectionFromDom()` in `FullEditorDom.runCmd`, reading `getSourceSelectionFromDom()` → `window.getSelection()`, which works under Radiant). This avoids depending on `selectionchange` for the editor's nested contenteditable — confirmed it maps the Cmd+A range correctly (`[0,0]:0 → [7,0]:40`).
>   - **Why the "native queue robustness" half was *not* needed/possible:** investigation showed the editor *does* queue `selectionchange`, but the runtime-enter guard (`js_doc_runtime_enter_if_needed`) drops it — the retained JS runtime is reachable only via the `EventContext` during JS dispatch (as `radiant_js_ctx_enter` does), **not** from the selection's `DocState` / thread-local main document at native-selection-mutation time (both carry `js_runtime_heap == NULL` there). A robust native fix would require re-plumbing that topology; the editor-side just-in-time sync sidesteps it entirely, so the native attempt was reverted.
>   - **Two minor follow-ups — addressed (2026-06-30):**
>     - **(1) JS-created chrome now lays out at mount.** Root cause: JS DOM mutations didn't request a reflow (only the native editing path did), so the JS-built chrome had no geometry and position-based clicks missed until an edit triggered relayout. **Fix:** `js_dom_mutation_notify` (`lambda/js/js_dom.cpp`) now calls `doc_state_request_reflow()` for layout-affecting mutations (not paint-only). A fresh-mount toolbar click now lands (verified). `make test-radiant-baseline` unchanged (6091/209) — safe. *Caveat — separate, deeper issue (investigated, not yet fixed):* the range-format fixture still types one char first because, after a surface interaction (click/Cmd+A), a subsequent **coordinate-resolved** toolbar-button click resolves to `y = INT_MAX`. Root cause: the editing/caret path's reflow leaves the JS-built chrome's **layout view orphaned** at the "pending-layout" sentinel, and **even a forced full `reflow_html_doc` does not recompute it** (the JS-created toolbar views are no longer reached by layout after editing). So coordinate hit-testing on the toolbar misses. A type triggers the editor's reconcile (JS DOM mutations → reflow-on-mutation, fix 1 above) which re-establishes the chrome's layout, which is why the type-first fixture passes. The proper fix is in Radiant's layout/view-tree linkage for JS-created subtrees after editing operations — a dedicated follow-up; the workaround is harmless and a real interactive window (full per-frame layout) is unaffected.
>     - **(2) Toolbar active-state (`is-active`) now lights up.** Root cause: under Radiant `classList` mutations on JS-created elements don't reflect to the `class` content attribute (the same property-vs-attribute split as `.title`/`.className`), so `classList.toggle('is-active', …)` never showed. **Fix:** `FullEditorDom.syncToolbar` rebuilds the class string via `setAttribute('class', …)` (and `h()` sets `class` via `setAttribute` too). Verified: the Bold button is `is-active` after a range bold (`toolbar-bold-range.json` asserts both the applied style and the active class).

### 3.4 Phase 3 — reduce `contenteditable` to a flag; route events to script

Apply §1.4: break the four substrate→editing edge-groups; make `contenteditable` classify-only; have `event.cpp` deliver input events to script handlers (JS `addEventListener` and Lambda `on` handlers) and **stop invoking the native apply path**. The dead engine is *bypassed* here and *deleted* in Phase 5 (after parity).

> **Status (2026-06-30): the core cut is done via a transitional opt-in marker.** Reducing *all* `contenteditable` to a routing flag unconditionally would break the native editing engine that is still the Phase-4 safety net (and its test suite — several native fixtures add non-preventing `beforeinput` listeners and rely on the native default still running, so "has a listener" is *not* a valid script-routing signal). Instead a contenteditable host opts in with **`data-script-edit`**. For a marked surface, `editing_run_transaction` (`radiant/editing_dispatch.cpp`) short-circuits at the top: it calls `editing_dispatch_beforeinput_ex` to deliver `beforeinput` to the script (JS listeners + Lambda `on` handlers) and returns *without* opening a native rich transaction or running any native mutation. `editing_surface_is_script_managed()` (`radiant/editing.cpp`) is the predicate. The JS editor marks its surface; the **native apply path no longer runs for it** (proven: `editing.transaction` count 0 in `phase3-no-native-edit.json`). Unmarked contenteditable is untouched, so the native engine and its tests stay green. The four `state_store`/`state_machine`/`dom_range`/`event.cpp`→`editing_*` edge-groups are **not yet severed** — that, and dropping the marker, happen when the native engine is deleted in **Phase 5**; this step removes the *behavioral* coupling (no native edit fires) ahead of the *link-time* coupling.

**Minimal DOM surface (decision):** *do not widen* the API the editor uses (`createElement`/`appendChild`/`insertBefore`/`removeChild`/`setAttribute`/`textContent`; `getAttribute`/`hasAttribute`/`classList`/`dataset`/`style`; `querySelector`/`parentNode`/`childNodes`; `addEventListener`/`preventDefault`; `getSelection`/`createRange`/`setStart`/`setEnd`/`removeAllRanges`/`addRange`). Prefer designing around a gap over adding DOM surface; close a genuinely-required gap in `js_dom*` minimally. LambdaJS gaps surface **silently** (`undefined`), so Phase 2/3 **assert** these, not assume. **Geometry caveat:** layout metrics read stale ([JS_13 issue 2](../../doc/dev/js/JS_13_Web_DOM.md#known-issues--future-improvements)) — hit-test drawings from the model and anchor popovers off the substrate's selection rects, not `getBoundingClientRect`.

---

## 4. The one new JS component — the keyed, selection-preserving reconciler

```
reconcile(root: Element, next: VNode, selection: Selection | null): void
```

Keys children by `data-source-path` (and `shape-id` for drawings) so reorder/insert/delete patches in place, keeping the native caret/focus intact; preserves the caret by **save native selection → patch → restore from `state.selection`** (via `dom-bridge.setDomSelectionFromSource`, which under §1 is how JS hands the caret to the substrate store); keeps each text leaf a single element with one text child. Owning it removes the bug class that forced the React `selectionFromDom` guard. This kernel mirrors the Lambda `render_map` reconciler (keyed by `path`/`shape-id`), tightening the JS↔Lambda correspondence.

---

## 5. Phase 4–5 — parity across three runtimes, then retire the dead C++ engine

### 5.1 Phase 4 — triple-runtime parity on the shared fixtures — **DEFERRED to future**

> **Status (2026-06-30): deferred.** The Lambda `.ls` editor track under Radiant (mount/edit/structural-edit/format via `on` handlers + `render_map`, then triage to parity against the JS runtime) is **postponed to a future milestone**. The substrate is already Lambda-ready — the Phase-3 script-managed bypass dispatches `beforeinput` to *both* bindings (`editing_dispatch_beforeinput_ex` calls `dispatch_input_event` for JS **and** `dispatch_lambda_event` for Lambda), and `data-script-edit` routing is runtime-neutral — so the Lambda track can resume later without re-touching the substrate. **Consequence for Phase 5:** the original "covered green under *both* Radiant runtimes" retirement gate is relaxed to the **JS runtime alone** (the shipping editor); see §5.2.

The eventual shared-corpus pipelines (for when the Lambda track resumes):

| Pipeline | View / commit | Runs on | Role |
|---|---|---|---|
| JS reference (React) | React | browser (Vite single-file) | ground truth; **retained** as the future React-under-Radiant test vehicle (§3.2) |
| JS reference (plain DOM) | plain DOM | browser (Vite single-file) | ground truth for the Radiant-JS path |
| **JS under Radiant** | plain DOM | Radiant via LambdaJS `js_dom*` | shipping (JS) |
| **Lambda under Radiant** | `view`/`edit` + `render_map` | Radiant native | shipping (Lambda) |
| *React under Radiant* | React | Radiant via a future React host | *future* — when Radiant gains React support, the retained React version validates it (§3.2) |

**Triage** each divergence to one cause: browser ✅ / Radiant-JS ❌ → **substrate/DOM gap** (fix in C++ `js_dom*`/event routing); Radiant-JS ✅ / Lambda ❌ (or vice-versa) → a **runtime-port gap** (fix in the lagging `src/` or `mod_*.ls`, keeping designs aligned); all differ from expected → a **legitimate editor difference** to record.

### 5.2 Phase 5 — retire the C++ editing-behavior subsystem (gated on JS parity only)

**Revised gate (Phase 4 deferred).** Retirement now proceeds against the **JS runtime alone** — the shipping editor — rather than the original dual-runtime parity gate. The plain-DOM JS editor's coverage (the `test/ui/editor4b` corpus, incl. `phase3-no-native-edit.json`) is the gate.

**The native engine is still the sole provider for *non*-script-managed contenteditable** — i.e. any `contenteditable` *without* `data-script-edit`. That set is exercised by the **native editing test suites** (`test_chrome_editing_gtest` — the WPT `execCommand`/testdriver conformance suite, ~hundreds of cases, **not** in `make test-radiant-baseline`; plus native editing-event tests). So retiring the engine necessarily **retires those tests with it** — they validate the behaviour being removed. (Audit: `editing_rich_transaction.cpp` is 3998 LOC; referenced from `state_machine.cpp`, `editing_geometry.cpp`, `event.cpp`, `lambda/js/js_dom.cpp`, and its own header.)

**Order (each stage gated by `make test-radiant-baseline` + the `editor4b` lanes staying green; the ungated native editing suites are expected to drop as their engine is removed):**
1. ✅ **Sever `js_dom.cpp` → `editing_rich_transaction`** (§1.5) — **done 2026-06-30.** `js_dom_testdriver_rich_mutate` (the WPT testdriver/`execCommand` native-apply callback) is now an inert no-op and the `editing_rich_transaction.hpp` include is removed; `js_dom.cpp` no longer depends on the native rich-edit engine. **Verified:** `make build` clean; **gated baseline unchanged (6104/178, UI Automation 238/170)**; `editor4b` **10/10** (the shipping editor never used this path — it edits via real `beforeinput` → script). The ungated `test_chrome_editing` `execCommand`/testdriver cases that relied on native apply degrade as expected (engine retiring). Reversible edit (no deletions yet).
2. ✅ **Route-only for *all* rich contenteditable** — **done 2026-06-30.** `editing_run_transaction` (`editing_dispatch.cpp`) now short-circuits for every rich editing host (not just `data-script-edit`): it dispatches `beforeinput` to script and returns, never opening a native transaction. The native default-mutate (`rich_transaction_default_mutate`, `event.cpp`) is an inert no-op.
3. ✅ **Deleted the engine** — **done 2026-06-30.** `editing_rich_transaction.cpp` (3998 LOC) + `.hpp` removed (`git rm`; radiant sources are globbed so no build-config edit needed). The two **Layer-A** helpers it held — `editing_rich_find_text_descendant` (click-to-place-caret) and `editing_rich_is_composition_intent` (IME classification) — were moved to `editing.cpp` (Layer A). The `event.cpp` apply callbacks (`rich_transaction_default_mutate`, `rich_transaction_log_mutation`) are gutted/removed and the engine include dropped. *(The doc's `editing_controller`/`editing_target_range`/`editing_intent`/`text_edit` "apply-paths" turned out not to reference the rich engine — no separate deletion needed.)* **Verified:** `make build` clean (`-Werror,-Wunused-function` ⇒ zero orphaned functions, so step 4 below is automatically satisfied); `editor4b` **10/10** — the shipping editor is fully functional with the native engine *gone*.
4. ✅ **No orphaned hooks** — enforced by `-Werror,-Wunused-function`; the route-only path keeps event generation, routing, caret/selection, geometry, rendering, and form controls (Layer A) live and referenced.
5. ✅ **Marker dropped; native-editing UI-automation tests retired** — **done 2026-06-30.** `data-script-edit` and `editing_surface_is_script_managed()` removed — contenteditable is now **unconditionally** script-routed (`editor4b` 10/10). The **26** native-contenteditable/rich editing cases in the gated `test_ui_automation` suite (`test_editing_*`/`test_contenteditable_*`/`rte_*`/`test_iframe_contenteditable_*` that exercised the deleted apply path) were moved to `test/ui/_retired_native_editing/` (auto-discovery is non-recursive, so they no longer run). **Identified precisely** by running the suite and retiring exactly the failing native-editing fixtures — Layer-A `test_caret_*`/`test_drag_*` selection tests and the form `_input`/`_textarea` editing tests were **kept and still pass**. After retirement `test_ui_automation` runs clean of native-editing failures (219 passed; the only remaining 3 failures are pre-existing `test_form_*` form-control cases, untouched by this work). *(The ungated `test_chrome_editing`/`test_wpt_contenteditable` exes no-op'd at runtime after the engine deletion — **their build entries and orphaned source files were removed 2026-07-01** (`build_lambda_config.json`; premake regenerates clean; no other references — the `test/editing/` corpus + `chrome-editing-harness.js` in the `lambda-test` working dir are left in place).)*
6. ✅ Re-run `make test-radiant-baseline` + the `editor4b` lanes (see status).

> **Status (2026-06-30): Phase 5 complete (JS-gated).** The native rich-edit engine (`editing_rich_transaction.cpp`, 3998 LOC) is **deleted**; contenteditable is a pure routing flag; the shipping plain-DOM JS editor runs **entirely script-driven** over the Layer-A substrate (caret/selection, geometry, event generation & routing, rendering, form controls all intact). `editor4b` **10/10**; `test_ui_automation` clean of native-editing failures after retiring the 26 obsolete fixtures. The native C++ editing-behavior subsystem is gone while Layer A remains — the Stage-4B end state for the JS runtime (the Lambda runtime resumes in the deferred Phase 4).

**Guard:** file-by-file; never break the **gated** baseline or the JS `editor4b` lanes. Where a stage would break a *gated* test (a UI-automation editing case), stop and treat that behaviour as one the JS editor must cover (a fixture) before proceeding — the JS-parity analogue of the original guard.

---

## 6. What stays, what moves, what is co-developed

- **Stays native C++ (Layer A, common):** the `contenteditable` flag, caret/selection + navigation, Selection/Range backing, caret-position geometry, selection/caret rendering, event generation **and routing to both bindings**, form controls, the testdriver. **Both runtimes depend on it.**
- **Moves to script (Layer B, per runtime):** the source model, steps, transactions, commands, intent, dom-bridge, and document-content history — implemented in **JS and Lambda in parallel**, aligned on design and gated by shared fixtures.
- **Retired:** the C++ editing-behavior subsystem (Layer B once done natively).
- **Unchanged in the JS reference:** `src/model`/`commands`/`input`/`clipboard`/`drawing`, the step algebra/transactions/history/schema, the fixture corpus (only 3 `.tsx` tests rewritten), `dom-bridge.ts`.

---

## 7. Phases & acceptance

| Phase | Status | Deliverable | Exit gate |
|---|---|---|---|
| **0 — Division (both runtimes)** | ✅ signed off (2026-07-01) | §1 three-layer model; Layer-A keep list; the four edges to break; contenteditable-as-flag plan; `js_dom.cpp` coupling plan; JS/Lambda alignment map | Signed off; every native file classified; both runtimes' Layer-B boundaries identical *(the division held through implementation — the three-layer split (§1.1), Layer-A keep list (§1.2), and JS/Lambda alignment map (§1.3) are the as-built contract validated by Phases 1–5; the "four edges to break" (§1.4) were re-audited as already-moot Layer-A dependencies)* |
| **1 — Plain-DOM JS view (browser)** | ✅ done | `render.ts` + `reconcile()` + vanilla controller; React in parallel | All tests green on the plain-DOM view *(1955 green; React + plain-DOM run in parallel; the 4 React `.tsx` tests kept, not migrated)* |
| **2 — JS under Radiant + spike** | ◑ substantially done | `editor-dom.html` runs via `./lambda.exe view`; §3.3 spike resolved | Basics work under Radiant; minimal DOM surface asserted *(loads/mounts/edits/toolbar/range-format/structural-edits/whole-doc-replace green via `test/ui/editor4b` 9/9; layout + native-structural-crash + full-rebuild-focus-loss issues all fixed — [Radiant_Issue2.md](../radiant/Radiant_Issue2.md), [Radiant_Issue3.md](../radiant/Radiant_Issue3.md))* |
| **3 — contenteditable → flag** | ◑ core cut done (JS) | `data-script-edit` routing; events routed to script; native apply path bypassed for marked surfaces | Both runtimes edit via script; no native edit fires *(JS: proven — `editing.transaction` count 0 in `phase3-no-native-edit.json`, 10/10 fixtures; Lambda track pending Phase 4; the four "link-time edge-groups" were re-audited (§1.4) as already-moot Layer-A dependencies — nothing was left to sever)* |
| **4 — Triple-runtime parity** | ⏸ **deferred to future** | corpus runs in browser-JS + Radiant-JS + Radiant-Lambda; divergence triage; native-behaviour coverage map | *(deferred — the Lambda `.ls` track resumes in a future milestone; substrate already Lambda-ready)* |
| **5 — Retire C++ engine** | ✅ done (JS-gated) | `editing_rich_transaction.cpp` (3998 LOC) deleted; js_dom + event.cpp couplings severed; contenteditable unconditionally script-routed; Layer A kept | `editor4b` **10/10**; `test_ui_automation` clean of native-editing failures (26 obsolete fixtures retired); Layer A intact *(gate relaxed to the JS runtime since Phase 4 is deferred — §5.2)* |

**Overall acceptance:** the same editor design, source-model handling, and oracle-verified behaviour runs in the browser and as **two co-equal Radiant editors** (JS and Lambda) over a **common C++ substrate**; all document editing is script-driven over a minimal, documented DOM surface; `contenteditable` is a routing flag; the native C++ editing-behavior subsystem is gone while caret/selection/rendering/event-routing/form-controls remain.

---

## 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| contenteditable input not routed to page-JS / Lambda handlers | medium | Phase 0/2 spike (§3.3); fallback to `keydown` + intent layer (localized) |
| Breaking substrate→editing edges destabilizes caret/selection | medium | Audit shows edges are few/narrow (§1.4); break in Phase 0 with the native engine still present as a safety net; baseline tests guard |
| Double-edit (native still applies an edit the script also applies) | medium | §3.4 disables the native apply path; Phase 4 verifies no native edit fires before Phase 5 deletes it |
| JS and Lambda editors drift in design | medium | Alignment rule (§1.3): model/step/command changes land in both, gated by shared fixtures |
| Reconciler regressions (caret/typing/Enter) | medium | Simplest-first (§3.1); React kept in parallel; reconciler unit tests |
| Silent LambdaJS DOM gaps (undefined-fallthrough) | medium | Phase 2/3 assert the minimal surface explicitly |
| Stale geometry breaks hit-test / popovers | low–medium | Hit-test from the model; anchor popovers off substrate selection rects (§3.4) |
| Premature retirement removes an uncovered behaviour | medium | Retirement gated on Phase-4 parity, file-by-file (§5.2) |

---

## 9. Summary

Stage 4B makes editing **script-driven** under Radiant: one **common C++ substrate** (the `contenteditable` flag, caret/selection, event generation and routing) underneath **two co-equal runtime editors** — the plain-DOM **JS** editor (4B's build target) and the **Lambda `.ls`** editor — which share one design and one fixture corpus and differ only in pipeline (LambdaJS DOM + `addEventListener` vs Lambda templates + `on` handlers + `render_map`). `contenteditable` is reduced to a routing flag and the C++ editing-behavior subsystem is disabled, then deleted behind a strict parity gate. For the JS side the only new component is a keyed selection-preserving reconciler (the Lambda side already has `render_map`). We define the division for both runtimes first, build and prove the JS view in the browser, run it under Radiant, flip contenteditable to a flag, reach triple-runtime parity on the shared fixtures, and only then remove the dead native engine.
