# Radiant Selection / Range Design Document

## Implementation Status (2026-04-27)

**Phases 1–6 have shipped.** A few design choices changed during
implementation; this section is the authoritative summary, the rest of
the document remains the original design narrative with inline notes
where it diverges.

| Phase | Status | Notes |
|-------|--------|-------|
| 1 — DOM types & GTest | ✅ done | `radiant/dom_range.{hpp,cpp}`, 66/66 GTest. |
| 2 — JS bindings | ✅ done | `lambda/js/js_dom_selection.{hpp,cpp}`. |
| 3 — Mutation envelopes & live ranges | ✅ done | `dom_range_replace_for_mutation` + spec adjustments. |
| 4 — Range mutation methods | ✅ done | `cloneContents` / `extractContents` / `deleteContents` / `surroundContents` / `insertNode`. |
| 5 — `Selection.modify` & word breaking | ✅ done | utf8proc-backed iterator. |
| 6 — Rendering & input wiring | ✅ done (revised, see below) | bidirectional sync, NOT a unification. |
| 6A — `render_selection()` activated | ✅ done | inline glyph painter disabled; multi-rect overlay drives text-selection backgrounds. |
| 6B — Consolidated legacy storage | ✅ done | `CaretState` / `SelectionState` now owned by `DomSelection`; single allocation; types preserved. |
| 7 — Polish | 🔵 pending | shrink WPT skip list, `selectionchange` event, doc page. |

### Phase 6 deviation from the original plan

The original plan (§3.3 / §4.1) called for **deleting**
`CaretState` / `SelectionState` and making `DomSelection` the sole
container. **What actually shipped is bidirectional sync** between the
two, not unification:

- `RadiantState` keeps `caret`, `selection`, AND `dom_selection`. The
  legacy structs cache precise visual coordinates (caret X/Y/height
  computed glyph-precisely by `event.cpp::calculate_position_from_char_offset`);
  `DomSelection` holds the spec-canonical anchor/focus.
- A re-entry-guarded sync (counter `dom_selection_sync_depth` on
  `RadiantState`) propagates changes both ways:
  - **legacy → DOM**: `dom_selection_sync_from_legacy_selection()` and
    `_from_legacy_caret()` run at the end of `caret_set` /
    `selection_start` / `selection_extend` / `selection_extend_to_view`.
  - **DOM → legacy**: `legacy_sync_from_dom_selection()` runs at the
    end of `sync_anchor_focus()` in `dom_range.cpp`, called by every
    spec mutator (collapse / extend / setBaseAndExtent / etc.).
- The renderer's selection highlight is still painted **inline per glyph**
  in `render_text_view` from the legacy `SelectionState`. The new
  multi-rect `render_selection()` (see §4.4) is implemented and uses
  `dom_range_for_each_rect()`, but is currently disabled in
  `render_ui_overlays` to keep zero behavior change. Phase 7 may flip
  the switch.
- Mouse handlers in `event.cpp` were **not** rerouted through
  `dom_hit_test_to_boundary`. The bidirectional sync makes the legacy
  call sites produce a correct `DomSelection` automatically without
  losing event.cpp's glyph-precise caret coordinates.

**Why the deviation:** the resolver (`dom_range_resolver.cpp`) uses
linear interpolation within a `TextRect` to keep it glyph-free and
unit-testable (no GLFW/font dependency). `event.cpp` already had a
pixel-perfect path. Replacing the legacy structs entirely would have
regressed caret precision; the bidirectional sync gives JS the
canonical DOM-anchored model while the renderer keeps the precise
visual cache.

### Phase 6A — `render_selection()` activated

- `render.cpp::render_text_view`: inline per-glyph selection background
  painter disabled (forced `has_selection = false`). The helper
  functions `get_selection_range_for_view` / `is_view_in_selection`
  are retained for the cross-view image-overlay path at
  `render.cpp:~3074`.
- `render.cpp::render_ui_overlays`: now calls `render_selection()`
  unconditionally. Reads `state->dom_selection`, calls
  `dom_range_for_each_rect()`, paints one #0078D7 │ α=0x80 rectangle
  per visual fragment. Cross-text-node and cross-paragraph selections
  paint correctly for the first time.
