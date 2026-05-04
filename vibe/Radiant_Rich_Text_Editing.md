# Radiant Rich Text Editing — Design Proposal

**Date:** 2026-05-04
**Status:** Proposal
**Prerequisites:** Reactive_UI Phases 1–26 (Reactive_UI.md … Reactive_UI5.md), MarkEditor (Mark_Editor2.md), DomRange (Radiant_Design_Selection.md), text_control (`textarea`/`input` editing).

---

## 1. Objective

Promote Lambda/Radiant from "string-level form-control editing" (single-line `<input>`, plain-text `<textarea>`) to **structured rich-text editing** over arbitrary Mark documents. The user should be able to:

- Open a `.md`, `.html`, `.tex`, `.wiki`, or any Mark/Lambda tree.
- Click into the rendered page and edit it as a *first-class structured document* — typing, splitting paragraphs, toggling bold, inserting links, lists, tables, images.
- Have every keystroke produce a typed, undoable transaction over the **source Mark tree**, not the rendered DOM.
- Round-trip back to the original (or any) format via Lambda's existing formatters.

The design borrows the rigorous parts of **ProseMirror** (schema, transactions/steps, OT-friendly), the ergonomic parts of **Slate.js** (path-based positions, JSON-native model), and integrates them with what Lambda already has.

> **Non-goals (this proposal):** collaborative editing (OT/CRDT) — designed-for but deferred; full IME / RTL / complex-script shaping beyond what Radiant text already supports; WYSIWYG print/PDF editing.

---

## 2. What Lambda Already Has

The reactive UI work has already built ~70% of the substrate. We are not starting from zero.

| Capability | Where | Role in rich-text editing |
|---|---|---|
| Universal source tree (Mark/Lambda elements) | `lambda-data.hpp`, parsers under `lambda/input/` | The **document model** (PM "Node", Slate "Element") |
| `MarkEditor` with versioned immutable mode | `mark_editor.hpp/.cpp`, `Mark_Editor2.md` | Atomic, undoable mutations on the Mark tree |
| `edit_bridge` C ABI | `edit_bridge.{h,cpp}` | JIT-compiled edit handlers can call typed mutation ops |
| `view` / `edit` templates with pattern dispatch | `template_registry.{h,cpp}`, `Reactive_UI.md §4–8` | Source-tree → result-tree transformation |
| `render_map` (source ↔ result reconciliation) | `render_map.{h,c}` | PM-style "decorations" / Slate "rendering" — tells us which DOM node came from which source node |
| `DomBoundary` / `DomRange` / `DomSelection` | `dom_range.hpp`, `Radiant_Design_Selection.md` | W3C-conformant selection over the *rendered* DOM |
| `state_store` (`CaretState`, `SelectionState`, dirty/reflow scheduling) | `state_store.hpp` | Visual caret + incremental repaint |
| `text_control` (input / textarea editing, UTF-16↔UTF-8, clipboard) | `text_control.{hpp,cpp}` | Plain-text editing primitives we will lift to structured editing |
| Format parsers (Markdown, HTML, Wiki, LaTeX, …) and formatters | `lambda/input/`, `lambda/format/` | Load and save the source document |

What is **missing** is the conceptual layer that ties these together for rich text:

1. A **content schema** declaring which Mark elements are blocks vs. inlines vs. marks, and what may nest in what.
2. A **canonical position model** that addresses points *inside* the source tree (not just the DOM tree) and survives template re-execution.
3. A **transaction layer** — typed, composable steps over the Mark tree — that subsumes today's `MarkEditor` calls.
4. **Input mapping**: keystrokes / paste / IME / contenteditable `beforeinput` events → transactions on the source.
5. A **bidirectional binding** between source positions and DOM positions so the visual caret and the model caret stay in sync across re-renders.

The rest of this document specifies those five layers.

---

## 3. Architecture Overview

