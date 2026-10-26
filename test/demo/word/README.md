# Word

A native Lambda/Radiant word-editor demo ported from the core workflow in
[`ref/wordcraft`](../../../ref/wordcraft). The application, document model,
commands, ribbon, and dialogs are Lambda; Radiant supplies layout, painting,
input, clipboard transport, and selection geometry. No Rust binary, WASM,
JavaScript editor, or browser is required.

Run from the main checkout root (see [worktree build guidance](../../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas) for linked worktrees):

```sh
make release
./lambda.exe demo word
# Or open the entry script directly:
./lambda.exe view test/demo/word/word.ls
```

Use the release build for interactive typing. `make build` creates a debug
executable with detailed layout logging; it is useful for debugging, but its
input latency does not represent normal interactive performance. To measure
individual physical keystrokes and native repainting, run
`python3 test/demo/word/benchmark_typing.py --paint` after the release build.
The benchmark checks the resulting text and writes timings under
`temp/word/performance/`.

The studio handbook is editable. Start a blank document with **New**, or return
to the example with **Studio example**. Select text to format it; formatting
at a caret sets the marks for subsequent typing.

The interface follows photo-album's white panels, cool gray sidebar, blue
accents, rounded controls, and system typography. Both demos use the shared
outline SVG icons in `../mod_icons.ls`. Word's `mod_ribbon_icons.ls` adds bright
blue document, violet creative, teal data, and amber review accents, with saturated
fine contours and matching tinted fills. Equation controls share the same palette.
The ribbons share command sizes, group captions, and selection states. When
space is short, entire command groups collapse into labeled dropdowns as the
window is resized, without adding a horizontal scrollbar. Insert pairs large
primary commands with compact secondary actions. The native Word window has an
800 px minimum width.
Command labels use at most two balanced lines, with button widths fitting the
full text. Compact icons, stacked controls, and galleries keep the ribbon at
80 px without group captions, or 97 px with captions. Document illustrations
retain their own colors and typography.

Ribbon group labels are hidden by default for a shorter ribbon. Toggle
**View → Ribbon Group Labels** to show or hide them across all tabs, including
contextual ribbons. The choice stays active when opening or creating documents
in the same app session. Startup defaults live in `mod_app.ls`'s
`default_settings`; `app.initial({show_ribbon_group_labels:true})` enables labels
at startup without changing document content.

References adds document-owned sources for books, journal articles, and websites.
Use **Insert Citation → Add New Source**, or create sources without citing them
through **Manage Sources**. The source form supports personal/corporate authors,
year, publication details, page ranges, DOI, URL, and accessed date; **Show all
bibliography fields** exposes the optional fields. Separate personal authors
with semicolons and enter names as `Last, First`.

Choose APA, Chicago author–date, or IEEE. These reuse Lambda's bundled CSL
processor and its [documented coverage](../../../lmd/package/doc/latex/citeproc/README.md).
Double-click a citation or use its context menu to change its page locator or
edit its source. Source/style edits update the citation fields and bibliography
in one undo step. **Bibliography** inserts the cited sources at the caret;
**Update Citations & Bibliography** refreshes fields after text edits, rich paste,
or rearrangement. IEEE numbering follows first citation order. A cited source
cannot be deleted until its citations are removed. Typing after insertion starts
outside the citation.

Source records and fields survive editable snapshots and HTML; PDF preserves
the formatted output. A copied citation carries an embedded source, so pasting
it into another document keeps it resolvable. State and source positions remain
owned by the immutable editor model (**S1.4**, **D7.2.5**). This core workflow
does not include Zotero integration, CSL style installation, note citations, or
Word citation-field/DOCX source interoperability. Each field cites one source.
The existing restriction on structural edits during tracked changes also
applies to citation insertion and bibliography updates.

## Available workflow

- Document context menu: right-click or press Shift+F10 for undo/redo, clipboard
  actions, selection deletion, character formatting, paragraph settings, links,
  and comments. The focused item adds table rows/columns and table options,
  list indentation, link editing/removal, typed object settings and rotation,
  bookmark removal, comment/revision review, and header/footer settings.
  Arrow keys, Home/End, Escape, and Tab
  navigate or dismiss it. Read mode offers plain-text copying and Select All.
