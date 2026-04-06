# Reactive UI in Lambda

Lambda's reactive UI combines XSLT-style template matching with React-style component state, applied to a functional scripting language with JIT compilation.

## Core Concepts

### Templates: `view` and `edit`

Templates are top-level constructs that transform source data into presentation elements.

```lambda
view <todo_item> state toggled: false {
  let done = if (toggled) (!~.done) else ~.done
  <li class:(if (done) "todo-item done" else "todo-item")
    <span class:"checkbox"; if (done) "✓" else "○">
    <span class:"todo-text"; ~.text>
  >
}
on click() {
  toggled = not toggled
}
```

- **`view`** — Read-only. Can hold local UI state but cannot mutate the source model.
- **`edit`** — Read-write. Handlers can mutate the matched model in-place.

The template body is a **pure functional transformation**: model → element tree. Mutation only happens inside `on` handlers using procedural semantics.

### Pattern Matching and `apply()`

Templates declare a **pattern** that matches source data by structure:

```lambda
view <todo_item>           // matches elements with tag "todo_item"
edit <todo_list>           // matches elements with tag "todo_list"
view {name: string}        // matches maps with a "name" field
```

The `apply()` system function dispatches a source item to the best-matching template:

```lambda
for (item in items)
  apply(<todo_item text:item.text, done:item.done>)
```

When multiple templates match, the most specific pattern wins (analogous to XSLT priority).

### State

Each template instance (one per source item) has isolated local state declared with `state`:

```lambda
view <todo_item> state toggled: false, expanded: true { ... }
```

State is keyed by `(source_item, template_ref, state_name)` in a central store. Reading state in the body calls `tmpl_state_get_or_init()`; writing state in handlers calls `tmpl_state_set()`.

### Event Handlers

`on` blocks after the template body define event handlers:

```lambda
on click(evt) {
  if (evt.target_class == "delete-btn")
    emit("delete_item", ~)
  else
    toggled = not toggled
}
```

Handlers receive the matched model as `~` and an optional event object with `{type, target_class, target_tag, x, y}`. Cross-template communication uses `emit(event_name, data)`, which walks up the DOM ancestry to find a parent template with a matching handler.

## Reactive Loop

```
User event (click, input, keydown)
  → hit-test → find target DomElement
  → DomElement.native_element → reverse render map lookup
  → find (source_item, template_ref)
  → invoke JIT-compiled handler(source_item, event)
  → handler mutates state (view) or model (edit)
  → render_map_mark_dirty(source_item, template_ref)
  → render_map_retransform: re-execute only dirty template bodies
  → replace result in element tree
  → full DOM rebuild → CSS cascade → layout → render
```

### Dirty Tracking

The render map stores `(source_item, template_ref) → result_node` for every `apply()` call. When a handler mutates state or model, the corresponding entry is flagged `dirty = true`. Retransform iterates all entries but only re-executes dirty ones — if 8 todo items exist and 1 is clicked, only that 1 template body re-runs.

### Compilation

Templates and handlers are JIT-compiled once via MIR during script transpilation. Each event invokes the already-compiled function pointer — no re-parsing or re-compilation occurs at runtime.

## Comparison with Prior Art

### vs. XSLT

Lambda's template system draws directly from XSLT's model of declarative, pattern-matched transformations over structured data.

| | XSLT | Lambda |
|---|---|---|
| **Paradigm** | Declarative transformation rules | Functional transformation with procedural handlers |
| **Matching** | `<xsl:template match="todo_item">` | `view <todo_item> { ... }` |
| **Dispatch** | `<xsl:apply-templates select="item"/>` | `apply(<todo_item ...>)` |
| **Specificity** | Priority attribute + import precedence | Pattern specificity (structural depth) |
| **Data model** | XML nodes (DOM/infoset) | Lambda element tree (tagged values) |
| **State** | None — pure transformation | `state` declarations per template instance |
| **Events** | None — batch transform only | `on click`, `on input`, `on keydown`, `emit()` |
| **Reactivity** | None — re-run entire stylesheet | Dirty tracking, selective re-execution |
| **Output** | XML/HTML/text serialization | Live rendered UI via Radiant layout engine |

XSLT is a batch transformation language: source → stylesheet → output document, done. Lambda keeps the transformation live — the template registry persists, and events trigger targeted re-execution of individual template bodies.

The key thing Lambda borrows from XSLT is **separation of data and presentation via pattern matching**. Templates don't know where they'll be used; they declare what shape of data they handle and how to present it. The `apply()` mechanism decouples parent templates from child template implementations, just as `xsl:apply-templates` does.

What XSLT lacks is any notion of interactivity. Lambda adds local state and event handlers while preserving the declarative body.

### vs. React

Lambda's component model resembles React functional components, but differs in data flow and architecture.

| | React | Lambda |
|---|---|---|
| **Component definition** | `function Todo({ item }) { ... }` | `view <todo_item> { ... }` |
| **State** | `useState()` hook, per-instance | `state name: val` declaration, per-(source, template) |
| **Event handling** | `onClick={handler}` inline JSX | `on click(evt) { ... }` block after body |
| **Re-render trigger** | `setState()` → re-render component subtree | `tmpl_state_set()` → mark dirty → retransform |
| **Reconciliation** | Virtual DOM diff (fiber tree) | Re-execute dirty template body, full DOM rebuild |
| **Data binding** | Props down, callbacks up | Pattern matching down, `emit()` up |
| **Compilation** | Babel/SWC → JS bundles, JIT by V8 | Tree-sitter parse → MIR JIT → native code |
| **Model mutation** | Immutable state convention + reducers | `edit` templates mutate in-place; `view` is read-only |
| **Component selection** | Explicit JSX tag: `<Todo item={x}/>` | `apply()` dispatches by pattern match |

React components are explicitly referenced by name in JSX. Lambda uses structural pattern matching — you `apply()` a data item and the framework selects the template, similar to method dispatch in OOP or XSLT's `apply-templates`. This makes templates more reusable and loosely coupled, at the cost of less explicit control flow.

React's virtual DOM diff compares old and new component trees to compute minimal DOM patches. Lambda currently does a full DOM rebuild after retransform — acceptable for small apps but will need incremental patching for larger UIs.

Both systems share the principle that **the view is a function of state**: the template body is re-executed with the same source item, and whatever state has changed produces a different element tree. Neither system requires imperative DOM manipulation.

### Summary: Where Lambda Sits

Lambda's reactive UI occupies a unique position:

- **From XSLT**: Pattern-matched template dispatch over structured data, recursive `apply()`, specificity-based selection.
- **From React**: Per-instance component state, event-driven re-rendering, functional transformation bodies.
- **Unique to Lambda**: JIT-compiled templates (MIR), unified data model (elements serve as both model and view), `edit` vs `view` distinction enforcing read/write discipline, `emit()` for cross-template events without prop drilling.
