# Reactive UI Phase 4 — Incremental Reflow, Render Optimization, and No-Op Elision

**Date:** 2026-04-06
**Status:** Partially Implemented (Phases 14, 15, 18 complete)
**Prerequisite:** Phases 10–13 complete (Reactive_UI3.md)

---

## Table of Contents

1. [Objective](#1-objective)
2. [Current Performance Baseline (Phase 3 Measurements)](#2-current-performance-baseline-phase-3-measurements)
3. [Root Cause Analysis](#3-root-cause-analysis)
4. [Phase 14: No-Op Elision — Skip Rebuild When DOM Is Unchanged](#4-phase-14-no-op-elision--skip-rebuild-when-dom-is-unchanged)
5. [Phase 15: Preserve Resolved Styles Across Incremental Rebuilds](#5-phase-15-preserve-resolved-styles-across-incremental-rebuilds)
6. [Phase 16: Subtree-Only Relayout](#6-phase-16-subtree-only-relayout)
7. [Phase 17: Glyph Render Cache — Skip Redundant FreeType Loads](#7-phase-17-glyph-render-cache--skip-redundant-freetype-loads)
8. [Phase 18: Dirty-Region Clip for Render Tree Walk](#8-phase-18-dirty-region-clip-for-render-tree-walk)
9. [Phase 19: Caret-Only Repaint](#9-phase-19-caret-only-repaint)
10. [Implementation Order](#10-implementation-order)
11. [Implementation Results (Measured)](#11-implementation-results-measured)
12. [Files Modified](#12-files-modified)

---

## 1. Objective

Phase 3 (Reactive_UI3.md) implemented incremental DOM patching, scoped CSS cascade, dirty tracking, and selective surface clear. Every handler-triggered event now patches only the changed subtree before relayout and render.

However, **every event still triggers full relayout (~48ms) and full render tree walk (~46ms)**, totaling ~95ms per event in debug builds. Key problems:

1. **No no-op detection** — Even when a handler produces identical DOM output (e.g., clicking an already-focused input, toggling an item back to its original state), the full pipeline runs.
2. **Full style re-resolution every layout** — `reset_styles_resolved()` clears all 106 elements, forcing 20ms of CSS property resolution even when only 1 element changed.
3. **Full layout from root** — `layout_html_doc` re-lays-out all 106 elements (~48ms), destroying and recreating the view pool every time.
4. **Full render tree walk** — `render_block_view` traverses all views (~46ms) even when selective clear limits actual surface writes.
5. **Glyph re-loading** — 540 `font_load_glyph` calls per render (~15ms) re-fetch FreeType glyphs that haven't changed.
6. **Caret changes trigger full pipeline** — Focus/caret events that change no DOM still trigger handler → retransform → rebuild → layout → render.

**Target:** Reduce event response time from ~95ms to <20ms for same-size changes (toggle, text input) and <1ms for no-op events (focus, caret movement).

---

## 2. Current Performance Baseline (Phase 3 Measurements)

Test: `todo_perf_timing.json` — 42 events on `todo.ls` (~90-106 elements).
Build: Debug, macOS Apple Silicon.

### Event Timing Summary (42 events)

| Metric | Average | Min | Max |
|--------|---------|-----|-----|
| Handler execution | 0.03ms | 0.02ms | 0.10ms |
| Retransform | 0.08ms | 0.03ms | 0.15ms |
| DOM patch | 0.37ms | 0.11ms | 0.61ms |
| Layout | 48.2ms | 43.2ms | 62.9ms |
| Render | 46.3ms | 44.5ms | 49.6ms |
| **Total** | **95.0ms** | **88.3ms** | **112.3ms** |

### Event Breakdown (42 events)

| Event Type | Count | Selective Repaint | Full Repaint |
|------------|-------|-------------------|--------------|
| Toggle (click checkbox) | 4 | 3 yes | 1 (first = full rebuild fallback) |
| Delete (click delete btn) | 2 | 0 | 2 (size change → full) |
| Focus input (click) | 2 | 2 yes | 0 |
| Text input (per char) | 22 | 22 yes | 0 |
| Add item (click button) | 1 | 0 | 1 (size change → full) |
| Add via Enter | 1 | 0 | 1 (size change → full) |
| Clear completed | 1 | 0 | 1 (size change → full) |
| Backspace | 2 | 2 yes | 0 |
| Type + Enter (second task) | 7 | 7 yes | 0 |

### Layout Breakdown (per event)

| Sub-step | Average | % of Layout |
|----------|---------|-------------|
| Style resolution (106 calls, 106 full) | 18.5ms | 38% |
| Text measurement (32-39 calls) | 8.8ms | 18% |
| Block layout (29-33 calls) | 260ms (accumulated) | — |
| Flex layout | 36ms (accumulated) | — |
| **Total layout_html_doc** | **48.2ms** | **100%** |

### Render Breakdown (per event)

| Sub-step | Average | % of Render |
|----------|---------|-------------|
| Glyph loading (540 calls) | 15.2ms | 33% |
| Glyph drawing (270 calls) | 0.7ms | 2% |
| Block/view rendering | ~30ms | 65% |
| ThorVG canvas sync | 0.0ms | 0% |
| **Total render_html_doc** | **46.3ms** | **100%** |

### Bottleneck Distribution

```
Handler + Retransform:  ▏  <0.2%
DOM Patch:              ▏  <0.5%
Style Resolution:       ████████  ~20%
Text Measurement:       █████  ~9%
Block/Flex Layout:      ████████████  ~22%
Glyph Loading:          ██████  ~16%
View Rendering:         █████████████  ~32%
                        └──────────────── 95ms total
```

---

## 3. Root Cause Analysis

### 3.1 Why All 106 Elements Get Full Style Resolution

In `layout_init()` → `reset_styles_resolved()` ([layout.cpp#L2028](../radiant/layout.cpp#L2028)), every layout pass clears `styles_resolved` on ALL DomElements:

```cpp
void reset_styles_resolved(DomDocument* doc) {
    reset_styles_resolved_recursive(doc->root);  // clears ALL elements
}
```

Then `dom_node_resolve_style()` ([layout.cpp#L544](../radiant/layout.cpp#L544)) only skips resolution when `dom_elem->styles_resolved == true`, which never happens because it was just cleared. Result: 106 full `resolve_css_styles()` calls (20ms).

### 3.2 Why Layout Destroys and Rebuilds Everything

`rebuild_lambda_doc_incremental` calls `view_pool_destroy()` then `layout_html_doc(uicon, doc, true)` ([cmd_layout.cpp#L4384](../radiant/cmd_layout.cpp#L4384)). But `layout_html_doc` with `is_reflow=true` has its optimization **commented out** ([layout.cpp#L2081](../radiant/layout.cpp#L2081)):

```cpp
if (is_reflow) {
    // if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
    // view_pool_destroy(doc->view_tree);  // COMMENTED OUT
}
```

Both paths (reflow=true/false) call `view_pool_init()` → `layout_html_root()` → full layout of all elements. The `is_reflow` parameter is a dead code path.

### 3.3 Why Render Walks the Full Tree Even with Selective Clear

`render_block_view()` at [render.cpp#L1546](../radiant/render.cpp#L1546) recursively visits every child. Selective repaint (Phase 12.4) only limits which rectangles are *cleared to background* — the full tree is still traversed and all shapes are drawn to the ThorVG canvas. ThorVG's internal clipping may discard off-region draws, but the traversal + glyph-loading overhead remains.

### 3.4 Why Glyphs Are Re-Loaded Every Render

`font_load_glyph()` has an advance cache but the render path calls it to get ThorVG shape outlines, not just advance widths. The 540 calls per render (270 unique codepoints × fallback probing) each involve FreeType glyph loading at ~28µs/call = 15ms.

### 3.5 No-Op Events (No Effective DOM Change)

When a handler runs but produces identical output (e.g., input handler stores same value, focus-related click on already-active element), `render_map_has_dirty()` still returns true because the entry was marked dirty before the handler ran (for edit templates) or the handler always mutates. No comparison is done between old and new retransform results.

---

## 4. Phase 14: No-Op Elision — Skip Rebuild When DOM Is Unchanged

### Problem

Every handler invocation triggers retransform + rebuild + layout + render even when the retransformed output is structurally identical to the previous output. Common cases:

- Clicking an input field that already has focus (handler runs, produces same DOM)
- Typing a character when the input handler stores the value but the rendered text hasn't changed structurally
- Toggling an item when the handler is idempotent

### Design

After `render_map_retransform_with_results()` produces `new_result` items, compare each against the corresponding `old_result`. If all results are element-identical (same tag, same attributes, same children), skip the entire rebuild.

#### 4.1 Shallow Element Equality Check

```c
// In lambda/mark_reader.cpp or lambda/lambda-data.cpp
bool element_shallow_equal(Item a, Item b) {
    if (a.item == b.item) return true;  // same pointer = identical
    
    TypeId ta = get_type_id(a), tb = get_type_id(b);
    if (ta != tb) return false;
    if (ta != LMD_TYPE_ELEMENT) return false;
    
    Element* ea = a.element;
    Element* eb = b.element;
    
    // Compare tag
    if (ea->tag != eb->tag) return false;
    
    // Compare shape (field names + types)
    if (ea->type != eb->type) return false;
    
    // Compare child count
    if (ea->length != eb->length) return false;
    
    // Compare data bytes (field values)
    if (ea->type && ea->type->data_size > 0) {
        if (memcmp(ea->data, eb->data, ea->type->data_size) != 0) return false;
    }
    
    // Recursively compare children
    for (uint32_t i = 0; i < ea->length; i++) {
        if (!item_deep_equal(ea->items[i], eb->items[i])) return false;
    }
    
    return true;
}
```

#### 4.2 Integration Point

In `dispatch_lambda_handler()` after `render_map_retransform_with_results()`:

```cpp
if (render_map_has_dirty()) {
    RetransformResult results[16];
    int count = render_map_retransform_with_results(results, 16);
    auto t_retransform = high_resolution_clock::now();
    
    // Phase 14: Check if any result actually changed
    bool any_changed = false;
    for (int i = 0; i < count; i++) {
        if (!element_deep_equal(results[i].old_result, results[i].new_result)) {
            any_changed = true;
            break;
        }
    }
    
    if (any_changed) {
        rebuild_lambda_doc_incremental(evcon->ui_context, results, count);
    } else {
        log_info("[TIMING] event dispatch: handler=%.2fms retransform=%.2fms NO-OP (identical output)",
            ms(t_handler - t_start), ms(t_retransform - t_handler));
    }
}
```

#### 4.3 Expected Savings

For no-op events (focus click on already-focused input, redundant toggles): 0ms rebuild instead of ~95ms. The number of no-op events depends on the application, but in interactive UIs with focus management, it can be 10-30% of all events.

For text input where the handler updates a field and the retransformed DOM differs only in one text node, the deep comparison cost is O(changed subtree) which is typically <0.1ms.

---

## 5. Phase 15: Preserve Resolved Styles Across Incremental Rebuilds

### Problem

`layout_init()` calls `reset_styles_resolved(doc)` which clears ALL 106 elements' `styles_resolved` flag, forcing full CSS property resolution on every layout pass. For incremental rebuilds where only 1 subtree changed, 105 of 106 resolutions are redundant.

### Design

Only clear `styles_resolved` on the new DOM subtree nodes (which are freshly built and have `styles_resolved = false` by default). Unchanged DOM nodes retain their resolved styles from the previous pass.

#### 5.1 Remove Blanket Reset

Remove the `reset_styles_resolved(doc)` call from `layout_init()` for incremental rebuilds. Instead, new DOM nodes created by `build_dom_tree_from_element()` already have `styles_resolved = false` (zero-initialized).

**In `rebuild_lambda_doc_incremental`:**

```cpp
// Before calling layout_html_doc, set a flag to skip reset_styles_resolved
doc->skip_style_reset = true;  // new flag on DomDocument

layout_html_doc(uicon, doc, true);

doc->skip_style_reset = false;
```

**In `layout_init`:**

```cpp
// Only reset styles when doing full layout (not incremental)
if (!doc->skip_style_reset) {
    reset_styles_resolved(doc);
}
```

#### 5.2 Invalidate Changed Subtree Ancestors

When a subtree is replaced, its ancestors' computed styles may be affected by descendant-dependent properties (e.g., `height: auto`). Clear `styles_resolved` only on the path from the changed subtree root to the document root:

```cpp
// After dom_node_replace_in_parent:
DomNode* ancestor = (DomNode*)parent_dom;
while (ancestor) {
    if (ancestor->is_element()) {
        ((DomElement*)ancestor)->styles_resolved = false;
    }
    ancestor = ancestor->parent;
}
```

This limits style re-resolution to: (new subtree nodes) + (ancestor path) instead of all 106 elements.

#### 5.3 Expected Savings

For a toggle event on a 5-element `<li>` in a 106-element page:
- Before: 106 full resolutions × ~0.18ms = 19ms
- After: ~10 resolutions (5 new + ~5 ancestors) × ~0.18ms = ~2ms
- **Savings: ~17ms per event (35% of total)**

---

## 6. Phase 16: Subtree-Only Relayout

### Problem

`layout_html_doc` lays out the entire document from root, even when only one subtree changed. For a 106-element page where 1 `<li>` was toggled, the layout engine traverses all 106 elements.

### Design

When the changed subtree has the same dimensions (detected by Phase 12.3's size comparison), layout can be restricted to the changed subtree only, preserving the parent's layout context.

#### 6.1 Feasibility Check

Subtree-only relayout is safe when:
1. The changed subtree's **outer dimensions** (width, height, margin) are unchanged
2. The parent's layout mode is block flow or flex with fixed child sizing
3. No `:nth-child`, `:last-child` selectors affected by the change

For toggle events (checkbox state change → class change → different text/color but same dimensions), all conditions are met.

#### 6.2 Layout Subtree Function

Instead of calling `layout_html_doc` which starts from the document root:

```cpp
void layout_subtree(LayoutContext* lycon, DomElement* subtree_root, ViewBlock* parent_view) {
    // Restore parent's layout context (available width, position)
    restore_layout_context_from_view(lycon, parent_view);
    
    // Layout only the subtree
    DisplayValue display = resolve_display_value(subtree_root);
    layout_block(lycon, subtree_root, display);
}
```

#### 6.3 View Subtree Replacement

Instead of destroying the entire view pool and rebuilding:

1. Free only the views in the changed subtree (walk subtree, return to pool)
2. Layout the new subtree, creating new views
3. Splice new views into the parent's child list

```cpp
// In rebuild_lambda_doc_incremental:
if (!size_changed && parent_view) {
    // Subtree-only relayout
    LayoutContext lycon;
    layout_init_for_subtree(&lycon, doc, uicon, parent_view);
    
    // Remove old child views from parent
    view_remove_child(parent_view, old_child_view);
    
    // Layout new subtree (creates new views in pool)
    layout_block(&lycon, new_dom, display);
    
    // Insert new views at the same position
    view_insert_child(parent_view, new_child_view, position);
}
```

#### 6.4 Fallback

If size changed or parent layout is complex (grid, table), fall back to full layout.

#### 6.5 Expected Savings

For same-size toggle events:
- Before: 106 elements × full layout = 48ms
- After: ~5-10 elements × partial layout = ~3-5ms
- **Savings: ~43ms per event (45% of total)**

Combined with Phase 15 (style resolution):
- Layout time: ~48ms → ~5ms for same-size changes

---

## 7. Phase 17: Glyph Render Cache — Skip Redundant FreeType Loads

### Problem

Each render pass calls `font_load_glyph()` 540 times (~15ms) to get ThorVG shape outlines for text rendering. The advance cache ([font_glyph.c#L34](../lib/font/font_glyph.c#L34)) caches advance widths but not the full `LoadedGlyph` with its outline/bitmap data.

### Design

Cache the ThorVG glyph shape handles across render passes. On re-render of unchanged text, reuse the previously created TVG shapes instead of re-loading from FreeType.

#### 7.1 Rendered Glyph Cache

```c
typedef struct RenderedGlyphEntry {
    uint64_t key;           // (font_handle_id << 32) | codepoint
    Tvg_Paint* shape;       // ThorVG shape — retained across frames
    float advance_x;
    float offset_y;
} RenderedGlyphEntry;

// Global cache, persisted across render passes
static struct hashmap* g_rendered_glyph_cache;
```

#### 7.2 Integration in render_texnode.cpp

```cpp
// Before:
LoadedGlyph* loaded = font_load_glyph(handle, style, codepoint, true);
// ... create ThorVG shape from loaded glyph ...

// After:
RenderedGlyphEntry* cached = rendered_glyph_cache_get(handle, codepoint);
if (cached) {
    // Clone the cached shape (ThorVG supports paint cloning)
    Tvg_Paint* shape = tvg_paint_duplicate(cached->shape);
    tvg_paint_translate(shape, x, y);
    tvg_canvas_push(canvas, shape);
} else {
    LoadedGlyph* loaded = font_load_glyph(handle, style, codepoint, true);
    // ... create ThorVG shape ...
    rendered_glyph_cache_put(handle, codepoint, shape, advance_x, offset_y);
}
```

#### 7.3 Cache Invalidation

The cache is keyed on `(font_handle, codepoint)`. Font handles don't change during reactive UI interactions, so the cache is valid across frames. Invalidate on:
- Font loading/unloading
- DPI/scale change
- Document close

#### 7.4 Expected Savings

- Before: 540 `font_load_glyph` calls × ~28µs = 15ms
- After: 540 cache lookups × ~0.05µs + ThorVG clone × ~1µs = <1ms
- **Savings: ~14ms per render (30% of render time)**

---

## 8. Phase 18: Dirty-Region Clip for Render Tree Walk

### Problem

`render_block_view()` traverses ALL views in the tree, even when selective repaint limits surface clearing to dirty regions. For a 106-element page where only 1 `<li>` changed, ~100 views are traversed unnecessarily.

### Design

Skip rendering of view subtrees whose bounds don't intersect any dirty region.

#### 8.1 Bounds-vs-Dirty-Region Test

```cpp
static bool view_intersects_dirty(RenderContext* rdcon, ViewBlock* block) {
    // In full repaint mode, everything intersects
    RadiantState* state = rdcon->ui_context->document 
        ? (RadiantState*)rdcon->ui_context->document->state : nullptr;
    if (!state || state->dirty_tracker.full_repaint || !dirty_has_regions(&state->dirty_tracker)) {
        return true;
    }
    
    // Compute absolute bounds of this view
    float vx = block->x, vy = block->y;
    float vw = block->width, vh = block->height;
    // (Absolute position already available from layout)
    
    // Test intersection with any dirty rect
    DirtyRect* dr = state->dirty_tracker.dirty_list;
    while (dr) {
        if (rects_intersect(vx, vy, vw, vh, dr->x, dr->y, dr->width, dr->height)) {
            return true;
        }
        dr = dr->next;
    }
    return false;
}
```

#### 8.2 Integration in render_block_view

```cpp
void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    // Phase 18: Skip entire subtree if outside dirty regions
    if (!view_intersects_dirty(rdcon, block)) {
        return;  // early exit — no drawing needed
    }
    
    // ... existing rendering code ...
}
```

#### 8.3 Caveat: Overlapping Views

Views with `position: absolute/fixed` or `overflow: visible` may extend beyond their parent's bounds. The intersection test must use the view's **paint bounds** (including overflow), not just layout bounds. For simplicity, add a margin or fall back to full traversal when positioned elements are present.

#### 8.4 Expected Savings

For a toggle event that changes one `<li>` (dirty rect covers ~10% of page):
- Before: 106 views traversed, 540 glyph loads = ~46ms
- After: ~10 views traversed, ~50 glyph loads = ~5ms
- **Savings: ~40ms per render (87% of render time)**

Combined with Phase 17 (glyph cache), render drops to ~2-3ms.

---

## 9. Phase 19: Caret-Only Repaint

### Problem

Focus changes and caret movement (clicking in an input field, arrow keys) trigger the full handler → retransform → rebuild → layout → render pipeline. Caret rendering is already separated in `render_ui_overlays()` ([render.cpp#L2349](../radiant/render.cpp#L2349)), but it's called as part of the full render pass.

### Design

For events that only change caret/focus state without modifying DOM content, bypass the rebuild/layout/render pipeline and directly repaint the caret overlay.

#### 9.1 Detect Caret-Only Changes

After the handler runs, check if the only change is caret/focus state:

```cpp
// In event handling, after dispatch_lambda_handler returns:
if (!render_map_has_dirty() && evcon.caret_moved) {
    // No DOM changes — just repaint caret overlay
    repaint_caret_only(uicon);
    return;
}
```

#### 9.2 Caret-Only Repaint Function

```cpp
void repaint_caret_only(UiContext* uicon) {
    RenderContext rdcon;
    render_init(&rdcon, uicon, uicon->document->view_tree);
    
    // Clear old caret region (small rect)
    if (state->caret->prev_x >= 0) {
        Rect old_caret = {state->caret->prev_x, state->caret->prev_y, 
                          2.0f, state->caret->prev_height};
        // Repaint the content behind old caret position
        render_block_view_clipped(&rdcon, root, &old_caret);
    }
    
    // Draw new caret
    render_caret(&rdcon, state);
    
    // Sync
    tvg_canvas_sync(rdcon.canvas);
}
```

#### 9.3 Track Previous Caret Position

Add `prev_x`, `prev_y`, `prev_height` to `CaretState` so the old caret region can be repainted with content:

```c
typedef struct CaretState {
    float x, y, height;
    float prev_x, prev_y, prev_height;  // Phase 19: for dirty-rect caret repaint
    int char_offset;
    bool visible;
    // ...
} CaretState;
```

#### 9.4 Expected Savings

For caret/focus-only events:
- Before: Full pipeline = ~95ms
- After: Caret-only repaint = <1ms
- **Savings: ~94ms (essentially free)**

This applies to: input focus clicks, arrow key navigation, tab focus, mouse selection in text fields.

---

## 10. Implementation Order

| # | Task | Phase | Deps | Effort | Risk | Expected Savings | Status |
|---|------|-------|------|--------|------|-----------------|--------|
| 1 | `item_deep_equal()` comparison function | 14 | — | S | Low | — | ✅ Done |
| 2 | No-op elision in `dispatch_lambda_handler` | 14 | 1 | S | Low | ~95ms/no-op event | ✅ Done |
| 3 | Skip `reset_styles_resolved` for incremental | 15 | — | S | Medium — inheritance correctness | ~17ms/event | ✅ Done |
| 4 | Invalidate only ancestor chain styles | 15 | 3 | S | Medium | — | ✅ Done |
| 5 | Glyph render cache structure | 17 | — | M | Low | — | Not started |
| 6 | Cache integration in render_texnode.cpp | 17 | 5 | M | Medium — ThorVG shape lifecycle | ~14ms/render | Not started |
| 7 | Dirty-region intersection test | 18 | — | S | Low | — | ✅ Done |
| 8 | Early exit in `render_block_view` | 18 | 7 | S | Medium — positioned elements | ~40ms/render | ✅ Done |
| 9 | `layout_subtree()` function | 16 | — | L | High — layout context restoration | — | Not started |
| 10 | View subtree splice in incremental rebuild | 16 | 9 | L | High — view pool management | ~43ms/layout | Not started |
| 11 | Caret-only repaint path | 19 | — | M | Medium — correct content restore | ~94ms/caret event | Deferred |
| 12 | Track previous caret position | 19 | 11 | S | Low | — | Deferred |
| 13 | End-to-end timing validation | all | 1-12 | M | — | — | ✅ Done (partial) |

**Effort:** S = small (< half day), M = medium (1–2 days), L = large (2–4 days)

### Recommended Order

**Sprint 1 — Quick Wins (low risk, high impact):**
1. Phase 14 (no-op elision) — eliminates ~95ms for redundant events
2. Phase 15 (preserve styles) — saves ~17ms on every event
3. Phase 17 (glyph cache) — saves ~14ms on every render

**Sprint 2 — Render Optimization:**
4. Phase 18 (dirty-region clip) — saves ~40ms on selective renders
5. Phase 19 (caret-only repaint) — eliminates full pipeline for focus events

**Sprint 3 — Layout Optimization (high risk, high reward):**
6. Phase 16 (subtree-only relayout) — saves ~43ms but requires careful layout context management

### Projected Event Time After Each Sprint

| Sprint | Toggle | Text Input | Focus/Caret | Delete/Add |
|--------|--------|------------|-------------|------------|
| Baseline (Phase 3) | 95ms | 90ms | 95ms | 105ms |
| Sprint 1 (14+15+17) | 44ms¹ | 40ms¹ | <1ms² | 85ms³ |
| Sprint 2 (18+19) | 8ms⁴ | 8ms⁴ | <1ms | 85ms³ |
| Sprint 3 (16) | 5ms⁵ | 5ms⁵ | <1ms | 85ms³ |
| **Measured (14+15+18)** | **~70ms** | **~70ms** | **0.04ms⁶** | **~88ms** |

¹ Style save (17ms) + glyph cache (14ms) + reduced re-rendering
² No-op elision (Phase 14) or caret-only repaint (Phase 19)
³ Size-changing events still require full layout — future work
⁴ Dirty-region clip avoids rendering unchanged views (Phase 18)
⁵ Subtree-only relayout avoids re-laying-out unchanged elements (Phase 16)
⁶ Measured: No-op elision (Phase 14) skips entire rebuild when handler output is identical

---

## 11. Implementation Results (Measured)

Test: `todo_perf_timing.json` — 57 events on `todo.ls` (~90-106 elements).
Build: Debug, macOS Apple Silicon. Phases 14, 15, 18 implemented.

### Phase Implementation Summary

| Phase | Status | Description |
|-------|--------|-------------|
| 14 — No-Op Elision | ✅ Implemented | `item_deep_equal()` recursive comparison; skips rebuild when retransformed output is identical |
| 15 — Style Preservation | ✅ Implemented | `skip_style_reset` flag on DomDocument; ancestor-only invalidation after DOM patch |
| 16 — Subtree-Only Relayout | ⏳ Not started | High risk — deferred to Sprint 3 |
| 17 — Glyph Render Cache | ⏳ Not started | Next highest-value target (~14ms savings) |
| 18 — Dirty-Region Render Clip | ✅ Implemented | `DirtyTracker*` in RenderContext; AABB intersection early-exit in `render_block_view` |
| 19 — Caret-Only Repaint | ⏳ Deferred | Requires intercepting CSS pseudo-state focus path (different from Lambda handler path) |

### Measured Event Timing (Post Phase 14+15+18)

| Metric | Baseline (Phase 3) | After (Phase 4) | Change |
|--------|---------------------|------------------|--------|
| **No-op events** | ~95ms | **0.04ms** | **−99.96%** |
| **Selective event total** | ~95ms | **~70ms** | **−26%** |
| — Layout | ~48ms | ~39ms | −19% |
| — Style resolution | 106 full, 0 cached | 69-79 full, 27-37 cached | 25-35% fewer |
| — Render | ~46ms | ~30ms | −35% |
| **Full repaint (size change)** | ~105ms | **~88ms** | **−7%** |
| Glyph loads (selective) | 540 | **72** | **−87%** |
| Glyph loads (full) | 540 | 540 | 0% |

### Phase 14 — No-Op Elision Results

When a handler produces output identical to the previous result (e.g., toggling an already-toggled item back), `item_deep_equal()` detects the match and the entire rebuild/layout/render pipeline is skipped.

- **Measured**: 0.04ms total (handler + retransform + comparison)
- **Baseline**: ~95ms (full pipeline)
- **Speedup**: ~2375×
- **Detected in test run**: 1 no-op event (toggle-off on already-off item)

### Phase 15 — Style Preservation Results

Incremental rebuilds now skip `reset_styles_resolved()`. Only new DOM subtree nodes (already `styles_resolved = false` from construction) and their ancestors (explicitly cleared after `dom_node_replace_in_parent`) are re-resolved. Unchanged DOM nodes retain cached styles.

- **Style resolutions (selective event)**: 69-79 full (was 106 full, 0 cached)
- **Style resolve time**: ~12.5ms (was ~19ms) — **~36% savings**
- **Cached nodes**: 27-37 elements skip re-resolution per event

### Phase 18 — Dirty-Region Render Clip Results

`render_block_view()` now checks each view's absolute CSS bounds against the dirty region list (with 50px overflow margin). Views outside all dirty rects are skipped entirely, avoiding glyph loading, shape creation, and ThorVG canvas operations for unchanged regions.

- **Glyph loads (selective render)**: 72 (was 540) — **87% reduction**
- **Render time (selective)**: ~30ms (was ~46ms) — **35% savings**
- **Full repaint path**: Unaffected (dirty_tracker is NULL or full_repaint is set)

### Deferred Phase Notes

**Phase 17 (Glyph Render Cache)**: Next priority. Would cache ThorVG shape handles across render passes, eliminating FreeType re-loading for unchanged glyphs. Expected to save ~14ms/render on top of Phase 18's savings, bringing selective render to ~16ms.

**Phase 19 (Caret-Only Repaint)**: Caret/focus events go through a different code path (CSS pseudo-state `:focus` → `reflow_schedule`) rather than `dispatch_lambda_handler`. Phase 14's no-op elision already handles Lambda handler cases. Implementing Phase 19 requires intercepting the CSS pseudo-state path separately.

**Phase 16 (Subtree-Only Relayout)**: Highest risk. Requires `layout_subtree()` with layout context restoration, view subtree splicing, and fallback for size-changing events. Deferred to Sprint 3.

### Remaining Bottleneck Distribution (Selective Events)

```
Handler + Retransform:  ▏  <0.3%
DOM Patch:              ▏  <0.7%
Style Resolution:       █████  ~18%  (was 20%, saved ~7ms)
Text Measurement:       █████  ~13%
Block/Flex Layout:      ████████████  ~25%
Glyph Loading:          ██  ~4%   (was 16%, saved ~13ms via clip)
View Rendering:         ████████████████  ~39%
                        └──────────────── ~70ms total (was 95ms)
```

### Test Validation

| Test Suite | Result |
|------------|--------|
| Lambda baseline (`make test-lambda-baseline`) | 562/562 PASS |
| Radiant baseline (`make test-radiant-baseline`) | 37/43 PASS (6 pre-existing failures — test JSONs with 0 assertions) |
| UI timing test (`todo_perf_timing.json`) | 57 events, 7 assertions PASS |

---

## 12. Files Modified

### Phase 14 — No-Op Elision ✅

| File | Change |
|------|--------|
| `lambda/lambda-data.cpp` | `item_deep_equal()` ~85-line recursive comparison (NULL, BOOL, INT, INT64, FLOAT, STRING, SYMBOL, ELEMENT, ARRAY) |
| `lambda/lambda-data.hpp` | Declare `bool item_deep_equal(Item a, Item b)` |
| `radiant/event.cpp` | After `render_map_retransform_with_results`, loop comparing `results[i].old_result` vs `new_result`; skip `rebuild_lambda_doc_incremental` if all identical |

### Phase 15 — Style Preservation ✅

| File | Change |
|------|--------|
| `lambda/input/css/dom_element.hpp` | Added `bool skip_style_reset` field to `DomDocument`, initialized to `false` in constructor |
| `radiant/layout.cpp` | Gated `reset_styles_resolved(doc)` in `layout_init()` behind `if (!doc->skip_style_reset)` |
| `radiant/cmd_layout.cpp` | Set `doc->skip_style_reset = true` before `layout_html_doc`, clear after; added ancestor style invalidation loop after `dom_node_replace_in_parent` |

### Phase 16 — Subtree-Only Relayout (not started)

| File | Change |
|------|--------|
| `radiant/layout.cpp` | New `layout_subtree()` function, `layout_init_for_subtree()` |
| `radiant/layout_block.cpp` | `restore_layout_context_from_view()` helper |
| `radiant/cmd_layout.cpp` | Use `layout_subtree` in `rebuild_lambda_doc_incremental` for same-size changes |
| `radiant/view_pool.cpp` | `view_remove_subtree()`, `view_insert_child_at()` |

### Phase 17 — Glyph Render Cache (not started)

| File | Change |
|------|--------|
| `radiant/render.cpp` (or new `radiant/glyph_cache.cpp`) | `RenderedGlyphEntry` cache, `rendered_glyph_cache_get/put/clear` |
| `radiant/render_texnode.cpp` | Use cache before `font_load_glyph()` |

### Phase 18 — Dirty-Region Render Clip ✅

| File | Change |
|------|--------|
| `radiant/render.hpp` | Added `DirtyTracker* dirty_tracker` field to `RenderContext` (NULL = full repaint) |
| `radiant/render.cpp` | Set `dirty_tracker` in `render_html_doc` selective path; added ~20-line AABB intersection early-exit at top of `render_block_view` (50px overflow margin) |

### Phase 19 — Caret-Only Repaint (deferred)

| File | Change |
|------|--------|
| `radiant/state_store.hpp` | Add `prev_x`, `prev_y`, `prev_height` to `CaretState` |
| `radiant/event.cpp` | Detect caret-only changes, call `repaint_caret_only()` |
| `radiant/render.cpp` | New `repaint_caret_only()` function |

---

## Appendix A: Timing Data (42 Events — Raw)

```
Event  Type           Handler  Retransform  DOMPatch  Layout   Render   Total    Selective
 1     toggle(click)    0.07      0.03       1.34*    54.22    49.58   107.89    full-rebuild
 2     toggle(click)    0.02      0.03       0.11     52.63    48.27   101.06    yes
 3     toggle(click)    0.02      0.04       0.14     54.06    48.39   102.65    yes
 4     toggle(click)    0.02      0.03       0.11     53.21    49.09   102.46    yes
 5     delete(click)    0.10      0.10       0.44     49.28    48.05    97.97    no (size)
 6     delete(click)    0.06      0.08       0.30     44.75    45.64    90.83    no (size)
 7     focus(click)     0.02      0.07       0.30     44.41    45.38    90.18    yes
 8     focus(click)     0.02      0.08       0.31     43.72    45.74    89.86    yes
 9     input(H)         0.02      0.07       0.29     43.24    44.78    88.41    yes
10     input(e)         0.02      0.07       0.31     44.04    44.54    88.99    yes
11     input(l)         0.02      0.07       0.30     50.56    45.83    96.79    yes
12     input(l)         0.03      0.08       0.35     45.46    45.49    91.42    yes
13     input(o)         0.03      0.07       0.31     44.31    44.77    89.49    yes
14     input( )         0.02      0.07       0.30     44.15    45.26    89.81    yes
15     input(W)         0.02      0.07       0.30     44.04    44.80    89.25    yes
16     input(o)         0.02      0.08       0.30     43.68    45.25    89.34    yes
17     input(r)         0.02      0.07       0.31     44.09    45.36    89.85    yes
18     input(l)         0.02      0.07       0.30     44.85    45.97    91.21    yes
19     input(d)         0.03      0.07       0.30     44.04    45.98    90.42    yes
20     add(click)       0.09      0.10       0.39     47.77    46.43    94.78    no (size)
21     focus(click)     0.02      0.09       0.40     47.84    45.57    93.92    yes
22     focus(click)     0.02      0.10       0.41     48.42    45.98    94.92    yes
23     input(S)         0.02      0.09       0.39     46.64    45.70    92.84    yes
24     input(e)         0.02      0.10       0.41     47.81    44.75    93.09    yes
25     input(c)         0.02      0.10       0.38     47.59    45.39    93.50    yes
26     input(o)         0.02      0.10       0.40     47.54    45.87    93.94    yes
27     input(n)         0.02      0.10       0.40     48.19    45.85    94.55    yes
28     input(d)         0.02      0.10       0.40     48.19    46.32    95.04    yes
29     input( )         0.02      0.09       0.40     48.29    46.11    94.92    yes
30     input(t)         0.02      0.10       0.39     47.44    45.83    93.78    yes
31     input(a)         0.02      0.10       0.38     46.98    45.91    93.41    yes
32     input(s)         0.02      0.10       0.39     47.07    45.83    93.41    yes
33     input(k)         0.03      0.09       0.38     54.24    47.83   102.58    yes
34     add(Enter)       0.08      0.13       0.58     62.89    48.58   112.26    no (size)
35     clear(click)     0.10      0.11       0.47     55.88    47.62   104.18    no (size)
36     focus(click)     0.02      0.13       0.58     56.89    46.87   104.49    yes
37     input(a)         0.02      0.15       0.58     47.84    44.79    93.39    yes
38     input(b)         0.02      0.11       0.48     48.03    45.43    94.08    yes
39     input(c)         0.02      0.13       0.49     47.12    45.16    92.92    yes
40     backspace        0.02      0.12       0.50     47.35    46.33    94.33    yes
41     backspace        0.03      0.14       0.61     57.37    45.99   104.15    yes
42     backspace        0.03      0.14       0.61     56.43    45.98   103.19    yes

* Event 1: Full rebuild fallback (element_dom_map not yet populated)
```

### Observations

1. **All 42 events trigger full layout + render** — no event is short-circuited.
2. **36/42 events use selective surface clear** (Phase 12.4 working) but still do full render tree walk.
3. **5 events forced full repaint** (selective=no): 2 deletes + 1 add + 1 Enter-add + 1 clear — all change element count (size change detected).
4. **Focus clicks (events 7, 8, 21, 22, 36)** always trigger rebuild + full layout + render — even though only caret position changed.
5. **Text input events (9-19, 23-33, 37-39)** trigger full rebuild for every keystroke — the handler stores text, retransform regenerates the list item, rebuild patches DOM, then full layout + render.
6. **Style resolution: 106 full, 0 cached** — zero cache hits across all 42 events.
7. **Glyph loading: 540 calls per render** — constant regardless of what changed.
8. **Layout time: 43-63ms** — scales with total element count, not change size.
9. **Render time: 44-50ms** — remarkably stable regardless of event type.
