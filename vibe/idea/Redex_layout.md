# Formal Layout Semantics in PLT Redex — Proposal

## Executive Summary

This proposal outlines a plan to develop an **alternative, formal specification of HTML/CSS layout algorithms** using PLT Redex — the Racket-based domain-specific language for defining and testing operational semantics. The Redex model would serve three purposes:

1. **Formal specification** — A precise, executable, mathematically-grounded definition of how layout works in Lambda/Radiant.
2. **Extensible framework** — A foundation that generalizes beyond HTML/CSS to graph layout, constraint-based layout, and future layout paradigms.
3. **Verification oracle** — An independent reference implementation to validate and debug the C++ layout engine.

---

## 1. Motivation

### 1.1 The Problem with CSS Layout

CSS layout is specified across dozens of W3C documents (CSS2.1, Flexbox, Grid, Display Level 3, Sizing Level 3, Positioning, etc.) in a mix of prose and pseudocode. The specification is:

- **Scattered** — a single layout pass touches 5+ spec documents.
- **Ambiguous** — edge cases are underspecified or defined by "browser behavior."
- **Untestable as a whole** — no single executable model exists for the full layout algorithm.

Radiant's current C++ implementation (~10,000 lines across `layout_block.cpp`, `layout_flex_multipass.cpp`, `layout_grid_multipass.cpp`, `layout_table.cpp`, `layout_inline.cpp`, `layout_positioned.cpp`, etc.) is faithful to browser behavior but lacks a formal foundation. When bugs appear, debugging requires comparing pixel output against Chrome — there is no authoritative intermediate specification to consult.

### 1.2 Why PLT Redex?

PLT Redex is purpose-built for this kind of work:

| Capability | Benefit for Layout |
|---|---|
| **define-language** | Define the grammar of the box tree, style structs, and constraints |
| **define-judgment-form** | Express layout rules as formal judgments (e.g., "given available width W, this box has height H") |
| **define-metafunction** | Implement layout algorithms (margin collapsing, flex sizing) as reusable functions |
| **define-reduction-relation** | Model multi-pass layout as state machine transitions |
| **redex-check** | Random testing: generate arbitrary box trees, run layout, check invariants |
| **traces / stepper** | Visualize layout computation step by step |
| **typesetting** | Auto-generate publication-quality figures of reduction rules |

Unlike a reimplementation in Python or JavaScript, a Redex model is **both a formal specification and a runnable program**. It can be typeset into a technical report, used for random testing, and stepped through interactively.

### 1.3 Alignment with Lambda/Radiant Goals

Lambda already has a Racket-aware philosophy (pure functional, expression-oriented, pattern matching). A Redex model fits naturally:

- Lambda's `Element` / `Map` data model maps directly to Redex `define-language` terms.
- The layout pipeline (style resolution → dispatch → block/inline/flex/grid/table → view tree) is a textbook reduction relation.
- Future expansion into graph layout (Dagre is already in `radiant/graph_dagre.cpp`) and constraint-based layout would benefit from a formal framework.

---

## 2. Feasibility Analysis

### 2.1 Prior Art

There is growing academic interest in formalizing CSS semantics:

| Work | Approach | Scope |
|---|---|---|
| **Cassius** (Pavel Panchekha et al., 2018) | SMT-based CSS verification in Rosette/Racket | Block + inline layout subset |
| **VizAssert** (same group) | Automated CSS property verification | Visual invariants |
| **Troika** (Li et al.) | Formal CSS selector semantics | Selectors only |
| **WebSSOS** (various) | Small-step semantics for web features | DOM events, not layout |

Cassius is the closest precedent: it models CSS block and inline layout in Racket (not Redex specifically, but the same ecosystem), proving that layout *can* be formalized in this way. Our model would go further by:

- Covering **Flex, Grid, and Table** layout modes.
- Using **Redex specifically** for better tooling (random testing, stepping, typesetting).
- Integrating with **a real layout engine** (Radiant) for differential testing.
- Extending to **non-HTML layout** (graph layout, constraint layout).

### 2.2 Complexity Assessment

