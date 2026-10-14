# Lambda DOCX Package Proposal

> **Status:** Initial semantic import implemented (2026-10-14), Phases 0–3;
> the optional viewer route in Phase 4 remains deferred. No formal ruling changes.
> **Package:** `lambda.doc.docx`, implemented as Lambda Script under
> `lmd/package/doc/docx.ls` with helpers under `lmd/package/doc/docx/`.
> **Scope:** Read `.docx` files and convert their supported WordprocessingML
> content into a semantic Lambda element tree, an HTML fragment, plain text,
> and structured diagnostics.
> **Formal linkage:** D7.2.1 governs package/module behavior; D7.2.4 governs
> the shipped `lambda.*` namespace and document-package paths. This proposal
> does not revise either ruling.

## 1. Summary

Add a pure Lambda Script package that reads Office Open XML Word documents
(`.docx`) and exposes their document content as Lambda data. The first release
targets useful semantic conversion for ordinary prose documents, rather than
pixel-accurate Word pagination or round-trip editing.

The package will reuse Lambda's ZIP-backed input and XML parser. It will follow
the package's relationship graph to find the main document part, read the
supporting style, numbering, and media parts that the supported conversion
needs, and lower the source into a semantic element tree. The same result will
provide an HTML fragment, extracted text, metadata, and diagnostics.

```text
.docx path or ZIP file tree
  -> Lambda ZIP filesystem (`input(..., "zip")`)
  -> OPC relationships and WordprocessingML parts
  -> normalized document model
  -> semantic Lambda elements
  -> HTML fragment / plain text / diagnostics
```

The import path is `lambda.doc.docx`, backed by
`lmd/package/doc/docx.ls`, consistent with D7.2.4's document package layout.
The package has no native runtime dependency and adds no third-party runtime
library.

## 2. Goals and non-goals

### 2.1 Goals

1. Read DOCX from a filesystem path or an already opened Lambda ZIP file tree.
2. Resolve the actual main document part through package relationships rather
   than assuming it is always `word/document.xml`.
3. Preserve semantic structure and reading order in a Lambda element tree.
4. Convert common paragraph, run, list, table, link, and image content into a
   predictable HTML fragment.
5. Offer plain-text extraction and stable diagnostics for unsupported or
   malformed content.
6. Keep parsing and conversion in Lambda Script, using existing ZIP and XML
   facilities until a demonstrated limitation requires a Lambda-side runtime
   change.
7. Make the supported subset explicit and fixture-backed.

### 2.2 Non-goals for the first release

- Faithful reproduction of Word's page layout, pagination, typography, or
  print output.
- Writing or editing `.docx`, round-tripping source XML, or preserving unknown
  parts for re-emission.
- The legacy binary `.doc` format, macro-enabled `.docm`, templates, or
  encrypted documents.
- Executing fields, macros, embedded scripts, external relationships, or
  external resources.
- Full coverage of OOXML, drawing canvas, charts, SmartArt, embedded OLE,
  equations, tracked changes, comments, content controls, or floating text
  boxes in v1.
- Silently claiming support for an item that is merely skipped. Unsupported
  material that affects visible content must produce a diagnostic.
- Changing generic `input()` format dispatch or making `lambda view file.docx`
  render a Word document as part of the package's first milestone.

## 3. Design basis

The references establish two useful design points:

