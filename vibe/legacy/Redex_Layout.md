# Implementing a Redex-like Layout Specification in Lambda

## Executive Summary

This document analyzes the feasibility of implementing a **PLT Redex–equivalent formal layout specification** directly in Lambda Script, rather than using Racket/Redex as a separate tool. The conclusion: **it is feasible**, with Lambda's element types serving as a natural substitute for algebraic data types, and only one significant language enhancement needed (element destructuring in `match`).

---

## 1. What Redex Provides

PLT Redex is a Racket DSL for defining and testing operational semantics. Its core capabilities are:

| Redex Facility | Purpose |
|---|---|
| `define-language` | Define grammars as tagged variant types (box tree, styles, views) |
| `define-judgment-form` | Express layout rules as formal judgments |
| `define-metafunction` | Implement algorithms as reusable typed functions |
| `define-reduction-relation` | Model multi-pass layout as state machine transitions |
| `redex-check` | Random testing against invariants |
| `traces` / stepper | Step-by-step execution visualization |
| Typesetting | Generate publication-quality reduction rule figures |

The question is: which of these can Lambda already cover, and what's missing?

---

## 2. Lambda's Existing Strengths

Lambda already covers a substantial portion of what Redex provides:

| Redex Capability | Lambda Equivalent | Coverage |
|---|---|---|
| Tagged variant types (`define-language`) | **Element types** (`<block>`, `<flex>`, `<text>`) | ✅ Excellent |
| Recursive functions (`define-metafunction`) | `fn` with full tail-call optimization | ✅ Excellent |
| Immutable data structures | All data immutable by default | ✅ Excellent |
| Tree-structured data | Elements + Maps + Lists | ✅ Excellent |
| Pattern matching (type/value) | `match` with type, literal, range, or-patterns | ✅ Good |
| Higher-order functions | First-class functions, closures, composition | ✅ Excellent |
| Collection transforms | Pipes (`\|`), `where`, `for`, method chaining | ✅ Excellent |
| Structural typing | Maps/elements checked structurally | ✅ Good |
| First-class types | Types as values, `type()`, `is` checks | ✅ Good |
| JSON I/O (for differential testing) | Built-in `input()` / `format()` for JSON | ✅ Excellent |
| Modules and imports | `import`, `pub` exports | ✅ Good |
| State machines (`define-reduction-relation`) | Recursive `fn` + element-based states + TCO | ✅ Good |

### What Would NOT Be Needed from Redex

| Redex Feature | Why Not Needed in Lambda |
|---|---|
| S-expression syntax | Lambda's element syntax is more readable for trees |
| `define-language` grammar | Element type declarations serve the same role |
| `define-metafunction` | `fn` already exists |
| `define-judgment-form` | Functions with element pattern matching suffice |
| `define-reduction-relation` | State machine via recursive `fn` + element states + TCO |
| `traces` / stepper | Logging + intermediate state JSON export |
| Typesetting of rules | Not needed — Lambda's goal is executable spec, not paper publication |

---

## 3. Elements as Algebraic Data Types

### 3.1 The Key Insight

Lambda's **element type** is structurally equivalent to an ADT constructor:

| ADT Concept | Element Equivalent |
|---|---|
| Constructor **name** | Element **tag name** (`<block>`, `<flex>`, `<text>`) |
| Named **fields** / payload | Element **attributes** (`<block id: "div", width: 800>`) |
| Positional **payload** | Element **body content** (`<block; child1, child2>`) |
| Recursive nesting | Elements can contain elements as body content |
| Type checking | `value is <div>` — validator checks element tag names |

### 3.2 Runtime Support Already Exists

The machinery for element-as-ADT dispatch is already implemented:

- **`name(el)`** returns the tag as a symbol (`'block`)
- **`el.attr`** accesses named attribute fields
- **`el[0]`, `el[1]`** accesses positional body content by index
- **`len(el)`** returns body content count
- **`el is <tag>`** — the schema validator already checks element tag names
  - `TypeElmt` stores `name` field for the expected tag
  - `validate_against_element_type()` compares `element_type->name` against actual tag
  - `TypeElmt` inherits from `TypeMap`, so attribute types are checked structurally