- Slight precision trade: within a single `TextRect`, rectangle edges
  use linear interpolation rather than the glyph walk in
  `event.cpp::calculate_position_from_char_offset`. The visible caret
  itself remains glyph-precise (drawn from `state->caret` which is
  still updated by event.cpp's pixel-perfect path).
- Tests: 5713/5717 (no new regressions).

### Phase 6B — Consolidated legacy storage under `DomSelection`

`CaretState` and `SelectionState` are now **owned by `DomSelection`**
instead of being independently arena-allocated by the
`caret_*` / `selection_*` API:

- `DomSelection` carries `CaretState* caret;` and
  `SelectionState* selection;` pointer fields
  (`radiant/dom_range.hpp`).
- `dom_selection_create()` calls
  `dom_selection_attach_legacy_storage(s, state)` (strong def in
  `state_store.cpp`, weak no-op fallback in `dom_range.cpp` for
  unit-test linkage). The helper allocates both legacy structs into
  `state->arena` and aliases `state->caret = s->caret;`
  `state->selection = s->selection;`.
- All lazy-allocation paths in `caret_set` / `caret_set_position` /
  `selection_start` / `selection_set` / `legacy_sync_from_dom_selection`
  are replaced with a single `sync_ensure_selection(state)` call.
  Storage is guaranteed to exist after the first `DomSelection`
  access; subsequent paths are pure writes.

**What this delivered:**
- Single arena allocation for the trio (DomSelection + CaretState +
  SelectionState) sharing one lifetime owned by `DomSelection`.
- Five lazy-alloc blocks deleted; null-check overhead reduced.
- All call sites unchanged — `state->caret->x` and
  `state->selection->is_selecting` are the same memory they were
  before, so `event.cpp`, `render_caret`, `render_caret_svg`,
  `view_pool::dump_state`, `event_sim` helpers all keep working.

**What this did NOT do** (and why):
- The typedefs `CaretState` / `SelectionState` themselves remain.
  Removing the names would force rewriting ~25 field-access sites
  across `event.cpp` (mouse drag/click handlers updating glyph-precise
  caret X/Y/height + iframe offset), `render.cpp::render_caret`,
  `render_svg.cpp::render_caret_svg`, `view_pool::dump_state`, and
  `event_sim::assert_caret/_selection` — all of which currently rely
  on the C struct field syntax. The substantive wins (no parallel
  allocations, no scattered alloc paths, single owner) are achieved
  by 6B without that risk.
- The `dom_selection_sync_depth` re-entry counter and the bidirectional
  sync helpers stay. Even with shared backing storage, the
  `(View*, byte_offset)` ↔ `(DomNode*, utf16_offset)` conversion is
  real work that must run on every mutation — same memory doesn't
  obviate the need for re-entry protection.

**Test results:** `test_dom_range_gtest` 66/66; `make test-radiant-baseline`
5713/5717 (only 4 pre-existing render-visual failures, no new
regressions).

---

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

> **Implementation note (Phase 6 actual):** `CaretState` and
> `SelectionState` were **not** deleted. They are kept in parallel
> with `DomSelection` and bidirectionally synced. See the
> Implementation Status section above. The fields below all exist on
> `DomSelection` as designed, but the renderer reads precise visual
> coordinates from the legacy structs.

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

### 4.1 StateStore additions (Phase 6 actual)

```cpp
// radiant/state_store.hpp
typedef struct RadiantState {
    /* ... existing pool/arena/state_map/dirty/reflow/focus fields ... */

    // Legacy interactive state (kept; see Implementation Status note).
    CaretState*     caret;
    SelectionState* selection;

    // DOM-spec selection / live ranges (additive).
    DomSelection*  dom_selection;            // single editing selection (lazy)
    DomRange*      live_ranges;              // doubly-linked list head
    uint32_t       next_range_id;
    bool           selection_layout_dirty;   // set by mutation, cleared by resolver
    int            dom_selection_sync_depth; // re-entry guard for the
                                             // legacy <-> DOM sync hooks

    /* ... cursor, drag_target, animation_scheduler, etc. ... */
} RadiantState;
```

> **Original plan (not done):** delete `CaretState*` / `SelectionState*`.
> **Actual:** both kept and bidirectionally synced (see Implementation
> Status). The mapping below remains the *logical* equivalence; in the
> shipped code each row is held in both places and kept in sync.

| Old field | Logical equivalent on `DomSelection` |
|-----------|----------------|
| `state->caret->view` / `char_offset` / `line` / `column` | `dom_selection->ranges[0]->start_view` / `start_byte_offset` / `start_line` / `start_column` (when `is_collapsed`) |
| `state->caret->x` / `y` / `height` | `dom_selection->ranges[0]->start_x/y` + `dom_selection->caret_height` |
| `state->caret->visible` / `blink_time` | `dom_selection->caret_visible` / `caret_blink_time` |
| `state->caret->prev_abs_*` | `dom_selection->caret_prev_abs_*` |
| `state->selection->anchor_view`, `focus_view`, `*_offset`, `*_line` | `dom_selection->ranges[0]->start_view`/`end_view` + the inlined layout cache; logical anchor/focus order derived from `dom_selection->direction` |
| `state->selection->start_x/y`, `end_x/y`, `iframe_offset_*` | same fields on `dom_selection->ranges[0]` |
| `state->selection->is_collapsed` | `dom_selection->is_collapsed` (now the canonical caret-vs-range discriminant) |
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

The spec hit-test API exists:

```cpp
// radiant/dom_range_resolver.hpp
DomBoundary dom_hit_test_to_boundary(View* root_view, float vx, float vy);
```

It walks the view tree to find the `TextRect` under `(vx, vy)`,
linear-interpolates a byte offset, and converts back to UTF-16. It is
available for accessibility, headless testing, and any new input source.

> **Phase 6 actual:** the live mouse path in `event.cpp` was **not**
> rerouted through `dom_hit_test_to_boundary`. The legacy entry points
> (`selection_start` / `selection_extend` / `caret_set`) keep their
> glyph-precise caret X/Y/height computation
> (`calculate_position_from_char_offset`), and the legacy→DOM sync
> hooks make `state->dom_selection` reflect every user gesture
> automatically. JS observes the same selection the user produced via
> the mouse, without losing pixel-perfect caret placement.

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

> **Phase 6A actual:** `render_selection()` is implemented as designed
> on top of `dom_range_for_each_rect()` in
> `radiant/dom_range_resolver.cpp` (one rect per `TextRect` crossed,
> handles single-text-node and cross-text-node ranges) and is now
> **active** in `render_ui_overlays`. The previous inline
> glyph-by-glyph painter in `render_text_view` is disabled so the
> overlay does not double-paint. See §6A in the Implementation Status
> section above for details.

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

## 8. Form Input Integration (`<input>` / `<textarea>`)

> **Status:** proposal — not yet implemented. Motivated by the four
> remaining WPT selection failures (`stringifier_editable_element_tentative`,
> `move_by_word_korean`, `move_by_word_with_symbol`, and the input/textarea
> halves of several `move_by_*` tests) which all reduce to "Lambda has
> no `HTMLInputElement` / `HTMLTextAreaElement` selection model".

HTML form text controls are a special case the W3C selection design
deliberately keeps off the main DOM tree: the user-visible glyphs of an
`<input type=text>` or `<textarea>` are **not** child `Text` nodes. They
live in an *anonymous, browser-managed text buffer* identified only by
the control's `value` attribute. The DOM `Selection` (the topic of
sections 1–7) cannot directly span into a control; instead each control
exposes its own private selection through `selectionStart` /
`selectionEnd` / `selectionDirection` / `setSelectionRange()` /
`select()`. The HTML spec calls these the **"text control selection"**
APIs (HTML §4.10.6).

