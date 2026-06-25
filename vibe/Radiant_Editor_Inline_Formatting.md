# Radiant Editor — Inline Formatting (Marks) + Selection Design

**Date:** 2026-06-25
**Status:** Approved
**Resolves:** Divergences #1 and #2 documented during test-corpus build (task #3).
**Related:** [Radiant_Editor_Stage4_Drawings.md §3](Radiant_Editor_Stage4_Drawings.md) (schema), [Radiant_Rich_Text_Editor3.md §3.1](Radiant_Rich_Text_Editor3.md) (mark notes).

---

## 0. TL;DR

Inline formatting (bold, italic, underline, color, link, code, …) is represented as a **flat dictionary on each text leaf**:

```ts
interface TextLeaf {
  kind: 'text'
  text: string
  marks: { [name: string]: AttrValue }   // ← dict, not array, not nested elements
}
```

- **No nesting.** Marks are never elements in the document tree. Adjacent text leaves with different marks coexist as siblings inside a block; adjacent leaves with identical marks are merged by normalization.
- **Fixtures use `<span style="…">`.** Inline-styled text in HTML fixtures is rendered as `<span>` with a `style` attribute. **`<span>`s never nest** — that's the load-bearing rule.
- **Semantic marks keep their own element.** `<a href>` for links and `<code>` for code — still flat marks in the model, with attributes carried on the element. Same "no nesting" rule applies.
- **Round-trips losslessly.** Parser converts inline elements → dict on text leaves; renderer converts dict → inline elements at draw time.

Companion decision — **selection representation** (§11): `AllSelection` is dropped from the model. "Select all" is just a `TextSelection` whose endpoints sit at the doc-root boundaries; fixtures use the existing `<anchor></anchor>` / `<focus></focus>` markers around the doc content, with no new marker tag invented.

---

## 1. Why flat marks — never nested

These are the three load-bearing reasons. Whenever a future design discussion proposes a nested-mark representation, point at this section.

1. **Mark ordering is unstable as a structural choice.** `<em><strong>x</strong></em>` and `<strong><em>x</em></strong>` are visually identical and semantically equivalent, but they're different trees. Every editor operation would have to canonicalize or risk drift. Flat sets sidestep the problem.

2. **Mark algebra is closed under union / intersection.** Bold-italic = `{bold, italic}`. Removing bold from a bold-italic range = `{italic}`. Toggling = symmetric difference. These are set operations on a flat array (or dict), not tree manipulations.

3. **Selections over marks are straightforward.** "Make the selected range bold" is "for each text leaf overlapping the range, ensure `bold` is in its mark set". With a nested tree you'd be doing wrap/unwrap with all the boundary edge cases (split at start, split at end, merge with neighbors).

Slate adopted (1)–(3) by putting marks directly as properties on text leaves; ProseMirror adopted (1)–(3) by giving text nodes a `Mark[]` set. We adopt (1)–(3) by giving text leaves a `marks` **dictionary** — strictly simpler than PM's mark-with-attrs object and avoiding Slate's "no marks field, just arbitrary properties" approach.

---

## 2. Mark representation

### 2.1 Type

```ts
type AttrValue = string | number | boolean | null | AttrValue[] | { [k: string]: AttrValue }

interface TextLeaf {
  kind: 'text'
  text: string
  marks: { [name: string]: AttrValue }
}
```

The dict shape is the canonical JSON representation. It is what `mod_doc.ls` will store, what fixtures will reflect, and what every transform will read and write.

### 2.2 Examples

Plain text — empty dict, always present:

```json
{ "kind": "text", "text": "plain", "marks": {} }
```

Boolean marks (presence ≡ "applied") — typical for style:

```json
{ "kind": "text", "text": "important", "marks": { "bold": true, "italic": true } }
```

Value marks — when the mark carries data:

```json
{ "kind": "text", "text": "warning", "marks": { "color": "#ff0000" } }
{ "kind": "text", "text": "click",   "marks": { "link": "https://example.com" } }
{ "kind": "text", "text": "$ ls",    "marks": { "code": true, "fontFamily": "monospace" } }
```

Combined — multiple marks at once, including booleans and values:

```json
{
  "kind": "text",
  "text": "bold red link",
  "marks": {
    "bold": true,
    "color": "#ff0000",
    "link": "https://example.com"
  }
}
```

### 2.3 Equality and merging

Two text leaves are mergeable iff their `marks` dictionaries are deeply equal — same keys, same values. Normalization merges adjacent mergeable leaves into one.

```ts
marksEqual({}, {})                                 // true
marksEqual({ bold: true }, { bold: true })         // true
marksEqual({ bold: true }, { italic: true })       // false
marksEqual({ color: '#ff0' }, { color: '#ff0' })   // true
marksEqual({ color: '#ff0' }, { color: '#f00' })   // false
```

### 2.4 Set operations

```ts
// add a mark
marks = { ...leaf.marks, bold: true }

// remove a mark
const { bold, ...rest } = leaf.marks
marks = rest

// toggle
marks = ('bold' in leaf.marks)
  ? (() => { const { bold, ...rest } = leaf.marks; return rest })()
  : { ...leaf.marks, bold: true }

// union (e.g., extending a bold range over an italic range)
marks = { ...a.marks, ...b.marks }   // later wins for conflicting values
```

### 2.5 Canonical for serialization

When serializing for fixtures or persistence, keys are ordered alphabetically by `name` to ensure byte-identical output for fixture round-trips:

```json
// canonical
{ "marks": { "bold": true, "color": "#ff0000", "italic": true } }
```

---

## 3. The "no nested spans" rule

In the document model, the rule is: **text leaves are direct children of block-level nodes; there are never inline mark elements between them.**

In HTML fixtures, the rule is expressed structurally: **`<span>` elements never contain other `<span>` elements.** Semantic mark elements (`<a>`, `<code>`) have the same restriction — they contain only text content, never another inline mark element.

### 3.1 What this means visually

A run of italic-bold text:

```html
<!-- ✓ correct: three siblings, no nesting -->
<p>
  <span style="font-style: italic">italic </span>
  <span style="font-style: italic; font-weight: bold">italic bold </span>
  <span style="font-style: italic">italic</span>
</p>

<!-- ✗ WRONG: spans nested inside each other -->
<p>
  <span style="font-style: italic">
    italic
    <span style="font-weight: bold">bold</span>
    italic
  </span>
</p>

<!-- ✗ ALSO WRONG: even legacy nested elements -->
<p><em>italic <strong>bold</strong> italic</em></p>
```

### 3.2 What this means for parser inputs

The parser is **lenient on input** — pasted HTML from the web will have nested `<em>/<strong>` and the parser must accept it. Acceptance does NOT mean preserving the nesting. The parser **flattens nested marks into the dictionary form** during parsing:

```html
<em>italic <strong>bold</strong> italic</em>
```

parses to:

```json
[
  { "kind": "text", "text": "italic ",     "marks": { "italic": true } },
  { "kind": "text", "text": "bold",        "marks": { "italic": true, "bold": true } },
  { "kind": "text", "text": " italic",     "marks": { "italic": true } }
]
```

The normalization step that already merges adjacent same-mark leaves continues to apply.

### 3.3 What this means for fixture authors

When **writing** a fixture by hand, use the `<span style>` form (or semantic elements where appropriate). Lint the fixture directory with a simple "no nested spans" check before committing:

```bash
# pseudocode for the lint
for f in test/**/input.html test/**/output.html; do
  if grep -P '<span[^>]*>[^<]*<span' "$f"; then
    echo "ERROR: $f has nested <span>"; exit 1
  fi
done
```

(A real lint can use a proper DOM walker; the grep is illustrative.)

---

## 4. Fixture HTML conventions

### 4.1 Style marks → `<span style="…">`

For purely visual marks, fixtures use `<span>` with a CSS `style` attribute. The CSS properties used in fixtures come from the canonical mark vocabulary (§5).

```html
<p>
  this is <span style="font-weight: bold">bold</span> text.
  this is <span style="color: #ff0000">red</span> text.
  this is <span style="font-weight: bold; font-style: italic">bold italic</span> text.
</p>
```

### 4.2 Semantic marks → their own element

Two marks carry meaning beyond styling. Both are still flat marks in the model — the dedicated element just carries the mark's attributes more legibly than a `style` attribute could.

