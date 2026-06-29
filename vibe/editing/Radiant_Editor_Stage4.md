# Radiant Rich Editor — Stage 4: Rich-Text Editing & Embedded-Drawing Integration (High-Level)

**Date:** 2026-06-25 · **Updated:** 2026-06-29
**Status:** Integration architecture. Rich-text editing is the deliverable; drawings appear as **embedded block embeds** whose *detailed* design and implementation now live in **[Stage 5: Drawing Editor](Radiant_Editor_Stage5.md)**. This document keeps only the **high-level integrated design** — how a drawing surface plugs into the flow-document editor — so Stage 4 stays focused on rich text.
**Builds on:** [Radiant_Rich_Text_Editing.md](Radiant_Rich_Text_Editing.md) (Stage 1),
[Radiant_Rich_Text_Editing2.md](Radiant_Rich_Text_Editing2.md) (Stage 2),
[Radiant_Rich_Text_Editor3.md](Radiant_Rich_Text_Editor3.md) (Stage 3),
[Reactive_UI.md](Reactive_UI.md) (reactive substrate).
**Leads to:** [Radiant_Editor_Stage5.md](Radiant_Editor_Stage5.md) — the full draw.io-class drawing editor (data model, tools, routing, snap, clipboard, render contract, module plan, tests, phases).

---

## 0. TL;DR

**Scope.** Promote the editor from "structured text only" to "structured text **with** structured drawings". Add a single new block type — `<drawing>` — that, when focused, switches the editor into a **canvas mode** with its own tool palette, hit-test, selection, and key bindings, while leaving the surrounding flow document under the existing ProseMirror-shaped text editor.

**The big four design choices (locked from preceding decisions):**

1. **Flow-doc with inline drawings** (Notion / OneNote / Affine "Page mode" embedding a drawing surface). The doc is primarily text; drawings are block embeds. Not an infinite canvas; not a Figma-style scene.
2. **Custom Mark records for shapes** — `<shape kind: 'rect' x: 100 y: 100 width: 200 height: 100 fill: "#fff">`, `<connector from-shape: "S1" to-shape: "S2" routing: 'orthogonal'>`. SVG is *render output*, never source-of-truth. This keeps the source format inspectable, format-able, schema-validatable, and lets us carry routing/snapping/anchoring metadata that SVG can't express natively.
3. **Reuse the existing transaction algebra.** No new step kinds for the storage layer. Every shape edit lowers to `SET_ATTR` + `REPLACE` (the steps `mod_step.ls` already ships). New commands (`cmd_move_shapes`, `cmd_route_connector`, `cmd_group`, …) are *gestures* that compile to existing steps. This keeps history, mapping, and forthcoming collab uniform across text and drawings.
4. **Two reference editors studied deep:** **Affine BlockSuite** for the embed-in-doc UX, surface-block model, and focus-driven mode switch; **maxGraph** (the maintained mxGraph / Draw.io fork) for connector routing, edge geometry, ports, and waypoints. Spot ideas pulled from tldraw (tool state machine) and Excalidraw (immutable-element store) where they fit; CodeMirror 6 and Lexical only at the periphery.

**The architectural shape:**

```
        ┌─── Flow doc (existing PM-shaped editor) ─────────────────┐
        │                                                          │
        │  <p>...</p>  <heading>...</heading>                      │
        │  ┌── <drawing> ─── (new in Stage 4) ─────────────────┐   │
        │  │  <layer>                                          │   │
        │  │    <shape kind: 'rect' …>                         │   │
        │  │    <shape kind: 'ellipse' …>                      │   │
        │  │    <connector from-shape: "..." to-shape: "...">  │   │
        │  │    <text-frame …> <p …> </text-frame>             │   │
        │  │  </layer>                                         │   │
        │  └────────────────────────────────────────────────────┘   │
        │  <p>The text continues.</p>                              │
        └──────────────────────────────────────────────────────────┘
```

**The mode model:**

- **Flow mode** (default): caret + selection inside text; drawings render as static blocks; clicking inside a drawing **focuses** it and transitions to canvas mode.
- **Canvas mode** (focused drawing): tool palette appears, key bindings change (V=select, R=rect, O=ellipse, L=line, C=connector, T=text, H=hand, Esc=back to select), text caret hides; clicking outside the drawing or pressing Esc twice returns to flow mode.

**What does *not* change.** `mod_doc.ls`, `mod_step.ls`, `mod_source_pos.ls`, `mod_transaction.ls`, `mod_history.ls` extend (new schema entries, new commands) but don't fork. The render pipeline is `view`/`edit` templates as already designed. The DOM bridge keeps its current shape; we add a *geometric* hit-test path alongside the existing tree hit-test.

---

