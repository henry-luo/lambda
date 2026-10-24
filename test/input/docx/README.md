# DOCX semantic import fixtures

`generate.py` reproducibly creates the synthetic `.docx` files in this directory
using Python's standard library. Run `python3 test/input/docx/generate.py` from
the checkout. ZIP timestamps, entry order and content are fixed; no temporary
files or office installation are needed.

The repo-local goldens in `test/lambda/docx/` cover the public import API,
path/ZIP-tree equivalence, significant whitespace and Unicode, namespace-prefix
independence and Strict namespaces, default/inherited/direct run formatting,
paragraph-mark properties separated from visible run formatting,
custom style maps, numbering starts/continuation/nesting, table headers and
merged cells, image relationships with UTF-8 URI escapes, metadata, bookmarks,
safe links, empty paragraphs, unsupported content and typed errors. Every test
has a paired `.txt` and is registered with the Lambda baseline.

Malformed cases cover missing main relationships, package-root escapes (literal
and percent-encoded), macro-enabled main content types, mismatched tags,
unbound prefixes, DTDs, embedded NULs and style cycles. External image and link
fixtures contain inert test URLs; the reader does not request them.

## Independent reference documents

`reference/` contains unchanged public fixtures from
[Mammoth.js](https://github.com/mwilliamson/mammoth.js/tree/791a189230977143cde64dbf9f1d24dce96ac8a1/test/test-data),
pinned to commit `791a189230977143cde64dbf9f1d24dce96ac8a1`. Its BSD license is
included. `reference/manifest.json` records source URLs, SHA-256 hashes and
producer metadata extracted from the package. These are reference documents
used by Lambda's own tests; this does not run the upstream Mammoth suite.

All 17 upstream `.docx` fixtures are present, byte-for-byte unchanged. Fifteen
import without diagnostics, covering paragraphs, lists, tables, underline/strike,
raster media, Strict namespaces, UTF-8 BOM, empty paragraphs, footnotes,
endnotes, comments and links owned by annotation parts. The remaining two
also import: `external-picture.docx` reports `external-image` without loading
the external file, and `text-box.docx` preserves its block content while
reporting `unsupported-text-box-layout` for omitted floating geometry.
Strict unsupported mode rejects those two diagnostic cases.

`tiny-picture-target-base-relative.docx` contains `0x0314` in the ZIP
version-needed field. APPNOTE §4.4.3.1 maps the version to the low byte
(`0x14`, ZIP 2.0); the shared ZIP reader now interprets that field correctly.
Its package-root-relative image target resolves to the same raster as the
ordinary picture fixture. No reference is repacked to produce a pass.

The corpus includes both Microsoft Word and LibreOffice producer metadata;
the manifest records the exact producer for each file. Mammoth's embedded
style-map extension is metadata for that converter: Lambda retains the source
paragraph and uses its own explicit `style_map` option (D7.2.4, S1.8).
Output assertions are semantic; Word page appearance remains outside the
package profile. The three `docx_reference*.ls` goldens cover every reference.

`annotations.docx` and the generated annotation error fixtures exercise
relocated parts, repeated references, independent numbering, references in
table cells, duplicate/missing IDs, invalid roots, nested references, unsafe
note links and external annotation parts. Each part resolves its own
relationships; note diagnostics retain that part's path.

## Business-plan documents

The user-supplied `biz-plan1.docx` and `biz-plan2.docx` are covered by
`docx_business_plans.ls`. Their `.body.txt` sidecars independently concatenate
the main WordprocessingML part's `w:t` nodes in document order; the golden
compares every non-layout character with extracted text. It also checks both
embedded images per document, all one/six tables, the larger plan's cached TOC
links and its 51 omitted trailing grid slots. The documents remain unchanged.

The one-page proposal imports without diagnostics. The larger plan retains
main-body text, saved field results, links, photographs and financial tables;
its two cropped photographs and decorative theme-colored rectangle now retain
their signed column/paragraph offsets and behind-text order. Only its 12
header/footer-story warnings remain. The supported DrawingML rectangle is
selected instead of its legacy VML fallback. This is the converter's flow
representation (D7.2.4), without Word's 21-page pagination. Native and Chromium
previews were inspected at 1100 × 2200.

`cached-fields.docx` exercises nested TOC/page-reference fields spanning
paragraphs and a simple field with an inert external instruction. Field codes
and delimiters are metadata; only saved display runs are rendered, with no
field execution or external access (S1.8). `table-gaps.docx` independently
checks leading/trailing omitted columns and multi-column grid gaps.

`drawing-layers.docx` independently covers front/behind layers, full unsigned
source order, signed offsets, paragraph indentation, inset image cropping and
literal/theme rectangle fills. `drawing-unsupported.docx` retains diagnostics
for rotated/gradient shapes, invalid crops and page-relative placement, while
an unsupported modern shape keeps its compatibility fallback.

`legacy-graphics.docx` checks SmartArt's saved node text, paragraph/line breaks,
escaped markup, and OLE raster previews selected by `ShapeID`. Presentation
points, document points and VML shape/formula metadata are not visible text.
The absent OLE binary deliberately proves that static previews do not read the
embedded object (S1.8). The external/missing/root variants check bounded
diagnostics and placeholders without loading external relationships.

The unchanged `large-doc-15-MB-docx-file.docx` was checked directly; its
`saved-graphics.docx` reproduction is covered by `docx_legacy_graphics.ls` and
`doc_viewer_docx_legacy_graphics.json`. Its six
raster drawings and cached chart remain visible. The SmartArt layout now renders
from its composite/linear/text instructions, constraints and theme styles; the
embedded video's saved EMF icon and `Wildlife.wmv` label render as SVG with a
BMP alpha image. This file imports in strict unsupported mode with zero
diagnostics (D7.2.4v4, S1.8). Unimplemented SmartArt algorithms and EMF records
still produce diagnostics and retain the established text/object fallback.

`drawing-edge-fallback.docx` covers a 2px column offset with 6px wrapping
clearance, a complete saved raster compatibility representation, and empty
header/footer metadata including an uncached PAGE field (S1.8). The 7px
offset in `drawing-interior.docx` remains unsupported because text could fit on
both sides of the interior drawing. `drawing-fallback-partial.docx` retains
diagnostics for a fallback containing two images; `visible-stories.docx`
retains warnings for cached footer text, a visible border and shading.
`invalid-header-root.docx` verifies structural validation of inspected stories.

The unchanged `large-doc-cbbac18c-49b5-4751-943a-3b033aa654be.docx` was checked
directly; its `saved-anchors.docx` reproduction is covered by
`docx_edge_fallbacks.ls` and `doc_viewer_docx_edge_fallbacks.json`. All nine
saved images remain, including five compatibility fallbacks, and its portrait
now wraps with its source offset/clearance. Empty footer metadata no longer
warns. Its eight page-relative drawings still use flow presentation and retain
placement diagnostics: the flowing converter does not implement Word page
assignment or page-relative coordinates. These are actual remaining limits,
separate from the eight avoidable warnings fixed here (D7.2.4v4).

`diagram-composite.docx` independently varies template names, proportions,
right-to-left child placement, connection ordering and shade colors.
`diagram-cycle.docx` and `diagram-unsupported.docx` keep text with diagnostics
for cyclic data or an unsupported algorithm. `metafile-preview.docx` carries a
generated 2×2 premultiplied alpha bitmap plus a UTF-16 label containing a
surrogate pair and escaped markup. `metafile-truncated.docx` and the standalone
EMF fixtures exercise malformed input. `docx_graphics_render.ls` also checks
unknown record rejection and shared UTF-16 validation. The UTF-16 decoder is
shared with IMAP mailbox-name parsing rather than copied (S2.5.8).

The two `saved-*.docx` archives retain the original XML, relationships and EMF
preview while replacing raster payloads with the generated blue test pixel and
omitting embedded object/font payloads. The original large files are ignored
local inputs; default regressions use these small checked-in reproductions.
Regenerate them from the original inputs with:

```bash
python3 test/input/docx/minimize.py test/input/docx/large-doc-15-MB-docx-file.docx test/input/docx/saved-graphics.docx
python3 test/input/docx/minimize.py test/input/docx/large-doc-cbbac18c-49b5-4751-943a-3b033aa654be.docx test/input/docx/saved-anchors.docx
```