| Mark | HTML form | Notes |
|---|---|---|
| **Link** | `<a href="…">text</a>` | `href` is the mark's value. Optional `target`, `title` go in the dict alongside `link` |
| **Code** | `<code>text</code>` | semantic monospace + browser disables spell-check + screen reader semantics |

```html
<p>see <a href="https://example.com">the link</a>.</p>
<p>run <code>npm test</code> to verify.</p>
```

### 4.3 Combining semantic + style on the same run

The semantic element carries the semantic attribute; the `style` attribute goes on the SAME element. Still no nesting.

```html
<!-- bold link -->
<a href="https://example.com" style="font-weight: bold">bold link</a>

<!-- italic code -->
<code style="font-style: italic">italicized monospace</code>
```

Parses to a single text leaf with the combined marks dict:

```json
{
  "kind": "text",
  "text": "bold link",
  "marks": { "bold": true, "link": "https://example.com" }
}
```

### 4.4 Selection markers around marks

The `<cursor>`, `<anchor>`, `<focus>` markers are placed **outside** the `<span>` boundary — never inside, never at a position that would land "inside the bold formatting". The normalization that resolves marker positions onto adjacent text leaves applies as before:

```html
<!-- ✓ caret right before the bold run -->
<p>plain <cursor></cursor><span style="font-weight: bold">bold</span></p>

<!-- ✓ selection wrapping a bold run -->
<p>plain <anchor></anchor><span style="font-weight: bold">bold</span><focus></focus> tail</p>

<!-- ✗ marker inside the span (parser tolerates; conventionally avoided) -->
<p><span style="font-weight: bold">bo<cursor></cursor>ld</span></p>
```

---

## 5. Canonical mark vocabulary

The vocabulary is a stable list of mark names with their HTML projection. Adding a new mark requires updating this table.

### 5.1 Boolean style marks

| Mark name | CSS property | Fixture form |
|---|---|---|
| `bold` | `font-weight: bold` (or `700`) | `<span style="font-weight: bold">x</span>` |
| `italic` | `font-style: italic` | `<span style="font-style: italic">x</span>` |
| `underline` | `text-decoration: underline` | `<span style="text-decoration: underline">x</span>` |
| `strikethrough` | `text-decoration: line-through` | `<span style="text-decoration: line-through">x</span>` |
| `subscript` | `vertical-align: sub; font-size: smaller` | (or `<sub>x</sub>`) |
| `superscript` | `vertical-align: super; font-size: smaller` | (or `<sup>x</sup>`) |

`underline` and `strikethrough` both project to `text-decoration` — when both are set the renderer combines: `text-decoration: underline line-through`.

### 5.2 Value style marks

| Mark name | Value type | CSS property | Fixture form |
|---|---|---|---|
| `color`      | string (CSS color) | `color`            | `<span style="color: #ff0000">x</span>` |
| `background` | string (CSS color) | `background-color` | `<span style="background-color: yellow">x</span>` |
| `fontFamily` | string             | `font-family`      | `<span style="font-family: monospace">x</span>` |
| `fontSize`   | string (CSS len)   | `font-size`        | `<span style="font-size: 14px">x</span>` |

### 5.3 Semantic marks

| Mark name | Value | HTML element | Notes |
|---|---|---|---|
| `link`     | string (href) | `<a href="…">` | extension: `link` can be an object `{href, target, title}` if more attrs needed; the common form is a bare string |
| `code`     | true          | `<code>` | always boolean |
| `kbd`      | true          | `<kbd>`  | keyboard input (future) |

When the parser sees `<a href="https://example.com">x</a>`, it produces `{ marks: { link: "https://example.com" } }`. When the parser sees `<a href="..." target="_blank" title="docs">x</a>`, it produces the object form: `{ marks: { link: { href: "...", target: "_blank", title: "docs" } } }`.

### 5.4 Reserved / future

`comment`, `highlight`, `mention` — declared here as reserved names so they won't collide with custom marks. Implementation when needed.

---

## 6. Parser behavior

### 6.1 Tag → mark conversion table

The parser converts known inline elements into mark dictionary entries, then drops the element itself from the tree.