| Layout Mode | Complexity | Redex Feasibility | Notes |
|---|---|---|---|
| **Block flow** | Medium | ✅ High | Vertical stacking + margin collapsing; well-defined rules |
| **Inline flow** | Medium-High | ✅ High | Line breaking + vertical alignment; abstracting font metrics |
| **Flexbox** | High | ✅ Medium-High | 9-phase algorithm is well-specified; maps to sequential metafunctions |
| **Grid** | High | ⚠️ Medium | Track sizing algorithm is complex but deterministic |
| **Table** | Very High | ⚠️ Medium-Low | Auto table layout is notoriously underspecified |
| **Positioned** | Medium | ✅ High | Containing block resolution is straightforward |
| **Floats** | High | ⚠️ Medium | Float interaction with BFC is complex but bounded |
| **Intrinsic sizing** | Medium | ✅ High | Min/max content as recursive measurement |

**Verdict**: Fully feasible. Block, inline, flex, and positioned layout are strong candidates for initial modeling. Grid can follow. Table layout can be modeled at a higher abstraction level (omitting browser-specific quirks).

### 2.3 Abstraction Decisions

The Redex model would intentionally abstract away:

- **Text shaping / font metrics** — Treat text as boxes with given width/height (parameterized by a measurement oracle).
- **CSS parsing / cascade** — Start from resolved computed styles, not raw CSS.
- **Pixel rounding** — Work in exact rationals, document where rounding matters.
- **Color / rendering** — Layout only; no paint step.
- **Platform specifics** — No FreeType, CoreText, or GLFW dependencies.

This mirrors the separation already present in Radiant: `resolve_css_style.cpp` handles cascade, then layout functions receive resolved `BlockProp`, `FlexProp`, `GridProp` structs.

---

## 3. Design

### 3.1 Language Definition

The core Redex language defines the box tree and style types:

```racket
(define-language CSS-Layout
  ;; === Box Tree ===
  (Box ::= (block BoxId Styles (Box ...))      ; block container
         | (inline BoxId Styles (InlineContent ...))  ; inline container
         | (flex BoxId Styles (Box ...))         ; flex container
         | (grid BoxId Styles GridDef (Box ...)) ; grid container
         | (table BoxId Styles (TableRow ...))   ; table container
         | (text BoxId Styles string number)     ; text leaf (content, measured-width)
         | (replaced BoxId Styles number number)) ; replaced element (intrinsic w, h)

  (InlineContent ::= Box | (text BoxId Styles string number))

  ;; === Styles (resolved computed values) ===
  (Styles ::= (style
                (display Display)
                (position Position)
                (width SizeValue) (height SizeValue)
                (min-width SizeValue) (min-height SizeValue)
                (max-width SizeValue) (max-height SizeValue)
                (margin Edges) (padding Edges) (border-width Edges)
                StyleExtras ...))

  (Display ::= block inline inline-block flex grid table none)
  (Position ::= static relative absolute fixed sticky)
  (SizeValue ::= auto (px number) (% number) (fr number)
               | min-content | max-content | fit-content)
  (Edges ::= (edges number number number number))  ; top right bottom left

  ;; === Flex-specific ===
  (StyleExtras ::=
    (flex-direction FlexDir) | (flex-wrap FlexWrap)
    | (justify-content JustifyContent) | (align-items AlignItems)
    | (flex-grow number) | (flex-shrink number)
    | (flex-basis SizeValue) | (align-self AlignSelf)
    | (order integer)
    ;; Grid-specific
    | (grid-template-rows (TrackSize ...))
    | (grid-template-columns (TrackSize ...))
    | (row-gap number) | (column-gap number)
    | (grid-row integer integer) | (grid-column integer integer)
    ;; empty
    | ·)

  (FlexDir ::= row row-reverse column column-reverse)
  (FlexWrap ::= nowrap wrap wrap-reverse)
  (JustifyContent ::= flex-start flex-end center space-between space-around space-evenly)
  (AlignItems ::= flex-start flex-end center baseline stretch)
  (AlignSelf ::= auto flex-start flex-end center baseline stretch)
  (TrackSize ::= auto (px number) (fr number) (minmax SizeValue SizeValue))

  ;; === Grid Definition ===
  (GridDef ::= (grid-def (TrackSize ...) (TrackSize ...)))  ; rows, columns

  ;; === Table Structure ===
  (TableRow ::= (row BoxId Styles (TableCell ...)))
  (TableCell ::= (cell BoxId Styles integer (Box ...)))  ; colspan

  ;; === Layout Result (View Tree) ===
  (View ::= (view BoxId number number number number (View ...)))
           ;; id, x, y, width, height, children
  (BoxId ::= variable-not-otherwise-mentioned)

  ;; === Available Space ===
  (AvailableSpace ::= (definite number) indefinite min-content max-content)

  ;; === Layout State ===
  (LayoutState ::= (state AvailableSpace number View ...))
                ;; available-width, current-y, accumulated views
)
```

