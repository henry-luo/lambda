# Radiant State Store Dump — Design

## Goal

Provide a debug/inspection facility that dumps the full Radiant `DocState`
(see [`radiant/state_store.hpp`](../../radiant/state_store.hpp)) to a log file in
**Mark Notation**, structured as a tree that mirrors the source HTML element
tree. Each element is annotated with its changed runtime states as Mark
attributes. The dump is compact: elements without changed runtime state are
pruned, and state values equal to their DOM/form defaults are omitted.

Because the output is Mark, the dump is a first-class Lambda value: it can be
re-parsed, schema-validated, and diffed structurally between frames.

This supersedes the earlier JSON-record sketch. Rationale: a Mark tree that
mirrors the DOM is far easier to comprehend than a flat list of
`(node, name) → value` records, and it is a native, re-parseable format.

---

## 1. Confirmation: MarkBuilder string-output capability

**Question:** can `MarkBuilder` write Mark text directly instead of building a
tree?

**Answer: not today.** `MarkBuilder`
([`lambda/mark_builder.hpp`](../../lambda/mark_builder.hpp)) only ever
*materializes an arena-allocated tree*. Every construction path
(`element()/map()/array()` → `.attr()/.put()/.append()` → `.final()`) allocates
`Element`/`Map`/`Array`/`String` structs from the `Input` arena and returns an
`Item`. There is no streaming or string-emit backend, and the builder requires
an `Input*` (pool, arena, name_pool, type_list) to exist.

The canonical Mark **text** serializer is a *separate* function:

```cpp
// lambda/ast.hpp
void print_item(StrBuf *strbuf, Item item, int depth=0, const char* indent="  ");
void print_root_item(StrBuf *strbuf, Item item, const char* indent="  ");
```

`print_root_item` ([`lambda/print.cpp:763`](../../lambda/print.cpp)) already
emits exact Mark syntax — `<tag attr:val, attr2:val; child child "text">` — with
indentation, correct string/symbol escaping, and nested maps/arrays. It writes
into a growable `StrBuf` (`lib/strbuf.h`), so there is **no fixed-buffer limit**
(unlike the `JsonWriter` used by the event-state log, which silently overflows
past 4 KB).

### Implication for "use MarkBuilder to dump, writing string directly"

Two ways to satisfy the intent, in order of preference:

- **Option A — build tree, then serialize (recommended).** Use `MarkBuilder` to
  build the pruned, annotated tree (small — only the stateful spine), then call
  `print_root_item(sb, root)` and write `sb` to the dump file. No change to
  `MarkBuilder`. Reuses the canonical, tested serializer and its indentation. The
  intermediate tree is transient (built in a scratch `Input`/arena, discarded
  after serialization).

- **Option B — add a streaming string mode to MarkBuilder (only if needed).**
  `MarkBuilder`'s fluent API is *bottom-up* (`parent.child(builtItem)` requires
  the child `Item` to already exist), whereas streaming text is *top-down*. A
  faithful string mode therefore needs each `ElementBuilder`/`MapBuilder`/
  `ArrayBuilder` to render into a **per-builder `StrBuf` fragment**; `final()`
  returns the fragment and the parent appends it. **Critically, this must reuse
  the scalar/value emitters from `print.cpp` (factor them into a shared
  `print_scalar(StrBuf*, Item)`), not duplicate serialization logic** — otherwise
  the two Mark serializers will diverge. Constraint: a string-mode builder is
  write-once and not re-readable; attributes must be emitted before children.

**Decision: Option A.** The dump is occasional and the materialized tree contains
only stateful elements, so allocation cost is negligible, and we avoid
reimplementing (or refactoring) the serializer. A future enhancement may add the
string-streaming mode to `MarkBuilder` (Option B) so the tree-building step can be
elided, but that is out of scope here.

---

## 2. Output format

### 2.1 Tree shape

One Mark element tree per dump. The synthetic root is `<doc>`, carrying
document-level state; its descendants mirror the source HTML element tree.

```
<doc  doc-level-states...;
  <body index:1;
    <form index:0, id:"loginForm";
      <input  index:2, id:"agree", flags:[checked, focused]>
      <button index:5, id:"submit", flags:[hover, focus, focus-visible]>
    >
    <div index:3, id:"feed", flags:[hover], scroll:{y:320, max_y:1180, v_dragging:true}>
  >
>
```

- **Nesting** = DOM parent/child. Tag name = DOM tag name.
- **`index`** = the element's nth-child position **among all source DOM child
  nodes** (not among surviving siblings, and not same-tag sibling index), so a
  reader can map each node back to the real DOM even though siblings were
  pruned.
- **`id`** = author `id` attribute when present (preferred stable handle).

### 2.2 Pruning rule