- Home ribbon (`mod_home_ui.ls`, model in `mod_home.ls`) follows Word's Undo,
  Clipboard, Font, Paragraph, Styles, and Editing groups. **Clipboard**: Paste
  with Keep Source Formatting or Keep Text Only, Cut, Copy, and Format Painter,
  which copies the caret's character formatting onto the next dragged selection
  (Escape or a second click cancels it). **Font**: family and size, Grow/Shrink
  Font, Change Case (sentence, lower, upper, capitalized, toggle), Clear All
  Formatting, Character Border, bold, italic, underline, strike, subscript and
  superscript, Text Highlight Color and Font Color split buttons that repeat
  the last color chosen from their palettes, and Character Shading.
  **Paragraph**: Bullets and Numbering with marker libraries (filled, hollow,
  and square bullets; 1., a., A., i., and I.), Decrease/Increase Indent (list
  level inside lists, half-inch steps elsewhere), Sort A to Z / Z to A for
  selected paragraphs or list items, Show/Hide ¶, alignment, Line and Paragraph
  Spacing (1.0–3.0 plus space before/after), paragraph Shading, and Borders
  (single sides, All, Outside, None, and Horizontal Line). Each command is one
  undo step. Not yet implemented: Word's East Asian typography tools (Phonetic
  Guide, Enclose Characters, Asian Layout, Distributed), Text Effects,
  underline styles, and Multilevel List. Paragraph settings apply spacing
  before/after, left indentation, and first-line indentation in one undo step.
  Tab/Shift+Tab adjust list indentation while keeping the caret in the item;
  the same indentation appears in editable snapshots, HTML, and PDF exports.
  The Editing group opens Find and Replace in Navigation. Find's dropdown moves
  between matches; Select offers the whole document, current paragraph, and
  table cell/row/column/table selections. Dropdowns support keyboard navigation.
  Selecting text opens a floating formatting toolbar with font and size,
  emphasis, highlighting, text color, and paragraph alignment. It preserves the
  selected range through repeated formatting, follows scrolling and zoom, and
  stays within the document viewport. Escape or using another pane dismisses it.
  Ribbon, floating-toolbar and Word Art font pickers discover installed native
  families when the workspace opens, through `dom.query_local_fonts`. A document's
  requested family remains selectable when it is missing locally. Long native
  menus show a draggable scrollbar, accept wheel scrolling, and keep the current
  keyboard choice visible without scrolling the document.
- Insert ribbon: configurable tables (1–32 rows, 1–12 columns, optional header),
  embedded local pictures with dimensions and alternative text, editable links,
  link removal, and horizontal rules. Picture settings resize existing images.
  Shapes offers fifteen basic, flowchart, and decorative shapes with editable
  size, fill, outline color/weight, rotation, and text. Shapes use shared contours and start inline with text,
  and retain their settings in snapshots and HTML; PDF includes their SVG image.
  Click an existing picture or shape to select it, then open its settings, or
  double-click it to edit directly.
  The Pages group adds an editable cover (title, subtitle, author) at the start,
  a blank page at the caret, or a page break. A blank page splits a body paragraph
  or heading and isolates a new editable paragraph between two page boundaries.
  The Symbol dialog offers punctuation, mathematics, currencies, Greek letters,
  and arrows, plus custom text of 1–64 printable Unicode characters.
- Table Design appears beside Table Layout when the caret or selection is in a
  table. It provides ten style previews; Header Row, Total Row, First/Last Column,
  and Banded Rows/Columns options; selected-cell shading with custom colors; and
  a border pen with line style, weight, color, and all/outside/inside/individual
  edge presets. Borders opens the existing detailed dialog. Direct cell fills
  and borders survive style changes. A transparent selection overlay keeps
  fills visible while selecting cells. Design settings and direct formatting
  persist in snapshots, HTML, and PDF; each change takes one undo step.
- Table Layout provides Select and Delete menus, view-only gridlines, table
  properties, row/column insertion, merge/split cells, split table, AutoFit to
  contents/window or fixed columns, cell height/width in points, row/column
  distribution, a nine-position alignment grid, text direction, and cell margins.
  A shared visual grid accounts for rowspans and colspans in selection, borders,
  merge/split, and insertion/deletion. Splitting keeps the content in the first
  resulting cell; merging preserves each cell's text and marks with line breaks.
  Size fields retain focus while typing. Table ranges survive native caret echoes.
  Data tools sort by text or number, keep header/footer groups, repeat leading
  rows through a real `thead`, and convert cells into formatted paragraphs with
  tabs, commas, or paragraph separators. Formula supports SUM, AVERAGE, MIN, MAX,
  and COUNT over ABOVE/BELOW/LEFT/RIGHT, one cell, or an A1:B3 range. Results update
  when Calculate is pressed; this is a bounded formula dialog, not Word's full field
  language. Sorting vertically merged cells requires splitting them first, and
  merges stay within a row group. Pen/eraser table drawing remains unimplemented.
  Layout edits preserve surrounding content and take one undo step. Snapshots,
  HTML, and PDF retain table sizes, margins, direction, and repeated header groups.