### 3.2 Judgment Forms — Layout Rules

Layout is expressed as **judgment forms** mapping (Box, AvailableSpace) → View:

```racket
;; Main layout dispatch
(define-judgment-form CSS-Layout
  #:mode (layout I I O)        ; Box × AvailableSpace → View
  #:contract (layout Box AvailableSpace View)

  ;; Block container
  [(layout-block (block id styles (child ...)) avail view)
   ---
   (layout (block id styles (child ...)) avail view)]

  ;; Flex container
  [(layout-flex (flex id styles (child ...)) avail view)
   ---
   (layout (flex id styles (child ...)) avail view)]

  ;; Grid container
  [(layout-grid (grid id styles grid-def (child ...)) avail view)
   ---
   (layout (grid id styles grid-def (child ...)) avail view)]

  ;; Text leaf
  [(measure-text styles text-content measured-w avail view)
   ---
   (layout (text id styles text-content measured-w) avail view)]

  ;; Replaced element (img, etc.)
  [(layout-replaced id styles intrinsic-w intrinsic-h avail view)
   ---
   (layout (replaced id styles intrinsic-w intrinsic-h) avail view)])
```

### 3.3 Metafunctions — Layout Algorithms

Each layout mode is a metafunction implementing its algorithm:

```racket
;; Block layout: stack children vertically, compute content height
(define-metafunction CSS-Layout
  layout-block-children : ((Box ...) number number) -> (number (View ...))
  ;; base case: no more children
  [(layout-block-children (() avail-w current-y)) (current-y ())]
  ;; recursive case: layout child, advance y
  [(layout-block-children (((child rest ...) avail-w current-y)))
   (let ([child-view (layout child (definite avail-w))]
         [child-h (view-height child-view)]
         [new-y (+ current-y child-h)])
     (let-values ([(final-y rest-views)
                   (layout-block-children (rest ...) avail-w new-y)])
       (values final-y (cons child-view rest-views))))])
```

```racket
;; Flex sizing: resolve flexible lengths (CSS Flexbox §9.7)
(define-metafunction CSS-Layout
  resolve-flex-lengths : (FlexLine number) -> (FlexLine)
  ;; Distribute free space proportional to flex-grow
  ;; or shrink items proportional to flex-shrink × flex-basis
  ...)
```

### 3.4 Reduction Relation — Multi-Pass Layout

Multi-pass algorithms (flex 9-phase, grid track sizing) are modeled as reduction relations:

```racket
(define-extended-language Flex-Layout CSS-Layout
  (FlexState ::=
    (flex-init container items)          ; Phase 1: collect items
    (flex-sorted container items)        ; Phase 2: sort by order
    (flex-lines container lines)         ; Phase 3: create flex lines
    (flex-resolved container lines)      ; Phase 4: resolve flexible lengths
    (flex-cross-sized container lines)   ; Phase 5: cross sizes
    (flex-main-aligned container lines)  ; Phase 6: main axis alignment
    (flex-cross-aligned container lines) ; Phase 7: cross axis alignment
    (flex-content-aligned container lines) ; Phase 8: align-content
    (flex-done View)))                   ; Phase 9: final result

(define flex-reduction
  (reduction-relation Flex-Layout
    ;; Phase 1 → 2: Sort items by CSS order property
    (--> (flex-init container items)
         (flex-sorted container (sort-by-order items))
         "sort-by-order")
    ;; Phase 2 → 3: Partition into flex lines
    (--> (flex-sorted container items)
         (flex-lines container (create-flex-lines container items))
         "create-lines")
    ;; Phase 3 → 4: Resolve flexible lengths
    (--> (flex-lines container lines)
         (flex-resolved container (resolve-all-lines container lines))
         "resolve-lengths")
    ;; ... phases 4-9 ...
    ))
```

