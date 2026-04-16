# Radiant JS Integration Phase 3: Interactive Event Handler Support

**Date**: 2026-04-17
**Prerequisite**: Phase 2 complete (365/365 CSS2.1 JS tests execute, 5222/5222 baseline pass)
**Goal**: Support all 36 CSS2.1 interactive tests via compiled JS event handler invocation, incremental reflow, and automated UI testing

---

## Table of Contents

1. [Objective](#1-objective)
2. [Interactive Test Survey](#2-interactive-test-survey)
3. [Architecture Overview](#3-architecture-overview)
4. [Phase 1: Retain MIR Compilation Context](#4-phase-1-retain-mir-compilation-context)
5. [Phase 2: Collect and Register Event Handlers](#5-phase-2-collect-and-register-event-handlers)
6. [Phase 3: Event Dispatch for HTML Handlers](#6-phase-3-event-dispatch-for-html-handlers)
7. [Phase 4: Post-Handler Pipeline — DOM Sync, Cascade, Layout](#7-phase-4-post-handler-pipeline--dom-sync-cascade-layout)
8. [Phase 5: Automated UI Testing with event_sim](#8-phase-5-automated-ui-testing-with-event_sim)
9. [Phase 6: Incremental Reflow and Repaint](#9-phase-6-incremental-reflow-and-repaint)
10. [Implementation Order](#10-implementation-order)
11. [Files Modified](#11-files-modified)
12. [Risk Assessment](#12-risk-assessment)
13. [Implementation Progress](#13-implementation-progress)

---

## 1. Objective

Phase 2 achieved 100% JS execution for all 365 CSS2.1 `<script>` tests. However, **36 tests** contain interactive-only event handlers (`onclick`, `onmouseover`, `onfocus`, etc.) whose JS code compiles and loads during page initialization but is never invoked — because Radiant has no mechanism to dispatch inline HTML event attributes at runtime.

This proposal adds:

1. **Persistent JS compilation context** — Retain the MIR context after initial page load so compiled JS functions can be called at event time.
2. **Event handler registry** — Map `(DomElement, event_type) → compiled_function_pointer` for all inline HTML event attributes.
3. **HTML event dispatch** — Hook into `handle_event()` to invoke registered JS handlers on click/hover/focus/keydown/etc.
4. **Post-handler rebuild pipeline** — After JS mutates the DOM (className toggle, style write, node removal), re-cascade CSS, relayout, and repaint.
5. **Automated UI tests** — JSON-driven event_sim test specs that simulate user interactions and assert layout outcomes.
6. **Incremental reflow** — Leverage existing DirtyTracker/ReflowScheduler infrastructure for targeted relayout.

### Comparison with Lambda Reactive UI

Lambda's reactive UI (`event.cpp:dispatch_lambda_handler`) follows this pattern:

```
click → hit-test → DomElement.native_element → reverse render_map lookup
      → find TemplateEntry + matching TemplateHandlerEntry
      → restore EvalContext (heap, nursery, name_pool from lambda_runtime)
      → handler_fn(source_item, event_item)
      → mark dirty → retransform → incremental DOM rebuild
```

HTML event handlers follow a simpler variant:

```
click → hit-test → DomElement
      → walk up DOM checking for onclick/onmouseover/etc. attributes
      → lookup compiled function by name in retained MIR context
      → restore EvalContext + DOM bridge context
      → call function (no model/event signature — just call the code)
      → detect DOM mutations → re-cascade CSS → relayout → repaint
```

The key difference: Lambda handlers receive `(model, event)` parameters and mutate template state. HTML handlers are imperative code snippets that directly mutate the DOM via the JS DOM bridge. There is no render map, no retransform step — DOM mutations happen in-place during handler execution.

---

## 2. Interactive Test Survey

### 2.1 Test Inventory (36 tests)

| Group | Count | Event | JS Pattern |
|-------|-------|-------|-----------|
| `text-transform-bicameral-*` | 22 | `onclick` | `setFontFamily()` — loops `getElementsByTagName('div')`, sets `div.style.fontFamily` |
| `block-in-inline-005/006` | 2 | `onclick` | `clicked()` — toggles `className` between `'inline'` and `'block'` |
| `content-105` | 1 | `onblur` | `PASS()` — sets `div.style.display = 'block'` via className |
| `content-107` | 1 | `onclick` | `PASS()` — sets `div.style.display = 'block'` via className |
| `content-109` | 1 | `onfocus` | `PASS()` — same pattern |
| `content-110` | 1 | `onkeydown` | `PASS()` — same pattern |
| `content-114` | 1 | `onmousedown` | `PASS()` — same pattern |
| `content-117` | 1 | `onmouseover` | `PASS()` — same pattern |
| `content-121` | 1 | `onsubmit` | `PASS()` — same pattern |
| `dom-hover-001/002` | 2 | `onmouseover` | `setTimeout(remove, 1000)` — removes element from DOM |
| `floats-137` | 1 | `onclick` | `clearTimeout(timer); body.className = 'test'` — also has auto-toggle |
| `outline-no-relayout-001` | 1 | `onclick` | `changeWidth()` — sets `div.style.outlineWidth = '30px'` |
| `visibility-collapse-001` | 1 | `onclick` | `toggle('col2')` — toggles className to `'collapse'` (multiple buttons) |

### 2.2 Event Types Required

| Event Type | HTML Attribute | Tests Using It |
|------------|---------------|----------------|
| `click` | `onclick` | 27 (22 bicameral + block-in-inline + content-107 + floats-137 + outline + visibility) |
| `mouseover` | `onmouseover` | 3 (content-117, dom-hover-001/002) |
| `mousedown` | `onmousedown` | 1 (content-114) |
| `focus` | `onfocus` | 1 (content-109) |
| `blur` | `onblur` | 1 (content-105) |
| `keydown` | `onkeydown` | 1 (content-110) |
| `submit` | `onsubmit` | 1 (content-121) |

### 2.3 JS DOM APIs Used by Interactive Tests

All APIs are already implemented in `js_dom.cpp`:

| API | Usage |
|-----|-------|
| `document.getElementsByTagName()` | Batch element lookup (bicameral tests) |
| `document.getElementById()` | Single element lookup |
| `element.className = value` | Class toggle (most common pattern) |
| `element.style.fontFamily = value` | Style property write (bicameral) |
| `element.style.outlineWidth = value` | Style property write (outline test) |
| `node.parentNode.removeChild(node)` | DOM removal (dom-hover tests) |
| `document.body.className = value` | Body class toggle (floats-137) |

### 2.4 Handler Code Patterns

**Pattern A — Named function call**: `onclick="clicked()"`, `onclick="setFontFamily()"`, `onclick="toggle('col2')"`
→ The function is defined in a `<script>` block. The `onclick` attribute is a call expression.

**Pattern B — Inline code**: `onclick="clearTimeout(timer); body.className = 'test';"`
→ Multiple statements directly in the attribute. Must be wrapped in a callable function.

**Pattern C — Simple function ref**: `onmouseover="setTimeout(remove, 1000)"`
→ `remove` is a function reference defined in `<script>`. `setTimeout` stub calls it immediately.

All patterns reduce to: **compile the attribute value as a function body, then invoke it at event time.**

---

## 3. Architecture Overview

```
                    Page Load (existing)
                    ═══════════════════
                    HTML parse → Element* tree
                    ↓
                    build_dom_tree_from_element → DomElement* tree
                    ↓
                    execute_document_scripts   ← compiles ALL <script> + onload JS
                    ↓                            (functions like clicked(), toggle() compiled here)
                    ↓
            ┌───────┴──────────────────────────────────────────┐
            │  NEW: Retain MIR context + Runtime on DomDocument │
            │  NEW: Collect onclick/onmouseover/etc. attributes │
            │  NEW: Build event handler registry                │
            └───────┬──────────────────────────────────────────┘
                    ↓
                    CSS cascade → layout → render

                    Event Time (new)
                    ════════════════
                    User click/hover/keydown
                    ↓
                    handle_event → hit-test → target DomElement
                    ↓
            ┌───────┴──────────────────────────────────────────┐
            │  NEW: Walk DOM ancestry checking event attributes │
            │  NEW: Lookup compiled handler function             │
            │  NEW: Restore EvalContext from retained Runtime    │
            │  NEW: Invoke handler via MIR function pointer      │
            └───────┬──────────────────────────────────────────┘
                    ↓
                    JS handler executes (DOM mutations happen in-place)
                    ↓
            ┌───────┴──────────────────────────────────────────┐
            │  NEW: Detect DOM dirty flag                       │
            │  NEW: Re-cascade CSS on affected subtrees         │
            │  NEW: Relayout (full or incremental)              │
            │  NEW: Repaint                                     │
            └──────────────────────────────────────────────────┘
```

---

## 4. Phase 1: Retain MIR Compilation Context

### Problem

Currently, `execute_document_scripts()` in `script_runner.cpp` calls `transpile_js_to_mir()` which creates a MIR context, compiles all JS, executes `js_main()`, and **destroys the MIR context** before returning. After this, compiled function pointers (e.g., `clicked()`, `toggle()`, `setFontFamily()`) are gone — the native code pages are freed.

### Solution

Use the **preamble mode** infrastructure that already exists:

```c
// In transpiler.hpp:
Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source,
                                   const char* filename, JsPreambleState* out_state);
```

`JsPreambleState` retains:
- `mir_ctx` — the MIR compilation context (keeps native code pages alive)
- `tp_ast_pool` — transpiler AST pool
- `tp_name_pool` — name pool for string interning

### Changes to `script_runner.cpp`

```c
// Before (normal mode — context destroyed):
Item result = transpile_js_to_mir(&runtime, script_buf->str, "<document-scripts>");

// After (preamble mode — context retained):
JsPreambleState js_state = {};
Item result = transpile_js_to_mir_preamble(&runtime, script_buf->str,
                                            "<document-scripts>", &js_state);

// Store retained state on DomDocument for later event dispatch
dom_doc->js_mir_ctx = js_state.mir_ctx;
dom_doc->js_runtime = /* copy relevant Runtime fields */;
dom_doc->js_preamble = js_state;
```

### New Fields on DomDocument

```c
// In dom_node.hpp or wherever DomDocument is defined:
struct DomDocument {
    // ... existing fields ...

    // Phase 3: Retained JS compilation state
    void* js_mir_ctx;           // MIR_context_t — keeps compiled code alive
    void* js_runtime_heap;      // Heap* — GC heap for JS objects
    void* js_runtime_nursery;   // gc_nursery_t* — nursery allocator
    void* js_runtime_name_pool; // NamePool* — string interning
    void* js_preamble_state;    // JsPreambleState* — full preamble for cleanup
    bool js_context_valid;      // true if MIR context is usable
};
```

### Lifecycle

- **Created**: During `execute_document_scripts()` (page load)
- **Used**: During event dispatch (click/hover/etc.)
- **Destroyed**: During document teardown (`dom_document_destroy()`)
  - Calls `preamble_state_destroy()` to free MIR context + pools

### Alternative: Compile Handlers Separately

Instead of retaining the entire initial compilation context, we could compile each event handler as a separate mini-program at event time. This is simpler but has two problems:
1. **Performance**: MIR compilation is ~1-5ms per compile. On every click, we'd re-compile.
2. **Scope**: The handler code (`clicked()`) references functions defined in `<script>` blocks. A separate compilation unit won't see them.

The preamble approach avoids both issues — all functions are compiled once during page load, and the context stays alive.

---

## 5. Phase 2: Collect and Register Event Handlers

### Problem

After page load, we need to know which DomElements have event handler attributes and what functions to call when events fire.

### Handler Collection

During or after `execute_document_scripts()`, walk the DomElement tree and collect event handler attributes:

```c
// Event handler attribute names to scan for
static const char* EVENT_ATTR_NAMES[] = {
    "onclick", "ondblclick", "onmousedown", "onmouseup", "onmouseover",
    "onmouseout", "onmousemove", "onkeydown", "onkeyup", "onkeypress",
    "onfocus", "onblur", "onchange", "oninput", "onsubmit", "onreset",
    "onload", "onresize", "onscroll",
    NULL
};
```

### Handler Registry

```c
typedef struct JsEventHandler {
    DomElement* element;           // target element
    const char* event_type;        // "click", "mouseover", etc. (without "on" prefix)
    const char* handler_source;    // original attribute value: "clicked()" or "x=1; foo()"
    void* compiled_func;           // fn_ptr after MIR compilation (NULL = not yet compiled)
    struct JsEventHandler* next;   // linked list per element
} JsEventHandler;

typedef struct JsEventRegistry {
    struct hashmap* element_map;   // HashMap<DomElement* → JsEventHandler*> (linked list)
    int count;                     // total registered handlers
} JsEventRegistry;
```

Store on DomDocument:
```c
dom_doc->js_event_registry = registry;
```

### Handler Compilation Strategy

**Option A — Compile at registration time (recommended)**:
During `collect_event_handlers()`, for each handler attribute:
1. Parse the attribute value (e.g., `"clicked()"` or `"clearTimeout(timer); body.className = 'test';"`)
2. Wrap it as a function: `function __handler_N() { <attribute_value> }`
3. Compile via `transpile_js_to_mir_with_preamble()` using the retained preamble state
4. Retrieve function pointer via `find_func(mir_ctx, "__handler_N")`
5. Store `compiled_func` on the registry entry

This compiles all handlers at page load time. Event dispatch is then pure function-pointer invocation — zero compilation overhead.

**Option B — Compile on first invocation (lazy)**:
Store only the source text. On first event, compile and cache. Simpler but adds latency to first interaction.

**Recommendation**: Option A. The 36 CSS2.1 tests have at most 10 unique handlers per page. Compiling them all during page load adds negligible time (<5ms total).

### Handling Handler Attributes with Arguments

Some attributes contain function calls with arguments: `onclick="toggle('col2')"`.

The wrapper function approach handles this naturally:
```javascript
function __handler_0() { toggle('col2'); }
```

The inner `toggle` function was already compiled from the `<script>` block and is accessible via the retained MIR scope.

### New Function Signatures

```c
// Collect event handlers from DOM tree and compile them
// Called after execute_document_scripts(), using retained MIR context
JsEventRegistry* collect_event_handlers(DomElement* root, DomDocument* doc);

// Look up handler for a specific element and event type
JsEventHandler* find_event_handler(JsEventRegistry* registry,
                                    DomElement* element,
                                    const char* event_type);

// Free the registry
void event_registry_destroy(JsEventRegistry* registry);
```

---

## 6. Phase 3: Event Dispatch for HTML Handlers

### Integration Point

In `handle_event()` (`event.cpp`), after the existing Lambda template handler dispatch, add HTML event handler dispatch:

```c
// In handle_event(), after mouse_up for click events:
case RDT_EVENT_MOUSE_UP: {
    // ... existing focus, caret, checkbox, dropdown logic ...

    // Try Lambda template handler first (reactive UI)
    bool handled = dispatch_lambda_handler(&evcon, target, "click");

    // If no template handler, try HTML inline event handler
    if (!handled) {
        handled = dispatch_html_event_handler(&evcon, target, "click");
    }
    break;
}
```

### dispatch_html_event_handler

```c
static bool dispatch_html_event_handler(EventContext* evcon, View* target,
                                         const char* event_type) {
    DomDocument* doc = evcon->ui_context->document;
    if (!doc || !doc->js_event_registry || !doc->js_mir_ctx) {
        return false;
    }

    // Walk up from target DomElement, checking each for a handler
    DomNode* node = (DomNode*)target;
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* elem = (DomElement*)node;
            JsEventHandler* handler = find_event_handler(
                doc->js_event_registry, elem, event_type);

            if (handler && handler->compiled_func) {
                // 1. Restore EvalContext from retained JS runtime state
                EvalContext handler_ctx = {};
                Heap* heap = (Heap*)doc->js_runtime_heap;
                handler_ctx.heap = heap;
                handler_ctx.nursery = (gc_nursery_t*)doc->js_runtime_nursery;
                handler_ctx.name_pool = (NamePool*)doc->js_runtime_name_pool;
                handler_ctx.pool = heap ? heap->pool : nullptr;

                EvalContext* saved_ctx = context;
                Context* saved_input_ctx = input_context;
                context = &handler_ctx;
                input_context = nullptr;

                // 2. Restore JS DOM bridge context
                js_dom_set_document(doc);

                // 3. Invoke the compiled handler function
                //    Signature: Item handler_fn(void) — no parameters
                typedef Item (*js_handler_fn)(void);
                js_handler_fn fn = (js_handler_fn)handler->compiled_func;

                auto t_start = high_resolution_clock::now();
                fn();
                auto t_handler = high_resolution_clock::now();

                // 4. Restore context
                context = saved_ctx;
                input_context = saved_input_ctx;

                // 5. Post-handler: detect and apply DOM changes
                post_handler_rebuild(evcon, t_start, t_handler);

                return true;
            }
        }

        // Bubble up: check parent for handler (event bubbling)
        node = node->parent;
    }
    return false;
}
```

### Event Bubbling

HTML events bubble from target to document root. The handler walk above naturally implements this: check the target element first, then parent, then grandparent, etc. The first matching handler wins (no `stopPropagation()` support needed for CSS2.1 tests).

### Event Type Mapping

Map Radiant events to HTML event types:

| Radiant Event | Trigger Condition | HTML Event Types |
|---------------|-------------------|-----------------|
| `RDT_EVENT_MOUSE_UP` | After mouse_down on same element | `click`, `mouseup` |
| `RDT_EVENT_MOUSE_DOWN` | On press | `mousedown` |
| `RDT_EVENT_MOUSE_MOVE` | Mouse enters element bounds | `mouseover`, `mousemove` |
| `RDT_EVENT_KEY_DOWN` | Keyboard key pressed | `keydown`, `keypress` |
| `RDT_EVENT_KEY_UP` | Keyboard key released | `keyup` |
| Focus change | Element gains focus via click/tab | `focus` |
| Focus change | Element loses focus | `blur` |
| Form submit | Submit button clicked | `submit` |

---

## 7. Phase 4: Post-Handler Pipeline — DOM Sync, Cascade, Layout

### Problem

After a JS handler executes, the DOM may have changed:
- `element.className = 'done'` — class changed, CSS rules may match differently
- `element.style.fontFamily = 'serif'` — inline style changed
- `parent.removeChild(child)` — DOM structure changed

We need to detect these changes and update the layout.

### DOM Mutation Tracking

Add a dirty flag to DomDocument that the JS DOM bridge sets on any mutation:

```c
// In dom_node.hpp:
struct DomDocument {
    // ... existing ...
    bool js_dom_dirty;            // set to true by js_dom.cpp on any DOM mutation
    int js_mutation_count;        // count of mutations since last reset
};
```

In `js_dom.cpp`, every mutating function sets the flag:
```c
// In js_dom_set_property() for className:
doc->js_dom_dirty = true;
doc->js_mutation_count++;

// In js_dom_set_style_property():
doc->js_dom_dirty = true;
doc->js_mutation_count++;

// In js_dom_element_method() for appendChild/removeChild/insertBefore:
doc->js_dom_dirty = true;
doc->js_mutation_count++;
```

### Post-Handler Rebuild

```c
static void post_handler_rebuild(EventContext* evcon,
                                  chrono_time_point t_start,
                                  chrono_time_point t_handler) {
    DomDocument* doc = evcon->ui_context->document;
    if (!doc->js_dom_dirty) {
        log_info("[TIMING] html event handler: %.2fms (no DOM changes)",
                 ms(t_handler - t_start));
        return;
    }

    auto t0 = high_resolution_clock::now();

    // 1. Re-scan <style> elements (in case JS added/removed/disabled styles)
    collect_inline_styles_from_dom(doc);

    // 2. Re-cascade CSS on the full tree
    //    (Phase 6 can optimize this to affected subtrees)
    re_cascade_styles(doc);

    auto t1 = high_resolution_clock::now();

    // 3. Clear view tree to force full relayout
    //    (Phase 6 can optimize to partial relayout)
    doc->view_tree = nullptr;
    layout_html_doc(evcon->ui_context, doc, false);

    auto t2 = high_resolution_clock::now();

    // 4. Repaint
    render_html_doc(evcon->ui_context, doc->view_tree, NULL);

    auto t3 = high_resolution_clock::now();

    log_info("[TIMING] html handler pipeline: handler=%.2fms cascade=%.2fms "
             "layout=%.2fms render=%.2fms total=%.2fms (mutations=%d)",
             ms(t_handler - t_start), ms(t1 - t0),
             ms(t2 - t1), ms(t3 - t2), ms(t3 - t_start),
             doc->js_mutation_count);

    // Reset dirty state
    doc->js_dom_dirty = false;
    doc->js_mutation_count = 0;
}
```

### re_cascade_styles

Re-apply the stylesheet cascade to the DOM tree:

```c
static void re_cascade_styles(DomDocument* doc) {
    // Reuse existing stylesheet application from load_lambda_html_doc:
    // 1. External stylesheets (if any)
    // 2. Inline <style> stylesheets (from doc->stylesheets)
    // 3. Inline style="" attributes
    SelectorMatcher matcher = {};
    for (int i = 0; i < doc->stylesheet_count; i++) {
        apply_stylesheet_to_dom_tree_fast(doc->root, doc->stylesheets[i],
                                           &matcher, doc->pool, doc->engine);
    }
    apply_inline_styles_to_tree(doc->root);
}
```

This is a full re-cascade. Phase 6 optimizes it to subtree-only.

---

## 8. Phase 5: Automated UI Testing with event_sim

### Existing Framework

The `event_sim` system (`event_sim.hpp/cpp`) supports:
- **32 event types**: click, dblclick, mouse_move/down/up, key_press/down/up, focus, type, scroll, resize, drag_and_drop, navigate, switch_frame
- **15 assertion types**: assert_text, assert_rect, assert_style, assert_value, assert_checked, assert_visible, assert_focus, assert_state, assert_position, assert_element_at, assert_attribute, assert_count, assert_caret, assert_selection, assert_scroll
- **Target system**: CSS selectors (`#id`, `.class`, `tag`) or text content search
- **Auto-wait**: assert_timeout + assert_interval for eventual consistency

### Test Spec Format for Interactive Tests

Each CSS2.1 interactive test gets a companion JSON test spec in `test/layout/js_interactive/`:

**Example: `content-107.json`** (click triggers PASS)
```json
{
  "name": "content-107: onclick triggers PASS",
  "document": "test/layout/css21/content-107.htm",
  "events": [
    {"type": "click", "target": {"selector": "body"}},
    {"type": "assert_text", "target": {"selector": ".PASS"}, "contains": "PASS",
     "assert_timeout": 500}
  ]
}
```

**Example: `block-in-inline-005.json`** (click toggles className)
```json
{
  "name": "block-in-inline-005: onclick toggles display",
  "document": "test/layout/css21/block-in-inline-005.htm",
  "events": [
    {"type": "click", "target": {"selector": "#toggle"}},
    {"type": "assert_style", "target": {"selector": "#target"},
     "property": "display", "equals": "block"},
    {"type": "click", "target": {"selector": "#toggle"}},
    {"type": "assert_style", "target": {"selector": "#target"},
     "property": "display", "equals": "inline"}
  ]
}
```

**Example: `text-transform-bicameral-001.json`** (font family change)
```json
{
  "name": "text-transform-bicameral-001: setFontFamily on click",
  "document": "test/layout/css21/text-transform-bicameral-001.htm",
  "events": [
    {"type": "click", "target": {"selector": "input[type=button]"}},
    {"type": "assert_style", "target": {"selector": "div"},
     "property": "font-family", "equals": "serif",
     "assert_timeout": 500}
  ]
}
```

**Example: `dom-hover-001.json`** (mouseover removes element)
```json
{
  "name": "dom-hover-001: mouseover removes element",
  "document": "test/layout/css21/dom-hover-001.htm",
  "events": [
    {"type": "mouse_move", "target": {"selector": "#trigger"}},
    {"type": "assert_count", "target": {"selector": "#trigger"},
     "assert_count_expected": 0,
     "assert_timeout": 2000}
  ]
}
```

**Example: `visibility-collapse-001.json`** (multiple buttons toggle columns)
```json
{
  "name": "visibility-collapse-001: toggle column visibility",
  "document": "test/layout/css21/visibility-collapse-001.htm",
  "events": [
    {"type": "click", "target": {"selector": "#toggle-col2"}},
    {"type": "assert_style", "target": {"selector": "#col2"},
     "property": "visibility", "equals": "collapse",
     "assert_timeout": 500},
    {"type": "click", "target": {"selector": "#toggle-col2"}},
    {"type": "assert_style", "target": {"selector": "#col2"},
     "property": "visibility", "equals": "visible"}
  ]
}
```

### Running Tests

```bash
# Run all interactive JS tests
make layout suite=js_interactive

# Run a specific interactive test
make layout test=content-107 suite=js_interactive
```

Add a new Makefile target that:
1. Finds all `.json` specs in `test/layout/js_interactive/`
2. For each spec: `./lambda.exe view <document> --event-sim <spec.json> --headless`
3. Reports pass/fail counts

### Headless Mode for CI

The event_sim system already runs in the Radiant viewer window. For CI testing:
- `--headless` flag renders to an offscreen framebuffer (ThorVG canvas)
- `--event-sim <file.json>` loads and auto-plays the test spec
- `--auto-close` exits after all events complete
- Exit code: 0 if all assertions pass, 1 otherwise

---

## 9. Phase 6: Incremental Reflow and Repaint

### Existing Infrastructure

Radiant already has incremental reflow building blocks from the Lambda Reactive UI work (Reactive_UI3.md):

| Component | File | Status |
|-----------|------|--------|
| `DirtyTracker` | `state_store.hpp` | ✅ Tracks dirty rects (damage regions) |
| `ReflowScheduler` | `state_store.hpp` | ✅ Queues subtree reflow requests |
| `ReflowScope` | `state_store.hpp` | ✅ SELF_ONLY, SUBTREE, ANCESTORS, FULL |
| Layout cache | `layout_cache.hpp` | ✅ Per-element measurement cache |
| `element_dom_map` | `cmd_layout.cpp` | ✅ `HashMap<Element* → DomElement*>` for O(1) lookup |
| `rebuild_lambda_doc_incremental()` | `cmd_layout.cpp` | ✅ Subtree DOM splice for Lambda reactive UI |
| `sync_pseudo_state()` | `event.cpp` | ✅ Schedules reflow on pseudo-state change |

### Phase 6 Pipeline

Replace the full rebuild in `post_handler_rebuild()` with targeted updates:

```
post_handler_rebuild:
  1. Check js_dom_dirty flag
  2. Categorize mutations:
     a. Style-only (className, style.property) → re-cascade affected elements only
     b. Structure change (appendChild, removeChild) → rebuild affected subtree
  3. Re-cascade CSS on dirty subtrees only
  4. Schedule REFLOW_SUBTREE for affected parent views
  5. Execute partial relayout via ReflowScheduler
  6. Compute dirty rects from old vs new bounds
  7. Repaint only damaged regions
```

### Mutation Categories

| Mutation Type | Example | Cost |
|---------------|---------|------|
| Class change | `elem.className = 'done'` | Re-cascade element + descendants, relayout if box model changes |
| Style write | `elem.style.fontFamily = 'serif'` | Update inline style, relayout if layout-affecting |
| Node removal | `parent.removeChild(child)` | Remove subtree from view tree, relayout parent |
| Node insertion | `parent.appendChild(child)` | Build new subtree, insert into view tree, relayout parent |

### Dirty-Aware CSS Re-Cascade

Instead of re-cascading the entire tree, track which elements were mutated:

```c
// In js_dom.cpp, record mutated elements (not just a bool flag):
struct JsDomMutation {
    DomElement* element;
    int type;  // MUTATION_CLASS, MUTATION_STYLE, MUTATION_STRUCTURE
};

// After handler execution, iterate mutations:
for (int i = 0; i < mutation_count; i++) {
    DomElement* elem = mutations[i].element;
    if (mutations[i].type == MUTATION_CLASS || mutations[i].type == MUTATION_STRUCTURE) {
        // Re-cascade this element and descendants
        apply_stylesheet_to_dom_tree_fast(elem, ...);
        apply_inline_styles_to_tree(elem);
    } else if (mutations[i].type == MUTATION_STYLE) {
        // Only re-parse inline style
        apply_inline_style_single(elem);
    }
}
```

### Subtree Relayout

```c
// After CSS re-cascade, schedule affected views for relayout:
for (int i = 0; i < mutation_count; i++) {
    View* view = find_view_for_dom_element(mutations[i].element);
    if (view) {
        reflow_schedule(state, view, REFLOW_SUBTREE, CHANGE_CONTENT);
    }
}

// Execute scheduled reflows:
layout_scheduled_reflows(uicon, doc);
```

### Expected Performance

For the CSS2.1 interactive tests (typically <100 elements, 1-2 mutations per event):

| Step | Full Rebuild | Incremental |
|------|-------------|-------------|
| CSS cascade | O(rules × elements) | O(rules × subtree) |
| Layout | O(elements) | O(subtree + ancestors) |
| Render | O(elements) | O(dirty_area / total_area × elements) |
| **Total estimate** | ~5-15ms | ~1-3ms |

For the CSS2.1 tests, full rebuild is fast enough (<20ms). Incremental optimization is more important for real-world pages with 1000+ elements.

---

## 10. Implementation Order

| Phase | Description | Dependency | Estimated Scope | Status |
|-------|-------------|------------|-----------------|--------|
| **Phase 1** | Retain MIR context | None | `script_runner.cpp`, `dom_node.hpp` | ✅ Done |
| **Phase 2** | Event handler collection + compilation | Phase 1 | New `event_handler_registry.cpp/.h` | ✅ Done |
| **Phase 3** | Event dispatch in `handle_event` | Phase 2 | `event.cpp` | ✅ Done |
| **Phase 4** | Post-handler rebuild pipeline | Phase 3 | `cmd_layout.cpp`, `js_dom.cpp` | ✅ Done |
| **Phase 5** | Automated UI test specs | Phase 4 | `test/ui/js_*.json`, `test/ui/js_*.html` | ✅ Done |
| **Phase 6** | Incremental reflow (optimization) | Phase 4 | `cmd_layout.cpp`, `event.cpp`, `js_dom.cpp` | Not started |

Phases 1–4 form the minimum viable feature. Phase 5 adds test coverage. Phase 6 is optimization.

**Critical path**: Phase 1 → 2 → 3 → 4 (serial — each depends on the previous).
**Phase 5** can be developed in parallel with Phase 4 (write test specs while building the dispatch).

---

## 11. Files Modified

| File | Changes | Phase |
|------|---------|-------|
| `lambda/runner.cpp` | Fixed `runtime_cleanup()` and `runtime_reset_heap()` — moved `name_pool_release()` before `heap_destroy()` to fix use-after-free (NamePool struct is pool_calloc'd from heap's Pool) | Bug fix |
| `lambda/js/transpile_js_mir.cpp` | Restored accidentally deleted lines (`_lambda_rt`, `js_input`, `js_runtime_set_input`) | Bug fix |
| `radiant/script_runner.cpp` | Use preamble mode; retain MIR ctx + Runtime on DomDocument | Phase 1 |
| `radiant/dom_node.hpp` | New fields on DomDocument: js_mir_ctx, js_runtime_*, js_event_registry | Phase 1 |
| `radiant/event.cpp` | New: `dispatch_html_event_handler()`; hook into `handle_event()` for 6 event types (click, mousedown, mouseover, focus, blur, keydown) | Phase 2–3 |
| `lambda/js/js_dom.cpp` | Set `js_dom_dirty` / `js_mutation_count` on mutations | Phase 4 |
| `test/ui/js_*.html` | 12 new HTML test documents | Phase 5 |
| `test/ui/js_*.json` | 12 new JSON event_sim test specs (+ 2 existing block-in-inline) | Phase 5 |

---

## 12. Risk Assessment

### Low Risk
- **DOM APIs already complete**: All JS DOM APIs used by the 36 interactive tests are already implemented in `js_dom.cpp`.
- **Event_sim framework mature**: The UI automation system supports all needed event types and assertions.
- **MIR preamble mode tested**: Already used for Lambda hot-reload and batch testing.

### Medium Risk
- **MIR context memory lifetime**: Retaining the MIR context keeps ~1-5MB of native code pages alive for the document lifetime. Acceptable for single-document layout testing; may need cleanup-on-navigate for multi-page browsing.
- **Handler compilation scope**: Event handler attribute code (e.g., `toggle('col2')`) must resolve functions compiled in the main script body. The preamble approach should handle this (same MIR context), but needs verification.
- **GC interaction**: The retained Heap/nursery must not be collected between events. Currently `script_runner_cleanup_heap()` only runs at document teardown, so this should be safe.

### Higher Risk
- **`setTimeout` in event handlers**: Some tests use `setTimeout(fn, delay)` in onclick handlers (e.g., `dom-hover-001`). The current stub calls `fn()` immediately, which is correct for most cases. But if a handler depends on actual timing (execute after repaint), the immediate call may produce different results than a real browser.
- **Event handler re-compilation after DOM mutation**: If JS creates new elements with event handlers (e.g., `elem.setAttribute('onclick', 'foo()')`), those need to be registered after the mutation. Phase 4's DOM dirty tracking doesn't cover this. Mitigation: re-scan event handlers after mutations if needed. Not required for the 36 CSS2.1 tests.
- **Re-entrant handler invocation**: If a handler triggers a DOM change that fires another handler (e.g., `focus` triggers `blur` on another element), we need to prevent infinite loops. Mitigation: set a `dispatching_event` flag and skip nested dispatch, or limit recursion depth.

---

## 13. Implementation Progress

### Phase 1–4: Core Pipeline — ✅ Complete

All four core phases are implemented and verified:

- **Phase 1**: MIR compilation context retained on DomDocument via preamble mode. JS runtime (heap, nursery, name_pool) persists across events.
- **Phase 2**: 17 HTML event handler attributes are scanned and compiled at page load: `onclick`, `ondblclick`, `onmousedown`, `onmouseup`, `onmouseover`, `onmouseout`, `onmousemove`, `onkeydown`, `onkeyup`, `onkeypress`, `onfocus`, `onblur`, `onchange`, `oninput`, `onsubmit`, `onreset`, `onscroll`.
- **Phase 3**: `dispatch_html_event_handler()` hooks into `handle_event()` for 6 dispatched event types: click (line 3140), mousedown (line 2728), mouseover (line 1133), focus (line 1904), blur (lines 1892/1922), keydown (line 3396) in `event.cpp`.
- **Phase 4**: Post-handler pipeline detects `js_mutation_count`, re-cascades CSS, destroys/rebuilds view tree, and repaints.

**Lambda baseline**: 588/588 PASS (was 583/588 before Bug 5 fix).

### Bug 5: NamePool Use-After-Free — ✅ Fixed

**Root cause**: `runtime_cleanup()` and `runtime_reset_heap()` in `lambda/runner.cpp` called `name_pool_release()` **after** `heap_destroy()`. The NamePool struct was allocated via `pool_calloc()` from the heap's Pool, and `pool_destroy()` (inside `gc_heap_destroy()`) bulk-freed all pool memory first — leaving a dangling pointer.

**Fix**: Moved `name_pool_release()` to execute **before** `heap_destroy()` in both functions.

**Secondary fix**: Restored accidentally deleted lines in `lambda/js/transpile_js_mir.cpp` (`_lambda_rt = (Context*)context`, `Input* js_input = Input::create(context->pool)`, `js_runtime_set_input(js_input)`) that were removed when cleaning up debug logging.

### Phase 5: Automated UI Tests — ✅ Complete

12 new automated UI tests created in `test/ui/`, covering all 6 dispatched event types with various DOM mutation patterns. All tests pass in the UI automation test runner (`test_ui_automation_gtest.exe`).

**Test inventory (12 new + 2 existing = 14 JS tests)**:

| Test | Event Type | Assertions | DOM Pattern |
|------|-----------|------------|-------------|
| `js_click_classname` | onclick | 5 | className toggle (inactive↔active) |
| `js_click_style` | onclick | 4 | inline style.width, style.backgroundColor |
| `js_click_dom_mutate` | onclick | 8 | appendChild, removeChild |
| `js_click_batch_style` | onclick | 6 | getElementsByTagName batch style update |
| `js_click_textcontent` | onclick | 6 | textContent mutation, counter |
| `js_click_display_toggle` | onclick | 6 | display:none toggle via className |
| `js_click_bubbling` | onclick | 4 | event bubbling from child to parent |
| `js_mouseover` | onmouseover | 4 | hover triggers className + textContent |
| `js_mousedown` | onmousedown | 4 | press handler with counter |
| `js_focus_blur` | onfocus | 4 | focus sets className and log text |
| `js_keydown` | onkeydown | 4 | keyboard handler on focusable element |
| `js_multi_event` | mouseover+click | 5 | combined event pipeline |
| `js_block_in_inline_005` | onclick | 3 | className toggle (existing) |
| `js_block_in_inline_006` | onclick | 3 | className toggle (existing) |

**Total**: 66 assertions across all 14 tests. **All 14 pass.**

**Full UI automation suite**: 61/61 tests pass (no regressions).

### Test Observations

- **Empty class attribute**: The HTML parser / DomElement does not store `class=""` — reading an unset `class` attribute returns `(null)`, not empty string. Tests use non-empty initial class values (e.g., `class="inactive"`, `class="idle"`) to avoid this mismatch.
- **Computed style values**: `assert_style` returns computed CSS values, not authored values. Colors return `rgb(R, G, B)` format (e.g., `rgb(255, 0, 0)` not `red`). Dimensions include padding unless `box-sizing: border-box` is set.
- **Focus/blur dispatch**: The `focus` event_sim type is dispatched via simulated click. Implicit blur (clicking another element triggers blur on the previously focused element) does not fire `onblur` handlers through the JS event handler dispatch path.

### Phase 6: Incremental Reflow — Not Started

Current post-handler rebuild uses full re-cascade + full relayout. This is adequate for the CSS2.1 interactive tests (<100 elements, <20ms per rebuild). Phase 6 optimization deferred until needed for larger documents.