This section proposes how Radiant should layer that second selection
model on top of the `DomSelection` infrastructure built in §3–§7.

### 8.1 Two selections, one document

Per the HTML spec there are *two* coexisting selection models inside one
document:

| Model | Anchored on | Owns | Scope |
|-------|-------------|------|-------|
| **DOM selection** (§3) | `(DomNode*, utf16-offset)` boundaries | `DomSelection` on `RadiantState` | Whole document tree, stops at the boundary of an `<input>`/`<textarea>` element |
| **Text-control selection** (this §) | UTF-16 offsets into the control's `value` string | `TextControlSelection` on the form element's per-control state | One text control |

When focus is in a text control, the document selection is **not** the
authority for the user-visible caret inside the control. Instead the
control's `TextControlSelection` is, and `window.getSelection()` returns
a selection that is collapsed *outside* the control element (per spec
the document selection's boundaries simply don't enter the control's
anonymous tree). The one observable interaction:
`Selection.toString()` *does* include the visible selected substring of
the focused text control — this is exactly what the
`stringifier-editable-element.tentative.html` WPT test asserts.

### 8.2 Data model: `TextControlSelection`

Stored on the existing per-control state structure attached to a form
`DomElement` (the field already wired through Radiant's layout code as
`item->form`):

```cpp
// radiant/dom_text_control.hpp  (NEW)

typedef enum {
    DOM_TC_DIR_NONE = 0,    // "none"
    DOM_TC_DIR_FORWARD,     // "forward"
    DOM_TC_DIR_BACKWARD,    // "backward"
} DomTextControlDirection;

typedef struct DomTextControlSelection {
    DomElement* host;            // <input> or <textarea>
    Str*        value;           // canonical UTF-8 storage of `.value`
                                 // (normalized per type=text rules:
                                 // <textarea> keeps newlines; <input>
                                 // strips CR/LF before storage).
    uint32_t    utf16_length;    // cached; invalidated on edit
    Utf16OffsetCache* u16_cache; // shared codepath with DomText (§7.7)

    // Selection inside the control. UTF-16 code unit offsets into `value`.
    uint32_t    sel_start;       // == sel_end ⇒ caret
    uint32_t    sel_end;
    DomTextControlDirection direction;
    bool        is_dirty_sel;    // true when modified since last 'select' event

    // Layout cache mirroring DomRange's pattern (§3.2).
    bool        layout_valid;
    View*       inner_view;      // anonymous RDT_VIEW_TEXT for the value
    int         start_byte_off;  // UTF-8 byte within inner_view
    int         end_byte_off;
    float       caret_x, caret_y, caret_h;  // for collapsed sel_start
} DomTextControlSelection;
```

