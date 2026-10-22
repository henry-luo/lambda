# Word

A native Lambda/Radiant word-editor demo ported from the core workflow in
[`ref/wordcraft`](../../../ref/wordcraft). The application, document model,
commands, ribbon, and dialogs are Lambda; Radiant supplies layout, painting,
input, clipboard transport, and selection geometry. No Rust binary, WASM,
JavaScript editor, or browser is required.

Run from the repository root:

```sh
make build
./lambda.exe demo word
# Or open the entry script directly:
./lambda.exe view test/demo/word/word.ls
```

The studio handbook is editable. Start a blank document with **New**, or return
to the example with **Studio example**. Select text to format it; formatting
at a caret sets the marks for subsequent typing.

## Available workflow

- Home ribbon: undo/redo; bold, italic, underline, strike, subscript and
  superscript; font family and size; text color and highlighting; paragraph
  alignment and line spacing; clear character formatting; six paragraph styles;
  bullets, numbering, and list indent/outdent. Paragraph settings apply spacing
  before/after, left indentation, and first-line indentation in one undo step.
- Insert ribbon: configurable tables (1–32 rows, 1–12 columns, optional header),
  embedded local pictures with dimensions and alternative text, editable links,
  link removal, and horizontal rules. Picture settings resize existing images.
  Shapes offers fifteen basic, flowchart, and decorative shapes with editable
  size, fill, outline color/weight, rotation, and text. Shapes use shared contours, sit inline with text,
  and retain their settings in snapshots and HTML; PDF includes their SVG image.
  Click an existing picture or shape to select it, then open its settings, or
  double-click it to edit directly.
  The Pages group adds an editable cover (title, subtitle, author) at the start,
  a blank page at the caret, or a page break. A blank page splits a body paragraph
  or heading and isolates a new editable paragraph between two page boundaries.
  The Symbol dialog offers punctuation, mathematics, currencies, Greek letters,
  and arrows, plus custom text of 1–64 printable Unicode characters.
- Header and Footer: repeating text, left/center/right alignment, font size,
  on-screen edge distance, and an option to leave the first page blank.
  `{PAGE}` and `{NUMPAGES}` fields update with explicit screen sheets and actual
  PDF pages. Double-click an existing header/footer to edit both stories.
  PDF places these stories in the page's top/bottom margin boxes, centered
  vertically within the margins; the edge-distance control affects the canvas.
  Settings and page setup reject margins that cannot fit the configured stories.
- Equations: editable LaTeX source with formula examples, display/inline math
  typesetting, size, and color. The existing `lambda.doc.math` package supplies
  typesetting and embedded glyphs. Double-click an equation to revise its source.
- WordArt: solid, outlined, hollow, gradient, and shadowed text, with straight,
  upward/downward arch, and wave transforms; editable text, font, size, colors,
  and dimensions. Equations and WordArt remain editable inline SVG objects in
  snapshots and HTML; PDF preserves their appearance.
- Bookmarks: name the current paragraph, list targets, Go to, delete, or move an
  existing name to another paragraph. Names use up to 40 ASCII letters, digits,
  or underscores and cannot start with a digit. Each paragraph has one bookmark.
  The link dialog's **Place in this document** inserts an internal link to a
  bookmark; **Go to bookmark** follows it from the ribbon. Bookmark targets and
  links survive editable snapshots and HTML export/import. Read mode permits
  bookmark navigation while disabling insertion and deletion.
- Contextual Table ribbon: rows above/below, columns left/right, row/column/table
  deletion, four table styles, and nine cell alignments. Tab/Shift+Tab move
  between cells; Tab at the final cell adds a row.
- Layout: A4, Letter, and A5 paper; portrait/landscape; preset or custom margins
  in millimetres; explicit page breaks. Paper geometry changes the editing
  canvas and exported PDF pages. Invalid margins keep the dialog open.
- Review: comments anchored to selected text, author and quoted context, a
  sidebar, previous/next and Go to text, resolve/reopen, and delete. Overlapping
  anchors survive character formatting and editing; comment changes support undo.
  Comments and page settings survive editable snapshots and exported HTML imports.