```
        ┌─────────────────────────────────────────────────────────────┐
        │  Source document  ── Mark/Lambda tree (the canonical truth) │
        │  (parsed from .md / .html / .tex / .wiki / .ls / live edit) │
        └──────────────┬───────────────────────────────────▲──────────┘
                       │                                   │
              parse / load                          format / save
                       │                                   │
                       ▼                                   │
        ┌──────────────────────────────────┐               │
        │  Schema  ── declares block /     │               │
        │  inline / mark roles & content   │               │
        │  expressions per element tag     │               │
        └──────────────┬───────────────────┘               │
                       │                                   │
                       │ validate                          │
                       │                                   │
                       ▼                                   │
        ┌──────────────────────────────────┐    ┌─────────────────────┐
        │  view / edit templates           │    │  Transaction        │
        │  (Reactive_UI §4–8)              │    │  (Step list)        │
        │   apply()  ─────────────────────►│◄───│  apply_steps()      │
        └──────────────┬───────────────────┘    │  (rebases on conf-  │
                       │ result tree            │   licting concur-   │
                       │                        │   rent edits)       │
                       ▼                        └─────────▲───────────┘
        ┌──────────────────────────────────┐              │
        │  render_map (source↔result)      │              │
        │  + Radiant DOM + layout          │              │
        └──────────────┬───────────────────┘              │
                       │ paints                           │
                       ▼                                  │
        ┌──────────────────────────────────┐              │
        │  Window / event.cpp              │              │
        │  beforeinput / keydown / paste / │  ── input    │
        │  pointer events / IME / drag    ─┼──mapping────►│
        └──────────────────────────────────┘              │
                                                          │
        ┌──────────────────────────────────┐              │
        │  DomSelection (W3C)  ◄─────────► │  Source      │
        │  + visual CaretState             │  Position    │
        │  Radiant_Design_Selection.md     │  (path+offs) │
        └──────────────────────────────────┘──────────────┘
```

**Single direction of authority:** the source Mark tree is the only place state changes. Everything visual is a derived projection that reconciles via `render_map` and Radiant's incremental relayout.

---

## 4. Data Model

### 4.1 The source: Mark tree, unchanged

Lambda already represents every supported document format as a uniform Mark tree of elements, attributes, text strings, maps, and arrays. We **do not invent a new document type** for editing — we add an interpretation layer (the schema) on top.

```lambda
// Markdown source for "Hello **world**" parses to roughly:
<doc
    <p
        "Hello "
        <strong "world">
    >
>
```

This is already JSON-native (Slate-like) and immutable-friendly via `MarkEditor` (PM-like).

### 4.2 Schema

Best of ProseMirror: **a schema is mandatory** for any subtree that the user can edit. It declares, per element tag:

- `role`: `block` | `inline` | `mark` | `leaf` | `atom`
- `content`: a content expression over child roles (`block+`, `inline*`, `(paragraph | heading)+`, `list_item+`, …)
- `marks`: which marks may apply (for inlines)
- `attrs`: declared attributes with defaults & validators
- `selectable` / `editable` / `draggable` flags

The schema is itself a Lambda value, hence inspectable / overridable / extensible from script. A canonical schema for the Markdown / CommonMark subset ships in `lambda/format/markdown_schema.ls`; users can compose or override it.

```lambda
let md_schema = schema {
    doc:       { role: 'block, content: 'block+ },
    paragraph: { role: 'block, content: 'inline*, marks: 'all },
    heading:   { role: 'block, content: 'inline*, marks: 'all,
                 attrs: { level: { type: int, default: 1, validate: |v| v >= 1 and v <= 6 } } },
    list:      { role: 'block, content: 'list_item+,
                 attrs: { ordered: bool } },
    list_item: { role: 'block, content: 'paragraph block* },
    code_block:{ role: 'block, content: 'text*, marks: 'none, code: true },
    text:      { role: 'inline },
    image:     { role: 'inline, leaf: true,
                 attrs: { src: string, alt: string } },
    link:      { role: 'mark,  attrs: { href: string, title: string? } },
    strong:    { role: 'mark },
    em:        { role: 'mark },
    code:      { role: 'mark, excludes: 'all },
}
```

