# Redex Layout Code Review — Design, Structure & Readability

> A comprehensive review of the Racket/PLT Redex CSS layout engine codebase in `test/redex/`.
> Focus: code duplication, module boundaries, naming, abstractions, and maintainability.

---

## Table of Contents

1. [Codebase at a Glance](#1-codebase-at-a-glance)
2. [God File: reference-import.rkt (3656 lines)](#2-god-file-reference-importrkt-3656-lines)
3. [Table Layout Hiding Inside layout-dispatch.rkt](#3-table-layout-hiding-inside-layout-dispatchrkt)
4. [Pervasive Code Duplication](#4-pervasive-code-duplication)
5. [No view Struct — Raw S-expression Fragility](#5-no-view-struct--raw-s-expression-fragility)
6. [Magic Numbers Without Names](#6-magic-numbers-without-names)
7. [Inconsistent Naming](#7-inconsistent-naming)
8. [Imperative State in Functional Code](#8-imperative-state-in-functional-code)
9. [Mutation Analysis — Not Purely Functional](#9-mutation-analysis--not-purely-functional)
10. [Monolithic Flex/Grid Functions](#10-monolithic-flexgrid-functions)
11. [inline-styles->redex-styles Could Be Data-Driven](#11-inline-styles-redex-styles-could-be-data-driven)
12. [Large Functions (>100 lines)](#12-large-functions-100-lines)
13. [What's Already Good](#13-whats-already-good)
14. [Prioritized Recommendations](#14-prioritized-recommendations)

---

## 1. Codebase at a Glance

| File | Lines | Role |
|------|------:|------|
| `reference-import.rkt` | 3656 | HTML/CSS parsing, box tree construction, reference extraction |
| `layout-grid.rkt` | 2190 | CSS Grid algorithm |
| `layout-flex.rkt` | 2090 | CSS Flexbox algorithm |
| `layout-dispatch.rkt` | 1444 | Layout dispatch + text layout + **table layout** |
| `layout-block.rkt` | 1135 | Block flow with floats & margin collapsing |
| `compare-layouts.rkt` | 380 | Differential tree comparison |
| `font-metrics.rkt` | 368 | JSON font metrics & text measurement |
| `json-bridge.rkt` | 364 | JSON ↔ Redex term conversion |
| `layout-common.rkt` | 357 | Shared box model & length resolution |
| `layout-positioned.rkt` | 313 | Absolute/fixed/relative positioning |
| `test-differential.rkt` | 290 | Batch test runner CLI |
| `layout-inline.rkt` | 254 | Inline formatting context |
| `layout-intrinsic.rkt` | 221 | Min/max-content sizing |
| `css-layout-lang.rkt` | 183 | PLT Redex grammar definition |
| `run-layout.rkt` | 53 | CLI entry point |
| **Total** | **~13,300** | |

### Module Dependency Graph

```
css-layout-lang.rkt ← (foundation: no deps)
     ↑
layout-common.rkt ← css-layout-lang
     ↑
font-metrics.rkt ← (standalone)
     ↑
layout-intrinsic.rkt ← css-layout-lang, layout-common (but re-defines its functions!)
layout-positioned.rkt ← css-layout-lang, layout-common
layout-inline.rkt ← css-layout-lang, layout-common, font-metrics
     ↑
layout-block.rkt ← css-layout-lang, layout-common, layout-positioned, layout-inline, font-metrics
layout-flex.rkt ← css-layout-lang, layout-common, layout-positioned
layout-grid.rkt ← css-layout-lang, layout-common, layout-positioned
     ↑
layout-dispatch.rkt ← ALL layout modules, font-metrics
     ↑
reference-import.rkt ← css-layout-lang, layout-common, font-metrics (NOT layout-dispatch ✓)
     ↑
compare-layouts.rkt ← (standalone)
json-bridge.rkt ← css-layout-lang, layout-common
     ↑
test-differential.rkt ← layout-dispatch, reference-import, compare-layouts
run-layout.rkt ← layout-dispatch, json-bridge
```

**Good**: `reference-import.rkt` does NOT depend on layout modules, keeping input construction separate from the layout engine.

---

## 2. God File: `reference-import.rkt` (3656 lines)

This single file handles **5+ distinct responsibilities**:

1. **HTML parsing** — tag matching, attribute extraction, entity decoding (~lines 128–1230)
2. **CSS parsing** — inline style parsing, selector matching, specificity, cascade (~lines 310–930)
3. **CSS property mapping** — value normalization, shorthand expansion, enum mapping (~lines 1268–2340)
4. **Grid template parsing** — `grid-template-areas`, track lists, auto-repeat (~lines 2034–2270)
5. **Box tree construction** — element→box conversion, anonymous table boxes, block-in-inline (~lines 2495–3310)
6. **Reference data extraction** — expected layout tree construction (~lines 3318–3500)
7. **Test case assembly** — viewport resolution, body margins (~lines 3486–3656)
8. **Text measurement** — Ahem and proportional font (~lines 110–300)

### Proposed Split

| Proposed Module | Responsibility | Est. Lines |
|-----------------|---------------|-----------|
| `html-parser.rkt` | Tag matching, attribute extraction, entity decoding | ~600 |
| `css-resolver.rkt` | Selector matching, specificity, cascade, property normalization | ~800 |
| `box-builder.rkt` | Element→box conversion, anonymous boxes, block-in-inline | ~900 |
| `reference-import.rkt` | Thin orchestrator combining the above | ~400 |

### Monster Function: `element->box-tree` (~800 lines)

This single function handles:
- Style resolution from CSS cascading
- Font selection and text measurement
- Display type classification
- Table anonymous box generation (rows, cells, tbody wrapping)
- Block-in-inline splitting
- Flex/grid container construction
- Out-of-flow element handling
- `::before`/`::after` pseudo-element injection

Deep nesting reaches 8+ levels. It should be decomposed into ~8 smaller functions, each handling one concern.

### Monster Function: `inline-styles->redex-styles` (~500 lines)

A massive `cond` cascade matching CSS property names one at a time:

```racket
(when (cdr-or-false 'margin-top alist)
  (add! 'margin-top (parse-length (cdr-or-false 'margin-top alist))))
;; repeat 60+ times for each CSS property
```

This is highly repetitive boilerplate. See [Section 10](#10-inline-styles-redex-styles-could-be-data-driven) for the data-driven alternative.

---

## 3. Table Layout Hiding Inside `layout-dispatch.rkt`

Despite its name suggesting a thin dispatcher, `layout-dispatch.rkt` contains:

| Section | Lines | Scope |
|---------|------:|-------|
| Layout dispatch | ~133 | ✓ Correct for this file |
| Document layout | ~88 | ✓ Correct |
| Text layout (word-wrapping) | ~290 | Borderline |
| Replaced element layout | ~78 | Borderline |
| **Table layout** | **~790** | ✗ Should be its own file |

The table layout code (functions `compute-table-auto-width`, `layout-table-simple`, `layout-table-rows`, plus row stretching, cell vertical alignment, column sizing) is **larger than several standalone files**. It should be extracted to `layout-table.rkt`, matching the pattern of `layout-flex.rkt` and `layout-grid.rkt`.

Additionally, the Ahem character width mapping in `layout-dispatch.rkt` duplicates the Unicode space width handling that exists in `font-metrics.rkt`.

---

## 4. Pervasive Code Duplication

This is the **single most impactful design problem**. Multiple utility functions are copy-pasted verbatim across 2–6 files.

### 4.1 `set-view-pos` / `set-view-position` — 6 copies, 2 names

| Function Name | File |
|---------------|------|
| `set-view-pos` | `layout-block.rkt` |
| `set-view-pos` | `layout-dispatch.rkt` |
| `set-view-pos` | `layout-flex.rkt` |
| `set-view-pos` | `layout-grid.rkt` |
| `set-view-position` | `layout-inline.rkt` |
| `set-view-position` | `layout-positioned.rkt` |

**Two different names for the same function.** All 6 should be a single export from `layout-common.rkt`.

### 4.2 `get-box-styles` — 4 copies

| File |
|------|
| `layout-block.rkt` |
| `layout-dispatch.rkt` |
| `layout-flex.rkt` |
| `layout-grid.rkt` |

This 12-line match expression is identical in all four files. Adding a new box type requires updating 4 places.

### 4.3 Functions re-defined in `layout-intrinsic.rkt`

`layout-intrinsic.rkt` `require`s `layout-common.rkt` but then **re-defines 4 of its functions locally**:
- `get-style-prop`
- `extract-box-model`
- `get-edges`
- `horizontal-pb`

The local versions differ only marginally in `extract-box-model` (simpler `auto→0` handling). This is likely a copy-paste artifact.

### 4.4 Other Duplicates

| Function | Copies | Files |
|----------|:------:|-------|
| `set-view-size` | 2 | `layout-flex.rkt`, `layout-grid.rkt` |
| `offset-view` / `offset-view-node` / `offset-view*` | 3 | `layout-block.rkt`, `layout-flex.rkt`, `layout-grid.rkt` |
| `compute-view-baseline` | 2 | `layout-flex.rkt`, `layout-grid.rkt` |
| `resolve-gap` | 2 | `layout-flex.rkt`, `layout-grid.rkt` |
| Unicode space widths (`0.333`, `0.25`, etc.) | 2 | `layout-dispatch.rkt`, `font-metrics.rkt` |

**Note**: The two copies of `compute-view-baseline` have subtly different behavior — the flex version checks for a stored baseline first, while the grid version doesn't. This is likely a latent bug or unintentional inconsistency.

### Summary of Duplication

| Function | Copies | Should Be In |
|----------|:------:|--------------|
| `set-view-pos`/`set-view-position` | **6** | `layout-common.rkt` |
| `get-box-styles` | **4** | `layout-common.rkt` |
| `offset-view` variants | **3** | `layout-common.rkt` |
| `get-style-prop` | **2** | already in `layout-common.rkt` |
| `extract-box-model` | **2** | already in `layout-common.rkt` |
| `get-edges` | **2** | already in `layout-common.rkt` |
| `horizontal-pb` | **2** | already in `layout-common.rkt` |
| `set-view-size` | **2** | `layout-common.rkt` |
| `compute-view-baseline` | **2** | `layout-common.rkt` |
| `resolve-gap` | **2** | `layout-common.rkt` |

---

## 5. No `view` Struct — Raw S-expression Fragility

Views are represented as raw lists with varying arity:

```racket
(view id x y w h children)           ;; 7 elements
(view id x y w h children baseline)  ;; 8 elements
(view-text id x y w h text)          ;; 7 elements
```

Every function that touches views must pattern-match **all 3 variants**:

```racket
[(view ,id ,x ,y ,w ,h ,children ,baseline) ...]
[(view ,id ,x ,y ,w ,h ,children) ...]
[(view-text ,id ,x ,y ,w ,h ,text) ...]
```

This 3-case match block appears in:
- `set-view-pos` (×6 copies)
- `set-view-size` (×2 copies)
- `offset-view` (×3 copies)
- Many inline sites throughout the codebase

Introducing a proper Racket `struct` would:
- Eliminate all variant-matching boilerplate
- Provide type-safe field access instead of `list-ref` with magic indices
- Make the 6 copies of `set-view-pos` unnecessary (one `struct-copy` one-liner)
- Enable compile-time checking for field access errors

---

## 6. Magic Numbers Without Names

| Value | Where | What It Means |
|------:|-------|---------------|
| `16` | ~15 sites across codebase | CSS default font-size |
| `8` | `reference-import.rkt` | `<body>` default margin (CSS UA) |
| `12` | `layout-dispatch.rkt` | `max-layout-depth` — no explanation of why 12 |
| `50` | `layout-block.rkt` (2 sites) | Estimated height for float avoidance |
| `0.5` | multiple files | `current-ex-ratio` default |
| `17.71` | `layout-dispatch.rkt` | `<br>` height fallback (16 × 1.107) — should be computed |
| `1200 × 800` | `reference-import.rkt` | Default viewport size |
| `0.333`, `0.25`, `0.167` | `layout-dispatch.rkt`, `font-metrics.rkt` | Unicode space widths (duplicated) |

**Recommendation**: Define as top-level named constants:
```racket
(define CSS-DEFAULT-FONT-SIZE 16)
(define UA-BODY-MARGIN 8)
(define MAX-LAYOUT-DEPTH 12)  ;; TODO: document why 12
(define DEFAULT-VIEWPORT-WIDTH 1200)
(define DEFAULT-VIEWPORT-HEIGHT 800)
```

---

## 7. Inconsistent Naming

| Pattern A | Pattern B | Where |
|-----------|-----------|-------|
| `set-view-pos` | `set-view-position` | 4 files vs 2 files |
| `dispatch-fn` | `layout` | Parameter name for the layout callback varies across modules |
| `offset-view` | `offset-view-node` / `offset-view*` | Same logic, 3 names |
| `get-style-prop` | `resolve-css-property` | Two names for property lookup in different contexts |

**Recommendation**: Pick canonical names and use them everywhere. `set-view-pos`, `dispatch-fn`, `offset-view`, `get-style-prop`.

---

## 8. Imperative State in Functional Code

### Float Tracking in `layout-block-children`

`layout-block-children` in `layout-block.rkt` tracks float state via 3 mutable variables (`float-lefts`, `float-rights`, `max-float-bottom`) mutated by closures inside a `let loop`. The flow of state is hard to follow and reason about.

**Recommendation**: Thread a `float-context` struct through the loop:

```racket
(struct float-ctx (lefts rights max-bottom) #:transparent)
```

### Mutable List Building in `reference-import.rkt`

`reference-import.rkt` uses heavy `set!` mutation for building result lists where idiomatic `for/fold` or `for/list` would be clearer and less error-prone.

---

## 9. Mutation Analysis — Not Purely Functional

The codebase is **functional at module boundaries** (functions take inputs, return view trees) but **imperative inside complex algorithms**. Total: ~230 mutation sites across the layout engine.

### Mutation Counts by File

| File | `set!` | `vector-set!` | `make-vector`/`make-hash` | `parameterize` | Style |
|------|:------:|:-------------:|:-------------------------:|:--------------:|-------|
| `layout-grid.rkt` | 59 | 17 | 6 | 0 | **Heavily imperative** |
| `reference-import.rkt` | 53 | 0 | 0 | 9 | **Heavily imperative** |
| `layout-inline.rkt` | 25 | 0 | 0 | 0 | **Imperative loop** |
| `font-metrics.rkt` | 25 | 0 | 0 | 0 | Module-level init (load-once) |
| `json-bridge.rkt` | 24 | 0 | 0 | 0 | Imperative tree walk |
| `layout-flex.rkt` | 17 | 6 | 1 | 0 | **Mixed** — imperative inner loops |
| `layout-positioned.rkt` | 8 | 0 | 0 | 0 | Mild |
| `layout-dispatch.rkt` | 7 | 0 | 1 | 2 | Mild |
| `layout-block.rkt` | 5 | 0 | 0 | 6 | **Mostly functional** (floats are `set!`) |
| `layout-common.rkt` | 1 | 0 | 0 | 0 | Essentially pure |
| `layout-intrinsic.rkt` | 0 | 0 | 0 | 0 | **Pure** |
| `css-layout-lang.rkt` | 0 | 0 | 0 | 0 | **Pure** (grammar only) |

### Where Mutation Lives

**Grid & Flex (worst offenders)** — Track sizing algorithms use mutable vectors with `vector-set!` for the "freeze tracks" loop pattern from the CSS spec. Grid also mutates placement state (`max-r`, `max-c`, `item-infos`) extensively during auto-placement. These algorithms mirror the imperative language of the CSS spec itself, which describes them in terms of mutable state.

**Inline layout** — The line-breaking loop in `layout-inline.rkt` mutates 4 variables (`current-x`, `current-y`, `line-height`, `completed-line-widths`) through every word/item — classic imperative word-wrapping.

**Block layout** — Mostly functional with `let loop` recursion, except float state (`float-lefts`, `float-rights`, `max-float-bottom`) tracked via `set!`. The 6 `parameterize` calls propagate float context to nested BFCs.

**`reference-import.rkt`** — Uses `set!` heavily for list-building during HTML parsing and box tree construction (the `add!` pattern for accumulating style properties). Most of these could be replaced with `for/fold` or `for/list`.

**`font-metrics.rkt`** — The 25 `set!` calls are all module-level load-once initialization (populating the font registry from JSON at startup). Not a concern — this is standard Racket module init.

### Purely Functional Modules

Only three modules are truly pure (zero mutation):
- `layout-intrinsic.rkt` — min/max-content sizing
- `css-layout-lang.rkt` — PLT Redex grammar definition
- `layout-common.rkt` — essentially pure (1 `set!` for module init)

### Mutation Patterns

| Pattern | Where | Occurrences | Functional Alternative |
|---------|-------|:-----------:|------------------------|
| **Accumulator loop** — building result lists with `set!` | `reference-import.rkt`, `layout-grid.rkt` | ~30 | `for/fold` or `for/list` |
| **Track sizing** — mutable vectors for "freeze" algorithms | `layout-grid.rkt`, `layout-flex.rkt` | ~23 | Could use immutable vectors with `vector-copy`, but performance cost |
| **Position cursors** — mutable x/y during line-breaking | `layout-inline.rkt`, `layout-flex.rkt` | ~20 | Thread a `pos` struct through `let loop` |
| **Float context** — mutable float lists in block layout | `layout-block.rkt` | 5 | `float-ctx` struct threaded through loop |
| **Module init** — one-time loading of font data | `font-metrics.rkt` | 25 | Acceptable as-is |

### Assessment

The imperative style is a pragmatic choice — CSS layout specs are written in imperative pseudocode, and translating them to purely functional Racket would obscure the correspondence. The grid "freeze tracks" algorithm, for example, is inherently stateful. However, the **accumulator pattern** (`set!` for list-building) and **position cursor pattern** (`set!` for x/y tracking) could be converted to functional style (`for/fold`, threaded structs) with clear readability wins and no performance cost.

---

## 10. Monolithic Flex/Grid Functions

`layout-flex` (~1900 lines) and `layout-grid` (~1800 lines) are each essentially **single monolithic functions**. While they have good internal CSS spec-reference comments marking each phase, extracting the phases into named sub-functions would:

- Make each phase independently testable
- Reduce indentation depth
- Create a readable "table of contents" at the top-level function

For example, `layout-flex` could become:

```racket
(define (layout-flex box avail-w avail-h dispatch-fn)
  ;; CSS Flexbox §9.2: Line Length Determination
  (define items (flex-collect-items ...))
  ;; CSS Flexbox §9.3: Main Size Determination
  (define lines (flex-resolve-flexible-lengths items ...))
  ;; CSS Flexbox §9.4: Cross Size Determination
  (define positioned (flex-cross-axis-alignment lines ...))
  ;; CSS Flexbox §9.5: Main-Axis Alignment
  (flex-emit-views positioned ...))
```

---

## 11. `inline-styles->redex-styles` Could Be Data-Driven

The ~500-line function follows a repetitive pattern for each of ~60 CSS properties:

```racket
(when (cdr-or-false 'margin-top alist)
  (add! 'margin-top (parse-length (cdr-or-false 'margin-top alist))))
```

A declarative property mapping table would reduce this to ~100 lines:

```racket
(define css-property-table
  `((margin-top      . ,parse-length)
    (margin-right    . ,parse-length)
    (margin-bottom   . ,parse-length)
    (margin-left     . ,parse-length)
    (padding-top     . ,parse-length)
    (font-size       . ,parse-font-size)
    (display         . ,parse-display)
    (position        . ,parse-position)
    ...))

(for ([entry (in-list css-property-table)])
  (define val (cdr-or-false (car entry) alist))
  (when val
    (add! (car entry) ((cdr entry) val))))
```

Properties with special handling (shorthands like `margin`, `background`, `border`) can remain as explicit cases after the table-driven loop.

---

## 12. Large Functions (>100 lines)

| Function | File | Approx Lines | Concern |
|----------|------|------------:|---------|
| `layout-flex` | `layout-flex.rkt` | ~1900 | Entire flex algorithm in one function |
| `layout-grid` | `layout-grid.rkt` | ~1800 | Entire grid algorithm in one function |
| `element->box-tree` | `reference-import.rkt` | ~800 | 10+ responsibilities |
| `layout-block-children` | `layout-block.rkt` | ~540 | Block stacking with floats, clear, IFC |
| `inline-styles->redex-styles` | `reference-import.rkt` | ~500 | Repetitive CSS property mapping |
| `layout-table-simple` | `layout-dispatch.rkt` | ~350 | Table sizing + row groups |
| `layout-text-inner` | `layout-dispatch.rkt` | ~230 | Word-wrapping |
| `layout-table-rows` | `layout-dispatch.rkt` | ~210 | Row + cell layout |
| `parse-children-until` | `reference-import.rkt` | ~140 | Recursive HTML parsing |
| `layout-block` | `layout-block.rkt` | ~125 | Block layout entry |

---

## 13. What's Already Good

- **CSS spec references** — Nearly every non-trivial block cites the relevant spec section (e.g., "CSS 2.2 §10.3.3", "CSS Flexbox §9.3"). This is excellent for maintainability and auditability.
- **Module dependency structure** — `reference-import.rkt` does NOT depend on layout modules, cleanly separating input construction from the layout engine.
- **`compare-layouts.rkt`** — Well-structured tolerance config via a `compare-config` struct. Clear separation of tree-walk comparison logic.
- **`font-metrics.rkt`** — Clean, focused, well-documented. Exactly one responsibility.
- **`css-layout-lang.rkt`** — Concise, correct PLT Redex grammar definition.
- **`layout-common.rkt`** — Has the right idea (shared box model + length resolution). It just needs more functions moved into it.
- **`test-differential.rkt`** — Clean CLI with `command-line` parsing. Tolerance parameters properly structured.

---

## 14. Prioritized Recommendations

| # | Change | Impact | Risk | Effort |
|:-:|--------|:------:|:----:|:------:|
| 1 | **Consolidate duplicated functions into `layout-common.rkt`** — `set-view-pos`, `get-box-styles`, `offset-view`, `set-view-size`, `compute-view-baseline`, `resolve-gap` | High | Low | Medium |
| 2 | **Extract table layout into `layout-table.rkt`** (~790 lines out of `layout-dispatch.rkt`) | Medium | Low | Low |
| 3 | **Split `reference-import.rkt`** into `html-parser.rkt` + `css-resolver.rkt` + `box-builder.rkt` | High | Medium | High |
| 4 | **Introduce `view` struct** to replace raw S-expression views | High | Medium | High |
| 5 | **Remove local re-definitions in `layout-intrinsic.rkt`** — just use `layout-common.rkt` imports | Low | Very Low | Very Low |
| 6 | **Decompose `element->box-tree`** (~800 lines → ~8 functions) | Medium | Medium | Medium |
| 7 | **Data-drive `inline-styles->redex-styles`** (500 → ~100 lines) | Medium | Low | Medium |
| 8 | **Name all magic numbers** as top-level constants | Low | Very Low | Very Low |
| 9 | **Standardize naming** — pick `set-view-pos` everywhere, pick `dispatch-fn` everywhere | Low | Very Low | Low |
| 10 | **Extract flex/grid phases** into named sub-functions | Medium | Medium | High |

**Quick wins** (items 2, 5, 8, 9) are low-risk, low-effort changes that can be done incrementally without affecting test results.

**Highest ROI** (item 1) eliminates ~25 copy-pasted function definitions and is the single change that most improves maintainability.