`DomElement::form` (already present) gains one pointer:

```cpp
struct FormElementState {            // existing
    /* ... type, form-association, validity bits ... */
    DomTextControlSelection* tc_sel; // NEW; allocated lazily on first
                                     // focus or programmatic access.
};
```

The selection lives next to the form-element state — *not* on
`RadiantState->live_ranges`. It is **not** a `DomRange`; it does **not**
participate in `dom_mutation_*` envelopes. (Mutations to the control's
value go through a dedicated path, §8.5.)

### 8.3 Authority over the user-visible caret

Today (Phase 6) the visible caret is driven by the legacy
`CaretState` / `SelectionState` pair on `RadiantState`, kept in sync
with `DomSelection`. With text controls in the mix the rule becomes:

```
focused element type           ┃ caret authority
───────────────────────────────╂──────────────────────────────────────
none / non-control element     ┃ DomSelection  → CaretState (existing)
<input> text-like / <textarea> ┃ TextControlSelection → CaretState
                               ┃ (the document DomSelection is
                               ┃  collapsed at the control's parent
                               ┃  boundary and rendered as hidden)
```

`event.cpp`'s mouse / keyboard handlers gain a single dispatch at the
top:

```cpp
DomElement* tc = focused_text_control(state);
if (tc) {
    text_control_handle_event(state, tc, event);   // updates tc_sel
} else {
    /* ...existing path that updates CaretState/DomSelection... */
}
```

`text_control_handle_event` runs the same hit-test → `(view, byte)` →
UTF-16 conversion the document path uses, but the result is written to
`tc->form->tc_sel`, never to `state->dom_selection`. The legacy→DOM
sync hook on `RadiantState` is bypassed for this branch.

### 8.4 Selection authority and `Selection.toString()` interaction

Per HTML §4.10.6 and the WPT `stringifier_editable_element_tentative`
test, when the document selection's `range[0]` *contains* a focused
text control, `Selection.toString()` must include the substring
`tc_sel->value[tc_sel->sel_start .. tc_sel->sel_end]` at the position
where the control element appears. The change to
`dom_range_to_string` (currently in `radiant/dom_range.cpp`) is
localised:

```cpp
static void emit_node_text(StrBuf* out, DomNode* n,
                           uint32_t lo, uint32_t hi /* utf16 */) {
    if (n->is_element() && elem_is_text_control(n->as_element())) {
        DomTextControlSelection* tc = n->as_element()->form
                                      ? n->as_element()->form->tc_sel : nullptr;
        if (tc && tc->sel_start != tc->sel_end) {
            // Emit the substring of the control's *value* that the
            // user sees as selected, not the control's children.
            tc_emit_substring(out, tc, tc->sel_start, tc->sel_end);
        }
        return;   // never recurse into control's anonymous text
    }
    /* ... existing recursive walk ... */
}
```

