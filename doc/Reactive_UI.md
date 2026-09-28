# Reactive UI in Lambda

Lambda's reactive UI combines XSLT-style template matching with React-style component state. A **template** is a pure function from a data item to an element tree; **state** is local storage kept per template instance; and **`on` handlers** are procedures that react to events by changing that state or, in an `edit` template, the model itself. After a handler runs, only the templates whose inputs changed are re-run, and the rendered document is patched in place.

> **Try it:** `./lambda.exe view test/lambda/ui/todo.ls` opens a reactive todo list; `test/lambda/ui/todo2.ls` adds a second panel, inline editing, drag-and-drop and file management. The examples below follow those scripts. A template body is `fn` context and its handlers are `pn` context (S12.1.3).

## Templates: `view` and `edit`

A template is declared at the top level of a script with a **pattern**, optional **state**, a **body**, and any number of **`on` handlers** after the body:

```lambda
view <todo_item> state toggled: false {
  let done = if (toggled) (not ~.done) else ~.done;
  <li class:(if (done) "todo-item done" else "todo-item"),
    <span class:"checkbox", if (done) "✓" else "○">
    <span class:"todo-text", ~.text>
  >
}
on click(evt) {
  toggled = not toggled
}

apply(<todo_item text: "Write the docs", done: false>)
```

