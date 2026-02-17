# Redex Layout Phase 2 — Expanding to Full HTML Layout Coverage

> Proposal for extending the PLT Redex CSS layout specification from 508 to 1000+ differential tests, covering `<style>` blocks, inline/inline-block, table layout, float, text content, and multi-suite test ingestion.

---

## Table of Contents

1. [Current State & Motivation](#1-current-state--motivation)
2. [Test Landscape — What's Available](#2-test-landscape--whats-available)
3. [Key Architectural Change: JSON-Computed-Style Import](#3-key-architectural-change-json-computed-style-import)
4. [Feature Roadmap — 6 Phases](#4-feature-roadmap--6-phases)
5. [Phase 2A: Low-Hanging Fruit — Flex & Grid Expansion (est. +340 tests)](#5-phase-2a-low-hanging-fruit--flex--grid-expansion)
6. [Phase 2B: Inline & Inline-Block Layout (est. +100 tests)](#6-phase-2b-inline--inline-block-layout)
7. [Phase 2C: Table Layout (est. +100 tests)](#7-phase-2c-table-layout)
8. [Phase 2D: Float Layout (est. +60 tests)](#8-phase-2d-float-layout)
9. [Phase 2E: Text & Font Measurement (est. +50 tests)](#9-phase-2e-text--font-measurement)
10. [Phase 2F: Advanced / Remaining (est. +50 tests)](#10-phase-2f-advanced--remaining)
11. [Implementation Plan & Milestones](#11-implementation-plan--milestones)
12. [File Changes Summary](#12-file-changes-summary)

---

## 1. Current State & Motivation

### Phase 1 Achievement

| Metric | Value |
|--------|-------|
| Differential tests (Phase 1) | **508/508 (100%)** |
| Differential tests (Phase 2A) | **621/709 (87.6%)** |
| Dedicated flex suite | **153/156 (98.1%)** |
| Dedicated grid suite | **117/123 (95.1%)** |
| Dedicated box suite | **68/77 (88.3%)** |
| Unit tests | 40/40 (100%) |
| Test source | 10 suites: baseline, flex, grid, position, table, basic, box, page, flex-nest, text_flow |
| Layout modes | Block, Flex, Grid, Positioned, Inline (partial) |
| Total Redex code | ~7,200 lines across 14 `.rkt` files |

### The Gap

The current 508 tests come from a carefully filtered subset of the `baseline/` directory — only tests that:
- Use inline `style=""` attributes (no `<style>` blocks)
- Contain only `<div>` elements (no `<span>`, `<img>`, `<p>`, `<table>`)
- Have no visible text content

This means **2,533 additional tests** with reference JSONs exist across all suites but are currently unreachable.

### Motivation

1. **Verification breadth** — The C++ Radiant engine has separate flex (156) and grid (123) test suites. The Redex model now passes **153/156 flex** and **117/123 grid** tests, serving as a verified second reference.
2. **Real-world coverage** — Production HTML uses `<style>` blocks, inline/inline-block elements, tables, floats, and text. The current model only covers synthetic flex/grid tests.
3. **Specification completeness** — A layout specification that can't handle `<p>` or `<span>` is incomplete as a CSS reference.

---

## 2. Test Landscape — What's Available

### Tests Per Suite (with reference JSONs)

| Suite | Total Files | With Ref JSON | Key Display Modes |
|-------|-------------|---------------|-------------------|
| **baseline** | 1,972 | 1,972 | block, flex, grid, table, inline |
| **table** | 284 | 284 | table, table-row, table-cell |
| **box** | 185 | 185 | block, inline, inline-block, float |
| **flex** | 156 | 156 | flex (advanced: aspect-ratio, absolute) |
| **basic** | 153 | 153 | block, inline, inline-block, table |
| **grid** | 123 | 123 | grid (advanced: auto-flow, minmax) |
| **position** | 104 | 104 | absolute, float, inline-block |
| **page** | 42 | 42 | mixed real-world HTML pages |
| **text_flow** | 14 | 14 | block with real text & fonts |
| **flex-nest** | 8 | 8 | nested flex with inline content |
| **Grand Total** | **3,041** | **3,041** | |

### Currently Tested: 508 / Potential: 3,041

### Tests By Required Layout Mode

| Layout Mode Required | Test Count | Notes |
|---------------------|------------|-------|
| **block-only** | 873 | Already supported — just needs JSON import |
| **flex** (no grid) | 835 | Already supported — needs JSON import + some new features |
| **table** (any table-*) | 422 | New layout mode needed |
| **inline** (inline/inline-block) | 386 | Partially implemented, needs expansion |
| **flex + grid** | 245 | Already supported |
| **grid** (no flex) | 60 | Already supported |
| **list-item** | 64 | Minor: block with marker offset |
| **inline-table** | 18 | Hybrid: inline placement + table internals |

### Why Baseline Has 1,972 Files But Only 508 Were Tested

The baseline directory contains both `.html` (1,009) and `.htm` (963) files:
- The 963 `.htm` files are **CSS 2.1 test suite** ports (W3C conformance tests) — heavily using `<style>`, `<span>`, `<p>`, `<table>`, text content
- Of the 1,009 `.html` files, 507 are "simple" (inline-style `<div>` only) and 502 are "complex" (have `<style>`, `<span>`, `<img>`, or text)

Breakdown of the 502 non-simple `.html` files:
- **454** have `<style>` blocks
- **236** have visible text content
- **177** have both `<style>` + text
- **11** use `<span>` elements
- **2** use `<img>` elements

---

## 3. Key Architectural Change: JSON-Computed-Style Import

### The Breakthrough Insight

Every reference JSON already contains **computed CSS styles** for every DOM node — `display`, `position`, margins, paddings, borders, flex properties, font-size, line-height, and more. This means:

> **We don't need to parse `<style>` blocks or implement CSS cascade at all.**
> We can read the browser-computed styles directly from the reference JSON.

### Current Pipeline (Phase 1)

```
HTML file ──(parse inline styles)──► Redex Box Tree
                                         │
JSON file ──(extract positions)────► Expected Layout
                                         │
                              Redex Layout Engine
                                         │
                                    Compare ◄──────── Expected
```

The HTML parser only handles `style="..."` inline attributes. It cannot parse `<style>` blocks or resolve CSS selectors.

### New Pipeline (Phase 2)

```
JSON file ──(read computed styles)──► Redex Box Tree  ← NEW
         ──(read layout positions)──► Expected Layout
                                         │
HTML file ──(extract text content)──► Text measurements ← NEW
                                         │
                              Redex Layout Engine
                                         │
                                    Compare ◄──────── Expected
```

The JSON reference file becomes the **sole source of truth** for CSS styles. The HTML file is only needed for:
1. **Text content extraction** — the JSON doesn't include text node content
2. **Classification** — determining which box types and features a test requires
3. **Font identification** — knowing which font is used for text measurement

### `reference-import-v2.rkt` — New JSON-Based Importer

A new module `reference-import-v2.rkt` will:

1. **Walk the JSON `layout_tree`** node by node
2. **Read `computed` styles** from each node → produce Redex `Styles` terms
3. **Map `display` values** to box types:
   - `block` → `(block ...)`
   - `flex` / `inline-flex` → `(flex ...)`
   - `grid` / `inline-grid` → `(grid ...)`
   - `inline` → `(inline ...)`
   - `inline-block` → `(inline-block ...)`
   - `table` → `(table ...)`
   - `table-row` → `(row ...)`
   - `table-cell` → `(cell ...)`
   - `none` → `(none ...)`
   - `list-item` → `(block ...)` with list-item marker
4. **Extract text content** from the HTML file to annotate text nodes
5. **Build the expected layout tree** from `layout` coordinates

Key advantage: **Zero CSS parsing needed.** The browser already did all cascade resolution, specificity, inheritance, and shorthand expansion. We get the final computed values for free.

### Computed Style Fields Available in JSON

```json
{
  "computed": {
    "display": "flex",
    "position": "relative",
    "marginTop": 0, "marginRight": 0, "marginBottom": 0, "marginLeft": 0,
    "paddingTop": 10, "paddingRight": 10, "paddingBottom": 10, "paddingLeft": 10,
    "borderTopWidth": 2, "borderRightWidth": 2, "borderBottomWidth": 2, "borderLeftWidth": 2,
    "flexDirection": "row", "flexWrap": "nowrap",
    "justifyContent": "normal", "alignItems": "normal", "alignContent": "normal",
    "flexGrow": 0, "flexShrink": 1, "flexBasis": "auto",
    "alignSelf": "auto", "order": 0,
    "gap": "normal",
    "top": "auto", "right": "auto", "bottom": "auto", "left": "auto",
    "overflow": "visible", "overflowX": "visible", "overflowY": "visible",
    "zIndex": "auto",
    "fontSize": "16px", "fontFamily": "Arial, sans-serif",
    "fontWeight": "400", "lineHeight": "normal",
    "textAlign": "start", "verticalAlign": "baseline"
  }
}
```

This covers **every CSS property** our layout engine needs, including flex, grid, positioning, box model, and typography properties.

---

## 4. Feature Roadmap — 6 Phases

```
Phase 2A: Flex/Grid Expansion (JSON import)     ──── est. +340 tests ──── Weeks 1-2
    │
Phase 2B: Inline & Inline-Block                 ──── est. +100 tests ──── Weeks 2-3
    │
Phase 2C: Table Layout                          ──── est. +100 tests ──── Weeks 3-5
    │
Phase 2D: Float Layout                          ──── est. +60 tests  ──── Weeks 5-6
    │
Phase 2E: Text & Font Measurement               ──── est. +50 tests  ──── Weeks 6-7
    │
Phase 2F: Advanced / Remaining                   ──── est. +50 tests  ──── Weeks 7-8
    │
    ▼
Target: 508 + ~700 = 1,200+ tests (40%+ of 3,041)
```

---

## 5. Phase 2A: Low-Hanging Fruit — Flex & Grid Expansion (est. +340 tests)

### ✅ Phase 2A Status: COMPLETE

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Total tests discovered | 508 | **709** | +201 |
| Tests passing | 508 | **621** | **+113** |
| Tests failing | 0 | 88 | — |
| Errors | 0 | 0 | — |
| Pass rate (all suites) | 100% | **87.6%** | — |

**Comparison Rules:**

| Rule | Description |
|------|-------------|
| **Tree structure** | 100% element and text node match — child count (including text nodes) must be identical between actual and expected trees. Any structural mismatch is an immediate failure. |
| **Numeric tolerance** | $\text{tol} = \min\bigl(\max(3,\; 0.03 \times |\text{ref}|),\; 10\bigr)$ — base 3 px, proportional 3% of reference value, hard cap 10 px. Applied to `x`, `y`, `width`, `height` of every node. |

**Per-suite breakdown:**

| Suite | Found | Pass | Fail | Pass Rate |
|-------|-------|------|------|-----------|
| baseline | 532 | 513 | 19 | 96.4% |
| flex | 109 | 83 | 26 | 76.1% |
| grid | 55 | 24 | 31 | 43.6% |
| position | 12 | 0 | 12 | 0% (all float tests) |
| other (basic, box, page, etc.) | 1 | 0 | 1 | — |

**What was implemented:**

1. **Multi-suite test discovery** — `test-differential.rkt` now supports `--suite` flag (comma-separated or "all"), discovering tests across 10 suites with `.html` and `.htm` extension support.
2. **Percentage margin/padding resolution** — `parse-edge-shorthand` and `parse-margin-shorthand` now preserve `(% N)` values; `extract-box-model` resolves them against containing block inline-size; all 15+ call sites across 7 files updated.
3. **Aspect-ratio in positioned layout** — `layout-positioned.rkt` now derives missing dimension from aspect-ratio for absolutely positioned elements (explicit, inset, or min-width derived).
4. **Flex min/max main-axis re-resolve** — When indefinite main-axis is clamped by min-height/max-height, flex items are re-resolved with the constrained size as definite (Phase 5b in flex algorithm).
5. **Grid fr track sizing freeze algorithm** — CSS Grid §12.7.1 compliant: tracks whose base-size exceeds proportional share are frozen iteratively.
6. **Grid fr sub-1 sum** — When sum of flex factors < 1, the divisor is clamped to 1 per CSS Grid spec.
7. **Display type extension** — `inline-flex` → flex, table display types → block approximation.
8. **Grid percentage padding resolution** — Fixed `(% N)` values leaking into arithmetic in grid branch of `element->box-tree`.

**Remaining 88 failures by category:**

| Category | Count | Root Cause |
|----------|-------|------------|
| Floats (baseline + position) | 31 | Float layout not implemented |
| Grid (various) | 32 | Track sizing, absolute positioning in grid areas, alignment |
| Flex (various) | 14 | Wrapped row sizing, overflow clamping, bevy edge cases |
| Aspect-ratio + min/max | 7 | min-width/min-height interaction with aspect-ratio |
| Block-in-inline | 3 | Inline formatting context not implemented |
| Other | 2 | Percentage nesting edge cases |

### Goal

Enable Redex to test against the `flex/` (156), `grid/` (123), and unlocked baseline tests that use `<style>` blocks but only need block+flex+grid layout (no inline/table/text).

### Why This Is Low-Hanging Fruit

The flex and grid suites use the same inline-style `<div>` pattern as the current 508 tests, but live in a different directory. 109 of the 156 flex tests and 55 of the 123 grid tests are "simple" (no `<style>`, no text). They're structurally identical to what we already handle — they just need to be discovered and fed to the engine.

Additionally, ~200 baseline tests use `<style>` blocks but only flex/grid/block display types. With the JSON-computed-style importer, these become testable immediately.

### Implementation

#### 5.1 Multi-Suite Test Discovery

Modify `test-differential.rkt` to support multiple test directories:

```racket
;; New: configurable test source directories
(define test-suites
  '(("baseline" . "layout/data/baseline")
    ("flex"     . "layout/data/flex")
    ("grid"     . "layout/data/grid")
    ("basic"    . "layout/data/basic")
    ("box"      . "layout/data/box")
    ("position" . "layout/data/position")
    ("table"    . "layout/data/table")
    ("text_flow" . "layout/data/text_flow")
    ("flex-nest" . "layout/data/flex-nest")))
```

Add `--suite` CLI option to select which suites to run.

#### 5.2 JSON-Computed-Style Importer (`reference-import-v2.rkt`)

New module that reads the reference JSON's `computed` object to build the Redex box tree:

```racket
;; Core function: JSON node → Redex box term
(define (json-node->box node parent-styles)
  (define comp (hash-ref node 'computed (hash)))
  (define display (hash-ref comp 'display "block"))
  (define id (generate-box-id node))
  (define styles (computed->redex-styles comp))
  (define children (hash-ref node 'children '()))
  
  (case (classify-display display)
    [(block) `(block ,id ,styles ,(map-children children styles))]
    [(flex)  `(flex ,id ,styles ,(map-children children styles))]
    [(grid)  `(grid ,id ,styles ,(grid-def-from-computed comp) ,(map-children children styles))]
    [(inline) `(inline ,id ,styles ,(map-inline-children children styles))]
    [(inline-block) `(inline-block ,id ,styles ,(map-children children styles))]
    [(table) `(table ,id ,styles ,(map-table-children children styles))]
    [(none)  `(none ,id)]
    [else    `(block ,id ,styles ,(map-children children styles))]))
```

Key functions:
- `computed->redex-styles` — Map JSON computed values to Redex `(style ...)` terms
- `classify-display` — Group CSS display values into box types
- `map-table-children` — Handle table-row-group / table-row / table-cell hierarchy
- `grid-def-from-computed` — Extract grid template definitions (requires additional JSON fields or HTML parsing fallback)

#### 5.3 Feature Gaps to Fix in Existing Layout Modules

Several flex/grid features that work in the baseline 508 may need fixes for the expanded test suites:

| Feature | Tests Affected | Module |
|---------|---------------|--------|
| `aspect-ratio` property | ~15 flex tests | `layout-flex.rkt` |
| Absolute in flex: inset-based sizing | ~11 flex tests | `layout-positioned.rkt` |
| Grid `auto-flow: dense` | ~5 grid tests | `layout-grid.rkt` |
| Grid `minmax()` track sizing | ~10 grid tests | `layout-grid.rkt` |
| Grid percentage sizing | ~8 grid tests | `layout-grid.rkt` |
| `align-items: baseline` | ~10 flex tests | `layout-flex.rkt` |
| Negative-space `justify-content` | ~4 flex tests | `layout-flex.rkt` |
| `wrap-reverse` cross-axis | ~10 flex tests | `layout-flex.rkt` |

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| `flex/` suite (simple) | ~109 |
| `grid/` suite (simple) | ~55 |
| Baseline (style-block, block/flex/grid only) | ~180 |
| **Subtotal** | **~340** |
| **Cumulative** | **~850** |

---

## 6. Phase 2B: Inline & Inline-Block Layout (est. +100 tests)

### Goal

Support `display: inline` and `display: inline-block` elements so that tests with `<span>`, `<a>`, `<strong>`, `<em>`, `<code>` elements can pass.

### Current State

`layout-inline.rkt` (167 lines) has a basic skeleton with:
- Simple greedy line-breaking
- Fixed 20px line height
- Inline-block placement
- No vertical-align support
- No proper inline formatting context

### What's Needed

#### 6.1 Inline Formatting Context (IFC)

Implement proper CSS 2.2 §9.4.2:

```
Line Box Formation:
  1. Collect inline-level boxes into a line box
  2. Each line box spans the full available width
  3. Break lines when content exceeds available width
  4. Vertical-align positions content within each line box

Key concepts:
  - Strut: invisible zero-width inline box setting minimum line height
  - Baseline: alignment reference for inline boxes
  - Line-height: half-leading model
  - Vertical-align: baseline, top, middle, bottom, sub, super, <length>
```

#### 6.2 Inline Box Model

Inline elements have different box model rules:
- Horizontal margins, padding, borders apply normally
- Vertical margins don't apply
- Vertical padding/border don't affect line height
- Content contributes to line height via `line-height` property

#### 6.3 Inline-Block

Inline-block is simpler — it's a block box placed inline:
- Lay out internally as a block
- Place as a single atomic inline-level box
- Participates in vertical-align of the line box
- Margins apply normally

#### 6.4 CSS Language Extensions

Add to `css-layout-lang.rkt`:
```racket
;; Extended display types
(Display ::= block | inline | inline-block | flex | grid | table | none
           | inline-flex | inline-grid | list-item
           | table-row | table-cell | table-row-group | table-column
           | table-column-group | table-caption | table-header-group
           | table-footer-group | inline-table)
```

#### 6.5 Affected Test Suites

| Suite | Tests Needing Inline | Notes |
|-------|---------------------|-------|
| baseline (.htm) | ~145 | `<span>`, `<p>` with inline children |
| basic | ~79 | `<span>`, `<input>` elements |
| box | ~103 | `<span>` heavily used |
| position | ~15 | `<span>` in positioning contexts |

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| Baseline (inline/inline-block only) | ~50 |
| Basic suite | ~25 |
| Box suite | ~25 |
| **Subtotal** | **~100** |
| **Cumulative** | **~950** |

---

## 7. Phase 2C: Table Layout (est. +100 tests)

### Goal

Implement CSS table layout so that the 284-test `table/` suite and ~200 baseline table tests become testable.

### Complexity Assessment

Table layout is the **most complex new layout mode** to implement. It has unique algorithms for:
- Column width distribution (fixed vs. auto layout)
- Row height distribution
- Cell spanning (colspan/rowspan)
- Border collapse vs. separate
- Caption placement
- Anonymous table object generation

### What's Needed

#### 7.1 Table Layout Algorithm (`layout-table.rkt`)

Implement CSS 2.2 §17:

```
Table Layout Algorithm:
  1. Determine column widths
     - Fixed layout (table-layout: fixed): use first row's explicit widths
     - Auto layout: compute min/max content widths per column
  2. Distribute remaining width to columns
  3. Determine row heights
     - Each cell contributes to its row's height
     - Spanning cells distribute height across spanned rows
  4. Position cells within their row/column intersections
  5. Handle border-spacing / border-collapse
  6. Position captions above/below table
```

#### 7.2 Data Structures

```racket
;; Table-specific structures
(struct table-column (index width min-width max-width) #:transparent #:mutable)
(struct table-row (index height cells) #:transparent #:mutable)
(struct table-cell-info (row col rowspan colspan box) #:transparent)
```

#### 7.3 Table Box Tree Construction

The JSON-computed-style importer needs special handling for table elements:

```
display: table           → (table id styles table-children)
display: table-row-group → (row-group id styles (rows...))
display: table-row       → (row id styles (cells...))
display: table-cell      → (cell id styles colspan (children...))
display: table-caption   → (caption id styles (children...))
```

#### 7.4 Anonymous Table Object Generation

CSS 2.2 §17.2.1 requires browsers to insert anonymous table objects when table elements are used without proper parent/child relationships. The reference JSON already reflects this (browsers generate anonymous wrappers), so we need to either:
- **Option A**: Detect and skip anonymous wrappers in comparison (simpler)
- **Option B**: Generate anonymous wrappers in our box tree (more correct)

Recommendation: **Option A** initially, **Option B** for the full table-anonymous-objects tests.

#### 7.5 Implementation Phases

| Sub-Phase | Tests | Complexity |
|-----------|-------|-----------|
| Fixed table layout (explicit widths) | ~30 | Low |
| Auto table layout (column distribution) | ~40 | Medium |
| Border-spacing / collapse | ~20 | Medium |
| Colspan/rowspan | ~30 | High |
| Caption positioning | ~10 | Low |
| Anonymous table objects | ~100+ | High (defer to 2F) |

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| Table suite (core) | ~80 |
| Baseline table tests | ~20 |
| **Subtotal** | **~100** |
| **Cumulative** | **~1,050** |

---

## 8. Phase 2D: Float Layout (est. +60 tests)

### Goal

Implement CSS float layout for the `position/` suite (59 float tests) and baseline float tests.

### What's Needed

#### 8.1 Float Algorithm

CSS 2.2 §9.5:

```
Float Layout:
  1. Remove floated box from normal flow
  2. Shift to left/right edge of containing block
  3. Float stacks: subsequent floats shift past previous ones
  4. Clear property: move below cleared floats
  5. BFC (Block Formatting Context): containers with overflow != visible
     contain their floats
  6. Float intrusion: non-float block content wraps around floats
```

#### 8.2 Float Context

```racket
;; Track active floats for a block formatting context
(struct float-state
  (left-floats     ; list of (x y width height) — active left floats
   right-floats    ; list of (x y width height) — active right floats
   clear-y)        ; y position below all cleared floats
  #:transparent #:mutable)
```

#### 8.3 Integration Points

- `layout-block.rkt`: Before positioning a child, check `float` property
  - If `float: left/right`, position via float algorithm instead of normal flow
  - Non-float children: reduce available width by active float intrusion
- `layout-common.rkt`: Add `clear` property handling
- `reference-import-v2.rkt`: Read `float` computed style (not available in current JSON — may need to add `cssFloat` to extraction script, or detect from layout positions)

#### 8.4 Scope Limitation

Float is notoriously complex. For Phase 2, focus on:
- ✅ Basic left/right float positioning
- ✅ Float stacking (multiple floats)
- ✅ Clear property
- ✅ BFC containment (overflow: hidden/auto)
- ❌ Negative margins on floats (defer)
- ❌ Float interaction with inline content (defer)
- ❌ Float interaction with table layout (defer)

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| Position suite (float tests) | ~40 |
| Baseline float tests | ~20 |
| **Subtotal** | **~60** |
| **Cumulative** | **~1,110** |

---

## 9. Phase 2E: Text & Font Measurement (est. +50 tests)

### Goal

Enable tests with text content to pass by implementing font metric-based text measurement beyond the Ahem font.

### Current State

Phase 1 added Ahem font support (all glyphs = 1em × 1em square). This handles ~50 text tests in the baseline. But the remaining ~200+ text tests use real fonts (Arial, Liberation Sans, serif, etc.).

### What's Needed

#### 9.1 Font Metric Extraction

Extract font metrics for commonly used test fonts:

| Font | Usage | Source |
|------|-------|--------|
| **Liberation Sans** | text_flow suite (14 tests) | `test/layout/data/font/LiberationSans-*.ttf` |
| **Arial** | baseline & basic | System font / metric table |
| **serif/sans-serif** | CSS 2.1 tests | Default fallback metrics |

Use `fontTools` (Python) to extract per-font:
- Units per em
- Ascender / descender / line-gap
- Per-glyph advance widths (or average character width)
- Space width, word-spacing

Store as JSON files: `test/redex/font-metrics/liberation-sans.json`, etc.

#### 9.2 Text Measurement Engine

```racket
;; measure-text : string font-name font-size → number (width in px)
(define (measure-text content font-name font-size metrics-table)
  (define upem (hash-ref metrics-table 'units-per-em 1000))
  (define scale (/ font-size upem))
  (for/sum ([ch (in-string content)])
    (define glyph-width (hash-ref (hash-ref metrics-table 'widths (hash))
                                   ch
                                   (hash-ref metrics-table 'default-width 500)))
    (* glyph-width scale)))
```

#### 9.3 Line Breaking

For multi-line text, implement:
1. Word-based breaking (split at spaces)
2. Per-word width measurement
3. Greedy line-fill algorithm
4. Line height from `line-height` × `font-size`

#### 9.4 Integration

- `reference-import-v2.rkt`: Extract text node content from HTML, create `(text id styles content measured-w)` box terms
- `layout-dispatch.rkt`: Use font metrics for text layout instead of hardcoded Ahem
- Font selection: Use `fontFamily` from computed styles to select the right metric table

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| text_flow suite | ~14 |
| Baseline text tests (non-Ahem) | ~30 |
| Basic suite (text) | ~6 |
| **Subtotal** | **~50** |
| **Cumulative** | **~1,160** |

---

## 10. Phase 2F: Advanced / Remaining (est. +50 tests)

### 10.1 `list-item` Display

64 tests use `list-item` display. Implementation is simple:
- Treat as `block` with a marker box
- Marker is positioned to the left (outside) or inside the content box
- Marker width ≈ 1.5em for bullet, varies for numbered lists

### 10.2 `overflow: hidden/scroll/auto`

Several tests rely on `overflow` to establish a Block Formatting Context (BFC):
- BFC containers contain floats
- BFC containers don't have margin collapse with children
- Clipping doesn't affect layout (just rendering)

### 10.3 Remaining Flex/Grid Edge Cases

Some flex/grid tests from Phase 2A may need additional feature work:
- `aspect-ratio` property (if not completed in 2A)
- `baseline` alignment for flex items
- Grid `subgrid` (if any tests require it)
- `writing-mode: vertical-*` (very few tests)

### 10.4 Anonymous Table Object Generation

The table suite has ~110 `table-anonymous-objects-*.htm` tests that test CSS's rules for generating anonymous table wrappers. These are specialized tests that require understanding CSS 2.2 §17.2.1.

### 10.5 Page Layout Tests

The `page/` suite (42 tests) contains real-world HTML pages with mixed layout modes. These will gradually become passable as inline, table, and float support matures.

### Expected Outcome

| Source | Tests Added |
|--------|-------------|
| List-item tests | ~20 |
| BFC / overflow tests | ~15 |
| Page suite (partial) | ~15 |
| **Subtotal** | **~50** |
| **Cumulative** | **~1,210** |

---

## 11. Implementation Plan & Milestones

### Phase Dependency Graph

```
                    ┌─────────────────────────────┐
                    │  JSON-Computed-Style Import  │
                    │  (reference-import-v2.rkt)   │
                    │  + Multi-suite discovery     │
                    └──────────┬──────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
     ┌────────────┐   ┌────────────┐   ┌────────────┐
     │  Phase 2A  │   │  Phase 2B  │   │  Phase 2C  │
     │  Flex/Grid │   │  Inline    │   │  Table     │
     │  Expansion │   │  Layout    │   │  Layout    │
     └─────┬──────┘   └─────┬──────┘   └─────┬──────┘
           │                │                │
           └────────────────┼────────────────┘
                            ▼
              ┌────────────────────────────┐
              │  Phase 2D: Float Layout    │
              │  (depends on block + BFC)  │
              └──────────┬─────────────────┘
                         ▼
              ┌────────────────────────────┐
              │  Phase 2E: Text & Fonts    │
              │  (independent, can start   │
              │   earlier in parallel)     │
              └──────────┬─────────────────┘
                         ▼
              ┌────────────────────────────┐
              │  Phase 2F: Advanced        │
              └────────────────────────────┘
```

### Milestone Targets

| Milestone | Est. Tests | Pass Rate (of 3,041) | Key Deliverable |
|-----------|-----------|---------------------|-----------------|
| **M0** (current) | 508 | 16.7% | Phase 1 complete |
| **M1** — JSON import + flex/grid suites | ~850 | 28.0% | `reference-import-v2.rkt`, multi-suite runner |
| **M2** — Inline/inline-block | ~950 | 31.2% | `layout-inline.rkt` rewrite |
| **M3** — Core table layout | ~1,050 | 34.5% | `layout-table.rkt` (fixed + auto) |
| **M4** — Float + BFC | ~1,110 | 36.5% | Float algorithm in `layout-block.rkt` |
| **M5** — Text measurement | ~1,160 | 38.1% | Font metric tables + text layout |
| **M6** — Advanced/cleanup | ~1,210 | 39.8% | list-item, remaining edge cases |

### Effort Estimates

| Phase | New Code (est. lines) | Effort | Priority |
|-------|----------------------|--------|----------|
| JSON Import + Multi-suite | ~500 | 2-3 days | **P0** — unlocks everything |
| Phase 2A: Flex/Grid fixes | ~300 | 3-5 days | **P0** — highest test yield |
| Phase 2B: Inline layout | ~400 | 3-4 days | **P1** — many tests blocked |
| Phase 2C: Table layout | ~600 | 5-7 days | **P1** — large test suite |
| Phase 2D: Float layout | ~400 | 3-4 days | **P2** — moderate test yield |
| Phase 2E: Text/fonts | ~300 | 2-3 days | **P2** — quality improvement |
| Phase 2F: Advanced | ~200 | 2-3 days | **P3** — diminishing returns |
| **Total** | **~2,700** | **~3-4 weeks** | |

---

## 12. File Changes Summary

### New Files

| File | Purpose | Phase |
|------|---------|-------|
| `reference-import-v2.rkt` | JSON-computed-style → Redex box tree converter | 2A |
| `test-differential-v2.rkt` | Multi-suite test runner with `--suite` option | 2A |
| `font-metrics/liberation-sans.json` | Extracted font metrics | 2E |
| `font-metrics/arial.json` | Extracted font metrics | 2E |
| `font-metrics/default-serif.json` | Fallback font metrics | 2E |

### Modified Files

| File | Changes | Phase |
|------|---------|-------|
| `css-layout-lang.rkt` | Add display types (inline-flex, list-item, table-*), float/clear enums, table caption | 2A–2C |
| `layout-dispatch.rkt` | Route new display types, integrate JSON importer | 2A |
| `layout-flex.rkt` | aspect-ratio, baseline alignment, wrap-reverse fix, negative justify | 2A |
| `layout-grid.rkt` | minmax(), dense auto-flow, percentage tracks | 2A |
| `layout-inline.rkt` | Complete rewrite: IFC, vertical-align, half-leading line height | 2B |
| `layout-block.rkt` | Float integration, BFC containment, list-item marker | 2D, 2F |
| `layout-common.rkt` | Float state, clear handling, font metric resolution | 2B–2E |
| `layout-positioned.rkt` | Inset-based sizing for absolute flex items | 2A |
| `compare-layouts.rkt` | Handle new box types in comparison, skip anonymous wrappers | 2A–2C |

### Unchanged Files

| File | Reason |
|------|--------|
| `layout-intrinsic.rkt` | Min/max content sizing — works for new modes |
| `json-bridge.rkt` | C++ JSON bridge — separate from browser reference pipeline |
| `test-all.rkt` | Unit test runner — add new tests incrementally |

---

## Appendix A: JSON Reference Format Deep Dive

### Node Structure

```json
{
  "nodeType": "element",
  "tag": "div",
  "id": "test-root",
  "classes": ["container"],
  "layout": {
    "x": 0, "y": 0, "width": 400, "height": 300,
    "contentWidth": 400, "contentHeight": 300,
    "scrollWidth": 400, "scrollHeight": 300
  },
  "computed": {
    "display": "flex",
    "position": "relative",
    "marginTop": 0, "marginRight": 0, "marginBottom": 0, "marginLeft": 0,
    "paddingTop": 10, "paddingRight": 10, "paddingBottom": 10, "paddingLeft": 10,
    "borderTopWidth": 1, "borderRightWidth": 1, "borderBottomWidth": 1, "borderLeftWidth": 1,
    "flexDirection": "row", "flexWrap": "nowrap",
    "justifyContent": "flex-start", "alignItems": "stretch",
    "alignContent": "stretch",
    "flexGrow": 0, "flexShrink": 1, "flexBasis": "auto",
    "alignSelf": "auto", "order": 0, "gap": "0px",
    "top": "auto", "right": "auto", "bottom": "auto", "left": "auto",
    "overflow": "visible", "zIndex": "auto",
    "fontSize": "16px", "fontFamily": "Arial", "lineHeight": "normal",
    "textAlign": "start", "verticalAlign": "baseline"
  },
  "children": [ ... ]
}
```

### Text Nodes

```json
{
  "nodeType": "text",
  "layout": {
    "rects": [{"x": 10, "y": 5, "width": 100, "height": 20}],
    "hasLayout": true
  }
}
```

Text nodes have a `rects` array (one per line of text) instead of single `x/y/w/h`. The text content itself is NOT in the JSON — it must be extracted from the HTML source.

### What's Missing from JSON

| Data | Source | Workaround |
|------|--------|------------|
| Text content | HTML file | Parse from HTML |
| Grid template definitions | HTML `<style>` / inline | Parse from HTML or infer from track count |
| `aspect-ratio` value | Not in standard computed | May need to add to extraction script |
| `float` property | Not always in computed | Check via `cssFloat` computed property |
| Custom properties (CSS vars) | Already resolved | N/A — computed values are final |

### Potential Enhancement: Extend Reference Extraction Script

If any computed properties are missing from the JSON, the Puppeteer extraction script (`test/scripts/gentest/`) can be updated to capture additional fields:

```javascript
// Add to extraction script
const additionalProps = [
  'cssFloat', 'clear', 'aspectRatio',
  'gridTemplateRows', 'gridTemplateColumns',
  'gridAutoRows', 'gridAutoColumns',
  'tableLayout', 'borderSpacing', 'borderCollapse',
  'captionSide', 'emptyCells'
];
```

---

## Appendix B: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| JSON missing critical computed styles | Medium | High | Extend extraction script; fall back to HTML parsing |
| Table layout complexity exceeds estimate | High | Medium | Start with fixed-layout-only; defer anonymous objects |
| Float interaction with inline content | Medium | Medium | Limit to block-level float wrapping initially |
| Font metric accuracy insufficient | Low | Low | Accept wider tolerance for text tests; use browser-measured widths as hints |
| Regression in existing 508 tests | Low | High | Run Phase 1 test suite as regression gate on every change |
| Grid template not in computed styles | High | Medium | Parse grid template from HTML inline styles or `<style>` blocks as fallback |

---

*Created: February 15, 2026*
*Based on analysis of 3,041 test files across 11 suites with reference JSONs*

---

## Appendix C: Progress Log

### Phase 2A — Complete (Feb 15, 2026)

**508 → 621 passing (+113), 709 discovered, 0 errors.**

Approach pivoted from the proposed JSON-computed-style importer (`reference-import-v2.rkt`) to a simpler strategy: extend test discovery to scan flex/, grid/, position/, and other directories for "simple" inline-style-only tests that already work with the existing HTML pipeline. This avoids JSON import entirely for the ~200 simple tests across non-baseline suites.

**Key finding:** The reference JSON computed styles omit `width`, `height`, `box-sizing`, `aspect-ratio`, and `grid-template-*` — making a pure JSON-based importer insufficient without also parsing HTML. The inline-style pipeline remains the primary path.

**Files modified (9):**

| File | Changes |
|------|---------|
| `test-differential.rkt` | `--suite` CLI (comma-sep or "all"), 10-suite discovery, `.htm` support |
| `reference-import.rkt` | `(% N)` preservation in margin/padding parsing, `inline-flex`/table display mapping, grid percentage padding fix |
| `layout-common.rkt` | `resolve-edge-value`, `extract-box-model` with optional `containing-width` |
| `layout-flex.rkt` | Phase 5b min/max main-axis re-resolve, `containing-width` pass-through (3 sites) |
| `layout-grid.rkt` | Fr freeze algorithm (§12.7.1), fr sub-1 sum fix, `containing-width` pass-through (2 sites) |
| `layout-positioned.rkt` | Aspect-ratio: derive missing dimension from width/height/insets |
| `layout-block.rkt` | `containing-width` pass-through (2 sites) |
| `layout-dispatch.rkt` | `containing-width` pass-through (2 sites) |
| `layout-inline.rkt` | `containing-width` pass-through |

**Remaining 88 failures:** 28 floats (not implemented), 31 grid (track spanning, absolute in grid areas, baseline alignment), 14 flex (wrap sizing, overflow), 7 aspect-ratio+min/max, 3 block-in-inline, 2 percentage edge cases, 3 other.

**Comparison rules:** 100% element + text node tree match; numeric tolerance $\min(\max(3, 0.03 \times |\text{ref}|), 10)$ (base 3 px, cap 10 px).

**Additional fixes (tolerance-tightening pass):**
- Grid item margin positioning — alignment offsets now include margins; stretch sizing subtracts margins
- Grid item margin in track sizing — `measure-grid-item-min` adds item margins to border-box size
- Flex baseline cross-axis margin — cross-offset now includes `cross-margin-before`
- Flex `min-width: auto` for items with explicit size — new `measure-flex-item-content-min` returns true content-based minimum per CSS Flexbox §4.5

---

### Dedicated Flex & Grid Suites — Deep Pass (ongoing)

After Phase 2A brought in multi-suite discovery, dedicated passes on the full `flex/` (156 tests) and `grid/` (123 tests) suites pushed both to near-complete coverage.

#### Current Results

| Suite | Total | Pass | Fail | Pass Rate |
|-------|-------|------|------|-----------|
| **flex/** | 156 | **153** | 3 | **98.1%** |
| **grid/** | 123 | **117** | 6 | **95.1%** |
| **Combined** | 279 | **270** | 9 | **96.8%** |

#### Key Fixes Implemented

**Flex fixes:**

| Fix | Description | Tests Gained |
|-----|-------------|--------------|
| Phase 5b main-axis re-resolve | When indefinite main is clamped by min/max-height, re-resolve items with constrained size as definite | ~5 |
| `wrap-reverse` cross-axis | Reverse cross-axis origin and direction for `flex-wrap: wrap-reverse` | ~4 |
| Cross-size derivation heuristic | For items with explicit cross-dim or max-cross constraint, derive cross-size from container rather than using indefinite | ~6 |
| AR + max-main measurement | In Phase 3 basis sizing, measure with AR-derived cross when item has aspect-ratio + max-main constraint | ~3 |
| Post-cross cross-size update | After `determine-cross-sizes`, update cross-size using actual child-main via AR (with max-cross cap) | ~3 |
| AR cross constraint dispatch | In `determine-cross-sizes`, derive definite cross-avail from `child-main × AR` when item has aspect-ratio + auto cross + indefinite cross-avail; dispatch with derived constraint instead of indefinite | +1 |
| Flex margin/shrink fixes | Percentage margin resolution, shrink with min-size clamping, baseline cross-margin | ~8 |

**Grid fixes:**

| Fix | Description | Tests Gained |
|-----|-------------|--------------|
| Fr freeze algorithm (§12.7.1) | Tracks whose base-size exceeds proportional share are frozen iteratively | ~5 |
| Fr sub-1 sum floor | When sum of flex factors < 1, use `max(sum, 1)` as divisor per spec | ~3 |
| `(content-sized n)` avail-width | New avail-width variant signals content-derived width to grid layout, allowing `auto-container-size?` to skip the `max(sum,1)` floor for content-sized containers | +1 |
| Span + fr growth-limit guard | Skip growth-limit distribution (Step 2) when spanning item crosses fr tracks — prevents non-fr tracks from absorbing all space before Phase 3 flexible sizing | +1 |
| Percentage gap resolution | Resolve `(% N)` gap values against container size | ~3 |
| Max-width + min-content mode | Clamp available width by max-width when in min-content sizing mode | ~2 |
| Margin percent resolution | Resolve percentage margins against containing block during track sizing | ~2 |
| Grid item margin in alignment | Include margins in alignment offset and subtract from stretch sizing | ~3 |

#### Remaining 3 Flex Failures

All 3 failures use `writing-mode: vertical-lr`, which swaps the inline/block axes. Writing-mode support is not implemented.

| Test | Root Cause | Details |
|------|-----------|---------|
| `intrinsic_sizing_main_size_column.html` | `writing-mode: vertical-lr` | Expected w=10, h=40; actual w=40, h=10 (axes swapped) |
| `intrinsic_sizing_main_size_column_nested.html` | `writing-mode: vertical-lr` | Same axis-swap pattern, nested containers |
| `intrinsic_sizing_main_size_column_wrap.html` | `writing-mode: vertical-lr` | Same axis-swap pattern, wrapping variant |

#### Remaining 6 Grid Failures

| Test | Category | Root Cause |
|------|----------|-----------|
| `grid_015_content_sizing.html` | **Font-dependent** | Uses Arial font (not Ahem); 29 mismatches from text measurement differences. Non-Ahem font metrics cannot be replicated without a real font engine. |
| `grid_119_negative_lines.html` | **Font + complex** | Uses negative line numbers for implicit grid placement + Arial font; 12 mismatches from combined font metrics and implicit grid edge cases. |
| `grid_relayout_vertical_text.html` | **Writing-mode** | Uses `writing-mode: vertical-lr`; 9 mismatches from unsupported axis swap. |
| `grid_span_13_most_non_flex_with_minmax_indefinite.html` | **Extreme complexity** | 13 different track types including `minmax()`, `fit-content()`, `auto`, `fr`, fixed, and `%` — with spanning items across all + circular 20% dependency. 15 mismatches. |
| `grid_span_13_most_non_flex_with_minmax_indefinite_hidden.html` | **Extreme complexity** | Same as above with `overflow: hidden`; 15 mismatches. |
| `xgrid_fr_span_2_proportion_sub_1_sum_with_non_spanned_track.html` | **Browser quirk** | Browser gives 78/9 split for fr tracks; CSS Grid spec §12.7.1 algorithm gives 120/60. The browser deviates from spec when non-spanned tracks exist alongside spanned fr tracks with sub-1 sum. 3 mismatches. |

#### Summary

The 9 remaining failures fall into categories that are either **impossible** without new engine features (writing-mode), **inherently imprecise** (non-Ahem fonts), **prohibitively complex** (13-track-type spanning), or **browser deviations from spec**. All tractable flex and grid tests now pass.

---

### Phase 2B++ — Box Suite Block & Inline Enhancement (Feb 17, 2026)

**Box suite: 56 → 68/77 (+12), no regressions on flex (153/156) or grid (117/123).**

Dedicated pass on the `box/` suite (77 discovered tests) focusing on block-level and inline layout features needed by CSS 2.1 conformance tests. Three major fixes implemented across multiple sessions.

#### Current Results

| Suite | Total | Pass | Fail | Pass Rate |
|-------|-------|------|------|----------|
| **box/** | 77 | **68** | 9 | **88.3%** |
| **flex/** | 156 | **153** | 3 | **98.1%** |
| **grid/** | 123 | **117** | 6 | **95.1%** |

#### Key Fixes Implemented

**1. Times Serif Font Metrics (+6: 56→62)**

The box suite CSS 2.1 tests use Times (serif) as the default browser font, not Arial. Previously all text measurement used Arial metrics, causing systematic width mismatches.

| Fix | Description |
|-----|-------------|
| Dual font metric system | Added `font-metrics` symbol ('times or 'arial) propagated through styles. Selection based on both `font-family` and `font-weight`. |
| `times-char-widths` hash table | Per-character Times New Roman width ratios in `reference-import.rkt` (~30 characters). |
| `times-char-ratio` function | Per-character Times width ratios in `layout-dispatch.rkt` with uppercase kerning correction (-0.025em). |
| Font-family + weight detection | `use-arial-metrics?` = true when font-family contains sans-serif/arial/helvetica OR font-weight is bold. Otherwise defaults to Times. |
| Proportional line-height | Times ratio 1.107 (vs Arial 1.15). Space width 0.250em (vs Arial 0.278em). |

**2. Block-in-Inline Strut Heights (+5: 62→67)**

Implemented CSS 2.2 §9.2.1.1: when an inline element contains block-level children, it is split into anonymous blocks. The empty anonymous block portions before and after contribute a "strut" height equal to the line-height.

| Fix | Description |
|-----|-------------|
| `has-block-child?` detection | In `element->box-tree`, checks if any child box is `(block ...)`. |
| Inline → block conversion | When `display: inline` has block children, converts to `(block ...)` with strut height annotations. |
| `__before-strut-height` / `__after-strut-height` | Style properties storing the strut height (line-height-ratio × font-size). |
| `layout-block.rkt` integration | Applies `before-strut-h` as initial y-offset and `after-strut-h` as additional final height. |
| 10 block-in-inline tests pass | All `block-in-inline-*.htm` tests in the box suite now pass. |

**3. Inline `::before` Content Width (+1: 67→68)**

| Fix | Description |
|-----|-------------|
| `inject-before-content` extended | Handles non-block `::before` display by measuring content width with `measure-text-proportional`. |
| `__before-inline-width` | Stored in parent's inline-alist, passed through to layout as x-offset for first text child. |
| `layout-block.rkt` x-offset | Applies `before-inline-w` to first text child's x position. |

#### Attempted & Reverted

| Attempt | Reason for Revert |
|---------|-------------------|
| `white-space: pre` text node preservation | Modified `maybe-add-text-node!` to skip normalization when parent has `white-space: pre`. Caused 8 regressions (68→60) because the HTML parser's text node creation runs before CSS properties are known — whitespace between HTML elements (not meaningful content) was being preserved. Fundamental architectural issue: CSS-aware text node handling would require a two-pass approach. |

#### Files Modified (3)

| File | Changes |
|------|--------|
| `reference-import.rkt` | Times char-width hash table, font-weight/font-family detection, `font-metrics` symbol, `measure-text-proportional` dual-font support, `has-block-child?` + strut height computation, inline→block conversion, inline `::before` width measurement, `__before-inline-width` pass-through |
| `layout-dispatch.rkt` | `font-metrics` detection from styles, `times-char-ratio` function, `arial-char-ratio` function (refactored from old `char-width`), dual `space-w-char` (0.250 Times / 0.278 Arial), proportional line-height ratio per font |
| `layout-block.rkt` | `before-strut-h` / `after-strut-h` from `__before-strut-height` / `__after-strut-height`, initial-y / final-y strut integration, `before-inline-w` from `__before-inline-width` applied as x-offset to first text child |

#### Remaining 9 Box Failures

| Test | Category | Root Cause |
|------|----------|------------|
| `box_004_borders.html` | **Whitespace text nodes** | Child-count mismatch (13 vs 8). Whitespace text nodes between `inline-block` elements need preservation — `maybe-add-text-node!` strips whitespace-only nodes before CSS context is known. |
| `box_006_text_align.html` | **Inline formatting context** | Full IFC needed — `<strong>` inline elements should share lines with adjacent text. Currently stacked vertically instead of composed horizontally. |
| `box_013_nested_boxes.html` | **Box-sizing + flex** | 15/42 mismatches with systematic ~20px width offset. Tests `box-sizing: border-box` with universal selector + nested percentages + margin collapse + embedded flex layout. |
| `run-in-basic-008.htm` | **Relative positioning** | y offset expected=0, actual=-32. `position: relative; top: 2em` interaction with containing block offsets. |
| `run-in-basic-014.htm` | **white-space: pre** | Child-count 3 vs 2. Browser preserves whitespace text node between elements under `white-space: pre`. Requires CSS-aware HTML parsing (two-pass). |
| `run-in-pre-ref.htm` | **white-space: pre** | Same root cause as run-in-basic-014 — whitespace text node count mismatch. |
| `run-in-breaking-001.htm` | **white-space: pre + layout** | Child-count 3 vs 2 (same pre issue) plus height mismatch (46 vs 152.96). |
| `text-indent-011.htm` | **line-height: 0** | text.y expected=-18, text.height expected=35. `line-height: 0` causes text to overflow upward (negative y). |
| `text-indent-012.htm` | **Absolute + float** | div.y expected=0, actual=75. Complex interaction of absolute positioning + float + text-indent. |

#### Priority for Next Session

1. **white-space: pre** (3 tests) — Two-pass approach: preserve all whitespace text nodes with a flag during HTML parse, then drop based on CSS `white-space` property in a second pass.
2. **box_013 nested sizing** (1 test) — Investigate the 20px offset from `box-sizing: border-box` interacting with embedded flex container.
3. **IFC / box_006** (1 test) — Inline elements sharing lines with text content requires proper inline formatting context implementation.
4. **Remaining edge cases** (4 tests) — Relative positioning, line-height: 0, absolute+float interactions.