Conversely, when the document selection's range is *outside* the
control element entirely (the common case), `tc_sel` is irrelevant and
the existing logic applies unchanged. The set-membership test
"selection contains the control" is the standard
`dom_boundary_compare` against `(parent, indexOf(control))` and
`(parent, indexOf(control) + 1)`.

### 8.5 Mutation path: `value` and DOM attribute

The control's textual content has **two** observable surfaces and they
must be kept consistent:

1. **`element.value`** (IDL attribute) — UTF-16 string, the user-typed
   content. Stored as `tc_sel->value` (UTF-8) plus the cached
   `utf16_length`.
2. **`element.getAttribute("value")`** — the *original* default. Per
   HTML this never changes after parsing for `<input>`; for
   `<textarea>` the default is `element.defaultValue` (the text-content
   children of the element at parse time).

A new mutation envelope, parallel to §6's `dom_mutation_text_replace_data`:

```cpp
void dom_text_control_set_value(DomElement* host,
                                const char* utf8, uint32_t utf8_len);
// Used by IDL setter `element.value = "..."` AND by user keystrokes.
// Steps (per HTML §4.10.6):
//   1. Normalise the new value (strip CR/LF for non-textarea).
//   2. Replace tc_sel->value, invalidate utf16_length and u16_cache.
//   3. Per spec: if the API is "value" setter, set sel_start = sel_end
//      = new_length, direction = "none". If "user input", clamp
//      sel_start/sel_end to new_length.
//   4. Mark layout dirty (host->form_layout_dirty) and request reflow
//      so the anonymous inner view repaints.
//   5. Fire `input` event then `change` on blur per existing form code.
```

The DOM `Selection`'s live ranges are unaffected by this path because
they cannot have endpoints inside the control's anonymous buffer; their
endpoint structure (§5.5) only sees the control element itself, which
is unchanged.

### 8.6 JS bindings

Add `lambda/js/js_dom_form_text.cpp` (or extend `js_dom.cpp` if the
delta is small) and register the following on the host class table for
`HTMLInputElement` and `HTMLTextAreaElement`:

| WebIDL member | Implementation |
|---------------|----------------|
| `attribute DOMString value` getter | reads `tc_sel->value` (lazy-create on first access) |
| `attribute DOMString value` setter | calls `dom_text_control_set_value` with `apiSource = "value"` |
| `attribute unsigned long? selectionStart` | reads `tc_sel->sel_start`; null if control is non-text-like |
| `attribute unsigned long? selectionEnd` | reads `tc_sel->sel_end` |
| `attribute DOMString? selectionDirection` | reads `tc_sel->direction` mapped to `"forward"`/`"backward"`/`"none"` |
| `void setSelectionRange(start, end, direction?)` | clamps + writes to `tc_sel`; fires `select` event if changed |
| `void select()` | `setSelectionRange(0, utf16_length, "none")` |
| `void setRangeText(repl, start?, end?, mode?)` | combines `dom_text_control_set_value` + caret rules |
| `attribute DOMString defaultValue` | mirrors `getAttribute("value")` for input; element textContent for textarea |

The WPT shim removals: once these bindings exist, the
`EditorTestUtils` / proxy / `_wpt_word_*` helpers added during the
2025-11 drill (see `test/wpt/wpt_testharness_shim.js`) become
redundant and should be deleted along with the corresponding entries
on the SKIP list.

### 8.7 Word-break inside controls (`Selection.modify` analogue)

The four WPT `move-by-word-*.html` tests register two halves: one for
`<div contenteditable>` (already passing — uses
`dom_selection_modify` + the script-class iterator from
`radiant/dom_range.cpp`), one for `<textarea>`/`<input>` (failing,
this section). The fix re-uses the same algorithm:

```cpp
// radiant/dom_text_control.cpp

uint32_t tc_word_forward(const DomTextControlSelection* tc, uint32_t pos);
uint32_t tc_word_backward(const DomTextControlSelection* tc, uint32_t pos);
```

Both run on the control's `value` string, applying the same
`cp_script_class` rule used for the DOM tree path so that
Korean/Latin/symbol transitions break consistently inside and outside
text controls. Connected to the JS layer through a `keydown`
handler in the form-control event path (Alt+Arrow on macOS,
Ctrl+Arrow on others), or directly from the WPT-driver
`sendMoveWordLeftKey/RightKey` helpers when those run.

