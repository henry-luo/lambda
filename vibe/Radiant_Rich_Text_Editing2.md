# Radiant Rich Text Editing — Stage 2 Prototype Proposal

**Date:** 2026-05-05
**Status:** Proposal
**Builds on:** [Radiant_Rich_Text_Editing.md](Radiant_Rich_Text_Editing.md) (Stage 1 design)

---

## 1. Objective

Stage 1 specified the schema / position / step / transaction / input‑intent layers and landed the Lambda‑side editor module set under [lambda/package/editor/](../lambda/package/editor/) (`mod_editor.ls`, `mod_commands.ls`, `mod_step.ls`, `mod_transaction.ls`, `mod_history.ls`, `mod_input_intent.ls`, `mod_md_schema.ls`, `mod_paste.ls`, `mod_html_paste.ls`, `mod_decorations.ls`, `mod_dom_bridge.ls`, `mod_source_pos.ls`, `mod_doc.ls`, `mod_collab.ls`).

Stage 2 turns that machinery into a **runnable Slate/ProseMirror‑class prototype** that:

1. Loads any markdown file under [test/input/](../test/input/) and renders it as an editable document.
2. Provides a **toolbar** with: Bold, Italic, Underline, Bullet/Ordered List, Table, Image, Link, Blockquote, Code, Undo/Redo.
3. Supports **bold / italic / underline** mark toggling on selected text.
4. Supports **lists** (insert, indent/outdent, convert paragraph ↔ list item).
5. Supports a **simple table** (insert N×M, add/delete row/column).
6. Supports **images** (insert by URL, alt text, selectable as a `NodeSelection`).
7. Supports **cut / copy / paste** (plain text, HTML, internal slice) plus drag‑and‑drop.
8. Supports **full caret + selection** (click, drag, Shift+arrows, word/line/all).
9. Lives under [test/ui/](../test/ui/) and is covered by **UI automation** (`*.json` event scripts driven by `./lambda.exe view ... --event-file`).

The prototype reuses the existing HTML DOM / Radiant text‑control / `state_store` infrastructure and binds the toolbar buttons to the `edit_*` commands already exported from `mod_editor.ls`.

> Comparable scope to the **Slate.js Rich Text example** and the **ProseMirror basic example**, in a single `lambda.exe view` invocation, with no browser involved.

---

## 2. What Stage 1 Already Delivered

| Capability | Where | Status |
|---|---|---|
| Mark/Lambda source tree as the model | [lambda/lambda-data.hpp](../lambda/lambda-data.hpp) | reused as‑is |
| Schema (markdown / commonmark / html5 subsets) | [mod_md_schema.ls](../lambda/package/editor/mod_md_schema.ls), [mod_edit_schema.ls](../lambda/package/editor/mod_edit_schema.ls) | shipped |
| `SourcePos` / `SourcePath` / `resolve_pos` | [mod_source_pos.ls](../lambda/package/editor/mod_source_pos.ls) | shipped |
| Steps (replace, replace‑around, mark add/remove, set‑node‑type) with `apply` / `invert` / `map` | [mod_step.ls](../lambda/package/editor/mod_step.ls) | shipped |
| Transactions + history (compression, selection restore) | [mod_transaction.ls](../lambda/package/editor/mod_transaction.ls), [mod_history.ls](../lambda/package/editor/mod_history.ls) | shipped |
| High‑level commands (insert text, split block, toggle mark, list indent, paste text/html, insert image/link/code/table, …) | [mod_commands.ls](../lambda/package/editor/mod_commands.ls) | shipped |
| `beforeinput`‑style intent mapper | [mod_input_intent.ls](../lambda/package/editor/mod_input_intent.ls) | shipped |
| Public façade (`edit_open` / `edit_exec` / `edit_dispatch` / `edit_set_decorations` / `edit_can_*`) | [mod_editor.ls](../lambda/package/editor/mod_editor.ls) | shipped |
| Smoke UI fixture (toolbar + textarea, selection‑aware bold) | [test/ui/test_rich_text_editor.html](../test/ui/test_rich_text_editor.html), [test/ui/test_rich_text_editor.json](../test/ui/test_rich_text_editor.json) | shipped (passes `27/27` assertions) |
| JS‑backed text control runtime context fix | [lambda/js/js_dom_selection.cpp](../lambda/js/js_dom_selection.cpp) | shipped |

What is **missing for a real prototype**:

- A renderer that turns the `editor.doc` Mark tree into editable DOM (not a textarea) — supplied by the `view <doc_block>` / `view <doc_inline>` templates in [test/ui/rte_prototype.ls](../test/ui/rte_prototype.ls).
- A toolbar wired to the actual commands rather than ad‑hoc string toggling — supplied by the `view <rte_toolbar>` template's `on click(evt)` handler dispatching `emit("rte_cmd", edit_cmd_*())` to the parent `edit <rte_app>`.
- UI automation that drives the prototype end‑to‑end against a real `.md` file.

Stage 2 fills exactly those gaps.

---

## 3. Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│  test/ui/rte_prototype.ls       (single Lambda script — no JS)     │
│                                                                    │
│  let doc = input(SOURCE_PATH, 'markdown)                           │
│  let editor = edit_open(doc, null, null)                           │
│                                                                    │
│  view <rte_toolbar>  { … buttons with `on click` handlers … }      │
│  edit <rte_doc>      { … renders editor.doc, owns selection … }    │
│  view <doc_block>    { … paragraph / heading / list / table … }    │
│  view <doc_inline>   { … strong / em / u / code / link / image … } │
│                                                                    │
│  <html …                                                           │
│    <body                                                           │
│      apply(<rte_toolbar editor:editor>)                            │
│      apply(<rte_doc     editor:editor>)                            │
│      <pre id:"markdown-output">                                    │
│      <span id:"status";  "opened:" ++ SOURCE_PATH>                 │
│  >>>                                                               │
└────────────────────────┬───────────────────────────────────────────┘
                         │ apply / on click / on keydown / emit
                         ▼
┌────────────────────────────────────────────────────────────────────┐
│  lambda/package/editor/mod_editor.ls   (Stage 1, reused as-is)     │
│  • edit_open / edit_exec / edit_dispatch / edit_can_exec           │
│  • command_tx for every cmd in §6                                  │
│  • history (undo/redo) + selection mapping                         │
└────────────────────────┬───────────────────────────────────────────┘
                         │ Mark tree mutations (immutable copy-on-write)
                         ▼
┌────────────────────────────────────────────────────────────────────┐
│  Radiant render_map  +  template_registry  (existing)              │
│  • dirty-tracks (source_item, template_ref) → result_node          │
│  • re-executes only dirty template bodies on each tick             │
│  • emits HTML/CSS into the layout tree                             │
└────────────────────────────────────────────────────────────────────┘
```

**Single source of truth:** the immutable Mark tree inside `editor.doc`. Toolbar `on click` handlers call `editor = edit_exec(editor, cmd)`; that mutates the local `editor` value, which `apply(<rte_doc editor:editor>)` re-renders through Radiant's `render_map` reconciliation. There is no DOM mutation written by hand: the `view <doc_block>` / `view <doc_inline>` templates fully describe the projection, and Radiant patches what changed.

This mirrors Slate's `Editor` / `Renderer` split and ProseMirror's `EditorState` / `EditorView` split, but reuses Radiant's existing `render_map` reconciliation rather than introducing a new diff engine — and it does so **without any JavaScript** in the prototype, exactly like [test/lambda/ui/todo.ls](../test/lambda/ui/todo.ls).

**Markdown ↔ tree boundary.** Markdown is parsed once at script start by `input(SOURCE_PATH, 'markdown)` into the source Mark tree, and serialised back to markdown only on **save** — either user-triggered (Save button / `Cmd+S` handler in `rte_toolbar`) or by an autosave timer. There is **no live markdown serialisation per keystroke**: the `#markdown-output` element is rendered empty by default and is only populated by the save handler, which calls `output(editor.doc, 'markdown)` and writes the result through the `markdown_output` state. This matches Slate.js / ProseMirror, where the document model is the editing target and the source format is an import/export concern.

---

## 3a. Implementation Status (2026-05-05)

### Landed

| Item | Where |
|---|---|
| Stage 2 design rewritten around Lambda reactive UI (no JS) | this doc, §3–§11 |
| Obsolete JS prototype deleted | removed `test/ui/rte_prototype.html` and the JS-era `test/ui/rte_prototype.json` |
| **S2.1 scaffold** — pure Lambda reactive UI script | [test/ui/rte_prototype.ls](../test/ui/rte_prototype.ls) |
| Top-level `edit <rte_app>` template owning `status` and `markdown_output` state | same |
| `view <toolbar_button>` with `on click() { emit("rte_cmd", ~.cmd) }` and 13 toolbar instances dispatched by `apply(<toolbar_button cmd:..., label:...>)` | same |
| `on rte_cmd(evt)` stub in `edit <rte_app>` updating `status` and (for `btn-save`) calling `format(initial_doc, 'markdown)` | same |
| One-shot `let initial_doc^err = input('./test/input/simple.md', 'markdown)` at script start (parse-once boundary) | same |
| Inline page shell + CSS via `<html lang:"en" <head <style "..."> ...>` matching the convention in `test/lambda/ui/todo.ls` | same |
| End-to-end smoke that drives initial render, toolbar dispatch, and Save | [test/ui/rte_prototype.json](../test/ui/rte_prototype.json) |
| Verified `./lambda.exe test/ui/rte_prototype.ls` compiles via MIR Direct and reaches JIT execution | local run |

### Active blocker — `build_dom_tree` rejects markdown subtree (S2.1 finish)

Rendering the parsed markdown body inside `#doc` (either via per-tag `view <h1>`/`view <p>`/… templates or by embedding `initial_doc[0]` directly under a `<div>`) crashes Radiant during DOM construction:

```
build_dom_tree: <body> child 0 has invalid type=224 (raw=0x340001620), skipping
build_dom_tree: <h2> child 0 has invalid type=160 (raw=0x4a0010680), skipping
build_dom_tree: <p> has length=1 but items=NULL
build_dom_tree: <blockquote> child 0 has invalid type=96 (raw=0x340402da0), skipping
signal handler: SIGSEGV at 0x0 is not stack overflow, re-raising
```

Two distinct issues are in play:

1. **Splat semantics.** The natural per-tag template `view <h1> { <h1 for (c in ~) c> }` does not splat the loop result into the parent element — `for (c in ~) c` produces a list value that becomes a single child of unexpected type, hence the `length=1 but items=NULL` and `invalid type=…` reports. The correct pattern is the bare **`apply;` statement** (per `Reactive_UI.md` §8.3), which the framework expands into a child-template dispatch per child of `~`:

   ```lambda
   view <h1> { <h1 ; apply; > }
   view <p>  { <p  ; apply; > }
   view <strong> { <strong ; apply; > }
   ```

   `apply;` (statement form, no parens, terminating semicolon) re-dispatches each child of the matched item through the template registry. The function-call form `apply(target)` requires an explicit target argument and is used for top-level entry (`apply(initial_doc[0])`). A single catch-all `view any { ~ }` plus per-tag wrappers cover the whole markdown subtree.
2. **Inline value types in the markdown Mark tree.** The markdown parser ([lambda/input/markup/](../lambda/input/markup/)) emits inline children that include compound scalar variants (`TypeId` 224, 160, 96 — string-like with attached metadata) which Radiant's `build_dom_tree` does not recognise as valid `Item` payloads and skips with a warning, then dereferences a NULL on the next step.

### Next steps

1. Add per-tag `view <h1>`, `view <p>`, `view <strong>`, `view <em>`, `view <code>`, `view <a>`, `view <ul>`, `view <ol>`, `view <li>`, `view <blockquote>`, `view <span>`, `view <img>`, `view <hr>` templates of the form `<tag ; apply; >` (note: `apply;` statement, not `apply()` call), plus a catch-all `view any { ~ }` to coerce the unknown inline scalar variants to their string content.
2. Locate the `TypeId` ↔ `build_dom_tree` mapping in [radiant/](../radiant/) and either widen the accepted set or convert the offending inline items to plain `<span>` wrappers in a per-tag template before they reach Radiant's DOM builder.
3. Once `#doc` renders the markdown source visually, swap the local `editor` from `format(initial_doc, 'markdown)` (current Save behaviour) to the full `mod_editor.ls` flow: `let editor = edit_open(initial_doc, null, null)` plus `editor = edit_exec(editor, edit_cmd_*())` inside `on rte_cmd`, and re-render `#doc` from `editor.doc`. This is the start of S2.2.

### Working notes

- Use `<tag ; apply; >` for per-tag templates. The bare **`apply;` statement** (parens-less, terminating semicolon) is the splat form per `Reactive_UI.md` §8.3 — it dispatches each child of `~` through the template registry, splatting them as element children. The function-call form `apply(target)` is for top-level entry (e.g. `apply(initial_doc[0])`) and requires an explicit target. The `<tag for (c in ~) c>` form does not splat: `for` returns a list which becomes a single (broken) child.
- Smoke runner accepts `.ls` paths in the JSON `"html"` field — no runner change needed for reactive-UI prototypes.
- `./lambda.exe view <script>.ls --event-file <file>.json --headless` is the canonical smoke invocation; `--no-log` suppresses the per-event trace once the test stabilises.

---

## 4. Component Inventory

### 4.1 Single Lambda script — `test/ui/rte_prototype.ls`

The entire prototype is one `.ls` file driven by `./lambda.exe view test/ui/rte_prototype.ls`. It contains:

| Section | Purpose |
|---|---|
| Imports | `import editor` (the `mod_editor.ls` package: `edit_open`, `edit_exec`, `edit_can_exec`, `edit_cmd_*`). |
| One-shot parse | `let initial_doc = input(SOURCE_PATH, 'markdown)` — Lambda's existing markdown parser hands back a Mark tree. |
| Editor state | `let editor = edit_open(initial_doc, null, null)` produces the `{kind:'editor', doc, schema, selection, history, …}` value used everywhere downstream. |
| `view <rte_toolbar>` | Renders a row of buttons. Each button has a stable `class:` so the `on click` handler can dispatch by class. The handler calls `editor = edit_exec(editor, edit_cmd_*())`; the framework re-renders. |
| `edit <rte_doc>` | Owns the editable region. Renders `editor.doc` via recursive `apply()` over `view <doc_block>` / `view <doc_inline>`. Holds caret/selection in `state` (`anchor_path`, `head_path`, `anchor_off`, `head_off`). Routes `on input` / `on keydown` / `on click` to `edit_dispatch(editor, intent)`. |
| `view <doc_block>` / `view <doc_inline>` | Pattern-match on Mark tags (`<paragraph>`, `<heading level:int>`, `<list ordered:bool>`, `<list_item>`, `<code_block>`, `<blockquote>`, `<table>`, `<table_row>`, `<table_cell>`, `<hr>`; inline: `<text>`, `<strong>`, `<em>`, `<u>`, `<code>`, `<link href:string>`, `<image src:string alt:string>`). Each emits the corresponding HTML element with `data-spath` attributes for selection mapping. |
| Save handler | The Save button's `on click` runs `markdown_output = output(editor.doc, 'markdown)`; the surrounding `<pre id:"markdown-output">` reads that state. Tests assert against `#markdown-output`. |

The body of the script ends with the literal HTML page that hosts the templates:

```lambda
<html lang:"en"
  <head <title "Radiant RTE Prototype"> <link rel:"stylesheet" href:"rte_prototype.css">>
  <body
    apply(<rte_toolbar editor:editor>)
    apply(<rte_doc editor:editor>)
    <pre id:"markdown-output"; markdown_output>
    <span id:"status"; status_text>
  >
>
```

### 4.2 Reused, unchanged

| Already exists | Where | What we use it for |
|---|---|---|
| Markdown parser | `lambda/input/input-markdown.cpp` | `input(path, 'markdown)` |
| Markdown formatter | `lambda/format/format-markdown.cpp` | `output(doc, 'markdown)` on save |
| Editor module set | `lambda/package/editor/mod_editor.ls` (and siblings) | `edit_open`, `edit_exec`, `edit_can_exec`, `edit_dispatch`, all `edit_cmd_*` |
| Reactive UI runtime | `lambda/template_registry.{h,cpp}`, `lambda/render_map.{h,c}` | `view`/`edit` template dispatch, dirty-tracking, incremental DOM patch |
| Selection / caret | `radiant/dom_range.hpp`, `radiant/state_store.hpp`, `radiant/text_control.{hpp,cpp}` | Visual caret + selection paint; `evt.caret_pos`, `evt.selection_start/end` already wired in `todo2.ls` |
| Event simulation | `radiant/event_sim.cpp` (events `click`, `tripleclick`, `key_press`, `key_combo`, `paste_text`, `assert_*`) | UI automation against the prototype |

### 4.3 Net new code (Stage 2)

Only Lambda script — **no C/C++, no JS**:

| File | Approx LoC | Purpose |
|---|---|---|
| [test/ui/rte_prototype.ls](../test/ui/rte_prototype.ls) | ~400 | The whole prototype: imports + templates + page shell. |
| [test/ui/rte_prototype.css](../test/ui/rte_prototype.css) | ~80 | Toolbar layout, active-button state, table/figure visuals, selection ring. |
| [test/ui/rte_prototype.json](../test/ui/rte_prototype.json) | ~100 | End-to-end smoke (see §7). |
| Schema entry for `<u>` mark in [mod_md_schema.ls](../lambda/package/editor/mod_md_schema.ls) | ~10 | Underline support (the only schema gap). |
| `edit_cmd_wrap_list(kind)` in [mod_editor.ls](../lambda/package/editor/mod_editor.ls) | ~6 | Toolbar-friendly list wrapping (delegates to existing list helpers). |

> The Stage 1 design's `js_edit_session.cpp` and the JS-facing `RTE.*` surface are **dropped** in this revision. They belonged to a JS-driven shell that we no longer need: the `view`/`edit` templates already give us the same dispatch surface, in-language and JIT-compiled.

### 4.4 Selection / caret

`edit <rte_doc>` keeps `state` for the current `SourceSelection` (`anchor_path`, `anchor_off`, `head_path`, `head_off`, `kind: 'text|node|all`). Click and pointer events arrive on the matched DOM nodes; the framework already passes `(source_item, template_ref)` back through `render_map` reverse-lookup, so we know which doc node was clicked. `on input` / `on keydown` translate platform key/IME events into the `InputIntent` enum already understood by `edit_dispatch`. Caret painting is handled by Radiant's `state_store.set_caret(...)` — the same path the existing `<textarea>` uses.

For `NodeSelection` (atomic blocks: image, hr, code block selected as a unit), the templates render a `class:"rte-node-selected"` ring purely via state, no decoration plumbing needed in Stage 2.

### 4.5 Clipboard

Reuse Radiant's clipboard event surface (already consumed by `text_control.cpp`):

- **Cut** = `edit_exec(editor, edit_cmd_history_undo)` *(no — actually:)* `Copy` then `edit_dispatch(editor, {input_type:"deleteContentBackward"})`.
- **Copy** = `output(slice(editor.doc, sel), 'markdown)` placed on the clipboard via Radiant's existing clipboard write.
- **Paste** = clipboard read → `edit_exec(editor, edit_cmd_paste_html(html, fallback_text))` if `text/html` present, else `edit_cmd_paste_text(text)`.

All three commands already exist in [mod_editor.ls](../lambda/package/editor/mod_editor.ls); Stage 2 only wires the toolbar `Cut/Copy/Paste` buttons (and `Cmd+X/C/V` in the `edit <rte_doc>` `on keydown` handler) to them.

---

## 5. UI Prototype Files

All under [test/ui/](../test/ui/):

| File | Purpose |
|---|---|
| `rte_prototype.ls` | The whole prototype — pure Lambda script with `view <rte_toolbar>`, `edit <rte_doc>`, and per-tag `view <doc_block>` / `view <doc_inline>` templates. Toolbar buttons dispatch via `on click(evt) { editor = edit_exec(editor, …) }`. Hidden `<pre id:"markdown-output">` filled by Save / `Cmd+S` only. Boots from `input('test/input/simple.md', 'markdown)` at script start. |
| `rte_prototype.css` | Toolbar layout, button states (`.active`), node selection ring, table cell borders. Loaded via `<link rel:"stylesheet">` from the page shell. |
| `rte_prototype.json` | End-to-end UI automation script (see §7) — same event vocabulary as the Stage 1 fixtures. |
| `rte_lists.json` | Focused list-command regressions. |
| `rte_table.json` | Insert / add row / add col / delete row regressions. |
| `rte_image.json` | Insert image, select as node, delete. |
| `rte_clipboard.json` | Cut / copy / paste round-trip. |
| `rte_undo.json` | Multi-step undo / redo with selection restoration. |
| `rte_caret.json` | Click, Shift+arrow extension, word-select, select-all. |

Toolbar template (illustrative — Lambda syntax, not HTML):

```lambda
view <rte_toolbar> {
  <div id:"toolbar", role:"toolbar"
    <button class:"btn-bold";       "B">
    <button class:"btn-italic";     "I">
    <button class:"btn-underline";  "U">
    <button class:"btn-ul";         "•">
    <button class:"btn-ol";         "1.">
    <button class:"btn-quote";      "❝">
    <button class:"btn-code";       "</>">
    <button class:"btn-link";       "🔗">
    <button class:"btn-image";      "🖼">
    <button class:"btn-table";      "⊞">
    <button class:"btn-undo";       "↶">
    <button class:"btn-redo";       "↷">
    <button class:"btn-save";       "💾">
  >
}
on click(evt) {
  let cls = evt.target_class
  if      (cls == "btn-bold")      { emit("rte_cmd", edit_cmd_toggle_mark('strong)) }
  else if (cls == "btn-italic")    { emit("rte_cmd", edit_cmd_toggle_mark('em)) }
  else if (cls == "btn-underline") { emit("rte_cmd", edit_cmd_toggle_mark('u)) }
  else if (cls == "btn-ul")        { emit("rte_cmd", edit_cmd_wrap_list('bullet)) }
  else if (cls == "btn-ol")        { emit("rte_cmd", edit_cmd_wrap_list('ordered)) }
  else if (cls == "btn-quote")     { emit("rte_cmd", edit_cmd_wrap_blockquote()) }
  else if (cls == "btn-code")      { emit("rte_cmd", edit_cmd_set_block_type('code_block)) }
  else if (cls == "btn-link")      { emit("rte_cmd", edit_cmd_insert_link("", null, "")) }
  else if (cls == "btn-image")     { emit("rte_cmd", edit_cmd_insert_image("", "")) }
  else if (cls == "btn-table")     { emit("rte_cmd", edit_cmd_insert_table(3, 3, true)) }
  else if (cls == "btn-undo")      { emit("rte_cmd", edit_cmd_history_undo()) }
  else if (cls == "btn-redo")      { emit("rte_cmd", edit_cmd_history_redo()) }
  else if (cls == "btn-save")      { emit("rte_save", null) }
}
```

The parent `edit <rte_doc>` (or a top-level `edit <rte_app>`) listens for the `rte_cmd` and `rte_save` events:

```lambda
on rte_cmd(evt) {
  ~.editor = edit_exec(~.editor, evt)   // evt is the command record
}
on rte_save(_) {
  ~.markdown_output = output(~.editor.doc, 'markdown)
}
```

Active button state is computed on each render by checking `edit_can_exec(editor, edit_cmd_*())` and adding `class:"active"` accordingly — the framework re-runs the toolbar template body every time `editor` is dirty.

---

## 6. Command Coverage

Mapping from toolbar / keyboard intents to commands already exported by `mod_editor.ls`:

| UI action | Command | Notes |
|---|---|---|
| Click `B` / `Cmd+B` | `edit_cmd_toggle_mark('strong)` | already implemented |
| Click `I` / `Cmd+I` | `edit_cmd_toggle_mark('em)` | already implemented |
| Click `U` / `Cmd+U` | `edit_cmd_toggle_mark('u)` | schema entry needed in `mod_md_schema.ls` (`u` as `mark` role; serialises to `<u>`) |
| Click `•` | `edit_cmd_wrap_list('bullet)` | new thin wrapper around existing list helpers |
| Click `1.` | `edit_cmd_wrap_list('ordered)` | same |
| `Tab` / `Shift+Tab` in list | `edit_cmd_indent_list_item` / `outdent_list_item` | already implemented |
| Click `⊞` | `edit_cmd_insert_table(rows, cols, header)` | already implemented |
| Click `🖼` | `edit_cmd_insert_image(src, alt)` | already implemented; toolbar opens a tiny inline prompt for URL |
| Click `🔗` | `edit_cmd_insert_link(href, title, label)` | already implemented |
| Click `</>` | `edit_cmd_set_block_type('code_block)` | already implemented |
| Click `❝` | `edit_cmd_wrap_blockquote` | already implemented |
| `Enter` | dispatch `insertParagraph` → `cmd_split_block` | already implemented |
| `Shift+Enter` | dispatch `insertLineBreak` → `cmd_insert_line_break` | already implemented |
| `Backspace` / `Delete` | dispatch `deleteContentBackward` / `Forward` | already implemented |
| `Cmd+Z` / `Cmd+Shift+Z` | `edit_cmd_history_undo` / `redo` | already implemented |
| `Cmd+X/C/V` | clipboard bridge → `cut/copy/paste` (§4.5) | new wiring only |

The only new *Lambda* code is `edit_cmd_wrap_list` and a `u` mark schema entry (~40 lines combined). Everything else is bridge plumbing.

---

## 7. UI Automation Plan

`rte_prototype.json` (end‑to‑end smoke) drives the full surface:

```json
{
  "name": "Rich text editor prototype — markdown round trip",
  "html": "test/ui/rte_prototype.ls",
  "viewport": {"width": 980, "height": 720},
  "events": [
    {"type": "wait",        "ms": 250},
    {"type": "assert_text", "target": {"selector": "#status"},
                            "contains": "opened:test/input/simple.md"},
    {"type": "assert_count","target": {"selector": "#doc h1"},          "count": 1},
    {"type": "assert_count","target": {"selector": "#doc ul li"},       "count": 3},
    {"type": "assert_count","target": {"selector": "#doc pre code"},    "count": 1},

    {"type": "log", "message": "--- bold on selected text ---"},
    {"type": "tripleclick", "target": {"selector": "#doc p:nth-of-type(1)"}},
    {"type": "assert_selection", "is_collapsed": false, "check_dom": true},
    {"type": "click",       "target": {"selector": "#toolbar .btn-bold"}},
    {"type": "assert_count","target": {"selector": "#doc p:nth-of-type(1) strong"}, "count": 1},
    {"type": "click",       "target": {"selector": "#toolbar .btn-save"}},
    {"type": "assert_text", "target": {"selector": "#markdown-output"},
                            "contains": "**simple markdown document"},

    {"type": "log", "message": "--- italic + underline stack ---"},
    {"type": "click",       "target": {"selector": "#toolbar .btn-italic"}},
    {"type": "click",       "target": {"selector": "#toolbar .btn-underline"}},
    {"type": "assert_count","target": {"selector": "#doc p:nth-of-type(1) em u"}, "count": 1},

    {"type": "log", "message": "--- list indent ---"},
    {"type": "click",       "target": {"selector": "#doc ul li:nth-of-type(2)"}},
    {"type": "key_press",   "key": "Tab"},
    {"type": "assert_count","target": {"selector": "#doc ul ul li"}, "count": 1},

    {"type": "log", "message": "--- insert table ---"},
    {"type": "click",       "target": {"selector": "#toolbar .btn-table"}},
    {"type": "assert_count","target": {"selector": "#doc table"},       "count": 1},
    {"type": "assert_count","target": {"selector": "#doc table tr"},    "count": 3},
    {"type": "assert_count","target": {"selector": "#doc table th"},    "count": 3},

    {"type": "log", "message": "--- insert image (programmatic prompt stub) ---"},
    {"type": "click",       "target": {"selector": "#toolbar .btn-image"}},
    {"type": "type",        "target": {"selector": "#image-url-input"}, "text": "logo.png"},
    {"type": "key_press",   "key": "Enter"},
    {"type": "assert_count","target": {"selector": "#doc img[src='logo.png']"}, "count": 1},

    {"type": "log", "message": "--- copy / paste round trip ---"},
    {"type": "tripleclick", "target": {"selector": "#doc h1"}},
    {"type": "key_combo",   "key": "c", "mods_str": "super"},
    {"type": "click",       "target": {"selector": "#doc p:last-of-type"}},
    {"type": "key_press",   "key": "End"},
    {"type": "key_combo",   "key": "v", "mods_str": "super"},
    {"type": "click",       "target": {"selector": "#toolbar .btn-save"}},
    {"type": "assert_text", "target": {"selector": "#markdown-output"},
                            "contains": "Simple Markdown Test"},

    {"type": "log", "message": "--- undo / redo ---"},
    {"type": "key_combo",   "key": "z", "mods_str": "super"},
    {"type": "key_combo",   "key": "z", "mods_str": "super"},
    {"type": "key_combo",   "key": "z", "mods_str": "super"},
    {"type": "assert_count","target": {"selector": "#doc table"}, "count": 0},
    {"type": "key_combo",   "key": "z", "mods_str": "super shift"},
    {"type": "assert_count","target": {"selector": "#doc table"}, "count": 1},

    {"type": "log", "message": "--- caret + selection ---"},
    {"type": "click",       "target": {"selector": "#doc p:nth-of-type(1)"}},
    {"type": "key_press",   "key": "Right", "mods_str": "shift"},
    {"type": "key_press",   "key": "Right", "mods_str": "shift"},
    {"type": "assert_selection", "is_collapsed": false, "check_dom": true}
  ]
}
```

The smaller `rte_*.json` files cover one feature each, keeping individual runs under one second. They are auto‑discovered by [test/test_ui_automation_gtest.cpp](../test/test_ui_automation_gtest.cpp).

---

## 8. Phased Plan

| Phase | Scope | Exit criterion |
|---|---|---|
| **S2.1 — Page shell + parse + render** | `rte_prototype.ls` boots: `input(SOURCE_PATH, 'markdown)` → `edit_open` → `edit <rte_doc>` renders document via `view <doc_block>` / `view <doc_inline>` for the markdown subset used by `simple.md`. | `./lambda.exe view test/ui/rte_prototype.ls` shows visually correct `#doc` with `data-spath` on every node. |
| **S2.2 — Toolbar + emit/dispatch** | `view <rte_toolbar>` with all buttons; `on click` `emit("rte_cmd", …)`; `edit <rte_app>` (or `edit <rte_doc>`) `on rte_cmd` runs `editor = edit_exec(…)`. | Clicking `.btn-bold` over a selection wraps it in `<strong>` and the toolbar's `class:"active"` flips on. |
| **S2.3 — Selection sync** | `edit <rte_doc>` keeps `state` for `SourceSelection`; click / `on keydown` translate platform events into `InputIntent`s for `edit_dispatch`. | `rte_caret.json` passes; `assert_selection` agrees with the DOM selection. |
| **S2.4 — Lists + tables + images + links** | Add `edit_cmd_wrap_list` to `mod_editor.ls`; image / link buttons open an inline `<input>` overlay rendered by a `view <prompt>` template; submit calls `edit_cmd_insert_image` / `edit_cmd_insert_link`. | `rte_lists.json`, `rte_table.json`, `rte_image.json` pass. |
| **S2.5 — Clipboard** | Wire `Cut/Copy/Paste` buttons + `Cmd+X/C/V` in `on keydown` to `edit_cmd_paste_text` / `paste_html` and `output(slice, 'markdown)` for copy. | `rte_clipboard.json` passes; round-trip preserves marks and lists. |
| **S2.6 — Undo / redo + save** | `Cmd+Z` / `Cmd+Shift+Z` dispatch `history_undo` / `redo`; Save button / `Cmd+S` calls `output(editor.doc, 'markdown)` and writes `markdown_output` state. | `rte_undo.json` passes; `#markdown-output` populated only after Save. |
| **S2.7 — Underline schema** | Add `u` mark entry to `mod_md_schema.ls` (serialises to `<u>`). | Bold + italic + underline stack survives round-trip through markdown. |
| **S2.8 — End-to-end script + CI** | `rte_prototype.json` discovered by GTest; added to `make test-radiant-baseline` allowed-set. | `make test-radiant-baseline` green. |

Each phase ends with a runnable `./lambda.exe view test/ui/rte_prototype.ls --event-file test/ui/rte_*.json --headless --no-log` invocation showing zero failed assertions.

---

## 9. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| `view`/`edit` template re-runs on every state change might be expensive for large docs. | The framework already dirty-tracks `(source_item, template_ref)` pairs; each `apply()` call participates. Keep `view <doc_block>` shallow so per-paragraph re-renders touch minimal subtrees. |
| Selection mapping between rendered HTML and the source Mark tree. | Each `view <doc_block>` / `view <doc_inline>` template emits `data-spath` derived from the matched element's path. The framework already supplies `(source_item, template_ref)` reverse lookup via `render_map`; we use that to compute `SourcePos` on click. |
| `contenteditable` semantics in Radiant are partial. | We **own all DOM mutation** via the templates; the host element only carries selection/focus events. `contenteditable="true"` is used purely for caret + focus, not for text insertion. All input goes through `edit_dispatch(editor, intent)`. |
| Image / link prompt UX in headless mode. | Provide a `view <prompt>` template that renders a tiny inline `<input class:"prompt-input">` plus `<button class:"prompt-ok">` so UI automation can `type` + click. |
| Save on every keystroke would be slow. | Save is explicit (button or `Cmd+S`); the only state field touched on edit is `editor`, not `markdown_output`. |

---

## 10. Out of Scope (Stage 2)

- Collaborative editing (`mod_collab.ls` skeleton stays as a placeholder).
- Full IME composition over structured DOM (textarea path already covers IME today; structured IME is Stage 3).
- Math / mermaid / mixed‑format embeds (atomic block plumbing exists; specific editors per embed are Stage 3).
- WYSIWYG print/PDF editing.
- Multi‑cursor.

---

## 11. Acceptance Criteria
A reviewer running:

```bash
make build
./lambda.exe view test/ui/rte_prototype.ls \
  --event-file test/ui/rte_prototype.json --headless --no-log
```

must see:

- `Result: PASS` with zero failed assertions.
- The serialised markdown in `#markdown-output` round‑trips through `lambda/format/markdown` without losing the bold / italic / underline marks, list nesting, table structure, or inserted image.
- `make test-radiant-baseline` stays green.
- Manually: `./lambda.exe view test/ui/rte_prototype.html` opens the prototype, the toolbar is responsive, typing in `#doc` mutates the source tree (visible in `#markdown-output`), and undo/redo restore both content and selection.

When that bar is met, Lambda has a Slate/ProseMirror‑class rich‑text editing prototype running on its own runtime, with every keystroke a typed transaction over the source Mark tree and every visible change a derived projection through `render_map`.