> Include an element **iff** it has its own changed runtime state **OR** it has a
> descendant with changed runtime state.

This keeps nesting identical to the HTML tree while emitting only the stateful
"spine":

- **Stateful element** → changed annotation set (see §3).
- **Stateless connector ancestor** (on the path to a stateful node) → only
  `index` and `id` (if present) — just enough to reconstruct the path.
- **Stateless leaf subtree** → dropped entirely.

Implementation: a single post-order DFS returns "did this subtree contain any
state"; an element is emitted iff that is true for it or any descendant.

### 2.3 Default-omission (compactness)

Applied at every level so the dump stays small:

1. **Element-level:** an element appears only if it (or a descendant) is stateful.
2. **Attribute-level:** a state attribute is emitted only if its value differs
   from the relevant baseline. For form controls, that baseline is the
   author/default control state: the matching element attribute when present
   (`checked`, `value`, `selected`, `disabled`, `readonly`, `required`, `min`,
   etc.), otherwise the initial/default control state computed by the form
   helpers. E.g. `scroll:{...}` includes only the sub-fields that are non-zero;
   `selected_index`, `range_value`, text values, and form flags are dropped when
   they still match their DOM/form defaults.
3. **Flag-level:** boolean pseudo-states and `ViewState` interaction bits collapse
   into a single sparse `flags:[...]` array listing only the set bits. An element
   with no set flags omits `flags` entirely.
4. **Doc-level:** `<doc>` attributes are emitted only when active —
   empty selection / absent caret / absent dropdown / `zoom:1.0` / zero document
   scroll are simply not written.

A fully default element contributes nothing; a document at rest with no
interaction produces just `<doc>` (or an empty dump).

### 2.4 Default baseline rule

The dump represents **changed runtime state relative to DOM/form defaults**.
When a state corresponds to an author attribute, the attribute value is the
baseline. When there is no matching author attribute, the baseline is the
initial/default control state already used by Radiant's control helpers.

Examples:

- A checkbox with `checked` in the source omits `form:{checked:true}` until the
  user unchecks it.
- A checkbox without `checked` omits `form:{checked:false}` until the user checks
  it.
- A `<select>` uses the first selected `<option>` as the default; if none is
  selected, it uses the control helper's initial/default selected index.
- A range input compares `range_value` against the control's default range value,
  not literal `0`.
- Runtime-only interaction states such as `hover`, `focus`, `active`,
  `dropdown_open`, and scrollbar dragging use their inactive value as baseline.

---

## 3. State → Mark attribute schema

States come from three places in `DocState`; they map onto element attributes as
follows. All fields obey default-omission (§2.3).

### 3.1 Per-element states (on each stateful element)

| Source in `DocState` | Mark attribute |
|---|---|
| `state_map` boolean pseudo-classes (`:hover`, `:active`, `:focus`, `:focus-visible`, `:focus-within`, `:checked`, `:disabled`, `:selected`, `:target`, `:placeholder-shown`, …) | merged into `flags:[hover, checked, focus, …]` |
| `view_state_map` → `ViewState.flags` (`hovered`/`active`/`focused`) | merged into the same `flags:[…]` |
| `state_map` named values (`value`, `selection-start`, `selection-end`) | `value:"…"`, `sel:[start, end]` |
| `ViewState.data.scroll` | `scroll:{x, y, max_x, max_y, h_dragging, v_dragging}` |
| `ViewState.data.form` | `form:{checked, selected_index, range_value, sel:[s,e], value:"…", dropdown_open, hover_index}` |

`flags` uses short, lower-case names without the leading `:` (e.g. `:focus-visible`
→ `focus-visible`). Pseudo-flags and `ViewState` interaction bits are merged and
de-duplicated into one array.

### 3.2 Document-level states (on the `<doc>` root only)

| Source | Mark attribute |
|---|---|
| `focus` (`FocusState`) | `focus:{target:"#submit", visible:true, from_keyboard:true}` |
| `caret` (`CaretState`) | `caret:{target:"#comment", offset:14, line:2, column:0}` |
| `selection` (`SelectionState`) | `selection:{anchor:{node:"#p", offset:3}, focus:{node:"#p", offset:9}}` when non-empty |
| `hover_target` / `active_target` / `drag_target` | `hover:"#submit"`, `active:"#btn"`, `drag:"#card"` |
| `drag_drop` (`DragDropState`) | `drag_drop:{source:"#card", target:"#bin", active:true}` |
| `open_dropdown` + geometry | `dropdown:{owner:"#sel", x, y, w, h}` |
| `context_menu_target` + geometry | `context_menu:{target:"#input", hover:2}` |
| `scroll_x/scroll_y`, `zoom_level` | `scroll:{x, y}` when non-zero, `zoom:N` when not `1.0` |
| `is_dirty`/`needs_reflow`/`needs_repaint`, `reflow_scheduler.pending`, `dirty_tracker` | `render:{dirty:true, reflow:true, repaint:true, pending:N, rects:N}` with false/zero fields omitted |
| `editing` (`EditingInteractionState`) | `editing:{…}` (composition / autoscroll / active surface — only when active) |
| `visited_links` count (privacy-preserving) | `visited:{count:N}` (no URLs) |
| video (`has_active_video`, controls) | `video:{active:true, …}` (only when present) |