- Tracked text changes: author settings, insertion/deletion marks, a Changes
  pane, Go to text, individual or all-change accept/reject, and a Show markup
  toggle. Consecutive typing shares a revision; deleting pending inserted text
  cancels it. Review and editing both support undo/redo. Snapshot and HTML
  preserve pending revisions; PDF exports visible revision marks and text export
  uses the final text with pending deletions omitted.
- Navigation: document headings, Unicode-aware case-insensitive search with a
  Match case toggle, previous/next match, replacement, and one-step undo for
  Replace all across formatting runs within a paragraph.
- View: navigation pane, read mode, focus mode, and 50–150% zoom. Explicit page
  breaks create separate editable sheets with the chosen size and margins;
  ordinary Enter continues on the same sheet. PDF export paginates using
  that document's paper, orientation, margins, and explicit page breaks.
- Native typing, paragraph splitting/joining, selection, deletion, composition,
  and clipboard actions use the existing `lambda.editor` protocol.
- Open editable `.word.json` snapshots, HTML, Markdown, text, and supported
  semantic DOCX content. Imports become copies. Unsupported model nodes are
  rejected rather than discarded.
- Save lossless version-1 `.word.json` snapshots with atomic writes, round-trip
  validation, an existing-file prompt, and external-change detection. Unsaved
  changes guard New, Open, sample replacement, and window close. Export HTML,
  text, or paginated PDF; exports leave the editable checkpoint unchanged.

| Shortcut | Action |
| --- | --- |
| Cmd/Ctrl + B / I / U | Bold / italic / underline |
| Cmd/Ctrl + Z | Undo |
| Cmd/Ctrl + Shift + Z, Cmd/Ctrl + Y | Redo |
| Cmd/Ctrl + S / Shift + S | Save / Save as |
| Cmd/Ctrl + F | Find |
| Cmd/Ctrl + K | Insert or edit a link |
| Cmd/Ctrl + Enter | Page break |
| Cmd/Ctrl + Alt + M | Comment on selected text |
| Cmd/Ctrl + Space | Clear character formatting, preserving comment anchors |
| Tab / Shift + Tab in a table | Next / previous cell |
| Escape | Cancel a dialog or leave focus mode |

Save and export default to `temp/word/`. Other destinations must have an
existing parent directory. Editable snapshots are limited to 4 MB, including
embedded pictures. `.word.json` is the editable format; DOCX writing,
RTF/ODT, spell/grammar checking, comment replies, collaboration,
mail merge, rich or section-specific header/footer stories, and Word-compatible
automatic pagination in the editing canvas are outside this demo. Screen sheets
grow when their contents overflow; PDF adds pages to fit. Shapes are inline;
floating objects and attached connectors are outside this milestone.
Text tracking covers text-only paragraphs, headings, and table cells, including
formatting runs. Character formatting applies without revisions. Structural
edits, links, and embedded-content edits require tracking to be turned off;
they are rejected while tracking is on. Review does not track object, story,
or paragraph-format changes. LaTeX support follows the math package's command
coverage; unsupported commands leave the equation dialog open.
Font substitution follows the installed fonts available to Radiant. Search does
not span paragraph boundaries.
Bookmarks target paragraphs rather than arbitrary text ranges; deleting a
bookmarked paragraph removes its target, leaving any links for the author to
repair. Cover insertion prepends a new cover; it does not replace an earlier one.
PDF text is currently painted as glyph outlines rather than selectable text.
DOCX bookmarks and retained raw HTML are rejected as unsupported nodes; source
documents remain untouched. Imported image paths should be absolute or data URLs.

## Source analysis and port decisions

WordCraft is a layered word processor, not only an egui text widget. Its
`crates/doc` model owns paragraphs, character runs, tables, sections, and
secondary stories. `crates/engine/src/session.rs` owns the document, selection,
history, layout cache, and view state. `Session::run` resolves a `CommandSpec`,
checks availability, checkpoints mutations, executes the command, and restores
the previous document on failure. The ribbon, shortcuts, CLI, and MCP share
those command IDs. `crates/layout` performs shaping and pagination; the egui
front end paints that derived output. File adapters belong to separate crates.

This port keeps the command/session boundary and translates document operations
into Lambda's existing immutable editor transactions. It reuses the editor's
insertion, deletion, structural editing, history, IME, and DOM-selection bridge
instead of introducing another editing engine. **S12.1.3** makes the template
body a pure projection and handlers the state owner; **D7.2.5** keeps editing
policy in Lambda packages, with Radiant providing the native mechanisms.