### 3.5 Module Structure

```
redex-layout/
├── README.md
├── info.rkt                       # Racket package metadata
├── css-layout-lang.rkt            # define-language: box tree + styles
├── layout-common.rkt              # Shared metafunctions (resolve-length, box-model)
├── layout-block.rkt               # Block flow layout
├── layout-inline.rkt              # Inline layout + line breaking
├── layout-flex.rkt                # Flexbox 9-phase algorithm
├── layout-grid.rkt                # Grid track sizing + placement
├── layout-table.rkt               # Table layout
├── layout-positioned.rkt          # Absolute / fixed positioning
├── layout-intrinsic.rkt           # Min-content / max-content sizing
├── layout-dispatch.rkt            # Top-level layout dispatch
├── graph-layout/                  # Non-HTML layout extensions
│   ├── graph-lang.rkt             # Graph language definition
│   ├── graph-dagre.rkt            # Dagre layered graph layout
│   └── graph-force.rkt            # Force-directed layout (future)
├── constraint-layout/             # Constraint-based layout (future)
│   ├── constraint-lang.rkt        # Constraint language definition
│   └── cassowary.rkt              # Linear constraint solver
├── tests/
│   ├── test-block.rkt             # Block layout unit tests
│   ├── test-flex.rkt              # Flex layout tests
│   ├── test-grid.rkt              # Grid layout tests
│   ├── test-invariants.rkt        # Cross-cutting property tests
│   └── test-differential.rkt      # Differential tests vs. Radiant output
└── doc/
    ├── semantics.scrbl            # Scribble document: typeset rules
    └── figures/                   # Generated reduction rule figures
```

---

## 4. Generalization Beyond HTML Layout

### 4.1 Unified Layout Algebra

The key insight is that all layout algorithms share a common pattern:

```
Layout : (Tree × Constraints × AvailableSpace) → PositionedTree
```

Where:
- **Tree** is a hierarchical structure of containers and leaves.
- **Constraints** are the styling/configuration rules (CSS, graph attributes, etc.).
- **AvailableSpace** is the space budget from the parent.
- **PositionedTree** assigns (x, y, width, height) to every node.

This pattern holds for:

| Domain | Tree | Constraints | Available Space |
|---|---|---|---|
| **HTML/CSS** | DOM + box tree | Computed CSS properties | Viewport / containing block |
| **Graph layout** | Node/edge list | Rank separation, node spacing | Bounding box |
| **Constraint layout** | Widget tree | Linear constraints (Cassowary) | Window dimensions |
| **TeX/document** | Paragraph/page tree | Badness, penalties, glue | Column width / page height |

In Redex, this unifies naturally:

```racket
(define-language Layout-Algebra
  ;; Abstract over domain-specific trees and constraints
  (LayoutTree ::= (node NodeId Properties (LayoutTree ...))
                | (leaf NodeId Properties Measurement))

  (Properties ::= (props (PropertyName PropertyValue) ...))

  (LayoutResult ::= (positioned NodeId Rect (LayoutResult ...)))
  (Rect ::= (rect number number number number)))  ; x y w h

;; Every layout mode implements:
(define-judgment-form Layout-Algebra
  #:mode (perform-layout I I I O)
  #:contract (perform-layout LayoutTree Properties AvailableSpace LayoutResult)
  ...)
```

### 4.2 Graph Layout in Redex

The existing Dagre implementation in `radiant/graph_dagre.cpp` follows a well-defined pipeline:

1. **Rank assignment** — assign each node to a layer (`dagre_assign_ranks`)
2. **Layer creation** — group nodes by rank (`dagre_create_layers`)
3. **Crossing reduction** — minimize edge crossings (`dagre_reduce_crossings`)
4. **Coordinate assignment** — position nodes within layers (`dagre_assign_coordinates`)
5. **Edge routing** — compute edge paths (`dagre_route_edges`)

This maps perfectly to a Redex reduction relation:

