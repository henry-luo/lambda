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
ZIP/JAR browsing remains the default. Word pagination, separate stories and
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
no actual notes to display; ordinary notes, external stories and other
unsupported story types retain diagnostics. Missing/malformed note parts
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

Separate stories, revision/field semantics, complex drawings/equations, exact
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

Nine public Mammoth reference documents are pinned to commit
`791a189230977143cde64dbf9f1d24dce96ac8a1`, with license, source URLs,
SHA-256 hashes and producer metadata under `test/input/docx/reference/`.
Eight import. The unchanged ninth, `tiny-picture-target-base-relative.docx`,
declares version-needed `0x0314`, rejected by the shared ZIP reader's supported
version bound. It is an explicit rejection case, never counted as an import
pass. These are Lambda's own tests using reference documents, not a run of the
upstream Mammoth suite. LibreOffice producer coverage remains unverified.

The existing unrelated dirty worktree edits are outside this change; no files
were staged or committed.