Element references inside doc-level maps use the same compact handle as element
keys: `"#id"` when an author `id` exists, otherwise a **source child-index path**
from the document root, e.g. `"0.1.3"` or `"html.1.body.0.form.2"` depending on
the final helper format. Every numeric segment must be the nth-child index among
all source DOM child nodes, matching the emitted `index` attribute.

This should be implemented as a new shared helper rather than directly reusing
the existing event log `build_node_path()` because that helper currently counts
same-tag siblings and may fall back to `src:line` / `node:id` forms. No
`src:line` form is used in the Mark dump.

---

## 4. Worked example

A login form where the submit button is hovered+focused via keyboard, the
"agree" checkbox is checked and focused, the feed div is mid scroll-drag, and the
caret sits in a comment field:

```
<doc  focus:{target:"#submit", visible:true, from_keyboard:true},
      caret:{target:"#comment", offset:14, line:2},
      hover:"#submit",
      scroll:{y:320},
      render:{dirty:true, repaint:true, rects:2};
  <body index:1;
    <form index:0, id:"loginForm";
      <input  index:2, id:"agree", flags:[checked, focused]>
      <button index:5, id:"submit", flags:[hover, focus, focus-visible]>
    >
    <div index:3, id:"feed", flags:[hover], scroll:{y:320, max_y:1180, v_dragging:true}>
  >
>
```

`<html>` (no state) collapses to just the `<doc>` root + `<body>` connector.
`<form>` is a stateless connector (only `index` + `id`) kept because its children
are stateful. Every other stateless element in the document is absent.

---

## 5. Implementation

### 5.1 Entry point

```cpp
// radiant/state_store.cpp  (it already owns the map-iteration + ViewState accessors)

// Build + serialize the dump in memory, or write it to a file.
StrBuf* radiant_state_dump_mark(DocState* state);
bool    radiant_state_dump_to_file(DocState* state, const char* path);
```

### 5.2 Algorithm (Option A — build then serialize)

1. Create a scratch `Input` + `MarkBuilder` (transient; discarded after dump).
   Do not reuse the live document `Input`, otherwise debug dumps permanently
   allocate dump-only objects in the document arena.
2. **Post-order DFS** over the DOM from the document root:
   - For each element, gather its states from `state_map` (grouped by node) and
     `view_state_map` (by `view_id`), applying default-omission against the
     baseline rule in §2.4.
   - Recurse into children, collecting their built sub-elements.
   - An element is *kept* iff it has changed state or any child was kept.
   - If kept, build an `ElementBuilder` for it: set `index`, optional `id`, then
     the state attributes (`flags`, `scroll`, `form`, `value`, `sel`), then add
     the kept child elements via `.child()`. Call `.final()`.
   - If not kept, return "nothing" up the recursion.
3. Wrap the kept top-level subtree(s) under a synthetic `<doc>` element built with
   the document-level attributes from §3.2.
4. `print_root_item(sb, doc_item)` → `StrBuf`.
5. Write `sb` to the dump file (and/or return the `String`).

State grouping: iterate `state_map` once via `hashmap_iter`
([`state_store.cpp`](../../radiant/state_store.cpp)) into a temporary
`node → flags/values` accumulator keyed by `DomNode*`, so the DFS can look up a
node's states in O(1) instead of re-scanning per element.

### 5.3 Default-omission helpers

Small predicates colocated with the builder, e.g.:

```cpp
static bool view_state_is_default(const ViewState* vs, View* view);   // compares against §2.4 baseline
static void emit_scroll_attr(ElementBuilder& eb, const ScrollSub* s); // skips zero sub-fields
static void emit_form_attr(ElementBuilder& eb, View* view, const FormSub* f); // skips DOM/form defaults
```

`flags` is assembled into an `ArrayBuilder`, appending only set bits; if the
array ends empty, the `flags` attribute is not set on the element.

---

## 6. Output channel & triggering

- **Mode: per-cascade time series.** A dump is emitted at the end of each event
  cascade (alongside the existing `radiant_state_settle` snapshot in
  [`state_machine.cpp`](../../radiant/state_machine.cpp)), gated behind a
  verbosity/CLI flag so it is off by default. Each cascade appends one
  self-contained Mark tree to the series, tagged with the cascade id / sequence
  number so dumps can be correlated with the event-state log.