- **`match` with `case <tag>:`** — dispatches via `fn_is()` → validator → tag name check

### 3.3 Element Data Model

An element in Lambda has **dual nature** — it is simultaneously a **list** (ordered body content) and a **map** (named attributes):

```
struct Element : List {
    // From List: Item* items, int64_t length (body content)
    // Attributes:
    void* type;      // attr type/shape (TypeElmt, inherits TypeMap)
    void* data;      // packed attribute data struct
};
```

This dual nature maps perfectly to layout tree nodes, which have both **named properties** (styles, id) and **ordered children** (child boxes).

---

## 4. Modeling Layout in Lambda

### 4.1 Box Tree Representation

The Redex `define-language` for the box tree maps directly to Lambda element types:

**Redex:**
```racket
(define-language CSS-Layout
  (Box ::= (block BoxId Styles (Box ...))
         | (inline BoxId Styles (InlineContent ...))
         | (flex BoxId Styles (Box ...))
         | (text BoxId Styles string number)
         | (replaced BoxId Styles number number)))
```

**Lambda:**
```lambda
// Type declarations for documentation and validation
type Styles = {
    display: symbol,
    position: symbol,
    width: float, height: float,
    min_width: float, min_height: float,
    max_width: float, max_height: float,
    margin: (float, float, float, float),
    padding: (float, float, float, float),
    border_width: (float, float, float, float)
}

type FlexStyles = {
    flex_direction: symbol,
    flex_wrap: symbol,
    justify_content: symbol,
    align_items: symbol,
    flex_grow: float,
    flex_shrink: float,
    flex_basis: float
}

// "Constructors" are element literals — tag name = variant, attrs = fields, body = children
let example_tree =
    <flex id: "main", styles: {display: 'flex, flex_direction: 'row, width: 800};
        <block id: "sidebar", styles: {display: 'block, width: 200}>
        <block id: "content", styles: {display: 'block, flex_grow: 1};
            <text id: "p1", styles: {display: 'inline}; "Hello world">
        >
    >
```

### 4.2 Layout Dispatch

Layout dispatch on box type uses `match` with element type patterns:

```lambda
// Main layout dispatch — equivalent to Redex define-judgment-form
fn layout(box, avail_width: float) => match box {
    case <block>:    layout_block(box, avail_width)
    case <inline>:   layout_inline(box, avail_width)
    case <flex>:     layout_flex(box, avail_width)
    case <grid>:     layout_grid(box, avail_width)
    case <text>:     layout_text(box, avail_width)
    case <replaced>: layout_replaced(box, avail_width)
    default:         error("unknown box type: " ++ string(name(box)))
}
```

### 4.3 View Tree Output

Layout results are also elements:

```lambda
// View tree node: positioned box with computed geometry
// <view id, x, y, width, height; child_views...>

fn layout_block(box, avail_width: float) => {
    let styles = box.styles
    let content_width = resolve_width(styles, avail_width)
    let pad = styles.padding
    let child_avail = content_width - pad.1 - pad.3  // left + right padding

    // Layout children, accumulate y offset
    let result = fold_children(box, child_avail)

    let content_height = resolve_height(styles, result.y)
    let total_height = content_height + pad.0 + pad.2  // top + bottom padding

    <view id: box.id, x: 0, y: 0, w: avail_width, h: total_height;
        *result.views
    >
}

// Helper: fold over children, stacking vertically
fn fold_children(box, avail_width: float) => {
    fn go(i: int, y: float, views) => {
        if (i >= len(box)) {y: y, views: views}
        else {
            let child_view = layout(box[i], avail_width)
            go(i + 1, y + child_view.h, views ++ [child_view])
        }
    }
    go(0, 0.0, [])  // TCO applies here
}
```

