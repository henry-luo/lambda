# Lambda DocBook initial implementation

**Status:** Initial core HTML profile implemented, 2026-10-15. Broad xslTNG
coverage remains in progress; this is not the completed full port.
**Design:** [Lambda_Pkg_Docbook.md](../Lambda_Pkg_Docbook.md).
**Formal linkage:** D7.2.4 (package namespace), S12.1.1v3 (effects),
S2.6.1v2–S2.6.5 (content), S8.2.2v6 (namespaces), S10.4.3v2 (lineage),
S1.8 (code/data), S7.4.2/S7.4.4 (errors). No formal rulings changed.

## Delivered

`lambda.doc.docbook` exposes pure `convert`/`to_html` and procedural
`read`/`write`. The package runs as Lambda source without Java or XSLT at
runtime. It returns HTML element trees, publication pages, assets, metadata,
TOC entries and ordered diagnostics.

The baseline is verified xslTNG 2.8.5, commit
`b26a7c42445b5aae1e3e6cda866513427b2b061c`. The distribution SHA-256 is
`7c915be8a7fbd217e04aef6a863d449339117d34254a8d48013f46490b5c7193`.
The reference processor is the distribution's Saxon-HE 12.10. Reproducible
pins, license, resources and module mapping live beside the package.

The implementation separates XML adaptation/analysis, rendering modes,
CALS occupancy, generated text, media planning and publication assembly.
Chunk ownership is computed before rendering so forward references and TOC
links can target the correct output file. Common XML namespace expansion
was extracted into `lambda.doc.xml` and reused by DOCX; existing editor path
helpers were promoted to `lambda.io.path` and re-exported by the editor.

English locale list templates are translated into finite data instructions.
Their literal text retains upstream whitespace; the formatter interprets
content, text, token and repeat instructions without executing strings.
This preserves locale behavior without fixture-specific output adjustments.

## Validation

Package scripts cover prose, namespace aliases, duplicate IDs, options,
numbering, cross-references, tables, notes, lists, custom handlers, chunk
planning, path rejection and actual file/media export. Every script has a
paired `.txt` golden; both directories are registered with the existing
Lambda harness. `DocBookTests.PinnedXslTngBodiesAgree` runs the checked-in
official body comparisons for single and chunked output.

The comparison checks eight source documents in both output modes, retaining
body attributes/classes/text and validating local links and assets. Only
declared representation differences are normalized. Reference refresh is a
separate explicit action and always uses the pinned official transform.

The final focused run passed 71 checks: 16 DocBook scripts, the native
reference gate, and 54 DOCX/editor regressions covering the shared helpers.
The MIR-only DocBook run passed all 17 checks. The reference gate compares
24 complete bodies across single-page and chunked publications.

```sh
./test/test_lambda_gtest.exe --gtest_filter='DocBookTests.*:AutoDiscovered/*docbook*:*docx*:*edit_*:*editor_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='DocBookTests.*:AutoDiscovered/*docbook*'
```

A native Radiant render of the exported article produced
`temp/docbook-preview.svg`. External Google Fonts were unavailable in the
sandbox, so this checks successful HTML/CSS rendering with fallback fonts,
not pixel equivalence with a browser using those fonts. Python parsing,
manifest/resource hashes, paired goldens and owned-source whitespace checks
also passed. Unmodified upstream CSS retains two original trailing spaces;
official verbatim goldens retain their significant space-only content line.

The full `make test-lambda-baseline` was attempted. Its input-test build
currently fails on unresolved `_array_push` from `wiki_record_tag` in the
workspace's `block_wiki.cpp`; this task does not change that parser or add a
runtime link to conceal the module-boundary issue. Focused Lambda package
checks remain independently runnable. Forced interpreter mode separately
exposes the existing unsupported `AST_NODE_INDEX_ASSIGN_STAM` capability in
the procedural loader; automatic/MIR execution is the verified route.

## Direct source viewing

Direct source viewing now accepts
`./lambda.exe view test/input/docbook/showcase.xml`. The sample is an original
DocBook book with chapters, a table, lists, links and notes. XML dispatch uses
expanded root names and preserves generic XML's stylesheet/source behavior.
The source adapter resolves bundled CSS from Lambda's installation and
captured media through data URIs, without writing an export (D7.2.4,
S12.1.1v3). The file browser previews it through that route and retains XML
in the Source tab.

The viewer test exposed a shared loader ordering defect: `:root` matching
consults `DomDocument.root`, but generated-document loaders published that
field only after the initial cascade. `create_layout_dom` now publishes the
constructed root before CSS discovery/cascade so inherited xslTNG variables
are available. The upstream CSS remains unmodified.

Validation passed five focused native view checks (DocBook direct/browser,
generic XML and XHTML), plus all 18 DocBook package/reference checks. The
source-view fixtures assert the rendered title, chapter structure, links,
table, footnote and an upstream root-variable background color; the browser
fixture also checks the original XML Source tab. The targeted Radiant integer
cast lint passed. Both full Lambda and Radiant baselines were attempted again
and remain blocked at their test builds by the unrelated wiki `_array_push`
linker failure.

## Remaining milestones

| Proposal milestone | Current state |
|---|---|
| 0 — Baseline/audit | Source/distribution/processor pin, copied resources, reference runner and XML/HTML audit completed |
| 1 — Foundation | Namespace/content adaptation, occurrence indexes, IDs, basic numbering and dispatch implemented |
| 2 — Useful subset | Core article/book HTML, notes/lists/tables, CSS and local media export implemented |
| 3 — Broad HTML | Partial; advanced vocabulary, locale predicates, generated material and preprocessing remain |
| 4 — Publications | Core chunk planning/navigation/links, customization and direct XML viewing implemented; full media/resource closure remains |
| 5 — Release/upkeep | API/support/provenance docs and reference gate present; full release scope and release performance measurements remain |

The exact supported profile and intentional differences are maintained in
[`UPSTREAM.md`](../../lmd/package/doc/docbook/UPSTREAM.md) and
[`port-map.json`](../../lmd/package/doc/docbook/port-map.json). In particular,
upstream CSS still references external Google Fonts, table-specific notes
are diagnosed, and full localization, generated material, scripts and print
publishing are not claimed.