| HTML input | Action |
|---|---|
| `<span style="...">x</span>` | parse CSS via §5 vocabulary; merge into descendant text-leaves' marks |
| `<strong>x</strong>` / `<b>x</b>` | push `bold: true` into descendants' marks |
| `<em>x</em>` / `<i>x</i>` | push `italic: true` into descendants' marks |
| `<u>x</u>` | push `underline: true` |
| `<s>x</s>` / `<del>x</del>` | push `strikethrough: true` |
| `<sub>x</sub>` | push `subscript: true` |
| `<sup>x</sup>` | push `superscript: true` |
| `<a href="...">x</a>` | push `link: '...'` (or object if more attrs) |
| `<code>x</code>` | push `code: true` |
| `<font color="...">x</font>` | (legacy) push `color: '...'` |
| `<br>` | preserved as a `<br>` node (line break is not a mark) |

### 6.2 Pseudocode

```ts
function parseInline(el: Element, parentMarks: MarkDict): Child[] {
  const out: Child[] = []
  for (const child of el.childNodes) {
    if (isTextNode(child)) {
      out.push({ kind: 'text', text: child.data, marks: { ...parentMarks } })
    } else if (isMarkerElement(child)) {
      recordMarker(child, /* current position */)
    } else if (isInlineMarkElement(child)) {
      const childMarks = marksFromElement(child)
      const merged = { ...parentMarks, ...childMarks }
      out.push(...parseInline(child, merged))
    } else {
      // unknown inline element — recurse without adding marks (passthrough)
      out.push(...parseInline(child, parentMarks))
    }
  }
  return out
}
```

After parsing, the normalization step merges adjacent text leaves whose marks dictionaries are deep-equal.

### 6.3 CSS parsing

The fixture parser handles a small canonical subset of CSS — exactly the properties in §5. A `style` attribute is split on `;`, each piece on `:`, and the table maps property → mark name:

```ts
const CSS_TO_MARK: Record<string, (value: string) => { name: string; value: AttrValue }> = {
  'font-weight':     v => v === 'bold' || +v >= 600 ? { name: 'bold',          value: true } : { name: '', value: null },
  'font-style':      v => v === 'italic'             ? { name: 'italic',        value: true } : { name: '', value: null },
  'text-decoration': v => /underline/.test(v)        ? { name: 'underline',     value: true }
                       :  /line-through/.test(v)     ? { name: 'strikethrough', value: true } : { name: '', value: null },
  'color':           v => ({ name: 'color',      value: v }),
  'background-color':v => ({ name: 'background', value: v }),
  'font-family':     v => ({ name: 'fontFamily', value: v }),
  'font-size':       v => ({ name: 'fontSize',   value: v }),
}
```

Unknown CSS properties are dropped (lenient parser). The `text-decoration` rule is special — it can contribute multiple marks if the value contains multiple keywords.

---

## 7. Renderer behavior

Rendering is **mark-aware coalescing**: walk a block's children, collect runs of text leaves with identical marks, and emit ONE wrapping element per run. The output for a fixture round-trip is the canonical `<span style>` form; for the demo / production editor we may emit `<strong>`/`<em>` for cleaner HTML — same data, two render modes.

### 7.1 Fixture-mode output

```ts
function renderTextLeafForFixture(leaf: TextLeaf): string {
  const tag = pickSemanticTag(leaf.marks)   // 'a' | 'code' | 'span' | null
  const attrs = renderAttrs(leaf.marks, tag)
  if (tag === null) return escapeText(leaf.text)
  return `<${tag} ${attrs}>${escapeText(leaf.text)}</${tag}>`
}

function pickSemanticTag(marks: MarkDict): string | null {
  if ('link' in marks) return 'a'
  if ('code' in marks) return 'code'
  if (Object.keys(marks).length > 0) return 'span'
  return null
}
```

Style marks are emitted as a `style` attribute. Semantic marks contribute their HTML attribute (`href`). Selection markers are emitted as their own elements outside the wrapping span (per §4.4).

### 7.2 Demo-mode output (production editor)

The demo / production editor may prefer cleaner HTML — bold rendered as `<strong>` rather than `<span style="font-weight:bold">`. This is **a renderer mode toggle**, not a different data model:

```ts
const RENDER_MODE: 'fixture' | 'semantic' = 'semantic'
```