- [Mammoth.js](https://github.com/mwilliamson/mammoth.js) maps Word styles to
  simple semantic HTML and explicitly accepts that HTML conversion will not
  reproduce every complex Word layout. Its heading, list, table, emphasis,
  hyperlink, image, and diagnostic surface is a useful baseline for a semantic
  importer.
- [docx2html](https://github.com/lalalic/docx2html) traverses a document model
  and maps Word model types to HTML elements. Its coverage inventory highlights
  section stories, headers/footers, numbering, tables, and image relationships;
  it also illustrates how Word positioning and shape behavior quickly exceed
  ordinary HTML semantics.

Lambda should combine semantic output with an explicit normalized model and
diagnostics. It should not copy or depend on either project's implementation.
Their APIs and dependency stacks are reference material only.

An OOXML Word document is a ZIP package containing related XML and media parts.
The main document part is identified through a package relationship; documents
may also contain separate styles, numbering, footnote, endnote, header, footer,
and image parts. See [ECMA-376](https://ecma-international.org/publications-and-standards/standards/ecma-376/)
and Microsoft's [WordprocessingML document structure](https://learn.microsoft.com/en-us/office/open-xml/word/structure-of-a-wordprocessingml-document).

## 4. Proposed public API

The package provides one primary conversion call and small convenience
functions with the following result contract:

```lambda
import docx: lambda.doc.docx

let result = docx.convert("test/input/docx/minimal.docx")^
result.document       // semantic Lambda element tree
result.html            // HTML fragment, UTF-8
result.text            // plain text in document reading order
result.metadata        // available core document properties
result.diagnostics     // ordered warnings and conversion notes
```

Public functions:

| Function | Contract |
|----------|----------|
| `convert(source, options = null)` | Read and convert a path or ZIP file-tree root. Returns `{document, html, text, metadata, diagnostics}`. |
| `to_elements(source, options = null)` | Return the semantic element tree from the same conversion pipeline. |
| `to_html(source, options = null)` | Return the generated HTML fragment. |
| `extract_text(source, options = null)` | Return plain text with paragraph boundaries preserved. |

`source` is either a path string or the ZIP file-tree element returned by
`input(path, "zip")`. V1 does not accept remote URLs or fetch resources. A
future lower-level model API may be added if consumers need to transform
normalized Word structures before HTML lowering; it should not expose parser
internals as a stable contract by default.

The default `document` root is an `<article class="docx-document">` element.
The HTML result is a fragment, not a complete page or stylesheet. The package
does not imply that its HTML can reproduce page dimensions or layout from
Word.

### 4.1 Options

Keep the initial option surface small and data-oriented:

- `style_map`: a Lambda map from a Word style name or style ID to an allowed
  semantic tag or class mapping. Do not introduce Mammoth's separate textual
  style-map language in v1.
- `include_empty_paragraphs`: preserve empty source paragraphs when true;
  default false.
- `images`: `"data-uri"` or `"omit"`; default `"data-uri"` for supported,
  package-internal image parts.
- `id_prefix`: prefix main-story bookmark identifiers and anchor links; note
  identifiers will use the same prefix when note support is added.
- `unsupported`: `"warn"` (default) or `"error"` for encountered features
  outside the supported profile.

Every option must be validated; unknown option names should follow the
package's documented error behavior rather than being silently ignored.

## 5. Input and conversion architecture

### 5.1 Package loading

1. Normalize a path or ZIP-tree input to Lambda's existing ZIP filesystem
   representation using the current `input` API.
2. Read `[Content_Types].xml` and `_rels/.rels`; identify the internal
   `officeDocument` relationship and resolve its target within the package.
3. Read the corresponding `.rels` file for the main part and build a lookup
   from relationship ID to normalized package target and relationship type.
4. Read optional related parts only when needed: styles, numbering, core
   properties, and media in the initial profile. Missing optional parts use
   Word defaults and do not make a valid minimal document fail.
5. Parse XML through the existing namespace-aware XML input path, then adapt
   its Lambda tree into package-local accessors for WordprocessingML names,
   attributes, and child order.

Do not hardcode relationship IDs, part ordering, or the conventional
`word/document.xml` path. The checked-in minimal fixture happens to use that
path, but the importer must follow the package's declared relationship.

### 5.2 Normalized model and element lowering

Keep a package-private normalized model between XML extraction and HTML
lowering. It should retain ordered blocks and inline runs, resolved style and
numbering references, relationships, and source-part context needed for
diagnostics. This makes OOXML traversal independently reviewable and avoids
mixing XML representation details into HTML serialization.

Lower the normalized model to Lambda elements with an explicit, table-driven
mapping. Do not emit arbitrary tag names or attributes read from the DOCX.
HTML serialization should reuse an existing package helper where practical;
otherwise add one shared serializer helper rather than parallel escaping and
void-element implementations.

### 5.3 V1 semantic mapping

| Word content | Proposed Lambda/HTML representation |
|--------------|--------------------------------------|
| Document body | `<article class="docx-document">` with ordered block children |
| Normal paragraph | `<p>` |
| Heading styles 1–6 | `<h1>` through `<h6>`; custom style map can override supported semantic tags |
| Bold, italic, strike, underline | `<strong>`, `<em>`, `<s>`, `<u>` |
| Superscript and subscript | `<sup>` and `<sub>` |
| Tabs and line breaks | Text plus `<br>` where needed, preserving visible order |
| Hyperlink relationship | `<a href="…">`; external targets remain inert until the caller activates a link |
| Simple ordered/unordered lists | `<ol>` / `<ul>` and nested `<li>`, derived from numbering definitions and paragraph level |
| Tables | `<table>`, row and cell elements; preserve text, nested paragraphs, and supported row/column spans |
| Package-internal raster images | `<img>` with a data URI by default, or omit with a diagnostic |
| Page/section break | Stable semantic break marker; no claim of Word pagination |

Preserve Unicode text and run order. Apply document defaults, named styles,
paragraph/run properties, and direct formatting in a defined precedence order
before lowering. Full visual properties such as font metrics, exact spacing,
columns, borders, and floating placement are not part of the semantic v1
contract.

### 5.4 Diagnostics and errors

Return conversion diagnostics as an ordered array of maps with stable fields:
`{severity, code, part, message}`. A diagnostic code should be stable across
wording improvements. Examples include unsupported visible drawing, unresolved
internal relationship, malformed numbering reference, ignored tracked-change
content, and invalid package structure.

Use a raised Lambda error for an unreadable archive, missing main-document
relationship, malformed required XML, invalid path target, or other condition
that prevents conversion. Use warnings for optional parts and unsupported
constructs that do not prevent useful output. `unsupported: "error"` upgrades
the selected unsupported-feature diagnostics to a conversion error.

## 6. Security and resource handling

DOCX is untrusted ZIP/XML input. The design should rely on existing ZIP
resource limits and preserve their error path; it must not inflate members
outside those limits or copy archive extraction to disk. Additionally:

- Never fetch external relationships or resolve them against the local
  filesystem. Preserve a validated hyperlink target as data only.
- Normalize internal part paths and reject targets that escape the package
  root. Treat relationship targets as package URIs, not OS paths.
- Do not execute macros, fields, embedded scripts, or document-provided
  JavaScript. Never emit source-controlled tag names or active event-handler
  attributes.
- Validate hyperlink schemes before putting targets into `href`; reject
  `javascript:`, `file:`, and other executable/local schemes. Keep rejected
  values in diagnostics only when doing so cannot expose sensitive local data.
- Restrict embedded image data to supported image content types and the
  archive's existing size limits. Never turn an external image relationship
  into a network request.
- Treat malformed relationships and XML as conversion diagnostics or errors;
  do not fall back to an arbitrary part based on a filename guess.

## 7. Implementation plan

### Phase 0 — fixture and acceptance contract

**Work:** Define a versioned supported-feature matrix and deterministic DOCX
fixtures: minimal document, headings and inline formatting, inherited styles,
nested lists, tables, internal and external hyperlinks, image relationships,
Unicode, empty paragraphs, missing optional parts, malformed relationships,
unsupported drawing, and path-normalization cases. Include documents produced
by at least Word and LibreOffice when available, alongside small synthetic
fixtures. Keep generated fixtures reproducible and document their provenance.

**Locations:** `test/input/docx/`, with existing ZIP coverage under
`test/input/zip/office.docx` used as an initial package-shape fixture;
`test/lambda/docx/` for script-level goldens.

**Acceptance:** Every listed feature has an expected semantic tree and
diagnostic outcome before conversion behavior is implemented. Each new
golden-driven Lambda `.ls` test receives its paired `.txt` result.

### Phase 1 — package and relationship reader

**Work:** Add `lmd/package/doc/docx.ls` and focused helpers under
`lmd/package/doc/docx/` for ZIP tree access, XML names, relationship parsing,
part-target normalization, content types, and structured errors. Resolve the
main part from package relationships. Support paths and pre-opened ZIP trees.

**Dependencies:** Existing `input(..., "zip")`, `input(..., "xml")`,
`lambda/input/input-zip.cpp`, `lambda/input/input-xml.cpp`, and Lambda package
resolution governed by D7.2.4.

**Acceptance:** Minimal valid documents load even with a nonstandard main-part
path and optional parts absent. Corrupt ZIP, invalid required XML, missing
main relationship, and package-root escape fixtures fail with stable errors.
No generic `input()` dispatch or native DOCX parser is added.

### Phase 2 — semantic document conversion

**Work:** Add normalized model extraction and element lowering for body
paragraphs, headings, text runs, basic emphasis, links, simple numbering,
tables, page/section break markers, and text extraction. Resolve style and
numbering definitions before emitting elements. Add the result diagnostics
contract and deterministic HTML serialization.

**Dependencies:** Phase 1, existing Lambda element construction and HTML
formatting conventions; use `lmd/package/latex/latex.ls` and
`lmd/package/latex/to_html.ls` as package-structure references, not as code to
duplicate.

**Acceptance:** Script fixtures produce stable element, HTML, text, and
diagnostic outputs for the supported matrix. Unknown or unsupported visible
content does not disappear without a warning.

### Phase 3 — images, metadata, and package registry

**Work:** Resolve internal image relationships and content types, emit
supported image data URIs, read core properties, prefix generated identifiers,
and complete path/link validation. Document and register `lambda.doc.docx` in
`doc/Lambda_Packages.md`.

**Acceptance:** Relationship order and part names do not affect output;
missing/unsupported media yields deterministic diagnostics; external resources
are never fetched; package conversion remains in-memory and deterministic.

### Phase 4 — optional document viewer integration

**Work:** After the package API and limitations stabilize, add a viewer route
for DOCX preview in `lmd/package/doc/doc_viewer.ls`. Keep archive browsing
available as a separate action. This phase is a separate acceptance gate and
does not expand the semantic converter into a paginated Word renderer.

**Acceptance:** `lambda view file.docx` shows semantic document content and
the viewer still permits inspecting the underlying ZIP parts. Run the
relevant Radiant baseline only for this viewer integration.

## 8. Acceptance gates for the first release

1. `import docx: lambda.doc.docx` resolves from the source checkout and release
   package tree.
2. `convert`, `to_elements`, `to_html`, and `extract_text` follow the documented
   result and error contracts for both path and ZIP-tree inputs.
3. The initial feature matrix is covered by deterministic `.ls`/`.txt` tests
   and DOCX fixtures with recorded provenance.
4. Main-part and media lookup follows relationships, not fixed paths or ZIP
   entry order.
5. Style inheritance and list numbering produce stable headings and lists for
   the supported Word subset.
6. Tables, text order, Unicode, links, and supported images survive conversion
   into Lambda elements and HTML.
7. Unsupported visible features are reported; diagnostics are stable and
   ordered.
8. The converter does not access the network, extract to disk, execute active
   document content, or exceed existing ZIP resource limits.
9. The package is pure Lambda Script and does not add a runtime dependency on
   Mammoth, `docx2html`, `docx4js`, Node.js, or a system office suite.
10. `make test-lambda-baseline` passes after package implementation. If Phase 4
    is included, run `make test-radiant-baseline` as a separate viewer gate.

## 9. Deferred features

- Footnotes and endnotes with reciprocal links.
- Headers and footers as section-aware semantic content.
- Comments, tracked-change policy, bookmarks beyond simple main-story anchors,
  and additional field types.
- More complete table borders/styles, column layout, and shape-to-SVG subset.
- Equations, charts, SmartArt, embedded OLE, and drawing canvas.
- Faithful page layout or PDF rendering from DOCX.
- A DOCX writer, editing, and round-trip preservation.
- `docm`, encrypted documents, legacy `.doc`, and remote resource resolution.

Each deferred feature should first receive fixtures and a documented output
mapping. If it changes a formal Lambda package, I/O, or security ruling, revise
the relevant formal spec and this design together as required by the
documentation convention.

## 10. Risks and design constraints

| Risk | Response |
|------|----------|
| OOXML is broad and Word files rely on inherited defaults | Publish a bounded support matrix; add fixtures before claiming a feature; diagnose unsupported visible constructs. |
| The ZIP tree and XML parser expose structure that is awkward for OOXML namespace handling | Build one package-local accessor layer and test namespace-prefix variation; only propose a Lambda-side helper if the limitation is reproduced. |
| Semantic HTML loses page appearance | State flow-layout semantics in the API and leave print fidelity to a separate future design. |
| External relationships and crafted paths could cause data access or active links | Never fetch, normalize package URIs, reject package-root escapes, and validate emitted link schemes. |
| Images can make output large | Reuse archive limits; support omission and report size/type limitations rather than writing extracted assets. |
| A partial converter may silently lose meaning | Stable ordered diagnostics and acceptance fixtures require visible unsupported content to be reported. |

## 11. References

- [Mammoth.js: DOCX to semantic HTML](https://github.com/mwilliamson/mammoth.js)
- [docx2html: DOCX model to HTML](https://github.com/lalalic/docx2html)
- [ECMA-376, Office Open XML](https://ecma-international.org/publications-and-standards/standards/ecma-376/)
- [Microsoft Learn: Structure of a WordprocessingML document](https://learn.microsoft.com/en-us/office/open-xml/word/structure-of-a-wordprocessingml-document)
- Lambda package namespace and package module rules: D7.2.1 and D7.2.4 in
  `doc/Lambda_Formal_Design.md`.
- Existing package directory and import guide: `doc/Lambda_Packages.md`.
- Existing ZIP and XML input implementations: `lambda/input/input-zip.cpp`
  and `lambda/input/input-xml.cpp`.

## Appendix A. Implementation profile (2026-10-14)

The public entry module is `lmd/package/doc/docx.ls`. Its helpers separate
namespace/path handling (`common.ls`), source/part capture (`package.ls`), style
and numbering resolution (`styles.ls`), the private document model (`model.ls`)
and static HTML element lowering (`render.ls`). HTML serialization reuses
`lambda.latex.to_html`; data-URI construction is shared with archive preview
through `lambda.doc.media`. This does not add the Phase 4 DOCX viewer route.

Native changes expose the existing strict XML parser as
`{type:"xml", flavor:"document"}` and retain the source byte count through
`parse()`, preventing significant-space loss, recovery of malformed XML, and
NUL-prefix acceptance. A reproduced generic-element admission defect in the
validator is repaired under D7.4.5v2, so captured ZIP elements satisfy `is
element`, unions, array element contracts and typed package calls. `name()`
also reads a virtual element's intrinsic tag through the shared protocol,
with precise roots for the owner and returned tag during symbol allocation.

Fixtures and supported behaviors are documented in `test/input/docx/README.md`.
The independent fixture set is pinned to Mammoth commit
`791a189230977143cde64dbf9f1d24dce96ac8a1`: eight documents import; a ninth
declares ZIP version-needed `0x0314` and remains rejected by the existing ZIP
reader. The latter is an explicit negative case, never counted as import
coverage. The fixtures are reused reference documents, not a run of Mammoth's
upstream test suite. Word producer metadata is recorded; no LibreOffice
installation or producer corpus was available for this initial run.

Plain text uses two trailing newlines per paragraph and tab/newline separators
for tables. Bookmarks and anchor links are implemented for the simple main-story
case. Separate stories, full revision/field semantics and Word pagination
remain outside this profile. The native XML mode is UTF-8 only; unsupported
encodings fail explicitly. Detailed validation results are recorded in
`vibe/impl/Lambda_Impl_Docx.md`.
