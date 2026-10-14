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

Eight reference documents import successfully: paragraphs, lists, tables,
underline/strike, raster media, Strict namespaces and UTF-8 BOM. A ninth,
`tiny-picture-target-base-relative.docx`, contains `0x0314` in the ZIP
version-needed field. The shared ZIP reader currently rejects versions above
45, so the unmodified file is an explicit rejection case. Its payload uses
stored/DEFLATE compression; the failure is the version declaration, not an
unsupported compression method. No reference is repacked to produce a pass.

Eight references identify Microsoft Office Word as their producer; the BOM
fixture contains no producer metadata. LibreOffice producer coverage remains
unverified. Output assertions are semantic; Word page appearance is outside
the initial package profile.
