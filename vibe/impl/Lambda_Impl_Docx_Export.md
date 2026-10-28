# DOCX export implementation

Implemented 2026-10-28. The writer extends the existing script document package
under **D7.2.1**, **D7.2.4v4**, **D7.5.2** and **S12.4.1v3**. It treats input as
data (**S1.8**) and uses existing bounded ZIP/XML facilities. No formal ruling,
third-party runtime dependency or original-source archive preservation was added.

## Public behavior

- `lambda.doc.docx.export_document(document, options=null)` is pure and returns
  `{archive, diagnostics}`. Input is HTML text or an HTML-style element tree.
- `lambda.doc.docx.save(document, path, options=null)` is procedural and returns
  `{path, diagnostics}` after an atomic, deterministic ZIP write.
- `lambda convert report.html -t docx -o report.docx` supports HTML, Markdown and
  literal text sources. The CLI uses `unsupported:"error"` by default;
  `--loss allow` publishes diagnosed degradation. Invalid input or a failed write
  returns nonzero and preserves an existing destination.
- Word's Export dialog accepts `.docx`. Save/Save As still writes `.word.json`,
  preserving the full editor model. Export leaves the editable path/checkpoint
  unchanged and shows conversion diagnostics in the existing yellow notice.

The output contains editable WordprocessingML, rather than an HTML attachment.
It includes OPC content types/relationships, main story, styles, numbering,
core metadata and captured raster parts. The native converter encodes the ZIP
while the transform runtime still owns the tree and binary image values.

## Finite profile and code boundaries

`lmd/package/doc/docx/write.ls` owns normalization, block/run traversal, lists,
bookmarks, hyperlinks and package assembly. `export_style.ls` translates the
supported CSS lengths, fonts, colors, marks, paragraph properties and shared
paragraph/cell border edges. `export_table.ls` constructs merged grids, fills
omitted slots, skips non-row whitespace, keeps column constraints and header
rows, and retains captions. `export_media.ls` captures supported raster bytes
once per source and emits independently sized picture occurrences.
`export_common.ls` shares traversal, XML part construction and schema ordering.

Accepted behavior includes Unicode/XML escaping, headings, font family/size,
color/shading/tracking, bold/italic/underline/strike/sub/sup, tabs, explicit
breaks, paragraph alignment/spacing/indentation, page breaks, list starts and
restarts, nested levels through eight, table merges/grids, safe links/bookmarks,
pictures, core metadata and page geometry. Local pictures require an explicit
base folder in the package API; CLI paths resolve relative to the input file.
Remote pictures are not requested. Missing pictures retain alt text with a
warning. Invalid options/XML characters and excessive tree depth are fatal.
Strict diagnosed-loss failures occur before publication.

Word adapts its existing tag and named paragraph typography rather than copying
font defaults. Reopening a DOCX requests source formatting and empty paragraphs,
removes the viewer's physical page wrapper, restores recognized paper settings,
and turns source table grids into editable cells with explicit widths. The
shared HTML adapter's `source_attrs` helper was promoted for reuse. Multiple
cell paragraphs become editable lines with a warning because the editor schema
holds inline cell content. Other atomic/view-only content keeps the prior
text-plus-warning fallback.

## Validation

Artifacts live under `temp/docx-export/`; no golden was weakened to accept a
failure. `test/input/docx/export/generate.py` reproduces source fixtures.

| Gate | Result |
|------|--------|
| `make build` | Pass; CLI ZIP serialization integration rebuilt |
| DOCX package plus affected Word IO/open cases | 26/26 cases pass |
| New export goldens, interpreter | 4/4 cases, 61 boolean assertions pass |
| New export goldens, MIR JIT | 4/4 cases, 61 boolean assertions pass |
| Native Word `docx_export` replay | 15 assertions pass |
| Existing Word `file_open` replay | 17 assertions pass; warnings, fatal errors, cancellation and retry |
| CLI export | 6/6 cases: Markdown, literal text, HTML, strict rejection, allowed losses, failed destination |
| Shared transform XML path | EPS-to-SVG conversion passes |
| Independent package/read check | ZIP/XML plus python-docx 1.2.0; 3 archives pass |
| Required Lambda baseline | 6,224/6,225 pass; existing `tests_greetings_model` failure |
| `git diff --check` | Pass |

The aggregate baseline ran after CLI/package integration; subsequent named-style,
paragraph-border and caption refinements were qualified by the focused package,
tier and native UI gates above. No Radiant or JS engine source changed.

The independent checker opens actual saved bytes and verifies package targets,
content types, Word property order, runs/fonts/colors, list restarts, sparse and
merged tables, repeated header rows, picture dimensions/deduplication, safe links,
bookmarks, page settings, determinism and failed-write preservation. It also
checks the Word sample's subtitle and quotation formatting. This is not an
exhaustive XSD validator or a Microsoft Word visual comparison.

Native renders were visually inspected: `report.png` (mixed export fixture),
`ui-reopened.png` (editable Word copy), and `ui-warning.png` (yellow loss notice,
unchanged dirty checkpoint and retained comment). Qualification metadata and
artifact/source hashes are recorded in `temp/docx-export/qualification.json`.

## Remaining boundaries

- No original unknown OOXML parts, macros, encryption, source XML round trip,
  or Microsoft Word pagination guarantee.
- No full CSS cascade/layout or external stylesheet interpretation. Export uses
  inline declarations and explicit tag-default options; unsupported content
  normally keeps visible text/alt text and emits `export-*` diagnostics.
- No arbitrary drawing, chart/equation, annotation or tracked-revision semantics.
  Word reports comments, pending revisions, header/footer stories, multiple
  columns and mirrored-margin loss; `.word.json` retains them.
- Native source preview and the editing canvas remain flowing approximations;
  named fonts must be available to the receiving application.
- Generic `format(value,'docx')` is not registered. Use the package API, Word
  Export or `convert -t docx`.

OOXML references: [WordprocessingML structure](https://learn.microsoft.com/en-us/office/open-xml/word/structure-of-a-wordprocessingml-document),
[picture markup](https://learn.microsoft.com/en-us/office/open-xml/word/how-to-insert-a-picture-into-a-word-processing-document),
[paragraph styles](https://learn.microsoft.com/en-us/office/open-xml/word/how-to-create-and-add-a-paragraph-style-to-a-word-processing-document),
and [tables](https://learn.microsoft.com/en-us/office/open-xml/word/how-to-insert-a-table-into-a-word-processing-document).