| WordCraft source | Lambda implementation |
| --- | --- |
| `crates/doc/src/lib.rs`, `para.rs`, `props.rs` | `lambda.editor.mod_doc` nodes, text leaves, and marks; `mod_model.ls` adds selected-range CSS formatting |
| `crates/engine/src/lib.rs` (`CommandSpec`, `Registry`) | `mod_model.ls` command registry, retaining corresponding upstream IDs |
| `crates/engine/src/session.rs` (`Session::run`, history) | `mod_app.ls` controller plus `lambda.editor` transaction/history facade |
| `crates/engine/src/cmd/{format,para,text,table,edit}.rs` | Existing editor commands plus range formatting, paragraph attributes, style application, and cross-run replacement |
| `crates/engine/src/cmd/find_pattern.rs` | Literal paragraph search mapped back to source positions; wildcard/regex language is not ported |
| `crates/engine/src/cmd/{table,table_style,insert,para}.rs` | Reused structural commands plus table presentation, embedded pictures, link editing, and paragraph settings in `mod_model.ls` |
| `crates/engine/src/cmd/{page,review}.rs` | `mod_page.ls` document-owned paper settings and screen sheet grouping; `mod_review.ls` comment metadata with text-mark anchors |
| `crates/engine/src/cmd/insert.rs` | `mod_insert.ls` pages, bookmarks, and Unicode symbols; `mod_shapes.ls` uses drawing contours and image transactions for embedded SVG shapes |
| Secondary stories, math, WordArt, and text revisions | `mod_stories.ls`, `mod_equation.ls`, `mod_wordart.ls`, and `mod_changes.ls`; generated objects share `mod_objects.ls` |
| `crates/ui-egui/src/{ribbon,canvas}.rs` | `mod_ui.ls`, `mod_surface.ls`, `word.css`; native HTML/CSS layout and caret geometry |
| `crates/engine/src/sample.rs` | Adapted studio handbook in `mod_document.ls` |
| `crates/{docx,formats,pdf}` and `engine/src/io.rs` | Existing Lambda format adapters, conflict-aware `lambda.edit.session`, and Radiant PDF export through `mod_io.ls` |

Character formatting splits only the selected portions of boundary leaves.
Their selection positions are remapped explicitly, including backwards ranges;
ordinary replace-step mapping would collapse positions inside a replaced leaf.
Replace all applies existing insertion transactions from the final match
backwards and composes their steps into one history item. View changes and
selection movement never change the saved document checkpoint (**S1.4**,
**D7.2.1**).

Page settings and comment metadata are document attributes, so the same editor
history restores them atomically with text marks (**S1.4**, **D7.2.5**). HTML
exports carry this metadata in a named meta element. Comments use IDs on span
marks, allowing overlapping anchors without a second offset-mapping system.
Case-insensitive search maps folded Unicode characters back to source positions,
including lowercase expansions (**S17.7.2**).

Bookmark targets use paragraph IDs prefixed with `word-bookmark-`. The shared
editor already retains `id` on the original paragraph when Enter splits it,
so the new paragraph never duplicates the target. Moving a bookmark removes
the old target and assigns the new one in one transaction; imported unrelated
IDs are preserved. Covers, blank pages, symbols, and bookmark mutations all
use the same editor history (**S1.4**, **D7.2.5**).

Screen sheets wrap the original model children without changing their source
paths. The paragraph schema marks page boundaries and cover roles
`keep_on_split:false`, so Enter does not duplicate them (**D7.2.5**).
Shapes store their parameters in an image's `data-word-shape` attribute;
regenerating its SVG, dimensions, and metadata is one transaction (**S1.4**).
Equations and WordArt follow the same image transaction, preserving editable
parameters alongside the rendered SVG. Decorative glyph paths are shared with
Photo through `lambda.doc.shapes` rather than copied.

Tracked changes use the editor's optional transaction filter before history is
recorded (**D7.2.5**). Insertion and deletion span marks carry revision ID, kind,
and author; the Changes pane is derived from those marks. Accept/reject transforms
the marked leaves instead of restoring an old document, preserving other edits.
Transient IME updates stay unmarked; the committed composition is tracked once.