### 4.4 Multi-Pass Algorithms as State Machines

Redex's `define-reduction-relation` for multi-pass algorithms (flex 9-phase, grid track sizing) maps to recursive functions over element-encoded states:

```lambda
// Flex layout phases as element-tagged states
// Each phase is an element whose tag = phase name, body = phase data

fn flex_step(state) => match state {
    case <flex-init>:
        // Phase 1→2: Sort items by CSS order property
        <flex-sorted; sort_by_order(state[0])>

    case <flex-sorted>:
        // Phase 2→3: Partition into flex lines
        <flex-lines; create_flex_lines(state[0])>

    case <flex-lines>:
        // Phase 3→4: Resolve flexible lengths
        <flex-resolved; resolve_flex_lengths(state[0])>

    case <flex-resolved>:
        // Phase 4→5: Determine cross sizes
        <flex-cross-sized; compute_cross_sizes(state[0])>

    case <flex-cross-sized>:
        // Phase 5→6: Main axis alignment
        <flex-main-aligned; align_main_axis(state[0])>

    case <flex-main-aligned>:
        // Phase 6→7: Cross axis alignment
        <flex-cross-aligned; align_cross_axis(state[0])>

    case <flex-cross-aligned>:
        // Phase 7→8: Align-content distribution
        <flex-content-aligned; align_content(state[0])>

    case <flex-content-aligned>:
        // Phase 8→9: Build final view
        <flex-done; build_flex_view(state[0])>

    case <flex-done>:
        state  // terminal state
}

// Run flex layout to completion
fn flex_layout(container, items) => {
    fn run(s) => match s {
        case <flex-done>: s[0]   // extract final view from terminal state
        default: run(flex_step(s))  // TCO kicks in — no stack growth
    }
    run(<flex-init; {container: container, items: items}>)
}

// Trace execution (for debugging) — collect all intermediate states
fn flex_trace(container, items) => {
    fn collect(s, trace) => match s {
        case <flex-done>: trace ++ [s]
        default: collect(flex_step(s), trace ++ [s])
    }
    collect(<flex-init; {container: container, items: items}>, [])
}
```

### 4.5 Common Layout Utilities

```lambda
// Box model computation — equivalent to Redex layout-common metafunctions

fn resolve_width(styles, avail: float) float => match styles.width {
    case null: avail   // auto
    case float: styles.width
    // percentage would be: avail * (styles.width_pct / 100.0)
}

fn resolve_height(styles, content_h: float) float => match styles.height {
    case null: content_h   // auto = content height
    case float: styles.height
}

fn content_box_width(styles, avail: float) float => {
    let w = resolve_width(styles, avail)
    let pad = styles.padding
    w - pad.1 - pad.3  // subtract left + right padding
}

fn clamp_size(value: float, min_val: float, max_val: float) float =>
    max(min_val, min(max_val, value))

// Margin collapsing (block flow)
fn collapse_margins(margin_bottom: float, margin_top: float) float =>
    if (margin_bottom >= 0 and margin_top >= 0)
        max(margin_bottom, margin_top)
    else if (margin_bottom < 0 and margin_top < 0)
        min(margin_bottom, margin_top)
    else
        margin_bottom + margin_top
```

### 4.6 Intrinsic Sizing

```lambda
// Min-content and max-content width measurement
fn min_content_width(box) float => match box {
    case <text>: measure_word_width(box[0])  // widest single word
    case <replaced>: box.intrinsic_w
    case <block>: {
        if (len(box) == 0) 0.0
        else max(for (i in 0 to len(box) - 1) min_content_width(box[i]))
    }
    case <flex>: min_content_width_flex(box)
    default: 0.0
}

fn max_content_width(box) float => match box {
    case <text>: measure_text_width(box[0])  // full text without wrapping
    case <replaced>: box.intrinsic_w
    case <block>: {
        if (len(box) == 0) 0.0
        else max(for (i in 0 to len(box) - 1) max_content_width(box[i]))
    }
    case <flex>: max_content_width_flex(box)
    default: 0.0
}
```