```racket
(define-language Graph-Layout
  (GraphState ::=
    (graph-init Nodes Edges)
    (graph-ranked RankedNodes Edges)
    (graph-layered Layers Edges)
    (graph-ordered Layers Edges)          ; crossings minimized
    (graph-positioned PositionedNodes Edges)
    (graph-routed GraphLayout))           ; final result

  (Node ::= (node NodeId Label number number))  ; id, label, width, height
  (Edge ::= (edge NodeId NodeId EdgeStyle))
  ...)

(define dagre-layout
  (reduction-relation Graph-Layout
    (--> (graph-init nodes edges)
         (graph-ranked (assign-ranks nodes edges) edges)
         "assign-ranks")
    (--> (graph-ranked ranked-nodes edges)
         (graph-layered (create-layers ranked-nodes) edges)
         "create-layers")
    (--> (graph-layered layers edges)
         (graph-ordered (reduce-crossings layers edges) edges)
         "crossing-reduction")
    (--> (graph-ordered layers edges)
         (graph-positioned (assign-coordinates layers edges) edges)
         "assign-coordinates")
    (--> (graph-positioned positioned edges)
         (graph-routed (route-edges positioned edges))
         "route-edges")))
```

### 4.3 Constraint-Based Layout (Future)

A constraint-based layout system (e.g., for GUI widget layout) would add:

```racket
(define-language Constraint-Layout
  ;; Cassowary-style linear constraints
  (Constraint ::= (= LinearExpr LinearExpr)
                | (<= LinearExpr LinearExpr)
                | (>= LinearExpr LinearExpr))
  (LinearExpr ::= number | variable
                | (+ LinearExpr LinearExpr)
                | (* number LinearExpr))
  (Strength ::= required strong medium weak))
```

This could power future Lambda features like custom layout algorithms specified in the Lambda language itself.

---

## 5. Integration with Lambda/Radiant

### 5.1 Data Exchange Format

The bridge between Radiant (C++) and Redex (Racket) uses **JSON as the interchange format**:

```
┌──────────────┐       JSON        ┌──────────────┐
│   Radiant    │  ──────────────→  │   Redex      │
│   (C++)      │  box-tree.json    │   (Racket)   │
│              │                   │              │
│  ViewBlock   │  ←──────────────  │  View terms  │
│  ViewSpan    │  layout.json      │              │
│  ViewText    │                   │              │
└──────────────┘                   └──────────────┘
```

**Box tree JSON** (Radiant → Redex): Exported after style resolution, before layout.

```json
{
  "type": "block",
  "id": "div#main",
  "styles": {
    "display": "flex",
    "flex-direction": "row",
    "width": {"px": 800},
    "height": "auto",
    "padding": {"top": 10, "right": 20, "bottom": 10, "left": 20}
  },
  "children": [
    {
      "type": "block",
      "id": "div.sidebar",
      "styles": {
        "flex-grow": 0,
        "flex-shrink": 0,
        "flex-basis": {"px": 200},
        "height": "auto"
      },
      "children": [...]
    }
  ]
}
```

**Layout result JSON** (Redex → comparison): Position data for every box.

```json
{
  "id": "div#main",
  "x": 0, "y": 0, "width": 800, "height": 400,
  "children": [
    {"id": "div.sidebar", "x": 20, "y": 10, "width": 200, "height": 380, "children": [...]}
  ]
}
```

### 5.2 Differential Testing Pipeline

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  HTML/CSS   │    │  Radiant    │    │  Redex      │
│  test case  │───→│  C++ layout │    │  layout     │
│             │    │  engine     │    │  model      │
└─────────────┘    └──────┬──────┘    └──────┬──────┘
                          │                   │
                   view_tree.json       layout.json
                          │                   │
                          └───────┬───────────┘
                                  │
                           ┌──────▼──────┐
                           │  Comparator │
                           │  (diff)     │
                           └──────┬──────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │  Match: ✅ Verified        │
                    │  Mismatch: 🔍 Investigate  │
                    └───────────────────────────┘
