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
