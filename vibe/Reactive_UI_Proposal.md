# Reactive UI Proposal for Lambda / Radiant

**Date:** 2026-03-27  
**Status:** Phases 1–4 implemented, Phase 5 event dispatch bridge complete

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Design Philosophy](#2-design-philosophy)
3. [Language Extensions: `view` and `edit`](#3-language-extensions-view-and-edit)
   - [3.1 Viewer Definition](#31-viewer-definition)
   - [3.2 Editor Definition](#32-editor-definition)
   - [3.3 Syntax Breakdown](#33-syntax-breakdown)
   - [3.4 Event Handlers](#34-event-handlers)
   - [3.5 Scope and Restrictions](#35-scope-and-restrictions)
4. [System Function: `apply()`](#4-system-function-apply)
5. [Element `id` and the `page` Keyword](#4a-element-id-and-the-page-keyword)
   - [4A.1 Element `id` Attribute](#4a1-element-id-attribute)
   - [4A.2 The `page` Keyword](#4a2-the-page-keyword)
6. [Parent Pointer Removal (Future)](#4b-parent-pointer-removal-future)
7. [Reactive Lifecycle](#5-reactive-lifecycle)
   - [5.1 Initial Render](#51-initial-render)
   - [5.2 Event Dispatch](#52-event-dispatch)
   - [5.3 Re-transformation and Patch](#53-re-transformation-and-patch)
8. [Central State Store](#5a-central-state-store)
   - [5A.1 State Key Structure](#5a1-state-key-structure)
   - [5A.2 Lifecycle and Scope](#5a2-lifecycle-and-scope)
   - [5A.3 Implementation](#5a3-implementation-extending-radiants-statestore)
   - [5A.4 State Access in Handlers](#5a4-state-access-in-handlers)
9. [DAG-Based Document Model via MarkEditor](#6-dag-based-document-model-via-markeditor)
   - [6.1 MarkEditor Integration](#61-markeditor-integration)
   - [6.2 Structural Sharing](#62-structural-sharing)
   - [6.3 Undo/Redo via EditVersion Chain](#63-undoredo-via-editversion-chain)
   - [6.4 Memory Management](#64-memory-management)
10. [Integration with Radiant](#7-integration-with-radiant)
    - [7.1 Layout Pipeline](#71-layout-pipeline)
    - [7.2 Event Routing](#72-event-routing)
    - [7.3 Incremental Repaint](#73-incremental-repaint)
11. [Template Matching and Dispatch](#8-template-matching-and-dispatch)
    - [8.1 XSLT-Style Pattern Matching](#81-xslt-style-pattern-matching)
    - [8.2 Specificity](#82-specificity)
    - [8.2.1 Future: Priority and Mode](#821-future-priority-and-mode-xslt-style)
    - [8.3 Recursive Application and `apply;` Statement](#83-recursive-application-and-apply-statement)
12. [Grammar Extensions](#9-grammar-extensions)
13. [Examples](#10-examples)
14. [Implementation Phases](#11-implementation-phases)
15. [Open Questions and Suggestions](#12-open-questions-and-suggestions)

---

## 1. Executive Summary

This proposal introduces **reactive UI programming** to Lambda/Radiant by adding two new top-level constructs — `view` (read-only viewer) and `edit` (read-write editor) — that combine XSLT-style template matching with React-style component state and event handling.

**Key idea:** Source data is transformed to a presentation tree via pattern-matched templates. UI events trigger handler code that mutates component state (viewers) or both state and model (editors). Mutations automatically re-trigger template transformation and Radiant layout patching — no manual DOM manipulation required.

**Design influences:**

| Concept | Inspiration | Lambda Adaptation |
|---------|------------|-------------------|
| Template matching on source data | XSLT `<xsl:template match="...">` | `view pattern` / `edit pattern` (bare pattern) |
| Functional component body | React functional components | Functional statement body returns element tree |
| Local state | React `useState` | `state name: val` declarations |
| Event handlers | React `onClick`, `onChange` | `on event_name(evt) { ... }` blocks |
| Model binding | Two-way data binding | `edit` can mutate the matched model |
| Immutable document history | Persistent data structures / Immer | DAG tree via MarkEditor (copy-on-write) |

---

## 2. Design Philosophy

1. **Functional-first presentation** — The template body is a pure functional transformation from model → view tree. No imperative DOM manipulation.

2. **Controlled mutability** — Only `state` (viewer/editor) and `model` (editor only) are mutable, and only within `on` handlers. Handler code uses procedural (`pn`-like) semantics.

3. **Pattern-driven dispatch** — Templates are selected based on Lambda type patterns, analogous to XSLT's `match` attribute. More specific patterns win.

4. **Automatic reconciliation** — After handler execution, the framework re-applies templates and diffs the output against the current view tree, patching only what changed (React-style virtual DOM diffing).

5. **Persistent document model** — The source document is edited through `MarkEditor` in `EDIT_MODE_IMMUTABLE`, reusing its existing copy-on-write, version history, and undo/redo infrastructure.

---

## 3. Language Extensions: `view` and `edit`

### 3.1 Viewer Definition

```lambda
view vw_name: pattern (params) return_type state name: val, ... {
    // functional statement body — transforms model into view tree
    // returns element(s) for Radiant to render
}
on event_a(evt) { /* procedural handler code */ }
on event_b()    { /* procedural handler code */ }
```

**Semantics:** A viewer is a read-only template. It transforms a matched model item into a presentation element tree. Viewers can hold local state (e.g., collapsed/expanded, scroll position, hover highlight) but **cannot** modify the source model.

### 3.2 Editor Definition

```lambda
edit ed_name: pattern (params) return_type state name: val, ... {
    // functional statement body — same as viewer
}
on event_a(evt) { /* can mutate both state and model */ }
on event_b()    { /* can mutate both state and model */ }
```

**Semantics:** An editor extends viewer with write access to the model. Mutations to the model go through the DAG system, producing a new immutable version and pushing the old version onto the undo stack.

### 3.3 Syntax Breakdown

```
┌─────────┐ ┌────────┐ ┌─────────────┐ ┌────────┐ ┌───────────┐ ┌───────────────────┐ ┌─────────────┐
│view/edit│ │name:   │ │model_pattern│ │(params)│ │return_type│ │state k:v, k:v, ...│ │{ body }      │
└─────────┘ └────────┘ └─────────────┘ └────────┘ └───────────┘ └───────────────────┘ └─────────────┘
 keyword     optional    REQUIRED        optional   optional      optional              REQUIRED
             name + ':'  bare pattern    only if    Lambda type   constant init
                                         ret type
```

**Pattern forms:**

| Form | Example | Matches |
|------|---------|---------|
| Element pattern | `<section>`, `<todo done: bool>` | Element by tag (and optional attributes) |
| Map pattern | `{name: string, age: int}` | Map by field structure |
| Type/name | `array`, `string`, `my_type` | Named type or pattern |
| Union | `<p> \| <span> \| <code>` | Any of the alternatives |

**Syntax parts:**

| Part | Required | Description |
|------|----------|-------------|
| `view` / `edit` | **yes** | Keyword — determines viewer vs editor semantics |
| `name:` | no | Named template for explicit invocation — identifier followed by `:`. Anonymous templates (no name) are dispatched by pattern only |
| `model_pattern` | **yes** | Bare pattern that matches the source item — element `<...>`, map `{...}`, type name, or union with `\|`. No brackets |
| `(params)` | no | Additional parameters. **Optional** — parentheses can be omitted entirely if there is no return type |
| `return_type` | no | Return type annotation (defaults to `element`). When present, `()` is **required** before it to disambiguate |
| `state k: val, ...` | no | Local state declarations with constant initial values. Comma-separated `name: literal` pairs |
| `{ body }` | **yes** | Functional statement body — pure transformation producing view elements. The matched model binds to `~` (tilde, Lambda's context item). `~` is available in both the body and all `on` handlers |
| `on event(evt) { ... }` | no | Zero or more event handler blocks following the body. Procedural semantics (like `pn`). `~` refers to the same matched model item as in the body |

**Examples of `()` optionality:**

```lambda
view <p> { ... }                               // no params, no return type — () omitted
view link_elmt state x: 0 { ... }             // match on type name, no () needed
view link_elmt () int state x: 0 { ... }      // () required because return type follows
view my_view: <div> (mode: symbol) { ... }    // named template — name: before pattern
```

### 3.4 Event Handlers

Event handlers use `on` keyword followed by an event name and optional parameter:

```lambda
on click(evt) {
    // evt is the event object with fields:
    //   .type: symbol      — event type ('click', 'keydown', etc.)
    //   .target: element   — the element that received the event
    //   .x, .y: float      — mouse coordinates (for pointer events)
    //   .key: string       — key name (for keyboard events)
    //   .mods: int         — modifier bitmask (shift, ctrl, alt, super)
    //   .text: string      — text input (for text-input events)

    // mutate state:
    expanded = !expanded

    // mutate model (edit only):
    ~.title = "New Title"
}
```

**Handler semantics:**
- Handlers execute in procedural mode (`pn`-like): `var`, assignment (`=`), `while`, `return` are all available.
- **`~` refers to the current model item** bound by the pattern match — the same `~` as in the template body. This is consistent: `~` always means "the model item this template was matched against".
- State variables declared in the `state` clause are directly accessible by name.
- In a `view`, assignment to `~` or any model path raises a compile-time error.
- In an `edit`, assignment to `~` paths produces a new DAG version of the document via `MarkEditor`.

**Handler names** can be either **built-in event names** (mapped from Radiant/HTML events, see tables below) or **user-defined names**. User-defined handlers are invoked programmatically via `emit(handler_name, data)` from other handlers or from application code, enabling custom inter-component signaling.

#### Built-in UI Events (HTML naming convention)

UI event names follow HTML/DOM conventions for familiarity:

| Lambda Event | Radiant Event | Handler Parameter | Description |
|-------------|---------------|-------------------|-------------|
| `click` | `RDT_EVENT_CLICK` | `MouseButtonEvent` | Single click |
| `dblclick` | `RDT_EVENT_DBL_CLICK` | `MouseButtonEvent` | Double click |
| `mousedown` | `RDT_EVENT_MOUSE_DOWN` | `MouseButtonEvent` | Mouse button pressed |
| `mouseup` | `RDT_EVENT_MOUSE_UP` | `MouseButtonEvent` | Mouse button released |
| `mousemove` | `RDT_EVENT_MOUSE_MOVE` | `MousePositionEvent` | Mouse moved |
| `mouseenter` | (synthetic) | `MousePositionEvent` | Mouse enters element (no bubble) |
| `mouseleave` | (synthetic) | `MousePositionEvent` | Mouse leaves element (no bubble) |
| `scroll` | `RDT_EVENT_SCROLL` | `ScrollEvent` | Scroll wheel / touchpad |
| `keydown` | `RDT_EVENT_KEY_DOWN` | `KeyEvent` | Key pressed |
| `keyup` | `RDT_EVENT_KEY_UP` | `KeyEvent` | Key released |
| `input` | `RDT_EVENT_TEXT_INPUT` | `TextInputEvent` | Text input character |
| `change` | (synthetic) | `ChangeEvent` | Input value committed |
| `focusin` | `RDT_EVENT_FOCUS_IN` | `FocusEvent` | Element gains focus |
| `focusout` | `RDT_EVENT_FOCUS_OUT` | `FocusEvent` | Element loses focus |
| `submit` | (synthetic) | `Event` | Form submission |

#### Built-in Lifecycle Events

| Lambda Event | When Fired | Parameter | Description |
|-------------|-----------|-----------|-------------|
| `init` | After first match | none | **Initialization hook** — runs once when a template is first matched to a model item. Use to compute initial state from model data, load external data, or perform setup. Runs before first render. State mutations here do not trigger a re-render (they are part of the initial render) |
| `mount` | After first render | none | **Mount hook** — runs once after the template's output is first rendered and laid out by Radiant (pixels on screen). Use for measuring layout dimensions, starting animations, or integrating with external libraries that need a live element. Analogous to React's `componentDidMount` / `useEffect(fn, [])` |
| `update` | After re-render | none | **Update hook** — runs after each re-render caused by state or model changes (not after the initial render). Use for reacting to visual changes, adjusting scroll position, or triggering side effects based on new state. Analogous to React's `componentDidUpdate` / `useEffect(fn)` (without empty deps) |
| `error` | On child error | `err: error` | Error boundary — catches errors from child template rendering or handler execution |
| `unmount` | Before unmatch | none | Cleanup hook — runs when the template instance is about to be removed (model item deleted or no longer matches). Use for resource cleanup |

```lambda
// init example — compute initial state from model
view <dashboard> state stats: null {
    if stats != null then
        <div class: "stats"; stats.summary>
    else
        <div class: "loading"; "Computing...">
}
on init() {
    // compute derived state on first match
    stats = {
        total: count(~.items),
        done: count(~.items where ~.done),
        summary: count(~.items where ~.done) ++ "/" ++ count(~.items)
    }
}
on unmount() {
    // cleanup if needed
}
```

### 3.5 Scope and Restrictions

1. **Top-level only** — `view` and `edit` declarations must appear at the script's top level (like `fn` and `pn`). They cannot be nested inside functions or other templates.

2. **Body is functional** — The body block follows `fn` semantics: no `var`, no assignment, no `while`. It is a pure transformation.

3. **Handlers are procedural** — The `on` blocks follow `pn` semantics: `var`, assignment, `while`, `return` are available. Side effects are confined to state and model mutation.

4. **State is centrally stored and session-persistent** — State is managed in a central store keyed by `(matched_model_item, template_name_or_ref, state_name)`. State persists for the lifetime of the UI session (window/page). When a model item switches to a different template (due to pattern re-evaluation), states for the previous template are **retained** in the store — the item may switch back later. When the window/page is closed, all session state is released. More persistent storage is reserved for future work.

5. **No direct DOM access** — Templates produce abstract element trees. The framework handles DOM creation, diffing, and patching. The `page` keyword provides query access to the rendered page (see §4).

---

## 4. System Function: `apply()`

```lambda
apply(target, options?, ...params)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `any` | The model item (typically an element tree) to apply templates to |
| `options` | `map?` | Optional configuration map |
| `...params` | `any*` | Additional parameters forwarded to template `(params)` |

**Return value:** The rendered element tree produced by applying matched templates to `target`.

### Options Map

| Key         | Type       | Default | Description                                              |
| ----------- | ---------- | ------- | -------------------------------------------------------- |
| `mode`      | `symbol`   | `'view'` | `'view'` or `'edit'` — selects viewer or editor templates  |
| `recursive` | `bool`     | `true`  | Whether to recursively apply templates to child elements |
| `depth`     | `int`      | `∞`     | Max recursion depth for template application             |
| `scope`     | `string`   | `""`    | Template namespace/scope filter                          |
| `on_change` | `function` | `null`  | Callback invoked when model changes (edit mode)          |

### Dispatch Algorithm

1. Collect all `view` or `edit` templates (based on `mode`) whose model pattern matches `target`.
2. Rank by **specificity** (see §8.2).
3. Invoke the highest-priority template with `~` bound to `target` and params forwarded.
4. If `recursive` is true, recursively apply templates to each child element in the output.
5. Return the final element tree.

### Usage Examples

```lambda
// apply viewer templates to a data document
let doc = read("data.json")
let ui = apply(doc)

// apply with explicit mode and params
let ui = apply(doc, {mode: 'edit'}, "admin")

// apply in a pipeline
read("data.json") | apply(~, {mode: 'view'}) | render(~, "output.svg")
```

---

## 4A. Element `id` and the `page` Keyword

### 4A.1 Element `id` Attribute

Template output elements can have an `id` attribute, following HTML convention:

```lambda
view <todo-list> {
    <div id: "todo-app";
        <h1; "Todos">
        <button id: "add-btn"; "Add">
        <ul id: "todo-list";
            apply;
        >
    >
}
```

The `id` must be unique within the rendered page. It serves two purposes:
1. **CSS styling** — Radiant can resolve `#id` selectors.
2. **Programmatic access** — The `page` keyword can locate elements by id.

### 4A.2 The `page` Keyword

`page` is a new keyword that refers to the **rendered result page** — the live view tree produced by `apply()` and laid out by Radiant. It provides two capabilities:

#### Element Access by ID

```lambda
page.id.event_name(params)   // invoke event handler on element with id
```

Examples:
```lambda
// from a handler, programmatically trigger another element's handler
on keydown(evt) {
    if evt.key == "Enter" then
        page.add_btn.click()      // invoke click handler on #add-btn
}

// invoke a user-defined handler on a specific element
page.editor.save({format: 'json'})
```

#### Page Query (pattern matching)

```lambda
page ? pattern    // query rendered page elements, like Lambda markup query
```

This uses the same query syntax as Lambda's standard element queries, but operates on the rendered view tree instead of the source model:

```lambda
// find all visible todo items in the rendered page
let visible_todos = page ? <li class: "todo">

// find element by id via query
let btn = page ? <button id: "add-btn">
```

#### Page DOM Properties

Page elements expose additional layout/rendering properties beyond source attributes, similar to HTML DOM properties:

| Property | Type | Description |
|----------|------|-------------|
| `.x`, `.y` | `float` | Position relative to parent (from Radiant layout) |
| `.width`, `.height` | `float` | Computed dimensions |
| `.visible` | `bool` | Whether the element is in the visible viewport |
| `.scroll_top`, `.scroll_left` | `float` | Scroll position (for scrollable elements) |
| `.computed_style` | `map` | Resolved CSS properties |

```lambda
// read layout properties from a handler
on click(evt) {
    let pos = page.sidebar.y
    let size = page.sidebar.height
}
```

> **Naming note:** `page` was chosen because it represents the rendered page/screen. Alternatives considered: `view` (conflicts with template keyword), `screen` (too platform-specific), `dom` (too HTML-specific), `ui` (too generic). `page` aligns with Radiant's document-oriented rendering model.

---

## 4B. Parent Pointer Removal (Future)

> **Status:** Design note for future implementation. No code changes in current phase.

Since the source document is managed as a **DAG tree** (where the same subtree may be shared across versions), traditional parent pointers on elements do not work — a node may have multiple logical parents across versions.

**Current design:** Lambda elements have a parent pointer. This will need to be removed for DAG compatibility.

**Proposed parent resolution strategy:**

1. **Arena-based root identification** — Given a node, determine which document arena it belongs to. The arena identifies the document (and version).
2. **Root-down traversal** — From the document root, traverse down to locate the node and identify its parent path.
3. **Cached parent mapping** — For performance, maintain a lazily-built `node → parent` cache (HashMap). The cache is invalidated when the document version changes.

```
get_parent(node):
    1. if cache[node] exists → return cache[node]
    2. root = get_doc_root(node.arena)
    3. traverse root → children → ... until node found
    4. cache[node] = found_parent
    5. return found_parent
```

This is not needed for the initial reactive UI implementation — templates always work top-down from the matched model item (`~`). Parent access is rare in template bodies. The parent cache will be implemented when specific use cases demand it.

---

## 5. Reactive Lifecycle

### 5.1 Initial Render

```
Source Model ──→ apply() ──→ Template Matching ──→ View Tree ──→ Radiant Layout ──→ Screen
                                                      │
                                                      ├── state initialized
                                                      └── event handlers registered
```

1. `apply(model)` is called with the source data.
2. Templates are matched against the model (and recursively against children).
3. Each matched template executes its functional body, producing an element subtree.
4. State is initialized from `state` declarations.
5. Event handlers from `on` blocks are registered on the corresponding output elements.
6. The complete view tree is passed to Radiant for CSS resolution, layout, and rendering.

#### Retained Runtime Context

When rendering a reactive Lambda script via `lambda view`, the runtime context (GC heap, JIT-compiled code, name pool, script pools) is **retained** across the session rather than being destroyed after the initial evaluation. This is a key design decision:

- **No deep-copy**: The result element tree returned by `run_script_mir()` references objects directly on the GC heap. The `deep_copy` that previously copied all data to a separate output pool has been **completely removed** — it was unnecessary with GC-managed memory and harmful because it invalidated the render map's `Element*` pointers.

- **Pointer stability**: The render map stores `(source_item, template_ref) → result_node` entries where `result_node` is an `Element*` on the GC heap. DomElements hold `native_element` pointers to these same Elements. Because the runtime is retained, both the render map and the DOM tree reference the same live objects — enabling reverse lookup during event dispatch.

- **Session lifetime**: The `Runtime*` is heap-allocated and stored on `DomDocument::lambda_runtime`. It lives for the duration of the view window. The GC heap, nursery, and name pool are stored directly on the `Runtime` struct, persisting across evaluations. Event handlers execute in the same GC heap and JIT context as the initial evaluation, so they can allocate new objects, call JIT-compiled template functions, and mutate state without reinitializing the runtime.

- **Heap lifecycle**: The GC heap is created on the first evaluation and stored on `Runtime`. Subsequent evaluations (event handler re-evaluations) reuse the same heap. `runtime_cleanup()` destroys the heap, nursery, and name pool when the document is closed. For batch/test scenarios where each evaluation should be independent, `runtime_reset_heap()` destroys and NULLs the heap between runs.

### 5.2 Event Dispatch

```
User Event ──→ Radiant Event ──→ Hit Test ──→ Find Target Element
                                                     │
                                                     ▼
                                              Lookup owning template instance
                                                     │
                                                     ▼
                                              Execute on handler(evt)
                                                     │
                                              ┌──────┴──────┐
                                              │             │
                                         state mutated  model mutated
                                              │        (edit only)
                                              │             │
                                              └──────┬──────┘
                                                     │
                                                     ▼
                                              Schedule re-transform
```

1. Radiant receives a raw event (mouse, keyboard, focus) from the platform layer.
2. Hit testing identifies the target view element.
3. The framework finds the template instance that produced this element.
4. The corresponding `on` handler executes with procedural semantics.
5. Any state or model mutations are recorded.
6. A re-transform is scheduled (batched — multiple events in the same frame produce a single re-transform).

### 5.3 Observer-Based Re-transformation

Instead of React-style virtual DOM diffing, Lambda uses an **observer-based** reconciliation model. The key insight is that we already know *which* source items changed (via the DAG document model), so we can skip the expensive tree diff entirely.

#### Source-to-Result Mapping

The framework maintains a **render map** — a bidirectional mapping from source items to the result nodes they produced:

```
RenderMap = HashMap<(source_item, template_ref) → ResultEntry>

ResultEntry {
    result_nodes: Item[]       // the result node(s) produced by this template invocation
    parent_result: Item        // the parent node in the result tree that contains these nodes
    child_index: int           // insertion position within the parent's children
    dirty: bool                // whether this entry needs re-transformation
}
```

During the initial `apply()` call, as each template produces output, the framework records:
- Which `(source_item, template_ref)` pair produced which result node(s)
- Where those result nodes sit in the result tree (parent + position)

#### Two-Phase Update

When a handler mutates state or model, the update proceeds in two phases:

```
Phase 1: Mark Dirty                    Phase 2: Re-transform
┌─────────────────────┐                ┌─────────────────────────────────┐
│ State changed for   │                │ Walk result tree from root      │
│ (item, tmpl, state) │                │                                 │
│         │           │                │ For each dirty node:            │
│         ▼           │                │   1. Re-execute template body   │
│ Look up RenderMap   │                │   2. Replace old result nodes   │
│ for (item, tmpl)    │                │      with new result nodes      │
│         │           │                │   3. Update RenderMap entry     │
│         ▼           │                │   4. Schedule Radiant reflow    │
│ Mark entry dirty    │                │      for affected subtree       │
└─────────────────────┘                └─────────────────────────────────┘
```

**Phase 1 — Mark Dirty:**

1. After a handler executes, identify which `(source_item, template_ref)` pairs are affected:
   - **State mutation:** The handler's own `(model_item, template_ref)` is affected.
   - **Model mutation (edit):** The modified source node *and* any source nodes whose subtree contains the modified node may be affected. The DAG version change identifies exactly which nodes have new versions.
2. Look up each affected pair in the `RenderMap` and set `dirty = true`.

**Phase 2 — Re-transform:**

1. Walk the result tree top-down starting from the root.
2. For each node that corresponds to a dirty `RenderMap` entry:
   a. Re-execute the template body with the current state and model.
   b. Replace the old result node(s) at `(parent_result, child_index)` with the new result node(s).
   c. Update the `RenderMap` entry with the new result nodes.
   d. Feed the replacement scope to Radiant's `DirtyTracker` and `ReflowScheduler`.
3. Non-dirty subtrees are skipped entirely — no traversal, no comparison, no re-execution.

#### Why Top-Down Walk?

The top-down walk in Phase 2 ensures correct ordering when a parent and child are both dirty. The parent is re-transformed first, which may produce entirely new children — making the child's dirty mark irrelevant (it was replaced). This avoids wasted work and ensures consistency.

#### Advantages over Tree Diff

| Aspect | Tree Diff (React-style) | Observer-Based |
|--------|------------------------|----------------|
| Work per update | O(result tree size) | O(dirty nodes only) |
| Unchanged subtrees | Visited and compared | Skipped entirely |
| Knowledge of *what* changed | None — must discover via diff | Known from state/model mutation |
| Allocation | New virtual tree every render | Only dirty subtrees re-created |
| Complexity | Heuristic matching algorithms | Direct lookup + replace |

#### State-Only vs Model Changes

- **State-only change** (most common): Exactly one `RenderMap` entry is dirty — the template instance whose state was mutated. Re-transform is O(1) template executions.
- **Model change** (edit mode): The DAG version diff identifies which source nodes changed. Each changed source node's `RenderMap` entries are marked dirty. Typically a small fraction of the full tree.

---

## 5A. Central State Store

Template state is managed in a **central store** rather than being embedded in template instances. This decouples state lifetime from the view tree, enabling state preservation across re-renders, re-matches, and template hot-reloads.

### 5A.1 State Key Structure

Each state entry is keyed by a triple:

```
StateKey = (matched_model_item, template_name_or_ref, state_name)
```

| Component | Type | Description |
|-----------|------|-------------|
| `matched_model_item` | `Item` (pointer identity) | The model node that the template was matched against. Uses the node's identity in the current document version |
| `template_name_or_ref` | `const char*` or template index | Named templates use their name; anonymous templates use their definition-site reference (file + line) |
| `state_name` | `const char*` (interned) | The state variable name from the `state` declaration |

**Example:** For a template `view todo_item: <todo-item> state editing: false { ... }` matched against model node `item_3`:

```
("item_3", "todo_item", "editing") → false
("item_3", "todo_item", "editing") → true   // after click handler mutates it
```

### 5A.2 Lifecycle and Scope

| Scope | Lifetime | Description |
|-------|----------|-------------|
| **Session** (current) | Window/page open → close | State persists for the lifetime of the UI session. Released when the window/page is closed |
| **Persistent** (future) | Across sessions | File-backed or localStorage-backed state that survives window close. Reserved for future implementation |

**State initialization:**
1. When a template is first matched against a model item, the store checks for an existing entry with the same key triple.
2. If found → restore the stored value (state survives re-render).
3. If not found → initialize from the `state` declaration's default value and store it.

**State retention across template switches:**
- When a model item is re-evaluated and matches a *different* template (e.g., the item's type or attributes changed), the state entries for the **previous** template are **not deleted**. They remain in the store because the model may revert and re-match the original template.
- This means state keys for the same `model_item` but different `template_ref` values coexist in the store. This is intentional and inexpensive (state entries are small).
- Example: A `<todo-item>` switches between `view_simple` and `view_detail` templates. Each template's state (e.g., `expanded`, `editing`) persists independently.

**State cleanup:**
- When the UI session ends (window/page close), all session-scoped state entries are released.
- When a model item is no longer present in the document (e.g., deleted by an edit), its state entries become orphaned. Orphan cleanup runs lazily during the next GC cycle or can be triggered explicitly.
- Future: configurable cleanup policies (e.g., LRU eviction, max entries per item).

### 5A.3 Implementation: Extending Radiant's StateStore

The existing `StateStore` (`radiant/state_store.hpp`) already provides the right primitives — it stores `(node*, name) → Item` mappings with change callbacks and dirty tracking. The central state store extends this by:

1. **Expanding the key** from `(node, name)` to `(model_item, template_ref, name)` — a composite `StateKey`.
2. **Decoupling from DOM nodes** — the first key component is a model `Item`, not a `View*` or `DomNode*`.
3. **Leveraging existing infrastructure** — `StateChangeCallback`, `DirtyTracker` integration, and `ReflowScheduler` all carry over.

```c
// extended state key for reactive templates
typedef struct ReactiveStateKey {
    Item model_item;           // the matched model node
    const char* template_ref;  // template name (interned) or definition ref
    const char* state_name;    // state variable name (interned)
} ReactiveStateKey;
```

### 5A.4 State Access in Handlers

In `on` handler code, state variables are accessed by name directly (syntactic sugar). The transpiler resolves each state name to a state store lookup/update:

```lambda
// source code
on click() {
    expanded = !expanded    // read and write state
}

// transpiler emits (conceptual):
//   let val = state_store_get(model_item, template_ref, "expanded")
//   state_store_set(model_item, template_ref, "expanded", !val)
```

---

## 6. DAG-Based Document Model via MarkEditor

The reactive layer reuses the existing `MarkEditor` class (`lambda/mark_editor.hpp`) in `EDIT_MODE_IMMUTABLE` for all source document mutations. This avoids building a new persistent data structure from scratch.

### 6.1 MarkEditor Integration

`MarkEditor` already provides:

| MarkEditor API | Reactive Use |
|---|---|
| `EDIT_MODE_IMMUTABLE` | Copy-on-write for every model mutation |
| `map_update()`, `map_delete()` | Mutate map fields in `edit` handlers |
| `elmt_update_attr()`, `elmt_delete_attr()` | Mutate element attributes |
| `elmt_insert_child()`, `elmt_delete_child()`, `elmt_replace_child()` | Mutate element children |
| `array_set()`, `array_insert()`, `array_delete()` | Mutate arrays |
| `commit(description)` | Save version after handler completes |
| `undo()` / `redo()` | Navigate version history |
| `current()` | Get current document root |
| `get_version(n)` | Access specific version |

When an `edit` handler assigns to a model path (e.g., `~.title = "New"`), the transpiler emits calls to the appropriate `MarkEditor` method. After the handler completes, `commit()` is called automatically, capturing the edit as a named version.

### 6.2 Structural Sharing (built into MarkEditor)

```
Version 0 (original)         Version 1 (after edit)
    Root₀                         Root₁
   /    \                        /    \
  A₀     B₀         →          A₀     B₁  ← new node
 / \    / \                    / \    / \
C₀  D₀ E₀  F₀                C₀  D₀ E₀  F₁ ← changed
                                            ↑ only B and F are new allocations
```

In immutable mode, `MarkEditor` creates new nodes only for the modified element and its ancestors to the root. All other nodes are shared references. This gives O(depth) allocation per edit.

### 6.3 Undo/Redo via EditVersion Chain

`MarkEditor` maintains a doubly-linked `EditVersion` chain:

```c
typedef struct EditVersion {
    Item root;               // document root at this version
    int version_number;      // sequential version number
    const char* description; // optional description (e.g., "toggle todo")
    struct EditVersion* prev;
    struct EditVersion* next;
} EditVersion;
```

| System Function | MarkEditor Call | Effect |
|---|---|---|
| `undo()` | `editor.undo()` | Move to previous version, re-render |
| `redo()` | `editor.redo()` | Move to next version, re-render |
| (implicit after handler) | `editor.commit(desc)` | Push new version |

### 6.4 Memory Management

- `MarkEditor` operates on `Input`'s arena/pool/name_pool/shape_pool — no separate allocator needed.
- **GC integration** — The version chain acts as a GC root set. Old versions beyond a configurable limit can be pruned, making their exclusive nodes eligible for collection.
- **Strings and symbols** are interned in the name pool — no duplication across versions.

**Path-based addressing:** Each node in the DAG can be addressed by its path from the root (e.g., `/doc/section[2]/paragraph[0]`). Lambda's existing path expression syntax (`~.section[2].paragraph[0]`) maps directly to `MarkEditor` operations.

---

## 7. Integration with Radiant

### 7.1 Layout Pipeline

The existing Radiant pipeline remains unchanged. The reactive layer sits **above** it:

```
Lambda Script     Reactive Layer        Radiant
┌──────────┐    ┌──────────────┐    ┌──────────────┐
│ apply()  │───→│ Template     │───→│ CSS Resolve  │
│          │    │ Matching &   │    │ Layout       │
│ on click │←──│ Reconciler   │←──│ Render       │
│ { ... }  │    │ (diff/patch) │    │ Hit Test     │
└──────────┘    └──────────────┘    └──────────────┘
```

Templates produce standard Lambda elements (e.g., `<div>`, `<span>`, `<input>`) that Radiant already knows how to lay out and render. No changes to the layout algorithms are required.

### 7.2 Event Routing

Currently Radiant dispatches events via `script_runner.h` (`execute_document_scripts`). The reactive layer replaces this with a template-aware dispatcher:

1. Radiant receives a raw event (mouse, keyboard, focus) from the platform layer.
2. Hit testing identifies the target view element.
3. The framework finds the template instance that produced this element.
4. The corresponding `on` handler for the matching event type is invoked.
5. After handler execution, the observer marks affected entries dirty and schedules re-transformation.

**Event propagation:** Events bubble from target to root (like DOM event bubbling). A handler can call `stop()` on the event object to prevent further propagation.

#### Implementation: Event Dispatch Bridge

The event dispatch bridge connects Radiant's hit-tested DomElements back to Lambda template handlers. Key components:

**Reverse Render Map** (`render_map.cpp`):
A secondary HashMap mapping `result_item_bits → (source_item, template_ref)`, maintained alongside the forward render map. Updated during `render_map_record()` and `render_map_retransform()`. Enables O(1) lookup of which template produced a given element.

**`dispatch_lambda_handler()`** (`radiant/event.cpp`):
Called on MOUSE_UP after checkbox/select handling. Walks up the DOM tree from the hit-test target:
1. For each `DomElement`, get `native_element` (Lambda `Element*`), construct an `Item`
2. Reverse-lookup in the render map → get `(source_item, template_ref)`
3. Find `TemplateEntry` by `template_ref`, search its handler list for the event name
4. Invoke `handler_fn(source_item)` — the MIR-compiled handler loads/saves state via `tmpl_state_get`/`tmpl_state_set`
5. If `render_map_has_dirty()`, call `render_map_retransform()` followed by `rebuild_lambda_doc()`
6. Return true to prevent further event bubbling

**DOM Rebuild** (`rebuild_lambda_doc()` in `radiant/cmd_layout.cpp`):
After retransform updates the Lambda element tree, rebuilds the entire Radiant DOM:
1. `build_dom_tree_from_element()` creates new DomElements from updated Lambda elements
2. `extract_and_collect_css()` re-extracts inline `<style>` rules
3. `apply_stylesheet_to_dom_tree_fast()` re-applies CSS cascade
4. `layout_html_doc()` + `render_html_doc()` performs full relayout and repaint

**Doc Root Tree Walk** (`render_map.cpp`):
Since `fn_apply1()` records `parent=ItemNull`, retransform uses a fallback: `replace_in_element_tree()` recursively walks from `s_doc_root` to find and replace old result nodes with new ones, ensuring the Lambda element tree is updated before DOM rebuild.

```
GLFW click → mouse_button_callback (window.cpp)
  → handle_event (event.cpp)
    → hit test: target_html_doc → find DomElement target
    → MOUSE_UP handler
      → dispatch_lambda_handler(target, "click")
        → walk DOM ancestors
        → reverse_render_map_lookup → (source_item, template_ref)
        → find TemplateEntry → find "click" handler
        → handler_fn(source_item) → tmpl_state_set → render_map_mark_dirty
        → render_map_retransform → re-execute body → replace in element tree
        → rebuild_lambda_doc → build DOM → CSS → layout → render
```

### 7.3 Incremental Repaint

The observer-based reconciler integrates with Radiant's existing `DirtyTracker` and `ReflowScheduler`:

- **State-only re-transform** (same structure, updated content) → `REFLOW_SELF_ONLY` or `REFLOW_CHILDREN`
- **Child count changed** (template produces more/fewer result nodes) → `REFLOW_CHILDREN` or `REFLOW_SUBTREE`
- **Structural changes** (template now matches a different pattern, or model node type changed) → `REFLOW_SUBTREE`
- Each result node replacement adds appropriate `DirtyRect` entries for the affected region.

---

## 8. Template Matching and Dispatch

### 8.1 XSLT-Style Pattern Matching

Templates are selected based on their model pattern. This uses Lambda's existing type/pattern matching system:

```lambda
// matches any element with tag 'section'
view <section> {
    <div class: "section";
        ~.title | apply
        for child in ~.children do apply(child)
    >
}

// matches a map with a 'name' field of type string
view { name: string, age: int } {
    <span class: "person"; ~.name ", age " ~.age>
}

// matches any array
view array {
    <ul;
        for item in ~ do <li; apply(item)>
    >
}

// matches specific element by tag and attribute
view <todo done: bool> {
    <div class: if ~.done then "done" else "pending";
        ~.text
    >
}

// union pattern — matches multiple element types
view <p> | <span> | <code> {
    <div class: "inline"; ~>
}
```

### 8.2 Specificity (current scope)

For the initial implementation, specificity is determined by pattern structure:

| Specificity | Rule | Example |
|-------------|------|---------|
| 1 (highest) | Named template invoked explicitly | `apply(item, {template: 'detail_view'})` |
| 2 | Element tag + attribute pattern | `<section title: string>` |
| 3 | Element tag pattern | `<section>` |
| 4 | Structural map pattern | `{name: string, age: int}` |
| 5 | Simple type pattern | `string` |
| 6 (lowest) | Catch-all | `any` |

Within the same specificity level:
- More fields in a map pattern → higher specificity.
- More specific types win (e.g., `int` beats `number`, `number` beats `any`).
- If still tied, the template defined **later** in the script wins (last-match-wins, like CSS).

### 8.2.1 Future: Priority and Mode (XSLT-style)

Full XSLT-style `priority` and `mode` support is planned as a future extension:

```lambda
// explicit numeric priority (higher wins)
view <section> priority: 10 { ... }

// named mode — only matched when apply() specifies the same mode
view <section> mode: 'print' {
    // print-specific rendering, no interactive elements
    <div class: "print-section"; ~.title apply; >
}
view <section> mode: 'screen' {
    // screen-specific rendering with interactive controls
    <div class: "section"; ~.title <button; "Edit"> apply; >
}

// apply with mode selection
apply(doc, {mode: 'print'})  // uses print templates
apply(doc, {mode: 'screen'}) // uses screen templates
```

| Feature | Description | Status |
|---------|-------------|--------|
| `priority: number` | Explicit numeric priority; overrides pattern-based specificity | Future |
| `mode: symbol` | Named mode; template only matches when `apply()` specifies the same mode | Future |
| Default mode | Templates without `mode:` match any mode (or a default unnamed mode) | Future |

### 8.3 Recursive Application and `apply;` Statement

By default, `apply()` recurses into the result tree. When a child element in the template output does not have a matching template, it is passed through as-is. This mirrors XSLT's default template behavior.

#### `apply;` Statement

Inside a `view`/`edit` template body, the bare **`apply;`** statement applies templates to all child nodes of the current model item (`~`). This is the primary mechanism for recursive descent:

```lambda
view <article> {
    <div class: "article";
        <h1; ~.title>
        apply;   // apply templates to all children of ~
    >
}
```

**Semantics of `apply;`:**
- Equivalent to `for child in children(~) do apply(child)`
- Inherits the current mode (`view`/`edit`), params, and options from the enclosing `apply()` call
- Each child is matched against all registered templates; the best match is selected per §8.2
- Children with no matching template are passed through as-is (identity transform)
- Only valid inside a `view`/`edit` body — compile error if used elsewhere

`apply;` is a **statement**, not an expression. It produces output elements inline within the template body, just like any other statement in a functional block.

#### Explicit Child Application

For finer control, use `apply(child)` on specific children:

```lambda
view <article> {
    <div class: "article";
        <h1; ~.title>
        // explicitly apply templates to each section only
        for sec in ~.section do apply(sec)
    >
}
```

---

## 9. Grammar Extensions

The following Tree-sitter grammar rules need to be added to `lambda/tree-sitter-lambda/grammar.js`:

```javascript
// New top-level declarations
view_stam: $ => seq(
    field('kind', choice('view', 'edit')),
    optional(seq(field('name', $.identifier), ':')),  // name: (colon disambiguates)
    field('pattern', $.view_pattern),   // bare pattern — no brackets
    optional(seq(                        // () optional unless return type present
        '(', optional(seq(
            field('declare', $.parameter),
            repeat(seq(',', field('declare', $.parameter)))
        )), ')',
        optional(field('type', $.return_type)),
    )),
    optional(field('state', $.state_decl)),
    '{', field('body', $.content), '}',
    repeat(field('handler', $.event_handler)),
),

// View/edit pattern — element, map, type name, or union
view_pattern: $ => seq(
    field('alt', $.view_pattern_atom),
    repeat(seq('|', field('alt', $.view_pattern_atom))),
),

view_pattern_atom: $ => choice(
    $.element_pattern,    // <tag attr: type, ...>
    $.map_pattern,        // {field: type, ...}
    $.identifier,         // type or pattern name
),

// State declarations
state_decl: $ => seq(
    'state',
    field('entry', $.state_entry),
    repeat(seq(',', field('entry', $.state_entry))),
),

state_entry: $ => seq(
    field('name', $.identifier),
    ':',
    field('value', $._expr),  // must be a constant/literal
),

// Event handler block
event_handler: $ => seq(
    'on',
    field('event', $.identifier),  // built-in event name OR user-defined name
    '(', optional(field('declare', $.parameter)), ')',
    '{', field('body', $.content), '}',
),

// apply; statement — only valid inside view/edit body
apply_stam: $ => seq('apply', ';'),
```

**New keywords to reserve:** `view`, `edit`, `state`, `on`

**New statement rule:**
- Add `$.apply_stam` to `$.content` statement choices (only valid inside a `view`/`edit` body — enforced during semantic analysis, not in the grammar).

**Integration points:**
- Add `$.view_stam` to the top-level statement choices alongside `$.fn_stam`.
- `state` becomes a contextual keyword (only meaningful after `view`/`edit` parameter list).
- `on` becomes a contextual keyword (only meaningful after a `view`/`edit` body block).
- Handler event names are plain identifiers — no distinction between built-in and user-defined at the grammar level.

---

## 10. Examples

### 10.1 Simple Read-Only Viewer

```lambda
// todo-viewer.ls — displays a todo list

view <todo-list> {
    <div class: "todo-app";
        <h1; "Todo List">
        <ul;
            for item in ~.items do apply(item)
        >
    >
}

view <todo-item> state expanded: false {
    <li class: if ~.done then "done" else "";
        <span class: "text"; ~.text>
        if expanded then <span class: "detail"; ~.detail>
    >
}
on click() {
    expanded = !expanded
}

// entry point
let data = read("todos.json")
apply(data) | render(~, "todos.svg")
```

### 10.2 Interactive Editor with Undo

```lambda
// todo-editor.ls — editable todo list with undo/redo

edit <todo-list> state filter: 'all' {
    <div class: "todo-app";
        <h1; "Todo Editor">
        <div class: "toolbar";
            <button id: "add"; "Add">
            <button id: "undo"; "Undo">
            <button id: "redo"; "Redo">
            <select id: "filter";
                <option value: "all"; "All">
                <option value: "active"; "Active">
                <option value: "done"; "Done">
            >
        >
        let visible = match filter {
            case 'all':    ~.items
            case 'active': ~.items where !~.done
            case 'done':   ~.items where ~.done
        };
        <ul;
            for item in visible do apply(item)
        >
        <div class: "footer";
            count(~.items where ~.done) " of " count(~.items) " completed"
        >
    >
}
on click(evt) {
    match evt.target.id {
        case "add":  ~.items = ~.items ++ [<todo-item done: false; "New item">]
        case "undo": undo()
        case "redo": redo()
    }
}
on change(evt) {
    if evt.target.id == "filter" then
        filter = to_symbol(evt.target.value)
}

edit <todo-item> state editing: false {
    <li class: if ~.done then "done" else "";
        <input type: "checkbox", checked: ~.done>
        if editing then
            <input type: "text", value: ~.text>
        else
            <span class: "text"; ~.text>
    >
}
on click(evt) {
    if evt.target.type == "checkbox" then
        ~.done = !~.done
    else
        editing = true
}
on input(evt) {
    ~.text = evt.target.value
}
on focusout() {
    editing = false
}

// entry point
pn main() {
    var data = read("todos.json")
    apply(data, {mode: 'edit'})
}
```

### 10.3 Data Visualization Viewer

```lambda
// chart-viewer.ls — transform data into SVG chart

view chart: <dataset> (width: int = 600, height: int = 400) {
    let max_val = max(~.values);
    let bar_w = width / count(~.values);
    <svg width: width, height: height;
        for (val, i) in enumerate(~.values) do
            let bar_h = val / max_val * height;
            <rect
                x: i * bar_w,
                y: height - bar_h,
                width: bar_w - 2,
                height: bar_h,
                fill: ~.color ?? "steelblue"
            >
    >
}

view <dataset> state hover_idx: null {
    <div class: "chart-container";
        apply(~, {template: 'chart'}, 800, 500)
        if hover_idx != null then
            <div class: "tooltip";
                ~.labels[hover_idx] ": " ~.values[hover_idx]
            >
    >
}
on mousemove(evt) {
    // calculate which bar index the mouse is over
    let bar_w = 800 / count(~.values)
    hover_idx = floor(evt.x / bar_w)
}
on mouseout() {
    hover_idx = null
}
```

---

## 11. Implementation Phases

### Phase 1: Language, Grammar, Template Dispatch & View Rendering ✅

| Task | Description | Status |
|------|-------------|--------|
| Grammar rules | Add `view_stam`, `view_pattern`, `state_decl`, `event_handler` to `grammar.js` | ✅ Done |
| AST nodes | Add `VIEW_STAM`, `VIEW_PATTERN`, `STATE_DECL`, `EVENT_HANDLER` to `ast.hpp` and `build_ast.cpp` | ✅ Done |
| Parser regen | `make generate-grammar` | ✅ Done |
| Template registry | Collect all `view`/`edit` definitions at script load time | ✅ Done |
| Pattern compiler | Compile model patterns into efficient match predicates | ✅ Done |
| `apply()` sys func | Implement dispatch algorithm with specificity ranking | ✅ Done |
| MIR codegen | Transpile template bodies to MIR | ✅ Done |
| View rendering | Render the output element tree via Radiant | ✅ Done |

### Phase 2: State Management, Event Handling & View Update ✅

| Task | Description | Status |
|------|-------------|--------|
| Central state store | Implement state store keyed by `(model_item, template_ref, state_name)` | ✅ Done (`template_state.cpp`) |
| State mutation | Compile handler assignments to state store updates via `tmpl_state_set` | ✅ Done |
| Event dispatch | Route Radiant events to correct template instance and handler | ✅ Done (`dispatch_lambda_handler`) |
| Reverse render map | O(1) lookup from result_node → (source_item, template_ref) | ✅ Done (`render_map.cpp`) |
| DOM rebuild | Full DOM + CSS + layout rebuild after state change | ✅ Done (`rebuild_lambda_doc`) |
| MIR codegen (handlers) | Transpile `on` handler bodies to MIR | ✅ Done |

### Phase 3: Observer-Based Reconciliation ✅

| Task | Description | Status |
|------|-------------|--------|
| Render map | Implement `RenderMap` — `HashMap<(source_item, template_ref) → ResultEntry>` | ✅ Done |
| Map population | During `apply()`, record source→result mappings | ✅ Done |
| Dirty marking | `tmpl_state_set` → `render_map_mark_dirty` | ✅ Done |
| Top-down re-transform | Re-execute dirty template bodies, replace result nodes in element tree | ✅ Done |
| Doc root tree walk | Fallback parent fixup via `replace_in_element_tree` | ✅ Done |

### Phase 4: MarkEditor Integration for DAG Document Model ✅

| Task | Description | Status |
|------|-------------|--------|
| MarkEditor bridge | Wire `edit` handler assignments to `MarkEditor` methods | ✅ Done |
| Auto-commit | `edit` templates call `editor.commit()` after handler completes | ✅ Done |
| `undo()` / `redo()` | Expose as Lambda system functions | ✅ Done |

### Phase 5: Polish & Tooling

| Task | Description | Status |
|------|-------------|--------|
| Devtools | Template inspector, state viewer, event log | Not started |
| Hot reload | Re-parse templates without losing state | Not started |
| Performance | Memoization of pure template bodies, skip unchanged subtrees | Not started |
| Keyed list reconciliation | Position-independent identity for list items | Not started |
| Incremental reflow | Feed `DirtyRect` to Radiant instead of full DOM rebuild | Not started |
| Documentation | Language reference, tutorials, examples | In progress |
| Tests | Baseline tests for template matching, state, events | ✅ Done |

---

## 12. Open Questions and Suggestions

### Questions

| # | Question | Resolution / Options |
|---|----------|---------|
| Q1 | **Context item binding in handlers** — `~` refers to the matched model item in both the body and all `on` handlers. | **Resolved:** `~` is consistent everywhere within a template. No alternative binding needed. |
| Q2 | **State persistence across template switches** — When a model item switches templates, previous template's states are retained in the central store. | **Resolved:** States persist per `(model_item, template_ref, state_name)`. See §5A.2. |
| Q3 | **Cross-template communication** — User-defined handlers + `emit()` provide one channel. `page.id.handler()` provides another via the `page` keyword. | **Partially resolved.** Shared state store keys (future) could add a third option. |
| Q4 | **Async operations in handlers** — How to handle network requests, timers, or file I/O? | A) `async on fetch_data()`, B) Separate `effect` blocks, C) Callback-based with state update |
| Q5 | **CSS-in-Lambda** — Should templates inline CSS or reference external stylesheets? | A) External CSS only, B) `style` block in template, C) Both |

### Suggestions

1. **Memoization of pure template bodies** — Since the functional body is pure, if neither the model subtree nor the state has changed, the previous output can be reused without re-execution. This is analogous to React's `useMemo` / `React.memo` but automatic.

2. **Derived/computed state** — Consider adding a `computed` keyword for values derived from state+model that are cached and auto-invalidated:
   ```lambda
   view <todo-list> state filter: 'all', computed visible: ~.items where match_filter(~, filter) {
       ...
   }
   ```

3. **Template fragments** — Allow templates to return multiple root elements (fragments) without a wrapping container, avoiding unnecessary DOM nesting.

4. **Conditional event handlers** — Allow `on` blocks to be guarded:
   ```lambda
   on keydown(evt) if evt.mods & CTRL {
       // only fires for Ctrl+key combinations
   }
   ```

5. **Animation support** — Consider a `transition` or `animate` attribute on elements that interpolates CSS properties over time when they change:
   ```lambda
   <div class: "panel", style: "height: " ++ if expanded then "200px" else "0px",
        transition: "height 0.3s ease">
   ```

6. **Slot/children forwarding** — For composite templates, allow a template to accept child content:
   ```lambda
   view card: <card> {
       <div class: "card";
           <div class: "card-header"; ~.title>
           <div class: "card-body"; apply;>
       >
   }
   ```

7. **Error boundaries** — Like React error boundaries, allow a template to catch errors from child templates and render a fallback:
   ```lambda
   view <app> state error: null {
       if error != null then
           <div class: "error"; "Something went wrong: " ++ error.message>
       else
           apply;
   }
   on error(err) {
       error = err
   }
   ```

8. **Server-side rendering (SSR)** — The functional body produces a pure element tree with no dependency on the event system. This means `apply()` can run without Radiant for static rendering (HTML, SVG, PDF output), and the same templates can be used for both interactive and static output.

9. **Keyed list reconciliation** — For list re-ordering, the observer model can use a `key` attribute to match old and new children by identity rather than position, minimizing DOM moves:
   ```lambda
   for item in ~.items do
       <li key: item.id; apply(item)>
   ```
   With the observer model, each `<li>` has its own `RenderMap` entry keyed by the source `item`. When the list order changes, the framework can reorder existing result nodes rather than re-transforming them.

10. **Model change notifications** — For `edit` templates, consider providing an `on_model_change` callback or event so that the application can persist changes, sync with a server, or update other dependent views:
    ```lambda
    apply(data, {mode: 'edit', on_change: pn(new_model) { save("data.json", new_model) }})
    ```
