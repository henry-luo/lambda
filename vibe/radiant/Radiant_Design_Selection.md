# Radiant Selection / Range Design Document

## Overview

This document proposes the architectural and API enhancements required to make
Radiant fully conformant with the **W3C Selection API** and **DOM Range** spec
(WPT suite under [`ref/wpt/selection/`](../../ref/wpt/selection)). Today
Radiant has a working *visual* caret/selection layer driven by mouse and
keyboard events, but it operates on a **layout-tree (`View*`) anchor model**
with character offsets local to a `RDT_VIEW_TEXT` run. The W3C Selection /
Range API is defined on the **DOM tree** (`Node` + integer offset where the
offset's units depend on the node type). The two models must be reconciled.

The proposal is layered:

1. Introduce a **DOM-anchored boundary point** (`DomBoundary`) as the canonical
   selection primitive.
2. **Unify** today's `CaretState` and `SelectionState` with new `DomRange` and
   `DomSelection` types. There is one `DomSelection` per document, stored
   directly on the per-document `RadiantState` (the existing StateStore),
   replacing the separate `caret`/`selection` pointers.
3. The cached layout data (`View*`, byte offset, x/y, line/column, blink
   timer) moves *into* `DomRange` / `DomSelection` as resolver-managed
   fields — not into a parallel struct. After layout or hit-test the
   resolver fills these in; before that they are `NULL`/0 and consumers
   request resolution on demand.
4. Expose `Range` and `Selection` through the JS DOM bindings as
   `window.getSelection()`, `document.getSelection()`,
   `document.createRange()`, with full WPT-conformant property/method surface.
5. Wire DOM mutations (text edit, node insert/remove) so live ranges and the
   selection update in spec-conformant ways.

`RadiantState` remains **the** centralized container for all view state in a
document: hover/focus/active targets, dirty tracking, reflow scheduler,
animations — and now the selection. Existing visual rendering and
mouse/keyboard handling keeps working unchanged, while scripts (and the WPT
tests) drive the *same* selection through the DOM API.

---

## 1. Current State (What Exists)

### 1.1 Visual Caret / Selection (layout tree)

Defined in [radiant/state_store.hpp](radiant/state_store.hpp):

```cpp
typedef struct CaretState {
    View*    view;            // RDT_VIEW_TEXT (or container) holding the caret
    int      char_offset;     // byte/char offset within view's text run
    int      line, column;
    float    x, y, height;
    float    iframe_offset_x, iframe_offset_y;
    bool     visible;
    uint64_t blink_time;
    float    prev_abs_x, prev_abs_y, prev_abs_height;
} CaretState;

typedef struct SelectionState {
    View*  view;            // (deprecated; use anchor_view)
    View*  anchor_view;
    View*  focus_view;
    int    anchor_offset, anchor_line;
    int    focus_offset,  focus_line;
    bool   is_collapsed;
    bool   is_selecting;
    float  start_x, start_y, end_x, end_y;
    float  iframe_offset_x, iframe_offset_y;
} SelectionState;
```

API in [radiant/state_store.cpp](radiant/state_store.cpp):

| Function | Behavior |
|----------|----------|
| `caret_set(state, view, offset)` | Position caret at layout view + offset |
| `caret_set_position(state, view, line, col)` | Position by line/column (multi-line) |
| `caret_move(state, delta)` | Step ±1 grapheme, crossing view boundaries |
| `caret_move_to(state, where)` | Jump (line start/end, doc start/end) |
| `caret_move_line(state, delta)` | Vertical line movement |
| `caret_clear(state)` | Hide caret |
| `caret_toggle_blink(state)` | Blink tick |
| `selection_start(state, view, offset)` | Begin selection (anchor=focus) |
| `selection_extend(state, view, offset)` | Move focus point |
| `selection_set(state, anchor_view, anchor_off, focus_view, focus_off)` | Set both endpoints |
| `selection_select_all(state, view)` | Select entire view text |
| `selection_clear(state)` | Collapse and hide |
| `selection_has(state)` | True iff non-collapsed |

### 1.2 Hit-testing & event integration

`radiant/event.cpp` walks the view tree from `target_block_view()` /
`target_inline_view()` to map `(mouse_x, mouse_y) → View*`. There is *not* a
public function that returns a `(DomNode*, offset)` pair from a viewport
coordinate — that mapping happens implicitly inside text mouse handlers.

### 1.3 Rendering

`render_selection()` ([radiant/render.cpp](radiant/render.cpp#L3522)) draws a
single rectangle for `sel->view` using its `start_x/y/end_x/y`. **Multi-line
and cross-DOM-node ranges are not rendered correctly today** — selection
across two `<p>` elements paints only inside the anchor view.

### 1.4 DOM model

[lambda/input/css/dom_node.hpp](lambda/input/css/dom_node.hpp) defines
`DomNode` polymorphically with subclasses `DomElement`, `DomText`,
`DomComment`. Text storage is `DomText::text` (a Lambda `String*`). There
is no built-in concept of a "DOM offset" — but offsets into `DomText::text`
(by 16-bit code unit, per the spec) are well-defined. Children of a
`DomElement` are an ordered list, so an offset into an element ranges
`[0, child_count]`.

### 1.5 JS bindings

[lambda/js/js_dom.cpp](lambda/js/js_dom.cpp) exposes the DOM but **has no
binding for `Selection`, `Range`, `getSelection`, `createRange`, or
`StaticRange`** (greppable: zero matches in `lambda/js/`). Consequently the
WPT runner [test/wpt/test_wpt_selection_gtest.cpp](test/wpt/test_wpt_selection_gtest.cpp)
currently has 81 discovered tests, 32 deliberately skipped, and the
remaining 49 all fail at the first `getSelection is not a function` /
`document.createRange is not a function`.

### 1.6 Reactive plumbing

Selection lives inside `RadiantState`, already integrated with
`reflow_schedule()`, dirty rect tracking, focus management, and the keyboard
event path. The `state->is_dirty` / `needs_reflow` / `needs_repaint` flags
will be reused by the DOM-side selection updates described below.

---

## 2. Design Goals

1. **Spec conformance.** Pass the executable subset of `ref/wpt/selection/`
   (the 49 currently-failing tests, plus everything we can lift out of the
   skip list later).
2. **Single source of truth.** One selection per `DomDocument`, accessible
   identically from JS and from the rendering/input layer.
3. **Reflow-stable.** Selection survives reflow without losing endpoints.
   Layout-tree caches are recomputed; DOM boundaries are immortal.
4. **Live ranges.** Ranges hold weak references to nodes; DOM mutation
   updates them per WHATWG DOM §5.5 ("Boundary points").
5. **Cheap when idle.** Zero allocations on the hot path when selection is
   collapsed and untouched.
6. **Backwards compatible.** Existing `caret_*` / `selection_*` APIs keep
   their signatures; their implementations switch to the new DOM-anchored
   model.
7. **No `std::*` types**, all storage from the existing pool/arena, all
   logging through `log_debug/info/error`, per project rules.

---

## 3. Core Data Types

### 3.1 `DomBoundary` — canonical boundary point

```cpp
// radiant/dom_range.hpp
typedef struct DomBoundary {
    DomNode* node;     // owning node (DomElement or DomText)
    uint32_t offset;   // for DomText: UTF-16 code-unit offset into text
                       // for DomElement: child index in [0, child_count]
                       // for DomComment / others: 0..length per spec
} DomBoundary;
```

The spec defines the **valid offset range** as `[0, length]`, where `length`
is the number of UTF-16 code units for a CharacterData node and the number
of children for any other node. We must store offsets in **UTF-16 code units**
because every WPT test that calls `setStart(text, n)` reasons in JS string
indices. Today `DomText::text` is a Lambda `String` (UTF-8 bytes). We add
two helpers:

```cpp
uint32_t dom_text_utf16_length(const DomText* t);                      // O(n)
uint32_t dom_text_utf8_offset_from_utf16(const DomText* t, uint32_t u16);
uint32_t dom_text_utf16_offset_from_utf8(const DomText* t, uint32_t u8);
```

These are used at the boundary between *DOM-API offsets* and Radiant's
internal *layout-byte offsets*. A length cache on `DomText` (lazily
populated, invalidated on text mutation) keeps this O(1) for the common
ASCII case.

#### Boundary comparison

```cpp
typedef enum {
    DOM_BOUNDARY_BEFORE   = -1,
    DOM_BOUNDARY_EQUAL    =  0,
    DOM_BOUNDARY_AFTER    =  1,
    DOM_BOUNDARY_DISJOINT =  2,   // different DOM trees
} DomBoundaryOrder;

DomBoundaryOrder dom_boundary_compare(const DomBoundary* a,
                                      const DomBoundary* b);
```

Implements the spec algorithm: walk to LCA, compare child indices. Required
by `Range.compareBoundaryPoints()`, `Selection.containsNode()`, and the
sort step in `Selection.collapse`/`extend`.

### 3.2 `DomRange` — live range (unifies the old per-endpoint visual cache)

```cpp
typedef struct DomRange {
    RadiantState* state;            // owning state store (per document)
    DomBoundary   start;
    DomBoundary   end;              // start <= end (invariant maintained)
    bool          is_live;          // false for StaticRange
    uint32_t      id;               // monotonically assigned, for diagnostics
    struct DomRange* prev;          // doubly-linked into state->live_ranges
    struct DomRange* next;
    uint32_t      ref_count;        // referenced by JS handle and/or selection

    // ----- Layout cache (filled by the resolver, invalidated on reflow) -----
    // These replace the equivalent fields on the old SelectionState/CaretState.
    bool   layout_valid;            // false until resolver runs
    View*  start_view;              // RDT_VIEW_TEXT containing start.offset
    int    start_byte_offset;       // UTF-8 byte offset within start_view
    int    start_line, start_column;
    float  start_x, start_y;
    View*  end_view;
    int    end_byte_offset;
    int    end_line, end_column;
    float  end_x, end_y;
    float  iframe_offset_x, iframe_offset_y;
} DomRange;
```

All live ranges hang off `state->live_ranges` (the per-document StateStore) so
that DOM mutation hooks (see §6) can fix them up. The `layout_*` block is the
old `SelectionState` data inlined into the range, so there is *one* struct
per logical selection range, not two.

#### Range API

Mirror the spec exactly. Names use Radiant's `snake_case`:

| Spec method | C function |
|-------------|------------|
| `setStart(node, offset)` | `dom_range_set_start` |
| `setEnd(node, offset)` | `dom_range_set_end` |
| `setStartBefore/After(node)` | `dom_range_set_start_before/after` |
| `setEndBefore/After(node)` | `dom_range_set_end_before/after` |
| `collapse(toStart)` | `dom_range_collapse` |
| `selectNode(node)` | `dom_range_select_node` |
| `selectNodeContents(node)` | `dom_range_select_node_contents` |
| `compareBoundaryPoints(how, other)` | `dom_range_compare_boundary_points` |
| `cloneRange()` | `dom_range_clone` |
| `detach()` | `dom_range_detach` (no-op per modern spec, but defined) |
| `isPointInRange(node, off)` | `dom_range_is_point_in_range` |
| `comparePoint(node, off)` | `dom_range_compare_point` |
| `intersectsNode(node)` | `dom_range_intersects_node` |
| `cloneContents()` | `dom_range_clone_contents` (returns DocumentFragment) |
| `extractContents()` | `dom_range_extract_contents` |
| `deleteContents()` | `dom_range_delete_contents` |
| `surroundContents(newParent)` | `dom_range_surround_contents` |
| `insertNode(node)` | `dom_range_insert_node` |
| `toString()` | `dom_range_to_string` |
| read-only `startContainer/Offset`, `endContainer/Offset`, `commonAncestorContainer`, `collapsed` | accessors on `DomRange*` |

All mutating algorithms must run inside the same DOM-mutation envelope
(`dom_mutation_begin/end`) so that other live ranges and the selection are
fixed up exactly once.

### 3.3 `DomSelection` — the document's editing selection (also the caret)

A **collapsed** `DomSelection` *is* the caret. We do not keep a separate
`CaretState` struct: the caret is simply `selection->is_collapsed == true`,
rendered as a 1-pixel vertical bar at `range[0].start`'s resolved position.
Blink state and visibility — the only fields the old `CaretState` carried
that are not derivable from a range — move onto `DomSelection`.

```cpp
typedef enum {
    DOM_SEL_DIR_NONE = 0,    // empty or single-range non-directional
    DOM_SEL_DIR_FORWARD,     // anchor before focus
    DOM_SEL_DIR_BACKWARD,    // anchor after focus
} DomSelectionDirection;

typedef struct DomSelection {
    RadiantState* state;          // owning state store
    DomRange**    ranges;         // dynamically grown small array (see note)
    uint32_t      range_count;
    uint32_t      range_capacity;
    DomBoundary   anchor;         // mirrors range edge per direction
    DomBoundary   focus;
    DomSelectionDirection direction;
    bool          is_collapsed;   // derived; cached for fast JS access

    // ----- Caret presentation (only meaningful when is_collapsed) -----
    bool          caret_visible;  // blink toggle
    uint64_t      caret_blink_time;
    float         caret_height;   // resolved with anchor; 0 = stale
    // Phase 19 dirty-rect repaint tracking, formerly on CaretState
    float         caret_prev_abs_x, caret_prev_abs_y, caret_prev_abs_height;
} DomSelection;
```

> **Note on multi-range:** the spec allows any number of ranges, but every
> mainstream browser (and every WPT test we need to pass) only ever has 0
> or 1. We support `range_count == 0 | 1` rigorously and treat
> `addRange()` of additional ranges as a no-op per the WHATWG note,
> matching Chromium's behavior the WPT corpus assumes.

#### Selection API

| Spec member | C function |
|-------------|------------|
| `anchorNode`, `anchorOffset` | `dom_selection_anchor_node/offset` |
| `focusNode`, `focusOffset` | `dom_selection_focus_node/offset` |
| `isCollapsed` | `dom_selection_is_collapsed` |
| `rangeCount` | `dom_selection_range_count` |
| `type` (`"None"`/`"Caret"`/`"Range"`) | `dom_selection_type` |
| `getRangeAt(i)` | `dom_selection_get_range_at` |
| `addRange(range)` | `dom_selection_add_range` |
| `removeRange(range)` | `dom_selection_remove_range` |
| `removeAllRanges()` | `dom_selection_remove_all_ranges` |
| `empty()` (alias) | `dom_selection_empty` |
| `collapse(node, off)` | `dom_selection_collapse` |
| `setPosition(node, off)` (alias) | `dom_selection_set_position` |
| `collapseToStart/End()` | `dom_selection_collapse_to_start/end` |
| `extend(node, off)` | `dom_selection_extend` |
| `setBaseAndExtent(an, ao, fn, fo)` | `dom_selection_set_base_and_extent` |
| `selectAllChildren(node)` | `dom_selection_select_all_children` |
| `modify(alter, dir, granularity)` | `dom_selection_modify` |
| `deleteFromDocument()` | `dom_selection_delete_from_document` |
| `containsNode(n, partial)` | `dom_selection_contains_node` |
| `toString()` | `dom_selection_to_string` |

`DomSelection` is owned by the per-document `RadiantState`; created lazily
on first access.

---

## 4. Integration with `RadiantState` (StateStore)

`RadiantState` is **the** centralized container for all view state in a DOM
document. The selection and live ranges live there directly — *not* on
`DomDocument` — keeping `DomDocument` as a pure data-model object and
concentrating all interactive/runtime state in one place.

### 4.1 StateStore additions (replaces today's `caret` + `selection` pair)

```cpp
// radiant/state_store.hpp
typedef struct RadiantState {
    /* ... existing pool/arena/state_map/dirty/reflow/focus fields ... */

    // -- Selection & ranges (REPLACES the old CaretState* caret and
    //    SelectionState* selection pointers) --
    DomSelection* selection;           // single editing selection (lazy)
    DomRange*     live_ranges;         // doubly-linked list head
    uint32_t      next_range_id;
    bool          selection_layout_dirty; // set by mutation, cleared by resolver

    /* ... cursor, drag_target, animation_scheduler, etc. ... */
} RadiantState;
```

The old `CaretState*` and `SelectionState*` fields are **deleted**. Their
contents are now:

| Old field | New home |
|-----------|----------|
| `state->caret->view` / `char_offset` / `line` / `column` | `selection->ranges[0]->start_view` / `start_byte_offset` / `start_line` / `start_column` (when `is_collapsed`) |
| `state->caret->x` / `y` / `height` | `selection->ranges[0]->start_x/y` + `selection->caret_height` |
| `state->caret->visible` / `blink_time` | `selection->caret_visible` / `caret_blink_time` |
| `state->caret->prev_abs_*` | `selection->caret_prev_abs_*` |
| `state->selection->anchor_view`, `focus_view`, `*_offset`, `*_line` | `selection->ranges[0]->start_view`/`end_view` + the inlined layout cache; logical anchor/focus order derived from `selection->direction` |
| `state->selection->start_x/y`, `end_x/y`, `iframe_offset_*` | same fields on `selection->ranges[0]` |
| `state->selection->is_collapsed` | `selection->is_collapsed` (now the canonical caret-vs-range discriminant) |
| `state->selection->is_selecting` | `state->is_selecting` (transient mouse-drag flag stays on the StateStore, not on `DomSelection`, because it is input-state, not selection-state) |

### 4.2 Resolver: DOM boundary → layout cache

Layout coords inside `DomRange` are filled lazily by a resolver. Renderers
and input handlers do not read these fields directly; they call:

```cpp
// radiant/dom_range_resolver.cpp
void resolve_range_layout(RadiantState* state, DomRange* range);
void resolve_selection_layout(RadiantState* state, DomSelection* sel);
void resolve_boundary_layout(RadiantState* state,
                             const DomBoundary* b,
                             View** out_view, int* out_byte_offset,
                             float* out_x, float* out_y, float* out_h);
```

The resolver writes results back into `range->start_*` / `end_*` and sets
`range->layout_valid = true`. Triggers:

- After any `DomSelection`/`DomRange` mutation → mark
  `range->layout_valid = false` and `state->selection_layout_dirty = true`.
- Before `render_selection()` / `render_caret()` reads the fields, call
  `resolve_selection_layout()` if dirty.
- After `reflow_html_doc()` always invalidate every live range
  (`live_ranges` walk).

Because cache and DOM data live in **the same struct**, there is no
bookkeeping to keep two parallel objects in sync — invalidation is just
`range->layout_valid = false`.

### 4.3 Reverse direction: input → DOM

Existing input handlers currently call `selection_start(state, view, off)`
with a layout-tree pair. They will be replaced by:

```cpp
DomBoundary hit_test_to_boundary(RadiantState* state, float vx, float vy);
```

which maps a viewport coordinate to `(DomNode*, offset)` by walking the
layout tree to find the text view, then back to its `DomText` source and
converting the byte offset back to a UTF-16 offset. The result is fed to
`dom_selection_collapse` / `dom_selection_extend` — the same code paths the
JS API uses. As an optimization, the resolver may pre-populate the new
range's `start_view`/`start_byte_offset` directly from the hit-test result
(skipping a redundant DOM→layout walk on the first render after a click)
and mark `layout_valid = true` until the next reflow.

### 4.4 Multi-line / cross-node rendering

`render_selection()` is rewritten to iterate the *layout fragments* covered
by the selection's single range:

1. Resolve `start` and `end` to `(View*, byte_offset)` pairs.
2. Walk the inline-fragment list from start to end (`TextRect`/`InlineRun`
   chain) — this already exists for hit-testing.
3. For each fragment, compute its on-screen rect and emit a highlight
   rectangle (one per text-run, per line).
4. For block transitions, emit a "tail-of-line" stripe to the line edge per
   the standard browser behavior.

This naturally fixes today's single-rectangle-only limitation that breaks
selections crossing `<p>` boundaries.

---

## 5. JS Bindings

Add a new translation unit `lambda/js/js_dom_selection.cpp` registered from
`js_dom.cpp`'s `register_dom_globals()`:

### 5.1 New globals / methods

| Global / accessor | Implementation |
|-------------------|----------------|
| `window.getSelection()` | Returns the singleton `DomSelection` wrapped as JS object |
| `document.getSelection()` | Same |
| `document.createRange()` | Allocates a fresh `DomRange` (start = end = (document, 0)) |

### 5.2 JS object shape

Two JS host classes:

- **`Selection`** — wraps a `DomSelection*`. All properties (`anchorNode`,
  `focusNode`, `isCollapsed`, `rangeCount`, `type`) are getters that read
  the live struct. All methods marshal arguments and call the C API.
- **`Range`** — wraps a `DomRange*`. Lifetime managed by JS GC + an
  internal `ref_count` so that `selection.getRangeAt(0)` and stored JS
  references both keep the underlying `DomRange` alive.

Argument coercion follows WebIDL rules our JS already uses elsewhere
(`Node`, `unsigned long`). Errors must throw the right `DOMException`
(`IndexSizeError`, `InvalidNodeTypeError`, `WrongDocumentError`,
`HierarchyRequestError`, `NotFoundError`, `InvalidStateError`).

### 5.3 Shim removal

After bindings ship, delete the temporary `getSelection` workarounds (none
exist today) and shrink the WPT skip list in
[test/wpt/test_wpt_selection_gtest.cpp](test/wpt/test_wpt_selection_gtest.cpp).
The remaining "SKIP" entries should be only those that genuinely require
unsupported subsystems (Shadow DOM, drag, IME, video).

---

## 6. DOM Mutation & Live-Range Maintenance

The WHATWG DOM spec §5.5.3 defines exact rules for how boundary points must
move when nodes are inserted, removed, or split. Today Radiant performs
mutations in a few places (`innerHTML`, `appendChild`, `removeChild`,
`Text` data setters in `js_dom.cpp`, plus future text editing). We
introduce mutation envelopes:

```cpp
void dom_mutation_pre_insert(DomNode* parent, DomNode* node, DomNode* child);
void dom_mutation_post_insert(DomNode* parent, DomNode* node);

void dom_mutation_pre_remove(DomNode* parent, DomNode* child);
void dom_mutation_post_remove(DomNode* parent, DomNode* child);

void dom_mutation_text_replace_data(DomText* text,
                                    uint32_t offset, uint32_t count,
                                    const char* utf8, uint32_t utf8_len);

void dom_mutation_text_split(DomText* text, uint32_t offset, DomText* new_node);
```

Each is responsible for walking `state->live_ranges` (the per-document
StateStore's range list) and applying the
spec's adjustment rules; the `DomSelection`'s anchor/focus are kept in sync
via its (single) range.

For removals, a node deletion cascades down its subtree: any range
endpoint that lies strictly inside the removed subtree is moved to
`(parent, child_index)` per spec.

For text data replacement (the underlying primitive of
`appendData/insertData/deleteData/replaceData/normalize`), endpoint
offsets shift by the standard formula.

These hooks also flip `state->selection_layout_dirty = true` and
`state->needs_repaint = true` so the visual layer redraws.

---

## 7. Special Considerations

### 7.1 `Selection.modify(alter, direction, granularity)`

This is the trickiest method. WPT exercises only a subset:

- `alter` ∈ {`"move"`, `"extend"`}
- `direction` ∈ {`"forward"`, `"backward"`, `"left"`, `"right"`}
- `granularity` ∈ {`"character"`, `"word"`, `"line"`, `"paragraph"`,
  `"lineboundary"`, `"sentence"`, `"sentenceboundary"`,
  `"paragraphboundary"`, `"documentboundary"`}

We already have `caret_move()`, `caret_move_line()`, `caret_move_to()`
that operate at character/line/document granularity — they become the
implementation for `modify()`. **Word/sentence boundaries** require a
unicode word-break iterator. We adopt `unicode/icu`-equivalent tables
already used by `lib/utf8proc/`.

### 7.2 `deleteFromDocument()`

Defined as: if the selection is collapsed, do nothing; otherwise call
`dom_range_delete_contents` on `getRangeAt(0)`. After deletion, collapse
the selection to the range's new start. Must trigger the same reflow as
any other text mutation.

### 7.3 `containsNode(node, allowPartialContainment)`

Implemented by comparing `(node, 0)` and `(node, length(node))` against
the selection's range using `dom_boundary_compare`. The WPT tests in
`containsNode-*.html` are exhaustive on edge cases (start/end at the same
boundary as the node, partial overlap rules).

### 7.4 `toString()`

Per spec, returns the *concatenated text data of all Text nodes contained
or partially contained by the range*, **not** what the user sees. This
ignores CSS (`display:none`, `visibility:hidden`, generated content).
Because we operate on the DOM tree directly we get this for free.

### 7.5 Iframes

Today `SelectionState` carries `iframe_offset_x/y` for visual rendering.
With the DOM-anchored model each `DomDocument` has its **own**
`DomSelection`; the iframe offset is purely a rendering concern handled by
the resolver. WPT iframe tests are mostly in the skip list (require nested
documents); we leave them skipped initially.

### 7.6 Shadow DOM, Selection Events, `selectstart`/`selectionchange`

Out of scope for this proposal. The skip list keeps these tests skipped.
Event firing can be added later (`selectionchange` would slot naturally
into the existing event dispatch).

### 7.7 UTF-16 vs UTF-8

The single most pervasive issue. All public selection/range offsets are
**UTF-16 code units** (matching JS `String.length`). All Radiant internal
offsets remain **UTF-8 byte offsets** because the renderer, font shaper,
and `caret_move()` all operate on bytes. Conversion happens **only at the
DOM API boundary**, in:

- `dom_range_set_start/end` (UTF-16 → cached UTF-8 in resolver)
- `dom_selection_anchor_offset` getter (UTF-8 → UTF-16)
- `dom_range_to_string` (works on raw `DomText::text`, no conversion)

A small `Utf16OffsetCache` per `DomText` (built lazily, invalidated on
text mutation) keeps this O(1) amortized.

---

## 8. Implementation Phases

### Phase 1 — DOM types & tests (no JS, no rendering changes)

1. Add `radiant/dom_range.hpp` / `dom_range.cpp` with `DomBoundary`,
   `DomRange`, `dom_boundary_compare`, range constructors and offsets-only
   methods (`setStart`, `setEnd`, `collapse`, `selectNode`,
   `compareBoundaryPoints`, `isPointInRange`, `comparePoint`).
2. Add `DomSelection` with anchor/focus/range bookkeeping and the
   non-DOM-mutating methods (`collapse`, `extend`, `setBaseAndExtent`,
   `selectAllChildren`, `getRangeAt`, `addRange`, `removeRange`,
   `removeAllRanges`, `containsNode`).
3. Add `dom_text_utf16_length` etc. with `Utf16OffsetCache`.
4. **GTest** `test/test_dom_range_gtest.cpp` exercising all of the above
   directly (no JS) against a synthetic DOM.

Exit criteria: GTest 100% pass; existing baselines green.

### Phase 2 — JS bindings

1. `lambda/js/js_dom_selection.cpp` — `Selection`/`Range` host objects,
   register `window.getSelection`, `document.getSelection`,
   `document.createRange`.
2. Update `js_dom.cpp` registration tables (the `doc_methods[]` array
   currently lists `getElementById` etc. — add the new methods).
3. Wire `DOMException` throws.

Exit criteria: WPT runner shows the easy `getRangeAt`, `setStart`,
`collapse`, `isCollapsed`, `cloneRange`, `comparePoint`,
`compareBoundaryPoints`, `containsNode`, `toString` tests passing
(roughly 60% of the non-skipped 49).

### Phase 3 — Mutation envelopes & live ranges

1. Refactor `js_dom_set_property` (textContent, innerHTML, data setter)
   to call `dom_mutation_text_replace_data`.
2. Add hooks around `appendChild`, `removeChild`, `insertBefore`,
   `replaceChild`, `Text.splitText`, `Element.normalize`.
3. Implement live-range adjustment per spec.

Exit criteria: WPT range mutation tests (`addRange-*`, `removeAllRanges`,
`modify-*` move-only) pass.

### Phase 4 — Range mutation methods

1. `cloneContents`, `extractContents`, `deleteContents`,
   `surroundContents`, `insertNode`, `Selection.deleteFromDocument`.
2. `DocumentFragment` support if not already present.

Exit criteria: `extractContents-*`, `deleteContents-*`,
`surroundContents-*`, `cloneContents-*` WPT tests pass.

### Phase 5 — `Selection.modify` & word breaking

1. Word/sentence iterator using `lib/utf8proc/`.
2. Implement `modify()` for all granularities.
3. Promote layout-cache resolver to recompute on every reflow.

Exit criteria: `selection-modify-*` WPT tests pass.

### Phase 6 — Rendering & input wiring

1. Rewrite `render_selection()` for multi-line / cross-node spans
   (§4.4).
2. Add `hit_test_to_boundary()` and reroute mouse handlers through
   `dom_selection_collapse`/`extend`.
3. Reroute `caret_*` user-API to update `DomSelection` first, then
   re-resolve. Existing call-sites unchanged.

Exit criteria: visual selection in the GUI matches the WPT-driven DOM
selection one-to-one; existing `make test-radiant-baseline` 100% green.

### Phase 7 — Polish

1. Trim WPT skip list.
2. Add `selectionchange` event (optional).
3. Documentation in `doc/dev/Radiant_Selection.md` with API examples.

---

## 9. File / Module Layout

```
radiant/
  dom_range.hpp              ← NEW: DomBoundary, DomRange, DomSelection types
  dom_range.cpp              ← NEW: spec algorithms
  dom_range_resolver.hpp     ← NEW: DOM ↔ layout coordinate bridge
  dom_range_resolver.cpp     ← NEW
  state_store.hpp            ← MODIFY: DELETE CaretState/SelectionState pointers,
                                       ADD selection / live_ranges /
                                       next_range_id / selection_layout_dirty
  state_store.cpp            ← MODIFY: caret_*/selection_* delegate to
                                       DomSelection on state->selection
  event.cpp                  ← MODIFY: hit-test → DomBoundary path
  render.cpp                 ← MODIFY: render_selection multi-fragment;
                                       render_caret reads selection->ranges[0]

lambda/input/css/
  dom_document.hpp           ← UNCHANGED (no selection fields here)
  dom_node.hpp               ← MODIFY: optional Utf16OffsetCache pointer on DomText
  dom_text.cpp               ← MODIFY: invalidate cache on mutation

lambda/js/
  js_dom_selection.hpp       ← NEW
  js_dom_selection.cpp       ← NEW: Selection/Range bindings
  js_dom.cpp                 ← MODIFY: register globals, wire mutation hooks
  js_dom.h                   ← MODIFY: declare new entry points

test/
  test_dom_range_gtest.cpp   ← NEW: pure C++ unit tests for §3
  wpt/
    test_wpt_selection_gtest.cpp  ← MODIFY: shrink SKIP_SUBSTRINGS as
                                            phases land
    wpt_testharness_shim.js       ← MODIFY: nothing required; getSelection
                                            comes from real DOM bindings now
```

---

## 10. Risks & Open Questions

1. **DocumentFragment** support is currently minimal in the JS DOM. Range
   methods that return a fragment (`cloneContents`, `extractContents`)
   will need it shored up.
2. **`extractContents`/`surroundContents`** require deep node cloning and
   carry edge-cases around CDATA, entity references, and DocumentType
   nodes — Radiant's HTML-only DOM avoids most of those, but the WPT
   tests do try `.xhtml` variants.
3. **Layout invalidation cost** of mutating ranges every text input
   keystroke. Mitigated by batching (`state_begin_batch/end_batch`) and
   by only rerendering dirty rects.
4. **Selection direction semantics** when `addRange` is called more than
   once — we explicitly choose the Chromium behavior (ignore extras).
5. **Multi-document worlds.** Each `DomDocument` has its own
   `DomSelection`; `window.getSelection()` returns the top-level
   document's selection. Iframe selection access is deferred (Phase 7+).

---

## 11. Test Strategy

### Unit (GTest)

`test/test_dom_range_gtest.cpp` covers, with synthetic DOMs:

- All `DomBoundary` ordering edge cases (LCA at root, sibling, descendant).
- `DomRange` invariant maintenance under `setStart`/`setEnd`.
- Live-range adjustment for insert/remove/text-replace per WHATWG table.
- `DomSelection` collapse/extend/modify/contains/toString.
- UTF-16 ↔ UTF-8 boundary conversion on multi-byte text.

### Integration (WPT)

`test/wpt/test_wpt_selection_gtest.cpp` is the truth source. After each
phase the SKIP list shrinks; CI runs the runner so regressions are
visible per-test.

### Visual smoke

Existing `make test-radiant-baseline` exercises caret/selection
rendering; we keep it green and add a new HTML page with cross-paragraph
selection to verify §4.4 multi-line rendering.

---

## 12. Summary

The architectural pivot is small but decisive: **make the DOM the source
of truth for caret and selection**, treat the existing `View*`-anchored
state as a recomputable cache, then add the W3C `Selection` and `Range`
APIs on top of that DOM-anchored state. Every existing behavior (caret
blinking, focus rings, multi-line caret movement, mouse-drag selection)
keeps working because the visual layer becomes a *consumer* of
`DomSelection` instead of being its primary owner. New JS bindings give
WPT tests the API surface they expect, and a small set of mutation
envelopes guarantees that live ranges and the selection respond
correctly to DOM edits.

The end-state: Radiant passes the executable subset of `ref/wpt/selection`
and exposes a Selection / Range API that scripts (Sizzle, jQuery,
contenteditable shims, future React) can rely on without surprise.

---

**Last Updated:** 2026-04-27
**Status:** Proposal — pending review
**Related:** [Radiant_Design_State.md](Radiant_Design_State.md) ·
[test/wpt/test_wpt_selection_gtest.cpp](test/wpt/test_wpt_selection_gtest.cpp) ·
[ref/wpt/selection/](ref/wpt/selection)