---

## 5. Integration with Radiant (Differential Testing)

### 5.1 Data Exchange

The bridge between Radiant (C++) and the Lambda model uses JSON:

```
┌──────────────┐       JSON        ┌──────────────┐
│   Radiant    │  ──────────────→  │   Lambda     │
│   (C++)      │  box-tree.json    │   model      │
│              │                   │   (.ls)      │
│  ViewBlock   │  ←──────────────  │  View elts   │
│  ViewSpan    │  layout.json      │              │
└──────────────┘                   └──────────────┘
```

Lambda already has built-in JSON input/output:

```lambda
// Import box tree from Radiant
let box_tree = input("temp/box-tree.json", 'json)

// Convert JSON to element-based box tree
fn json_to_box(node) => {
    let tag = symbol(node.type)   // "block" → 'block
    let styles = node.styles
    let children = if (node.children)
        for (c in node.children) json_to_box(c)
    else null

    match tag {
        case 'text:
            <text id: node.id, styles: styles; node.content>
        case 'replaced:
            <replaced id: node.id, styles: styles,
                      intrinsic_w: node.intrinsic_w, intrinsic_h: node.intrinsic_h>
        default:
            if (children)
                // Dynamic element creation would need a helper
                <block id: node.id, styles: styles; *children>
            else
                <block id: node.id, styles: styles>
    }
}

// Run layout
let view = layout(json_to_box(box_tree), 800.0)

// Export result
print(format(view, 'json))
```

### 5.2 Comparison Pipeline

```bash
# Export box tree from Radiant
./lambda.exe layout test/layout/data/flex-basic.html --export-box-tree temp/box-tree.json

# Run Lambda layout model
./lambda.exe layout-model.ls temp/box-tree.json > temp/lambda-layout.json

# Compare results
./lambda.exe compare-layouts.ls view_tree.json temp/lambda-layout.json
```

### 5.3 Comparison Script

```lambda
// compare-layouts.ls
let tolerance = 0.5  // pixels

fn compare_views(expected, actual, path: string) => {
    let diffs = []
    let dx = abs(expected.x - actual.x)
    let dy = abs(expected.y - actual.y)
    let dw = abs(expected.w - actual.w)
    let dh = abs(expected.h - actual.h)

    let this_diffs = [dx, dy, dw, dh]
        | ~# where ~ > tolerance
        | {path: path, property: ["x", "y", "w", "h"][~#], delta: ~}

    let child_diffs = for (i in 0 to min(len(expected), len(actual)) - 1)
        compare_views(expected[i], actual[i], path ++ "/" ++ string(actual[i].id))

    [*this_diffs, *child_diffs]
}
```

---

## 6. Property-Based Testing (redex-check Equivalent)

Redex's `redex-check` generates random terms and checks invariants. This can be built as a Lambda library:

### 6.1 Random Box Tree Generation

```lambda
// Generate random box trees for property testing
fn random_box(depth: int, max_children: int) => {
    if (depth <= 0)
        <text id: "t" ++ string(rand_int(1000)),
              styles: random_styles('inline);
            "sample text"
        >
    else {
        let n = rand_int(max_children) + 1
        let tag = ['block, 'flex, 'inline][rand_int(3)]
        let children = for (i in 1 to n) random_box(depth - 1, max_children)
        <block id: "n" ++ string(rand_int(1000)),
               styles: random_styles(tag);
            *children
        >
    }
}

fn random_styles(display: symbol) => {
    {
        display: display,
        width: if (rand_int(2) == 0) null else float(rand_int(500) + 50),
        height: null,
        padding: (float(rand_int(20)), float(rand_int(20)),
                  float(rand_int(20)), float(rand_int(20))),
        margin: (float(rand_int(20)), float(rand_int(20)),
                 float(rand_int(20)), float(rand_int(20)))
    }
}
```

