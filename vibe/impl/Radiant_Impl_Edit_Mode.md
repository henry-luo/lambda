# Radiant Edit Mode — Implementation Plan and Record

**Date:** 2026-09-26
**Status:** Phases 1–3 implemented (master `060763130`); view-only kept
parts (§3.3) implemented in worktree `edit-view-only`, uncommitted; open
items in §5.
**Design:** [Radiant Edit Mode](../radiant/Radiant_Design_Edit_Mode.md) (proposal).
**Scope:** Phases 1–3 of the proposal: the `lambda edit` shell and Markdown
rich text, HTML rich text with a retained envelope, and basic SVG drawing.

The proposal's §10 product choices are implemented as proposed: semantic
(content-preserving) serialization with normalized spelling, basic SVG
shape/text tools first, local existing files only, explicit Save (no
autosave), and an authoring stylesheet for HTML with inactive source scripts.

## 1. Layering as built

| Layer | Owns | Files |
|---|---|---|
| CLI | `edit` command, shared launch options | `lambda/main.cpp` |
| Loader contract | path-source document transforms | `lambda/runtime/transpiler.hpp`, `transpile-mir.cpp` |
| DOM geometry | client bounds of elements drawn by an `<svg>` | `lambda/dom/dom.cpp`, `radiant/view_pool.cpp` |
| Shell host | edit app mode: Escape routing, guarded close, title | `radiant/window.cpp`, `radiant/event.cpp` |
| DOM waist | `set_close_guard`, `request_window_close`, `set_window_title` | `lambda/dom/dom_api.def`, `lambda/module/radiant/radiant_module.cpp` |
| `lambda.edit` | format registry (`edit`), session, shell, toolbar, rich-text surface, drawing surface, adapters (`markdown`, `html`, `svg`), shared adapter helpers (`model`) | `lambda/package/edit/*.ls` |
| `lambda.editor` | model, commands, history, selection bridge (reused) | `lambda/package/editor/*.ls` |
| Readers and writers | Markdown, HTML, XML readers and formatters (fixed where lossy, §2.4) | `lambda/input/*`, `lambda/format/*` |

Format dispatch has one owner (proposal §4): native code selects edit mode
and hands the package a path; `lambda.edit.edit.open_document` selects the
adapter from its descriptor registry (`open` is a reserved word). Native code
never learns about Markdown, HTML, or SVG editing.

## 2. Native decisions

### 2.1 Path-source transforms

`LambdaDocumentTransformConfig` gains a `source` field. The existing rows keep
`LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED` (native `input(target, type)` then
the export). The `edit` row uses `LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH`: the
export receives the resolved local path string and performs the read itself
in its procedural entry (S12.1.1v2: effects in `pn`; the package parses once).
S1.8 holds — the path is data, never generated source.

A transform that returns an error value prints its message as the load
diagnostic, so an unsupported suffix or invalid source is an actionable error
instead of an empty window.

### 2.2 Edit app mode and the close decision

`UiContext` records the application mode. In edit mode:

* Escape is delivered to the document instead of closing the window
  (proposal §7).
* A platform close request (close button, application quit) consults the
  document's close guard. An unarmed guard closes as before. An armed guard
  cancels the platform close and dispatches a `closerequest` event to the
  document root; the package decides Save / Discard / Cancel and approves
  the close with `dom.request_window_close`. Runtime cancellation and
  teardown run only after approval (proposal §8).
* The package arms the guard exactly while the session is dirty, so a
  clean or broken session never traps the window.

### 2.3 Test infrastructure

* UI fixtures may set `"command": "edit"` and `"working_copy"`; the runner
  copies the fixture document to the working copy under `./temp/` and edits
  the copy, so a Save never touches a committed file.
* New simulator events: `window_close` (the platform close path),
  `write_file` (external modification, `./temp/` only), `assert_file`
  (content / existence), and `assert_window_closed`.

### 2.5 Markup source positions

`parse(source, {type: …, sourcepos: true})` makes the markup reader record,
on each top-level block, the source lines it spans (`sourcepos`, cmark's
`"L:C-L:C"`, 1-based, trailing blank lines excluded). The Markdown adapter
needs the lines to keep a block as written (§3.3). The option is `parse()`
only — `input()` results are cached per URL, and positions are a property of
one reading, not of the document — and is carried on the `Input`
(`source_positions`) from `fn_parse2` through
`input_from_source_with_positions` to `parse_document`. Nested blocks carry no
positions: the adapter keeps whole top-level blocks only.