## What lives here vs. Stage 5

Stage 4 is the **rich-text editor** plus the **contract** by which a drawing becomes a first-class block in that editor. It deliberately stops at the integration boundary.

| Concern | Where |
|---|---|
| Rich-text editing model, steps, transactions, history, commands | Stages 1-3 |
| The `<drawing>` block as an **atomic, focus-switching embed** in the flow doc | **Stage 4 (this doc)** |
| Flow ↔ canvas **mode model**, selection bridging, focus annotation | **Stage 4 (this doc)** |
| The "reuse the existing transaction algebra / zero new step kinds" principle | **Stage 4 (this doc)** — stated; mechanics in Stage 5 |
| Drawing **data model** (shape/connector/group/text-frame records), schema | [Stage 5](Radiant_Editor_Stage5.md) §3 |
| **Tools**, **hit-test**, **routing**, **snap**, **clipboard**, **render templates** | [Stage 5](Radiant_Editor_Stage5.md) §6-§11 |
| Drawing **module plan**, **test suite**, **phased implementation** | [Stage 5](Radiant_Editor_Stage5.md) §13-§15 |
| Lambda **port progress** of the drawing layer | [Stage 5](Radiant_Editor_Stage5.md) §0.5 |

---

## 1. Scope and Non-Goals (Stage 4)

### 1.1 In scope — the integration

| Area | Capability |
|---|---|
| Block kind | One new block type, `<drawing>`, recognised by the flow-doc schema as an **atomic, editable** block embed |
| Mode model | **Flow mode** (text caret) ↔ **canvas mode** (drawing focused), driven by focus; key bindings and selection swap with the mode |
| Selection bridging | `node` selection on a `<drawing>` from flow mode; a `multi-node` selection variant for in-canvas multi-select (defined here, consumed in Stage 5) |
| Algebra reuse | The rule that **every** drawing edit lowers to the existing `set_attr` / `replace` / `replace_around` steps — **no new step kinds** — so history, mapping, and collab stay uniform across text and drawings |
| Bridge | The DOM-dispatch path that routes a pointer event landing inside a drawing into a **geometric** hit-test pipeline instead of the text tree hit-test |
| Rendering target | The `view`/`edit` template + `render_map` pipeline already used for text also renders drawings (details in Stage 5 §8) |

### 1.2 Out of scope here (→ Stage 5)

The entire *drawing editor* — shape primitives, connectors and routing, tools, snapping, alignment, groups/layers/z-order, clipboard, the render templates, the module plan, the test suite, and the phased implementation — is specified in **[Stage 5](Radiant_Editor_Stage5.md)**. Stage 4 only fixes the seams that keep that editor a well-behaved citizen of the rich-text document.

---

## 2. Position, Selection, and the Mode Model

### 2.1 `SourcePos` inside drawings

`SourcePath` is unchanged — child indices from the doc root. A shape's path looks like `[2, 0, 3]` (third block in doc, first layer, fourth shape). For drawing-objects:

- `offset` is **always 0** on a shape, connector, or group (they have no editable inner content position).
- For a `text-frame`, inner positions follow the normal flow-doc semantics (`offset` = byte offset in text leaf or child index in a node).
- For a `label`, same as text-frame inline positions.

This means the existing `mod_source_pos.ls` (Stage 1-3) needs **no changes** for drawing positions — the path scheme handles it; only the *interpretation* differs.

### 2.2 New selection variant: `MultiNodeSelection`

```lambda
multi_node_selection(paths)    // { kind: 'multi-node', paths: [SourcePath, ...] }
```

For shift-click and lasso multi-select. Paths are stored in document order. All paths must share a common ancestor (the lowest is the bounding canvas / group). Mixed text-and-shape selections are not allowed — if you have a multi-shape selection and click in flow text, the selection is replaced.

Existing variants (`text`, `node`, `all`) all continue to work; `node` selection on a `<drawing>` block is what flow-mode uses (the whole drawing as one selectable unit, like an image today).

### 2.3 The mode model

Two modes, owned by the editor session, stored in `editor.mode`:

| Mode | Active when | Effects |
|---|---|---|
| `'flow` | No drawing focused | Text caret visible; `selection` is `text/node/all` over flow doc; key bindings = text editor's |
| `'canvas` | A drawing is focused (clicked into, or auto-focused on insert) | Text caret hidden; `selection` is `node`/`multi-node` over drawing-objects; key bindings = canvas tool palette; `editor.tool` is non-null |

Transitions:

```
flow ──(click inside <drawing>)──→ canvas   (auto-selects empty / the clicked shape)
canvas ──(click outside <drawing>)──→ flow
canvas ──(Esc twice)──→ flow
canvas ──(focus another drawing)──→ canvas (mode stays; selection moves)
```