### 6.2 Invariant Checks

```lambda
// Structural invariant: every child fits within parent content area
fn children_within_parent(view) bool => {
    let pw = view.w
    let ph = view.h
    let ok = for (i in 0 to len(view) - 1) {
        let child = view[i]
        child.x >= 0 and child.y >= 0
            and child.x + child.w <= pw + 0.5
            and child.y + child.h <= ph + 0.5
            and children_within_parent(child)
    }
    not (false in ok)
}

// No sibling overlap in block flow
fn no_sibling_overlap(view) bool => {
    let children = for (i in 0 to len(view) - 1) view[i]
    let ok = for (i in 0 to len(children) - 2) {
        let a = children[i]
        let b = children[i + 1]
        a.y + a.h <= b.y + 0.5  // a ends before b starts
    }
    not (false in ok)
}

// Determinism: same input always produces same output
fn is_deterministic(box, avail: float) bool => {
    let v1 = layout(box, avail)
    let v2 = layout(box, avail)
    views_equal(v1, v2)
}

// Flex-grow fills all available space
fn flex_fills_space(view) bool => {
    let child_widths = for (i in 0 to len(view) - 1) view[i].w
    abs(sum(child_widths) - view.w) < 1.0
}

// No negative dimensions
fn all_non_negative(view) bool => {
    view.w >= 0 and view.h >= 0
        and (for (i in 0 to len(view) - 1) all_non_negative(view[i]))
            | not (false in ~)
}
```

### 6.3 Test Runner

```lambda
// Run property tests
fn check_property(name: string, gen_fn, prop_fn, trials: int) => {
    let failures = for (i in 1 to trials) {
        let box = gen_fn()
        let view = layout(box, 800.0)
        if (not prop_fn(view)) {input: box, output: view, trial: i}
        else null
    } where ~ != null

    if (len(failures) == 0)
        print("✅ " ++ name ++ ": " ++ string(trials) ++ " trials passed")
    else {
        print("❌ " ++ name ++ ": " ++ string(len(failures)) ++ " failures")
        failures
    }
}

// Run all checks
check_property("children within parent",
    () => random_box(3, 4), children_within_parent, 100)
check_property("no sibling overlap",
    () => random_box(3, 4), no_sibling_overlap, 100)
check_property("all non-negative",
    () => random_box(3, 4), all_non_negative, 100)
check_property("deterministic",
    () => random_box(2, 3),
    (view) => is_deterministic(view, 800.0), 50)
```

---

## 7. Module Structure

```
lambda-layout-model/
├── layout.ls                  # Top-level layout dispatch
├── layout_common.ls           # Box model, length resolution, margin collapsing
├── layout_block.ls            # Block flow layout
├── layout_inline.ls           # Inline layout + line breaking
├── layout_flex.ls             # Flexbox 9-phase algorithm (state machine)
├── layout_grid.ls             # Grid track sizing + placement
├── layout_table.ls            # Table layout
├── layout_positioned.ls       # Absolute / fixed positioning
├── layout_intrinsic.ls        # Min-content / max-content sizing
├── box_tree.ls                # Box tree type definitions and constructors
├── view_tree.ls               # View tree types and utilities
├── import_json.ls             # JSON → element box tree converter
├── export_json.ls             # View tree → JSON exporter
├── compare.ls                 # Differential comparison with Radiant output
├── properties.ls              # Property-based test invariants
├── random_gen.ls              # Random box tree generation
├── test_block.ls              # Block layout unit tests
├── test_flex.ls               # Flex layout tests
├── test_grid.ls               # Grid layout tests
├── test_properties.ls         # Property-based test runner
└── test_differential.ls       # Differential tests vs. Radiant output
```

---

## 8. Gap Analysis: What Lambda Still Needs

### 8.1 Element Destructuring in Match — 🔴 Primary Gap

**Current limitation:** You can dispatch on element tag in `match`, but cannot extract fields in the pattern.