- Illustrations: **Icons / Emojis** offers twenty outline icons with editable
  color, dimensions, and rotation, and a Unicode emoji gallery with custom
  sequences. Emojis insert as ordinary editable text. **Diagram** arranges
  2–8 text items as a process, hierarchy, cycle, or list. **Chart** offers column,
  bar, line, pie, and doughnut charts, with editable title, color, and up to
  twenty `Label, Value` rows. Negative values work in Cartesian charts; pie and
  doughnut charts require nonnegative values with a positive total. Preview
  checks data before insertion. Double-click or use the focused object's
  context menu to edit it. Icons, diagrams, and charts use the same move, resize,
  rotation, copy/paste, and undo controls as other illustrations; metadata and
  self-contained SVG survive snapshots and HTML, and PDF keeps their appearance.
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
- Word Art: solid, outlined, hollow, gradient, and shadowed text, with straight,
  upward/downward arch, and wave transforms; editable text, font, size, colors,
  and dimensions. Equations and Word Art remain editable inline SVG objects in
  snapshots and HTML; PDF preserves their appearance.
- Object transforms: select a picture, shape, Word Art, icon, diagram, or chart to show four corner
  resize handles and a circular rotation handle. Drag the object body to move
  it into a paragraph, list item, empty block, or table cell; a caret previews
  the drop position. The first drag can select the object and move it together.
  Corner cursors follow the object's rotation. Corner drags preserve the
  aspect ratio; Shift snaps rotation to 15° increments. Escape cancels a drag.
  Each completed drag creates one undo step. Picture, Shape, and Word Art
  settings also accept exact dimensions and a rotation from −180° to 180°.
  Word Art resizing scales its lettering; shape text keeps its font size.
  Handles follow zoom and scrolling and are hidden in read mode and dialogs.
- Shape Format appears when a shape or Word Art is selected. It offers shape
  presets, Edit Shape, Text Box, fill/outline color menus, outline weight and
  dashes, an outer shadow, Word Art text styles, text colors, and exact dimensions.
  Shape text supports horizontal alignment, top/middle/bottom alignment, and
  90°/270° direction. The Format Pane provides custom colors, gradients,
  opacity, native fonts, bold/italic shape text, text inset, rotation, and alt
  text. Word Art also exposes its existing curved text transforms in the pane.
  Arrange tools remain available in this ribbon. Formatting preserves placement,
  reflection, and surrounding text, and survives undo, clipboard, saving, and
  HTML/PDF export. Custom paths, edit points, 3D, glow, and soft edges are not
  implemented.
- Equation appears when an equation is selected. It provides preset equations,
  Unicode symbol galleries, and eleven structure galleries, including fractions,
  roots, scripts, integrals, accents, brackets, functions, limits, and matrices.
  Open the LaTeX source editor or double-click an equation; select source text
  before choosing a structure to wrap it. Apply or Ctrl/⌘ Enter updates the
  document; invalid drafts leave the applied equation intact. Close Equation
  applies a valid draft, while Discard Draft closes without applying it.
  Professional/linear views, actual inline/display flow, justification, size,
  color, and explicit equation labels persist in snapshots, clipboard, and
  HTML/PDF exports. Labels are manual; Word's UnicodeMath grammar and automatic
  equation numbering are not implemented.
- In-place equation editing (`mod_math_field.ls`, `mod_math_edit.ls`): click an
  equation to put a caret at the nearest slot inside it. Left/Right walk the
  slots in reading order (into numerators, denominators, scripts, radicals and
  brackets); Up/Down move between numerator and denominator, superscript and
  subscript, or root index and body, then to the nearest slot above or below;
  Home/End go to the equation's ends. Typing inserts symbols; `^` and `_` open
  scripts (`x_1^2` attaches both to `x`), `/` turns the preceding operand into a
  numerator, and `\name` followed by a space becomes that command (`\frac`,
  `\sqrt` and one-argument commands put the caret inside). Backspace/Delete
  remove symbols, enter a structure first, and remove it once empty. Equation
  ribbon structures and symbols insert at the caret with empty slots. Left at
  the start or Right at the end leaves the equation beside it in the text;
  Enter or Tab leaves to the right, and Escape keeps the equation selected.
  Each edit is one undo step and rewrites the LaTeX source, normalizing script
  arguments to braces. Caret slots come from one extra render with
  layout-neutral `\lambdamark` slots (`vibe/Lambda_Pkg_Math.md` §3.3). Not yet
  implemented: selecting a range inside an equation, linear-mode equations
  (they keep the source pane), and editing inside matrices and other
  environments, which act as single symbols.