In `semantic` mode the renderer emits `<strong>`, `<em>`, `<u>`, `<s>` for the corresponding boolean marks (combined when multiple apply — `<strong><em>x</em></strong>` is acceptable in OUTPUT because it's render-only; the input never has nesting). All non-boolean marks still go through `<span style>`.

For fixtures, always use `fixture` mode for byte-stable round-tripping.

### 7.3 Run coalescing example

Three flat text leaves with the same marks:

```json
[
  { "kind": "text", "text": "italic ",      "marks": { "italic": true } },
  { "kind": "text", "text": "italic bold ", "marks": { "italic": true, "bold": true } },
  { "kind": "text", "text": "italic",       "marks": { "italic": true } }
]
```

Render output (`fixture` mode):

```html
<span style="font-style: italic">italic </span>
<span style="font-style: italic; font-weight: bold">italic bold </span>
<span style="font-style: italic">italic</span>
```

Render output (`semantic` mode):

```html
<em>italic </em>
<em><strong>italic bold </strong></em>
<em>italic</em>
```

(Even in semantic mode, the runs are not merged across leaf boundaries — that's the renderer's choice; merging would require span-level optimization beyond this design's scope.)

---

## 8. Schema impact

The doc schema's mark-role entries (`strong`, `em`, `b`, `i`, `u`, `s`, `a`, `code`, `span`) become **parser-only hints** — entries that say "this tag, when encountered in input HTML, contributes a mark". They are NEVER part of the canonical document tree.

Schema validation runs on the canonical (post-parse) tree. Since marks are no longer tree nodes, the validator never sees them and never validates them as structural elements. Mark validity (allowed-on-which-content) is enforced at the COMMAND layer (e.g., `cmdToggleMark` checks the schema entry to know whether the current block allows the mark).

The schema entries' `role: 'mark'` field continues to mean what it always did — "treat this as a mark on parse". The `content`, `marks: 'all' | 'none'` fields on mark entries become irrelevant (marks have no content in the canonical model).

Concretely, `mod_md_schema.ls`'s `strong: {role: 'mark', ...}` keeps its `role` but its `content` is unused. A future cleanup can drop the unused fields.

---

## 9. Command impact

### 9.1 `cmdToggleMark`

The simplified algorithm:

```ts
function cmdToggleMark(state: EditorState, mark: string): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'text') return null
  if (selCollapsed(sel)) return cmdToggleStoredMark(state, mark)

  // For each text leaf overlapping the selection range:
  //   1. Split the leaf at the range boundaries so the affected portion is its own leaf.
  //   2. Toggle `mark` on that affected sub-leaf's marks dict.
  // Implemented as a single `replace` step on the parent block, swapping the
  // affected leaves with the (split + toggled) sequence.

  return buildToggleMarkTx(state, sel, mark)
}
```

The transaction produces a `replace` step that swaps `block.content[from..to]` with the same content but with the relevant sub-leaves' marks toggled. Adjacent post-toggle leaves with identical marks are merged.

### 9.2 `storedMarks` for collapsed selections

When the user presses Cmd+B with no range selected, we set `storedMarks = { bold: true }` (or merge into the existing storedMarks). The next `cmdInsertText` reads `state.stored_marks` and applies those marks to the inserted text leaf. This already exists in the editor's state — the format change is just from `Mark[]` to `MarkDict`.

### 9.3 No other command changes required

Every other command treats text leaves opaquely as `{ kind, text, marks }`. `cmdInsertText`, `cmdDeleteBackward`, etc. preserve the existing leaf's marks; the only mark-aware command is `cmdToggleMark`.

---

## 10. Migration

### 10.1 Type changes

```ts
// before
interface TextLeaf {
  kind: 'text'
  text: string
  marks: Mark[]            // Mark = string
}

// after
interface TextLeaf {
  kind: 'text'
  text: string
  marks: { [name: string]: AttrValue }
}
```

Helper rename: `hasMark(marks, m)` → `m in marks`. `withMark` / `withoutMark` become dict ops.

### 10.2 Fixture migration

Existing fixtures using `<strong>`, `<em>` etc. continue to parse correctly (the parser accepts them and flattens), but the **canonical** form for new and rewritten fixtures is `<span style>`. Migrate existing fixtures in bulk by running the parser → renderer round-trip with `RENDER_MODE='fixture'` and overwriting the file.

### 10.3 Add-mark fixture (the one that motivated this)

Before:
```html
<!-- input -->  <doc><p>hello <anchor></anchor>there<focus></focus>!</p></doc>
<!-- output --> <doc><p>hello <anchor></anchor><strong>there</strong><focus></focus>!</p></doc>
```

After:
```html
<!-- input -->  <doc><p>hello <anchor></anchor>there<focus></focus>!</p></doc>
<!-- output --> <doc><p>hello <anchor></anchor><span style="font-weight: bold">there</span><focus></focus>!</p></doc>
```

Once `cmdToggleMark` is updated per §9.1, this fixture goes from "infrastructure follow-up" to passing.

### 10.4 Lambda port alignment

`mod_doc.ls` already represents marks as `[symbol...]` (the array form). The migration is a single field-shape change:

```ls
// before
text_leaf = { kind: 'text', text: string, marks: [symbol...] }

// after
text_leaf = { kind: 'text', text: string, marks: { name: value, ... } }
```

Lambda's map type handles this natively. Step kinds `add_mark` and `remove_mark` adjust their payloads (`mark` becomes `{name, value}`). The MIR-side memory layout is unchanged — marks are just a map field on the leaf record.

---

## 11. Selection Representation

The flat-marks decision triggered a parallel simplification of the selection model: **`AllSelection` is dropped** in favor of a `TextSelection` whose endpoints sit at the doc-root boundaries.

### 11.1 Why drop it

A `TextSelection`'s endpoints are `SourcePos` values — `{path, offset}` pairs. There is no constraint that they must land inside a text leaf; they can sit at any path, including non-leaf positions at the doc root. So "everything in the doc is selected" is just:

```ts
{
  kind: 'text',
  anchor: { path: [], offset: 0 },                       // before doc.content[0]
  head:   { path: [], offset: doc.content.length }       // after the last child
}
```

Every other `TextSelection` operation already handles non-leaf endpoints — cross-leaf selection, multi-block ranges, etc. So once the all-spanning form works (and it does), `AllSelection` becomes redundant: a second representation of the same state.

ProseMirror keeps `AllSelection` because in collaborative editing "I want everything, even if other peers add content" is meaningfully different from "I want this specific range as captured at this moment." For single-user flow editing the distinction doesn't pay rent. Drop it.

### 11.2 Fixture form

`Ctrl+A` / `cmdSelectAll` leaves the state with the all-spanning `TextSelection`. The fixture form is just `<anchor></anchor>` + `<focus></focus>` around the doc's content:

```html
<doc>
  <anchor></anchor>
  <p>hello</p>
  <p>world</p><focus></focus>
</doc>
```

Authoring tip: indent for readability — the anchor sits between `<doc>`'s opening tag and the first block child; the focus sits between the last block child and `</doc>`'s closing tag. Whitespace-only text nodes between block children are stripped by the parser per existing rules, so the indentation doesn't affect parsing.

### 11.3 The updated Selection union

```ts
type Selection =
  | TextSelection
  | NodeSelection
  | MultiNodeSelection
// AllSelection removed
```

`NodeSelection` stays because of semantic divergence (§11.4); `MultiNodeSelection` stays for drawings (§11.5).

### 11.4 What stays: NodeSelection

`NodeSelection` (selecting an image, or in Stage 4 a shape, as an atomic unit) is **not** subsumed by the all-spanning TextSelection. The semantics differ:

| State | Behavior of pressing Delete |
|---|---|
| `TextSelection` containing an image | range delete: image + any surrounding selected text |
| `NodeSelection` on the image      | atomic delete: just the image |

`NodeSelection` keeps its own kind. Its fixture representation is **deferred** until the first NodeSelection fixture forces a choice; the likely form is a `<select-node target="0,3"/>` marker placed at the doc root, but we finalize when needed — not preemptively.

### 11.5 Deferred: MultiNodeSelection for drawings

Multi-shape selection (Stage 4) lives in `MultiNodeSelection { paths }`. Its fixture representation is a Stage-4-specific decision **deferred** until the first Tier-0 multi-select fixture is authored. Same principle as §11.4: don't invent markers until a fixture demands them.

### 11.6 Marker vocabulary is unchanged

After this decision, the fixture marker set is still exactly three custom elements:

| Element | Meaning |
|---|---|
| `<cursor></cursor>` | Collapsed caret position |
| `<anchor></anchor>` | Range start |
| `<focus></focus>`   | Range end |

No `<select-all>`, no `<select-node>`, no `<select-multi>` — adding them would invent representations for states that either fit the existing three markers (AllSelection) or aren't yet exercised by any fixture (NodeSelection, MultiNodeSelection).

### 11.7 Implementation impact

Five code sites change, roughly 30 lines total:

| Site | Change |
|---|---|
| `model/types.ts` Selection union | drop `AllSelection` |
| `model/source-pos.ts` | drop the `allSelection()` factory |
| `commands/text-commands.ts:cmdSelectAll` | build a `TextSelection` at doc boundaries instead of returning `allSelection()` |
| `model/source-pos.ts:selectionToString` | drop the `sel.kind === 'all'` branch — the cross-leaf path handles it correctly |
| `model/transaction.ts:selMap` | drop the `case 'all'` branch |
| `test/tier_b_prosemirror/state/select-all/expand` | un-skip — now passes |

### 11.8 Round-trip stability

A doc that starts with `selection = TextSelection(start, end)` and is saved → reloaded via the fixture format gets back the same selection bit-for-bit. Parser places anchor at `path=[]` offset `0`, focus at `path=[]` offset `N`. No special handling, no normalization corner cases.

### 11.9 Lambda port alignment

`mod_source_pos.ls` already provides all the primitives needed:

```ls
pub fn text_selection(anchor, head) => {kind: 'text', anchor: anchor, head: head}
pub fn node_selection(path)         => {kind: 'node', path: path}
pub fn all_selection()              => {kind: 'all'}     // ← removed
```

Drop `all_selection()` from the Lambda port; replace its single usage site (`cmd_select_all`) with the explicit all-spanning text_selection construction. The Lambda port stays 1:1 with the JS reference.

---

## 12. Open questions

1. **Should the dict allow nested objects as values, or constrain to scalars?** Position: allow scalars + flat objects (for `link: { href, target, title }`). Reject nested objects more than one level deep.

2. **CSS shorthand parsing (e.g. `font: bold 14px monospace`)?** Position: not in v1. Fixtures expand to longhand; pasted HTML with shorthand is accepted but only longhands per §5.3 are extracted (shorthand contributions silently dropped is acceptable for a fixture format).

3. **How does this interact with decorations?** Decorations are non-document overlays per Reactive_UI §8 — they have their own data structures and don't touch text-leaf marks. Decorations may carry mark-like styles (highlight, spellcheck squiggle) but they live in `decoration_set`, not in the leaf.

4. **Should we ship a `<span class="…">` form for theming?** Not in this design. Marks are about content semantics + applied formatting; class-based theming is a presentation concern that lives in the CSS layer.

---

## 13. Summary

The doc model gets two changes:

1. **Marks** — `marks: Mark[]` → `marks: { [name: string]: AttrValue }` (flat dict on every text leaf).
2. **Selection** — drop `AllSelection` from the `Selection` union; "select all" is a `TextSelection` at doc boundaries.

The fixture format gets two rules:

1. **`<span>` never nests** — inline-styled text always uses a single non-nested `<span style="…">` (or a semantic element like `<a>`/`<code>` for marks with semantics).
2. **No new selection markers** — `<cursor>`, `<anchor>`, `<focus>` cover every selection state currently in scope.

The parser and renderer get small adaptations to the canonical vocabulary. In exchange:

- Mark ordering ambiguity disappears
- Mark algebra is set operations on a dict
- Selection model loses a redundant variant
- The `add-mark` and `select-all/expand` fixtures both pass
- ~165 future Slate / PM fixtures involving marks become portable without per-fixture work
- One fewer concept in the editor (`AllSelection` gone)
- The Lambda port's mark + selection representations align with Slate / PM, the field-leading rich-text editors

Adopt **Path A from divergence #1** (flat dict marks + `<span style>` fixture form + no nesting) AND **the simplification from divergence #2** (drop `AllSelection`). This document is the canonical reference for both marks and selection decisions going forward.
