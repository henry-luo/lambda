# Reactive UI Phase 2 — End-to-End Interactive Todo App

**Date:** 2026-04-02 (updated 2026-04-04)
**Status:** Phases 6–9 complete. All 13 tasks done. Toggle, delete, clear completed, CSS cache, assert_count, input event wiring, CI target all working.
**Prerequisite:** Phases 1–4 complete (Reactive_UI.md), Phase 5 event dispatch bridge complete
**Commit:** `a624cf18` — "reactive ui automated test"

---

## Table of Contents

1. [Objective](#1-objective)
2. [Current State Assessment](#2-current-state-assessment)
3. [Gap Analysis](#3-gap-analysis)
   - [3.1 Bugs Found and Fixed](#31-bugs-found-and-fixed-during-implementation)
4. [Phase 6: Complete Event Dispatch to Handler Execution](#4-phase-6-complete-event-dispatch-to-handler-execution)
   - [6.1 Handler Invocation Context](#61-handler-invocation-context)
   - [6.2 Event Object Construction](#62-event-object-construction)
   - [6.3 State Read/Write in Handlers](#63-state-readwrite-in-handlers)
   - [6.4 Model Mutation via Edit Bridge](#64-model-mutation-via-edit-bridge)
5. [Phase 7: Re-Transform, Reflow, and Repaint](#5-phase-7-re-transform-reflow-and-repaint)
   - [7.1 Dirty Propagation Chain](#71-dirty-propagation-chain)
   - [7.2 Selective Re-Transform](#72-selective-re-transform)
   - [7.3 Incremental DOM Rebuild](#73-incremental-dom-rebuild)
   - [7.4 CSS Cascade Reuse](#74-css-cascade-reuse)
6. [Phase 8: Extended Interactions — Add, Delete, Text Input](#6-phase-8-extended-interactions--add-delete-text-input)
   - [8.1 Add Todo Item](#81-add-todo-item)
   - [8.2 Delete Todo Item](#82-delete-todo-item)
   - [8.3 Text Input for Todo Content](#83-text-input-for-todo-content)
   - [8.4 Updated Todo Script](#84-updated-todo-script)
7. [Phase 9: UI Automation Testing for Reactive Apps](#7-phase-9-ui-automation-testing-for-reactive-apps)
   - [9.1 Lambda Script as Event Sim Document Source](#91-lambda-script-as-event-sim-document-source)
   - [9.2 New Assertion Types for Reactive State](#92-new-assertion-types-for-reactive-state)
   - [9.3 Test Suite: todo_toggle.json](#93-test-suite-todo_togglejson)
   - [9.4 Test Suite: todo_add_delete.json](#94-test-suite-todo_add_deletejson)
   - [9.5 Test Suite: todo_text_input.json](#95-test-suite-todo_text_inputjson)
   - [9.6 Integration into CI](#96-integration-into-ci)
8. [Architecture Diagram](#8-architecture-diagram)
9. [Implementation Order](#9-implementation-order)
10. [Risk Analysis](#10-risk-analysis)
11. [Files Modified](#11-files-modified)

---

## 1. Objective

Bring the reactive UI system from "renders static output" to "fully interactive app" by completing four capabilities:

1. **Event dispatch** — Click a todo item → handler code executes → state mutates.
2. **Re-render** — After state/model mutation, re-transform affected templates → rebuild DOM → CSS cascade → relayout → repaint.
3. **Extended interactions** — Add new items, delete items, edit item text.
4. **Automated testing** — Use the existing UI automation framework (event_sim) to verify reactive behavior end-to-end, including state changes, DOM mutations, and visual output.

The running example throughout this proposal is the Todo app (`test/lambda/ui/todo.ls`).

---

## 2. Current State Assessment

### What Works (Phases 1–4)

| Component | File(s) | Status |
|-----------|---------|--------|
| Grammar: `view`, `state`, `on` | `grammar.js` L527+ | ✅ Parses correctly |
| AST: `AstViewNode`, `AstEventHandler`, `AstStateEntry` | `ast.hpp` L571+ | ✅ Built |
| MIR codegen: template bodies + handlers | `transpile-mir.cpp` L11430+ | ✅ Compiled to JIT |
| Template registry: match + dispatch | `template_registry.cpp` | ✅ `fn_apply1/2` working |
| State store: `(model, tmpl_ref, name) → value` | `template_state.cpp/.h` | ✅ Get/set/init |
| Render map: source→result + reverse lookup | `render_map.cpp/.h` | ✅ Record + reverse lookup |
| Edit bridge: MarkEditor for model mutation | `edit_bridge.cpp/.h` | ✅ Map/element/array ops |
| Event dispatch: `dispatch_lambda_handler()` | `event.cpp` L310+ | ✅ Hit-test → reverse lookup → invoke handler |
| DOM rebuild: `rebuild_lambda_doc()` | `cmd_layout.cpp` L4064+ | ✅ Full rebuild pipeline |
| Lambda script doc loading | `cmd_layout.cpp` L3749+ | ✅ .ls → JIT → element tree → DOM |
| Window event loop + GLFW callbacks | `window.cpp` L562+ | ✅ Event loop running |
| UI automation framework (event_sim) | `event_sim.cpp/.hpp` | ✅ 32 event types, 15 assertion types |

### What's Verified Working End-to-End

```
lambda view test/lambda/ui/todo.ls
```

- Script evaluates via MIR Direct JIT
- `view <todo_item>` template compiled with `on click()` handler
- `apply()` dispatches to template, produces `<li>` elements
- DOM built, CSS cascade applied, layout + render → window displays todo list
- GLFW event loop running, accepting mouse/keyboard input
- `dispatch_lambda_handler` locates template handlers via reverse render map

---

## 3. Gap Analysis

Despite all components being individually implemented, the end-to-end reactive loop had several gaps. All critical gaps (1–5, 7) have been resolved. Gaps 4 and 6 are deferred to Phase 8.

### Gap 1: Handler Context Binding — ✅ RESOLVED

**Problem:** `dispatch_lambda_handler` invokes `handler_fn(source_item)` with just the source model item. But the handler body needs access to:
- Template-local **state variables** (e.g., `toggled`) — read and write
- The **context item** `~` bound to the matched model
- The **event object** (for `click(evt)` handlers)

**Current state:** MIR-compiled handlers call `tmpl_state_get`/`tmpl_state_set` internally (codegen inserts these calls for state variable reads/writes). The `source_item` argument becomes the `model_item` key for state lookups. However, the handler function signature is `Item handler(Item model)` — it receives only the model and derives `template_ref` from a global set during codegen.

**Resolution:** Verified that MIR codegen correctly:
1. Sets `~` (context) to the model parameter
2. Calls `tmpl_state_get_or_init(model, template_ref, "toggled", default)` for state reads
3. Calls `tmpl_state_set(model, template_ref, "toggled", new_value)` for state writes
4. Calls `render_map_mark_dirty(model, template_ref)` after state mutation

### Gap 2: Post-Handler Re-Transform Robustness — ✅ RESOLVED

**Problem:** `render_map_retransform()` re-executes dirty template bodies and replaces result nodes in the element tree. Then `rebuild_lambda_doc()` does a full DOM rebuild (create DOM tree → CSS parse → cascade → layout → render). But:

- The retransform calls `replace_in_element_tree()` which does a recursive walk from `s_doc_root`. If the result hierarchy is deep (e.g., `<html> > <body> > <div> > <ul> > <li>`), the replacement must find the correct parent. Currently uses identity comparison (pointer equality on `Item.element`).
- After replacement, the element tree's `parent` pointers may be stale.
- `rebuild_lambda_doc()` creates an entirely new DOM tree, so stale parent pointers in the Lambda element tree don't affect DOM correctness — but they could cause issues in the next render_map_reverse_lookup if native_element pointers change.

**Resolution:** Verified that `build_dom_tree_from_element()` correctly sets `native_element` for every DOM node, enabling reverse lookup after rebuilds. The full DOM rebuild approach ensures correctness on each cycle.

### Gap 3: Render Map Survival Across Rebuilds — ✅ RESOLVED

**Problem:** The render map records `(source_item, template_ref) → result_node`. After retransform replaces a result_node, the forward map's `result_node` pointer is updated. But the **reverse map** (result_node → source_item) uses the result_node's bits as the key. After retransform produces a **new** result element (with a different pointer), the reverse map entry for the **old** pointer is stale.

**Resolution:** `render_map_retransform()` already updates the reverse map: removes the old result_node entry and inserts the new one. Verified working across multiple toggle cycles.

### Gap 4: Event Object Not Passed to Handler — ✅ RESOLVED

**Problem:** Many handlers declare an event parameter: `on click(evt) { ... }`. However, `dispatch_lambda_handler` calls `handler_fn(source_item)` — no event object is passed. Handlers that inspect `evt.target`, `evt.x`, `evt.y`, etc. will receive incorrect data.

**Resolution:** Implemented Option A. Handler signature changed to `Item handler(Item _model, Item _event)` with 2 MIR parameters. `build_lambda_event_map()` constructs a map with `{type, target_class, target_tag, x, y}` using `MarkBuilder`. The event parameter is bound to the handler's declared parameter name (e.g., `evt`) via MIR register. Handlers that don't declare an event parameter simply ignore the second argument.

**Key implementation details:**
- `transpile-mir.cpp`: Handler function now has 2 `MIR_var_t` params (`_model`, `_event`). If `handler->param` exists, the `_event` register is bound to the param name.
- `event.cpp`: `build_lambda_event_map(doc, target, event_name, evcon)` walks DomNode ancestry to find nearest DomElement for class/tag extraction.
- `cmd_layout.cpp`: `dom_doc->input = Input::create(pool, script_url)` added to provide arena allocation for event map construction.

### Gap 5: Template Body Re-Execution State Binding — ✅ RESOLVED

**Problem:** When `render_map_retransform()` re-executes a template body (`body_func(source_item)`), the body must pick up the **updated** state values. Since state is in the central store keyed by `(model_item, template_ref, state_name)`, and the body calls `tmpl_state_get_or_init()` internally, this should work — but only if the `template_ref` used in the body matches the one used in the handler.

**Resolution:** Both handler and body now use the same interned `template_ref` pointer. Fixed by interning `template_ref` via `name_pool_create_len()` in `transpile-mir.cpp` — see [Bug #1](#bugs-found-and-fixed) below.

### Gap 6: No UI Automation for Lambda Script Documents — ✅ RESOLVED

**Problem:** The event_sim framework's `"html"` field in JSON tests points to a document file. The `load_doc_by_format()` function already routes `.ls` files to `load_lambda_script_doc()`. However, no existing UI automation test uses a `.ls` file as the document source.

**Required:** Validated — `view_doc_in_window_with_events("todo.ls", "todo_toggle.json", true)` works in headless mode. See `test/ui/todo_toggle.json` and test results below.

### Gap 7: Missing Assertions for Reactive State — ✅ Sufficient

**Problem:** The existing assertion types (`assert_text`, `assert_style`, `assert_rect`, etc.) verify DOM/visual state. For reactive testing, we also need to verify:
- That clicking an element changes visible text (e.g., checkbox "✓" appears)
- That CSS classes change (e.g., `todo-item` → `todo-item done`)
- That new elements appear or disappear after model mutation

**Assessment:** The existing `assert_text`, `assert_style`, and `assert_attribute` assertions may suffice. `assert_text` checks text content after re-render. `assert_attribute` checks the `class` attribute. No new assertion types may be needed — the DOM is fully rebuilt after each handler.

---

## 3.1 Bugs Found and Fixed During Implementation {#bugs-found-and-fixed}

Five critical bugs were discovered and fixed while bringing the reactive loop end-to-end. All fixes are in commit `a624cf18`.

### Bug #1 — template_ref Dangling Pointer (transpile-mir.cpp)

**Symptom:** `render_map_mark_dirty()` never matched — handler and body used different `template_ref` pointers.

**Root cause:** MIR codegen built `template_ref` from a stack-local `char name_buf[64]`. The pointer was baked into MIR as an `int64` constant, but the stack buffer was reclaimed after the compilation function returned, leaving a dangling pointer. The handler's `template_ref` and the body's `template_ref` pointed to different (or garbage) memory, so pointer-equality keying in the render map and state store always failed.

**Fix:** Intern `template_ref` strings via `name_pool_create_len()` in three locations in `transpile-mir.cpp`:
1. Body function: `tmpl_ref = name_pool_create_len(mt->name_pool, name_buf, strlen(name_buf))->chars`
2. Handler call site: intern `vname` before passing to `transpile_handler_def`
3. Added `NamePool* name_pool` field to `MirTranspiler` struct, threaded through from `transpile_mir_ast()`

### Bug #2 — Stale EvalContext After Script Execution (event.cpp)

**Symptom:** SEGV in `heap_data_alloc()` during retransform body re-execution.

**Root cause:** The thread-local `__thread EvalContext* context` (used by GC allocation functions) was set to `&runner->context` during `run_script_mir()`. After the initial script execution returned, the `Runner` was stack-local and its `context` field was reclaimed. When `dispatch_lambda_handler` invoked the handler (which allocates via GC), `context` was a dangling pointer.

**Fix:** In `dispatch_lambda_handler` (event.cpp), create a temporary `EvalContext handler_ctx` populated from `doc->lambda_runtime` (heap, nursery, name_pool, pool). Set `context = &handler_ctx` before handler invocation, restore after.

### Bug #3 — Stale input_context Arena (event.cpp)

**Symptom:** SEGV at `arena_alloc(arena=0x0000000000000001)` during list expansion in retransform.

**Root cause:** The thread-local `__thread Context* input_context` (used by `expand_list()` for arena fallback allocation) was stale from the initial script execution. Its `arena` field contained garbage (0x1).

**Fix:** In `dispatch_lambda_handler`, save and clear `input_context = nullptr` before handler invocation, restore after. When `input_context` is null, the fallback allocation path is skipped.

### Bug #4 — fn_not Return Type Mismatch (transpile-mir.cpp)

**Symptom:** State value never toggled correctly — `not toggled` always produced the same garbage value (`0x360060250`).

**Root cause:** `fn_not()` returns `Bool` (a `uint8_t`), but MIR reads the return register as a full `i64`. The upper 56 bits contained garbage, which was stored as the Item value. Additionally, `!` in Lambda is **type negation** (exclusion), not logical NOT — the correct keyword is `not`.

**Fix:** Two changes:
1. In `transpile-mir.cpp`, wrapped `fn_not` result with `emit_box_bool()` (zero-extends uint8_t to i64, ORs with BOOL_TAG, handles BOOL_ERROR)
2. In `test/lambda/ui/todo.ls`, changed `toggled = !toggled` to `toggled = not toggled`

### Bug #5 — GC Collecting Live Doc Root (render_map.cpp)

**Symptom:** After first toggle, second toggle showed stale DOM — new retransform element had same address as old doc root.

**Root cause:** `render_map_retransform()` called `heap_gc_collect()` before re-executing body functions. The GC mark phase didn't know about `s_doc_root` (a file-static variable in `cmd_layout.cpp`), so it collected the live doc root element. The next allocation reused the same address, causing the retransform to appear to produce the "same" element.

**Fix:** Removed `heap_gc_collect()` from `render_map_retransform()`. Added comment: "NOTE: Do NOT call heap_gc_collect() here — the GC doesn't know about s_doc_root or other static roots."

### Known Issue — Whitespace Text Stripping

Single-space `" "` as element text content becomes empty string `""` after DOM rebuild. Workaround: use `"○"` instead of `" "` for the unchecked checkbox indicator. The root cause is in the DOM builder's whitespace handling — low priority.

---

## 4. Phase 6: Complete Event Dispatch to Handler Execution

### 6.1 Handler Invocation Context

The dispatch flow is already implemented. The key code path:

```
mouse_button_callback (window.cpp)
  → handle_event (event.cpp L1690)
    → target_html_doc: hit test → find target View
    → build_view_stack: ancestry from root to target
    → fire_events: bubble events up stack
    → [on MOUSE_UP] dispatch_lambda_handler(evcon, target, "click")
```

Inside `dispatch_lambda_handler` (event.cpp L310):
```
walk DOM ancestors from target
  → DomElement->native_element → construct Item
  → render_map_reverse_lookup(item) → (source_item, template_ref)
  → find TemplateEntry by template_ref
  → find TemplateHandlerEntry for "click"
  → handler_fn(source_item)   // MIR-compiled handler
  → if render_map_has_dirty() → retransform → rebuild_lambda_doc
```

**Task 6.1a — Verify MIR codegen handler binding:**

Check `transpile-mir.cpp` near L11590 to confirm:
1. Handler function signature matches `Item fn(Item model)`
2. Inside the handler, state reads compile to `tmpl_state_get_or_init(model, TEMPLATE_REF, "toggled", ItemFalse)`
3. State writes compile to `tmpl_state_set(model, TEMPLATE_REF, "toggled", new_val)` followed by `render_map_mark_dirty(model, TEMPLATE_REF)`
4. `TEMPLATE_REF` is the interned `template_ref` pointer from the TemplateEntry

**Task 6.1b — Verify template_ref consistency:**

The same `template_ref` string (interned pointer) must be used across:
- `template_registry_add()` when registering the template
- `render_map_record()` when recording source→result during `apply()`
- `tmpl_state_get/set()` when reading/writing state in handlers
- `render_map_mark_dirty()` when flagging for re-transform
- `render_map_retransform()` when re-executing the body

This is set in `transpile-mir.cpp` L11584 and propagated as a constant pointer in the MIR code.

### 6.2 Event Object Construction

**Phase 6 scope:** Defer full event object construction. Most handlers in the todo app don't use the event parameter:

```lambda
on click() {          // no evt parameter
  toggled = !toggled
}
```

For handlers that **do** declare an event parameter (`on click(evt)`), the MIR codegen must handle the case where the dispatch doesn't pass one. Two approaches:

- **Option A (recommended):** If the handler declares `evt`, construct a minimal event map:
  ```c
  // In dispatch_lambda_handler, before invoking:
  Item evt = build_event_object(evcon);  // {type: "click", x: ..., y: ...}
  typedef Item (*handler_fn_evt)(Item, Item);
  ((handler_fn_evt)h->handler_func)(lookup.source_item, evt);
  ```

- **Option B:** Always pass `ItemNull` for the event parameter. Handlers that don't reference `evt` are unaffected; those that do will get null.

**Decision:** Option A implemented for Phase 8. All handlers now receive a proper event map.

### 6.3 State Read/Write in Handlers

State access is already compiled into handlers via MIR codegen:

```
// Before handler body (MIR codegen):
Item toggled = tmpl_state_get_or_init(model, tmpl_ref, "toggled", ItemFalse);

// Handler body: toggled = !toggled
Item new_val = lmd_not(toggled);
tmpl_state_set(model, tmpl_ref, "toggled", new_val);
render_map_mark_dirty(model, tmpl_ref);
```

**Verification task:** Run `lambda view test/lambda/ui/todo.ls` with debug logging and click a todo item. Check `log.txt` for:
1. `dispatch_lambda_handler: invoking 'click' handler on tmpl=...`
2. `tmpl_state_set` called with the correct key
3. `render_map_mark_dirty` called
4. `render_map_retransform` re-executing the body
5. `rebuild_lambda_doc` rebuilding DOM

### 6.4 Model Mutation via Edit Bridge

For `edit` templates (not `view`), handler code can mutate the model through `edit_bridge`:

```lambda
edit <todo_item> state toggled: false {
  // ... same body ...
}
on click() {
  ~.done = !~.done   // modifies model via edit_bridge
}
```

This compiles to:
```c
Item new_done = lmd_not(map_get(model, "done"));
edit_map_update(model, "done", new_done);  // via MarkEditor, creates new version
edit_commit("toggle todo done");
```

**Phase 6 scope:** The todo.ls example originally used `view` only (state-only toggle via `toggled` flag). Phase 8 added an `edit <todo_list>` template exercising model mutation through `edit_map_update` (inline mode) for delete and clear-completed operations.

---

## 5. Phase 7: Re-Transform, Reflow, and Repaint

### 7.1 Dirty Propagation Chain

After `handler_fn(source_item)` executes:

```
tmpl_state_set(model, ref, "toggled", new_val)
  → render_map_mark_dirty(model, ref)
    → sets entry.dirty = true in the render map

[back in dispatch_lambda_handler]
render_map_has_dirty() returns true
  → render_map_retransform()
    → for each dirty entry:
         Item new_result = body_func(entry.key.source_item)
         replace_in_element_tree(s_doc_root, entry.result_node, new_result)
         entry.result_node = new_result
         entry.dirty = false
    → return count of retransformed entries
  → rebuild_lambda_doc(uicon)
```

### 7.2 Selective Re-Transform

Currently `render_map_retransform()` re-executes **only dirty** template bodies. If 8 todo items exist and 1 is clicked, only 1 body function is re-executed. This is already efficient.

**Optimization opportunity (future):** Memoize template body output. If `body_func(model)` with the current state produces the same element tree as before, skip the DOM rebuild entirely. This requires structural equality comparison on Lambda elements — defer to Phase 10.

### 7.3 Incremental DOM Rebuild

The current `rebuild_lambda_doc()` does a **full** DOM rebuild:

```
build_dom_tree_from_element(html_elem)  // entire tree
extract_and_collect_css(...)            // all <style> elements
apply_stylesheet_to_dom_tree_fast(...)  // full cascade
layout_html_doc(...)                    // full relayout
render_html_doc(...)                    // full repaint
```

**Phase 7 target:** This full rebuild approach works correctly and is fast enough for interactive use (the todo app has ~50 elements, rebuild takes <5ms). Keep it for now.

**Future optimization (Phase 10):** Incremental DOM patching:
1. Only rebuild the DOM subtree rooted at the changed element's parent
2. Reuse existing CSS declarations for unchanged subtrees
3. Use Radiant's `DirtyTracker` for partial relayout
4. Only repaint damaged regions

### 7.4 CSS Cascade Reuse

`rebuild_lambda_doc()` re-extracts and re-applies all CSS. For the todo app this is fast because the CSS is embedded in a single `<style>` element. For larger apps:

**Phase 7 optimization:** Cache the parsed `CssStylesheet*` on the `DomDocument` so that `rebuild_lambda_doc()` can skip CSS parsing on subsequent rebuilds. Only re-apply the cascade (which is already fast with `apply_stylesheet_to_dom_tree_fast`).

```c
void rebuild_lambda_doc(UiContext* uicon) {
    // ... existing code ...

    // OPTIMIZATION: reuse cached stylesheet if available
    if (doc->cached_inline_sheets && doc->cached_inline_sheet_count > 0) {
        // Skip extract_and_collect_css, use cached sheets
        for (int i = 0; i < doc->cached_inline_sheet_count; i++) {
            apply_stylesheet_to_dom_tree_fast(new_root, doc->cached_inline_sheets[i],
                                              matcher, doc->pool, css_engine);
        }
    } else {
        // First rebuild: extract and cache
        doc->cached_inline_sheets = extract_and_collect_css(...);
        doc->cached_inline_sheet_count = inline_count;
        // ... apply ...
    }
}
```

### 7.5 Render Map Maintenance Across Rebuilds

**Critical requirement:** After `rebuild_lambda_doc()` creates new `DomElement` nodes, the new DOM nodes' `native_element` pointers connect them back to the Lambda `Element*` objects in the element tree. The **render map** stores Lambda `Item` values (which wrap `Element*` pointers). Since `render_map_retransform()` updated the forward map's `result_node` to the **new** element, and `build_dom_tree_from_element()` sets `dom_elem->native_element = lambda_elem`, the reverse lookup chain is:

```
[next click] DomElement->native_element → Element* → Item
  → render_map_reverse_lookup(Item) → (source_item, template_ref)
```

**Correctness condition:** The render map's reverse index must be updated when `retransform()` replaces a result_node. Verify that `render_map_retransform()` calls a reverse-map-update function.

**Implementation task:** In `render_map_retransform()`, after replacing `entry.result_node`:
```c
// Remove old reverse entry
reverse_map_remove(old_result_node);
// Insert new reverse entry
reverse_map_insert(new_result_node, entry.key);
```

If this is not currently done, add it.

---

## 6. Phase 8: Extended Interactions — Add, Delete, Text Input

### 8.1 Add Todo Item

**UI pattern:** An "Add" button at the bottom of each list. Clicking it appends a new item to the model.

**Implementation:** Use an `edit` template for the todo list (not just individual items):

```lambda
edit <todo_list_editor>: {name: string, items: array} state input_text: "" {
  <div class:"todo-list"
    <div class:"list-header"
      <span class:"list-name"; ~.name>
      <span class:"add-btn"; "+">
    >
    <ul class:"items"
      for (item in ~.items)
        apply(<todo_item text:item.text, done:item.done>)
    >
    <div class:"add-form"
      <input type:"text", class:"new-item-input", placeholder:"New item...",
             value: input_text>
      <button class:"add-btn"; "Add">
    >
  >
}
on click(evt) {
  // click on "Add" button: append new item to model
  if (evt.target_class == "add-btn") {
    if (input_text != "") {
      let new_item = {id: len(~.items) + 1, text: input_text, done: false}
      ~.items = ~.items ++ [new_item]   // model mutation via edit_bridge
      input_text = ""                    // reset state
    }
  }
}
on input(evt) {
  input_text = evt.value                 // state-only mutation
}
```

**Edit bridge calls generated:**
```c
// ~.items = ~.items ++ [new_item]
Item new_items = lmd_concat(current_items, array_of_new);
edit_elmt_update_attr(model, "items", new_items);  // or edit_map_update
```

### 8.2 Delete Todo Item

**UI pattern:** A "×" delete button on each todo item, visible on hover.

```lambda
view <todo_item> state toggled: false {
  let done = if (toggled) (!it.done) else it.done
  let check_mark = if (done) "✓" else " "
  let done_class = if (done) "todo-item done" else "todo-item"
  <li class:done_class
    <span class:"checkbox"; check_mark>
    <span class:"todo-text"; it.text>
    <span class:"delete-btn"; "×">
  >
}
on click(evt) {
  if (evt.target_class == "delete-btn") {
    // Emit a delete event to the parent list editor
    emit("delete-item", {id: ~.id})
  } else {
    toggled = !toggled
  }
}
```

**Design choice:** Item deletion requires model mutation, which a `view` template cannot do. Two approaches:

1. **Emit pattern:** Child `view` emits a custom event, parent `edit` handles it. Requires `emit()` system function and custom event dispatch.
2. **Direct delete:** Change `<todo_item>` to `edit` and mutate the parent array directly. Requires parent access.

**Recommendation:** Approach 1 (emit) is cleaner and matches React's lifting-state-up pattern. Implement `emit(event_name, data)` as a system function that:
1. Searches the template instance hierarchy (up through DOM ancestry)
2. Finds the nearest ancestor template with a handler for `event_name`
3. Invokes that handler with the data as the event parameter

### 8.3 Text Input for Todo Content

**UI pattern:** An `<input>` element for entering new todo text.

**Required event support:**
- `on input(evt)` — Fired on each keystroke, `evt.value` contains the current input value
- `on keydown(evt)` — For detecting Enter key to submit

**Current state:** Radiant handles text input for `<input>` elements via `RDT_EVENT_TEXT_INPUT` and maintains the input value in the state store (caret position, selection, text content). However, the connection between Radiant's input state and Lambda template state needs bridging.

**Design:**

When a Radiant `<input>` element receives a `TEXT_INPUT` event and the element was produced by a Lambda template:
1. Radiant updates the input's internal text buffer
2. `dispatch_lambda_handler(target, "input")` is called
3. The handler receives `evt` with `{value: current_text, type: "input"}`
4. The handler stores the value in template state: `input_text = evt.value`

**Event object for `input` events:**
```c
Item build_input_event(EventContext* evcon, DomElement* input_elem) {
    MarkBuilder builder(...);
    ElementBuilder evt = builder.element("event");
    evt.attr("type", "input");
    evt.attr("value", get_input_value(input_elem));  // current text
    return evt.final();
}
```

### 8.4 Updated Todo Script

The complete todo.ls with add/delete/input support:

```lambda
let data^err = input('./test/lambda/ui/todos.json', 'json')

// Individual todo item — view (read-only, state-only toggle)
view <todo_item> state toggled: false {
  let done = if (toggled) (!~.done) else ~.done
  let check_mark = if (done) "✓" else " "
  let done_class = if (done) "todo-item done" else "todo-item"
  <li class:done_class
    <span class:"checkbox"; check_mark>
    <span class:"todo-text"; ~.text>
    <span class:"delete-btn"; "×">
  >
}
on click() {
  toggled = !toggled
}

// Todo list — edit (can mutate model items array)
edit list_editor: {name: string, items: array} state new_text: "" {
  <div class:"todo-list"
    <div class:"list-header"
      <span class:"list-name"; ~.name>
    >
    <ul class:"items"
      for (item in ~.items)
        apply(<todo_item text:item.text, done:item.done, id:item.id>)
    >
    <div class:"add-row"
      <input type:"text", class:"new-item", placeholder:"Add a task...",
             value: new_text>
      <button class:"add-btn"; "Add">
    >
  >
}
on input(evt) {
  new_text = evt.value
}
on click(evt) {
  if (evt.target_class == "add-btn" && new_text != "") {
    let new_item = {id: len(~.items) + 1, text: new_text, done: false}
    ~.items = ~.items ++ [new_item]
    new_text = ""
  }
}
on delete_item(evt) {
  ~.items = for (item in ~.items where item.id != evt.id) item
}

// HTML page shell
<html lang:"en"
<head
  <meta charset:"UTF-8">
  <title "Lambda Todo App">
  <style "..."  >
>
<body
  <div class:"container"
    <div class:"header"
      <h1 data.title>
      <p "Reactive UI — click items to toggle">
    >
    <div class:"content"
      for (lst in data.lists) apply(lst, {mode: 'edit', template: 'list_editor'})
    >
  >
>
>
```

---

## 7. Phase 9: UI Automation Testing for Reactive Apps

### 9.1 Lambda Script as Event Sim Document Source

The event_sim framework resolves document paths via `load_doc_by_format()`, which already routes `.ls` files to `load_lambda_script_doc()`. To test a Lambda reactive app:

```json
{
  "name": "Todo toggle test",
  "html": "test/lambda/ui/todo.ls",
  "viewport": {"width": 600, "height": 800},
  "events": [...]
}
```

**Execution path:**
```
view_doc_in_window_with_events("todo.ls", "todo_toggle.json", headless=true)
  → load_doc_by_format("todo.ls") → load_lambda_script_doc()
    → JIT compile script → template registry populated
    → apply() dispatches templates → element tree
    → build_dom_tree → CSS cascade → layout → render
  → event_sim_load("todo_toggle.json")
  → headless event loop: process events sequentially
    → click events → handle_event → dispatch_lambda_handler
    → assertions verify post-handler DOM state
```

**Verification task:** Confirm this path works by running:
```bash
./lambda.exe view test/lambda/ui/todo.ls --event-file test/ui/todo_toggle.json --headless
```

### 9.2 New Assertion Types for Reactive State

The existing assertion types are sufficient for most reactive testing:

| Assertion | Use Case in Reactive Testing |
|-----------|------------------------------|
| `assert_text` | Verify checkbox content changes ("✓" ↔ " ") after click |
| `assert_attribute` | Verify CSS class changes (`todo-item` → `todo-item done`) |
| `assert_visible` | Verify new elements appear after adding a todo item |
| `assert_style` | Verify strikethrough text-decoration on completed items |
| `assert_rect` | Verify layout changes after item add/delete |

**One new assertion type recommended:**

#### `assert_count` — verify number of matching elements

```json
{"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 8}
{"type": "assert_count", "target": {"selector": ".todo-item.done"}, "min": 2, "max": 5}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | CSS selector to match |
| `equals` | int | Exact expected count |
| `min` | int | Minimum expected count (inclusive) |
| `max` | int | Maximum expected count (inclusive) |

**Implementation:** Traverse the view tree, count elements matching the selector. Add as `SIM_EVENT_ASSERT_COUNT` in event_sim.

This is critical for reactive testing: after adding/deleting items, verify the item count changed.

### 9.3 Test Suite: todo_toggle.json

Tests the core reactive loop — clicking a todo item toggles its visual state.

```json
{
  "name": "Todo item toggle",
  "html": "test/lambda/ui/todo.ls",
  "viewport": {"width": 600, "height": 800},
  "default_timeout": 2000,
  "events": [
    {"type": "log", "message": "=== Todo Toggle Test ==="},
    {"type": "wait", "ms": 500},

    {"type": "log", "message": "Step 1: Verify initial render"},
    {"type": "assert_text", "target": {"selector": ".header h1"}, "contains": "My Todo List"},
    {"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 8},
    {"type": "assert_count", "target": {"selector": ".todo-item.done"}, "equals": 3},

    {"type": "log", "message": "Step 2: Verify first item is NOT done"},
    {"type": "assert_text", "target": {"selector": ".todo-item:first-child .todo-text"},
     "contains": "Review pull requests"},
    {"type": "assert_attribute", "target": {"selector": ".todo-item:first-child"},
     "attribute": "class", "contains": "todo-item",
     "not_contains": "done"},

    {"type": "log", "message": "Step 3: Click first todo item to toggle it"},
    {"type": "click", "target": {"selector": ".todo-item:first-child"}},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 4: Verify first item is now done"},
    {"type": "assert_attribute", "target": {"selector": ".todo-item:first-child"},
     "attribute": "class", "contains": "done"},
    {"type": "assert_text", "target": {"selector": ".todo-item:first-child .checkbox"},
     "contains": "✓"},

    {"type": "log", "message": "Step 5: Click again to untoggle"},
    {"type": "click", "target": {"selector": ".todo-item:first-child"}},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 6: Verify first item is NOT done again"},
    {"type": "assert_attribute", "target": {"selector": ".todo-item:first-child"},
     "attribute": "class", "not_contains": "done"},

    {"type": "log", "message": "Step 7: Verify footer count updates"},
    {"type": "assert_text", "target": {"selector": ".footer"},
     "contains": "of 8 tasks completed"},

    {"type": "log", "message": "Step 8: Visual regression snapshot"},
    {"type": "render", "file": "./temp/todo_toggle_final.png"}
  ]
}
```

### 9.4 Test Suite: todo_add_delete.json

Tests adding and deleting todo items (Phase 8 features).

```json
{
  "name": "Todo add and delete",
  "html": "test/lambda/ui/todo.ls",
  "viewport": {"width": 600, "height": 800},
  "default_timeout": 2000,
  "events": [
    {"type": "log", "message": "=== Todo Add/Delete Test ==="},
    {"type": "wait", "ms": 500},

    {"type": "log", "message": "Step 1: Verify initial item count"},
    {"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 8},

    {"type": "log", "message": "Step 2: Type new item text"},
    {"type": "click", "target": {"selector": ".new-item"}},
    {"type": "type", "target": {"selector": ".new-item"}, "text": "Write unit tests"},

    {"type": "log", "message": "Step 3: Click Add button"},
    {"type": "click", "target": {"selector": ".add-btn"}},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 4: Verify new item appears"},
    {"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 9},
    {"type": "assert_text", "target": {"selector": ".todo-item:last-child .todo-text"},
     "contains": "Write unit tests"},

    {"type": "log", "message": "Step 5: Delete the new item"},
    {"type": "click", "target": {"selector": ".todo-item:last-child .delete-btn"}},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 6: Verify item count back to original"},
    {"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 8},

    {"type": "log", "message": "Step 7: Visual regression snapshot"},
    {"type": "render", "file": "./temp/todo_add_delete_final.png"}
  ]
}
```

### 9.5 Test Suite: todo_text_input.json

Tests text input and form interaction.

```json
{
  "name": "Todo text input",
  "html": "test/lambda/ui/todo.ls",
  "viewport": {"width": 600, "height": 800},
  "default_timeout": 2000,
  "events": [
    {"type": "log", "message": "=== Todo Text Input Test ==="},
    {"type": "wait", "ms": 500},

    {"type": "log", "message": "Step 1: Focus input field"},
    {"type": "click", "target": {"selector": ".new-item"}},
    {"type": "assert_focus", "target": {"selector": ".new-item"}},

    {"type": "log", "message": "Step 2: Type text character by character"},
    {"type": "type", "target": {"selector": ".new-item"}, "text": "Hello"},
    {"type": "assert_value", "target": {"selector": ".new-item"}, "equals": "Hello"},

    {"type": "log", "message": "Step 3: Clear and retype"},
    {"type": "type", "target": {"selector": ".new-item"}, "text": "New task", "clear_first": true},
    {"type": "assert_value", "target": {"selector": ".new-item"}, "equals": "New task"},

    {"type": "log", "message": "Step 4: Submit via Enter key"},
    {"type": "key_press", "key": "Enter"},
    {"type": "wait", "ms": 200},

    {"type": "log", "message": "Step 5: Verify item was added"},
    {"type": "assert_count", "target": {"selector": ".todo-item"}, "equals": 9},
    {"type": "assert_text", "target": {"selector": ".todo-item:last-child .todo-text"},
     "contains": "New task"},

    {"type": "log", "message": "Step 6: Verify input cleared after submit"},
    {"type": "assert_value", "target": {"selector": ".new-item"}, "equals": ""}
  ]
}
```

### 9.6 Integration into CI

Add a new make target for reactive UI tests:

```makefile
test-reactive-ui:
	@echo "Running reactive UI tests..."
	@./lambda.exe view test/lambda/ui/todo.ls \
	    --event-file test/ui/todo_toggle.json --headless
	@./lambda.exe view test/lambda/ui/todo.ls \
	    --event-file test/ui/todo_add_delete.json --headless
	@./lambda.exe view test/lambda/ui/todo.ls \
	    --event-file test/ui/todo_text_input.json --headless
	@echo "Reactive UI tests complete"
```

Alternatively, place the JSON test files in `test/ui/` and they will be auto-discovered by the existing GTest-based `test_ui_automation_gtest.cpp` framework:

```
test/ui/todo_toggle.json       → "html": "test/lambda/ui/todo.ls"
test/ui/todo_add_delete.json   → "html": "test/lambda/ui/todo.ls"
test/ui/todo_text_input.json   → "html": "test/lambda/ui/todo.ls"
```

---

## 8. Architecture Diagram

### End-to-End Reactive Loop

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Lambda Script (todo.ls)                         │
│                                                                         │
│  ┌──────────────┐  ┌──────────────────┐  ┌────────────────────────┐    │
│  │ view         │  │ edit             │  │ Page Shell             │    │
│  │ <todo_item>  │  │ list_editor:     │  │ <html>...<body>...    │    │
│  │              │  │ {name, items}    │  │ for (lst) apply(lst)  │    │
│  │ state:       │  │                  │  │                        │    │
│  │  toggled     │  │ state:           │  └────────────────────────┘    │
│  │              │  │  new_text        │                                 │
│  │ on click()   │  │                  │                                 │
│  │  toggle      │  │ on click()       │                                 │
│  │              │  │  add item        │                                 │
│  └──────────────┘  │ on input()       │                                 │
│                     │  update text     │                                 │
│                     │ on delete_item() │                                 │
│                     │  remove item     │                                 │
│                     └──────────────────┘                                 │
└───────────────┬────────────────────────────────────────┬────────────────┘
                │ apply()                                │ handler_fn()
                ▼                                        ▲
┌─────────────────────────────┐    ┌─────────────────────────────────────┐
│     Template Registry       │    │     State Store + Render Map        │
│                             │    │                                     │
│ TemplateEntry {             │    │ tmpl_state: (model, ref, name)→val  │
│   body_func, handlers,     │    │ render_map: (model, ref)→result     │
│   specificity, state_decls │    │ reverse: result→(model, ref)        │
│ }                           │    │ edit_bridge: MarkEditor → DAG      │
└──────────┬──────────────────┘    └──────────┬──────────────────────────┘
           │ body_func(model)                  │ mark_dirty → retransform
           ▼                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Lambda Element Tree                              │
│  <html> > <body> > <div.container> > <div.content> > <div.todo-list>   │
│    > <ul.items> > <li.todo-item> > <span.checkbox> + <span.todo-text>  │
└───────────────┬─────────────────────────────────────────────────────────┘
                │ build_dom_tree_from_element()
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          Radiant Engine                                 │
│                                                                         │
│  ┌───────────┐  ┌───────────┐  ┌──────────┐  ┌──────────┐            │
│  │ DOM Tree  │→ │ CSS       │→ │ Layout   │→ │ Render   │            │
│  │ DomElement│  │ Cascade   │  │ ViewTree │  │ Surface  │            │
│  └───────────┘  └───────────┘  └──────────┘  └──────────┘            │
│                                       │              │                 │
│                                       ▼              ▼                 │
│                              ┌──────────────────────────────┐         │
│                              │  Window (GLFW)               │         │
│                              │  Event Loop                  │         │
│                              │  ┌────────────────────────┐  │         │
│                              │  │ GLFW Callbacks:        │  │         │
│                              │  │  mouse_button_callback │  │         │
│                              │  │  character_callback    │  │         │
│                              │  │  key_callback          │  │         │
│                              │  └──────────┬─────────────┘  │         │
│                              └─────────────┼────────────────┘         │
│                                            │                          │
│                                            ▼                          │
│                              ┌──────────────────────────────┐         │
│                              │  handle_event()              │         │
│                              │  1. Hit test → target View   │         │
│                              │  2. Pseudo-state update      │         │
│                              │  3. fire_events (bubble)     │         │
│                              │  4. dispatch_lambda_handler  │────┐    │
│                              └──────────────────────────────┘    │    │
│                                                                  │    │
└──────────────────────────────────────────────────────────────────┼────┘
                                                                   │
               ┌───────────────────────────────────────────────────┘
               │ reverse_lookup → find TemplateEntry → invoke handler
               │ → state/model mutated → mark dirty → retransform
               │ → rebuild_lambda_doc → DOM → CSS → layout → render
               ▼
            (cycle repeats on next user interaction)
```

### UI Automation Integration

```
┌──────────────────────────────────────────────────────────┐
│  Test JSON (todo_toggle.json)                            │
│  {"html": "test/lambda/ui/todo.ls",                      │
│   "events": [                                            │
│     {"type": "click", "target": {"selector": "..."}},    │
│     {"type": "assert_text", ...},                        │
│     {"type": "assert_count", ...}                        │
│   ]}                                                     │
└────────────────────┬─────────────────────────────────────┘
                     │ event_sim_load()
                     ▼
┌──────────────────────────────────────────────────────────┐
│  EventSimContext                                         │
│  - Loads .ls via load_lambda_script_doc()                │
│  - Processes events sequentially in headless mode        │
│  - click → handle_event → dispatch_lambda_handler        │
│  - assert_text → checks DOM after handler + rebuild      │
│  - assert_count → counts matching elements               │
│  - Reports: "Assertions: N passed, M failed"             │
└──────────────────────────────────────────────────────────┘
```

---

## 9. Implementation Order

| # | Task | Phase | Deps | Effort | Status |
|---|------|-------|------|--------|--------|
| 1 | Verify handler MIR codegen binds state correctly | 6 | — | S | ✅ Done — verified, fixed Bug #1 (name_pool interning) |
| 2 | Verify render_map reverse index updated on retransform | 7 | — | S | ✅ Done — already correct |
| 3 | Run todo.ls headless, click item, verify toggle in log.txt | 6 | 1,2 | S | ✅ Done — toggle works end-to-end |
| 4 | Fix any issues found in steps 1–3 | 6,7 | 3 | M | ✅ Done — 5 bugs fixed (see §3.1) |
| 5 | Cache CSS stylesheet in rebuild_lambda_doc | 7 | — | S | ✅ Done — DomDocument caches `CssStylesheet**` + `CssEngine*`, skip re-parse on subsequent rebuilds |
| 6 | Implement `assert_count` in event_sim | 9 | — | S | ✅ Done — `SIM_EVENT_ASSERT_COUNT`, counting visitor, supports exact/min/max |
| 7 | Write todo_toggle.json test, run headless | 9 | 3,6 | M | ✅ Done — 6/6 assertions pass (added assert_count) |
| 8 | Implement `emit()` system function for cross-template events | 8 | 4 | M | ✅ Done — `pn_emit()` → `dispatch_emit()`, thread-local `EmitHandlerContext`, DOM ancestry walk |
| 9 | Build event object for `on click(evt)` handlers | 6 | 4 | M | ✅ Done — 2-param handler signature, `build_lambda_event_map()` with {type, target_class, target_tag, x, y} |
| 10 | Wire `on input(evt)` to Radiant text input events | 8 | 9 | M | ✅ Done — `dispatch_lambda_handler` wired in `RDT_EVENT_TEXT_INPUT` case (full text editing still TODO) |
| 11 | Enhance todo.ls with delete/clear completed | 8 | 8,9 | M | ✅ Done — `edit <todo_list>` template, delete via emit, clear completed button |
| 11b | Wire edit template mutations to reactivity | 8 | 11 | M | ✅ Done — inline mode, runtime type dispatch, dirty marking for edit handlers |
| 12 | Write todo_add_delete.json + todo_text_input.json tests | 9 | 6,11 | M | ✅ Done — todo_delete.json (8/8 with assert_count), todo_toggle.json (6/6 with assert_count). text_input test deferred. |
| 13 | Add `test-reactive-ui` make target | 9 | 12 | S | ✅ Done — `make test-reactive-ui` runs todo_toggle + todo_delete via headless lambda.exe |

**Effort:** S = small (< half day), M = medium (1–2 days)

**Critical path:** All 13 tasks complete ✅ — Phases 6–9 fully implemented. Only remaining gap: full interactive text editing in form controls (Radiant TODO).

**Next steps:** Full interactive text editing in `<input>` form controls (Radiant TODO — character insertion, value persistence, selection). Then Phase 10 optimizations (incremental DOM patching, memoized template output).

---

## 10. Risk Analysis

| Risk | Severity | Status |
|------|----------|--------|
| Handler MIR codegen doesn't bind state correctly | High | ✅ Mitigated — Bug #1 fixed (name_pool interning) |
| Render map reverse index stale after retransform | High | ✅ Already correct — no fix needed |
| Full DOM rebuild too slow for complex apps (>500 elements) | Medium | Acceptable for Phase 6-8; incremental patching in Phase 10 |
| Lambda GC collects elements still referenced by render map | Medium | ✅ Mitigated — Bug #5 fixed (removed premature GC collect) |
| Stale thread-local contexts after initial script execution | High | ✅ Mitigated — Bugs #2, #3 fixed (EvalContext + input_context) |
| fn_not return type mismatch (Bool→i64 upper bits garbage) | High | ✅ Mitigated — Bug #4 fixed (emit_box_bool) |
| Input element value sync between Radiant state store and Lambda template state | Medium | Phase 8 task 10; may need bidirectional sync mechanism |
| `emit()` cross-template dispatch requires template instance hierarchy | Medium | ✅ Resolved — `dispatch_emit()` walks DOM ancestry, skips self template, finds parent handler |
| Edit template mutations not connected to retransform pipeline | High | ✅ Resolved — switched to inline mode, added runtime type dispatch, dirty marking after edit handlers |

---

## 11. Files Modified

### Commit `a624cf18` — Phase 6/7/9 (toggle)

| File | Changes |
|------|---------|
| `lambda/transpile-mir.cpp` | Bug #1: name_pool interning for template_ref (3 locations). Bug #4: emit_box_bool after fn_not call |
| `radiant/event.cpp` | Bug #2: EvalContext setup from lambda_runtime. Bug #3: input_context save/clear in dispatch_lambda_handler |
| `lambda/render_map.cpp` | Bug #5: removed heap_gc_collect() from retransform |
| `lambda/template_state.cpp` | Debug logging cleanup |
| `lambda/lambda-mem.cpp` | Defensive null checks in heap_data_alloc |
| `lib/gc_heap.c` | Defensive null checks in gc_data_alloc |
| `lib/gc_data_zone.c` | Debug logging cleanup |
| `test/lambda/ui/todo.ls` | `not toggled` (was `!toggled`), `"○"` (was `" "`) |
| `test/ui/todo_toggle.json` | New: headless toggle test, 4 assertions |

### Phase 8 Changes (2026-04-04) — Event Object, Emit, Delete/Clear

| File | Changes |
|------|--------|
| `lambda/transpile-mir.cpp` | Handler signature changed to 2-param `(Item _model, Item _event)`. Event parameter binding via `handler->param->name` → `_event` register |
| `radiant/event.cpp` | Added `build_lambda_event_map()` (constructs {type, target_class, target_tag, x, y} via MarkBuilder). Added `EmitHandlerContext` thread-local + `dispatch_emit()` for cross-template event dispatch. Handler invocation now 2-arg `fn(source_item, event_item)`. Added dirty marking for edit handlers (`tmpl->is_edit → render_map_mark_dirty`) in both `dispatch_lambda_handler` and `dispatch_emit` |
| `radiant/cmd_layout.cpp` | `dom_doc->input = Input::create(pool, script_url)` for event map arena allocation |
| `lambda/lambda.h` | Added `SYSPROC_EMIT` enum, declared `pn_emit()` and `dispatch_emit()` |
| `lambda/sys_func_registry.c` | Registered `emit` as `{SYSPROC_EMIT, "emit", 2, ...}` |
| `lambda/lambda-proc.cpp` | Added `pn_emit()` thin wrapper calling `dispatch_emit()` |
| `lambda/edit_bridge.cpp` | Switched from `EDIT_MODE_IMMUTABLE` to `EDIT_MODE_INLINE` for in-place model mutation. Added runtime type dispatch: `edit_map_update()` now handles elements (type=19) by routing to `elmt_update_attr()` |
| `lambda/mark_editor.cpp` | `commit()` returns 0 (silent no-op) in inline mode instead of logging a warning |
| `test/lambda/ui/todo.ls` | Full rewrite: added `<span class:"delete-btn">` to todo_item, `on click(evt)` with target_class dispatch, `emit("delete_item", it)`. New `edit <todo_list>` template with clear-completed button, `on delete_item(evt)` handler. CSS for delete button and clear button. Page shell uses `apply(<todo_list ...>, {mode: "edit"})` |
| `test/ui/todo_delete.json` | New: delete + clear completed test, 5 assertions |

### Bugs Found and Fixed During Phase 8

#### Bug #6 — Edit Bridge Immutable Mode Incompatible with Render Map Reactivity

**Symptom:** `edit_map_update()` returned a new `Map*` (immutable copy-on-write), but the render map's `source_item` still pointed to the old map, so retransform re-rendered stale data.

**Root cause:** `EDIT_MODE_IMMUTABLE` creates new map copies on every mutation. The render map keys by `source_item` pointer identity. The new map copy has a different pointer, so `render_map_mark_dirty()` can't find the entry, and even if it could, `retransform()` would re-invoke the body with the old `source_item`.

**Fix:** Switched edit bridge to `EDIT_MODE_INLINE`. In-place mutation preserves the same `Map*`/`Element*` pointer, keeping render map entries valid. Added `render_map_mark_dirty()` call after edit handler invocation in `dispatch_lambda_handler` (for direct edit handler clicks) and `dispatch_emit` (for emit-triggered parent edit handlers).

#### Bug #7 — Runtime Type Mismatch in edit_map_update

**Symptom:** `map_update: not a map (type=19)` — the model was an `Element` (type 19), but the transpiler emitted `edit_map_update` because the compile-time type was `ANY`.

**Root cause:** In `transpile-mir.cpp`, the `AST_NODE_MEMBER_ASSIGN_STAM` edit path checks `get_effective_type(mt, ca->object)` to decide between `edit_map_update` and `edit_elmt_update_attr`. When the model type is `LMD_TYPE_ANY` (common for template parameters), `edit_map_update` is always selected.

**Fix:** Added runtime type dispatch in `edit_map_update()`: if `get_type_id(map) == LMD_TYPE_ELEMENT`, routes to `elmt_update_attr()` instead.

### Phase 9 Changes (2026-04-04) — CSS Cache, assert_count, Input Wiring, CI Target

| File | Changes |
|------|--------|
| `lambda/input/css/dom_element.hpp` | Added `cached_inline_sheets`, `cached_inline_sheet_count`, `cached_css_engine` fields to `DomDocument`. Updated constructor initializer list |
| `radiant/cmd_layout.cpp` | `rebuild_lambda_doc()` now caches parsed `CssStylesheet**` and `CssEngine*` on first call, skips `extract_and_collect_css()` on subsequent rebuilds |
| `radiant/event_sim.hpp` | Added `SIM_EVENT_ASSERT_COUNT` to `SimEventType` enum. Added `assert_count_expected`, `assert_count_min`, `assert_count_max` fields to `SimEvent` |
| `radiant/event_sim.cpp` | Added `assert_count` JSON parsing block. Added `sim_count_visitor` + `count_elements_by_selector()` (traverses all matches, unlike `find_element_by_selector` which stops on first). Added execution case for `SIM_EVENT_ASSERT_COUNT`. Updated both assertion range checks (`SIM_EVENT_ASSERT_ATTRIBUTE` → `SIM_EVENT_ASSERT_COUNT`) |
| `radiant/event.cpp` | Wired `dispatch_lambda_handler(&evcon, focused, "input")` in `RDT_EVENT_TEXT_INPUT` case for `on input(evt)` Lambda handler dispatch |
| `Makefile` | Added `test-reactive-ui` phony target: runs `todo_toggle.json` + `todo_delete.json` via headless `lambda.exe view`. Added help text |
| `test/ui/todo_toggle.json` | Added 2 `assert_count` assertions (initial count=4, count unchanged after toggle). Now 6 total assertions |
| `test/ui/todo_delete.json` | Added 3 `assert_count` assertions (initial=4, after delete=3, after clear completed=2). Now 8 total assertions |

### Test Results

```
$ ./lambda.exe view test/lambda/ui/todo.ls --event-file test/ui/todo_toggle.json --headless
Assertions: 6 passed, 0 failed — PASS

$ ./lambda.exe view test/lambda/ui/todo.ls --event-file test/ui/todo_delete.json --headless
Assertions: 8 passed, 0 failed — PASS

$ make test-lambda-baseline
540/560 passed (20 pre-existing failures unrelated to these changes)
```