- **`view`** is read-only: it may hold local state, but its handlers cannot mutate the matched model.
- **`edit`** is read-write: its handlers may assign into the model (`~.items = …`). An `edit` template is selected only when `apply` is called with `{mode: "edit"}`.
- The body is a pure transformation, model → element tree, and `~` is the matched item. Mutation happens only inside `on` handlers, which are procedures.
- The ordinary line-continuation rule applies inside the body: a `let` line that is followed by an element literal ends with `;`, because a line starting with `<` would otherwise continue it (see [Lambda_Syntax.md](Lambda_Syntax.md#line-continuation)).

### Patterns

| Pattern | Matches |
|---|---|
| `<todo_item>` | elements with tag `todo_item` |
| `<input type: 'checkbox'>` | elements with that tag whose attributes carry the pinned values |
| `{name: string}` | maps — currently by field **count** only, not by field name (a known gap) |
| a simple type such as `string` | values of that type |

When several templates match, the most specific wins: a named template, then an element pattern with attributes, then an element tag, then a map or simple type, then a catch-all. Ties go to the pattern with more pinned attributes, then more constraints, then the template defined last.

### `apply()`

`apply(item, options?)` dispatches one data item to the best-matching template and returns the element tree it produced. It is how a parent template renders its children without naming them, in the way `xsl:apply-templates` does:

```lambda
edit <todo_list> state new_text: "" {
  <ul class:"items",
    for (item in ~.items)
      apply(<todo_item text: item.text, done: item.done, id: item.id>)
  >
}

apply(<todo_list items: [{text: "a", done: false, id: 1}]>, {mode: "edit"})
```

Options: `{mode: "edit"}` makes `edit` templates eligible; `{template: "name"}` selects a named template directly.

### State

`state` declares per-instance local values, initialized once per source item: `state toggled: false, editing: false, edit_text: ""`. Each template instance — one per item `apply` was called with — has its own copy. The body reads state as ordinary names and handlers assign to it. A state entry written without an initializer binds engine-owned state whose value the host maintains.

### Event Handlers

`on <event>(evt) { … }` blocks follow the body. The handler runs as a procedure with `~` bound to the matched item and `evt` to the event:

| `evt` field | Meaning |
|---|---|
| `type` | the event name |
| `target_tag`, `target_class`, `target_parent_class`, `target_text` | the element that received the event |
| `key`, `shift`, `ctrl`, `alt`, `meta` | keyboard events |
| `char`, `caret_pos`, `selection_start`, `selection_end` | text input events |
| `text` | pasted text |

Handled events: `click`, `input`, `keydown`, `paste`, `cut`, `blur`, `dragstart`, `dragover`, `dragleave`, `drop`, `dragend`, and any custom name raised with `emit`. A handler may return `'prevent-default'` to suppress the engine's default action (form submission, the built-in text edit) or `'pass'` to let it through.

```lambda
view <todo_item> state toggled: false {
  <li class:"todo-item", <span class:"delete-btn", "×"> <span class:"todo-text", ~.text>>
}
on click(evt) {
  if (evt.target_class == "delete-btn") {
    emit("delete_item", ~)
    return
  }
  toggled = not toggled
}
on keydown(evt) {
  if (evt.key == "Enter") { return 'prevent-default' }
  'pass'
}
```

`emit(name, payload)` raises a custom event that travels up the rendered tree to the nearest enclosing template with an `on name` handler; there the payload arrives as `evt`. This is how a child asks its parent to change the model without either naming the other:

```lambda
edit <todo_list> {
  <ul for (item in ~.items) apply(<todo_item text: item.text, id: item.id>)>
}
on delete_item(evt) {
  ~.items = [for (item in ~.items where item.id != evt.id) item]
}
```

## The Reactive Loop

1. An input event is hit-tested to an element, which maps back to the template instance that rendered it, and that instance's handler runs.
2. The handler changes state (`view`) or the model (`edit`); every change marks the affected template instances dirty.
3. Only dirty template bodies are re-executed. A body whose result is unchanged is skipped; changed results are swapped into the document in place, and only the affected subtree is re-cascaded and re-laid out. A full document rebuild is the fallback, not the normal path.

Templates and handlers are ordinary Lambda functions: they are compiled once when the script loads and run on the interpreter or the MIR JIT like any other function.

## Comparison with Prior Art

### vs. XSLT

Lambda's template system draws directly from XSLT's model of declarative, pattern-matched transformations over structured data.

| | XSLT | Lambda |
|---|---|---|
| **Paradigm** | Declarative transformation rules | Functional transformation with procedural handlers |
| **Matching** | `<xsl:template match="todo_item">` | `view <todo_item> { ... }` |
| **Dispatch** | `<xsl:apply-templates select="item"/>` | `apply(<todo_item ...>)` |
| **Specificity** | Priority attribute + import precedence | Pattern specificity (named > attributes > tag > type), then pinned attributes and constraints |
| **Data model** | XML nodes (DOM/infoset) | Lambda element tree (tagged values) |
| **State** | None — pure transformation | `state` declarations per template instance |
| **Events** | None — batch transform only | `on click`, `on input`, `on keydown`, …, `emit()` |
| **Reactivity** | None — re-run entire stylesheet | Dirty tracking, selective re-execution, in-place patching |
| **Output** | XML/HTML/text serialization | Live rendered UI via the Radiant layout engine |

XSLT is a batch transformation language: source → stylesheet → output document, done. Lambda keeps the transformation live — the template registry persists, and events trigger targeted re-execution of individual template bodies.

The key thing Lambda borrows from XSLT is **separation of data and presentation via pattern matching**. Templates don't know where they'll be used; they declare what shape of data they handle and how to present it. The `apply()` mechanism decouples parent templates from child template implementations, just as `xsl:apply-templates` does. What XSLT lacks is any notion of interactivity; Lambda adds local state and event handlers while preserving the declarative body.

### vs. React

Lambda's component model resembles React functional components, but differs in data flow and architecture.

| | React | Lambda |
|---|---|---|
| **Component definition** | `function Todo({ item }) { ... }` | `view <todo_item> { ... }` |
| **State** | `useState()` hook, per instance | `state name: val` declaration, per (source item, template) |
| **Event handling** | `onClick={handler}` inline JSX | `on click(evt) { ... }` block after the body |
| **Re-render trigger** | `setState()` → re-render component subtree | state or model write → mark dirty → retransform |
| **Reconciliation** | Virtual DOM diff (fiber tree) | Re-execute dirty template bodies; patch changed results in place |
| **Data binding** | Props down, callbacks up | Pattern matching down, `emit()` up |
| **Compilation** | Babel/SWC → JS bundles, JIT by V8 | Lambda parser → interpreter / MIR JIT |
| **Model mutation** | Immutable state convention + reducers | `edit` templates mutate in place; `view` is read-only |
| **Component selection** | Explicit JSX tag: `<Todo item={x}/>` | `apply()` dispatches by pattern match |

React components are explicitly referenced by name in JSX. Lambda uses structural pattern matching — you `apply()` a data item and the framework selects the template, similar to method dispatch in OOP or XSLT's `apply-templates`. This makes templates more reusable and loosely coupled, at the cost of less explicit control flow.

Both systems share the principle that **the view is a function of state**: the template body is re-executed with the same source item, and whatever state has changed produces a different element tree. Neither system requires imperative DOM manipulation.

### Summary: Where Lambda Sits

- **From XSLT**: pattern-matched template dispatch over structured data, recursive `apply()`, specificity-based selection.
- **From React**: per-instance component state, event-driven re-rendering, functional transformation bodies.
- **Unique to Lambda**: one data model in which elements serve as both model and view, the `edit` vs `view` distinction enforcing read/write discipline, and `emit()` for cross-template events without prop drilling.