- Object Format appears when an image-backed object is selected. Its Arrange
  group is also available in Layout: nine page-margin positions, inline/square
  left or right/top and bottom/front/behind wrapping, layer order, six page-margin
  alignments, rotation, and horizontal/vertical flips. Layout Options accepts
  exact paragraph-relative positions and distance from text. Floating objects
  drag freely; inline and flow objects retain caret-based relocation. Each edit
  is undoable and persists in snapshots, rich clipboard content, HTML and PDF.
  The Selection Pane selects covered objects and hides/shows them. Check 2–20
  objects on one explicit page, then choose Group; the group moves, resizes
  proportionally and rotates as one object. Ungroup recovers editable members
  with their composed transforms and preserves the surrounding text. Groups
  must fit within 650 × 2000 px. Contour wrapping, page-fixed anchors,
  multi-object distribution, and DOCX drawing interoperability remain outside
  this core workflow. Position presets capture the current page geometry.
- Bookmarks: name the current paragraph, list targets, Go to, delete, or move an
  existing name to another paragraph. Names use up to 40 ASCII letters, digits,
  or underscores and cannot start with a digit. Each paragraph has one bookmark.
  The link dialog's **Place in this document** inserts an internal link to a
  bookmark; **Go to bookmark** follows it from the ribbon. Bookmark targets and
  links survive editable snapshots and HTML export/import. Read mode permits
  bookmark navigation while disabling insertion and deletion.
- Contextual Table ribbon: rows above/below, columns left/right, row/column/table
  deletion, four table styles, and nine cell alignments. Select cells, rows,
  columns, or the whole table from the ribbon or context menu. Drag within a
  cell to select text; crossing into another cell selects a rectangular cell
  range, including whole rows, columns, or the table when covered. Shift-click
  and Shift+arrow keys extend the range. Borders offers solid, dashed, dotted,
  double, or no lines, a width and color, and all/outside/inside/individual-edge
  presets. Formatting applies to the selected cells in one undo step. Copy
  includes table HTML and tab-separated text; pasting a grid into selected cells
  preserves the existing table. Delete clears cell contents. Tab/Shift+Tab move
  between cells; Tab at the final cell adds a row.
- Layout: A4, Letter, and A5 paper; portrait/landscape; explicit page breaks.
  Margins offers Normal, Narrow, Moderate, Wide and Mirrored presets with page
  previews and inch measurements. Custom Margins edits millimetres and switches
  between normal and facing-page inside/outside edges without discarding fields.
  Mirrored pages alternate margins in the editor, thumbnails, headers/footers
  and PDFs; rulers track the visible page and edit the corresponding stored edge.
  Margin changes support undo and editable/HTML round trips (**D7.2.5**).
  Invalid margins or insufficient header/column space keep the document intact.
- Columns: Layout → Columns offers One, Two, Three, and More Columns for a custom
  count (1–6), spacing in millimetres, and an optional separator line. Columns
  apply to the whole document; text, lists and tables flow through the same
  source nodes. Headers/footers stay full width. Column changes support history,
  snapshots, HTML round trips, thumbnails, Read Mode and Web Layout. Page setup
  and ruler margins retain at least 20 mm per column. PDF uses paginated native
  body regions through `radiant.export_document` with `source_format:"xhtml"`
  (**D2.10**, **D7.2.5**). Unequal Left/Right presets, section-specific settings
  and explicit column breaks remain unsupported.
- Review: comments anchored to selected text, author and quoted context, a
  sidebar, previous/next and Go to text, resolve/reopen, and delete. Overlapping
  anchors survive character formatting and editing; comment changes support undo.
  Comments and page settings survive editable snapshots and exported HTML imports.
