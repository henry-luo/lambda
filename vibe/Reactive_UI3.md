# Reactive UI Phase 3 — Performance & Language Cleanup

**Date:** 2026-04-05
**Status:** Phases 10, 11, 12.1–12.4, 13 implemented · All Phase 3 work complete
**Prerequisite:** Phases 6–9 complete (Reactive_UI2.md)

---

## Table of Contents

1. [Objective](#1-objective)
2. [Current Performance Baseline](#2-current-performance-baseline)
3. [Phase 10: Remove `it` Keyword — Use `~` Only](#3-phase-10-remove-it-keyword--use--only) ✅
4. [Phase 11: Event Handling Timing Instrumentation](#4-phase-11-event-handling-timing-instrumentation) ✅
5. [Phase 12: Incremental DOM Patching](#5-phase-12-incremental-dom-patching) ✅
   - [12.1 Subtree-Only DOM Rebuild](#121-subtree-only-dom-rebuild)
   - [12.2 CSS Cascade Reuse for Unchanged Subtrees](#122-css-cascade-reuse-for-unchanged-subtrees)
   - [12.3 Partial Relayout via DirtyTracker](#123-partial-relayout-via-dirtytracker)
   - [12.4 Damage-Region Repaint](#124-damage-region-repaint)
6. [Phase 13: Logging Cleanup](#6-phase-13-logging-cleanup) ✅
7. [Implementation Order](#7-implementation-order)
8. [Files Modified](#8-files-modified)

---

## 1. Objective

The reactive UI loop (event → handler → retransform → DOM → CSS → layout → render) is perceptibly slow. This proposal addresses four areas:

1. **Language cleanup** — Remove the `it` keyword; unify on `~` as the sole context accessor in templates.
2. **Instrumentation** — Add per-step timing to the reactive event path so we can measure where time is spent.
3. **Incremental DOM patching** — Replace the full DOM rebuild with targeted subtree updates.
4. **Logging cleanup** — Remove or gate excessive `log_info` calls in the hot event path that survive into debug builds.

---

## 2. Current Performance Baseline

Every user interaction triggers this full pipeline:

```
dispatch_lambda_handler                    [NO TIMING]
  ├─ DOM ancestry walk + reverse lookup    [NO TIMING]
  ├─ build_lambda_event_map                [NO TIMING]
  ├─ handler_fn(source_item, event_item)   [NO TIMING]
  ├─ render_map_retransform                [NO TIMING]
  │   └─ body_func(source_item)            [NO TIMING]
  │   └─ replace_in_element_tree           [NO TIMING]
  └─ rebuild_lambda_doc                    [NO TIMING]
      ├─ build_dom_tree_from_element       [NO TIMING] ← full tree, every time
      ├─ apply_stylesheet_to_dom_tree_fast [NO TIMING] ← full cascade, every time
      ├─ apply_inline_styles_to_tree       [NO TIMING]
      ├─ layout_html_doc                   [HAS TIMING] ← full layout, every time
      └─ render_html_doc                   [HAS TIMING] ← full repaint, every time
```

**After Phase 11 + 12.1 + 12.2 implementation:**

```
dispatch_lambda_handler                    [TIMED]
  ├─ DOM ancestry walk + reverse lookup
  ├─ build_lambda_event_map
  ├─ handler_fn(source_item, event_item)   [TIMED]
  ├─ retransform_with_results()            [TIMED] ← returns RetransformResult[]
  └─ rebuild_lambda_doc_incremental()      [TIMED]
      ├─ element_dom_map_lookup()          ← O(1) hashmap, find old DomElement
      ├─ build_dom_tree_from_element()     ← SUBTREE ONLY (changed element)
      ├─ dom_node_replace_in_parent()      ← O(1) linked-list splice
      ├─ apply_stylesheet_to_dom_tree_fast ← SUBTREE ONLY (scoped cascade)
      ├─ apply_inline_styles_to_tree       ← SUBTREE ONLY
      ├─ layout_html_doc                   [TIMED] ← still full layout (12.3 TODO)
      └─ render_html_doc                   [TIMED] ← still full repaint (12.4 TODO)
```

### What we know

- `layout_html_doc` and `render_html_doc` already have `[TIMING]` instrumentation via `std::chrono`.
- Everything upstream — handler execution, retransform, DOM rebuild, CSS cascade — has zero timing.
- The entire pipeline runs synchronously on the event thread. A 600-element page does full DOM rebuild + full CSS cascade + full layout + full render on every single click.
- `log_debug` and `log_info` are stripped in release builds (`NDEBUG`), so logging overhead is debug-only.

### Suspected bottlenecks (to be confirmed by Phase 11 instrumentation)

| Step | Suspected Cost | Why |
|------|---------------|-----|
| `build_dom_tree_from_element` | Medium-High | Allocates new `DomElement`/`DomText` for every node. For 600 elements → 600+ allocations per click. |
| `apply_stylesheet_to_dom_tree_fast` | Medium | Full selector matching on all rules × all elements, even when only 1 element changed. |
| `layout_html_doc` | Medium | Full layout from root, including unchanged subtrees. Layout cache helps but doesn't avoid the traversal. |
| `render_html_doc` | Low-Medium | Full canvas clear + full paint. ThorVG canvas sync is GPU-bound. |
| `handler_fn` + `body_func` | Low | JIT-compiled native code, executes in microseconds. |

---

## 3. Phase 10: Remove `it` Keyword — Use `~` Only ✅

### Rationale

Templates currently bind the model parameter to both `it` (as a local variable) and `~` (via the pipe-item register). Having two names for the same thing creates confusion. `~` is the established Lambda context accessor used throughout pipes and expressions. `it` is a regular identifier that only gets special binding inside `view`/`edit` bodies — it's not a grammar keyword, just a variable name injected by the transpiler.

### Current Implementation

In `transpile-mir.cpp`, two `set_var` calls bind `"it"`:

```cpp
// In view/edit body transpilation (~L10827):
set_var(mt, "it", model_reg, MIR_T_I64, LMD_TYPE_ANY);

// In handler body transpilation (~L10990):
set_var(mt, "it", model_reg, MIR_T_I64, LMD_TYPE_ANY);
```

`~` is already handled independently via `mt->pipe_item_reg` — no code change needed for `~` to work.

### Changes Required

#### 3.1 Remove `set_var("it", ...)` bindings

| File | Change |
|------|--------|
| `lambda/transpile-mir.cpp` ~L10827 | Remove `set_var(mt, "it", model_reg, ...)` from body transpilation |
| `lambda/transpile-mir.cpp` ~L10990 | Remove `set_var(mt, "it", model_reg, ...)` from handler transpilation |

Two lines deleted. No grammar changes, no AST changes, no interpreter changes.

#### 3.2 Update test scripts

Replace all `it.` and `it` references with `~.` and `~` in view/edit template bodies:

| File | Count | Example |
|------|-------|---------|
| `test/lambda/ui/todo.ls` | ~16 | `it.done` → `~.done`, `it.items` → `~.items` |
| `test/lambda/view_template.ls` | ~5 | `it.name` → `~.name` |
| `test/lambda/render_map.ls` | ~1 | `it.x` → `~.x` |

#### 3.3 Update documentation

| File | Change |
|------|--------|
| `doc/Reactive_UI.md` | Replace `it` references with `~` in examples |
| `vibe/Reactive_UI.md` | Update examples |
| `vibe/Reactive_UI2.md` | Update examples |

#### 3.4 Migration note

This is a breaking change for any `.ls` scripts using `it` in templates. Since the feature is new and only internal test scripts use it, the migration is trivial.

---

## 4. Phase 11: Event Handling Timing Instrumentation ✅

### Rationale

We cannot optimize what we cannot measure. The upstream half of the reactive pipeline has no timing instrumentation. We need to know exactly which step dominates.

### Design

Add timing to `dispatch_lambda_handler` and `rebuild_lambda_doc` using the same `std::chrono::high_resolution_clock` pattern already used in `layout_html_doc` and `render_html_doc`. Gate behind `log_info` (stripped in release) or use `log_notice` (survives release) for the summary line.

### 4.1 Instrumented `dispatch_lambda_handler`

Add timing points around each major step:

```cpp
auto t0 = high_resolution_clock::now();

// ... reverse lookup + template search ...
auto t1 = high_resolution_clock::now();

// ... handler invocation ...
fn(lookup.source_item, event_item);
auto t2 = high_resolution_clock::now();

// ... retransform ...
int count = render_map_retransform();
auto t3 = high_resolution_clock::now();

// ... rebuild ...
rebuild_lambda_doc(evcon->ui_context);
auto t4 = high_resolution_clock::now();

log_info("[TIMING] event dispatch: lookup=%.2fms handler=%.2fms retransform=%.2fms rebuild=%.2fms total=%.2fms",
    ms(t1-t0), ms(t2-t1), ms(t3-t2), ms(t4-t3), ms(t4-t0));
```

### 4.2 Instrumented `rebuild_lambda_doc`

Break down the rebuild into sub-steps:

```cpp
auto t0 = high_resolution_clock::now();
DomElement* new_root = build_dom_tree_from_element(html_elem, doc, nullptr);
auto t1 = high_resolution_clock::now();

// ... CSS cascade ...
auto t2 = high_resolution_clock::now();

// ... inline styles ...
auto t3 = high_resolution_clock::now();

layout_html_doc(uicon, doc, false);
auto t4 = high_resolution_clock::now();

render_html_doc(uicon, doc->view_tree, NULL);
auto t5 = high_resolution_clock::now();

log_info("[TIMING] rebuild: dom_build=%.2fms css_cascade=%.2fms inline_styles=%.2fms layout=%.2fms render=%.2fms total=%.2fms",
    ms(t1-t0), ms(t2-t1), ms(t3-t2), ms(t4-t3), ms(t5-t4), ms(t5-t0));
```

### 4.3 Expected output

After one click on a todo item:
```
[TIMING] event dispatch: lookup=0.01ms handler=0.02ms retransform=0.05ms rebuild=12.30ms total=12.38ms
[TIMING] rebuild: dom_build=1.20ms css_cascade=3.50ms inline_styles=0.30ms layout=4.80ms render=2.50ms total=12.30ms
[TIMING] layout_html_root: 4.2ms
[TIMING] render_block_view: 2.1ms
[TIMING] tvg_canvas_sync: 0.3ms
```

This will immediately reveal which step to attack first.

---

## 5. Phase 12: Incremental DOM Patching

Replace the current full-rebuild approach with targeted updates. The four sub-phases are ordered by expected impact and can be implemented independently.

### Current full-rebuild pipeline (per event):

```
rebuild_lambda_doc:
  1. build_dom_tree_from_element(html_root) ← ENTIRE tree
  2. apply_stylesheet_to_dom_tree_fast()    ← ALL rules × ALL elements
  3. apply_inline_styles_to_tree()          ← ALL elements
  4. layout_html_doc()                      ← doc->view_tree = nullptr → FULL layout
  5. render_html_doc()                      ← FULL repaint
```

### Target incremental pipeline (per event) — *implemented*:

```
rebuild_lambda_doc_incremental:
  1. record old bounds (from previous layout)         ← O(changed) parent-chain walk
  2. build_dom_subtree(changed_elem)                  ← ONLY changed subtree
  3. apply_stylesheet_to_subtree()                    ← ONLY new/changed nodes
  4. apply_inline_styles_to_subtree()                 ← ONLY new/changed nodes
  5. layout_html_doc(is_reflow=true)                  ← full layout (pool reused)
  6. compute dirty rects (old vs new bounds)           ← DirtyTracker
  7. render_html_doc (selective clear)                 ← full tree walk, selective surface clear
```

**Savings:** Steps 1–4 are O(changed subtree) vs O(entire tree). Step 5 remains full (true incremental layout is future work). Step 7 saves surface-clear time proportional to (1 − dirty_area/total_area) when element sizes unchanged.

### 12.1 Subtree-Only DOM Rebuild

#### Problem

`rebuild_lambda_doc` calls `build_dom_tree_from_element(html_root, ...)` which traverses the entire Lambda element tree and allocates a fresh `DomElement` / `DomText` for every single node. For a 600-element page, that's 600+ allocations even when only 1 `<li>` changed.

#### Design

The render map already knows which `(source_item, template_ref)` entries were retransformed. Each entry stores `parent_result` and `child_index`. After `render_map_retransform()` replaces the result node in the Lambda element tree, we know exactly which parent element has a changed child.

**New function: `rebuild_dom_subtree()`**

```
rebuild_dom_subtree(doc, parent_dom_elem, changed_lambda_elem, child_index):
  1. Build new DomElement subtree from changed_lambda_elem only
  2. Replace parent_dom_elem->children[child_index] with new subtree
  3. Return the new DomElement subtree root (for CSS/layout targeting)
```

**Integration point:** Replace `rebuild_lambda_doc` call in `dispatch_lambda_handler` with:

```cpp
if (render_map_has_dirty()) {
    RetransformResult results[MAX_DIRTY];
    int count = render_map_retransform_with_results(results);

    for (int i = 0; i < count; i++) {
        DomElement* changed_subtree = rebuild_dom_subtree(
            doc, results[i].parent_dom, results[i].new_result, results[i].child_index);
        apply_stylesheet_to_subtree(changed_subtree, cached_sheets, matcher, pool, engine);
        dirty_mark_element(&state->dirty_tracker, changed_subtree);
    }

    layout_dirty_subtrees(uicon, doc);
    repaint_damaged_regions(uicon, doc);
}
```

#### Key requirement: DOM↔Lambda element mapping

Currently `DomElement->native_element` points to the Lambda `Element*`. We need the reverse: given a Lambda element, find its `DomElement`. Two approaches:

**Option A — Element-to-DOM map:** Maintain a `HashMap<Element*, DomElement*>` updated during `build_dom_tree_from_element`. On retransform, look up the parent's `DomElement` from its Lambda `Element*`.

**Option B — Walk from DOM root:** Given `parent_result` (Lambda Element), walk the existing DOM tree comparing `native_element` pointers to find the parent `DomElement`. O(n) but only needed for dirty entries (typically 1).

**Recommendation:** Option A. The hashmap has O(1) lookup and the memory cost is one pointer per element — negligible.

#### render_map_retransform_with_results

Extend `render_map_retransform` to return information about what changed:

```c
typedef struct RetransformResult {
    Item parent_result;     // Lambda parent element
    Item new_result;        // new Lambda result element
    Item old_result;        // old Lambda result element (for DOM lookup)
    int child_index;        // position in parent
    const char* template_ref;
} RetransformResult;

int render_map_retransform_with_results(RetransformResult* out_results, int max_results);
```

### 12.2 CSS Cascade Reuse for Unchanged Subtrees

#### Problem

`apply_stylesheet_to_dom_tree_fast` walks the entire DOM tree and matches every CSS rule against every element. For a 600-element tree with 50 CSS rules, that's up to 30,000 selector matches — even when only a 5-element `<li>` subtree changed.

#### Design

After subtree-only DOM rebuild, only the new subtree nodes need style resolution. Unchanged DomElements retain their resolved `CssDeclaration` arrays from the previous cycle.

**New function: `apply_stylesheet_to_subtree()`**

```c
void apply_stylesheet_to_subtree(DomElement* subtree_root,
                                  CssStylesheet** sheets, int sheet_count,
                                  SelectorMatcher* matcher, Pool* pool,
                                  CssEngine* engine) {
    // Only match rules against subtree_root and its descendants
    // Inherit computed values from subtree_root->parent (already resolved)
    apply_stylesheet_to_dom_tree_fast(subtree_root, ...);  // reuse existing function
}
```

The existing `apply_stylesheet_to_dom_tree_fast` already does a recursive tree walk from its root argument. Passing a subtree root instead of the document root naturally limits the scope.

**Inheritance:** CSS inheritance must work correctly. The subtree root's parent already has resolved styles from the previous cycle. As long as `apply_stylesheet_to_dom_tree_fast` reads inherited values from `parent->computed_style`, this works without changes.

**Invalidation:** If a class or attribute changed on the subtree root (e.g., `todo-item` → `todo-item done`), the cascade for that element and its descendants must be recomputed. Since we're rebuilding the DOM subtree from scratch (new DomElements), they have no stale styles — the cascade is applied fresh to the new nodes.

### 12.3 Partial Relayout via DirtyTracker

#### Problem

`rebuild_lambda_doc` sets `doc->view_tree = nullptr` forcing `layout_html_doc` to create an entirely new view tree and lay out the full document from scratch. This discards all cached layout results for unchanged elements.

#### Existing infrastructure

Radiant already has the building blocks for partial relayout:

| Component | File | Status |
|-----------|------|--------|
| `DirtyTracker` | `radiant/state_store.hpp` | ✅ Implemented — dirty rect list, `full_repaint`/`full_reflow` flags |
| `DirtyRect` | `radiant/state_store.hpp` | ✅ Implemented — linked list of damage regions |
| `ReflowScheduler` | `radiant/state_store.hpp` | ✅ Implemented — `ReflowScope` enum (SELF_ONLY → FULL), request queue |
| `ReflowScope` | `radiant/state_store.hpp` | ✅ Implemented — `REFLOW_SUBTREE` is exactly what we need |
| Layout cache | `radiant/layout_cache.hpp` | ✅ Implemented — 9-slot per-element measurement cache |
| `needs_style_recompute` | `dom_element.hpp` | ✅ Flag on DomElement |
| `style_version` | `dom_element.hpp` | ✅ Version counter for cache invalidation |

None of these are wired into the reactive UI rebuild path.

#### Design

**Step 1: Preserve the view tree across rebuilds**

Instead of `doc->view_tree = nullptr`, keep the existing view tree. After subtree DOM rebuild, mark the affected view nodes as needing relayout.

**Step 2: Map DOM change to view tree node**

Each `DomElement` has a corresponding `View*` in the view tree (set during layout). After replacing a DOM subtree, find the parent view and schedule a subtree reflow:

```c
// After DOM subtree replacement:
View* parent_view = find_view_for_dom_element(parent_dom_elem);
reflow_schedule(&state->reflow_scheduler, parent_view, REFLOW_SUBTREE, REASON_CONTENT_CHANGE);
```

**Step 3: Execute partial reflow**

Process the reflow queue: for each `REFLOW_SUBTREE` request, re-layout only that view subtree. The parent's dimensions are fixed (unless content size forces a resize, which escalates to `REFLOW_ANCESTORS`).

```c
void layout_dirty_subtrees(UiContext* uicon, DomDocument* doc) {
    ReflowScheduler* sched = &state->reflow_scheduler;
    while (sched->pending) {
        ReflowRequest* req = sched->pending;
        sched->pending = req->next;

        if (req->scope == REFLOW_SUBTREE) {
            // Re-layout only this subtree with fixed available width from parent
            layout_subtree(&lycon, req->node);
        } else if (req->scope >= REFLOW_ANCESTORS) {
            // Content change affects ancestors — escalate to full reflow
            layout_html_doc(uicon, doc, true);
            break;
        }
    }
}
```

**Escalation rule:** If the changed subtree's computed dimensions differ from the previous layout, escalate from `REFLOW_SUBTREE` to `REFLOW_ANCESTORS` (the parent's layout depends on child sizes). For a todo toggle (only text/class change, same dimensions), `REFLOW_SUBTREE` suffices.

#### When to fall back to full reflow

| Scenario | Reflow scope |
|----------|-------------|
| Toggle checkbox (class change, same content) | `REFLOW_SUBTREE` — just re-style + repaint |
| Delete item (child removed) | `REFLOW_ANCESTORS` — parent's height changed |
| Add item (child added) | `REFLOW_ANCESTORS` — parent's height changed |
| Text input (content change) | `REFLOW_SUBTREE` if width unchanged, else `REFLOW_ANCESTORS` |

### 12.4 Damage-Region Repaint

#### Problem

`render_html_doc` clears the entire canvas and repaints every view, even when only one `<li>` element changed visual state.

#### Design

Use `DirtyTracker`'s dirty rect list to clip the repaint region:

```c
void repaint_damaged_regions(UiContext* uicon, DomDocument* doc) {
    DirtyTracker* tracker = &state->dirty_tracker;

    if (tracker->full_repaint || !dirty_has_regions(tracker)) {
        // Fall back to full repaint
        render_html_doc(uicon, doc->view_tree, NULL);
    } else {
        // Repaint only damaged rectangles
        for (DirtyRect* r = tracker->dirty_list; r; r = r->next) {
            RenderContext rdcon;
            render_init_clipped(&rdcon, uicon, doc->view_tree, r->x, r->y, r->width, r->height);
            render_block_view_clipped(&rdcon, root_block, r);
        }
        tvg_canvas_sync(rdcon.canvas);
    }
    dirty_clear(tracker);
}
```

**Dirty rect sources:**
1. After relayout, compare old and new bounds of the changed view. Mark both old bounds (to clear) and new bounds (to paint) as dirty.
2. `dirty_mark_element()` already computes absolute bounds from the view's position.

**ThorVG clipping:** ThorVG supports clip regions on canvas draw operations. Set a clip rect before rendering to limit GPU work to the damaged area.

#### Fallback

If the dirty region covers >50% of the viewport, or if the change affects the document height (scroll recalculation needed), fall back to full repaint. The `full_repaint` flag on `DirtyTracker` handles this.

---

## 6. Phase 13: Logging Cleanup ✅

### Current state

- `log_debug` and `log_info` → **stripped in release builds** (`#define log_debug(...) ((void)0)` when `NDEBUG` defined).
- Therefore, logging overhead only affects debug builds. In release, it's zero-cost.

### Debug build overhead

In `event.cpp` alone, there are 60+ `log_debug`/`log_info` calls. In a debug build, every mouse move generates dozens of log writes for hit-test traversal, hover state changes, and cursor updates. While these are useful for development, the I/O overhead in debug builds makes interactive testing sluggish.

### Recommended changes

#### 13.1 Gate verbose hit-test logging behind a flag

The `target_html_doc` hit-test function and `fire_*_event` functions generate the most log volume during interaction. Gate these behind a compile-time or runtime flag so they can be silenced during interactive debugging without losing other debug output.

```c
// In event.cpp, near top:
#ifndef VERBOSE_EVENT_LOG
#define VERBOSE_EVENT_LOG 0
#endif

// Replace individual log_debug calls in hot paths:
#if VERBOSE_EVENT_LOG
    log_debug("hit on block: %s", block->node_name());
#endif
```

Alternatively, use the existing `clog_debug(category, ...)` category-based logging with a dedicated event category that can be toggled via `log.conf`.

#### 13.2 Downgrade noisy `log_info` to `log_debug` in event path

Several `log_info` calls in the reactive path are diagnostic, not informational. In debug builds these survive even at higher log levels. Downgrade to `log_debug`:

| File | Line | Current | Change to |
|------|------|---------|-----------|
| `event.cpp` ~L630 | `log_info("dispatch_lambda_handler: invoking...")` | `log_debug` |
| `event.cpp` ~L684 | `log_info("dispatch_lambda_handler: retransformed %d...")` | `log_debug` |
| `event.cpp` ~L555 | `log_info("dispatch_emit: found '%s' handler...")` | `log_debug` |
| `event.cpp` ~L938+ | `log_info("find_checkbox_radio_input: ...")` (6 calls) | `log_debug` |
| `render_map.cpp` ~L147 | `log_info("render_map_record: tmpl=...")` | `log_debug` |
| `render_map.cpp` ~L279 | `log_info("render_map_retransform: re-transformed %d...")` | `log_debug` |
| `cmd_layout.cpp` ~L4117 | `log_info("rebuild_lambda_doc: rebuilding DOM...")` | `log_debug` |
| `cmd_layout.cpp` ~L4162 | `log_info("rebuild_lambda_doc: cached %d...")` | `log_debug` |
| `cmd_layout.cpp` ~L4191 | `log_info("rebuild_lambda_doc: restored focus...")` | `log_debug` |
| `cmd_layout.cpp` ~L4200 | `log_info("rebuild_lambda_doc: DOM rebuild and relayout complete")` | `log_debug` |

The `[TIMING]` lines in `layout_html_doc` and `render_html_doc` should remain `log_info` — they are intentional performance instrumentation.

#### 13.3 keep `[TIMING]` lines as `log_notice` for release profiling

For the new timing instrumentation added in Phase 11, use `log_notice` (survives NDEBUG) for the summary line so it can be observed in release builds when diagnosing user-reported lag:

```c
log_notice("[TIMING] event total: %.2fms (handler=%.2fms retransform=%.2fms rebuild=%.2fms)",
    ms(t4-t0), ms(t2-t1), ms(t3-t2), ms(t4-t3));
```

Use `log_info` for the detailed per-step breakdown (debug-only).

---

## 7. Implementation Order

| # | Task | Phase | Deps | Effort | Risk | Status |
|---|------|-------|------|--------|------|--------|
| 1 | Remove `set_var("it", ...)` from transpile-mir.cpp | 10 | — | S | Low — 2 lines removed | ✅ Done |
| 2 | Update test scripts: `it` → `~` | 10 | 1 | S | Low — ~22 replacements | ✅ Done |
| 3 | Update docs: `it` → `~` | 10 | 1 | S | Low | ✅ Done |
| 4 | Add timing to `dispatch_lambda_handler` | 11 | — | S | Low — read-only instrumentation | ✅ Done |
| 5 | Add timing to `rebuild_lambda_doc` | 11 | — | S | Low — read-only instrumentation | ✅ Done |
| 6 | Run timing, analyze bottleneck distribution | 11 | 4,5 | S | — | Next step |
| 7 | Downgrade noisy `log_info` → `log_debug` | 13 | — | S | Low | ✅ Done |
| 8 | Gate verbose hit-test logging | 13 | — | S | Low | ✅ Done |
| 9 | Element-to-DOM hashmap | 12.1 | — | M | Low | ✅ Done |
| 10 | `render_map_retransform_with_results` | 12.1 | — | M | Medium — API change | ✅ Done |
| 11 | `rebuild_dom_subtree` | 12.1 | 9,10 | M | Medium | ✅ Done |
| 12 | Scope CSS cascade to subtree | 12.2 | 11 | S | Low — already works by passing subtree root | ✅ Done |
| 13 | Dirty tracking from old/new bounds | 12.3 | 11 | M | Medium — compare old/new size, mark DirtyRects | ✅ Done |
| 14 | Reuse ViewTree, fix pool leak | 12.3 | 13 | S | Low — `view_pool_destroy` + `is_reflow=true` | ✅ Done |
| 15 | Selective surface clear via DirtyTracker | 12.4 | 13 | M | Medium — clear only dirty rects in `render_html_doc` | ✅ Done |
| 16 | End-to-end validation: click → partial update | 12 | 11-15 | M | — | ✅ Done |

**Effort:** S = small (< half day), M = medium (1–2 days), L = large (2–4 days)

**Recommended order:**

1. Phases 10 + 13 first (cleanup, low risk, immediate debug-build improvement).
2. Phase 11 next (instrumentation — must measure before optimizing).
3. Phase 12.1 + 12.2 (subtree DOM rebuild + scoped CSS — highest expected impact).
4. Phase 12.3 + 12.4 (partial relayout + damage repaint — complex, defer until 12.1/12.2 results are measured).

---

## 8. Files Modified

### Phase 10 — Remove `it`

| File | Change |
|------|--------|
| `lambda/transpile-mir.cpp` | Remove 2 × `set_var(mt, "it", ...)` |
| `test/lambda/ui/todo.ls` | `it.` → `~.`, `it` → `~` (~16 occurrences) |
| `test/lambda/view_template.ls` | `it.` → `~.` (~5 occurrences) |
| `test/lambda/render_map.ls` | `it.` → `~.` (~1 occurrence) |
| `doc/Reactive_UI.md` | Update examples |

### Phase 11 — Timing

| File | Change |
|------|--------|
| `radiant/event.cpp` | Add `std::chrono` timing around handler, retransform, rebuild in `dispatch_lambda_handler` |
| `radiant/cmd_layout.cpp` | Add timing around each step in `rebuild_lambda_doc` |

### Phase 12 — Incremental DOM (12.1–12.4 implemented)

| File | Change |
|------|--------|
| `lambda/render_map.h` | Add `RetransformResult` struct, `render_map_retransform_with_results()` |
| `lambda/render_map.cpp` | Implement `retransform_with_results` (captures old/new result + parent info) |
| `lambda/input/css/dom_element.hpp` | Add `element_dom_map` field to `DomDocument` |
| `lambda/input/css/dom_element.cpp` | `ElementDomMapEntry` hashmap helpers, populate during `build_dom_tree_from_element`, `dom_node_replace_in_parent` |
| `radiant/cmd_layout.cpp` | `rebuild_lambda_doc_incremental` — subtree DOM patch + scoped CSS + dirty tracking + ViewTree reuse |
| `radiant/event.cpp` | Use `retransform_with_results` + `rebuild_lambda_doc_incremental` |
| `radiant/render.cpp` | Selective surface clear via `DirtyTracker` in `render_html_doc` |

### Phase 13 — Logging

| File | Change |
|------|--------|
| `radiant/event.cpp` | Downgrade ~8 `log_info` → `log_debug`, gate verbose hit-test logs |
| `lambda/render_map.cpp` | Downgrade 2 `log_info` → `log_debug` |
| `radiant/cmd_layout.cpp` | Downgrade 4 `log_info` → `log_debug` in `rebuild_lambda_doc` |

---

## 9. Implementation Notes

### Phase 10 — Completed 2026-04-05

Removed `it` keyword from view/edit templates. `~` is now the sole context accessor.

**Transpiler changes (`lambda/transpile-mir.cpp`):**
- Removed `set_var(mt, "it", model_reg, ...)` from view/edit body transpilation and handler body transpilation.
- Added `in_view_context` check to `AST_NODE_CURRENT_ITEM` case: when `mt->in_view_context` is true, `~` resolves to `mt->view_model_reg` (the model parameter), not just in pipe context. This was needed because `~` previously only worked inside pipes — without `it`, it needed to resolve in the broader template body.

**Test scripts updated (5 files, ~25 replacements):**
- `test/lambda/ui/todo.ls` — `it.done` → `~.done`, `it.text` → `~.text`, etc.
- `test/lambda/view_template.ls` — `it * 2` → `~ * 2`, etc.
- `test/lambda/view_state.ls` — `it ++ ":"` → `~ ++ ":"`
- `test/lambda/render_map.ls` — `it ++ ":"` → `~ ++ ":"`
- `test/lambda/edit_bridge.ls` — `string(it)` → `string(~)`, etc.

**Docs updated:** `doc/Reactive_UI.md` examples changed from `it` to `~`.

### Phase 11 — Completed 2026-04-05

Added `std::chrono::high_resolution_clock` timing instrumentation to both event dispatch and document rebuild.

**`radiant/event.cpp` — `dispatch_lambda_handler`:**
- Timing around handler invocation, `render_map_retransform`, and `rebuild_lambda_doc`.
- Output: `[TIMING] event dispatch: handler=%.2fms retransform=%.2fms rebuild=%.2fms total=%.2fms`

**`radiant/cmd_layout.cpp` — `rebuild_lambda_doc`:**
- Timing around `build_dom_tree_from_element`, CSS cascade, `layout_html_doc`, `render_html_doc`.
- Output: `[TIMING] rebuild: dom_build=%.2fms css_cascade=%.2fms layout=%.2fms render=%.2fms total=%.2fms`

Both use `log_info` (stripped in release builds via `NDEBUG`).

**Next step:** Run `./lambda.exe view test/lambda/ui/todo.ls` in debug build and interact with the UI to collect actual timing data. This will determine which Phase 12 sub-phase to prioritize.

### Phase 13 — Completed 2026-04-05

Downgraded 25+ `log_info` → `log_debug` across hot event/render paths. Only `[TIMING]` instrumentation lines remain as `log_info`.

**Files changed:**
- `radiant/event.cpp` — 21 `log_info` → `log_debug` (dispatch_lambda_handler, dispatch_emit, form controls, mouse events, caret, URL opening, dropdown)
- `lambda/render_map.cpp` — 2 `log_info` → `log_debug` (render_map_record, retransform count)
- `radiant/cmd_layout.cpp` — 2 `log_info` → `log_debug` (cached sheets, restored focus)
- `radiant/render.cpp` — 2 `log_info` → `log_debug` (render_ui_overlays state)

### Build Verification (after all phases)

- **Build:** 0 errors, 273 warnings (debug build)
- **Tests:** 561/562 passed (254 Lambda Runtime, 106 Lambda Structured, 61 Lambda Errors, 6 Lambda Proc, 38 Lambda REPL, 19 TypeScript, 77/78 JS)
- **1 failure:** `test/js/v11_labeled_statements.js` — pre-existing array formatting difference, unrelated to these changes

### Phase 12.1 + 12.2 — Completed 2026-04-05

Implemented incremental DOM patching: only changed subtrees are rebuilt and re-cascaded. Falls back to full rebuild on first event (to populate the element-to-DOM map) and when incremental conditions aren't met.

**Architecture:**

```
Event → handler → retransform_with_results()
                        ↓
              [RetransformResult: old_result, new_result, parent_result, child_index]
                        ↓
           rebuild_lambda_doc_incremental()
                        ↓
              ┌─ Can incremental? ──→ NO → full rebuild (populates element_dom_map)
              └─ YES:
                  0. record old bounds from previous layout    [12.3]
                  for each result:
                    1. element_dom_map_lookup(old_result.element) → old_dom
                    2. build_dom_tree_from_element(new_result.element) → new_dom  [subtree only]
                    3. dom_node_replace_in_parent(parent_dom, old_dom, new_dom)
                    4. apply_stylesheet_to_dom_tree_fast(new_dom, ...)            [subtree only]
                    5. apply_inline_styles_to_tree(new_dom, new_elem, ...)        [subtree only]
                  6. view_pool_destroy + layout_html_doc(is_reflow=true)          [pool reused]
                  7. compute dirty rects (old/new bounds → DirtyTracker)          [12.3]
                  8. render_html_doc (selective clear via dirty rects)            [12.4]
```

**Key components:**

1. **Element-to-DOM hashmap** (`dom_element.cpp`): `ElementDomMapEntry` maps `Element*` → `DomElement*`. Created via `element_dom_map_create()`, populated automatically during `build_dom_tree_from_element` when `doc->element_dom_map` is non-null. O(1) lookup.

2. **`render_map_retransform_with_results`** (`render_map.cpp`): Clone of `render_map_retransform` that fills a `RetransformResult[]` array with old/new result Items, parent, and child_index. Enables the caller to know exactly what changed.

3. **`dom_node_replace_in_parent`** (`dom_element.cpp`): Splices a new DomNode into an old DomNode's position in the parent's linked-list child chain. O(1) pointer surgery.

4. **`rebuild_lambda_doc_incremental`** (`cmd_layout.cpp`): Main integration function. Checks feasibility (map exists, all results are Elements with valid map entries), then either patches subtrees or falls back to full `rebuild_lambda_doc`.

5. **Scoped CSS cascade**: Passes the new subtree root to `apply_stylesheet_to_dom_tree_fast` instead of document root. The existing function naturally limits scope by walking from its root argument.

**Event lifecycle:**
- 1st event: `retransform_with_results` → `rebuild_lambda_doc_incremental` → no map yet → fallback to `rebuild_lambda_doc` (creates map) → full rebuild
- 2nd+ events: `retransform_with_results` → `rebuild_lambda_doc_incremental` → map exists → incremental: record old bounds → patch subtree → scoped CSS → reuse ViewTree pool → layout → dirty rects → selective render

**Timing output:** `[TIMING] rebuild_incr: dom_patch=%.2fms layout=%.2fms render=%.2fms total=%.2fms (subtrees=%d, selective=yes/no)`

**Build:** 0 errors. **Tests:** 561/562 passed (1 pre-existing JS failure).

### Phase 12.3 — Dirty Tracking — Completed 2026-04-05

Records old and new bounds of changed DOM subtrees to determine which screen regions need repainting. Falls back to full repaint when element sizes change (layout cascading makes partial repaint unsafe).

**Key insight:** Views ARE DomNodes (via `View* = (View*)node` cast in `set_view()`). DomNode x/y/w/h fields persist from previous layout pass, enabling old-bounds capture before DOM patching.

**Algorithm:**

```
Before DOM patch:
  for each changed result:
    old_dom = element_dom_map_lookup(old_result.element)
    old_bounds[i] = compute_absolute_bounds(old_dom)  // walk parent chain

After DOM patch + CSS cascade + layout:
  for each changed result:
    new_bounds = compute_absolute_bounds(new_doms[i])
    if any size changed (|old_w - new_w| > 0.5 || |old_h - new_h| > 0.5):
      → full_repaint = true   (layout cascaded to siblings/ancestors)
    else:
      → dirty_mark_rect(old_bounds)  +  dirty_mark_rect(new_bounds)
```

**ViewTree reuse:** Instead of leaking the old ViewTree (`doc->view_tree = nullptr`), now calls `view_pool_destroy()` then `layout_html_doc(uicon, doc, true)` which reuses the ViewTree struct and creates a fresh pool.

**Files changed:**
- `radiant/cmd_layout.cpp` — Added `compute_absolute_bounds()` helper, old bounds recording before DOM patch, `new_doms[]` tracking, dirty rect computation after layout, ViewTree pool reuse, `view_pool_destroy` forward declaration
- Timing output now includes `selective=yes/no` flag

### Phase 12.4 — Selective Repaint — Completed 2026-04-05

Modified `render_html_doc` to check the DirtyTracker on document state. When dirty regions are available (and not `full_repaint`), only dirty rects are cleared to background color instead of the full surface.

**Algorithm in `render_html_doc`:**

```
state = uicon->document->state
if state has dirty regions AND not full_repaint:
  for each DirtyRect in dirty_list:
    fill_surface_rect(surface, dirty_rect * scale, background_color, clip)  // selective clear
else:
  fill_surface_rect(surface, NULL, background_color, clip)                  // full clear
```

**Scaling:** Dirty rects are in CSS logical pixels; surface is in physical pixels. The selective clear multiplies by `rdcon.scale` (pixel ratio) for HiDPI support.

**Files changed:**
- `radiant/render.cpp` — Selective clear in `render_html_doc`, `selective` flag in timing log

**Combined event pipeline (after all Phase 12 work):**

```
Event → handler → retransform_with_results()
  → rebuild_lambda_doc_incremental():
      1. Record old bounds (from previous layout pass)
      2. DOM patch: build_dom_tree, dom_node_replace_in_parent     [subtree only]
      3. CSS cascade: apply_stylesheet_to_dom_tree_fast            [subtree only]
      4. Layout: view_pool_destroy + layout_html_doc(is_reflow)    [full — view pool reused]
      5. Compute dirty rects (old/new bounds → DirtyTracker)
      6. Render: render_html_doc (selective clear via dirty rects) [full tree, selective clear]
```

**Build:** 0 errors. **Tests:** 561/562 passed (1 pre-existing JS failure).

**Timing output:** `[TIMING] rebuild_incr: dom_patch=%.2fms layout=%.2fms render=%.2fms total=%.2fms (subtrees=%d, selective=yes/no)`

---

## Appendix A: UI Automation Timing Results

**Test:** `test/ui/todo_perf_timing.json` — 42 events on `todo.ls` (3 lists, ~8 items).
**Build:** Debug. **Platform:** macOS, Apple Silicon.

### Summary

| Metric | Average | Min | Max |
|--------|---------|-----|-----|
| Handler | 0.03ms | 0.01ms | 0.08ms |
| Retransform | 0.07ms | 0.02ms | 0.12ms |
| Rebuild (DOM+CSS+Layout+Render) | 85.4ms | 79.3ms | 104.2ms |
| **Total** | **85.5ms** | **79.4ms** | **104.4ms** |

### Key Findings

- **Handler execution** is negligible (<0.1ms), confirming JIT-compiled handlers are fast.
- **Retransform** (re-executing template body) is also negligible (<0.15ms).
- **Rebuild dominates** at ~99.9% of total time. Within rebuild, layout (~47%) and render (~50%) are the main costs; DOM patching is <0.5%.
- First 4 events (toggles on initial render) are slightly slower (~91–96ms) due to full rebuild fallback. Subsequent events use incremental path (~80–85ms).
- Events that change element count (delete, add) cause size-changed → full repaint. Toggle/focus events use selective repaint.

### Bottleneck Distribution

```
Handler:      ████  <0.1%
Retransform:  ████  <0.1%
DOM patch:    ████  <0.5%
Layout:       ████████████████████████  ~47%
Render:       █████████████████████████ ~50%
```

**Conclusion:** Future optimization should target layout and render. The incremental DOM patching (Phase 12.1) successfully eliminated DOM rebuilds as a bottleneck (~0.3ms for subtree replacement vs ~40ms for full tree).

---

## Appendix B: Shared TypeElmt Mutation Bug (SIGSEGV Fix)

**Date:** 2026-04-12
**Symptom:** SIGSEGV in `item_keys()` → `get_type_id()` when clicking "clear completed" after 2+ delete operations on a multi-list todo app.

### Root Cause

`elmt_rebuild_with_new_shape()` in `mark_editor.cpp` mutated the `TypeElmt` **in-place** when an `edit` handler changed a field's type (e.g., `items` from `LMD_TYPE_ANY` → `LMD_TYPE_ARRAY`). The `TypeElmt` is shared across all Elements created from the same template (e.g., all 3 todo_list instances). Mutating the shared TypeElmt changed the field's byte size from 9 (TypedItem) to 8 (typed pointer) for ALL lists, but only the modified list's data buffer was rebuilt to match the new layout. Other lists' data buffers still used the original 9-byte TypedItem format, causing field reads to include the TypeId byte in the pointer value → invalid address → SIGSEGV.

### Fix

Always create a new `TypeElmt` when the shape changes, even in inline mode. This ensures the modified Element gets its own type descriptor while other Elements sharing the original type remain unaffected.

**File:** `lambda/mark_editor.cpp` — `elmt_rebuild_with_new_shape()`

### Minimal Reproduction

`test/ui/todo_two_delete_clear.json` — 2 deletes from one list + click clear-completed on another list.