Mode entry calls `mod_editor.set_mode('canvas', drawing_id)` which:
1. Activates the configured tool palette (default: select tool).
2. Hides the flow text caret.
3. Sets `editor.focused_drawing = drawing_id` (used by hit-test scoping).
4. Fires `'mode-change'` event to subscribers (toolbar UI re-renders).

Mode exit reverses; final selection inside the canvas is **retained** in the state store so re-entering the same drawing restores it (per Reactive_UI §5A.2's state-key model — `(drawing_id, "select_tool", "selection")`).

### 2.4 Focus & the `data-editable` annotation

Reactive_UI §7 already plans `data-editable: ~` on outputs of `edit` templates. We extend the convention: the `<drawing>` render outputs `data-drawing-focus: ~.id` on its outermost SVG node. The DOM dispatch hook in `event.cpp` walks up from a click target, and when it sees `data-drawing-focus`, it routes the event into the canvas mode pipeline (tool state machine) rather than the text pipeline.

---

## 3. Integration Extension Points (high-level)

Three seams connect the drawing surface to the rich-text editor. Each is stated here as a contract; Stage 5 implements behind it.

1. **Schema seam.** The flow-doc schema gains a `drawing` block entry (and the drawing-object roles). To the flow editor a `<drawing>` is just an atomic, selectable, editable block — the same shape as today's `image`. Stage 5 §3 defines the full sub-schema (layers, shapes, connectors, …).

2. **Step seam.** Drawing gestures never introduce new step kinds; they compile to `set_attr` / `replace` / `replace_around` (Stage 5 §5). Consequences the rich-text editor gets for free: one unified undo stack, position `Mapping` across shape edits, and a collab-ready (stable-id + `set_attr`) mutation model.

3. **Event/render seam.** The DOM bridge walks up from a click target; when it crosses a node carrying `data-drawing-focus`, it (a) enters canvas mode and (b) routes subsequent pointer events through a **geometric** hit-test rather than the text tree hit-test. Geometry-only attribute changes (`x`, `y`, `width`, `height`, `rotate`, `points`, `waypoints`, and style attrs) take a direct-DOM-patch fast path that skips relayout. The mechanics — `canvas_bridge.hpp`, `radiant/event.cpp` wiring, the fast path — are Stage 5 §8.3 / §13.3.

---

## 4. Summary

Stage 4 keeps the rich-text editor the centre of gravity and admits drawings through **one** new block type and a small, well-defined integration surface: an atomic block embed, a focus-driven flow/canvas mode switch, a selection model that bridges text and shapes, and the discipline that every shape edit lowers to the *existing* step algebra. Nothing about history, mapping, templates, or the DOM bridge forks. The full drawing editor — a draw.io-class diagram surface with shapes, connectors, routing, snapping, groups, clipboard, and its own tools and tests — is designed and built in **[Stage 5](Radiant_Editor_Stage5.md)**, slotting in behind the three seams above.

---

## Appendix A — Vanilla-DOM Editor (Future Third Rendering Target)

**Status:** deferred design note. **Decision (2026-06-27): keep React for the JS reference for now**, and implement a **vanilla-DOM** view *later* — when we want to run the JS editor live **under Radiant**. Until then, hold the React dependency **as lean as possible** (§A.3) so the future swap stays cheap. This appendix records the rationale and the pros/cons so the option is ready when the need arises.

### A.1 Why a third target

The JS reference (`test/editor-js/`) renders with **React** today. A third option — the *same* editor on a **vanilla-DOM** view — would let the JS implementation run **under Radiant** (via the LambdaJS DOM), giving an in-engine reference to validate against *before* the Lambda port is finished, and a way to cross-check Lambda vs JS in the same render environment. This mirrors **ProseMirror**, which is deliberately *not* React for exactly these reasons (tight contenteditable control, framework independence, embeddability); **Slate** chose React, and our reference currently follows Slate there (see [Stage 5](Radiant_Editor_Stage5.md) §2.1).

### A.2 Feasibility — LambdaJS already exposes a DOM

Present in-tree: `lambda/js/js_dom.{cpp,h}`, `js_dom_events.{cpp,h}`, `js_dom_selection.{cpp,h}` (design: `doc/dev/js/JS_13_Web_DOM.md`). A standards-only editor — `createElement` / `setAttribute` / `addEventListener` / `Selection` / `Range` — can run on it. **React effectively cannot** (it assumes a full browser runtime + scheduler). So vanilla DOM is the *enabler* for the Radiant host; React is a non-starter there.

> **De-risk before building.** Spike `js_dom` first to confirm it supports what the editor assumes under Radiant: contenteditable-style text input, `Selection` get/set (ideally `selection.modify` for caret nav), and a `beforeinput`-equivalent event. If those are present the path is clear; if contenteditable input isn't wired yet, that's a Radiant-side gap to scope separately.

### A.3 Keeping the React dependency lean (do this now)

The future migration is cheap **only if React never leaks past the view**. Discipline to hold today:

- **Core stays React-free.** `src/model`, `src/commands`, `src/drawing`, `src/input`, parser/serializer — no React types, hooks, or imports. (Today: 36 `.ts` files, all clean; only 3 `.tsx` in `src/`.)
- **Confine React to ~4 files** — `src/view/{render.tsx, EditorView.tsx, drawing/DrawingView.tsx, use-editor-state.ts}` — plus the `demo/` shell.
- **Keep `renderDoc` a (near-)pure `doc → view-structure` function** — separate *what* to render from *how* it commits, so only the commit layer changes later.
- **Use native DOM events** (`beforeinput`, `selectionchange`), not React synthetic events. *(Already done — chosen earlier to avoid React/jsdom quirks.)*
- **Keep source↔DOM mapping in `dom-bridge.ts`** — framework-agnostic; transfers as-is.
- **Keep editor state a plain reducer** (`use-editor-state.ts`) that React merely *drives*; a vanilla controller can drive the identical reducer.

Held to these, React is one thin, swappable shell over a portable core — and **1838 of 1841 tests are already headless** (only 3 `.tsx`), so the test suite is effectively framework-independent already.

### A.4 Vanilla DOM vs React — pros / cons

**Pros of vanilla DOM**

1. **Enables the Radiant target** — standards-only DOM runs on LambdaJS `js_dom*`; React can't host there. *(The decisive reason.)*
2. **Converges with the Lambda view** — Lambda renders via `view`/`edit` templates + `render_map` (a template + keyed reconciler, *not* a VDOM). A vanilla keyed reconciler mirrors that, keyed by `path` (text) / `shape-id` (drawings) just like `render_map` — tightening the JS↔Lambda correspondence.
3. **PM-style control of contenteditable + selection** — no reconciler clobbering the native caret (we already added a `selectionFromDom` guard to work around React doing exactly that).
4. **Smaller deps/build** — drop `react`/`react-dom` and the React Vite plugin; the single-file build simplifies.
5. **One view runs everywhere** — browser demo, headless tests, and Radiant.

**Cons of vanilla DOM**

1. **You inherit reconciliation** — keyed, focus-preserving incremental DOM patching is the hard part of an editor view (PM's `prosemirror-view` is large precisely for this). React does it for free today; `render.tsx` is simple *because of that*.
2. **Manual selection/focus restoration** on every edit (save native selection → patch → map → restore). *Mitigant:* the `dom-bridge.ts` source↔DOM mapping transfers.
3. **More verbose demo chrome** — toolbar, image-resize overlay, and Stage-4's tool palette are pleasant in JSX; vanilla needs a small `h()` helper or plain DOM.
4. **Migration + regression risk** on the demo (recall the earlier typing/Enter/resize bugs) — needs re-verification.
5. **Honest framing:** this simplifies the *dependency stack and portability*, **not** total complexity — it moves reconciliation from React into our own code. The win is control + reach, not fewer lines.

### A.5 The one hard part, and how to stage it

The whole migration reduces to **one new component: a keyed, selection-preserving reconciler.** Everything else (model, steps, commands, native events, selection mapping) is already in place. Recommended sequence when the time comes:

1. **Extract `renderDoc`** as a pure `doc → view-structure` function (it nearly is).
2. **Commit layer, simplest-first:** start with *full re-render + save/restore selection* (correct and easy, fine for a reference), then add keyed diffing only where perf demands it (typing, drag), keyed by `path` / `shape-id`.
3. **Replace the React shell** with a vanilla controller: native events → command → transaction → reconcile.
4. **Tests:** the 1838 headless cases are unaffected; rewrite the 3 `.tsx` component tests against the vanilla view.

Pay-off is double: a Radiant-hostable JS reference **and** a working prototype of the Lambda `render_map` reconciler.

### A.6 The three rendering targets

| Target | View layer | Runs on | Status |
|---|---|---|---|
| JS reference (browser demo) | **React** | browser (Vite single-file) | **current** |
| Lambda port | Lambda `view`/`edit` templates + `render_map` | Radiant (native) | **in progress** |
| JS-under-Radiant *(this appendix)* | **vanilla DOM** | Radiant via LambdaJS `js_dom*` | **deferred / future** |

All three share the same model, step algebra, commands, and oracle-verified behaviour — they differ **only** in the view/commit layer. Keeping React lean (§A.3) keeps the third target a small, well-scoped addition rather than a rewrite.
