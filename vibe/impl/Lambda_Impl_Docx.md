# DOCX package implementation record

**Date:** 2026-10-14. **Scope:** Phases 0–4 of
[Lambda_Pkg_Docx.md](../Lambda_Pkg_Docx.md), including document preview routing.
Package authority: D7.2.1/D7.2.4; virtual element
behavior: D7.4.5v2. No formal ruling changes.

## Phase 4: document viewer integration

`lambda view file.docx` now selects the document format before ZIP signature
routing. The shared document-transform registry calls
`lambda.doc.docx.view.render_document_file` with the source path (D7.2.4).
The existing Radiant format router also makes the same presentation available
to layout and render commands. Its route table can select a script transform
without a duplicate native loader function; extension matching is case
insensitive. The window recognizes DOCX and labels its format as Word.

`docx.css` styles flowing content on a white document surface; `docx.view`
presents the existing semantic tree and any unsupported-content notices.
The file browser treats DOCX as a file with the bundled Word icon. Selection
retains one conversion across view/source changes and tree redraws, and Source
shows the escaped HTML fragment. A DOCX member inside another ZIP is opened
through the existing nested-archive reader, retaining its shared resource
budget. Conversion errors appear in the preview rather than becoming ZIP text.

`lambda view file.docx --archive` explicitly opens the underlying package tree.
The option requires a source and cannot be combined with `--paged`. Generic
ZIP/JAR browsing remains the default. Word pagination, header/footer stories and
editing are still outside the converter profile.

Visual capture exposed a relative icon-font URL resolving beneath the browsed
directory. The viewer now embeds the bundled font from `sys.lambda.home#`
through the existing `lambda.doc.media` helper, so package assets do not depend
on the document's base URL.

### Viewer integration validation

- `make build` completed with zero errors (25 compiler warnings).
- The bundled viewer compiled with `--emit-ast-dump`; direct DOCX viewing and
  explicit `--archive` viewing completed with exit code 0.
- Native PNG renders of styles, numbering and merged tables were inspected.
  File-browser captures show styled Word content, converted HTML on Source,
  the embedded image, and the bundled Word icons. Capture events contain no
  assertions and are not a regression-suite run.
- The existing DOCX UI fixture now specifies rendered document content rather
  than ZIP rows. No new tests were added, and regression suites were not run
  for this follow-up. The initial package results below predate this change.
- Logs and captures are under `temp/docx/view-*`; `git diff --check` is clean.

## Sample document compatibility follow-up

The locally supplied `test/input/docx/file-sample_100kB.docx` reproduced seven
diagnostics: five unsupported-numbering entries, one drawing without raster
content, and one floating-image entry. Inspection of the original ZIP parts
identified three missing supported interpretations:

1. `w:numFmt="none"` is a valid list format with invisible markers. Numbering
   resolution now retains these lists, and HTML lowering preserves nested
   structure, level indentation and hanging indentation. Ordinary bullets
   and ordered lists retain their existing lowering.
2. The drawing is an internal DrawingML clustered column chart. `charts.ls`
   resolves its chart relationship and reads indexed category/name/value
   caches or literal points. It validates indexes and numeric values, keeps
   series order and RGB colors, and passes a data-only spec to the existing
   `lambda.chart.chart` SVG renderer (D7.2.4, S1.8). Cached values are used
   directly; formulas and workbook/external-data references are not executed
   or fetched. Sparse points keep their category-axis positions. Missing or
   unsupported chart data continues to produce diagnostics. Plain text
   includes the chart's tabular values in document order.
3. The picture uses `wp:anchor`, column-relative horizontal centering,
   paragraph-relative top alignment and square wrapping on the largest side.
   `drawing.ls` lowers this to a float box whose width/inner alignment preserve
   centering and reserve the occupied side for text flow. Left/right square
   wrapping and top/bottom wrapping share the same distance/effect-extent
   handling. Unsupported vertical/page positioning, behind-text placement
   and wrapping modes continue to produce diagnostics.

The same source file now converts with **zero diagnostics**. Direct native
viewing completed with exit code 0. Native PNG output was inspected: all
twelve bars, the three series labels/colors, category labels, hidden-marker
lists, and the centered raster image are visible. Evidence:
`temp/docx/sample-before.mark`, `sample-after.mark`, `sample-view.log`,
`sample-fixed.png`, and `sample-fixed-top.png`. These are reproduction and
render captures; no regression suites were run or new tests added for this
follow-up. No native or vendor code changes were needed.

