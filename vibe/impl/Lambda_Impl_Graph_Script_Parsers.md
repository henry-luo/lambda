# Graph and TikZ parsers in Lambda Script

**Date:** 2026-11-25

**Status:** All five C parsers retired. The Lambda cutover and release benchmark are complete.

**Design:** [Graph package §11.1 and §11.8](../Lambda_Pkg_Graph.md#111-parser-principles), GP1–GP4.

## Interpreter and script tuning

The performance follow-up starts from `0244714efc` in the main checkout.
It tunes the Lambda readers and AST interpreter under S12.1.2v3 and D8.3.1v3.
An optimized baseline executable is preserved in `temp/graph-parser-tuning/`.
Independent LaTeX work shares this checkout.

The shared scanner reads trivia directly from bytes while retaining custom
comment markers. DOT passes offsets alongside diagnostics and counters.
Mermaid stops at the first matching directive and retains its family table
at module scope. The interpreter predicts compact numeric comparisons and
reuses resolved call facts under D7.4.1 and D8.2.4. The package bridge still
validates source identity under D8.5.1 and skips the import-initialization walk
for an initialized module instance. Byte reads preserve u8 and absence
(S2.5.8, S7.2.1).

The optimized build matches the 92 serialized Mark results, 247 DOT recovery
probes, and 43 upstream SVG checks in each tier. The source-contract and
byte comparison goldens pass with forced and poisoned GC (D6.3.3).
Replacing a loaded parser's source within the same process selects the
new generation (D8.5.1).
The interpreter walker and frame-plan suites pass 51 of 52 cases.
The remaining checked-store fixture already rejects its unchecked procedure
call with E228 on the baseline executable. The full Lambda baseline passes
6,110 of 6,131 cases, with the same 21 failing test names as the upstream
render follow-up baseline. The cache, scalar comparison, security runtime,
and MIR forced-GC suites passed.

### Tuning measurements

The replay uses the `release_profile` host on macOS arm64, the same 92-file
manifest, three randomized process rounds, and nine three-pass samples
after seven warm passes in each process. Values are medians of process
medians in milliseconds. The isolated interpreter row runs the original
package sources on the tuned host. Each round includes all four variants
under the two recorded tier selections (D8.3.1v3).

| Change | Interpreter, 92 files | Auto, 92 files | Interpreter, 21 DOT files | Auto, 21 DOT files |
|---|---:|---:|---:|---:|
| Baseline | 143.32 | 68.59 | 51.20 | 23.42 |
| Scripts | 114.11 | 60.43 | 31.29 | 14.64 |
| Runtime | 130.31 | 70.97 | 50.07 | 21.37 |
| Combined | 97.18 | 55.22 | 26.30 | 12.32 |

The combined changes reduce whole-corpus warm time by 32% in the interpreter
and 19% in auto. The runtime-only auto results vary by process and show no
consistent improvement across all formats. A headless `view` spot check
with one file per format and five runs per file took 495 ms before and
485 ms after, summed per-file medians. This compares the complete loading,
layout and rendering path (D9.5.6v2). Raw samples and qualification logs
are under `temp/graph-parser-tuning/`.

## Completion record

- The migration covers the five source parsers. Layout, normalization and rendering continue to consume their existing Mark contracts (D9.5.6v2). The worktree is `master` at `8880b89318` and `lambda-input` at `91d88f1`, with implementation edits in both and independent LaTeX work in the main checkout.
- All five script readers share `lambda.io.source_scan` for byte-based scanning and source positions (D9.3.4, S2.5.8). The native input layer retains detection, acquisition, diagnostics and Structurizr include policy. It asks the host to parse admitted include fragments and adopts them into the same Input owner (D9.1.2v2, D9.1.3v2, D6.3.3, D11.1.2, D11.2.1). The retired parser and graph helper sources are removed. Standalone `lambda-input` reports a parser-host-required error for graph and TikZ. Restricted runtimes use their normal approved shipped-package source grants (D11.2.1). Explicitly parsed archive members use the host and share successful cache entries with native content access (S12.4.1v2).
- The release `liblambda-input.a` contains no D2, DOT, Mermaid, Structurizr or TikZ syntax-parser objects. The remaining graph input object is `input-graph-structurizr-resolver.cpp.o`, which applies include policy and calls the Lambda fragment reader (D9.1.3v2, D11.2.1).
- The macOS arm64 native oracle is `temp/graph-parser-port/native.exe` with SHA-256 `1e03da788674eaec609591e980e42e1c998a0d9c8955c1c232ecbf93a0389f15`. `*-corpus.ls` and `*-corpus.log` there record exact serialized source Mark equality over 3 D2, 21 DOT, 13 Structurizr, 47 Mermaid, and 8 TikZ fixtures. The DOT grammar probe in §2 characterizes the recovery tradeoff.
- `make build` and `make test-graph-graphviz test-graph-mermaid test-graph-structurizr` passed. The hosted parser, formatter, and Mermaid suites passed 23, 9 and 40 assertions respectively. The focused script runner passed 94 graph, TikZ and archive integration cases. The security-runtime suite passed all 126 cases. Both Radiant tests covering retained TikZ paint and shared loader equivalence passed. CLI checks covered DOT-to-Mark, schema validation, selected-view Structurizr HTML, D2 layout, Mermaid and TikZ SVG rendering, and headless `.gv` and `.dsl` viewing. The scripts total 3,851 lines against the 4,000-line source budget. Logs are under `temp/graph-parser-port/`.
- The source-contract golden covers malformed input, byte spans, archive-member dispatch, TikZ numeric underflow and nested diagnostics. It matched the oracle under `interp`, `jit`, `auto`, and collection on every allocation with freed memory poisoned. The restricted Structurizr include cases also passed under forced collection (D8.3.1v3, D6.3.3).
- The full Lambda baseline recorded 6,107 passes of 6,130 before the shipped-source test fixture update. The two Structurizr failures passed in the final security suite. Other failures include denied loopback binds and a DOM media playback case. The standalone input baseline reported loopback-related HTTP failures and a Bootstrap CSS round-trip failure. The archive header and native ownership audits passed. The staged input isolation rebuild stopped on obsolete Premake targets ([LI-115](../Lib_Input_Issue_Ledger.md#li-115)). The supplementary LaTeX-wide replay was stopped after a TeX expansion timeout. The full Radiant baseline was skipped.

## Follow-up upstream render comparison

The 2026-11-25 follow-up starts from Lambda `9d907361f3` and `lambda-input`
`a380230`. It adds 41 pinned upstream source fixtures and compares 43 SVG
renders because two Structurizr workspaces contribute two views each. The
corpus includes 8 D2, 9 DOT, 10 Mermaid, 7 Structurizr and 7 TikZ source files.
The manifest records SHA-256, source revisions and license notices. Candidate
files were checked against 407 existing sources for content duplicates after
removing whitespace.
[Fixture provenance and replay commands](../../test/lambda/graph/upstream/README.md)

The reference tools are D2 0.7.1, Graphviz 15.0.0 through Viz.js, Mermaid
11.16.0 through headless Chromium, Structurizr CLI 2025.11.09 libraries through
their DOT exporter and Graphviz, and TeX Live 2025 with PGF 3.1.10 through
pdfTeX and Poppler. Runtime comparisons honor the Lambda and Radiant
layout connection (D9.5.6v2). They check rendered relationships, labels,
markers, ports, selected paint, and geometry where the source gives an
invariant. Offline replay uses observations captured from these tools.

| Format | Corrections exercised by the upstream corpus |
|---|---|
| D2 | Emit every chained connection, preserve multiword and final isolated identifiers, retain unlabeled and parallel links, orient left arrows by their logical direction, and apply the declared graph direction. |
| DOT | Apply cluster outline colors, widths, dashes and fill precedence. Suppress the label when the cluster has no authored label. |
| Mermaid | Rank subgraph links using an entrance or exit member and route to the cluster border. Pass the compound option as text across the in-memory DOM bridge. Read full unquoted subgraph titles and separate an extended arrow operator from its inline label. |
| Structurizr | Match the generated view-key numbering, preserve inherited dynamic descriptions, resolve group selection to members, evaluate endpoint predicates, and exclude implied copies by relationship identifier. Lift deployment links that exist when the later instance is declared. |
| TikZ | Start emitted SVG paths with a move command. Honor draw and fill actions, path-local coordinate bases, help-line style, dash patterns and phase, and composite ellipse subpaths with even-odd fill. |

All 43 selected renders match their saved checks under automatic, interpreter,
and JIT execution (D8.3.1v3). A second reference generation reproduced
`expected.json` byte for byte. The focused Lambda integration suite passed
75 cases. The hosted parser, formatter, and Mermaid suites passed 23, 9,
and 40 cases. Both focused Radiant tests passed. The Lambda baseline
passed 6,109 of 6,130 cases. The 21 failures were outside graph and TikZ
coverage and include network binds, editor and sync cases, and DOM media.
The Radiant baseline was stopped after repeated idle timeouts in native window
and Scene3D cases following macOS application-service errors. Before it stopped,
the layout inventory reported 5,632 of 5,936 passes, and the page-load and
snapshot suites passed. The focused retained graph and TikZ tests completed.
Reference SVGs and Lambda renders are under
`temp/graph-upstream/`. Reference regeneration and focused test logs are
under `temp/graph-reference/`.

The selected viewport encloses every fixture. Without explicit viewport
dimensions, continuous export clips some large graphs
([RI-236](../Radiant_Issue_Ledger.md#ri-236)). Exploratory samples also
exposed remaining D2 gaps in nested containers and arrowhead maps and TikZ
gaps in bounding-box commands and bracketed ellipse radii.

## Release loading benchmark

On macOS 26.3 arm64, both release hosts used automatic tier selection (D8.3.1v3) and the same 92 fixtures totaling 36,620 bytes. The native executable was relinked from the pre-port release objects and archives. Serialized source Mark matched byte for byte on all 92 fixtures before timing.

| Format | Files | C parse | Lambda parse | C view | Lambda view |
|---|---:|---:|---:|---:|---:|
| D2 | 3 | 0.028 ms | 0.53 ms | 144 ms | 151 ms |
| DOT | 21 | 0.727 ms | 18.07 ms | 1,494 ms | 1,659 ms |
| Structurizr | 13 | 0.495 ms | 10.35 ms | 590 ms | 659 ms |
| Mermaid | 47 | 0.649 ms | 15.44 ms | 2,127 ms | 2,373 ms |
| TikZ | 8 | 0.151 ms | 5.67 ms | 904 ms | 954 ms |
| **Entire corpus** | **92** | **2.267 ms** | **54.86 ms** | **5,260 ms** | **5,796 ms** |

Parsing measures the production `parse` entry point through source Mark construction, with file bytes already in memory and parser modules warmed. Each row is the median of 11 samples of four passes. The complete corpus was timed as its own pass. The Lambda parse took 24.2× the C parse time for the corpus.

Viewing measures a fresh `lambda view --headless --no-log` process from spawn through exit, including first layout and rendering (D9.5.6v2). Each row sums the per-file medians of three repetitions. All 552 launches succeeded. Total viewing time rose 10.2%, while the median time for an individual file rose from 46.62 ms to 50.92 ms. The harness, fixture hashes, serialized Mark, and timing samples are in `temp/graph-parser-benchmark/`.

## 1. Contracts and ownership

- **D9.3.4** places shipped parsers in `lambda.graph` and `lambda.doc.tikz`, with Mermaid sequence Mark consumed by `lambda.chart`. Parsers are deterministic `fn` exports with local cursor state under **S12.1.1v5** and **S12.1.2v3**. The `parse(...)` and `input(...)` error channels follow **S7.6.2v5** and **S7.6.3v3**.
- **D9.1.1v4**, **D9.1.2v2**, **D9.1.3v2**, and **D9.1.4v2** keep native input acquisition and Mark APIs in `lambda-input` and runtime execution in Lambda. Use a narrow, request-scoped host interface. Lambda and Radiant integrate through **D9.5.6v2**. Use **D6.3.3** and **D5.2.5** for precise GC rooting and native Input ownership at the seam.
- Preserve existing source Mark tag and attribute types, source order, id generation, authored spelling and provenance, and the distinction between recovered DOT, Mermaid and Structurizr diagnostics and raised D2 and TikZ errors. Preserve canonical Mark, formatting and render behavior through the existing consumers.
- Preserve the effective format and feature switches observed today. The routing inventory includes explicit format selectors, `graph:flavor` where supported, `dot`/`graphviz`, `structurizr`/`c4`, the existing DOT default, file detection, and `LAMBDA_NO_GRAPH_INPUT` / `LAMBDA_NO_LATEX` profiles. Pin actual precedence with tests. `.dsl` detection requires a boundary-safe `workspace` after admissible trivia. Check graph and TikZ dispatch for in-memory text and bytes, filesystem nodes, local files, and supported remote targets.
- **D11.1.2** and **D11.2.1** govern Structurizr include I/O. The parser receives source text and produces inert directives. For graph and TikZ formats, native embedders supply an `InputScriptParser` provider. Calls without the provider return an explicit parser-host-required error.

## 2. Choosing the parser approach

Lambda `grammar` is a PEG declared in script. It supports ordered choice, lookahead, lexical patterns, space control, recursive rules, precedence, and labeled captures. It commits to a successful rule branch and returns rule-shaped elements, with opt-in byte spans. A whole-input failure raises at the furthest failure. Grammar output must be lowered to existing source Mark. Adjacent string tokens may merge, so a grammar must use labeled fields, symbol tokens, or source slices when token boundaries matter. These properties follow [GM4, GM10, GM11v2 and GM12](../Lambda_Design_Grammar_Parser.md#7-the-parse-tree-gm4-gm10gm11).

| Reader | Best case for a grammar | Work a direct reader avoids |
|---|---|---|
| D2 | Small statement core and one supported edge operator. | Newline-sensitive labels, dotted IDs versus property suffixes, and style blocks can stay on the same cursor as the scanner’s existing line recovery. |
| DOT/GV | Strongest candidate. The standard graph, statement, subgraph, endpoint, and attribute rules read naturally as productions. | Quoted concatenation and physical continuations, balanced HTML IDs, keyword boundaries and case, and `dot.endpoint`-style recovery need format-aware scanning or lowering around the grammar. |
| Mermaid | Separate productions could express short fragments for individual families. | Family dispatch, front matter and initialization directives, Markdown/HTML labels, shape delimiters, edge operators and ids, and context-dependent recovery stay local to each family reader. |
| Structurizr | The three statement shapes fit one small generic grammar. | A single line/block reader can classify by context, keep unknown statements, retain quoted argument distinctions, and parse included fragments in an inherited context with local diagnostics. |
| TikZ/PGFPlots | The bounded expression subset fits grammar precedence rules. | TeX control sequences, comments and groups, dynamic style/pic expansion, recovery and anchored errors need explicit parser state. Port the existing precedence parser with its limits and exact Mark first. |

The malformed DOT probe used `digraph { a -> ; c -> d; }`. Native `parse` returned `dot.endpoint` at line 1, column 16 plus source Mark for `c -> d`. A minimal Lambda grammar with named IDs raised at line 1, column 16. On `digraph { "é" -> b; c -> d; }`, the grammar captured the quoted ID at byte offsets `[10, 14)`. The probe covered whole-input failure and byte spans on a small grammar.

The [RTF comparison](../input/Input_RTF.md#performance-evidence) is a precedent. Both readers share semantic lowering, and the grammar reader builds a record tree and an offset index. Its release median was about 1.78× the direct reader on the 3,827-byte fixture and 2.63× on the expanded 30,624-byte fixture. Those ratios apply to RTF. The [diff reader](../../lmd/package/doc/diff/parse.ls) shows a good grammar fit for structured records with separate semantic checking.

**Decision (GP1):** Use direct Lambda scripts for all five production readers. Revisit a bounded grammar fragment only when it removes code while keeping Mark, limits, diagnostics, source anchors, and release measurements acceptable. DOT is the best full-grammar comparison candidate and PGF math the best isolated candidate. A second production parser or changes to the grammar engine require a separate justification.

## 3. Shared parsing utilities

Keep the shared cursor and scanners private to the shipped parsers initially. Start with D2 and DOT. Use a format-neutral module reachable by TikZ only for operations that have the same contract there. Names and signatures below describe responsibilities.

| Operation | Reuse and contract |
|---|---|
| Source, cursor and spans | Keep a bounded cursor into one immutable source with an explicit offset unit. Prefer character offsets for string operations in P1 and project to half-open byte spans at the Mark boundary. Confirm indexing cost using large multilingual input. If a byte cursor is simpler or faster, reuse `lambda.io.utf8` for text conversion where compatible. Test against native behavior for binary admission and malformed bytes before deciding how to decode. Count columns by UTF-8 code points after LF, preserve CRLF behavior, and include EOF in boundary tests. |
| Line and byte lookup | Reuse `lambda.io.search.lower_bound` for a prepared index. `lambda.io.line_index.from_bytes` currently requires nonempty LF-terminated input. Reuse its offset-building logic only if extracted without changing that public contract. Use the byte/character distinction in **S2.5.8** and **S17.4.1**. Avoid re-encoding or walking the source prefix for every emitted span. |
| Trivia, bounded runs and keyword lookahead | Share scanning mechanics and progress checks. Callers provide line-comment and block-comment rules, horizontal versus all whitespace, token punctuation, and case and boundary rules. Scan runs without repeatedly slicing the remaining input. Recognize operators at the current position with longest-match ordering where their grammar requires it. |
| Quotes, groups and separators | Share quote and delimiter walking where openers, closers, escapes and stop rules agree. Retain raw ranges and let the format decode escapes. Review `lambda.doc.latex.util.read_balanced`, `split_top_level`, and `tex_engine.reader.raw_group_end` for TikZ. Their character offsets, error conventions and TeX `%` treatment require an explicit adapter or a shared neutral primitive. Coordinate changes to TeX helpers with the active LaTeX work. Keep DOT’s HTML ID scanner, Mermaid’s composite shape recognition and TikZ’s control-sequence rules local. |
| Diagnostics and recovery | Extend `lambda.graph.diagnostics` with a source-diagnostic element builder if the native common shape warrants it. Keep established codes and optional fields. Reuse bounded movement across quoted or grouped regions. The grammar owns synchronization tokens and decides whether to retain a partial production. Assert each failing loop advances, exits, or returns to its parent. |
| Mark construction and limits | Use source Mark constructors local to each format and a shared span projection. Keep independent node/statement/edge/depth budgets with the existing owner of each limit. Use iterative flat scans and bounded nesting. Measure large flat sequences for repeated prefix copies in arrays or strings and use existing collection/join operations where their complexity is suitable. |

## 4. Implementation sequence

### P0 — Freeze the current contracts and capture native oracles

1. Trace `fn_parse2` and `input_resource` in `lambda/runtime/lambda-eval.cpp`, `input_from_source_n_with_name_parent` and the local/remote/target routes in `lambda-input/lambda/input/input.cpp`. Include the filesystem-node shortcut and preparse type/name-parent checks. Inventory native consumers of the public entry points in the main checkout, submodules, test runners and standalone build profiles.
2. Trace PARSED transforms for graph and TikZ in `lambda/runtime/script_run.cpp`, Radiant loader and export adapters, `graph.document.from_mermaid` for Markdown code fences, the document viewer, and the graph formatter. Trace TikZ’s LaTeX bridge, render exports, PGF math wrapper, generated fragments, and direct `parse` users.
3. Record differential source Mark and error outcomes from the pinned native host for successful, recovered, invalid, empty, malformed-byte, and size-limit inputs. Preserve attribute types, child order, diagnostics and source offsets. Compare Mark structurally, ignoring allocation identity and serialization whitespace only. Use the native corpus and pinned package references. Add paired `.ls` and `.txt` files for new Lambda golden tests. Retain reproducible release baseline commands and scratch captures under `temp/graph-parser-port/`.
4. Make an explicit compatibility decision for each native-only consumer. This includes the native parser assertions in `lambda-input/test/test_graph_parser_gtest.cpp` and `test_graph_mermaid_gtest.cpp` and any formatter tests that currently require parser input. Re-home behavioral assertions to the Lambda-host suites as necessary.

**Exit:** Each observable format has a native oracle, a production entry-point inventory, and an owner for preserving its assertions.

### P1 — D2 and the first shared scanner

1. Add a pure source-to-Mark entry under `lambda.graph`. Port the D2 identifier and property rules, `->` edges, quoted and bare labels, node and style blocks, synthesized endpoints and source spans. Preserve the existing parse error after a failed scan. Use D2’s native source assertions and `test/lambda/graph_d2_routing.ls` to pin Mark and geometry.
2. Introduce the minimal shared cursor and span routines in §3. Run flat/large and Unicode probes on the release build before choosing the offset representation. Cover UTF-8 before an error, CRLF, missing trailing LF, EOF in strings/blocks, and progress after malformed statements.
3. Land a narrow checked dispatch seam with D2 as its first installed parser. Prefer using the host at the existing effective-format dispatch after source bytes and format are known. Pass the current evaluator through an explicit host service and keep the runtime dependency on the Lambda side. Preserve `parse` and `input` return and schema behavior. Test fallback selection of the existing native formats during migration and the explicit provider availability outcome once a format is retired.

**Exit:** D2 parity holds at `parse`, `input` and a CLI render entry point. D2 establishes production dispatch, native-to-script lifetime handling and the first reusable scans.

### P2 — DOT/GV

1. Port strict, directed/undirected and optional graph IDs; numeric, bare, Unicode, HTML and concatenated quoted IDs; comments; ports and compass points; assignments, defaults and repeated attribute lists; nested/anonymous subgraphs, brace sets as endpoints and edge chains.
2. Reuse the D2 cursor and proven lexical operations. Preserve ordered declarations, subgraph roles, endpoint and attribute source kinds, id generation, and recovery inside nested lists or blocks. Keep HTML string scanning bounded and separate from HTML label lowering.
3. Run the native DOT source comparison, Graphviz conformance and formatter round trips. Preserve `dot.endpoint` recovery with a following valid edge, source offsets around Unicode and escaped physical newlines, and semantic HTML and Graph Scene Mark output for `.dot` and `.gv`.

**Exit:** `make test-graph-graphviz` and source compatibility checks use the production script route.

### P3 — Structurizr workspace, fragments, and includes

1. Port the generic statement record and classification table, one workspace entry and one fragment entry with inherited context. Preserve explicit assignments, implicit and explicit relationships, ordered and opaque statements, directive data, quoted metadata, source provenance, and recoverable roots. Reuse the shared cursor and delimiter scans.
2. Adapt `input-graph-structurizr-resolver.cpp` to submit bounded source spans to the same fragment provider. Keep the existing broker checks, allowed root, symlink and directory-entry policy, ordering, depth/file/byte accounting, cycle tracking, and error codes. Preserve `!include` nodes with nested file-local spans and `source-file`. Parse strings and remote content with inert includes. Include syntax inside each fragment is discovered by the script; the host supplies access to admitted source.
3. Preserve a single owner for each tree during include attachment. If retaining the input-owned attach path, copy provider results into the destination Input before adopting them. If attaching to a runtime tree, rebuild through runtime-owned Mark and pass the resolved fragment and metadata through a rooted boundary. Do not splice runtime GC values into a pool-owned graph without an established ownership bridge. Exercise allocation during nested includes and opening, closing and reopening live documents.
4. Run direct and broker-backed tests for context-specific fragments, directory sorting and skips, nested relative paths, cycles, path escapes, missing/remote includes, limits, and diagnostic provenance. Run static, dynamic and deployment C4 comparisons and selected-view CLI conversion.

**Exit:** `make test-graph-structurizr` and both local and broker-backed include controls pass on the same script parser. No C Structurizr syntax parser remains.

### P4 — Mermaid, including the chart-owned sequence adapter

1. Port the header classifier, YAML front matter, initialization metadata, comments and common newline/statement handling. Use one dispatch and source metadata builder for graph and chart families.
2. Port the flowchart reader first. Preserve source and implicit identity behavior, scoped subgraphs, direction, shapes and raw label formats, node lists and chains, edge ids and properties, class/style/linkStyle rules, accessibility and inert interactions. Retain declaration ordering and exact edge expansion behavior observable at the source boundary.
3. Port class, ER and flat state as separate readers, sharing only proven scans and common metadata emitters. Preserve UML member and relation roles, ER cardinality markers and labels, state notes and pseudostates, and the diagnostic for composite states.
4. Port sequence source Mark, including participants, blocks, activations and notes. Keep chart layout under `lambda.chart`. Route unsupported chart families to the existing `mermaid.chart-family` diagnostic. Exercise Markdown fences and standalone previews with identical text, and verify invalid or incomplete directives and family syntax continue to recover as before.

**Exit:** `make test-graph-mermaid`, Mermaid source-fidelity cases, chart sequence fixtures, Markdown embedding, and a retained-render integration case pass with the native parser absent.

### P5 — TikZ and PGFPlots

1. Port `input-tikz.cpp` into the document package. Separate TeX lexical scanning, picture/scope/path commands, options and style/pic declarations, axis and groupplot commands, and the bounded precedence expression parser. Reuse TeX group and comment helpers after comparing native lexical behavior. Reuse existing TikZ nodes and downstream evaluators.
2. Preserve `<tikz_picture>` and all supported child tags/attributes and ordering for standalone fragments, wrapped and multiple pictures, positioning, labels, paths, arrows and shapes, plots, tables, groupplots, loops, styles, pictures expanded by `\\pic`, and supported PGF math. Preserve the 1 MiB input bound, command, point, style and nesting limits, and the PGF expression limits of 4,096 bytes, 1,024 nodes, and depth 64.
3. Preserve parse errors with byte-accurate original-source locations and anchors for substituted fragments. Implement a reusable pure expression reader for existing expression Mark. The document entry and the existing synthetic `\\addplot{…};` wrapper use the script reader through `parse`. The wrapper keeps its established error origin. Native document loading uses `input`, and LaTeX islands and generated fragments use `parse`.
4. Run the `test_tikz*` script goldens, `test/lambda/ledger_tikz_error.ls`, LaTeX integration and PDF tests, the standalone `.pgf` render/loader case, and generated picture and expression error cases. Exercise both nested style/pic expansion and limits.

**Exit:** Existing TikZ/LaTeX entry points use one parser and existing renderers. Native TeX scanning still used by other formats remains under its current owner.

### P6 — Remove retired native code and validate all callers

1. Remove replaced parser sources, declarations and transitional test selectors after each phase passes its parity gate. Keep native detection and include-policy services in their owning modules. Check remaining callers of `input-graph.cpp` helpers before removing them. Keep formatter behavior and fixtures.
2. Replace the C-only `ParserLocBudget` with checks and reporting for shipped parser sources, packaged size, and release-binary delta. Keep all former behavior assertions in the package/host test suites. Adjust the input source inventory in `lambda-input/build_targets.json` where necessary and profile source exclusions in `build_lambda_config.json`. Regenerate the build via Make under **D9.1.5**.
3. Run all format gates, `make test-lambda-baseline`, `make test-radiant-baseline`, `make test-lambda-full` for the LaTeX coverage, and the standalone `lambda-input` baseline. Check host-free linkage and disabled profile behavior. Re-run `parse` and `input` under interpreter, JIT and automatic selection (D8.3.1v3). Use precise-GC stress at dispatch and fragment/ownership boundaries.
4. Release measurements use the same fixtures, machine, tier, and build profile on both sides. Separate cold package load from warm source-to-Mark, and time normalization/rendering separately. Record typical and large inputs, Unicode and malformed records, time, peak memory, release/native size and shipped scripts. Use `make release` in the main checkout only. Investigate regressions against the P0 baseline before cutover.
5. Verify end-to-end CLI `render`, `view --headless`, `layout`, and `convert -t html` for supported routes, plus the document viewer, Markdown and LaTeX embeds. Confirm the parser output survives retained-document layout and formatting with stable IDs, diagnostics and source provenance.

**Done when:** All five production syntax parsers and the Structurizr fragment reader are Lambda Script. The production entry points and pinned parity cases pass. Native code supplies format detection, source I/O, include policy, and runtime integration through their owning boundaries.