### 2.4 Defects fixed on the way

Each was found by an edit-mode test and fixed at its root; none is specific
to the editor.

* **MIR cross-module `pub let`.** Two imported modules exporting a `pub let`
  of the same name shared one import slot, so the second module's value
  replaced the first (the shell's CSS was the toolbar's). Imported pub vars
  are now named per defining script (`write_var_name_for_script`), matching
  how the consumer reads them. Regression: `test/lambda/import_pub_let_same_name.ls`.
* **Package exports compiled by MIR.** `interp_call_module_export` found only
  T0 slots; it now publishes the MIR `_b` entry of a compiled export
  (`lambda_function_publish_boxed_entry`, shared with the module registry).
* **Runtime-typed attributes.** `ElementReader::get_attr_string` read only
  static string attributes, so formatters dropped `href`/`language` values set
  at run time; it now falls back to the element's attribute value.
* **Markdown reader.** A line containing `$` ended its paragraph (an early
  math-parser workaround), so `costs $5` or inline `$x$` split one paragraph
  into several and a save would have rewritten the structure. Paragraphs now
  join like CommonMark; a `$$` block still interrupts. Separately, a blank line
  before a list that ends at a marker change no longer makes it loose.
* **Markdown writer.** `format(…, 'markdown')` gained a CommonMark block layer
  (`format-md.cpp`): blank-line separation, tight/loose lists with continuation
  indent, task items, fences longer than any backtick run, GFM tables with
  alignment, context-sensitive escaping, autolinks, titles, hard breaks.
  Superscript is the reader's `^x^`; subscript has no Markdown spelling (the
  reader's `~x~` is strikethrough) and is written as inline HTML.
  Regression: `test/lambda/format_markdown_commonmark.ls`. Two math
  round-trip tests fed non-canonical input (no blank line after a heading) and
  now use the canonical spelling; one-line display math keeps `$$x$$`.
* **HTML writer.** A doctype's public and system identifiers were dropped,
  changing a legacy page's quirks mode; they are written back.
* **HTML reader.** The `<svg>` start tag's own attributes skipped the SVG case
  adjustment (`viewBox` became `viewbox`), since the element was not yet on
  the open-element stack; WHATWG adjusts them for the token itself.
* **XML reader.** Every text node was trimmed and the white space after each
  child skipped, so `Hello <tspan>SVG</tspan>` lost its word space. Mixed
  content (text beside elements) now keeps character data as written; element-
  only white space and a text-only element's edges stay insignificant, as
  data-oriented XML expects. A DOCTYPE internal subset stepped over its `]`
  after white space and swallowed the rest of the document.
* **XML writer.** Comments and processing instructions were written as
  elements (`<!--> … </!-->`). Regression: `test/lambda/input_xml_mixed_content.ls`.
