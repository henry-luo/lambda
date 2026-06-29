# Radiant Rich Editor — Stage 5: Drawing Editor (draw.io-class)

**Date:** 2026-06-29
**Status:** Detailed design + implementation of the embedded drawing surface introduced at a high level in **[Stage 4](Radiant_Editor_Stage4.md)**. Design **validated** in a JS reference implementation (`test/editor-js/`, drawing layer green across 392 fixtures); **Lambda pure-logic core ported & headless-tested** (§0.5) — tool machine, render templates, snap, clipboard, and live-Radiant integration remain.
**Builds on:** [Stage 4 — high-level drawing integration](Radiant_Editor_Stage4.md) (the `<drawing>` block embed, the flow/canvas mode model, selection bridging, and the "zero new step kinds" rule), [Reactive_UI.md](Reactive_UI.md).
**Goal:** a full **draw.io-class** diagram editor embedded in the flow document — shapes, connectors with real routing, ports, groups, layers, snap/align, clipboard, and (roadmap) stencils, edgeless canvas, and collab.

> **Section numbering.** The dense internal cross-references (§3.2, §5.1, §9.3, …) are preserved from the original combined draft, so the detailed sections keep their original numbers (§2, §3, §5–§17). The high-level integration overview — TL;DR, integration scope, and the **flow/canvas mode model** (Stage 4 §2) — lives in [Stage 4](Radiant_Editor_Stage4.md); references below to "§4" point to **Stage 4 §2**.

---

## 0. Context (recap — full overview in Stage 4)

Stage 4 establishes the integration: the document is primarily rich text; a drawing is **one** new `<drawing>` block — an atomic, focus-switching embed. Clicking into it enters **canvas mode** (tool palette, geometric hit-test, shape selection); clicking out / Esc returns to **flow mode**. Every shape edit lowers to the existing `set_attr`/`replace` steps, so history, position mapping, and collab are uniform across text and drawings. This document specifies everything *inside* that surface.

The architectural shape (from Stage 4):

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

---

## 0.5 Implementation Progress — 2026-06-27

**Strategy shift since this proposal was written.** Rather than implement the drawing layer directly in Lambda, we built a **parallel JS/TypeScript reference implementation** (`test/editor-js/`) **first** — to validate the design end-to-end and serve as the executable **oracle** for the eventual Lambda port. This de-risked the design (data model, step-lowering, tool machine, routing) before committing it to Lambda, and gave us a deep regression corpus.

### What's validated — JS reference (`test/editor-js/`), green

The Stage-5 drawing model is **fully prototyped and passing** in the reference. Every locked design decision in this doc now has a working implementation:

| Stage-5 design element | JS reference module / status |
|---|---|
| §3 Data model (`drawing`/`layer`/`shape`/`connector`/`group`/`text-frame`, stable ids) | `src/drawing/schema.ts` ✓ |
| §5 Step-lowering — shape edits → `set_attr`/`replace`, **zero new step kinds** | `src/drawing/commands.ts` ✓ — `insertShape`, `moveShapes`, `resizeShape`, `rotateShape`, `setShapeAttr`, `deleteShapes`, `bringToFront`, `sendToBack` |
| §6 Tool state machine | `src/drawing/tools.ts` ✓ |
| §7 Geometric hit-test + per-shape geometry | `src/drawing/hit-test.ts`, `src/drawing/geom.ts` ✓ |
| §9 Connector routing | `src/drawing/router.ts`, `src/drawing/shape-utils.ts` ✓ |
| §14 Test suite | **392 drawing fixtures** (`test/tier_0_drawing/`), part of **1841 passing** overall |

The **"zero new step kinds"** rule (§5.1) and an **invert-roundtrip invariant** (every recorded transaction inverts to restore the input) hold across all 392 drawing fixtures — empirically confirming the §5.2 history/collab payoff. The JS reference also covers the surrounding text editor, the Chromium-editing conformance harvest, and a divergence report (informational gauge).

### Lambda port status — the actual Stage-5 deliverable

The port to `lambda/package/editor/` proceeds **text-first**, each step verified against the JS oracle via a bridge (`test/editor-js/tools/export-lambda-oracle.ts` → `test/lambda/editor/oracle_poc.ls`):

| Layer | Lambda port status |
|---|---|
| Text model / steps / commands (Stages 1-3 foundation) | **done** — 15 module tests + **606/606** oracle cases match JS |
| Marks → flat value-carrying form (`[{name,value}]`) | **done** |
| List-split + empty-block convention aligned to JS | **done** |
| **Drawing layer — pure-logic core** — `mod_geom` (geom + shape-utils + hit-test), `mod_drawing_schema`, `mod_doc_schema`, `mod_router`, `mod_drawing_commands` (§13.1) | **done (2026-06-29)** — ported from `src/drawing/{geom,shape-utils,hit-test,schema,router,commands}.ts`; 4 module tests in `test/lambda/editing/` (`drawing_schema`, `drawing_geom`, `drawing_router`, `drawing_commands`) green; full lambda gtest 391/392 (the 1 failure, `editor_paste_basic`, is a pre-existing stale golden, not from this work) |
| **Drawing layer — tool machine + render/UI** — `mod_tools`, `mod_selection_handles`, `mod_drawing_templates`, `mod_snap`, `mod_clipboard_drawing` (§13.1) | **not yet ported** — next; depend on the core above + (templates/handles) the live engine |

#### Port notes / divergences from the JS reference

The pure-logic core was ported faithfully, with three deliberate, documented divergences forced by the Lambda/Mark data model (not workarounds — root-cause constraints):

1. **Stable ids are passed explicitly to shape constructors** (`make_rect_shape(id, geom, style)`, `make_connector(id, args)`) rather than allocated from a mutable module-level counter as in JS — Lambda is pure functional. The tool/caller owns deterministic id allocation.
2. **No `name` attribute on `layer`/`group`.** In the Mark data model `name` is the reserved element-tag accessor (`name(el)` / `el['name']` return the tag), so a `name` attribute cannot be represented or validated on a Mark element. It is omitted from the validated schema; the plain-map command layer may still carry an arbitrary `name` entry in `attrs`.
3. **`atomic` is decoupled from "no children."** The Lambda validator (`mod_edit_schema`) previously treated `atomic: true` as "must be a leaf." That diverges from the JS reference, where `atomic` is purely an editor-behavior flag and content is always validated by `matchContent`. The check is now scoped to *empty-content* atomic entries (`content: []`), so `<hr>`-style leaves still reject children (existing message preserved) while atomic containers like `<drawing>`/`<connector>` validate by content. The Stage-1 baseline (`editor/schema_validate`, `editor/schema_basic`) stays green.

Authoring reminders for Lambda-source Mark literals (used in the `test/lambda/editing/` fixtures): symbols are written with closing quotes (`'rect'`); attributes are comma-separated (`<shape x: 1, y: 2>`); hyphenated tag/attr names must be quoted (`<'text-frame' 'from-shape': "S1">`); a bare comparison as a `{}` block's final expression must be fully parenthesized; and `input(path, 'mark')` currently drops string-valued attributes, so fixtures are written as inline source literals, not loaded `.mark` files.

### Phase map (§15) against reality

Phases **4a-4g are prototyped in the JS reference** (data model, shape tools, routing, snap, groups/z-order, clipboard all exist and are tested there). The Lambda **pure-logic core** of phases 4a (data model + geometry), 4c (shape commands: insert/move/resize/rotate/delete/z-order), and 4d (routing: straight/orthogonal/curved, port anchors, waypoints) is now ported and headless-tested (see the status table). What remains for Stage 5 proper:

1. **Port the remaining drawing modules to Lambda** — the tool state machine (`mod_tools`), selection handles, render templates, snap, and clipboard — building on the green core, validated case-by-case by extending the oracle bridge to the 392 drawing fixtures (today the bridge covers the text commands; drawing commands are the next mappings to add).
2. **Live-Radiant integration** the headless JS reference can't cover: the C `canvas_bridge.hpp` (§13.3), geometric hit-test in `radiant/event.cpp`, and the direct-DOM-patch fast path for drag (§8.3 / phase 4h).

**Net:** the design in this doc is no longer speculative — it has a complete, green executable model, and the data-model + geometry + routing + command core is now live in Lambda. Stage 5 is a *porting + integration* effort against a fixed reference, not a from-scratch build.

---

## 1. Scope and Non-Goals

### 1.1 In scope (Stage 5 delivers)

| Area | Capability |
|---|---|
| Doc model | New `<drawing>` block kind; schema declares shapes, layers, connectors, groups, text-frames |
| Shape primitives | `rect`, `ellipse`, `line`, `polyline`, `polygon`, `path`, `freehand`, `image`, `text-frame` |
| Connectors | Straight, **orthogonal** (default), and curved routings; user-editable waypoints; endpoint anchoring by point / side / port |
| Layers & groups | Z-order, `<layer>` containers, `<group>` containers (a group selects/moves as a unit) |
| Tools | `select`, `pan`, `hand`, `rect`, `ellipse`, `line`, `polyline`, `path`, `freehand`, `connector`, `text`, `eraser` — pluggable |
| Selection | Single shape, multi-shape (lasso + shift-click), group selection, all |
| Manipulation | Move, resize (8 handles), rotate, scale by drag, keyboard nudge, alignment to grid/objects |
| Snap | Grid snap, object-edge / center / midpoint snap, alignment guides |
| Clipboard | Copy/cut/paste shapes; paste SVG fragment → schema-coerced shapes; paste image → image shape |
| Undo/redo | Unified with text history; coalescing of drag gestures into one entry |
| Mode switch | Click-into-drawing enters canvas mode; Esc / click-out leaves; text editor preserved underneath |
| Round-trip | Drawings serialize to / from Mark notation deterministically; SVG export works via existing render pipeline |