```

Steps:

1. **Export box tree**: Add a `--export-box-tree` flag to `lambda.exe layout` that dumps the resolved box tree (after `resolve_css_styles()`, before `layout_block()`) to JSON.
2. **Run Redex model**: Feed JSON to the Racket model, which outputs its own layout result.
3. **Run Radiant**: Normal `lambda.exe layout` produces `view_tree.json`.
4. **Compare**: A script diffs the two outputs, tolerating small floating-point differences.

### 5.3 C++ Integration Points

Minimal changes to Radiant are needed:

| File | Change | Purpose |
|---|---|---|
| `radiant/layout.cpp` | Add `export_box_tree_json()` | Serialize resolved box tree to JSON |
| `radiant/layout_block.cpp` | Hook after `resolve_css_styles()` | Capture pre-layout state |
| `lambda/main.cpp` | Add `--export-box-tree` CLI flag | Trigger export mode |
| `lambda/format/format-json.cpp` | Reuse JSON serializer | Box tree → JSON output |

### 5.4 Test Infrastructure

```bash
# Run a single differential test
./lambda.exe layout test/layout/data/flex-basic.html --export-box-tree temp/box-tree.json
racket redex-layout/run-layout.rkt temp/box-tree.json > temp/redex-layout.json
python3 utils/compare-layouts.py view_tree.json temp/redex-layout.json

# Run all differential tests
make test-redex-differential
```

---

## 6. Implementation Plan

### Phase 1: Core Language & Block Layout (2–3 weeks)

**Goal**: Define the Redex language and implement block flow layout.

- [ ] Set up Racket project structure (`redex-layout/`)
- [ ] Define `CSS-Layout` language (box tree, styles, view types)
- [ ] Implement `layout-common.rkt` (box model computation, length resolution)
- [ ] Implement `layout-block.rkt` (vertical stacking, margin collapsing)
- [ ] Implement `layout-intrinsic.rkt` (min/max content width)
- [ ] Write unit tests for block layout cases
- [ ] Add JSON import/export for box trees

**Deliverable**: Block layout model that handles `<div>` stacking, padding, margins, and width/height resolution.

### Phase 2: Inline Layout & Text (2 weeks)

**Goal**: Model inline formatting context and line breaking.

- [ ] Implement `layout-inline.rkt` (line box management, wrapping)
- [ ] Abstract text measurement (parameterized width function)
- [ ] Implement vertical-align within line boxes
- [ ] Handle inline-block interaction
- [ ] Half-leading model

**Deliverable**: Inline layout with line breaking, matching Radiant's `layout_inline.cpp` behavior.

### Phase 3: Flexbox (3 weeks)

**Goal**: Full 9-phase flex algorithm in Redex.

- [ ] Implement `layout-flex.rkt` with reduction relation for 9 phases
- [ ] Flex item collection and order sorting
- [ ] Flex line creation (wrapping)
- [ ] Flexible length resolution (grow/shrink distribution)
- [ ] Main and cross axis alignment
- [ ] Multi-line alignment (`align-content`)
- [ ] Nested flex containers
- [ ] `redex-check` property tests (e.g., "items never exceed container", "grow fills space")

**Deliverable**: Flex layout model matching `layout_flex_multipass.cpp`.

### Phase 4: Grid Layout (3 weeks)

**Goal**: Grid track sizing and item placement.

- [ ] Implement `layout-grid.rkt`
- [ ] Track definition parsing (px, fr, auto, minmax)
- [ ] Explicit and auto placement
- [ ] Track sizing algorithm
- [ ] Fr unit distribution
- [ ] Grid alignment

**Deliverable**: Grid layout model matching `layout_grid_multipass.cpp`.

### Phase 5: Integration & Differential Testing (2 weeks)

**Goal**: End-to-end differential testing pipeline.

- [ ] Add `export_box_tree_json()` to Radiant
- [ ] Build JSON → Redex term importer
- [ ] Build comparison tool (`utils/compare-layouts.py`)
- [ ] Run against `test/layout/data/` test suite
- [ ] Document discrepancies and create regression tests

### Phase 6: Graph Layout Extension (2 weeks)

**Goal**: Formalize graph layout in the same framework.

- [ ] Define `Graph-Layout` language
- [ ] Implement Dagre algorithm phases as reduction relation
- [ ] Connect to existing `radiant/graph_dagre.cpp` for differential testing
- [ ] Demonstrate shared `Layout-Algebra` abstraction

### Phase 7: Documentation & Publication (1 week)

**Goal**: Typeset formal rules for technical documentation.

- [ ] Use Redex typesetting to generate rule figures
- [ ] Write Scribble documentation
- [ ] Add to Lambda's `doc/` folder

**Total estimated effort: 15–16 weeks** (part-time, interleaved with other work).

---

## 7. Properties & Invariants for Random Testing

One of Redex's strongest features is `redex-check` — random testing against properties. Layout has many universal invariants:

### 7.1 Structural Invariants

```racket
;; Every child's bounding box fits within its parent's content area
(redex-check CSS-Layout
  Box
  (let ([view (layout Box (definite 800))])
    (all-children-within-parent? view)))

