# Radiant Rich Editor — Stage 4B: Script-Driven Editing Under Radiant (Plain-DOM JS) over a Common C++ Substrate

**Date:** 2026-06-30 · **Status:** Proposal.
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

`dom-bridge.ts`, `intent-from-input-event.ts`, `intent.ts`, and all of `src/model`/`commands`/`input`/`clipboard`/`drawing` are **unchanged**. Commit-layer simplest-first (Appendix A.5): full re-render + save/restore selection first, then keyed diffing only where perf demands (typing, drag). Keep React in **parallel** (a second demo entry + build) until the plain-DOM view is green, then make it default. **Tests:** the 1838 headless cases are unaffected; rewrite the 3 `.tsx` tests against the plain-DOM view; add reconciler unit tests. **Exit:** all 1841 green on the plain-DOM view.

### 3.2 Phase 2 — run the JS editor under Radiant

Radiant runs page JS via `script_runner` (retains JS state for interactive windows): `./lambda.exe view test/html/editor-dom.html` loads the single-file plain-DOM editor live on the LambdaJS DOM; headless event-script playback (`event_sim`) runs the fixture corpus without a window. **De-risk spike first** (gates everything):

1. **`contenteditable` → input event reaches *page-JS***: confirm a `beforeinput`/`keydown` reaches a page-JS listener that can `preventDefault` it (not only the native editor). *Fallback if not: drive input from `keydown` + the intent layer — localized to `intent-from-input-event.ts`.*
2. **`Selection` get/set fidelity** across text spans + atoms.
3. **Caret navigation ownership**: per §1.2, the substrate moves the caret on arrow/word/line; confirm the script can *read* the result via `selectionchange`.

### 3.3 Phase 3 — reduce `contenteditable` to a flag; route events to script

Apply §1.4: break the four substrate→editing edge-groups; make `contenteditable` classify-only; have `event.cpp` deliver input events to script handlers (JS `addEventListener` and Lambda `on` handlers) and **stop invoking the native apply path**. The dead engine is *bypassed* here and *deleted* in Phase 5 (after parity).