```lambda
// Today (works but verbose):
fn layout(box, avail) => match box {
    case <block>: layout_block(box.id, box.styles, box, avail)
    case <text>:  layout_text(box.id, box.styles, box[0], avail)
}

// Desired (not yet supported):
fn layout(box, avail) => match box {
    case <block id, styles; *children>:
        layout_block(id, styles, children, avail)
    case <text id, styles; content>:
        layout_text(id, styles, content, avail)
}
```

**Workaround:** Access via `box.attr` and `box[i]` — functional, just verbose. Since `~` refers to the matched value inside arms, this is manageable:

```lambda
fn layout(box, avail) => match box {
    case <block>: {
        let id = ~.id, styles = ~.styles
        layout_block(id, styles, ~, avail)
    }
}
```

**Effort to implement:** Medium — requires grammar extension for element patterns with bindings, AST changes for destructuring nodes, and transpiler updates to emit field extraction code. No runtime changes needed since element access already works.

### 8.2 Exhaustiveness Checking — 🟡 Secondary Gap

The compiler does not know that `<block> | <flex> | <text> | <replaced>` covers all box variants. A `default` arm is always needed.

**Workaround:** Always include `default: error("unknown: " ++ string(name(box)))`.

**Effort to implement:** Medium-large — would need a way to declare closed element-tag unions and track coverage in the compiler.

### 8.3 Generics / Parametric Polymorphism — 🟡 Nice-to-Have

The "Layout Algebra" abstraction (unifying HTML layout, graph layout, constraint layout) would benefit from generics:

```lambda
// Desired (not supported):
fn perform_layout<T: LayoutNode>(tree: T, space: float) View => ...
```

**Workaround:** Use `any` type and runtime `is` checks. Or simply use separate function names for each layout domain.

### 8.4 No New Types or Runtime Changes Needed — ✅

Elements, maps, lists, and the existing type system fully cover the data model. No new container types, no runtime changes, no memory model changes.

### Summary Table

| Feature | Status | Impact | Workaround |
|---|---|---|---|
| Element tag dispatch in `match` | ✅ Works today | — | — |
| Element attribute access | ✅ Works today | — | — |
| Element body content access | ✅ Works today | — | — |
| Recursive element trees | ✅ Works today | — | — |
| `is <tag>` type checking | ✅ Works today | — | — |
| Tail call optimization | ✅ Works today | — | — |
| JSON import/export | ✅ Works today | — | — |
| Modules and imports | ✅ Works today | — | — |
| **Element destructuring in match** | 🔴 Missing | High | `~.attr` / `~[i]` access |
| **Exhaustiveness checking** | 🟡 Missing | Medium | `default:` with error |
| **Generics** | 🟡 Missing | Low | `any` type / separate functions |

---

## 9. Comparison: Lambda vs. Racket/Redex

### 9.1 Syntax Comparison

**Box tree definition:**

| | Redex (Racket) | Lambda |
|---|---|---|
| Block box | `(block "div" styles (child1 child2))` | `<block id: "div", styles: s; child1, child2>` |
| Text leaf | `(text "p1" styles "hello" 50.0)` | `<text id: "p1", styles: s; "hello">` |
| View result | `(view "div" 0 0 800 400 (child-views))` | `<view id: "div", x: 0, y: 0, w: 800, h: 400; *cvs>` |

**Layout dispatch:**

| | Redex | Lambda |
|---|---|---|
| Pattern match | `[(layout (block id styles children) avail view) ...]` | `case <block>: layout_block(~, avail)` |
| State transition | `(--> (flex-init c items) (flex-sorted c (sort items)))` | `case <flex-init>: <flex-sorted; sort(state[0])>` |

### 9.2 Advantages of Lambda over Redex