;; No two non-overlapping siblings overlap in the same flow
(redex-check CSS-Layout
  (block id styles (child_1 child_2))
  (let ([view (layout (block id styles (child_1 child_2)) (definite 800))])
    (no-sibling-overlap? view)))

;; Layout is deterministic: same input always produces same output
(redex-check CSS-Layout
  Box
  (equal? (layout Box (definite 800))
          (layout Box (definite 800))))
```

### 7.2 Flex-Specific Properties

```racket
;; flex-grow fills all available space
(redex-check Flex-Layout
  (flex id styles items)
  (let ([view (layout (flex id styles items) (definite 1000))])
    (when (any-item-has-flex-grow? items)
      (<= (abs (- (sum-child-widths view) (content-width view))) 1))))

;; flex-shrink never produces negative sizes
(redex-check Flex-Layout
  (flex id styles items)
  (let ([view (layout (flex id styles items) (definite 100))])  ; small container
    (all-children-non-negative? view)))
```

### 7.3 Grid-Specific Properties

```racket
;; Fr units distribute proportionally
(redex-check Grid-Layout
  grid-box
  (let ([view (layout grid-box (definite 600))])
    (fr-tracks-proportional? view)))

;; No grid item overflows its track boundaries
(redex-check Grid-Layout
  grid-box
  (let ([view (layout grid-box (definite 800))])
    (items-within-tracks? view)))
```

---

## 8. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| CSS spec ambiguities cause Redex/Radiant divergence | High | Medium | Document divergences as spec questions; treat Radiant (browser-compatible) as authoritative for edge cases |
| Text measurement abstraction loses fidelity | Medium | Low | Parameterize text as (string → width) function; import real measurements from FreeType when needed |
| Redex performance too slow for large documents | Medium | Low | Model is for correctness, not performance; test with small/medium cases (< 100 boxes) |
| Maintaining two implementations becomes burden | Medium | Medium | Automate differential testing; model only the algorithm, not the full engine |
| Racket/Redex learning curve | Low | Low | Team already familiar with functional programming; Redex is well-documented |

---

## 9. Expected Outcomes

1. **A formal, executable specification** of CSS Block, Inline, Flex, Grid, and Positioned layout in ~3,000–5,000 lines of Racket/Redex code.

2. **A generalized layout algebra** showing how HTML layout, graph layout, and constraint layout share a common structure.

3. **An automated differential testing pipeline** that catches regressions by comparing Radiant's C++ output against the Redex model.

4. **Publication-quality typeset rules** for inclusion in Lambda's technical documentation.

5. **A foundation for future layout modes** — any new layout paradigm (constraint-based UI, TeX-style paragraph breaking, etc.) can be specified in Redex first, then implemented in C++.

---

## 10. References

- [PLT Redex Documentation](https://docs.racket-lang.org/redex/)
- Felleisen, Findler, Flatt — *Semantics Engineering with PLT Redex* (MIT Press, 2009)
- Panchekha et al. — *Verifying That Web Pages Have Accessible Layout* (PLDI 2018) — Cassius
- [CSS 2.2 Visual Formatting Model](https://www.w3.org/TR/CSS22/visuren.html)
- [CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Grid Layout Module Level 2](https://www.w3.org/TR/css-grid-2/)
- [CSS Intrinsic & Extrinsic Sizing Module Level 3](https://www.w3.org/TR/css-sizing-3/)
- Radiant Layout Design Document: `doc/Radiant_Layout_Design.md`
- Radiant Graph Layout Proposal: `vibe/Radiant_Graph_Layout.md`