**Minimal DOM surface (decision):** *do not widen* the API the editor uses (`createElement`/`appendChild`/`insertBefore`/`removeChild`/`setAttribute`/`textContent`; `getAttribute`/`hasAttribute`/`classList`/`dataset`/`style`; `querySelector`/`parentNode`/`childNodes`; `addEventListener`/`preventDefault`; `getSelection`/`createRange`/`setStart`/`setEnd`/`removeAllRanges`/`addRange`). Prefer designing around a gap over adding DOM surface; close a genuinely-required gap in `js_dom*` minimally. LambdaJS gaps surface **silently** (`undefined`), so Phase 2/3 **assert** these, not assume. **Geometry caveat:** layout metrics read stale ([JS_13 issue 2](../../doc/dev/js/JS_13_Web_DOM.md#known-issues--future-improvements)) — hit-test drawings from the model and anchor popovers off the substrate's selection rects, not `getBoundingClientRect`.

---

## 4. The one new JS component — the keyed, selection-preserving reconciler

```
reconcile(root: Element, next: VNode, selection: Selection | null): void
```

Keys children by `data-source-path` (and `shape-id` for drawings) so reorder/insert/delete patches in place, keeping the native caret/focus intact; preserves the caret by **save native selection → patch → restore from `state.selection`** (via `dom-bridge.setDomSelectionFromSource`, which under §1 is how JS hands the caret to the substrate store); keeps each text leaf a single element with one text child. Owning it removes the bug class that forced the React `selectionFromDom` guard. This kernel mirrors the Lambda `render_map` reconciler (keyed by `path`/`shape-id`), tightening the JS↔Lambda correspondence.

---

## 5. Phase 4–5 — parity across three runtimes, then retire the dead C++ engine

### 5.1 Phase 4 — triple-runtime parity on the shared fixtures

The same fixture corpus runs in **three** runtime pipelines, all co-equal:

| Pipeline | View / commit | Runs on | Role |
|---|---|---|---|
| JS reference | plain DOM (was React) | browser (Vite single-file) | ground truth |
| **JS under Radiant** | plain DOM | Radiant via LambdaJS `js_dom*` | shipping (JS) |
| **Lambda under Radiant** | `view`/`edit` + `render_map` | Radiant native | shipping (Lambda) |

**Triage** each divergence to one cause: browser ✅ / Radiant-JS ❌ → **substrate/DOM gap** (fix in C++ `js_dom*`/event routing); Radiant-JS ✅ / Lambda ❌ (or vice-versa) → a **runtime-port gap** (fix in the lagging `src/` or `mod_*.ls`, keeping designs aligned); all differ from expected → a **legitimate editor difference** to record. **Retirement gate:** every behaviour the native C++ engine currently provides must have a green covering fixture under **both** Radiant runtimes before its native code is removed.

### 5.2 Phase 5 — retire the C++ editing-behavior subsystem

Once Phase 4's parity gate is green, **delete** the disabled editing-behavior layer (the **RETIRE** rows: `editing_rich_transaction.cpp` ~4k, the apply-paths of `editing_controller`/`editing_dispatch`/`editing_target_range`, `editing_intent`, the rich path of `text_edit`) and remove the `js_dom.cpp` coupling (§1.5), keeping Layer A intact. Steps: (1) sever `js_dom.cpp` → `editing_rich_transaction`; (2) delete dead files + build entries (`build_lambda_config.json` → `make`); (3) prune orphaned hooks from `event.cpp`/`state_machine.cpp`/`state_store.cpp`, leaving event generation, routing, and caret/selection intact; (4) re-run `make test-radiant-baseline` + the 4B fixture lanes. **Guard:** gated on parity, file-by-file; no native code deleted while it is the sole provider of a behaviour.

---

## 6. What stays, what moves, what is co-developed

- **Stays native C++ (Layer A, common):** the `contenteditable` flag, caret/selection + navigation, Selection/Range backing, caret-position geometry, selection/caret rendering, event generation **and routing to both bindings**, form controls, the testdriver. **Both runtimes depend on it.**
- **Moves to script (Layer B, per runtime):** the source model, steps, transactions, commands, intent, dom-bridge, and document-content history — implemented in **JS and Lambda in parallel**, aligned on design and gated by shared fixtures.
- **Retired:** the C++ editing-behavior subsystem (Layer B once done natively).
- **Unchanged in the JS reference:** `src/model`/`commands`/`input`/`clipboard`/`drawing`, the step algebra/transactions/history/schema, the fixture corpus (only 3 `.tsx` tests rewritten), `dom-bridge.ts`.

---

## 7. Phases & acceptance

| Phase | Deliverable | Exit gate |
|---|---|---|
| **0 — Division (both runtimes)** | §1 three-layer model; Layer-A keep list; the four edges to break; contenteditable-as-flag plan; `js_dom.cpp` coupling plan; JS/Lambda alignment map | Signed off; every native file classified; both runtimes' Layer-B boundaries identical |
| **1 — Plain-DOM JS view (browser)** | `render.ts` + `reconcile()` + vanilla controller; React in parallel | All 1841 tests green on the plain-DOM view |
| **2 — JS under Radiant + spike** | `editor-dom.html` runs via `./lambda.exe view`; §3.2 spike resolved | Basics work under Radiant; minimal DOM surface asserted |
| **3 — contenteditable → flag** | edges broken; events routed to script; native apply path bypassed | Both runtimes edit via script; no native edit fires |
| **4 — Triple-runtime parity** | corpus runs in browser-JS + Radiant-JS + Radiant-Lambda; divergence triage; native-behaviour coverage map | Every divergence triaged; **every native behaviour covered green under both Radiant runtimes** (retirement gate) |
| **5 — Retire C++ engine** | dead editing-behavior subsystem + couplings removed; Layer A kept | `make test-radiant-baseline` + 4B lanes green; no behaviour lost |

**Overall acceptance:** the same editor design, source-model handling, and oracle-verified behaviour runs in the browser and as **two co-equal Radiant editors** (JS and Lambda) over a **common C++ substrate**; all document editing is script-driven over a minimal, documented DOM surface; `contenteditable` is a routing flag; the native C++ editing-behavior subsystem is gone while caret/selection/rendering/event-routing/form-controls remain.

---

## 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| contenteditable input not routed to page-JS / Lambda handlers | medium | Phase 0/2 spike (§3.2); fallback to `keydown` + intent layer (localized) |
| Breaking substrate→editing edges destabilizes caret/selection | medium | Audit shows edges are few/narrow (§1.4); break in Phase 0 with the native engine still present as a safety net; baseline tests guard |
| Double-edit (native still applies an edit the script also applies) | medium | §3.3 disables the native apply path; Phase 4 verifies no native edit fires before Phase 5 deletes it |
| JS and Lambda editors drift in design | medium | Alignment rule (§1.3): model/step/command changes land in both, gated by shared fixtures |
| Reconciler regressions (caret/typing/Enter) | medium | Simplest-first (§3.1); React kept in parallel; reconciler unit tests |
| Silent LambdaJS DOM gaps (undefined-fallthrough) | medium | Phase 2/3 assert the minimal surface explicitly |
| Stale geometry breaks hit-test / popovers | low–medium | Hit-test from the model; anchor popovers off substrate selection rects (§3.3) |
| Premature retirement removes an uncovered behaviour | medium | Retirement gated on Phase-4 parity, file-by-file (§5.2) |

---

## 9. Summary

Stage 4B makes editing **script-driven** under Radiant: one **common C++ substrate** (the `contenteditable` flag, caret/selection, event generation and routing) underneath **two co-equal runtime editors** — the plain-DOM **JS** editor (4B's build target) and the **Lambda `.ls`** editor — which share one design and one fixture corpus and differ only in pipeline (LambdaJS DOM + `addEventListener` vs Lambda templates + `on` handlers + `render_map`). `contenteditable` is reduced to a routing flag and the C++ editing-behavior subsystem is disabled, then deleted behind a strict parity gate. For the JS side the only new component is a keyed selection-preserving reconciler (the Lambda side already has `render_map`). We define the division for both runtimes first, build and prove the JS view in the browser, run it under Radiant, flip contenteditable to a flag, reach triple-runtime parity on the shared fixtures, and only then remove the dead native engine.
