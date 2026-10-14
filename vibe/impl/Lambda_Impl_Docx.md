# DOCX package implementation record

**Date:** 2026-10-14. **Scope:** Phases 0–3 of
[Lambda_Pkg_Docx.md](../Lambda_Pkg_Docx.md); optional Phase 4 document preview
routing remains deferred. Package authority: D7.2.1/D7.2.4; virtual element
behavior: D7.4.5v2. No formal ruling changes.

## Implemented surface

`lambda.doc.docx` exports `convert`, `to_elements`, `to_html`, and
`extract_text`, accepting local paths and captured ZIP-tree roots. Results
contain semantic elements, an HTML fragment, reading-order text, metadata, and
ordered diagnostics. Helpers separate OPC loading, namespace/URI handling,
style and numbering resolution, private model construction, and HTML lowering.
Serialization reuses `lambda.latex.to_html`; raster data URIs share
`lambda.doc.media` with the existing archive viewer.

The profile covers inherited paragraph/character styles, direct run marks,
heading mapping, numbering starts/continuation/nesting, grid spans and vertical
table merges, internal raster media, links, simple bookmarks, core properties,
empty paragraphs and significant Unicode text. HTML numeric attributes are
textual at the DOM boundary, matching the existing LaTeX package convention;
the private model keeps numeric values.

Separate stories, revision/field semantics, complex drawings/equations, exact
Word page layout, writing DOCX, encrypted/macro-enabled documents, and the
direct DOCX viewer route remain outside the initial profile. External resources
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

## Verification

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