1. **Element syntax is more readable** for tree-structured data than S-expressions
2. **Named attributes** — `box.styles`, `view.width` vs. positional `(second (third term))`
3. **Pipe expressions** — `children \| layout(~, avail)` is more natural than `(map (λ (c) (layout c avail)) children)`
4. **Built-in JSON I/O** — no FFI needed for Radiant integration
5. **JIT compilation** — Lambda model runs with near-native performance vs. Racket interpreter
6. **Single language** — both the formal model and the production engine live in the same project
7. **Collection operations** — `where`, `for`, `sort`, `sum`, `max` built-in

### 9.3 Advantages of Redex over Lambda

1. **Random term generation** from grammar — built into `redex-check` (Lambda needs a library)
2. **Typeset reduction rules** — auto-generates publication-quality figures
3. **Stepping/tracing** — interactive stepper in DrRacket
4. **Established formalism** — Redex is a known tool in PL research
5. **Exhaustiveness checking** — Redex patterns are checked against the language definition

### 9.4 Verdict

For Lambda/Radiant's purposes — an **executable specification for differential testing** — Lambda is the better choice. The Redex advantages (typesetting, academic tooling) are secondary to the practical benefits of a single-language, high-performance, well-integrated model.

---

## 10. Implementation Plan

### Phase 1: Core Framework + Block Layout (2 weeks)

- [ ] Set up module structure (`lambda-layout-model/`)
- [ ] Define box tree element conventions (`box_tree.ls`)
- [ ] Implement common utilities (`layout_common.ls`): length resolution, box model, margin collapsing
- [ ] Implement block flow layout (`layout_block.ls`): vertical stacking, margin collapsing
- [ ] Implement intrinsic sizing (`layout_intrinsic.ls`): min/max content width
- [ ] JSON import/export for box trees and view trees
- [ ] Unit tests for block layout cases

### Phase 2: Inline Layout (2 weeks)

- [ ] Implement inline formatting context (`layout_inline.ls`)
- [ ] Abstract text measurement (parameterized width function)
- [ ] Line box management, wrapping, vertical-align
- [ ] Inline-block interaction

### Phase 3: Flexbox (3 weeks)

- [ ] Implement 9-phase flex algorithm as state machine (`layout_flex.ls`)
- [ ] Flex item collection, order sorting
- [ ] Flex line creation (wrapping)
- [ ] Flexible length resolution (grow/shrink)
- [ ] Main and cross axis alignment
- [ ] Property-based tests (fills space, no negatives, etc.)

### Phase 4: Grid Layout (3 weeks)

- [ ] Track definition parsing (`layout_grid.ls`)
- [ ] Explicit and auto placement
- [ ] Track sizing algorithm, fr unit distribution
- [ ] Grid alignment

### Phase 5: Differential Testing Integration (2 weeks)

- [ ] Add `--export-box-tree` to Radiant CLI
- [ ] Build comparison tool (`compare.ls`)
- [ ] Run against `test/layout/data/` test suite
- [ ] Document and triage discrepancies

### Phase 6: Property Testing Library (1 week)

- [ ] Random box tree generator (`random_gen.ls`)
- [ ] Invariant library (`properties.ls`)
- [ ] Test runner (`test_properties.ls`)

### Phase 7: Graph Layout Extension (2 weeks)

- [ ] Formalize Dagre algorithm phases as element-based state machine
- [ ] Differential testing against `radiant/graph_dagre.cpp`

**Total estimated effort: ~15 weeks** (part-time, interleaved with other work).

---

## 11. Conclusion

Lambda's element types provide a **natural, already-implemented encoding of algebraic data types** that is sufficient for a Redex-equivalent layout specification. The tag name serves as the constructor, attributes serve as named fields, and body content serves as positional payload. Pattern matching via `match` + `case <tag>` dispatches on constructor name. Recursive functions with TCO implement reduction relations.

The only significant language enhancement needed is **element destructuring in `match` patterns** — a scoped, incremental extension rather than a fundamental language redesign. With this addition, the Lambda layout model would be cleaner than the equivalent Redex code, while offering better performance, tighter integration with Radiant, and a lower barrier to entry for contributors who already know Lambda.