Toolbar pointer events carry a hit position for the button as well as an
optional document selection. The controller accepts document selections but
uses hit-position fallback only inside the editing surface (**D7.2.5**).
Form-control positions never replace the document selection. Empty paragraphs
receive a projected zero-width space so their node-position carets have a line
box; this placeholder is absent from saved documents.

Surface binding, selection ownership, document replacement, native action routing,
and selection completion use `lambda.edit.controller`, shared with the general
editor shell. Ribbon selectors and search fields use `lambda.ui.controls` with
declarative change actions; Word supplies command IDs and state updates. Live
control synchronization, typed option values, caret preservation, and IME handling
belong to the shared control package (**S12.1.3**, **D7.2.5**). See
[the shared API](../../../doc/dev/Shared_Editing.md).

Reactive subtree replacement now retires detached inline stylesheets and
publishes their replacements through the existing CSSOM collector. A changed
sheet invalidates the style epoch and recascades the whole document, since its
selectors may affect siblings outside the template (**D7.2.5**). The Review
workflow verifies that resolve/reopen changes the anchor's computed highlight.

Two integration details matter when extending the demo: `file` is a reserved
absolute-path scheme (**S2.4.5v2**), so file-session state uses `file_state`;
and `emit()` walks to ancestor templates, so root keyboard handlers call the
same controller directly. Boolean HTML attributes are omitted when false.

The demo also exposed a cached-import defect: cloning two cached import cones
could instantiate their shared dependency twice in one runtime. Registering
the second shell rebound the logical code unit to its uninitialized slab while
JIT-promoted functions were executing the first import. The loader now reuses
the existing non-retired instance, preserving execution-local state ownership
(**D7.2.1**, **D8.5.1v7**). The native regression is
`InputScriptCacheTest.CachedDiamondKeepsSharedPromotionSlab`.

Native selection restoration chooses the most specific recorded source owner
before an ancestor's structural fallback (**D7.2.5**). Generated table wrappers
change DOM child indices; accepting the document-root guess first moved clicks
to other cells or paragraphs. The pointer regression clicks empty and populated
cells, types without resetting selection, and repeats after zooming.

Caret and selection geometry use viewport coordinates throughout (**D7.2.5**).
The pointer's block origin already includes scrolling; saving that scroll again
as an iframe translation shifted the caret above the clicked line. The scroll
regression verifies the caret's painted pixels as well as focus and typing.
It also checks ribbon pixels above the document viewport: outer-shadow replay
must retain the ancestor overflow clip when the paper scrolls behind the ribbon.

## Verification

```sh
python3 test/demo/word/run_tests.py
python3 test/demo/word/run_tests.py --model-only
./test/test_input_script_cache_gtest.exe --gtest_filter='InputScriptCacheTest.CachedDiamondKeepsSharedPromotionSlab'
./test/test_source_pos_bridge_gtest.exe --gtest_filter='SourcePosBridgeRoundTrip.*'
./test/test_state_store_gtest.exe --gtest_filter='StateStoreDomMutationTest.CaretFrameOffsetExcludesAncestorScroll'
```

The runner checks model, snapshot, and HTML/Markdown/text/DOCX import goldens on
interpreter, auto, and JIT tiers; then replays typing/history/save,
formatting/search/table, open/export/dirty-guard/focus, table-caret, scrolled-caret,
table navigation/settings, pictures/links, paragraph/search, page setup, comments,
cover/blank pages, bookmarks/internal links, symbols, repeated stories, equations,
WordArt, shape settings, and tracked revisions
in headless Radiant. It checks the result records and exported file contents,
including a two-page landscape Letter PDF check, and writes its report and logs
under `temp/word/tests/`. The initial and edited screenshots are
`temp/word-initial.png` and `temp/word-edited.png`. Test reruns replace only
their own named files under `temp/word/`.
The Insert replay additionally verifies a three-page PDF containing an explicit
blank page and a four-page PDF after prepending a cover. Its screenshot is
`temp/word-insert-cover.png`.

WordCraft's original sample text and command vocabulary are used under its
MIT license, included as [LICENSE-MIT](LICENSE-MIT). The workspace styling is
original; no Microsoft assets or fonts are bundled.