**Why mandatory:** without it, "press Enter at end of `<h1>`" has no defined answer; with it, the engine knows the new node should be a `paragraph` (the schema's `defaultBlock`) because `heading.content` does not allow nested blocks.

**Marks vs nested elements.** Lambda's parser today often produces nested elements (`<strong>`, `<em>`) rather than PM-style `marks: [{type:'strong'}]`. We keep nested elements as the on-disk form and treat any element whose schema role is `mark` as logically equivalent to a PM mark on the contained text. This avoids forking the existing parsers/formatters while giving the editor PM's mark semantics (mark sets, mark-aware splitting, mark inheritance at typing position).

### 4.3 Position model — `SourcePos`

This is the single most important design decision and the one where ProseMirror and Slate disagree. We adopt **Slate-style paths**, augmented with the invariants that make ProseMirror's transaction algebra possible.

```c
typedef struct SourcePath {
    uint32_t  len;         // depth from doc root
    uint32_t* indices;     // child index at each depth (interned in arena)
} SourcePath;

typedef struct SourcePos {
    SourcePath path;       // path to the containing node
    uint32_t   offset;     // for text leaves: UTF-8 byte offset into text
                           // for non-leaf nodes: child index in [0, child_count]
                           // (matches DomBoundary semantics — §4 of dom_range.hpp)
} SourcePos;
```

Rationale for paths over PM integers:

1. **Already aligned with `DomBoundary`** in `dom_range.hpp`, which uses `(node, offset)`. We get a near-1:1 source ↔ DOM mapping.
2. **Diagnostics**: a path prints as `[0,2,1]:offset=4` and is human-debuggable.
3. **Mark tree is heterogeneous** (elements, maps, arrays, strings) — PM's flat integer scheme assumes a uniform open/close-token stream and would have to special-case maps.

Adopt these PM-derived invariants on top:

- A `SourcePos` is **canonical** (never two equivalent forms): inside a text leaf, points are always expressed as offsets into that leaf, never as boundary points between adjacent text leaves.
- Provide `pos_compare(a, b)`, `pos_min`, `pos_max`, `resolve(pos) → ResolvedPos` returning the chain of ancestor nodes (PM's `$pos.depth`, `$pos.parent`, `$pos.before(d)`, `$pos.after(d)`, …). This is what makes commands like *split-block* expressible.
- Provide a `Mapping` object: every `Step` (§5) carries a position-mapping function so any open `SourcePos` (including selection endpoints) can be translated through the step. This is the foundation for both undo/redo and (later) collaborative rebase.

### 4.4 Selection model — `SourceSelection`

Three subtypes, mirroring PM:

- `TextSelection { anchor: SourcePos, head: SourcePos }` — usual caret/range case.
- `NodeSelection { path: SourcePath }` — image, code block, table cell selected as a unit.
- `AllSelection` — Cmd+A.

The existing `DomSelection` (`dom_range.hpp`) is the **rendered projection** of a `SourceSelection`. The single source of truth is the source-side selection; a resolver converts to/from `DomBoundary` via `render_map`.

---

## 5. Transactions and Steps

### 5.1 Steps

Take ProseMirror wholesale here. A **Step** is a small, typed, JSON-serialisable, **invertible** edit. Every mutation goes through a step.

```c
typedef enum StepKind {
    STEP_REPLACE,        // replace [from..to] with given slice
    STEP_REPLACE_AROUND, // replace [from..to] keeping inner [gapFrom..gapTo]
    STEP_ADD_MARK,       // add mark across [from..to]
    STEP_REMOVE_MARK,    // remove mark across [from..to]
    STEP_SET_ATTR,       // set attribute on node at path
    STEP_SET_NODE_TYPE,  // re-tag node at path (paragraph→heading)
} StepKind;

typedef struct Step {
    StepKind   kind;
    SourcePos  from;
    SourcePos  to;
    /* kind-specific payload: slice, mark, attr name+value, new tag */
    ...
    Item (*invert)(struct Step* self, Item doc_before);
    SourcePos (*map)(struct Step* self, SourcePos pos, MapBias bias);
    Item (*apply)(struct Step* self, Item doc, MarkEditor* ed);
} Step;
```

`invert` returns the step that undoes this one. `map` maps a position through this step (for selection migration and for rebasing in collab). `apply` runs the step against `MarkEditor` to produce the new doc.

### 5.2 Transaction

A **Transaction** is a sequence of steps + a target selection + metadata (`scrollIntoView`, `addToHistory`, `meta` map). Transactions are produced by **commands** (composable functions `(state, dispatch?) → bool`) — exactly the PM pattern, which is markedly cleaner than Slate's "mutate `editor`" approach.

```lambda
fn cmd_toggle_strong(state) {
    let sel = state.selection
    if sel.empty { return cmd_toggle_stored_mark(state, 'strong) }
    let tr = state.tr |> add_mark('strong, sel.from, sel.to)
    state.dispatch(tr)
    return true
}
```

### 5.3 Mapping to existing `MarkEditor`

`Step::apply` lowers to one or more existing `MarkEditor` calls (`elmt_insert_child`, `elmt_replace_child`, `map_update`, …). The transaction layer therefore **adds structure on top of `MarkEditor` rather than replacing it**: existing reactive-UI handlers continue to work, and rich-text edits become just a higher-level vocabulary of commands.

`MarkEditor`'s versioned immutable mode (§Mark_Editor2.md) becomes the engine's history stack:

- Each committed transaction = one `EditVersion`.
- `edit_undo()` / `edit_redo()` already exist; we wrap them with selection restoration via `Mapping`.

### 5.4 History & undo

PM's history plugin: a ring of `(steps, inverted_steps, selection_before, selection_after)` items, with **history compression** (typing within 500 ms collapses to one undoable item). Slate has the same idea less formally. We adopt the PM design and implement it as a Lambda-side reactive plugin (no engine change required) using `MarkEditor::commit` for the underlying snapshot.

---

## 6. Templates: View vs Edit Renderings

### 6.1 The view template is already enough — *almost*

Existing `view <p> { ... }` templates already produce the result tree from a source node. For rich-text editing we need the result tree to be **edit-aware**:

- Editable inline content must produce DOM nodes the engine can find when the user clicks at coordinates `(x, y)` to position the caret.
- `render_map` already gives us source→result; we add result-DOM-node→source-pos as the inverse.

Concretely we extend `view` templates with a small editing-related convention rather than a new construct:

```lambda
view <p> {
    <p contenteditable: "true" data-editable: ~ {
        apply ~.children                    // template-recursion as today
    }>
}
```

`data-editable: ~` annotates the produced DOM with a back-reference to the source node. The runtime stores this in `render_map` (not in the rendered HTML — it's a side-band index), so positions can be resolved without polluting the output HTML if the doc is later exported.

### 6.2 Edit-only templates for atomic / structured insertions

When the editing UX is non-trivial — image with caption + size handles, table cell with merge controls — declare an `edit` template that runs **only when in editing mode**:

```lambda
view <image>      { <img src: ~.src alt: ~.alt> }
edit <image>      {                                     // only matched in edit mode
    <figure tabindex: 0 data-atomic: true
        <img src: ~.src alt: ~.alt>
        <figcaption (apply ~.alt)>
    >
}
```

`edit` templates win over `view` templates when the engine is in editing mode (template_registry already supports `is_edit`; we just wire mode-aware dispatch).

### 6.3 Round-trip to HTML and back

For **HTML export** the existing pipeline already works (`view` templates + Radiant's HTML renderer). For **HTML paste / drop**, the inverse is needed: parse the HTML fragment with Lambda's HTML parser, then **schema-coerce** it (drop disallowed elements, lift inlines that landed in block context, etc.) — the same algorithm PM uses for `parseSlice`. The coercion rules are derived mechanically from the schema and need to be written once.

---

## 7. Input Mapping (DOM events → transactions)

### 7.1 Use `beforeinput` as the primary signal

`event.cpp` today dispatches `keydown`/`keypress` and intercepts character insertion in `text_control.cpp`. For rich text we need the broader `InputEvent` (Level 2) `beforeinput` semantics, with `inputType` covering ~30 cases (`insertText`, `insertParagraph`, `deleteContentBackward`, `formatBold`, `historyUndo`, `insertFromPaste`, …).

**Why:** browsers' contenteditable reality forced this spec; following it lets us reuse browser-equivalent IME, autocorrect, and screen-reader signalling. Slate moved to `beforeinput` for this reason; PM has its own DOM observer but is migrating in the same direction.

For Radiant's native window we don't need to literally fire `beforeinput` — we just route platform key/IME/composition events through a **`InputIntent` enum** with the same shape and let one place (the input mapper) translate intents to transactions.

### 7.2 Default key map (initial set)

| Intent | Source | Command |
|---|---|---|
| `insertText` (printable, non-IME) | `event.cpp` keydown | `cmd_insert_text` |
| `insertParagraph` | Enter | `cmd_split_block` (schema-driven default block at depth) |
| `insertLineBreak` | Shift+Enter | `cmd_insert_hard_break` |
| `deleteContentBackward` | Backspace | `cmd_delete_backward` (joins blocks at boundary) |
| `deleteContentForward` | Delete | `cmd_delete_forward` |
| `deleteWordBackward` | Alt/Ctrl+Backspace | `cmd_delete_word_backward` |
| `formatBold` / `formatItalic` / `formatUnderline` | Cmd+B/I/U | `cmd_toggle_mark` |
| `historyUndo` / `historyRedo` | Cmd+Z / Cmd+Shift+Z | history plugin |
| `insertFromPaste` (text) | clipboard | `cmd_paste_text` |
| `insertFromPaste` (HTML) | clipboard | `cmd_paste_html` (parse → schema-coerce → replace selection) |
| `formatIndent` / `formatOutdent` | Tab / Shift+Tab in list | `cmd_indent_list_item` |
| Drag insert | drop event | `cmd_insert_at` |

Each command lives in `lambda/lambda/edit/commands.cpp` (new) and is exposed both to C/JIT (via `edit_bridge`) and to Lambda script (so users can redefine key maps).

### 7.3 IME / composition

A composition session must be **one transaction**. Today `text_control.cpp` already buffers per-control text; we promote the same pattern: a composition session opens a transaction, accumulates `insertCompositionText` steps (replacing the composing range each time), and commits or aborts on `compositionend`. Until commit, the source tree shows the in-progress text but history records nothing.

### 7.4 Selection sync

Two arrows in Architecture §3 need symmetric implementations:

- **DOM → source** (after pointer click, after browser-like contenteditable mutates the DOM): resolve the new `DomBoundary` via `render_map` reverse lookup → produce a `SourcePos` → set `state.selection` → fire `selectionchange`.
- **Source → DOM** (after a transaction): for each surviving selection endpoint, find the result DOM node via `render_map`, then place the visual caret using existing `state_store` / `CaretState` machinery and emit a focus + selection paint.

Both already half-exist for `<input>`/`<textarea>` (`tc_sync_legacy_to_form`, `tc_sync_form_to_legacy`); we generalise them to arbitrary editable subtrees by routing through `SourcePos` instead of byte offsets in a single string.

---

## 8. Rendering & Reconciliation

No new mechanism — reuse `render_map` two-phase update (`Phase 1: mark dirty after model mutation; Phase 2: top-down re-transform`).

The change for rich text is that **mutations are now driven by the transaction layer**, so `render_map_mark_dirty` is called per modified source node from `Step::apply`. The existing incremental DOM patch pipeline picks up from there. Performance characteristics from Reactive_UI4/5 (~32–42 ms) carry over because typing in a paragraph dirties exactly one source node.

A small but important addition: **decorations**. PM's decorations let us paint things like:

- Spell-check squiggles
- Find/replace highlights
- Collaborator cursors
- The active selection background

without mutating the document. We add a `decoration_set` per editor session; templates may opt in via `apply ~.children with decorations(deco_set)`. Decorations are rendered as Radiant overlays (existing display-list stage) and never enter the source tree or undo history.

---

## 9. Public API Surface

A new Lambda module `edit/` exposes the editor as a first-class object:

```lambda
import edit

let doc = input("README.md", 'markdown)
let editor = edit.open(doc, schema: edit.schemas.markdown)

// commands
editor.exec(edit.cmd.toggle_mark('strong))
editor.exec(edit.cmd.split_block)
editor.exec(edit.cmd.insert_text("hello"))

// observe
editor.on('change', |tr| log(tr.steps))
editor.on('selection', |sel| log(sel))

// render — pick existing view templates by selecting a "preset"
editor.mount(window, preset: 'markdown_wysiwyg)

// save back
output(editor.doc, "README.md", 'markdown)
```

Underneath, `edit.open` builds the `RadiantState`'s editor session, registers schema, links `MarkEditor` history, and selects which `view`/`edit` templates apply.

For C/JIT consumers the same surface is exported through `edit_bridge.h` extended with:

```c
EditSession* edit_session_new(Item root, Schema* schema);
bool         edit_session_exec(EditSession*, const char* cmd_name, Item args);
SourcePos    edit_session_selection_anchor(EditSession*);
SourcePos    edit_session_selection_head(EditSession*);
void         edit_session_subscribe(EditSession*, EditEventKind, EditCallback, void*);
```

---

## 10. Comparison Table — Where Each Idea Comes From

| Layer | Borrowed from | Why |
|---|---|---|
| Schema (mandatory, content expressions, mark roles) | **ProseMirror** | Makes "press Enter here" decidable; enables paste coercion |
| JSON-native, nested-array source tree | **Slate** | Already what Lambda has; no impedance mismatch |
| Path+offset positions (`SourcePos`) | **Slate** | Aligns with `DomBoundary`; debuggable |
| Resolved positions (`$pos.parent`, `$pos.before(d)`) | **ProseMirror** | Required for split/lift/wrap commands |
| Steps (typed, invertible, mappable) | **ProseMirror** | Foundation for history, collab, paste |
| Transaction = sequence of steps + new selection | **ProseMirror** | Single-commit unit; clean undo |
| Commands `(state, dispatch?) → bool` | **ProseMirror** | Dry-run probe for menus/keymaps; better than Slate's mutate-editor pattern |
| Plugin = `(editor) ⇒ editor` decoration | **Slate** | Easier authoring than PM's PluginSpec; we keep this for non-state plugins |
| `beforeinput` / `InputIntent` central mapper | **Slate** (and converging PM) | One place for IME, paste, screen-reader correctness |
| Decorations (non-document overlays) | **ProseMirror** | Spellcheck, find, collab cursors without dirtying history |
| `render_map` incremental reconciliation | **Lambda's existing work** | We did not need to import either system here |
| `MarkEditor` versioned snapshots | **Lambda's existing work** | Becomes the history backing store |

---

## 11. Phased Implementation Plan

### Phase R1 — Schema layer (no editing yet)

1. Define schema struct + content-expression matcher (port the simple PM grammar: `name`, `name+`, `name*`, `name?`, `(a | b)`, sequences).
2. Ship schemas for: `markdown`, `commonmark_strict`, `html5_subset`. (Each ~200 lines of Lambda.)
3. Add `schema_validate(doc, schema)` returning a list of violations — useful immediately for parser CI.

**Exit:** every existing parser fixture validates; lint warnings fixed in formatters as needed.

### Phase R2 — Position & selection over the source tree

1. Implement `SourcePath`, `SourcePos`, `pos_compare`, `resolve_pos`.
2. Wire `render_map` reverse lookup so a `(DomNode*, dom_offset)` can be translated to `SourcePos` and back.
3. Add `SourceSelection` and bidirectional sync with the existing `DomSelection`.

**Exit:** clicking in a rendered Markdown document logs the correct `SourcePos`; `selection_to_string` produces the same text as today's `DomRange::to_string`.

### Phase R3 — Steps, transactions, history

1. Implement the six step kinds in §5.1, each with `apply`, `invert`, `map`.
2. Build the `Transaction` builder; route through `MarkEditor` for storage.
3. Hook the existing `edit_undo`/`edit_redo` with selection restoration via `Mapping`.

**Exit:** programmatic edits via `editor.exec(...)` mutate the doc, fire `render_map` dirty flags, repaint correctly, and undo restores both doc and selection.

### Phase R4 — Input mapper & default commands

1. Add the `InputIntent` enum to `event.cpp` and translate platform key/IME events.
2. Implement the §7.2 command set.
3. Make typing in a `data-editable` element fire `cmd_insert_text` instead of the legacy textarea path; keep textarea/input fast path intact.

**Exit:** can open `README.md` in `lambda view`, click into a paragraph, type / split / merge / bold / italic, save the file back as Markdown with formatting preserved.

### Phase R5 — Paste, drop, decorations

1. HTML paste: parser + schema coercion.
2. Drag-and-drop of source-tree subtrees (extends the Phase 26 drag work in `Reactive_UI5.md`).
3. Decoration set + spellcheck + find/replace as the first two consumers.

**Exit:** copy from a browser → paste into Lambda → structure preserved up to schema; find/replace highlights live without entering history.

### Phase R6 — Collab-ready (deferred / optional)

Steps already serialise; add a `Mapping` rebase + a transport. Not on the critical path.

### Effort & risk

| Phase | Complexity | Main risk |
|---|---|---|
| R1 | low | schema completeness for existing fixtures |
| R2 | medium | UTF-8/UTF-16 boundary parity with `dom_range` |
| R3 | medium | step inversion correctness for `REPLACE_AROUND` |
| R4 | high | IME on each platform; Backspace at block boundary edge cases |
| R5 | medium | HTML→schema coercion fidelity |
| R6 | medium | only if collab is pursued |

---

## 12. Open Questions

1. **Marks as elements vs. PM-style mark sets** — kept as elements for now (§4.2). If profiling shows mark-toggle re-renders too much, introduce an internal mark-set view without changing on-disk form.
2. **Atomic blocks vs editable blocks** for embeds (charts, equations, custom widgets) — proposal: schema flag `atomic: true` makes the node a single `NodeSelection` target; an `edit` template owns its own internal editor (a sub-`EditSession`) for things like equation editors.
3. **Multi-cursor** (Slate strong, PM via plugin) — defer; orthogonal to everything above.
4. **Per-document vs per-template schema scoping** — proposal: per document, but a template may declare a *narrower* schema for its subtree (e.g., a code-block editor inside a Markdown doc constrains content to `text*`).
5. **Persistence of editor session state across reloads** — reuse the existing session-state store keyed by `(model_item, template_ref)` — already designed.

---

## 13. Summary

Lambda already has a universal source tree, a versioned mutation engine, a template-driven renderer, an incremental reconciler, and W3C-conformant DOM ranges. The proposal does not invent a new editor; it **adds a thin, well-known conceptual layer** — schema + path positions + steps + transactions + a `beforeinput`-style input mapper + decorations — borrowing the parts of ProseMirror and Slate.js that have proven themselves, and binding them to the existing reactive-UI substrate so the impact on the engine is localized and the application surface stays Lambda-idiomatic.

The end state: any document Lambda can parse becomes editable in place, every keystroke is a typed transaction over the source Mark tree, undo/redo is principled, and round-tripping through Markdown/HTML/LaTeX/Wiki/PDF reuses the formatters already in `lambda/format/`.