- Dictionary and Translate: Review → Dictionary (or Look Up in the context menu) defines the selected word, or the word at the caret, through the [Free Dictionary API](https://dictionaryapi.dev) (`GET https://api.dictionaryapi.dev/api/v2/entries/en/<word>`). The pane lists the phonetic spelling, then each part of speech with up to four definitions and examples, plus synonyms that look themselves up on click. A speaker button beside the headword reads it aloud through `dom.speak` (en-US); clicking it again, or closing the pane, stops the reading, and `speechend`/`speecherror` reset the button. Review → Translate (or Translate in the context menu) sends the selection, or the caret's paragraph, to Google Translate's keyless web endpoint (`translate.googleapis.com/translate_a/single?client=gtx`), detecting the source language and targeting one of 13 languages. Copy puts the translation on the clipboard, and Insert replaces the source text in one undoable step. If the document changed since the request, Insert replaces the current selection instead. Each request runs in a document-owned task (`start(…,{owner:'document'})` → `io.fetch`), which reports back through a `word_lookup` event, so the window keeps responding while it waits. A newer search drops an older one's late answer. Enter searches in the Dictionary field; Ctrl/⌘ Enter translates. Lookups also work in Read Mode. The pane takes the right-hand slot from Comments and Changes. dictionaryapi.dev ignores case, but its Cloudflare edge caches each spelling separately and can hold a stale HTML 404 page for a valid word ("evening", "happy"). An HTML 404 therefore retries the capitalized spelling. Only the API's own JSON `No Definitions Found` reads as an unknown word; other failures read as a temporary outage.
- Spelling: red wavy underlines appear directly beneath possible English (US) misspellings, including words split by inline formatting. The first check starts when each document opens; later checks run after a short typing pause in a document-owned task and isolated process (**S13.1.3v2**, **S13.3.1v2**). Existing underlines stay visible while an edit is pending, then the latest result replaces them. A newer edit cancels the old check and discards its result. IME composition defers checking. The checker reads the prepared dictionary and its line-offset index; full dictionary validation remains an explicit preparation step. Underlines use source-mapped range geometry outside the document and its undo history (**D7.2.5**, **S2.5.8**). It excludes code, math, deleted text, `spellcheck=false`, and non-English regions. Correction suggestions and grammar checks are future work.
- Tracked text changes: author settings, insertion/deletion marks, a Changes
  pane, Go to text, individual or all-change accept/reject, and a Show markup
  toggle. Consecutive typing shares a revision; deleting pending inserted text
  cancels it. Review and editing both support undo/redo. Snapshot and HTML
  preserve pending revisions; PDF exports visible revision marks and text export
  uses the final text with pending deletions omitted.
- Navigation: close the pane with its × button and reopen it from View or Find.
  Headings, Pages, and Results tabs support clicks and arrow/Home/End keys.
  Headings jump to their text; Pages shows content thumbnails with the document's
  paper proportions and jumps to each on-screen sheet, including blank pages.
  Thumbnails update after edits, page breaks, and undo without adding editing
  bindings or duplicate bookmark IDs. Read Mode and Web Layout support navigation.
  Results lists clickable match excerpts with the query highlighted. Search is
  Unicode-aware and case-insensitive, with a Match case toggle, previous/next
  match, replacement, and separate undo steps
  for Replace and Replace all across formatting runs within a paragraph.
- View: Read Mode, Print Layout, and Web Layout are available in the status bar
  and View ribbon, with the active view highlighted. Read Mode prevents editing;
  Print Layout shows paper sheets, margins, headers/footers, and rulers. Web Layout
  presents an editable continuous document that reflows with the window and zoom,
  hiding paper gaps, headers/footers, and rulers. Returning to Print Layout retains
  paper settings and explicit page breaks. Switching views preserves the document,
  selection, and undo history; leaving Read Mode restores the previous editing view.
  Navigation, focus mode, and 50–150% zoom remain independent. Centimeter rulers
  stay above and to the left of the document viewport, with shaded page margins
  and zero at the writing area's origin. They follow scrolling and zoom; the
  vertical scale restarts for each page. Drag the shaded margin boundaries to
  resize left/right margins on the horizontal ruler and top/bottom margins on
  the vertical ruler. The upper left triangle sets the first-line indent; the
  lower left triangle sets the hanging indent while holding the first line in
  place. The rectangle moves both together, and the right triangle sets the
  right indent. Controls follow the current paragraph, including list items and
  table cells. A drag previews the layout with a guide and creates one undo step
  when released; Escape cancels it. Arrow keys adjust a focused control by 1 pt
  or 0.5 mm; Shift makes the step ten times larger. Read Mode disables these
  controls. Page margins retain room for the body and configured headers/footers.
  The bottom status bar shows writing or
  reading mode, paper size, orientation, and page count. Explicit page
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
- Print (Home ribbon or Cmd/Ctrl + P) opens a Chrome-style print page: a sheet
  preview beside pages (all, odd, even, or a range), layout, paper size, pages
  per sheet (1–16), margins, scale, headers and footers, and background colors.
  Choices apply only to the printout. Lambda has no OS printer API, so the
  destination is Save as PDF. The preview measures each block at the printed
  width and breaks pages between blocks (keeping h1–h3 with what follows),
  matching the paginated PDF for ordinary flow; multi-column documents preview
  approximately. Default choices print the engine's own vector PDF. Ranges,
  scaling, and n-up print the preview sheets, rasterized at 288 dpi because
  Radiant's PDF writer does not yet apply CSS transforms or overflow clipping.

| Shortcut | Action |
| --- | --- |
| Cmd/Ctrl + B / I / U | Bold / italic / underline |
| Cmd/Ctrl + Z | Undo |
| Cmd/Ctrl + Shift + Z, Cmd/Ctrl + Y | Redo |
| Cmd/Ctrl + S / Shift + S | Save / Save as |
| Cmd/Ctrl + F | Find |
| Cmd/Ctrl + P | Print (Save as PDF) |
| Cmd/Ctrl + K | Insert or edit a link |
| Cmd/Ctrl + Enter | Page break |
| Cmd/Ctrl + Alt + M | Comment on selected text |
| Cmd/Ctrl + Space | Clear character formatting, preserving comment anchors |
| Tab / Shift + Tab in a table | Next / previous cell |
| Escape | Cancel a dialog or leave focus mode |

Save and export default to `temp/word/`. Other destinations must have an
existing parent directory. Editable snapshots are limited to 4 MB, including
embedded pictures. `.word.json` is the editable format; DOCX writing,
RTF/ODT, correction suggestions and grammar checking, comment replies, collaboration,
mail merge, rich or section-specific header/footer stories, and Word-compatible
automatic pagination in the editing canvas are outside this demo. Screen sheets
grow when their contents overflow; PDF adds pages to fit. Shapes are inline;
floating objects and attached connectors are outside this milestone.
Cell-range selection and border presets currently target rectangular grids;
merged-cell editing is outside this milestone.
Rotation paints around the center of the inline image box; surrounding text
continues to use the box's unrotated dimensions.
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
| Secondary stories, math, Word Art, and text revisions | `mod_stories.ls`, `mod_equation.ls`, `mod_wordart.ls`, and `mod_changes.ls`; generated objects share `mod_objects.ls` |
| `crates/ui-egui/src/{ribbon,canvas}.rs` | `mod_ui.ls`, `mod_surface.ls`, `word.css`; native HTML/CSS layout and caret geometry |
| Word's Home tab (beyond WordCraft) | `mod_home_ui.ls` ribbon groups and menus; `mod_home.ls` change case, Format Painter marks, paragraph borders, list styles, and sorting |
| In-place equation editing (beyond WordCraft) | `mod_math_field.ls` LaTeX row/item tree, caret slots, edits and slot geometry; `mod_math_edit.ls` click, key, caret drawing and ribbon insertion |
| `crates/engine/src/sample.rs` | Adapted studio handbook in `mod_document.ls` |
| `crates/{docx,formats,pdf}` and `engine/src/io.rs` | Existing Lambda format adapters, conflict-aware `lambda.edit.session`, and Radiant PDF export through `mod_io.ls` |

Character formatting splits only the selected portions of boundary leaves.
Their selection positions are remapped explicitly, including backwards ranges;
ordinary replace-step mapping would collapse positions inside a replaced leaf.
Text leaves retain separate inline wrappers in the live projection, so
Mark content normalization cannot merge away their source paths when formatting
is cleared (**S2.6.5**, **D7.2.5**). The toolbar retains its geometry in UI state;
viewport movement only repositions its overlay, preserving document selection.
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
Equations and Word Art follow the same image transaction, preserving editable
parameters alongside the rendered SVG. Decorative glyph paths are shared with
Photo through `lambda.doc.shapes` rather than copied.

`mod_transform.ls` routes object gestures through those same transactions.
Generated artwork is regenerated from its parameters, so resizing preserves
shape/Word Art editability; a picture retains its original image source.
`mod_handles.ls` projects selection chrome outside the editable surface and
temporarily previews dimensions/rotation in the DOM. Pointer release commits
once, while Escape, pointer cancellation, or lost capture restores the original
source and style without changing history or dirty state (**S1.4**, **D7.2.5**).
The parent projects the document before passing its rendered body to the gesture
controller, so preview redraws retain source bindings (**S12.1.3**, **D7.2.5**).
The workspace forwards its non-bubbling selection notifications to the document
controller so ribbon bindings continue to follow the caret.
Moves reuse `lambda.editor.mod_image` transactions and the drop-caret measurement
shared with Notes. The gesture owns pointer capture until release and commits
the captured source path, keeping selection and undo independent of the hover target.
Angles are stored with a CSS image transform, shared by the canvas and exported
HTML/PDF; rotating a shape no longer shrinks its artwork to fit a fixed viewport.

Selected pictures and generated objects supply their clipboard payload from the
model through `lambda.edit.controller` (**D7.2.5**). Copy preserves history and
selection; Cut captures the same payload before deleting the atom. Native
transport receives image bytes, HTML, and alternative text. Rich paste retains
the image attributes declared by Word's schema, including editable object
parameters, size, and rotation. `clipboard_objects.json` checks keyboard/menu
copy, PNG bytes, SVG/HTML alternatives, paste, Cut, and undo; the model golden
`word_clipboard_test.ls` also checks escaping and exclusion of undeclared attrs.

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

For a shared build, `--lambda-exe` can point at a stable executable snapshot.
`--ui-only --ui-start-at <fixture>` resumes an interrupted UI run while retaining
its generated files for the export checks; use the same code and host binary.

The runner checks model, snapshot, and HTML/Markdown/text/DOCX import goldens on
interpreter, auto, and JIT tiers; then replays typing/history/save,
formatting/search/table, open/export/dirty-guard/focus, table-caret, scrolled-caret,
table navigation/settings, pictures/links, paragraph/search, page setup, comments,
cover/blank pages, bookmarks/internal links, symbols, repeated stories, equations,
Word Art, shape settings, icons/emojis, diagrams, charts, and tracked revisions
in headless Radiant. It checks the result records and exported file contents,
including a two-page landscape Letter PDF check, and writes its report and logs
under `temp/word/tests/`. The initial and edited screenshots are
`temp/word-initial.png` and `temp/word-edited.png`. Test reruns replace only
their own named files under `temp/word/`.
The ribbon-appearance replay captures all eight ribbons, checks shared height and label/caption
bounds, opens insertion dialogs, and verifies command access after narrowing the
window to 900 pixels. Browsing the tools must leave the document unchanged.
The ribbon-overflow replay checks group menus, popup placement, tab switches,
document replacement, and expansion again as the window widens.
The citations replays cover source forms, authors and source types, validation,
locators, all three CSL styles, bibliography refresh, context actions, native
typing after a field, read mode, and snapshot/HTML/PDF persistence. Their model
golden adds undo/redo, source-carrying rich paste, and escaped/Unicode titles.
The object-transform replay covers mouse/touch resizing, rotation, Shift
snapping, Escape, zoom, undo/redo, read mode, and editable/HTML/PDF persistence.
The illustrations replay checks Unicode emoji input, all three insertion
dialogs, invalid data, gallery previews, focused menus, edits, resize handles,
copy/paste, undo/redo, read mode, and snapshot/HTML/PDF exports. Its model
golden also checks every layout/chart type and metadata after rich paste.
The Table Design replay checks contextual tabs, gallery previews, all six style
options, visible header/band colors, selection-aware shading and borders,
preserved direct formatting, undo/redo, HTML reopening, PDF export, and read mode.
The Table Layout replay checks its controls, gridline preference, merge/split
and history, size-field focus, alignment, margins, text direction, properties,
selection-preserving distribution, AutoFit, headers, formulas, sort, table
splitting, conversion, and exports. Its model golden also checks spans during
row/column edits, rectangular selection closure, caret echoes, invalid requests,
row groups, and snapshot/HTML round trips on every execution tier.
The object-move replay covers a first drag on an unselected object, mouse/touch
moves into text, lists, and cells, drop preview, cancellation, invalid drops,
selection, undo/redo, zoom, read mode, and the four resize cursors.
The table-selection replay covers ribbon/context selection, direct rectangular
drag and Shift-click ranges, border styling and history, rich clipboard grid
paste, replacement typing, deletion, and read mode. The table-drag replay checks
text selection within one cell, automatic cell/row/column/table selection in
both drag directions, live previews, keyboard extension, cancellation, zoom,
and ordinary editing after a cell-range selection. Logical node/multi-node
selections retain source paths while native text geometry receives a caret in
the first cell (**D7.2.5**). Cell lookup uses native DOM identity, so similarly
structured cells cannot alias. A raster unit test checks actual dashed, dotted,
and double collapsed-border pixels and color at 1× and 2× scaling.
The viewport replay checks caret/list clipping above the workspace and stable
physical typing, including Space and arrow keys, at normal and increased zoom.
Geometric bullets share the document transform during zoom; visible and clipped
marker pixels are checked on both sides of the document viewport boundary.
The ruler replay checks both axes against live page bounds, centimeter spacing,
margin origins, second-page scrolling, zoom, resizing, focus/read modes, and
stable typing. Native scrollbar drags in both directions update both rulers
while held, including at clamped limits; unchanged offsets emit no extra scroll
events. Paper/orientation/margin changes update the rulers and bottom
status bar, including at narrow widths. `mod_ruler.ls` keeps this chrome outside
the document's caret clip and source tree (**D7.2.5**).
The ruler-edit replay covers all four margins; first-line, hanging, left, and
right indents; live guides, exact cancellation, range and caret preservation,
undo/redo, keyboard adjustments, read mode, and zoom with horizontal scrolling.
Model goldens check size limits, header space, selected paragraphs, imported
length units, and table-cell paragraph creation with mapped selection and undo.
Ruler previews stay outside document history; a completed gesture uses the same
page or paragraph transaction as the dialogs (**S1.4**, **D7.2.5**).
The margins replay checks presets and previews, custom mode/field preservation,
validation, mirrored page/ruler geometry, thumbnails, stories, editing and file
round trips. The export fixture checks exact preset text edges and alternating
inside/outside edges in ordinary and column PDFs with independent PDF extraction.
The view-modes replay checks status/ribbon synchronization, read-only typing,
continuous page flow, story visibility, window/zoom reflow, image handles and
resizing, and selection/undo preservation across page breaks and view changes.
It also checks stable Space/arrow-key scrolling and narrow status bars.
`mod_view.ls` keeps presentation choices outside the document and its history
(**D7.2.5**).
The navigation replay checks closing/reopening, keyboard tabs, heading jumps,
live page thumbnails, blank-page navigation and typing, search-result jumps,
read-only selection, Web Layout, zoom, and paper orientation. Read Mode uses
ordinary Selection operations rather than the editable-surface completion bridge;
leaf wrappers keep those selections aligned with the source (**D7.2.5**).
The page-break typing replay checks immediate typing after a paragraph split,
an empty new page, and undo/redo. It also puts the native caret on the editor's
unmapped wrapper to verify typing retains the document and logical caret.
The columns replay covers text fragmented across columns, caret/typing, history,
custom spacing and rules, validation, page breaks, table-cell edits, responsive
views and file round trips. `tests/proc/_columns_export.ls` checks default HTML and explicit
XHTML export; the long PDF must contain five sheets and no leading blank. When
`pdftotext` is available, the harness verifies all 100 paragraphs appear once and
headers/page fields repeat correctly. Applied source nodes remain singular
through the presentation wrappers (**D7.2.5**).
The context-menu replay checks clipboard edits, selection preservation,
formatting, history, keyboard navigation, read-mode copying, and small-window bounds.
The focused-item replay checks each item menu and its command target, including
cell-padding clicks, row/column edits, object dialogs/rotation/deletion, review,
headers/footers, and read mode. `context_items.word.json` is its editable fixture.
The list replay checks visible indent/outdent, Tab/Shift+Tab, caret and history,
Enter continuing indentation, and editable/HTML round trips.
The empty-list replay checks immediate typing and typing after leaving and
returning with the mouse or arrow keys, including a blank item between filled
items, numbered lists, and undo/redo. Empty model containers map their rendered
placeholder back to the container caret; the placeholder never creates a model
text child (**D7.2.5**). The source-position bridge unit tests also distinguish
that placeholder from authored zero-width text.
The shape replay checks that the gallery clips before the following controls,
wheel scrolling reaches the last shape, and the dialog remains scrollable in a
small window.
The Insert replay additionally verifies a three-page PDF containing an explicit
blank page and a four-page PDF after prepending a cover. Its screenshot is
`temp/word-insert-cover.png`.

WordCraft's original sample text and command vocabulary are used under its
MIT license, included as [LICENSE-MIT](LICENSE-MIT). The workspace styling is
original; no Microsoft assets or fonts are bundled.

The export API procedures in `tests/proc/` run independently in the Lambda
baseline. Equations, shape formatting, and grouping construct fresh models
through `tests/fixtures/mod_export.ls` and round-trip their own saved files.
The UI runner sets `LAMBDA_WORD_EXPORT_UI=1` to verify the UI-produced files
instead; a missing UI artifact still fails that run.
