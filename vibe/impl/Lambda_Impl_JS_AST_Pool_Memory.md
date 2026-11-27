# Reduce retained JavaScript AST pool memory

## Current checkpoint

**Implemented, 2026-11-22.** The 224-page arm64 release replay measured **728.85 MiB** in live `js.ast` bytes, down **490.70 MiB (40.24%)**. The result is **2.88 MiB** below the 40% ceiling. The parser now frees its reduction buffers when parsing returns. Shared binding records use packed flags, and Lambda-only node fields live in typed extensions and an eight-byte builder prefix. [Result and verification](#release-result)

The main checkout was `master` at Lambda `9cc3be8e57` plus the working-tree implementation, with Radiant `161ad05` and the original benchmark's compatible Input `b35af04`. Both executables used the arm64 release profile and the same capture harness. [Build and benchmark provenance](../radiant/Radiant_Design_View.md#6-javascript-enabled-release-memory-census)

## Baseline allocation census

Each page ran in a fresh arm64 release process with local resources, a 1200 × 800 viewport, 250 ms of virtual settling and a live script realm through the post-layout sample. The JS-off control had zero `js.ast` pools. Summing the root pools with that exact label in JS-on snapshots gives **1,278,787,092 live bytes in 3,867 pools**, across 174 pages. The other 50 pages contributed zero. For affected pages, the median was 7.67 MiB, the nearest-rank p95 was 12.39 MiB, and the largest was 23.03 MiB on `web-tmpl/reveal/index.html`.

The 171 web templates contributed **1,191.45 MiB (97.7%)**. The 53 other fixtures contributed 28.10 MiB. The 285 pools of at least 1 MiB accounted for 835.46 MiB. Across all pools the allocator reported 21,222,023 allocations, zero frees before capture, 2,169.89 MiB committed and 2,672.50 MiB reserved. Changing extent sizes or allocator headers can improve commitment and footprint, while the 40% gate counts live requested payload under D5.2.4. [Pool counter definitions](../../lambda-input/lib/mempool.h)

An isolated release replay counted requests at the shared JS node constructor, the two parser stack allocation sites and the direct binder. It joined those counters to each page's **still-live allocator IDs** at capture. This excludes abandoned parses and temporary compiler units. Every page reproduced the original multiset of live `js.ast` pool sizes exactly.

| Disjoint component of the live pool total | MiB | How attributed |
|---|---:|---|
| Node records allocated during parse | 689.59 | 9,120,678 records, counted by kind and actual allocation size |
| Reduction stack entries | 219.85 | One 24-byte `JsCParseValue` per successful push |
| Temporary child-pointer arrays | 13.74 | Requests for reductions with more than two children |
| Additional allocations during direct binding | 151.96 | Pool growth from the end of parse to the end of bind |
| Other bytes at the end of parse | 15.03 | Parse-end pool total less the three counted parse categories, including pre-existing bytes |
| Growth after binding | 22.53 | Captured pool live bytes less the bind-end reading |
| Pools without a source parse | 106.85 | IDs recorded by the open-realm planner |
| **Total** | **1,219.55** | Sum before rounding |

The pool name describes its allocator owner. The interpreter keeps executable nodes, source facts, lexical scope and module identity available for closures, future events, dynamic code and promotion (D8.4.1–D8.4.5). The builder transfers the pool to the retained script on adoption. Its copied source uses a separate system allocation on that path. The common index also allocates arrays outside the pool. The original snapshots put execution overlays at just 0.31 MiB under `js.ast.overlay`. Scope of measurement matters when evaluating a cache or an index change. [Builder, cache and adoption](../../lambda/js/js_scope.cpp) · [Common index allocation](../../lambda/runtime/ast-core.cpp) · [Execution rules](../../doc/Lambda_Formal_Design.md#d84-lambdajs-execution-tiers)

The **106.85 MiB** row matched all 174 pools marked by `mvp_lmd_plan_open_realm`. This path creates a frontend to plan intrinsic library shapes and runtime slots without parsing a script. The script-pool label also covers the frontend's later realm allocations. The budget gives this row zero savings. [Open-realm planner and library shape allocation](../../lambda/js/mir/js_mir_program.cpp)

Among the attributed script owners, `jquery.js` accounted for 194.69 MiB, `jquery-1.8.2.min.js` for 119.97 MiB and `bootstrap.min.js` for 117.84 MiB, grouped by basename across page runs. Within a single page, grouping retained parsed units by source hash leaves only 1.70 MiB in copies beyond the smallest representative per hash, summed across the corpus. This is a generous upper estimate for sharing, since the cache key also distinguishes origin and parse mode. The benchmark gives cross-page sharing no credit because each process starts cold.

## 1. Give reduction scratch the lifetime of the parse

`js_c_push` allocates one linked cell from `tp->pool`. Popping it moves the head pointer. `js_c_take_values` already uses two embedded slots for the common unary and binary cases, but allocates a fresh pointer array for each wider reduction. Both allocation families stay in the script's pool after the program reduction has linked the executable tree. Their measured total is **233.59 MiB (19.15%)**. Removing them leaves **985.96 MiB**, with **254.23 MiB** still to save. [Push, take and final program reduction](../../lambda/js/js_c_parser.cpp)

Give each `JsCAstSink` its own growable stack of node-and-span pairs. Address entries by stack index so growth cannot invalidate a retained parser pointer. Preserve source-order and containment checks and drain top-level statements in the current order. Keep the two embedded child slots and reuse one growable buffer for wider reductions. Verify every consumer finishes with the borrowed slice before the next reduction. Any constructor that persists children must retain the child links or own its final array. Release both buffers on normal completion, syntax rejection, allocation failure and compiler recovery. Record maximum depth and arity so capacity follows the input. A single giant scratch arena retained by the realm would defeat the lifetime change.

## 2. Shrink the common bindings and the frequent node shapes

Use the existing core node kinds, stable pointer identities, child enumeration and dense IDs (D8.2.1–D8.2.5). Define a real shared C++ base containing the fields both walkers need. Put fields specific to a Lambda producer in a typed extension allocated by that producer. A JS constructor allocates a complete object of the common type. Shared walkers read the common type, and consumers of the extended fields must establish producer provenance. Audit every cast, copy, synthesized node, and in-place morph before converting a kind. This preserves direct pointer loads in the interpreter. [Common layouts](../../lambda/runtime/ast-core.hpp) · [JS aliases](../../lambda/js/js_ast.hpp) · [JS constructors](../../lambda/js/js_c_ast_helpers.cpp) · [Unified AST rules](../../doc/Lambda_Formal_Design.md#d82-the-unified-ast)

The following estimates use the **current 40-byte common header** and the measured count of each allocation. They can be validated one kind at a time. The first two kinds alone account for 350.04 MiB today.

| Node kind | Allocations | Current bytes each | Proposed bytes each | Projected saving |
|---|---:|---:|---:|---:|
| Identifier | 3,853,426 | 72 | 56 | 58.80 MiB |
| Member access | 1,244,454 | 72 | 64 | 9.49 MiB |
| Call | 546,211 | 128 | 64 | 33.34 MiB |
| Literal | 659,763 | 88 | 56 | 20.13 MiB |
| Binary operator | 569,029 | 88 | 64 | 13.02 MiB |
| Assignment expression | 330,522 | 128 | 64 | 20.17 MiB |
| **Six kinds** | **7,203,405** | | | **154.96 MiB** |

- **Identifier and member.** Keep the interned name and resolved binding on the identifier. Move the 32-bit static property-key cache slot onto its owning member alongside the computed and optional flags. Keep the member's property child and span. A private or computed key still uses its existing path. Preserve Lambda's capture metadata and the allocation size required where a Lambda identifier can morph into a member.
- **Call and assignment.** Retain callee and argument links, spread and optional flags, and the preplanned argument shape. Assignment needs its operands, operator and parenthesized-target flag. Lambda's pipe, colour, propagation and place-analysis fields belong on the producers that populate them.
- **Literal and binary.** A JS literal needs its tagged literal payload, realm cache slot and flags. Represent Number and BigInt according to their discriminator, retaining BigInt spelling and preserving negative zero. Audit the existing primary-node inheritance and every common reader. A binary needs two operands and its operator. Lambda spelling and pipe-analysis fields can live on its extended allocation. Keep type, constant handle and source span directly accessible throughout.

The binder census accounts for almost the whole 151.96 MiB phase. It allocated **458,905 `NameEntry` records at 200 bytes** (87.53 MiB) and **537,925 `NameScope` records at 120 bytes** (61.56 MiB). The remaining **2.87 MiB** covers other bind-phase allocations. Start with the entries. At **64 bytes per entry** and **88 bytes per scope**, the modeled savings are **59.52 MiB** and **16.42 MiB**, respectively. [Scope and binding creation](../../lambda/js/js_scope.cpp) · [Direct binder](../../lambda/js/js_direct_scope.cpp)

Keep declaration order, owning scope, resolved declaration, storage slot and JS flags on the binding. Keep Annex B companion identity, imports and live lexical semantics accessible wherever they apply. Move the extensive Lambda copy-on-write, type-binder and place-analysis fields to its typed record. Keep the parent, declaration list, name index, slot counts and JS activation facts on the common scope. Pack boolean state and audit which planner fields each language reads before settling its layout. Count every auxiliary allocation needed to preserve a rare fact against the projected saving. Keep the existing indexed lookup for wide scopes.

## 3. Close the remaining gap in the shared node header

Parser scratch, the six node layouts and the binding changes project to **464.48 MiB (38.09%)** together. That leaves **23.33 MiB** to reach 40%. Reducing the shared `AstNode` header from **40 to 32 bytes** offers **69.59 MiB** across the measured 9,120,678 constructor allocations. This is incremental to the six-kind table, which retains the original header in every target size. At these counts, an eight-byte cut on 3,058,505 allocations would cover the remaining gap.

The first 16 bytes of `AstNode` contain the kind, constant handle, binding epoch and Lambda parser syntax fields. The other 24 bytes hold the type pointer, sibling pointer and source span. Preserve all three and the kind and constant handle. Move the four-byte `last_bind_epoch` and four bytes of `syntax_*` into Lambda-owned construction and binding storage. The field accesses in this checkout are concentrated in `build_ast.cpp` and `parse_type_pattern.cpp`. Preserve Lambda's ability to bind retained fragments and run later binding epochs. [Common header](../../lambda/runtime/ast-core.hpp) · [Lambda binding](../../lambda/runtime/build_ast.cpp) · [Type-pattern construction](../../lambda/runtime/parse_type_pattern.cpp)

Prototype an eight-byte allocation prefix at Lambda construction sites that require those fields. Return an aligned pointer to the actual common node, and restrict access to the prefix to constructors and walkers that know its provenance. Audit synthetic, stack-allocated and morphed nodes, alignment, individual frees and existing offset assertions before selecting that representation. The common child walker, T0 reads and MIR lowering should retain direct access to semantic facts under D8.2.4–D8.2.5. A shared ABI change requires rebuilding all producers and consumers, including TypeScript, and revising the AST cache ABI for incompatible images. If the prefix costs Lambda execution time or complicates mixed-origin nodes, revisit its storage before landing this step.

| Corpus budget | Incremental saving | Cumulative saving |
|---|---:|---:|
| Parser stack and wide-reduction arrays | 233.59 MiB | 19.15% |
| Binding and scope layouts | 75.94 MiB | 25.38% |
| Six frequent node layouts | 154.96 MiB | 38.09% |
| Eight-byte header reduction | 69.59 MiB | **43.79%** |

The full model leaves **685.48 MiB**, about **46.25 MiB of headroom** below the acceptance ceiling. All size targets assume the arm64 ABI and the observed allocation counts. Record actual `sizeof`, allocation counts and extension bytes after each change. Phase projections can move together when a representation changes. Recompute the remaining budget from the measured total before taking on the shared-header change.

## Release result

| Metric | Baseline | Updated | Reduction |
|---|---:|---:|---:|
| `js.ast` live bytes across 224 runs | 1,219.55 MiB | 728.85 MiB | **40.24%** |
| `js.ast` committed bytes | 2,169.89 MiB | 1,254.14 MiB | 42.20% |
| `js.ast` reserved bytes | 2,672.50 MiB | 1,448.33 MiB | 45.81% |
| JS-on median process footprint | 76.24 MiB | 71.62 MiB | 6.06% |
| JS-on p95 process footprint | 117.66 MiB | 105.78 MiB | 10.09% |
| JS-on p95 peak RSS at capture | 143.47 MiB | 134.00 MiB | 6.60% |

The updated host reproduced 174 live realms and 3,867 pools. Live pool bytes totaled **764,250,420**, including the unparsed realm pools. The execution overlays retained 323,586 bytes in each build. The median pool payload among pages with a live realm fell from 7.67 to 4.57 MiB. The largest page, Reveal, fell from 23.03 to 13.44 MiB. Lambda's `direct-parser.pool` also fell from 255.72 to 223.86 MiB across the same JS-on runs. All 224 JS-off layouts and 221 JS-on layouts matched after stripping snapshot timestamps. Three templates differed in displayed clock text and its resulting geometry. Reported script exception counts matched for each page.

The shared header is 32 bytes on arm64. The identifier is 48 bytes. Member access, call, literal, binary and assignment records each use 56 bytes in JS. The literal keeps separate numeric and arbitrary-precision payload fields so MIR readers retain the existing numeric projection for BigInt. The shared binding and scope records use 144 and 96 bytes. Grouping and packing their existing flags kept both planners on direct field access and met the corpus target without auxiliary binding allocations. The Lambda syntax metadata and binding epoch remain in the allocation that owns each Lambda builder node. [AST layouts](../../lambda/runtime/ast-core.hpp) · [Lambda builder allocation](../../lambda/runtime/build_ast.cpp) · [Reduction scratch cleanup](../../lambda/js/js_scope.cpp)

The Test262 baseline passed **40,261 of 40,261** admitted cases. The focused JS script suite passed **277 tests**, including the scratch lifetime regression. The direct JS parser suite passed **22 tests**, and the JS/TypeScript parser differential matched **620 files**. Debug-profile syntax checks passed for every changed implementation file. A host with AddressSanitizer on the AST builder, walkers and allocator callers passed **22 smoke cases** across JavaScript AST, MIR and AUTO, Lambda interpreter and JIT, and two Radiant pages. The Lambda interpreter comparison passed 1,605 of 1,612 tests on both releases. Both runs had the same seven failures in the isolated harness, covering debug trace fixtures and socket-dependent cases.

The paired end-to-end release benchmark used fresh JS-on processes in alternating order with snapshot capture disabled. Median load and layout time was **284.72 → 283.93 ms** (−0.28%). The p95 was **1,028.64 → 1,003.31 ms** (−2.46%). The command also serializes the view tree, so these timings describe the full CLI operation. Seven repeats each on Bootstrap, Reveal and 3-column Portfolio gave median differences of −2.98%, −0.40% and +0.07%. The small `about.html` fixture showed +2.61% on the initial seven repeats and +0.62% on a 31-pair follow-up, from 75.84 to 76.31 ms.

## Verification protocol

1. **Memory.** Repeat the 224 matched page pairs with identical resource policy, cache policy, viewport, settle window and realm lifetime. Sum root-level JS-on minus JS-off live requested bytes for the same owners. The exact pass threshold is **767,272,255 bytes**. Include new retained buffers, side records, overlay growth and any replacement owner. Count a backing allocation once. Report committed and reserved bytes, process footprint median/p95, and peak RSS separately. Also sample teardown and repeated navigation to catch extended lifetimes. [Original measurement protocol](../radiant/Radiant_Design_View.md#6-javascript-enabled-release-memory-census)
2. **Behavior.** Compare per-page script outcomes, errors and layout trees to the control. In the attribution replay three clock templates changed their displayed minute or second and corresponding text geometry. Pin the script-visible clock for deterministic comparisons. Exercise closures and timers after first layout, module imports and cycles, direct eval, constructors, private/computed keys, optional chains, tagged literals, promotion, cache clones and allocation-failure cleanup. Keep cached templates immutable and realm-local GC references in traced execution state under D8.4.1–D8.4.6.
3. **Compatibility.** Run the direct parser tests and parser differential, AST/MIR parity and the Test262 baseline. Include TypeScript parsing and Lambda baseline coverage for shared records, in-place morphs, binding epochs and REPL extensions. Run a focused sanitizer build for stale pointers into scratch buffers and reads of extended fields from common records. Use release builds for the timing and memory comparisons.
4. **Performance.** Use uninstrumented matched release builds in alternating order. Take at least seven paired repetitions on the large jQuery, Bootstrap and Reveal pages, plus a small inline script and a page without a realm. Compare parse/bind/index time, first layout, T0 throughput and hot MIR promotion. Run the full corpus for end-to-end median and p95. Investigate any repeatable slowdown above 2% at the median or 5% at p95. Repeat borderline results with more samples. A change that buys pool bytes by adding hot lookups or by shifting memory to another allocator fails qualification.

Local evidence is in `temp/js-ast-retention/`. `base.py` recomputes the baseline from the original pairs. `owner-profile.py` builds the isolated diagnostic host, `run.py` replays the manifest, and `analyze.py` and `bind.py` join site and owner counters to each page's captured pool IDs. The summaries are `summary.txt` and `bind-summary.txt`. The implementation replay, performance results and test logs are under `temp/js-ast-impl/`.