- **File layout:** one file per document, accumulating the series —
  `./temp/state/state_${pid}_${doc}.mark`. Each entry is a complete Mark value, not a
  comment-delimited record:

  ```
  <entry cascade:17, seq:142;
    <doc ...>
  >
  ```

  This keeps the file human-readable, machine-splittable, and parseable as Mark
  without relying on comment handling. A `radiant_state_dump_to_file()` entry
  point is also exposed for tests and ad-hoc debugging.

Because each entry is a self-contained Mark tree, diffing consecutive cascades
shows exactly what one event changed — structurally, not as text noise.

---

## 7. Comparison to the event-state log

The existing event-state log
([`radiant/event_state_log.hpp`](../../radiant/event_state_log.hpp)) and its
`state.snapshot` record ([`radiant/state_machine.cpp`](../../radiant/state_machine.cpp)
`emit_state_snapshot`) remain as a **streaming JSONL event trace**. This Mark dump
is complementary: a **full, tree-structured snapshot** of the entire state store
at a point in time, optimized for human reading and structural diffing rather than
append-only event streaming. The two share the node-identity helpers
where their semantics match. The Mark dump needs a source child-index path helper
whose numeric segments match emitted `index` attributes; the event-state log can
adopt that helper later if identical compact handles are desired there too.

---

## 8. Scope and staged rollout

The reactivity/data-binding maps in `DocState` are **out of scope for the
initial dump and UI-test integration stages**. They are orthogonal to DOM-element
interaction state.

**Stage 1** dumps only DOM-element interaction state (§3): per-element pseudo /
form / scroll state plus the document-level interaction states on `<doc>`.

**Stage 2:** integrate the Mark state dump with **UI automated testing**.
Current `test/ui/*.json` fixtures verify behavior through targeted JSON event
assertions such as `assert_value`, `assert_selection`, `assert_focus`,
`assert_state_store`, and `assert_event_log`. Add a state-dump assertion that
captures the complete pruned Mark snapshot at selected critical steps and
compares it to a recorded fixture under:

```
test/ui/state/
```

Example fixture event:

```json
{
  "type": "assert_state_dump",
  "reference": "test/ui/state/test_form_input_clipboard_after_paste.mark"
}
```

At runtime, the UI simulator should **not** write a dump file for this assertion.
Instead it should call the in-memory dump API
`radiant_state_dump_mark(DocState*)`, normalize the resulting Mark text, read the
reference fixture, and compare the two strings. This keeps UI tests fast and
avoids churn in `./temp/state/`.
The file-backed `--state-dump` mode remains useful for interactive debugging,
recording new baselines, and investigating failing tests.

State dump assertions should be placed only at semantic checkpoints where the
state store as a whole matters: after a paste, after selection creation, after
drag/drop activation, after dropdown open/close, after scroll persistence, after
IME composition transitions, etc. Existing narrow JSON assertions can then be
reduced where the Mark fixture is strictly stronger. For example, a cluster of
`assert_focus` + `assert_value` + `assert_selection` checks can often become one
`assert_state_dump` plus a small number of DOM/content assertions that the state
dump intentionally does not cover.

Suggested Stage 2 implementation pieces:

1. Add a public in-memory serializer entry point:

   ```cpp
   StrBuf* radiant_state_dump_mark(DocState* state);
   ```

   It should share the same builder/serializer code as file logging and remain
   deterministic across runs.

2. Add a UI fixture event type, `assert_state_dump`, in the event simulator. It
   resolves the active document state, builds the Mark string in memory, reads
   the fixture path, normalizes line endings/trailing whitespace, and reports a
   useful diff on mismatch.

3. Add a small fixture update path for intentional changes, gated explicitly
   (for example by a CLI flag or environment variable), that writes the in-memory
   dump to `test/ui/state/...`. This should be opt-in so ordinary test runs never
   rewrite baselines.

4. Migrate high-value UI tests incrementally. Start with tests that currently
   rely on many state-specific JSON assertions, such as form input/clipboard,
   selection, drag/drop, dropdown, scroll persistence, and editing composition
   flows.

**Future stage:** add **`template_state_map`** (Lambda template reactive state,
keyed by `(model_item, template_ref, state_name)`; see `lambda/template_state.h`)
as a separate `<doc>`-level section, since it describes the template/data-binding
layer rather than the element tree.

**Omitted entirely:** **`render_map`** — the observer-based source→result
reconciliation map ([`lambda/render_map.h`](../../lambda/render_map.h)) that drives
Lambda's two-phase incremental template re-transform. It is an internal
reconciliation index, not user-facing interaction state. Revisit only if a future
debugging workflow needs an explicit reconciliation-map fixture; it is not part
of Stage 1 or Stage 2.