Crucially this **never** calls `dom_selection_modify` and never
touches the document's `DomSelection` — the document selection is
either outside the control (most common) or collapsed at the control
boundary. Browsers behave the same way: `getSelection().focusNode` does
not move while the user holds Alt+ArrowRight inside an `<input>`.

### 8.8 Rendering integration

`render.cpp` already has a code path for form controls
(`item->item_prop_type == DomElement::ITEM_PROP_FORM`,
~20 sites in `layout_flex.cpp`). Two small additions:

1. **Inner caret/selection paint.** When `tc_sel != nullptr` and the
   control has focus, call the same `render_selection()`/`render_caret()`
   helpers but feed them the cached `inner_view` + byte offsets from
   `tc_sel`, and clip to the control's content rect. Multi-line
   `<textarea>` is handled by walking the line-fragment chain of the
   inner view, the same way `dom_range_for_each_rect()` walks for
   document selections (§4.4).
2. **Suppress document selection inside the control.** If a document
   `DomRange` happens to span across the control element, the
   multi-fragment iteration in `dom_range_for_each_rect()` skips
   anonymous descendants of text controls — the visible selection
   inside the control is owned by `tc_sel`, not by the document range.

The legacy `CaretState` repaint dirty-rect tracking
(`caret_prev_abs_*`) is reused unchanged: when focus is in a control,
the caret rendering pulls from `tc_sel`; when not, from
`dom_selection->ranges[0]`.

### 8.9 Focus / blur interaction

The current focus tracking on `RadiantState` (`state->focus_target`
or equivalent) gains one rule applied at every focus transition:

| Transition | DomSelection effect | tc_sel effect |
|------------|---------------------|---------------|
| no focus → text control | collapse `DomSelection` at `(control.parent, indexOf(control))` and hide its caret | restore last-saved `(sel_start, sel_end, direction)` for the control; if first focus apply `<input autofocus>` rules |
| text control → other text control | save outgoing `tc_sel`; restore incoming | swap which `tc_sel` is the visible authority |
| text control → non-control DOM | drop the visible-caret override; the document `DomSelection` becomes authority again | leave outgoing `tc_sel` intact (it persists across blur per spec) |
| any → no focus | hide both | leave intact |

Per HTML, the text-control selection **persists** across blur — so we
keep it stored on `host->form->tc_sel` indefinitely (cleared only when
the host element is removed from the document, which the existing
`dom_mutation_pre_remove` envelope can hook).

### 8.10 Implementation phases

Inserted between current Phase 6 (shipped) and Phase 7 (polish):

**Phase 6C — `TextControlSelection` data model**
1. `radiant/dom_text_control.{hpp,cpp}` with the struct above and
   `dom_text_control_set_value`.
2. Allocate `tc_sel` lazily from `host->form` in
   `radiant/layout_flex.cpp`'s form-control init path.
3. GTest unit coverage on UTF-16 conversion + word-break iterators
   reusing the §11 harness.

**Phase 6D — JS bindings**
1. `lambda/js/js_dom_form_text.cpp` exposes the WebIDL surface from §8.6.
2. Plumb `setSelectionRange` / `select` / `value` / `setRangeText`
   through to `tc_sel`.
3. Update `dom_range_to_string` (§8.4) to consult focused-control
   `tc_sel` for the stringifier path.

**Phase 6E — Input event wiring**
1. `event.cpp` dispatch fork (§8.3).
2. Reuse `cp_script_class` for word-jump key handling on `tc_sel`.
3. Render caret/selection inside the control's inner view.

Each phase has a clear WPT exit criterion:
- 6C: GTest 100% on text-control unit tests.
- 6D: `stringifier_editable_element_tentative` passes; the textarea/input
  halves of `move_by_word_korean`/`move_by_word_with_symbol` pass via
  the WPT `setSelectionRange` driver.
- 6E: full `move_by_word_*` and `move_selection_range_into_different_root`
  (the latter via the existing iframe support combined with
  `tc_sel` on inputs inside the inner doc) pass.

After Phase 6E lands, the SKIP list in `test_wpt_selection_gtest.cpp`
can drop the `selection-direction-*`, `extend-selection-backward-on-input`,
and `user-select-on-input` entries.