### 1.2 Non-goals (deferred or out of scope)

- **Infinite-canvas / edgeless / whiteboard mode** for the *whole* doc. the embedded surface is flow-doc-with-embedded-drawings; the whole-doc **edgeless canvas** is on the §18 roadmap, not the MVP.
- **Real-time multi-user collab on shape edits.** The local model is collab-ready by virtue of stable shape IDs and `SET_ATTR`-only mutation, but the sync engine is out of scope (Stage 3 §F deferral stands).
- **Shape libraries beyond geometric primitives** — UML, BPMN, ER, electronic symbols, flowchart stencils. Plugin surface is exposed (custom shape kind via schema + view template); the libraries themselves are a separate effort.
- **Pressure-sensitive stylus / pen.** Freehand uses pointer position only; pressure is left for later.
- **CRDT / OT for shape ops.** Same reason as above; deferred.
- **Animation / transitions on shapes.** Static rendering only.

---

## 2. Reference Matrix — What We Take From Each

| Source | What we adopt | What we deliberately reject | Why |
|---|---|---|---|
| **Affine BlockSuite** ([affine.pro](https://affine.pro), [github](https://github.com/toeverything/AFFiNE)) | (a) The **surface block** model — a single block containing the shape collection, addressed at the shape granularity. (b) **Mode switch driven by focus**: focus enters the surface = canvas mode; focus leaves = back to flow. (c) Shapes have stable IDs and are flat children of the surface (no deep nesting), enabling fast retrieval and clean drag-reorder. | Their CRDT / Yjs sync layer; their second "Edgeless mode" that turns the whole doc into infinite canvas (out of scope for the MVP; see §18). | Affine is the closest existing system to "Notion-style doc with drawing embeds". Their decision to make the surface block a flat shape collection (rather than a deeply nested SVG-like tree) is the right ergonomic choice for an editor. |
| **maxGraph** ([maxgraph.github.io](https://maxgraph.github.io), the maintained mxGraph / Draw.io fork) | (a) Connector / edge geometry: source-anchor + waypoints + target-anchor. (b) Edge styles — orthogonal, curved, segment, straight — as named routing strategies. (c) **Ports** — a shape can declare anchor points connectors snap to. (d) Edge labels (text attached to a routed segment). (e) Routing handles users can drag to adjust automatic routes. | The whole mxGraph runtime / View / Stylesheet object hierarchy — replaced by Lambda templates. The cell-encoding XML — replaced by Mark. | Routing is the *one* non-trivial algorithm in a diagram editor, and maxGraph has the field-proven set. We re-implement the routing strategies in pure Lambda over our own shape model, but adopt the algorithms and the user-facing waypoint UX wholesale. |
| **tldraw** ([tldraw.com](https://tldraw.com), [github](https://github.com/tldraw/tldraw)) | (a) The **tool state machine pattern** (`StateNode` tree) — a small set of states with clean transitions, each owning pointer handlers + cursor. (b) The **shape interface contract**: `getGeometry`, `getHandles`, `getOutline`, `component`, `indicator` — informs our `view`/`edit` template surface. | Their reactive store; their multi-user sync; their renderer (canvas). | The state machine is the cleanest way to model "which tool is active and what does it do with this pointer event". The shape contract is the right level of abstraction for plugin extension. |
| **Excalidraw** ([excalidraw.com](https://excalidraw.com), [github](https://github.com/excalidraw/excalidraw)) | (a) `versionNonce` — every shape carries a random nonce used for cheap "did this change" checks. Useful for our `render_map` invalidation. (b) Element-snapshot history (cheap, coarse) as a reference for *what not to do* — we keep PM step-based history for the unified algebra. | Their flat-array document model; their canvas renderer; their absence of layers. | Excalidraw is the minimal reference, useful to keep us honest about which "elaborate" features (layers, groups, connectors) we actually need. |
| **CodeMirror 6** ([codemirror.net/6](https://codemirror.net/6)) | The "single `State` → `Transaction` → `State`" loop with `effects` separate from `changes`. Reinforces `mod_transaction.ls`'s direction. | Its facet system; its EditorView. | Periphery; one design pattern. |
| **Lexical** ([github](https://github.com/facebook/lexical)) | Keyed reconciler — stable node keys mean we can re-render without diffing. Maps cleanly to **stable shape IDs** in our model. | Its EditorState; its node class hierarchy. | Periphery; one design pattern (already noted in Editor3.md). |

### 2.1 Architectural comparison — where our model sits

How the reference editors differ structurally, and where **our `editor-js`** (the JS reference being ported to Lambda) lands. Our editor deliberately takes **Slate's tree + path model**, **ProseMirror's step/transaction algebra**, and **BlockSuite/Affine's embedded-drawing-surface UX** — while rejecting Editor.js's flat-block model (no structured inline) and BlockSuite's CRDT-first storage.

| Aspect                | **Our `editor-js`**                                                                       | Slate                                             | ProseMirror                                        | Editor.js (lib)                                | BlockSuite / Affine                                         |
| --------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------- | -------------------------------------------------- | ---------------------------------------------- | ----------------------------------------------------------- |
| **Doc model**         | structured tree — `node`/`text` leaves, flat mark dict                                    | structured tree (elements + text, marks as props) | schema-constrained tree                            | flat block list (JSON), per-block plugins      | block tree on a **Yjs CRDT** (flavoured blocks, stable IDs) |
| **Inline / marks**    | first-class flat mark dict on text leaves                                                 | first-class (marks as leaf props)                 | first-class (marks in schema)                      | **none** — block-internal contenteditable HTML | first-class (Yjs text + attributes)                         |
| **Positions**         | **Slate-style** `{path, offset}`                                                          | path + offset (`Point`/`Path`)                    | integer positions + `ResolvedPos`                  | none global; per-block selection               | block-ID + offset (own selection manager)                   |
| **Edit algebra**      | **PM-style** 7 invertible steps (`apply`/`invert`/`map`)                                  | Operations (`split_node`, `set_node`, …)          | Steps / Transactions (OT-ready, mappable)          | none (block CRUD)                              | CRDT (Yjs) mutations — no step algebra                      |
| **History**           | transaction-based, invertible, coalescing                                                 | operation-based undo stack                        | step-based, rebaseable; collab module              | snapshot plugin (coarse)                       | Yjs `UndoManager` (CRDT undo)                               |
| **View layer**        | **React** (demo) → **Lambda `view`/`edit` templates** (port)                              | React (`slate-react`)                             | own contenteditable view; React via libs           | vanilla DOM block tools (framework-agnostic)   | **Lit** web components                                      |
| **Drawings / canvas** | custom **Mark records** (`shape`/`connector`/…); shape edits lower to existing steps (§5) | none (text-only)                                  | none in core (NodeViews can embed; no shape model) | none (a block plugin could embed)              | **first-class** — `surface` block + edgeless canvas mode    |
| **Collab posture**    | stable-id + `set_attr`-only mutation → CRDT-ready (Stage 5)                               | manual                                            | OT via steps                                       | none                                           | **native** (Yjs)                                            |

**Reading the table for Stage 5:** our drawing layer is unique in lowering every shape gesture to the *same* step algebra as text (§5) — Slate/PM have no shape model, Editor.js has no algebra, and BlockSuite gets drawings but pays for them with a CRDT-coupled store and a separate edit path. We get BlockSuite's embedded-surface ergonomics on PM/Slate's unified, invertible, mappable substrate. (Naming note: our `editor-js` is the *JS reference editor*, **not** based on the Editor.js library — the row above shows they are opposite designs.)

---

## 3. Data Model — `<drawing>` and Shape Records

### 3.1 Top-level schema additions

Add to the doc schema (extends `mod_md_schema.ls` → new `mod_doc_schema.ls`):

```lambda
pub doc_schema = md_schema ++ {
  // ───────── drawing block (one inline embed in the flow doc) ─────────
  drawing: {
    role:    'block',
    content: [{tag: 'layer', qty: 'plus'}],
    marks:   'none',
    atomic:  true,                  // the surface is one selectable unit from flow-mode
    editable: true,                 // canvas editing happens *inside* it
    attrs: [
      {name: 'id',       required: true,  type: 'string'},
      {name: 'width',    required: false, type: 'int',    default: 800},
      {name: 'height',   required: false, type: 'int',    default: 600},
      {name: 'units',    required: false, type: 'symbol', default: 'px'},   // 'px | 'pt | 'mm
      {name: 'grid',     required: false, type: 'int',    default: 10},     // grid spacing
      {name: 'bg',       required: false, type: 'string', default: "#fff"}
    ]
  },

  // ───────── layer (z-stack inside one drawing) ─────────
  layer: {
    role:    'drawing-container',
    content: [{role: 'drawing-object', qty: 'star'}],
    marks:   'none',
    attrs: [
      {name: 'id',      required: true,  type: 'string'},
      {name: 'name',    required: false, type: 'string', default: ""},
      {name: 'visible', required: false, type: 'bool',   default: true},
      {name: 'locked',  required: false, type: 'bool',   default: false}
    ]
  },

  // ───────── shape (geometric primitives) ─────────
  shape: {
    role:    'drawing-object',
    content: [],                    // shapes are leaves (text-frame is a separate kind)
    marks:   'none',
    atomic:  true, selectable: true, draggable: true,
    attrs: [
      {name: 'id',     required: true, type: 'string'},
      {name: 'kind',   required: true, type: 'symbol',                       // 'rect | 'ellipse | 'line | 'polyline | 'polygon | 'path | 'freehand | 'image
                       validate: (v) => v in ['rect, 'ellipse, 'line, 'polyline, 'polygon, 'path, 'freehand, 'image]},
      // geometry — interpretation depends on kind, see §3.3
      {name: 'x',      required: false, type: 'float'},
      {name: 'y',      required: false, type: 'float'},
      {name: 'width',  required: false, type: 'float'},
      {name: 'height', required: false, type: 'float'},
      {name: 'rotate', required: false, type: 'float', default: 0.0},
      {name: 'points', required: false, type: 'string'},                     // for polyline/polygon/path/freehand: SVG-like d
      {name: 'src',    required: false, type: 'string'},                     // for image
      // style
      {name: 'fill',         required: false, type: 'string', default: "transparent"},
      {name: 'stroke',       required: false, type: 'string', default: "#000"},
      {name: 'stroke-width', required: false, type: 'float',  default: 1.0},
      {name: 'opacity',      required: false, type: 'float',  default: 1.0},
      // optional: anchor ports for connectors (see §9.2)
      {name: 'ports',  required: false, type: 'array'}                       // [{id, x, y}, ...] in shape-local coords
    ]
  },

  // ───────── connector (edge between two shapes or free points) ─────────
  connector: {
    role:    'drawing-object',
    content: [{tag: 'label', qty: 'star'}],     // edge labels, optional
    marks:   'none',
    atomic:  true, selectable: true,
    attrs: [
      {name: 'id',            required: true,  type: 'string'},
      // endpoint anchoring — shape+port reference, OR free point (x,y)
      {name: 'from-shape',    required: false, type: 'string'},   // shape id
      {name: 'from-port',     required: false, type: 'string'},   // port id on that shape
      {name: 'from-x',        required: false, type: 'float'},    // free anchor (used if from-shape absent)
      {name: 'from-y',        required: false, type: 'float'},
      {name: 'to-shape',      required: false, type: 'string'},
      {name: 'to-port',       required: false, type: 'string'},
      {name: 'to-x',          required: false, type: 'float'},
      {name: 'to-y',          required: false, type: 'float'},
      // routing
      {name: 'routing',       required: false, type: 'symbol', default: 'orthogonal'},   // 'straight | 'orthogonal | 'curved | 'segment
      {name: 'waypoints',     required: false, type: 'array',  default: []},             // [{x,y}, ...]: user-pinned bends
      // arrow style
      {name: 'start-arrow',   required: false, type: 'symbol', default: 'none'},         // 'none | 'arrow | 'open-arrow | 'diamond | 'circle
      {name: 'end-arrow',     required: false, type: 'symbol', default: 'arrow'},
      // style
      {name: 'stroke',        required: false, type: 'string', default: "#000"},
      {name: 'stroke-width',  required: false, type: 'float',  default: 1.0},
      {name: 'stroke-dash',   required: false, type: 'string', default: ""}              // "" | "4,4" | "1,3"
    ]
  },

  // ───────── group (sub-tree treated as one selectable unit) ─────────
  group: {
    role:    'drawing-object',
    content: [{role: 'drawing-object', qty: 'plus'}],
    marks:   'none',
    selectable: true, draggable: true,
    attrs: [
      {name: 'id',    required: true, type: 'string'},
      {name: 'name',  required: false, type: 'string', default: ""}
    ]
  },

  // ───────── text-frame (rich-text island in canvas-space) ─────────
  // contents are normal flow-mode content under PM-style editing
  text-frame: {
    role:    'drawing-object',
    content: [{role: 'block', qty: 'plus'}],     // re-uses the flow-doc block schema
    marks:   'none',
    selectable: true, draggable: true, editable: true,
    attrs: [
      {name: 'id',     required: true,  type: 'string'},
      {name: 'x',      required: true,  type: 'float'},
      {name: 'y',      required: true,  type: 'float'},
      {name: 'width',  required: true,  type: 'float'},
      {name: 'height', required: true,  type: 'float'},
      {name: 'rotate', required: false, type: 'float', default: 0.0},
      {name: 'bg',     required: false, type: 'string', default: "transparent"}
    ]
  },

  // optional edge label
  label: {
    role:    'drawing-object',
    content: [{role: 'inline', qty: 'star'}],
    marks:   'all',
    editable: true,
    attrs: [
      {name: 'offset', required: false, type: 'float', default: 0.5}     // 0..1 position along edge
    ]
  }
}
```

**Two new roles (`drawing-container`, `drawing-object`)** keep drawing children separated from flow-doc roles. The validator already has the predicate that strings (`'text` role) satisfy `'inline` requirements; the same trick applies — a `'group` satisfies `'drawing-object`, a `'shape` satisfies `'drawing-object`, etc. This requires a small extension of `mod_md_schema.ls`'s role-compatibility table:

```lambda
fn role_satisfies(actual, required) {
  if (actual == required) { true }
  else if (required == 'inline')         { actual in ['text, 'mark, 'inline] }
  else if (required == 'drawing-object') { actual == 'drawing-object' or actual == 'shape' or actual == 'connector'
                                              or actual == 'group' or actual == 'text-frame' or actual == 'label' }
  else { false }
}
```

### 3.2 Why custom records, not SVG

| Aspect | Custom records | SVG-as-source |
|---|---|---|
| Connector routing metadata | natural (`routing`, `waypoints`, `from-port`) | unrepresentable (would need `<defs>` hacks or namespaced attrs) |
| Schema validation | each kind has its own attr list & validators | SVG attrs are open-ended, hard to constrain |
| Ports / anchors | first-class `ports` array | would need `<defs>` markers + ids |
| Editor metadata (locked, layer name) | first-class attrs | needs namespaced extension attrs |
| Format round-trip | round-trips to other diagram formats (drawio xml, plantuml) by walking records | inherently SVG-shaped; round-trip lossy |
| Render | one template step per shape kind | identity transform, but loses every advantage above |

Trade-off: the SVG renderer (`view <shape>`) is more code, but it's already the case in maxGraph / tldraw / Excalidraw that the source records and the render output diverge. This is the dominant industry pattern; we follow it.

### 3.3 Shape kinds and their geometry interpretation

| `kind` | Required geometry attrs | Notes |
|---|---|---|
| `'rect` | `x, y, width, height` | corner radius via optional `rx` attr (future) |
| `'ellipse` | `x, y, width, height` | bounding box; for circle, `width==height` |
| `'line` | `x, y, width, height` | endpoints derived from bounding box corners — direction inferred from `points: "tl-br"` etc., OR `points: "x1,y1 x2,y2"` |
| `'polyline` | `x, y, points` | `points` = SVG-like `"x1,y1 x2,y2 ..."`; `x, y` is the bounding box origin (auto-recomputed) |
| `'polygon` | `x, y, points` | closed polyline |
| `'path` | `x, y, points` | `points` = SVG path `d` (`M…L…Z…`) |
| `'freehand` | `x, y, points` | identical to path; produced by freehand tool, allowed to be smoothed by render template |
| `'image` | `x, y, width, height, src` | `src` is URL or `data:` URI |

Coordinates are **canvas-local floats** (`drawing` block establishes the origin). Units are governed by the `<drawing>` `units` attr; the renderer translates to SVG units 1:1 by default.

### 3.4 Why connectors are siblings of shapes, not children

A connector is conceptually an edge in a graph; it could be modeled as a child of one of its endpoints (the "owner") or as a flat sibling. **We choose flat siblings** because:

- Either endpoint can be moved / regrouped without the connector changing parent.
- Multi-shape selection naturally includes both endpoints + connector.
- The DAG nature of a graph doesn't fit cleanly inside a tree if connectors are nested.
- This matches maxGraph (`mxCell` for both vertices and edges, sibling in cell tree) and tldraw (arrows are top-level shapes).

The connector references endpoints by `from-shape` / `to-shape` (string id). When a referenced shape is deleted, the connector becomes "dangling": it falls back to its last known free coordinates (`from-x`/`from-y` and `to-x`/`to-y`), which the runtime keeps up-to-date during anchored editing as a degradation-safe cache.

### 3.5 Stable IDs are mandatory

Every drawing-object element MUST carry a unique `id` attribute. IDs are:

- Generated by the editor at insertion (`shape_id_alloc()` returns a hash-prefixed monotonic id, e.g. `"s_a3"`).
- Stable across re-renders (this is what makes the keyed reconciler work — Lexical-style).
- Stable across undo/redo (re-insertion uses the same id from the inverse step).
- Used as endpoint references in connectors.
- Used as the **first key** in `render_map` for drawing-object subtrees: `(shape_id, template_ref) → ResultEntry`.

Mark already supports `id:` as an arbitrary attribute, no parser changes needed.

---

## 5. Step Algebra and Commands

### 5.1 Zero new step kinds — design rule

**No new step kinds.** Every drawing operation lowers to one or more of the existing seven (`mod_step.ls`):

| Drawing operation | Lowers to |
|---|---|
| Move a shape | one `set_attr` per coord that changed (`x`, `y`) |
| Resize a shape | one `set_attr` per coord (`x`, `y`, `width`, `height`) |
| Rotate a shape | one `set_attr` (`rotate`) |
| Recolor a shape | one `set_attr` (`fill` / `stroke` / `stroke-width`) |
| Insert a shape | one `replace` (insert at the layer's child slice) |
| Delete shape(s) | one `replace` per shape, or a batched `replace` over a contiguous range |
| Change connector routing | one `set_attr` (`routing`) and possibly `set_attr` (`waypoints`) |
| Change shape kind (e.g., rect→ellipse) | one `set_node_type` *(see note)* |
| Group selection | one `replace` to lift them out, one `replace` to wrap in `<group>` (or one `replace_around`) |
| Ungroup | one `replace_around` removing the group's open/close, preserving children |
| Reorder z (bring forward / back) | one `replace` swapping siblings |
| Move shape across layers | one `replace` deleting in old, one `replace` inserting in new |

> **Note on `set_node_type` for shapes.** The existing `set_node_type` re-tags a node. For shapes, since `kind` is an *attribute* not the tag, "change kind" is actually a `set_attr` on `kind`. Cleaner. `set_node_type` is reserved for paragraph↔heading and other tag-level conversions in flow text.

### 5.2 Why no new step kinds — collab and history payoff

Every existing-step has `apply`, `invert`, `map` already implemented and tested (Stage 1-3 baseline). Lowering shape operations to the same steps means:

- History storage is uniform; one undo step rewinds either a typing edit or a shape drag.
- `Mapping` (Stage 3 §3.1 deletion-tracking position mapping) automatically translates selections through shape edits without new code.
- Collab readiness (Stage 3 §3.8): a stable-id-anchored `set_attr` on shape `S1.x` is naturally commutable with a `set_attr` on `S2.y` — a CRDT-friendly property we want.
- Test fixtures from PM's transform test corpus continue to apply to the drawing layer with no new categories.

### 5.3 New commands (high-level gestures)

A **command** is `(state, dispatch?) -> bool` (the PM pattern — see `mod_commands.ls`). New commands live in `mod_drawing_commands.ls`:

| Command | Effect |
|---|---|
| `cmd_insert_shape(kind, geometry, style)` | Insert a new shape at end of current layer; select it |
| `cmd_insert_connector(from_anchor, to_anchor, routing)` | Insert a new connector at end of current layer; select it |
| `cmd_move_shapes(delta_x, delta_y, ids?)` | Translate selection (or specified ids) by delta — one transaction, `set_attr(x)` + `set_attr(y)` per shape |
| `cmd_resize_shape(id, new_geometry, handle)` | Resize one shape by handle drag; produces 1-4 `set_attr` steps |
| `cmd_rotate_shapes(angle, pivot, ids?)` | Rotate around pivot (selection bbox center by default) |
| `cmd_route_connector(id, routing, waypoints?)` | Set routing strategy and waypoints |
| `cmd_set_attr(id, name, value)` | Direct attribute write (used by sidebar inspector) |
| `cmd_group_selection()` | Wrap multi-selection in `<group>` |
| `cmd_ungroup(id)` | Unwrap a `<group>` |
| `cmd_bring_to_front(id?)`, `cmd_send_to_back(id?)` | Reorder z within layer |
| `cmd_bring_forward(id?)`, `cmd_send_backward(id?)` | Step z by one |
| `cmd_align(direction, ids?)` | `direction` ∈ `{'left, 'right, 'top, 'bottom, 'h-center, 'v-center}` |
| `cmd_distribute(axis, ids?)` | Equal spacing on axis |
| `cmd_delete_selection()` | One `replace` per selected shape; handles dangling connectors (re-anchor to last known free coord) |
| `cmd_duplicate_selection()` | Deep-clone with new ids, offset by (10,10) |
| `cmd_paste_shapes_at(pos, slice)` | Insert a slice (parsed from clipboard) at pos with id-remap |
| `cmd_enter_canvas_mode(drawing_id)` | Mode switch (Stage 4 §2.3) |
| `cmd_exit_canvas_mode()` | Mode switch (Stage 4 §2.3) |
| `cmd_set_tool(name)` | Activate tool (§6) |

All commands are pure dispatch: they construct a `Transaction` (a list of steps + a target selection) and call `editor.dispatch(tx)`. Dry-run mode (`dispatch=null`) is used by toolbars to gray out unapplicable buttons — the standard PM idiom.

### 5.4 Drag coalescing

A drag gesture (e.g., move a shape) produces many pointer-move events. Each event runs `cmd_move_shapes` with a fresh delta from the gesture start. We coalesce in two ways:

1. **Within the transaction stream**: rather than committing a new transaction per pointer-move, the tool begins a single open transaction (`editor.tr_begin('drag')`) on pointerdown; pointer-moves replace the last step's `set_attr` value (not append); pointerup commits.
2. **In the history**: even without (1), the history plugin's existing coalescing window (500 ms / same step kind / same path) groups micro-edits into one undo entry.

(1) is the cleaner architecture and is what tldraw / Excalidraw / Figma do. We follow it. Mechanism: `mod_transaction.ls` already supports replacing the most recent same-kind step on the same path; we expose this as `tx_amend(...)` and use it from the drag tool.

---

## 6. Tool State Machine

### 6.1 The `Tool` interface

A tool is a map (canonical "trait") implementing the following keys:

```lambda
{
  name:           symbol,                            // 'select | 'rect | 'connector | ...
  cursor:         string,                            // CSS cursor name
  paint_decorations: fn(state) -> [Decoration],      // optional overlay (e.g., snap guides)
  on_pointerdown:    fn(state, evt) -> state,        // pointer-down handler
  on_pointermove:    fn(state, evt) -> state,
  on_pointerup:      fn(state, evt) -> state,
  on_keydown:        fn(state, evt) -> bool,         // returns true if handled
  on_enter:          fn(state) -> state,             // when tool activates
  on_exit:           fn(state) -> state              // when tool deactivates
}
```

`state` here is the editor state at the time of the event; handlers return the new state. They typically achieve effects by dispatching commands.

### 6.2 Built-in tools

| Tool | `on_pointerdown` | `on_pointermove` | `on_pointerup` | Key shortcut |
|---|---|---|---|---|
| `select` | If on a shape → start drag-move; if on a handle → start drag-resize; if on a connector waypoint → start drag-waypoint; if on empty → start lasso | If dragging → `cmd_move_shapes` / `cmd_resize_shape` / `cmd_route_connector`; if lasso → update lasso rect | Commit transaction; lasso → set selection | V |
| `pan` / `hand` | Start pan | Update viewport offset | Commit viewport | H, Space (held) |
| `rect` | Start rubber-band; insert phantom rect | Resize phantom | `cmd_insert_shape('rect, geom)` | R |
| `ellipse` | Same as rect | Same as rect | `cmd_insert_shape('ellipse, geom)` | O |
| `line` | Anchor first point | Track second point | `cmd_insert_shape('line, geom)` | L |
| `polyline` | First click anchors; subsequent clicks add points; double-click ends | Live-update last point | On finish `cmd_insert_shape('polyline, ...)` | P |
| `path` | Same as polyline; with smoothing | Same | Same | (P + modifier) |
| `freehand` | Start path recording | Append sample point | `cmd_insert_shape('freehand, ...)` | F |
| `connector` | First click: anchor on shape/port (if hit) or free point; cursor changes | Track second anchor; show ghost route | `cmd_insert_connector(from, to, routing)` | C |
| `text` | Click to place a text-frame; immediately enter inner text edit | n/a | n/a | T |
| `eraser` | Pointer-down: enter erase-on-hover mode | Hover over shape: `cmd_delete_selection([hit_id])` | Release | E |

### 6.3 Tool registry (script-side, plugin-friendly)

`mod_tools.ls` exposes `register_tool(tool)`, `set_tool(name)`, `active_tool()`. Editor consumers can add their own tools, e.g., a "UML class" tool that on pointerup inserts a composite shape group.

### 6.4 State machine semantics

Unlike tldraw's nested `StateNode` hierarchy, ours is flat — one active tool at a time, each tool is self-contained, transitions go through `editor.set_tool(name)`. This is simpler and sufficient for Stage 5 scope; nested sub-states (e.g., "select tool → resizing handle 3") are handled by tool-internal flags rather than a state hierarchy.

---

## 7. Hit Testing

### 7.1 Two-layer hit-test

Current Radiant hit-test → DomElement → `render_map` reverse lookup → source node. This is **tree hit-test**: walk to leaf DOM, look up.

Drawings also need **geometric hit-test**: given (x, y) in canvas space, which shape contains the point, accounting for z-order?

Both coexist:

```
pointer event in <drawing> region
   │
   ▼
geometric hit-test (mod_geom.ls)        ←── reverse-z walk over current layer's shapes
   │   produces: hit_id, hit_kind ('shape | 'handle | 'connector-segment | 'connector-waypoint | 'empty)
   ▼
tool's on_pointer* handler              ←── interprets hit + tool state to dispatch a command
```

### 7.2 Per-shape geometry contract

Each shape kind exposes:

```lambda
fn shape_geometry(shape)        // returns Geometry { kind, bbox, outline, handles, ports }
fn shape_point_in(shape, p)     // returns bool
fn shape_segment_hit(shape, p)  // returns nearest segment + distance (for hollow shapes)
fn shape_handle_hit(shape, p, hit_radius) // returns handle id or null
```

`Geometry` is a precomputed object cached per shape between edits (Excalidraw's `versionNonce` trick: invalidate on attr change).

| Kind | `point_in` | `outline` |
|---|---|---|
| `rect`, `ellipse` | analytic | rect → 4 corners; ellipse → 32-segment polygon |
| `line` | within `stroke-width / 2` of segment | the segment itself |
| `polyline` | within `stroke-width / 2` of any segment | the path |
| `polygon` | even-odd ray casting | the path closed |
| `path`, `freehand` | parsed `d` → segments → ray casting (closed) or stroke distance (open) | the path |
| `image` | bounding-box test | rect |
| `text-frame` | bounding-box test | rect |
| `group` | union of children (recursive) | union outline |
| `connector` | per-segment distance test on routed polyline | routed polyline |

Rotation is applied as an inverse transform on the point before testing (rotate -θ around shape center).

### 7.3 Handle hit-test (for `select` tool)

Selected shapes show 8 resize handles + 1 rotate handle. Hit-test is small-radius point-in-circle for each handle. Handles are computed in screen space (constant size regardless of zoom).

### 7.4 Z-order traversal

For point hit-test on the canvas: iterate layers top to bottom (last in source = top), within each layer iterate children in reverse (last = top), return first containing shape. Hidden layers (`visible: false`) are skipped. Locked layers (`locked: true`) are skipped (their children pass through to lower layers).

---

## 8. Render Contract

### 8.1 Template structure (Reactive_UI §3 templates)

```lambda
edit <drawing> {
  <div class: "rdt-drawing"
       data-drawing-focus: ~.id
       style: ("width:" ++ ~.width ++ "px;height:" ++ ~.height ++ "px;background:" ++ ~.bg ++ ";")
       (
    <svg viewBox: ("0 0 " ++ ~.width ++ " " ++ ~.height)
         width: ~.width  height: ~.height
         (apply ~.children)>          // each layer
    (if editor.focused_drawing == ~.id then render_selection_overlay() else nothing)
    (if editor.focused_drawing == ~.id then render_tool_decorations() else nothing)
    )>
}

edit <layer> { <g data-layer: ~.id  (apply ~.children)> }

edit <shape kind: 'rect> {
  <rect x: ~.x  y: ~.y  width: ~.width  height: ~.height
        transform: ("rotate(" ++ ~.rotate ++ " " ++ (~.x + ~.width/2) ++ " " ++ (~.y + ~.height/2) ++ ")")
        fill: ~.fill  stroke: ~.stroke  stroke-width: ~.stroke-width
        opacity: ~.opacity  data-shape-id: ~.id>
}

edit <shape kind: 'ellipse> { ... <ellipse> ... }
edit <shape kind: 'line>    { ... <line> ... }
edit <shape kind: 'polyline>{ ... <polyline points: ~.points> ... }
edit <shape kind: 'path>    { ... <path d: ~.points> ... }
edit <shape kind: 'image>   { ... <image href: ~.src> ... }

edit <connector> {
  let route = compute_route(~) ;        // mod_router.ls
  <path d: (route_to_d(route))
        stroke: ~.stroke  stroke-width: ~.stroke-width
        stroke-dasharray: ~.stroke-dash
        marker-start: (arrow_marker_id(~.start-arrow))
        marker-end:   (arrow_marker_id(~.end-arrow))
        data-shape-id: ~.id
        fill: "none">
  (apply ~.children)                    // labels
}

edit <text-frame> {
  // text-frames render as <foreignObject> wrapping flow-doc content
  <foreignObject x: ~.x  y: ~.y  width: ~.width  height: ~.height
                 transform: (rotate_xform(~)) >
    <div class: "rdt-text-frame"
         data-editable: ~
         style: ("background:" ++ ~.bg ++ ";")
         (apply ~.children)>            // re-enters flow-doc templates
}
```

### 8.2 The selection / decoration layer

Renders as a sibling `<g>` over the drawing's main `<svg>`, painted only while the drawing is focused. Driven by `mod_selection_handles.ls` and the active tool's `paint_decorations`.

### 8.3 Position-only fast path

The standard render flow is: `set_attr(x)` → `render_map_mark_dirty` → Phase-2 re-transform → DOM rebuild → layout → render.

For drag gestures producing a `set_attr(x)` per frame on the same shape, the full path is expensive. The optimization (§15 phase 4h):

- Identify position-only attribute changes (`x`, `y`, `rotate`, `width`, `height`, `points`, `waypoints`).
- Patch the corresponding SVG attribute on the rendered DOM **directly** (no template re-execution, no relayout — only paint).
- Keep the `render_map` consistent by recording the new attribute value but skipping the re-transform.

This is safe because (a) for SVG primitives, the result template is an identity transform of geometry attrs; (b) the layout is canvas-fixed (the `<svg>` doesn't reflow when an inner shape moves); (c) connectors anchored to a moved shape recompute their `d` attribute the same way.

For non-geometry attrs (`fill`, `stroke`, …) the same fast-path applies — they're style attrs that don't trigger relayout.

For structure-changing edits (insert / delete / regroup / kind change) the standard slow path runs.

### 8.4 Cache invalidation: `versionNonce`

Each shape carries a runtime `_nonce` (not serialized). On any attribute change, the nonce bumps. The `render_map` lookup checks `(shape_id, _nonce)` before re-using a cached result. This is Excalidraw's trick and it's cheap.

---

## 9. Connector Routing

### 9.1 The routing pipeline

```
endpoints → resolved-anchor-points → routing strategy → polyline → simplified d-attribute
```

1. **Resolve endpoints** to absolute canvas points:
   - If `from-shape` set: look up shape, look up `from-port` if set, else use `closest-edge-point` algorithm from maxGraph.
   - Else use `(from-x, from-y)` free point.
2. **Apply routing strategy** (per `routing` attribute):
   - `'straight`: polyline = `[from, to]`.
   - `'orthogonal` (default): two-or-three-segment route avoiding endpoint shape boxes; rules adapted from maxGraph's `mxEdgeStyle.OrthConnector`.
   - `'curved`: `'orthogonal` with each corner replaced by a quarter-arc of configurable radius.
   - `'segment`: like orthogonal but allows segments at arbitrary angles (45°, 30°), maxGraph-style.
   - User waypoints (`waypoints` attr) are **pinned through-points** — the route is forced to pass through them. Between waypoints the strategy applies independently.
3. **Convert to SVG `d`** for rendering.

### 9.2 Anchors and ports

Three anchor kinds:

| Kind | Stored | Resolution |
|---|---|---|
| **Port anchor** | `from-shape: "S1", from-port: "p3"` | Look up shape `S1`, look up port `p3` in `S1.ports[]`; if absent, fall back to closest-edge-point. |
| **Shape edge anchor** (default) | `from-shape: "S1"` (no port) | Closest-edge-point algorithm: connect to the point on `S1`'s outline nearest the next routing point. |
| **Free anchor** | `from-x, from-y` (no `from-shape`) | Absolute canvas point. Used for dangling connectors. |

Ports are declared per shape via the `ports` attr — `[{id: "p1", x: 0, y: 0}, {id: "p2", x: 1, y: 0.5}, ...]` in shape-local normalized coords (0..1). Templates render port indicators when shape is hovered with the connector tool active.

### 9.3 The orthogonal routing algorithm

(Lambda port of maxGraph's `mxEdgeStyle.OrthConnector`.)

Given start `S`, end `E`, and the bounding boxes of the source/target shapes (when applicable):

1. Compute the **direction of exit** from each shape — perpendicular to the side of the shape closest to the other endpoint.
2. Try the **L-route** first (one bend): two segments meeting at a right angle.
3. If the L-route crosses either endpoint shape, try the **Z-route** (two bends, three segments).
4. If a Z-route also crosses, expand to a **U-route** (three bends, four segments) escaping around the shape.
5. The route is selected to minimize total path length subject to no-crossing-endpoints.
6. User `waypoints` interleave at the appropriate phase — each consecutive pair is routed independently.

The implementation is a port; we are explicit about the source.

### 9.4 Live re-routing on shape move

When a shape moves (drag), every connector anchored to it must re-route. The fast path:

- Connector lookup: maintain a `shape_id → [connector_id, ...]` reverse index (`mod_router.ls` builds it).
- For each affected connector, recompute the route, patch its `d` attribute directly (fast-path §8.3).
- This is O(deg(shape)) per frame, typically 1-3 connectors per shape — negligible.

### 9.5 Waypoint editing

Selecting a connector with the `select` tool shows its waypoints as small circles + midpoint indicators on each segment. Dragging a waypoint changes `waypoints[i]` (single `set_attr`). Dragging a midpoint *inserts* a waypoint there (single `set_attr` on `waypoints`). Right-click delete removes a waypoint.

---

## 10. Snap, Align, and Smart Guides

### 10.1 Snap-to-grid

When `<drawing>` declares `grid: 10`, geometry attrs snap to the nearest multiple of 10. Snapping is applied at the **command** level — `cmd_move_shapes` snaps the resulting `x`, `y`. Shift held disables snap (PM idiom).

### 10.2 Snap-to-object

When dragging, the snap module collects candidate anchor points from neighbouring shapes (within a viewport-scaled radius):

- Bounding box: 4 corners + 4 edge midpoints + center.
- Connector endpoints + waypoints.
- Drawing-level grid lines.

For each candidate, if the dragged shape's corresponding anchor is within snap-distance (default 8 px screen), snap; emit a **snap guide** decoration as a thin red dashed line along the alignment.

### 10.3 Smart sizing & equal spacing

`cmd_align` and `cmd_distribute` are command-level (not snap-time) operations. UI: the multi-select toolbar exposes the standard 6 align + 2 distribute buttons.

### 10.4 Guides as decorations

All snap / alignment visuals are **decorations** (Reactive_UI §8 / Stage 3 §3.10) — they never enter the source tree, never enter history. The active tool's `paint_decorations` produces them.

---

## 11. Clipboard and Paste

### 11.1 Copy / cut format

Copying drawing-objects writes to the OS clipboard in three flavours:

| Flavour | Use |
|---|---|
| `application/x-lambda-mark` | Round-trip into Lambda editor preserving full fidelity (canonical) |
| `image/svg+xml` | Paste into vector graphics tools / browsers (lossy — drops ports, routing metadata) |
| `image/png` | Paste into Word, raster apps; rendered by Radiant offscreen |

### 11.2 Paste

Paste detection order: `application/x-lambda-mark` → `image/svg+xml` → `image/png` → text. SVG paste schema-coerces shapes into our records (see §11.3). PNG paste creates one `image` shape. Text paste in canvas mode opens a new text-frame at cursor.

### 11.3 SVG schema-coercion

Adapt the algorithm sketched in Editor3.md for HTML paste: each tag is mapped to a shape kind, attributes filtered to the allowed set, unknown tags dropped, structure flattened (e.g., nested `<g>` collapses into a single `<group>`). The result is a `Slice` (Stage 3 §3.2) and is inserted via the existing `replace` step.

### 11.4 Drag-and-drop

| Source | Drop target | Action |
|---|---|---|
| Internal: shape | inside same drawing | `cmd_move_shapes` (or cross-layer if dropped on different layer) |
| Internal: shape | onto flow doc | Convert to inline image (render shape to PNG/SVG) — Stage 5 *uses* SVG export |
| External: file (svg / png / jpg) | onto drawing | Insert as shape (svg → coerce; png/jpg → image shape) |
| External: file | onto flow doc | Normal file paste (existing behaviour) |

---

## 12. History Integration

### 12.1 One transaction per gesture

A drag = one transaction. A click-resulting-in-insert = one transaction. A multi-step command (group → align → distribute by user) = one transaction per step (each is its own undo).

### 12.2 Coalescing

The history plugin's existing window (Stage 3 §5.4 / PM-derived `historyCompression`) collapses successive transactions whose:

- Last step has the same `kind`, same target path, and
- Time delta < 500 ms

— into one undo entry. This handles typing-in-text-frame and continuous drag both. Compression is **suppressed** between transactions of different *modes* (canvas vs flow) so undo always lands the cursor in the mode the edit happened in.

### 12.3 Selection + tool + mode restoration

Each transaction in history records `(selection_before, selection_after, mode_before, mode_after, tool_before)`. Undo restores all four. Redo restores `(selection_after, mode_after, …)`. This is how Figma / Sketch / Affine behave and is what users expect.

### 12.4 Connector dangling recovery

When undoing a deletion of a shape that had connectors anchored to it, the connectors are re-anchored to the restored shape (their `from-shape` / `to-shape` references survived the deletion, and now resolve again). The fall-back-to-free-coordinate behaviour (§3.4) means the user never sees broken connectors during the dangling window either.

---

## 13. Package Module Plan

### 13.1 New modules

| Module | Responsibility | LOC budget |
|---|---|---|
| `lambda/package/editor/mod_geom.ls` | Vec2, Rect, Matrix3x3, transforms, point-in-polygon, segment-distance, bbox math | 600 |
| `lambda/package/editor/mod_drawing_schema.ls` | Schema entries for `drawing`, `layer`, `shape`, `connector`, `group`, `text-frame`, `label`; the `'drawing-object` role | 300 |
| `lambda/package/editor/mod_doc_schema.ls` | Combined schema = `md_schema ++ drawing_schema`; default for new editors | 60 |
| `lambda/package/editor/mod_tools.ls` | Tool interface, tool registry, all built-in tools (select, pan, rect, ellipse, line, polyline, path, freehand, connector, text, eraser) | 1400 |
| `lambda/package/editor/mod_router.ls` | Connector routing (straight, orthogonal, curved, segment); port resolution; reverse `shape→connectors` index | 800 |
| `lambda/package/editor/mod_snap.ls` | Grid snap, object snap, guides as decorations | 350 |
| `lambda/package/editor/mod_drawing_commands.ls` | All `cmd_*` from §5.3 | 1200 |
| `lambda/package/editor/mod_selection_handles.ls` | Resize/rotate/waypoint handle rendering + hit-test | 400 |
| `lambda/package/editor/mod_drawing_templates.ls` | `view`/`edit` templates per shape kind, connector, text-frame; selection overlay | 900 |
| `lambda/package/editor/mod_clipboard_drawing.ls` | Copy/paste flavour handling for drawings; SVG coercion | 500 |

### 13.2 Modules extended

| Module | Extension |
|---|---|
| `mod_md_schema.ls` | Add `'drawing-object` role to `role_satisfies` |
| `mod_source_pos.ls` | Add `multi_node_selection`; add `selection_paths(sel)` helper |
| `mod_step.ls` | No new step kinds; add `step_amend` (in-place last-step replacement for drag) |
| `mod_transaction.ls` | `tx_amend`, `tx_meta` ('drag' marker), drag coalescing entry point |
| `mod_history.ls` | Mode/tool restoration in undo entries |
| `mod_editor.ls` | Add `editor.mode`, `editor.tool`, `editor.viewport`, `editor.focused_drawing`; new methods: `set_mode`, `set_tool`, `pan`, `zoom`, `set_viewport` |
| `mod_input_intent.ls` | Route pointer events through the active tool's handlers when mode = `'canvas` |
| `mod_dom_bridge.ls` | Geometric hit-test path; data-drawing-focus walk |
| `mod_html_paste.ls` | Add SVG flavour preference when source includes SVG fragment |
| `mod_decorations.ls` | Snap guide + handle decoration kinds |

### 13.3 Bridge to Radiant (C side)

`radiant/source_pos_bridge.hpp` already covers tree positions. New header:

```c
// radiant/canvas_bridge.hpp (Stage 5)
typedef struct CanvasBridge CanvasBridge;

// hit-test entry from radiant/event.cpp
typedef struct {
    const char* hit_kind;   // "shape" | "handle" | "connector-segment" | "connector-waypoint" | "empty"
    const char* hit_id;     // shape id (if any)
    int         handle_idx; // for "handle"
    float       canvas_x, canvas_y;
} CanvasHitResult;

bool canvas_bridge_hit_test(CanvasBridge*, float screen_x, float screen_y, CanvasHitResult* out);

// route a pointer event through the active tool's lambda handler
void canvas_bridge_pointer(CanvasBridge*, const char* phase /* "down|move|up" */,
                           float canvas_x, float canvas_y, int buttons, int mods);
```

The C side is thin — Lambda owns the tool state machine; C invokes the Lambda function for each pointer phase.

---

## 14. Test Suite Plan

A new tree `test/lambda/editing/` (parallel to the existing `test/lambda/editor/`) — kept separate so Stage-5 work doesn't entangle with the Stage 1-3 baseline.

### 14.1 Layout

```
test/lambda/editing/
├── cases.md                       # the test catalog (prepared first)
├── fixtures/
│   ├── docs/                      # source documents
│   │   ├── empty_drawing.mark
│   │   ├── two_rects.mark
│   │   ├── connector_basic.mark
│   │   ├── grouped_shapes.mark
│   │   ├── text_in_doc.mark
│   │   └── ...
│   ├── events/                    # input event scripts (JSON: tool + pointer/key sequence)
│   │   ├── click_into_drawing.json
│   │   ├── draw_rect.json
│   │   ├── drag_rect.json
│   │   ├── resize_rect_nw.json
│   │   ├── rotate_rect.json
│   │   ├── insert_orth_connector.json
│   │   ├── drag_waypoint.json
│   │   └── ...
│   └── expected/                  # expected result documents (post-edit)
│       └── ... (one file per case)
├── schema_basic.ls                # smoke: schema validates drawing fixtures
├── shape_insert.ls
├── shape_move.ls
├── shape_resize.ls
├── shape_rotate.ls
├── shape_recolor.ls
├── shape_delete.ls
├── shape_group_ungroup.ls
├── shape_z_order.ls
├── connector_insert.ls
├── connector_routing_orth.ls
├── connector_routing_curved.ls
├── connector_waypoint_edit.ls
├── connector_anchor_port.ls
├── connector_dangling.ls
├── snap_grid.ls
├── snap_object.ls
├── align_distribute.ls
├── multi_select.ls
├── tool_select.ls
├── tool_rect.ls
├── tool_connector.ls
├── mode_switch.ls
├── history_drag_coalesce.ls
├── history_undo_restores_selection.ls
├── clipboard_copy_paste_shape.ls
├── clipboard_paste_svg.ls
└── text_frame_inside_canvas.ls
```

### 14.2 Test case catalog — outline of `cases.md`

The catalog is written **first**, in `cases.md`, so test scope is reviewable before any fixtures are produced. Outline:

| Category | Cases | Case-derivation source(s) |
|---|---|---|
| Schema validation | 10 cases — valid drawings, invalid (missing id, bad kind, dangling connector ref, unknown attr, ill-typed attr, wrong child role, missing required port spec, attr out of bounds, structural-cycle, empty layer) | PM `prosemirror-model/test/test-schema.js` |
| `SourcePos` over drawings | 6 cases — path resolution to shape, to connector, to text-frame, multi-node path resolution, deepest-common-ancestor across multi-select | PM `prosemirror-model/test/test-resolve.js` |
| Step apply/invert/map | 10 cases per step kind on drawing-objects (set_attr on x/y/fill/kind, replace inserting shape, replace deleting shape, replace_around for group/ungroup) | PM `prosemirror-transform/test/test-step.js` |
| Tool: select | 8 cases — click on shape, click on empty, shift-click extend, lasso, drag-move, drag-resize, drag-rotate, ESC clears | tldraw select tool tests; BlockSuite edgeless selection |
| Tool: shape tools | 5 per tool — rect, ellipse, line, polyline (multi-click), path (smoothing), freehand (drag), text (insert + immediate text-edit) | tldraw shape tool tests |
| Tool: connector | 8 cases — anchor on shape, anchor on port, anchor free, anchor → drop on shape, anchor → drop on free, routing default, switch routing strategy, escape mid-draw | maxGraph mxConnector tests |
| Routing | 12 cases — straight, orthogonal L-route, orthogonal Z-route, orthogonal U-route, waypoint pinning, port anchor, edge anchor, route under move, multi-bend, label position, arrowhead variants, dangling fallback | maxGraph `EdgeStyle*` tests |
| Snap | 6 cases — grid snap on/off, object-edge snap, center snap, no snap with Shift, snap guide decoration emit, multi-axis snap | tldraw snap tests |
| Align / distribute | 10 cases — left/right/top/bottom/h-center/v-center align, horizontal distribute, vertical distribute, no-op on single selection, partial selection in nested groups | Figma / Sketch parity |
| Multi-select | 6 cases — shift-click add, shift-click subtract, lasso intersect, lasso enclose, group as one selectable, ESC clears | tldraw multi-select; BlockSuite edgeless multi-select |
| Mode switch | 6 cases — click into drawing enters canvas, ESC×2 returns to flow, click outside returns, focus state persists on re-enter, mode-aware undo, selection cleared on mode change | **BlockSuite edgeless mode tests**; Affine mode-switch |
| Group / ungroup | 5 cases — group selection, ungroup preserves z-order, nested groups, group then move, undo group | tldraw group tests; BlockSuite edgeless group/frame |
| Z-order | 4 cases — bring to front, send to back, step forward, step backward | maxGraph z-order |
| History coalesce | 8 cases — drag = one entry, typing in text-frame = one entry, drag-then-resize = two entries, mode-change breaks coalesce, undo restores selection, undo restores mode+tool, redo restores selection, undo after delete restores connectors | PM history tests |
| Clipboard | 8 cases — copy single shape, copy multi-select, copy group, paste preserves ids (with remap), paste SVG, paste PNG, paste text in canvas → new text-frame, cross-drawing paste | Excalidraw clipboard tests; BlockSuite edgeless clipboard |
| `text-frame` interop | 5 cases — text caret inside frame, PM commands work inside frame, marks (bold/italic) inside frame, frame resize affects text layout, frame delete | PM tests inside an arbitrary node; BlockSuite edgeless note/frame |
| Performance budgets | 4 cases — drag at 60Hz on 100-shape canvas, route 50 connectors on shape move, undo 100 typing chars, retransform single attr edit < 5ms | (no external ref) |

Total budget: ~150 cases across ~30 test files. Each case ≤ 100 lines of `.ls`. The fixture format keeps cases readable.

**BlockSuite edgeless as a case source.** BlockSuite (Affine) is the closest existing "document with an embedded canvas" system (§2.1), so its **edgeless** test suite is the best external source for the canvas-UX categories above — selection, multi-select, mode switch, groups/frames, and in-canvas clipboard (the embed-in-doc angle that tldraw and maxGraph don't cover). Two caveats on *how* to use it:

- **Manual case derivation, not auto-harvest.** Unlike the Chromium editing suite (clean declarative `assert_selection` we harvest mechanically), BlockSuite's edgeless tests are **Playwright** e2e (DOM/pixel + Yjs assertions). We read each scenario and author the equivalent `.ls` fixture by hand; we port the *cases*, not the code.
- **Its Yjs store / CRDT unit tests are not applicable** — our model is step/transaction-based, not CRDT-backed, so only the edgeless *interaction* scenarios transfer, not the storage tests.

(Deferred: actually mining BlockSuite's edgeless tests happens when the drawing layer is built — see §15. This entry records it as a planned source.)

### 14.3 Fixture format

**Source doc fixture** (`fixtures/docs/two_rects.mark`):

```lambda
<doc
  <p "Before the drawing.">
  <drawing id: "D1" width: 400 height: 300
    <layer id: "L1"
      <shape id: "S1" kind: 'rect x: 50 y: 50 width: 100 height: 80
             fill: "#fff" stroke: "#000" stroke-width: 1>
      <shape id: "S2" kind: 'rect x: 200 y: 100 width: 120 height: 80
             fill: "#eef" stroke: "#000" stroke-width: 1>
    >
  >
  <p "After the drawing.">
>
```

**Event script fixture** (`fixtures/events/drag_rect.json`):

```json
{
  "initial_mode": "flow",
  "initial_selection": {"kind": "text", "path": [0, 0], "offset": 0},
  "events": [
    {"type": "click",       "target": "shape:S1"},
    {"type": "pointerdown", "canvas_x": 100, "canvas_y": 100, "buttons": 1},
    {"type": "pointermove", "canvas_x": 150, "canvas_y": 130, "buttons": 1},
    {"type": "pointermove", "canvas_x": 180, "canvas_y": 150, "buttons": 1},
    {"type": "pointerup",   "canvas_x": 180, "canvas_y": 150, "buttons": 0}
  ]
}
```

**Expected result** (`fixtures/expected/drag_rect_result.mark`):

```lambda
<doc
  <p "Before the drawing.">
  <drawing id: "D1" width: 400 height: 300
    <layer id: "L1"
      <shape id: "S1" kind: 'rect x: 130 y: 100 width: 100 height: 80
             fill: "#fff" stroke: "#000" stroke-width: 1>
      <shape id: "S2" kind: 'rect x: 200 y: 100 width: 120 height: 80
             fill: "#eef" stroke: "#000" stroke-width: 1>
    >
  >
  <p "After the drawing.">
>
```

**Plus** the test asserts:
- Final history depth = 1 (one drag = one entry).
- Inverse of the recorded transaction restores the original doc exactly.
- Final selection = NodeSelection on S1.
- Mode = `'canvas`.

### 14.4 Test runner

`mod_test_harness.ls` (new helper) provides:

```lambda
fn run_case(doc_path, events_path, expected_path, asserts) -> result
```

It loads the doc, applies the events through a headless `editor` session (no Radiant binding — direct dispatch to commands via the input intent module), then asserts deep-equality on the result doc + the optional invariants. Headless makes the suite fast and deterministic.

A *separate* tier (§15 phase 4h) drives the same fixtures through the live Radiant pipeline to catch render/layout/hit-test bugs that the headless tier can't see. This is the existing pattern from `test/lambda/editor/` baseline tests, extended.

---

## 15. Phased Implementation

### Phase 4a — Data model + read-only rendering (1 week)

| Task | Output |
|---|---|
| Schema entries for drawing/layer/shape/connector/group/text-frame | `mod_drawing_schema.ls` |
| Combined schema | `mod_doc_schema.ls` |
| `view`/`edit` templates for each shape kind + connector + text-frame | `mod_drawing_templates.ls` |
| Routing minimal stub (straight only) | `mod_router.ls` (partial) |
| Geometry helpers (Vec2, Rect, Matrix3x3) | `mod_geom.ls` |
| First test: `schema_basic.ls` + `shape_render.ls` | `test/lambda/editing/` |

**Exit:** an existing Markdown doc with a `<drawing>` block parses, validates, and renders in `lambda view` showing the shapes correctly. No editing yet.

### Phase 4b — Mode model + select tool + geometric hit-test (1 week)

| Task | Output |
|---|---|
| `editor.mode`, `editor.focused_drawing`, `set_mode`, `set_tool` | `mod_editor.ls` extension |
| Tool interface + tool registry | `mod_tools.ls` skeleton |
| Select tool: click, lasso, multi-select | `mod_tools.ls` |
| Geometric hit-test per shape kind | `mod_geom.ls` + `mod_dom_bridge.ls` |
| `canvas_bridge.hpp` + glue in `radiant/event.cpp` | `radiant/canvas_bridge.{hpp,cpp}` |
| Selection overlay decoration (handles + bbox) | `mod_selection_handles.ls`, `mod_decorations.ls` |
| Tests: `tool_select.ls`, `mode_switch.ls`, `multi_select.ls` | tests |

**Exit:** click into a drawing, see the surrounding selection box; shift-click adds; lasso works; ESC×2 exits to flow mode.

### Phase 4c — Shape creation + drag-move + drag-resize + drag-rotate (1 week)

| Task | Output |
|---|---|
| Rect, ellipse, line, image tools | `mod_tools.ls` |
| `cmd_insert_shape`, `cmd_move_shapes`, `cmd_resize_shape`, `cmd_rotate_shapes` | `mod_drawing_commands.ls` |
| Drag coalescing (`tx_amend`) | `mod_transaction.ls`, `mod_step.ls` |
| Direct-DOM-patch fast path for geometry attrs | `mod_dom_bridge.ls`, `radiant/canvas_bridge.cpp` |
| Tests: shape_insert/move/resize/rotate, history_drag_coalesce | tests |

**Exit:** can draw rect/ellipse/line, drag them around, resize from 8 handles, rotate. Drag is one undo entry. 60Hz.

### Phase 4d — Connector tool + routing (1.5 weeks)

| Task | Output |
|---|---|
| Connector tool (anchor on shape / port / free) | `mod_tools.ls` |
| Orthogonal router (port of maxGraph's `OrthConnector`) | `mod_router.ls` |
| Curved router (orthogonal + arc fillets) | `mod_router.ls` |
| Closest-edge-point anchor resolver | `mod_router.ls` |
| Port declaration + render port indicators | `mod_drawing_templates.ls` |
| Waypoint editing (drag waypoint, insert midpoint) | `mod_tools.ls`, `mod_drawing_commands.ls` |
| Dangling-connector fallback | `mod_router.ls`, `mod_drawing_commands.ls` |
| Connector update on shape drag (reverse index) | `mod_router.ls`, fast-path in `mod_dom_bridge.ls` |
| Tests: connector_*, routing_* | tests |

**Exit:** can draw orthogonal and curved connectors between shapes, drag waypoints, anchor to ports; connectors re-route live during shape drag.

### Phase 4e — Snap + align + distribute (3 days)

| Task | Output |
|---|---|
| Grid snap | `mod_snap.ls` |
| Object snap (edges, centers, midpoints) | `mod_snap.ls` |
| Snap guide decorations | `mod_decorations.ls` |
| `cmd_align`, `cmd_distribute` | `mod_drawing_commands.ls` |
| Tests: snap_*, align_distribute | tests |

**Exit:** snap during drag, see guides, align/distribute via toolbar commands.

### Phase 4f — Groups, layers, z-order (3 days)

| Task | Output |
|---|---|
| `cmd_group_selection`, `cmd_ungroup` | `mod_drawing_commands.ls` |
| Z-order commands (bring forward / send back / etc.) | `mod_drawing_commands.ls` |
| Layer panel (script-side UI) | example `view <layer-panel>` |
| Tests: shape_group_ungroup, shape_z_order | tests |

**Exit:** group/ungroup works, z-order changes correctly, undo restores ordering.

### Phase 4g — Clipboard + paste (3 days)

| Task | Output |
|---|---|
| Mark + SVG + PNG clipboard flavours | `mod_clipboard_drawing.ls` |
| SVG coercion | `mod_clipboard_drawing.ls` |
| Drag-and-drop in/out | `mod_clipboard_drawing.ls`, `mod_input_intent.ls` |
| Tests: clipboard_* | tests |

**Exit:** copy/paste within editor; paste SVG from browser; paste PNG; drag SVG file in.

### Phase 4h — Polish + perf + text-frame interop (1 week)

| Task | Output |
|---|---|
| Direct-DOM-patch fast path generalization (style attrs too) | `mod_dom_bridge.ls`, `radiant/canvas_bridge.cpp` |
| Text-frame ↔ flow editor interop | `mod_drawing_templates.ls`, `mod_editor.ls` |
| Decoration de-flicker | `mod_decorations.ls` |
| Performance budgets in tests | `test/lambda/editing/perf_*.ls` |
| Cases.md → final test catalog | `test/lambda/editing/cases.md` |

**Exit:** perf budgets met; text-frame editing inside a drawing is bit-for-bit identical to flow editing; all 150 cases pass.

### 15.1 Estimated effort

| Phase | Effort |
|---|---|
| 4a | 1 week |
| 4b | 1 week |
| 4c | 1 week |
| 4d | 1.5 weeks |
| 4e | 3 days |
| 4f | 3 days |
| 4g | 3 days |
| 4h | 1 week |
| **Total** | **~6.5 weeks** |

---

## 16. Risks and Open Questions

### 16.1 Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Orthogonal router edge cases (overlapping shapes, very-near endpoints, zero-size bbox) | High | Direct port from maxGraph — they've shipped this for 15 years; port their test cases too |
| Drag perf on 100+ shapes | Medium | Direct-DOM-patch fast path bypasses retransform (§8.3); benchmarked in Phase 4h |
| `<foreignObject>` text-frame layout in Radiant | Medium | Radiant already lays out HTML inside SVG (CSS layout engine doesn't care about parent SVG); spike in Phase 4a to confirm |
| Mark parser tolerating `'rect`-style symbol attr values | Low | Already supported in tests of `chart/` |
| `replace_around` correctness for group/ungroup | Medium | Existing Stage-1-3 baseline covers `replace_around` for flow text — same algorithm; add fixtures for drawing case |
| Coordinate units (px vs pt) cross-format paste | Low | Stage 5 ships with `'px` only; other units deferred |
| Connector dangling resolution after undo of complex multi-shape delete | Medium | Test fixture `connector_dangling.ls` exercises explicitly; restore is purely by id |
| Schema validation cost on every transaction | Low | Validation is incremental — only changed subtrees re-validated; identical to text editor today |

### 16.2 Open questions

1. **Should a freehand drawing's `points` attribute be smoothed at insert time or kept raw?** Recommendation: raw on insert; smoothing is a per-render template decision so original samples are preserved for later edit.
2. **Should connectors carry `from-x/from-y` as a cache even when anchored?** Yes — refreshes during drag without re-resolving, also serves as dangling fallback (§3.4).
3. **Where do tool palettes live in the UI?** Default: floating toolbar at the top of the focused drawing, configurable via `editor.tool_palette_position` (`'top | 'left | 'floating`). Same construct as Affine.
4. **Are layers required or can a drawing have shapes directly?** Required for now (one default layer minimum) — simpler invariants. Users who want one layer never see the distinction (one default layer is created on `cmd_insert_shape` if missing).
5. **What about `text-frame` nesting depth?** A `text-frame` can contain a `<drawing>` (recursive drawings) — *not* in the Stage 5 MVP. Schema disallows it; we revisit if a use case appears.
6. **Image shapes — embed (data URI) or reference (URL)?** Both. Schema accepts either. Drag-from-OS produces data URIs; paste-from-clipboard produces data URIs; user can edit to URL.

### 16.3 What we explicitly do **not** import from PM or Draw.io

- PM's integer-position scheme — kept Slate-path-based (Stage 3 §3.1 decided this).
- mxGraph's stylesheet system — Lambda's template + CSS-resolver already provides this at the right level.
- mxGraph's view (`mxGraphView`) — Radiant is our view.
- maxGraph's cell editor — replaced by the existing PM-shaped text editor inside `text-frame`.
- Affine's Yjs sync layer — collab is Stage 5+.
- tldraw's signals-based reactive store — `render_map` + state store handles reactivity.

---

## 17. Summary

Stage 5 specifies the **canvas-mode editor** inside the `<drawing>` block that [Stage 4](Radiant_Editor_Stage4.md) introduces. It does so without forking the existing transaction algebra, history, or template machinery — every shape edit lowers to existing `set_attr` / `replace` / `replace_around` steps, every render goes through `view`/`edit` templates and `render_map`, every undo entry is a normal `Transaction`. The novelty lives in three small layers: a geometric hit-test (`mod_geom`), a tool state machine (`mod_tools`), and a connector router (`mod_router`). The architectural debt is bounded.

The two reference editors picked — Affine BlockSuite for embed UX and maxGraph for routing — are the right pair: Affine validates the "doc with drawing surface" shape end-to-end, maxGraph validates the routing algorithm at production scale. We adopt their architectural ideas surgically and run the algorithm port through our existing step/transaction/history substrate. The result is a unified rich-document editor whose drawings are first-class peers of paragraphs, with the same undo, the same selection model, the same DAG history, and the same path forward to collab.

The test plan (`test/lambda/editing/`) frontloads the catalog in `cases.md` — review the catalog first, then the fixtures, then the runners. ~150 cases covering schema, position, steps, all eight tools, routing, snap, align, multi-select, mode switch, history coalescing, clipboard, and the text-frame ↔ flow interop. PM's transform tests, maxGraph's edge-style tests, tldraw's tool tests, and **BlockSuite's edgeless tests** (for the embed-in-doc canvas UX — selection, mode switch, groups/frames) provide the case-derivation source-of-truth — we port the *cases*, not the JS.

---

## 18. Roadmap — Toward Full draw.io Parity

Stage 5's first cut is the embedded-diagram MVP (the sections above). A *full* draw.io-class editor extends it along these axes; several were listed as non-goals in §1.2 and now become the Stage-5+ roadmap:

| Area | MVP (this doc) | Full draw.io-class target |
|---|---|---|
| Shape vocabulary | geometric primitives (rect, ellipse, line, poly*, path, freehand, image) + text-frame | **shape libraries / stencils** — flowchart, UML, BPMN, ER, network, electrical; pluggable via schema + view template |
| Routing | straight / orthogonal (L+Z) / curved; port + edge anchors; user waypoints | **obstacle-avoiding** orthogonal routing, segment/iso routing, jump-overs, auto-port-side selection, edge labels with placement |
| Canvas model | flow-doc **embedded** surface (fixed-size block) | optional **edgeless / infinite canvas** mode for the whole document (the deferred whole-doc whiteboard) |
| Layout aids | grid snap, object snap, align, distribute | **auto-layout** (tree / layered / org-chart), containers/swimlanes, smart connectors that re-flow on layout |
| Collaboration | stable-id + `set_attr`-only mutation (collab-*ready*) | **live multi-user** sync over the step algebra (the Stage-3 §F deferral) |
| Pen / freehand | pointer-position freehand | **pressure-sensitive** stylus input, smoothing presets |
| Theming / styles | per-shape style attrs | **named styles / stylesheet**, theme palettes, copy-style |
| Import / export | Mark round-trip; SVG/PNG export; SVG paste coercion | **.drawio / mxGraph XML** import-export, PlantUML round-trip |

Each item is additive over the MVP substrate (records + steps + templates + tools) and does not require forking the integration contract from Stage 4.

---