Source semantics references:

- [OOXML numbering formats](https://learn.microsoft.com/de-de/dotnet/api/documentformat.openxml.wordprocessing.numberformatvalues?view=openxml-3.0.1): `none` means no numbering.
- [DrawingML number cache](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.charts.numberingcache?view=openxml-3.0.1): cached values represent the series' stored displayed data.
- [DrawingML anchors](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.wordprocessing.anchor?view=openxml-3.0.1) and [square wrapping](https://learn.microsoft.com/fr-fr/dotnet/api/documentformat.openxml.drawing.wordprocessing.wrapsquare?view=openxml-3.0.1): placement and wrapping come from the anchor's children.

## Meeting minutes content controls follow-up

`test/input/docx/meeting-minutes.docx` reproduced **308 diagnostics**:
306 unknown-element entries from 33 structured document tags and their
metadata, plus two unsupported-story entries. Thirty controls contain inline
runs and three contain paragraphs inside table cells. Their saved content
already contains the document's visible text; `sdtPr` describes the editor
control rather than additional prose.

`model.ls` now unwraps `w:sdt` through exactly one `w:sdtContent` before
interpreting inline, block, row or cell children. A shared helper preserves
nested control order, surrounding run formatting, paragraph boundaries and
list grouping. It excludes control properties and end-marker formatting from
content traversal. Malformed controls take the existing unsupported-element
diagnostic path. It displays the saved cache without evaluating XML data
bindings or looking up placeholder text in the glossary (S1.8, D7.2.4).

Both note parts contain only separator and continuation-separator definitions.
`package.ls` now reads internal footnote/endnote parts and checks their roots
and note types before diagnosing omitted stories. Separator-only parts have
no actual notes to display. At this stage ordinary notes, external stories and
other unsupported story types retained diagnostics; referenced annotations
are supported by the subsequent Mammoth corpus extension below. Missing/malformed note parts
follow the existing package/XML error lane.

The original document now reports **zero import diagnostics** and retains
the same HTML length and reading-order text. Direct native viewing and a
1100 x 2400 PNG render completed with exit code 0. The PNG was inspected:
all document sections, embedded illustration, bullets and tables are visible
without an import-warning banner. Evidence is under `temp/docx/`:
`meeting-before.mark`, `meeting-after.mark`, `meeting-view.log`,
`meeting-render.log`, and `meeting-fixed.png`. These are reproduction/render
captures; no regression suites were run or new tests added for this follow-up.
Only package scripts and their documentation changed; no rebuild is needed.

Source semantics references:

- [Inline content-control cache](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.sdtcontentrun?view=openxml-3.0.1) and [block content-control cache](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.sdtcontentblock?view=openxml-3.0.1): `sdtContent` stores the contents to display.
- [Row content-control cache](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.sdtcontentrow?view=openxml-3.0.1): content controls can enclose table structure.
- [Footnote/endnote type](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.footnoteendnotetype.type?view=openxml-3.0.1): separator definitions serve note layout rather than defining ordinary notes.

## Meeting minutes visual fidelity follow-up

The Word screenshot of `meeting-minutes.docx` exposed a separate loss after
the content-control fix: the importer preserved visible prose but discarded
theme fonts/colors, run sizes and capitalization, paragraph spacing, table
grid widths, cell shading/borders, row heights, and page dimensions. The
generic viewer stylesheet then rendered a small dark title, oversized bold
headings, uniform white grid cells and compressed document structure.

`theme.ls` reads the package's internal theme relationship and resolves
validated colors and major/minor Latin fonts. Color tint/shade changes HSL
lightness while retaining hue and saturation; CSS font names are quoted.
`presentation.ls` converts OOXML half-points, twips and border eighth-points
to CSS pixels. It emits only recognized properties and fixed CSS names.
`styles.ls` now carries the source run/paragraph properties through style
inheritance and direct formatting, plus base table styles. `model.ls` retains
the resolved properties, grid columns, cell formatting, row heights and page
dimensions; `render.ls` lowers them to semantic HTML with inline CSS. Empty
paragraph marks receive their own formatting in the viewer and browser while
the public `include_empty_paragraphs` option retains its default behavior.
The public converter uses `presentation:"semantic"` by default, preserving
the existing HTML contract. The viewer selects `presentation:"source"`;
callers can opt into that representation without changing text extraction.
The source text remains data and never becomes executable styling (S1.8,
D7.2.4).

The 1100 x 2400 native PNG in `temp/docx/meeting-formatted.png` was inspected
against the supplied Word screenshot. The title is two large blue lines;
section bands use the source green, blue and coral; source table gaps and
borderless edges render, with source widths and row spacing. Conversion still
reports zero import diagnostics. Century Gothic is specified in the document;
an additional viewer capture using Word's installed font directory is
`temp/docx/meeting-with-century-gothic.png`. Native font logs confirm the
requested family resolved from that directory. Direct and file-browser
viewers use the same source presentation. The focused existing DOCX golden
suite passed **9/9** after preserving the public semantic HTML default;
no new tests were added and aggregate baselines were not run. Exact Word
pagination, line breaking, conditional table styles and all page effects
remain outside the current flow profile.

The [OOXML font-size rule](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.fontsize?view=openxml-3.0.1)
defines `w:sz` in half-points; the [table guide](https://learn.microsoft.com/en-us/office/open-xml/word/working-with-wordprocessingml-tables)
describes grid columns, and the [theme font rule](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.runfonts?view=openxml-3.0.1)
defines theme font references.

## Sample 3 offset anchors follow-up

`sample3.docx` has two internal raster drawings with `positionH` offsets of
−53340 and 0 EMU from the column start, `positionV` offsets of 18415 and
−195580 EMU from their paragraphs, and `wrapSquare wrapText="bothSides"`.
The importer previously accepted only named horizontal alignment and vertical
top/zero, so it emitted two `unsupported-floating-image` warnings and left
both pictures inline before their text. This is the source of the reported
warning banner and run-under-image layout.

`drawing.ls` now reads signed offsets and positions this left-edge square-wrap
subset with float margins, adjusting for the separate wrapping distances so
the picture remains at its source offset. The existing aligned-anchor path is
unchanged. Interior positive horizontal offsets still receive diagnostics:
a single CSS float cannot reproduce Word's text on both sides (D7.2.4).
This flow preview does not claim exact Word wrap geometry or pagination.

The sample now converts with **zero DOCX diagnostics**; the native 1100 × 1800
render completed with exit code 0. `temp/docx/sample3-before.png` and
`sample3-after.png` were inspected: both images are visible, and the adjacent
paragraphs wrap alongside them. No regression suites or new tests were run.
The [OOXML position-offset rule](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.wordprocessing.positionoffset?view=openxml-3.0.1)
defines the signed EMU measurement relative to its positioning base; the
[square-wrap rule](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.wordprocessing.wrapsquare?view=openxml-3.0.1)
defines the bounding rectangle used for text wrapping.

## Implemented surface

`lambda.doc.docx` exports `convert`, `to_elements`, `to_html`, and
`extract_text`, accepting local paths and captured ZIP-tree roots. Results
contain semantic elements, an HTML fragment, reading-order text, metadata, and
ordered diagnostics. Helpers separate OPC loading, namespace/URI handling,
style and numbering resolution, private model construction, and HTML lowering.
Serialization reuses `lambda.latex.to_html`; raster data URIs share
`lambda.doc.media` with the existing archive viewer.

The profile covers inherited paragraph/character styles, direct run marks,
heading mapping, numbering starts/continuation/nesting and hidden markers,
grid spans and vertical table merges, internal raster media, cached clustered
column charts, supported paragraph-relative anchors, saved content-control
content, links, simple bookmarks, core properties,
empty paragraphs and significant Unicode text. HTML numeric attributes are
textual at the DOM boundary, matching the existing LaTeX package convention;
the private model keeps numeric values.

Header/footer stories, revision/field semantics, complex drawings/equations, exact
Word page layout, writing DOCX, and encrypted/macro-enabled documents remain
outside the initial profile. External resources
are never fetched; URI targets, hyperlink schemes and option mappings are
validated. Unsupported visible content is reported and strict mode rejects
diagnostics.

## Runtime causes repaired

1. Plain XML input trimmed text with `xml:space="preserve"` and recovered from
   mismatched closing tags. The existing strict parser is now exposed through
   `{type:"xml", flavor:"document"}`, preserving whitespace and validating
   namespaces. Plain XML behavior is unchanged.
2. NUL-terminated parsing accepted a valid XML prefix followed by an embedded
   NUL. `parse()` now forwards the Item's byte length; the document XML boundary
   rejects embedded NULs before entering the cursor parser.
3. ZIP-backed virtual elements reported `type() == element` but failed generic
   `is element` and compound validator contracts. Generic element admission now
   recognizes both carriers under D7.4.5v2.
4. Correct admission exposed `name()` returning null for virtual elements,
   breaking graph cluster and record-port adaptation. An isolated control with
   only virtual admission reverted reproduced the causal split (four graph
   checks pass there). `name()` now uses the existing virtual tag helper and
   roots the owner and returned tag across symbol allocation. No graph goldens
   or adapter behavior were changed.

Archive member acquisition precedes the XML error boundary so ZIP quota/CRC
failures retain their original error lane. DOCX structural failures use codes
7600–7607.

## Initial package verification (before viewer integration)

| Gate | Result |
|------|--------|
| DOCX + four graph regressions, automatic tier | 13/13 passed |
| Same cases, interpreter | 13/13 passed |
| Same cases, forced JIT | 13/13 passed |
| ZIP/media cases, forced GC at every allocation with poison, JIT | 2/2 passed |
| Release binary, independent copied package home, no source-tree package links | 2/2 assertions passed |
| Release binary, DOCX goldens on automatic tier | 9/9 passed |
| Synthetic DOCX regeneration | 27/27 byte-identical in this environment |
| Existing DOCX archive viewer | 9/9 fixture assertions; 4/4 harness tests passed |
| Native semantic rendering | PNG rendered and inspected; list starts, merged cells and raster visible |
| `make test-lambda-baseline` after final tag repair | 6,859/6,859 passed |
| `make test262-baseline` after final tag repair | 40,261/40,261 fully passing; zero regressions |

Focused commands use the real golden harness with
`--gtest_filter='AutoDiscovered/*docx_*:AutoDiscovered/*graphviz_reference_semantics:AutoDiscovered/*graphviz_render:AutoDiscovered/*graphviz_suite:AutoDiscovered/*structurizr_render'`
under `LAMBDA_EXEC_BACKEND=interp`, `auto`, and `jit`. The GC run selects
`*docx_docx_basic:*docx_docx_media` with `LAMBDA_GC_FORCE_EVERY=1` and
`LAMBDA_GC_POISON_FREED=1`.

Focused execution uses a private executable and package/test links beneath
`temp/docx/validation/`. Its final debug SHA-256 is
`40d30b45d072d42a17f91079cb2a86eba3d5ac4d1747a9ca61d7a1d9ec710711`.
The final release smoke uses SHA-256
`f308899f66611c967b9c4aa1b53c724aeeb2cc207f6d7616959f399a37fd7a80`.
Logs and the inspected `preview.png` are under `temp/docx/`. Aggregate logs use
separate directories, including `lambda-baseline-tag-results/`.

The earlier full Lambda run passed 6,855/6,859, exposing the four graph
regressions. Those are repaired, pass on all three tiers, and are covered by
the final full rerun; that earlier partial gate is not the final result.

A final isolated style fix keeps paragraph-mark `pPr/rPr` properties separate
from visible text runs, per [OOXML 17.3.1.29](https://learn.microsoft.com/en-us/openspecs/office_standards/ms-oi29500/de7e1842-5f80-4915-9925-3bf0b4fe84bf).
It adds a synthetic regression and is rechecked by the complete focused suite
after the full native baseline. No other package imports this style helper.

## Fixture provenance and boundaries

All 17 public Mammoth DOCX reference documents are pinned to commit
`791a189230977143cde64dbf9f1d24dce96ac8a1`, with license, source URLs,
SHA-256 hashes and producer metadata under `test/input/docx/reference/`.
All import: 15 without diagnostics, plus external-image and text-box-placement
diagnostic cases. The shared ZIP reader previously compared both bytes of the
version-needed field against 45, incorrectly rejecting the platform byte in
`0x0314`. It now checks the low version byte in both headers, following
[APPNOTE §4.4.3.1](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT);
the original root-relative raster fixture imports without repacking. A native
regression checks platform bytes, supported versions and genuine unsupported
versions in both local and central headers.

The package now retains referenced footnote, endnote and comment content,
including part-local hyperlinks, through shared model/render helpers under
D7.2.4. Annotation IDs are validated, repeated references share one appended
entry, and numbering follows reference order independently for each kind.
Text boxes reuse block lowering and move after their containing paragraph;
their floating geometry remains diagnosed. External images are never loaded,
and Mammoth's embedded style-map extension does not become executable input
or override Lambda's options (S1.8). The generated annotation controls exercise
part relocation, table-cell references, malformed IDs/roots, nested references
and unsafe/external targets. This follows the
[OOXML footnote-reference rule](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.footnotereference?view=openxml-3.0.1)
and [text-box content model](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.textboxcontent?view=openxml-3.0.1).

These are Lambda's own tests using unchanged reference documents, not a run
of the upstream Mammoth suite. Producer metadata now includes both Word and
LibreOffice. No formal ruling changes are required for this package extension.

Mammoth corpus verification: all **17/17** source hashes match the pinned
manifest; **12/12** DOCX goldens pass on each of `interp`, `auto` and `jit`;
the native ZIP suite passes **16/16**, including the extraction-version
regression. The aggregate Lambda baseline passes **6,655/6,656** reported
checks: all 2,112 input checks and all 4,543 completed Lambda checks pass,
but the MIR GC stress runner hits its 180-second idle limit in
`regression_music_layout`. That MusicXML script does not import DOCX. An
isolated retry passes all three stress modes in **241.6 seconds**, exceeding
that idle window. A stack sample shows string construction and precise
collection rather than DOCX/ZIP work. The aggregate timeout is not counted
as a pass, and no runner limits or test expectations were weakened.

## Business-plan loading follow-up

The checkout contains `biz-plan1.docx` and `biz-plan2.docx`. Both already
converted all main-document text and both embedded images. The larger plan
exposed two import defects: its 48 field delimiters and 16 instruction runs
generated unsupported-content warnings despite their saved TOC results being
present, and its 51 `gridAfter` declarations did not reserve HTML grid slots.
The native preview consequently opened on a warning list instead of content,
and shortened financial-table rows did not preserve their source columns.

`model.ls` now recognizes cached simple/complex fields, interpreting only
their saved result runs and links; no instruction is evaluated (S1.8).
It retains `gridBefore`/`gridAfter` counts, and `render.ls` uses one shared
empty-cell constructor to reserve those slots (D7.2.4). This follows
[OOXML complex fields](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.fieldchar?view=openxml-3.0.1),
[simple fields](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.simplefield?view=openxml-3.0.1)
and [omitted trailing grid columns](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.gridafter?view=openxml-3.0.1).
These extend the package's saved-content flow profile without formal ruling
changes or changes to the source documents.

Independent `.body.txt` oracles were derived from every main-part `w:t` node,
and the business-plan golden verifies complete body text, images, table
counts, TOC anchors and all 51 grid gaps. Two generated controls separately
cover cached nested fields across paragraph boundaries, inert external
instructions, and leading/trailing multi-column gaps. All **15/15** DOCX
goldens pass on `auto`, `interp` and `jit`. Both native 1100 × 1600 PNGs were
rendered and inspected under `temp/biz-plan/`; the larger plan's document is
visible after removing the 64 metadata warnings. The remaining 18 diagnostics
describe unsupported header/footer stories, drawing placement and VML shape
content. Word pagination and those features remain outside the flow profile.

The final `LAMBDA_TEST_IDLE_TIMEOUT=360 make test-lambda-baseline` passes
**6,971/6,971** checks: **2,112** input checks and **4,859** Lambda/runtime
checks, including all **313** GC stress cases. The documented idle allowance
lets the music-layout stress case finish in **228.3 seconds**. The complete
run log is `temp/biz-plan/lambda-baseline.log`.

The existing unrelated dirty worktree edits are outside this change; no files
were staged or committed.

## Business Plan 2 drawing placement and decorative shapes

All three drawings in `biz-plan2.docx` use no-wrap, behind-text anchors with
horizontal coordinates relative to the column and vertical coordinates relative
to their paragraph. The cover photo starts at (-96,-96) CSS pixels; the flat
rectangle starts at (-96,671.2), with size 816 × 288.96 and theme fill `DCDDD7`.
The second photo starts at (-96,0) in its own anchor paragraph. These are
paragraph anchors, rather than page-relative placement. Both photographs also
declare `a:srcRect` insets. The old float-only profile dropped these positions
and crops, and fallback-first compatibility selection discarded the supported
modern rectangle in favor of unsupported VML.

`drawing.ls` now lowers this bounded no-wrap profile into absolute boxes.
`render.ls` ranks unsigned source layer order into small CSS integers, keeps
behind-text layers inside the article's stacking context, compensates source
paragraph indentation and preserves empty anchor paragraph marks. Crops use
clipped frames and calculated image offsets. `theme.ls` resolves rectangle
fills through the actual theme fill-style matrix, including placeholder colors.
Only flat preset rectangles without text, transforms, outlines or effects
enter the shape profile. Unsupported compatibility choices keep their fallback;
unsupported shapes/crops/placements retain diagnostics (S1.8, D7.2.4).

Native rendering exposed a separate Radiant defect: negative layers painted
after ordinary text, hiding the cover title even though Chromium displayed the
same HTML correctly. `stacking_order.cpp` now collects negative positioned
blocks at their nearest stacking context, preserving numeric zero versus auto
z-index. Raster/vector walks replay intervening geometry and overflow scopes,
then paint those layers before flow content. Hit testing consumes the same
list in reverse, after flow content. The shared UI fixture tests signed layer
order, an auto paragraph anchor, an explicit zero context and ancestor clipping;
an independent flat-color reference verifies PNG/SVG/PDF output parity.

The [OOXML anchor](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.wordprocessing.anchor?view=openxml-3.0.1),
[relative height](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.wordprocessing.anchor.relativeheight?view=openxml-3.0.1)
and [source rectangle](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.drawing.sourcerectangle?view=openxml-3.0.1)
definitions govern the saved placement, order and crop data.

The business-plan regression now requires exactly three anchors, two cropped
JPEGs, one rectangle and only the 12 unsupported header/footer stories. It still
checks the complete independent body-text oracle, cached links and all tables.
Native and Chromium previews under `temp/biz-plan/` show the title over the
cover photo and decorative band, and the tall second photo beside the TOC.
The source DOCX files remain unchanged; full Word pagination remains deferred.

All **16/16** DOCX goldens pass on `auto`, `interp` and `jit`, including the new
generated drawing controls. The Lambda baseline passes **6,972/6,972** checks
(2,112 input and 4,860 runtime), with the documented 360-second idle allowance.
The final negative-layer output-parity regression passes for PNG/SVG/PDF,
including font/color inheritance through an inline ancestor. The Radiant
dimension-cast lint and `git diff --check` pass.

The render baseline exposed a missing `isolation:isolate` boundary in the new
negative-layer collector. Isolation now uses the shared validated CSS keyword
resolver and computed-property accessor, and stops layer lifting at its owner,
as required by [CSS Compositing §3.4.2](https://www.w3.org/TR/compositing-1/#isolation).
The independent output reference covers both isolation and explicit-zero
contexts; the existing `enhance5_negative_zindex_isolation_01` browser reference
remains the visual regression gate. Blend-group isolation beyond stacking
boundaries remains outside this change.

The final native Radiant sweep (`temp/biz-plan/drawings-radiant-final.log`)
passes all **211** scored render baseline entries, the **194/194** native view
checks, **51/51** page snapshots and **28/28** Scene3D checks. The new UI case
passes all **13** pixel/hit assertions. The aggregate remains non-green because
of five existing failures: `md_nuxt-readme`, `doc_editor_dct_image_pdf`,
`doc_editor_eml_html_preview`, `doc_editor_eml_preview`, and `doc_editor_tsv_table`.
A cached host built before this work (2026-10-14 21:49,
`temp/draw/arrow-picker/lambda-host.exe`, SHA-256
`b4d7151f47e69d6fa22b428bc0ce4a085006628e53891984edb3264aa221ac27`)
reproduces the same Markdown metric and all four editor assertion counts.
Evidence is in `temp/biz-plan/prechange-markdown-native.json` and
`temp/biz-plan/prechange-ui.log`; baseline expectations were not changed.
The initial sandbox run blocked macOS window services; rerunning with native
graphics access completed those checks and removed the page-snapshot failure.