### 8.11 Risks specific to text controls

1. **Defaulting on parse.** `<textarea>foo bar</textarea>` must seed
   `tc_sel->value = "foo bar"` and `defaultValue` from the children at
   parse time, *and* strip those children from the layout tree (browsers
   render the value, not the children). Existing form-init code in
   `layout_flex.cpp` already handles the latter; we only need to
   capture text content into `tc_sel->value`.
2. **`<input type=number/email/...>`.** Per HTML some types ban
   `selectionStart`/`End` (return null, throw on set). The
   `tc_sel != nullptr` allocation policy must respect a per-type
   table — gate the allocation in `host_type_supports_selection(host)`.
3. **Composition events / IME.** Out of scope for this proposal;
   composition leaves `tc_sel->value` and selection in their pre-IME
   state until the composition commits.
4. **Shadow DOM emulation.** A future Shadow DOM implementation may
   want to expose the control's anonymous buffer as a real shadow tree.
   The `TextControlSelection` design does not preclude that — it is
   strictly a layer above the buffer storage.

---

## 9. Implementation Phases

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

### Phase 6 — Rendering & input wiring ✅ done (revised)

**Original plan**

1. Rewrite `render_selection()` for multi-line / cross-node spans
   (§4.4).
2. Add `hit_test_to_boundary()` and reroute mouse handlers through
   `dom_selection_collapse`/`extend`.
3. Reroute `caret_*` user-API to update `DomSelection` first, then
   re-resolve. Existing call-sites unchanged.

**What actually shipped**

1. ✅ `render_selection()` rewritten on top of `dom_range_for_each_rect()`
   (`radiant/render.cpp`, `radiant/dom_range_resolver.{hpp,cpp}`).
   Disabled in `render_ui_overlays` pending Phase 7 (the inline glyph
   painter still drives visible highlights to keep zero behavior
   change).
2. ✅ `dom_hit_test_to_boundary()` added
   (`radiant/dom_range_resolver.hpp`). Live mouse path **not**
   rerouted: the legacy entry points keep their glyph-precise caret
   coordinates, and the bidirectional sync (item 3) makes
   `state->dom_selection` reflect every gesture.
3. ✅ Bidirectional sync instead of unidirectional reroute.
   `legacy_sync_from_dom_selection()` and
   `dom_selection_sync_from_legacy_*()` keep both states in agreement;
   re-entry counter `dom_selection_sync_depth` on `RadiantState`
   prevents ping-pong.

Exit criteria met: visual selection in the GUI matches the
WPT-driven DOM selection one-to-one; `make test-radiant-baseline`
5713/5717 with the same 4 pre-existing render-visual failures and no
new regressions.

### Phase 7 — Polish

1. Trim WPT skip list.
2. Add `selectionchange` event (optional).
3. Documentation in `doc/dev/Radiant_Selection.md` with API examples.

---

## 10. File / Module Layout

```
radiant/
  dom_range.hpp              ← NEW: DomBoundary, DomRange, DomSelection types
  dom_range.cpp              ← NEW: spec algorithms
  dom_range_resolver.hpp     ← NEW: DOM ↔ layout coordinate bridge
  dom_range_resolver.cpp     ← NEW
  state_store.hpp            ← MODIFY: ADD dom_selection / live_ranges /
                                       next_range_id /
                                       selection_layout_dirty /
                                       dom_selection_sync_depth
                                       (legacy CaretState/SelectionState
                                       pointers retained — see Phase 6
                                       deviation note).
  state_store.cpp            ← MODIFY: caret_*/selection_* mirror into
                                       dom_selection via
                                       dom_selection_sync_from_legacy_*;
                                       legacy_sync_from_dom_selection()
                                       handles the inverse direction.
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

## 11. Risks & Open Questions

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

## 12. Test Strategy

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

## 13. Summary

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
**Status:** Phases 1–6 implemented; Phase 7 polish pending; §8
"Form Input Integration" (Phases 6C/6D/6E) proposed and not yet
started. See the Implementation Status section at the top for
shipped-vs-planned diffs.
**Related:** [Radiant_Design_State.md](Radiant_Design_State.md) ·
[test/wpt/test_wpt_selection_gtest.cpp](test/wpt/test_wpt_selection_gtest.cpp) ·
[ref/wpt/selection/](ref/wpt/selection)