* **SVG client bounds.** An element drawn by an `<svg>` has no CSS box, so
  `getBoundingClientRect()` (and the UI simulator's targeting) reported a
  zero box at the `<svg>` origin. `view_get_visual_bounds` now reports the SVG
  bounding box transformed as painted (`dom_svg_element_client_bounds`).
* **Attribute name case.** `DomElement::get/has/remove/set_attribute`
  lowercased every name, but SVG content keeps its case (`gradientUnits` from
  the parser, `viewBox` from Lambda templates), so such attributes were
  unreadable — the hit test ignored a template canvas's `viewBox`, and with
  the reader fix above, the page root's too. DOM §4.9 lowercases only for HTML
  elements; a name stored exactly as asked now wins, and `set` updates it in
  place (`dom_element_attr_key`).
* **Dashed-stroke hit test.** An inexact dash length (3.2) put a boundary a
  few float steps from the walk's offset; the step could not advance the
  offset and the hit test spun forever. The walk now measures boundaries with
  a magnitude-relative step. Regression: `test/ui/hit-test/svg-case-and-dashes.json`.
* **MIR `raise` in a handler arm.** A `raise` in a `^ { … }` error arm marked
  the enclosing if branch as returned, and the if-join then discarded the
  handled value on the normal path (the HTML adapter's `import_text`). Each
  handler arm now owns its return flag (D8.3.4). Regression:
  `test/lambda/handler_raise_in_branch.ls`.
* **Block split cloned `id`.** Splitting a block copied all its attributes to
  the new half, so an HTML paragraph's `id` appeared twice; the new half now
  drops `id` (`lambda.editor` `split_right_attrs`).
* **Flex relayout.** A nested flex item's intrinsic contribution read its
  children's content-box FlexItemProp cache, which a relayout retains from the
  previous pass, so the toolbar collapsed after a reflow; contributions are
  measured, and a `<button>`'s stashed label width is not treated as a native
  control size. Regression: `test/ui/flex_wrap_nested_flex_reflow.json`.
* **SVG `<use>` painting (was E-5).** The painter resolved `<use href>` only
  through its resource table (`<defs>` children and gradients, symbols, …),
  so an instance of any other element drew nothing. It now resolves like
  `getElementById` in the SVG's DOM tree (SVG 2 §5.6), so a sprite `<symbol>`
  in another `<svg>` works too. The instance renders as a `<g>` whose content
  inherits from the `<use>`, and a circular reference renders nothing.
* **SVG text layout (was E-5).** Direct text went through ThorVG and
  `<tspan>`s through Radiant glyphs, two back ends with different baselines.
  Every text node was also trimmed, and a tspan kept only its fill and size.
  `<text>` is now laid out as runs on one line (SVG 2 §11). White space
  collapses across elements, and each run has its own font and fill. x/y/dx/dy
  apply to an element's first character, and text-anchor applies per chunk.
  Runs are measured with the glyph drawer's advances. A bold `<text>` with no
  `font-family` also fell back to a serif face: the default family's face
  file name ("Arial Bold") was being used as a family. Regression:
  `test/ui/svg_use_tspan_render.json`.

## 3. Package decisions

* One edit session per window, owned by the `edit <edit_app>` instance state
  (S1.4, D7.2.1): the session record is seeded from the model `~` at first
  render and replaced only in `on` handlers (S12.1.3).
* The rich-text surface reuses the `lambda.editor` model: nodes are rendered
  by `apply()` so the render map records source paths for the native
  selection bridge; the model root is the editor `doc` node.
* Dirty state compares the current model to the saved checkpoint by value.
* Save exports through the format adapter, re-parses the exported bytes and
  compares the re-imported model with the saved model (round-trip gate),
  checks the on-disk bytes against the last loaded/saved bytes (conflict
  gate), then writes a sibling temporary file and renames it over the
  target (failure-safe replacement). The checkpoint moves only after the
  rename succeeds.
* Opening applies the same round-trip gate before the page is built: a file
  whose content the adapter still cannot keep is refused with a diagnostic
  naming the first block that would change, not opened as a lossy editor
  (proposal §2). Content outside the profile no longer refuses by itself: it
  is kept as written and shown view-only (§3.3).
* Save As (proposal §8) keeps the format. A typed name is relative to the
  document's folder, as a native save panel opens there; `.`/`..` fold so one
  file compares equal however it is spelled. A target is refused when its
  suffix is not the format's, its folder does not exist (Save As never
  creates folders), or it is in another folder while the document holds a
  relative link, image, or raw HTML naming one — relative-URL rebasing is not
  implemented yet (open item E-1), and copying assets is never implicit. An
  existing target asks before overwriting. A refused Save As keeps its
  dialog, showing the path it tried.
* The Markdown profile omits commands the format cannot spell: underline and
  subscript are declined at the descriptor (`unsupported_input_types`), so the
  model never holds a mark a save would drop.

### 3.1 HTML (Phase 2)

* The parsed document is the envelope: doctype, `<html>`/`<body>` attributes,
  and the whole `<head>` stay as the parser's Mark tree and are written back
  as read (a fragment stays a fragment). Only body flow content becomes the
  model; its attributes, event handlers included, are kept as data and are
  never projected onto the surface.
* Editable profile: text blocks, the flow containers (`div`, `section`,
  `article`, `main`, `header`, `footer`, `nav`, `aside`, `blockquote`), lists,
  `pre` (a `<code>` inside keeps its attributes), tables whose cells hold
  runs, figures, `hr`, and the marks `strong b em i u s del sub sup span code`
  with their attributes as mark values. A container holding only a run gets an
  implied paragraph, written back bare while it is the container's only
  block. Anything else — unknown elements, comments, scripts, forms, a
  container mixing runs with blocks, a table with block cells — is an atomic
  node holding its parsed subtree and is written back through the HTML
  formatter; a retained inline element keeps the marks around it.
* Export is a small text writer over the model (Lambda element literals need
  a static tag); the body is re-indented. The normal form settles white space
  as HTML renders it, so the gate compares what the reader sees.

### 3.2 SVG (Phase 3)

* The source tree is the model: every element is a node with its attributes
  in source order and its children, text included. The source text before the
  root (XML declaration, comments, DOCTYPE with its internal subset — the XML
  reader does not keep a DOCTYPE) and after it is kept verbatim.
* The surface projects the model as inline SVG through the same XML writer
  and the XML reader (element literals cannot take a runtime tag), with each
  top-level object carrying `data-edit-path`; scripts, `on*` attributes, and
  other namespaces' metadata are not projected. Frames and handles are an
  overlay from session state. Zoom scales the canvas; the saved size and
  `viewBox` never change.
* Objects are the root's drawable children (a group moves as one). A press
  picks through Radiant's SVG paint hit-test; an object's frame is its own
  geometry where the tools know it, else its rendered box.
* Edits are lambda.editor Transactions of `set_attr`/`replace` Steps, so undo,
  redo, dirty state, and save are shared with rich text. Move uses an
  untransformed shape's own coordinates, else a translate in the parent's user
  space merged into a leading translate; resize is enabled for untransformed
  rectangles, images, circles, ellipses, and lines; duplicate gives the copy
  fresh ids and remaps `url(#…)`/`#…` references inside it; paint edits the
  `style` declaration where the property lives there; Back keeps `<defs>` and
  metadata first. Text objects holding one run are retyped from a dialog.
* A gesture runs press to release and commits one transaction; Escape before
  the release cancels it. There is no live preview (E-4).

### 3.3 View-only kept parts

* A part the rich-text editor cannot edit shows as it renders (proposal §2)
  and saves as read. Its rendering is computed once at import into a
  bookkeeping attribute `'edit view'` on the atomic node (the name holds a
  space, like the other bookkeeping names, so no writer emits it) and is
  excluded from the normal form (`sorted_attrs`), so the round-trip gate and
  dirty state never depend on it. Nodes an edit creates carry no view and
  fall back to their source.
* `view.ls` owns the projections. HTML goes through a sanitizing writer:
  `script style link meta base title head template noscript` and comments
  are dropped, `iframe frame frameset object embed applet` show as a named
  label, `on*`, `href`, `action`, `formaction`, `srcdoc`, `ping` and
  `javascript:` values are dropped (a link's target becomes its tooltip),
  and controls are disabled. Element literals need a static tag, so the
  writer builds HTML text and parses it back. A view that shows nothing (no
  text, no replaced element) is empty, and the surface shows the part's
  source instead.
* Math shows its TeX source, not a rendering (open item E-9). The edit
  application runs on MIR (the toolbar's `on` handlers are not interpretable,
  and a MIR parent demotes its whole import cone), and compiling the math
  package's `metrics_data.ls` on MIR costs about 22 s and 4.7 GB in a debug
  build (0.02 s interpreted). `lambda view` renders Markdown math by running
  the package in a separate interpreted runtime, which the edit surface has
  no hook for. The native MathML formatter is disabled.
* Markdown keeps a top-level block as an atomic `md_source` node holding its
  source lines verbatim (§2.5) and its view when the block holds a construct
  outside the profile (footnote references, say), is one the editor only
  shows (`html-block`, display math — as atoms the formatter would respell
  them), or does not survive its own export and re-import (nested emphasis
  such as `*a **b***`). Source lines no block claims — link reference
  definitions — are kept the same way, without a view. Export joins the
  formatter's output for each run of editable blocks and the kept lines with
  one blank line. The Markdown view renders the parser's vocabulary as HTML
  and sanitizes the whole block, so inline raw HTML tags — one token each —
  pair up as the source pairs them.
* Inline raw HTML and math inside editable blocks, and nested HTML blocks or
  display math, stay atoms saved through the formatter; raw HTML carries a
  view too. A lone inline tag or a comment shows its source.
* HTML `raw_html`/`html_block` nodes carry the sanitized view of their parsed
  subtree; a named anchor or an empty icon element shows its source label.
* The surface wraps every kept part in a `contenteditable=false` element with
  class `edit-view-only` (`edit-view-block` for blocks, `edit-view-source`
  when it shows source), titled "… kept as written (view only)".
* A part is edited only as a unit. A click in it does not select it: the
  native caret stays outside the non-editable element, while the DOM action's
  source selection lies inside the part. The shell therefore declines every
  action whose selection ends lie in a part (descriptor family other than
  delete, cut, copy, history, selection, setting) with a status hint. A
  deletion with the caret in a part removes the whole part: it runs as a
  model request on a node selection of the part, since the event's input
  fields are host-backed and a copy of the event loses them. Toolbar
  commands, including the Link and Image dialogs, apply the same check to
  the model selection. `lambda.editor` gained `edit_action_selection` and
  `edit_action_family` for this.

## 4. Progress

| Phase | Item | Status |
|---|---|---|
| 1 | `edit` CLI + shared launch options, help, `doc/Lambda_CLI.md` | done |
| 1 | path-source transform + load diagnostics | done |
| 1 | edit app mode (Escape, guarded close, title rows) | done |
| 1 | simulator events + runner `edit` fixtures | done |
| 1 | Markdown reader/writer round-trip fixes | done |
| 1 | `lambda.edit` shell, toolbar, session, Markdown adapter, Save As | done |
| 2 | HTML adapter with retained envelope | done |
| 3 | SVG adapter and drawing surface | done |
| 1–3 | tests | done: Lambda unit tests in `test/lambda/edit/` and `test/lambda/proc/edit_session_save.ls`; 16 UI fixtures in `test/ui/edit/` (suite `edit`) |
| — | view-only kept parts (§3.3), `parse` `sourcepos` (§2.5) | done: `test/lambda/edit/view_only.ls`, kept-block cases in `markdown_adapter.ls`, `test/lambda/parse_sourcepos.ls`, UI fixture `edit_md_view_only.json` |

## 5. Open items

| ID | Item |
|---|---|
| E-1 | Save As into another folder with relative references: rebase the URLs deliberately (proposal §8) instead of refusing. |
| E-2 | Save As paths assume `/` separators; Windows drive paths are not resolved. |
| E-3 | HTML "Open Link" (proposal §5): launch a viewer for the link without replacing the session. Links already select content and never navigate. Needs a host capability that does not exist yet — a DOM row that starts a viewer process (`dom.request_navigation` would replace the session) — and a decision on which viewer runs and which URL schemes it may open. |
| E-4 | Live drag preview needs pointer moves in the author template; they reach only UA behavior templates under capture (F20, ES5 hot-path guard). Needs a ruling before extending capture delivery to author templates. |
| E-5 | Resolved 2026-09-26: Radiant now paints `<use>` and lays out `<tspan>` runs (§2.4). Still open in the renderer: `url(#…)` paint, clip, and mask references resolve only within the painted `<svg>`; `textPath` and per-character x/y lists are not supported. |
| E-6 | HTML `<b>`/`<i>` stay their own marks; the Bold/Italic buttons reflect `strong`/`em` only. |
| E-7 | The XML reader does not honor `xml:space="preserve"` for text-only elements. |
| E-8 | Drawing: no pan tool (the canvas scrolls), no path-node editing, connectors, or layers (proposal §10 choice 2). |
| E-9 | View-only math shows TeX source (§3.3): rendering it needs the math package, whose `metrics_data.ls` costs ~22 s / 4.7 GB to compile on MIR (debug build). Fix the MIR cost, or give the edit surface a hook to render math in a separate interpreted runtime as `lambda view` does. |
| E-10 | A click in a view-only part does not select it (the native caret stays outside the non-editable element), so the part shows no selection highlight; typing is declined and Delete removes the part (§3.3). |
| E-11 | The Markdown reader has no footnote-definition syntax: `[^1]: word` parses as a link reference definition and `[^1]: two words` as a paragraph holding a footnote reference. Both are kept as written. |
