# A further 20% reduction in JavaScript parser and AST memory

## Current checkpoint

**Implemented the 20% combined target, 2026-11-22.** The 224-page release replay measured **1,164.99 MiB** in retained JS AST pools and index arrays, down **371.95 MiB (24.20%)**. The pool-only reduction was **41.90 MiB (5.75%)**. The proposed identifier and common-header changes remain open for the pool-only target. [Implementation result](#implementation-result)

The original pool-only goal corresponds to a **611,400,336-byte ceiling** for `js.ast`, including replacement storage. The combined target is **20% off the pool and index baseline**. Measure parser peak separately. All MiB figures below sum independent page runs. In the baseline, the measured pool and index arrays together had a median of **7.53 MiB**, a p95 of **17.21 MiB**, and a maximum of **28.87 MiB** across all 224 pages.

## What is still live after first layout

The diagnostic replay ran JS on using the prior release harness, the same 53 fixtures and 171 web templates, fresh processes, local resources, a 1200 × 800 viewport and 250 ms of virtual settling. It found 174 live realms, 3,693 parsed pools and 174 open-realm planner pools. Allocation counters were joined to live pool IDs. Index build, clone, append and destruction events were reconciled by backing-array address up to the post-layout snapshot, so destroyed compiler units contribute zero. [Benchmark and build provenance](../radiant/Radiant_Design_View.md#6-javascript-enabled-release-memory-census)

| Retained storage | MiB | Basis |
|---|---:|---|
| Parser-created node records | 469.30 | 9,120,678 constructor allocations |
| Binding records | 63.02 | 458,905 × 144 bytes |
| Lexical scopes | 49.25 | 537,925 × 96 bytes |
| Other parse allocations | 15.03 | Parse-end live bytes less constructor payload |
| Other bind allocations | 2.87 | Bind growth less entries and scopes |
| Growth after binding | 22.53 | Captured pool bytes less bind-end bytes |
| Open-realm planner pools | 106.85 | 174 pools without a source parse |
| **Pool subtotal** | **728.85** | Same metric as round one |
| Seven structural index columns | 425.09 | 36 bytes × 12,381,696 capacity slots |
| Pointer-to-ID lookup | 282.54 | 12 bytes × 24,688,512 hash slots |
| Reverse-query offsets and values | 83.46 | Allocated slice lengths |
| Function, binding and scope index arrays | 17.01 | Allocated capacities |
| **Index array subtotal** | **808.10** | 4,764 live indexes with 9,106,763 occupied node rows |

The combined figure is a **lower bound** on all script parser and AST storage. Declaration hash maps, the optional profile-support byte array, source copies, allocator overhead and execution overlays are outside the subtotal. Source lengths for retained parsed units summed to 33.72 MiB. The prior replay measured execution overlay pools at 0.31 MiB. Cache clones account for just **1.87 MiB** of the measured index arrays despite 1,071 live clone indexes. The planner's initial library-shape call accounted for **12.96 MiB** across the 174 realms. Both models give open-realm pools zero savings. [Index allocation and queries](../../lambda/runtime/ast-core.cpp) · [Source adoption and cache clones](../../lambda/js/js_scope.cpp) · [Realm initialization](../../lambda/js/mvp-lmd/mvp_lmd_program.cpp)

The final diagnostic replay reproduced the previous release’s pool size multiset **on all 224 pages**, totaling **764,250,420 bytes**. An earlier replay differed by 736 bytes in one pool on the Indus countdown page. Its only view differences after timestamp removal were the three clock templates seen in round one.

## 1. Change the index first

| Change, in implementation order | How the estimate was calculated | Projected saving |
|---|---|---:|
| Build child and sibling arrays only for a requesting pass | 2 × 4 bytes × structural capacity | **94.46 MiB** |
| Store the parent as a dense node ID | 4 bytes × structural capacity | **47.23 MiB** |
| Store one node ID per pointer-lookup hash slot | 8 bytes × hash capacity | **188.36 MiB** |
| **Combined, at current capacities** | Distinct backing arrays | **330.06 MiB** |

1. The only consumer of `first_children` and `next_siblings` outside index maintenance is `lambda_flow_children`. Have the Lambda closure-flow pass request this adjacency list. Build it in structural ID order from parent IDs, preserving the current reverse insertion order. JS still has the common child enumerator, parent edges, subtree boundaries and reverse queries required by D8.2.4. Keep requested adjacency valid on append or rebuild it before its next reader. [Lambda consumer](../../lambda/runtime/build_ast.cpp) · [Index construction and cloning](../../lambda/runtime/ast-core.cpp)
2. The index already resolves the parent ID during insertion. Store that ID, with `AST_NODE_ID_INVALID` for roots. Parent-ID queries become one array read. A caller that needs the parent pointer gets it from `nodes[parent_id]`. Check fragment attachment, synthetic functions and the Lambda REPL as well as regular tree walks. [Index API](../../lambda/runtime/ast-core.hpp)
3. The pointer hash table currently stores both a node pointer and its dense ID, and the dense node array already stores the pointer. Keep the hash function and load factor. Probe an ID, stop at the invalid sentinel, and verify a candidate with `nodes[id] == key`. When inserting a duplicate, return the ID already found. Measure the extra dependent load on successful lookups, especially in MIR promotion. The preceding parent change removes one source of frequent hash probes. [Lookup, rehash and parent queries](../../lambda/runtime/ast-core.cpp)

At these capacities the measured combined subtotal would fall to **1,206.89 MiB**. Releasing spare geometric capacity on sealed units is a fallback. After the three changes, exact-sizing the remaining 24-byte structural rows could recover up to **74.96 MiB** more. Repacking has copy and peak costs, and execution overlays can append synthetic field initializers. Give it no credit in the primary estimate. [Field initializer overlays](../../lambda/js/js_scope.cpp)

## 2. Cut another 20% from the original pools

The following changes count only bytes inside the historical pool metric. The identifier and declarator rows use today's 32-byte node header. The final row applies the common header saving once to each constructor allocation.

| Change | Proposed arm64 layout or charge | Projected pool saving |
|---|---|---:|
| Split Lambda-only binding fields from the common record | 144 → 64 bytes × 458,905 entries | 35.01 MiB |
| Allocate a scope's declaration list and name index fields on first declaration | 96 → 48 bytes × 537,925 scopes, charging 40 bytes to each of 132,722 remaining scopes | 19.56 MiB |
| Store one name-or-binding reference per identifier | 48 → 40 bytes × 3,853,426 constructor allocations | 29.40 MiB |
| Split Lambda-only declarator fields | 88 → 64 bytes × 245,869 declarations | 5.63 MiB |
| Compact the common header, with an escape for long spans and constants | 32 → 24 bytes × 9,120,678 records, charging 24 bytes for each of 900 observed long spans | 69.56 MiB |
| **Total** | **13.40 MiB of headroom over the required 145.77 MiB** | **159.16 MiB** |

**Bindings and scopes.** Keep the name, declaration, owner, declaration order, slot, JS flags, import and Annex B links directly accessible in a common binding. Allocate Lambda's type-binder, copy-on-write and interpreter planning data in a typed Lambda record. Of the constructed scopes, **405,203 (75.3%)** were observed empty at indexing. Keep each scope identity, parent, kind, strictness, activation owner and slot facts available even for an empty scope. Move the list ends, hash pointer, entry count and hash occupancy/capacity to the lazily allocated declaration storage. Preserve declaration order and the existing wider-scope hash lookup. Charge all later materializations. Tests must cover direct eval, `with`, catch and loop environments, module live bindings and Annex B collisions. [Shared layouts](../../lambda/runtime/ast-core.hpp) · [Direct scope binding](../../lambda/js/js_direct_scope.cpp)

**Identifiers and declarators.** Of 3,837,920 indexed identifier occurrences in retained parsed units, 2,320,185 had a binding. Every bound occurrence pointed to exactly the same interned name as its binding. A name-only identifier needs its interned spelling. A resolved occurrence can obtain that spelling from its binding. Use one discriminated reference with accessors for spelling and binding. Preserve source spelling through the original source span and preserve an explicit spelling wherever binding identity and name genuinely differ. Confirm alignment on supported ABIs if using a low-bit tag. Clear or rebind an identifier only after recovering its name. Retain static property-key child nodes, source positions and stable IDs. The 64-byte declarator keeps `id`, `init`, `entry` and `declared_type` after the existing header. Lambda owns the extra fields it populates. TypeScript annotations and the shared declaration-index reader continue through the common record. [Identifier binding and fixups](../../lambda/js/js_direct_scope.cpp) · [Typed declarator construction](../../lambda/js/js_c_ast_helpers.cpp)

**Common header.** Prototype this last. Use a 16-bit node kind, a 16-bit source length, the full 32-bit start offset, the direct effective type pointer and one pointer for the sibling link. Reserve length `UINT16_MAX` to indicate a pointer to an extended record in the sibling slot. That record can hold the actual sibling, full end offset, constant kind and full constant index. A normal node retains direct kind and type reads. Reading its sibling or span requires a predictable length check. This keeps source positions and inferred type and constant facts on the node under D8.2.4–D8.2.5. [Common header](../../lambda/runtime/ast-core.hpp) · [Source coordinate contract](../../lambda/runtime/source_span.h)

Among indexed nodes in the retained parse units, **900** spans needed at least 65,535 bytes, zero spans were reversed, and zero had a constant handle at initial index publication. These counts describe this JS corpus. Lambda's constant folding, later span widening, constructed nodes outside the index, and stack-allocated synthetic nodes all need explicit support. Use owner-backed storage when widening a retained node and inline extended storage for stack temporaries that need it. Publish only after allocation succeeds. Keep copied or morphed records independent when their sibling or source facts can diverge. Count every extra record at capture and remeasure Lambda as well. The 13.40 MiB margin also pays for layout padding and spelling exceptions. [Lambda constant folding](../../lambda/runtime/interp.cpp) · [Constant materialization](../../lambda/runtime/const_materialize.cpp) · [Builder ownership](../../lambda/runtime/ast_build.hpp)

## 3. Reduce parser work during loading

**Static import discovery built and discarded 1,179 complete frontends on 151 pages.** The replay saw 4,872 successful parse-builds. Of these, 3,693 had live pools at capture. The other 1,179 were positively identified at `js_ast_prebuild_discover_imports`. That callback runs the full parse, bind, validate and index pipeline, reads the top-level import and re-export specifiers, then destroys the frontend. At the callback those temporary frontends accounted for **590.77 MiB of pool payload and 793.99 MiB of index arrays cumulatively**. The largest single discovery frontend used **15.12 MiB** across those two categories. Concurrent process peak requires a time-series measurement. The parser's two reduction buffers for retained units had a median peak capacity of **512 bytes**, p95 **3 KiB** and maximum **96 KiB** per parse. [Discovery callback](../../lambda/js/js_module_ast_prebuild.cpp) · [Compiler pass sequence](../../lambda/js/js_c_parser.cpp)

Give discovery a reduction sink for the existing grammar that collects only top-level static module requests and their decoded specifiers. Reuse the normal parser's handling of comments, strings, escapes, templates and automatic semicolon insertion. Keep dynamic `import()`, `import.meta` and nested expressions out of the static dependency list. Preserve the existing profile/mode, resolution identity, failure behavior and execution-time validation. If a validated template already exists under the exact cache key, its module requests can serve discovery under its lease. Preserve the bounded worker and dependency-future protocol in D8.3.5 and D8.5.1. Compare the ordered requests and resulting module futures with the current path, including malformed modules, cycles, re-exports and TS inputs. Give this change zero credit toward either retained-memory target. [Prebuild scheduler](../../lambda/runtime/module_ast_prebuild.cpp) · [Formal compilation and cache rulings](../../doc/Lambda_Formal_Design.md#d83-lambda-execution-tiers)

## Implementation result

| Retained measure | Before | After | Change |
|---|---:|---:|---:|
| `js.ast` live payload | 728.85 MiB | 686.95 MiB | −5.75% |
| Structural index columns | 425.09 MiB | 283.39 MiB | −33.33% |
| Pointer-to-ID hash slots | 282.54 MiB | 94.18 MiB | −66.67% |
| **All index arrays measured** | **808.10 MiB** | **478.05 MiB** | **−40.84%** |
| **Pools plus index arrays** | **1,536.95 MiB** | **1,164.99 MiB** | **−24.20%** |
| Median JS-on process footprint | 71.62 MiB | 67.01 MiB | −6.44% |
| p95 JS-on process footprint | 105.78 MiB | 96.75 MiB | −8.54% |

The candidate retained **1,221,583,584 bytes**, **67,701,292 bytes below** the combined target. It matched 174 realms, 3,867 AST pools, 4,764 indexes, 9,106,763 indexed nodes, and the previous reverse-query payload exactly. All 224 JS-off layouts and 221 JS-on layouts matched after removing timestamps. The three JS-on differences came from the same wall-clock templates as in the baseline. Per-page counts of recognized script exceptions matched. The declaration maps, profile-support bytes and source copies are outside the measured index and pool subtotal.

The index keeps dense parent IDs and ID-only pointer lookup slots. It derives closure-flow adjacency from the current parent edges inside the one Lambda pass that consumes it, then frees both arrays at pass exit. This preserves D8.2.4 when fragments append or clones rebuild. Binding and declarator records use typed Lambda extensions. Arm64 `NameEntry` changed **144 → 80 bytes** and `AstDeclaratorNode` **88 → 64 bytes**. `NameScope` changed **96 → 72 bytes**, with a 32-byte declaration record allocated for nonempty scopes. The inline first binding keeps ordered reads on the original pointer path. The layout changes account for **43,933,872 bytes** of the observed **43,934,064-byte** pool reduction. [Index representation](../../lambda/runtime/ast-core.hpp) · [Closure-flow scratch](../../lambda/runtime/build_ast.cpp)

The pool-only target still requires **103.87 MiB**. The original identifier and header estimates total **98.96 MiB**. Closing the rest of the gap would require further scope savings or another measured pool cut. The first-binding pointer and Lambda scope fields currently remain inline. A typed split of the two Lambda-only scope pointers would be worth up to **8.21 MiB** at the observed scope count, subject to ownership and performance checks.

Static import discovery preserves the existing parser mode test. Its classic-script branch returns an empty dependency set without creating a compiler frontend. For modules it runs parse, bind and early-error validation with the same compiler pass manager, then reads the requests before indexing. The release replay counted **1,179 classic-script scans and zero module discovery scans** in the 224 pages, with zero frontend pool or index bytes on those scans. The JS suite covers real static imports, cycles and re-exports. The earlier 1,384.76 MiB cumulative observation measured complete discarded frontends across all scans. It is a payload sum across work items. [Discovery implementation](../../lambda/js/js_module_ast_prebuild.cpp) · [Pass sequence](../../lambda/js/js_c_parser.cpp)

The release JS script suite passed **277 tests**. The direct C parser suite passed **22 tests**, and the JS/TS parser differential matched **620 files**. Test262 passed **40,261 of 40,261** admitted cases with zero regressions. The paired Lambda interpreter runs each passed **1,605 of 1,612** cases. Both had the same seven failures in the staged harness across trace configuration and local socket fixtures. The candidate used Input `3c5d8fc` and Radiant `95f4b4f` in isolated roots because the checked-out Radiant pin omits the pending-load resource API required by the host adapter. The prior indexed reference used Input `b35af04` and Radiant `161ad05`. Index counts and layout comparisons above state the observed parity across those builds. Candidate diagnostics and replay logs are under `temp/js-ast-next/impl/`.

### Release performance and repeated loads

The timing control rebuilt Lambda `7003ce6019` with the same isolated Input and Radiant revisions as the candidate. Its six-page pilot reproduced the original pool sizes and layouts. The timing replay alternated baseline and candidate execution order with census capture disabled. Across the full 224-page corpus, median CLI time was **277.76 → 289.76 ms (+4.32%)**, the p95 was **1,038.32 → 1,047.35 ms (+0.87%)**, and the median paired change was **−0.11%**. The 174 JS-active pages had a median CLI change of **+0.50%** and a median paired parse change of **−4.43%**.

A seven-repeat follow-up on seven pages, including Reveal, produced an aggregate median of **294.22 → 295.30 ms (+0.37%)** and a median paired change of **+1.18%**. Across 14 paired repeats of Reveal, the medians were **1,276.82 → 1,316.86 ms (+3.14%)** and the median paired change was **+1.48%**. Reveal's median paired parse change was **−4.71%** and its execution change was **+1.12%**. The procedure measures the entire layout CLI, including startup and serialization. [Timing script and local evidence](../../temp/js-ast-next/impl/perf.py)

Eleven paired runs on each of three standalone jQuery and Bootstrap source samples recorded parse-build **1.42–1.97% faster**, binding **1.20–3.81% faster**, and indexing **13.18–18.85% faster** by median paired time. Each library sat inside an uncalled function, allowing compilation independently of browser globals. The phase measurement covers compilation of those samples. The JS suite exercised T0 and hot MIR behavior. Dedicated steady-state throughput was outside these timing runs.

The same-process layout batch alternated 3-col Portfolio and Reveal for up to 20 loads. Each build held 24 AST pools at the final post-layout capture after either two or 20 loads. Baseline live pool bytes were **17,036,765** at both points, and candidate live bytes were **16,027,349** at both points. The batch uses the shared loader runtime. It measures the final loaded page with warmed caches.

## Measurement and acceptance

- **Memory.** Use the same 224 matched JS-on/off pairs with the realm alive at first layout. Require at most **611,400,336 bytes** for the original pool measure and **1,289,284,876 bytes** for the measured pool-plus-index subtotal. Include replacement allocations and charge newly introduced metadata, wherever allocated. Extend the census to declaration hash maps, profile-support buffers, source owners and overlays on both builds, and report the percentage on that broader denominator as well. Count each shared backing allocation once. Report per-page median/p95 and the maximum, plus committed bytes, process footprint and peak RSS. Sample parse/build, concurrent discovery, post-layout, teardown and repeated navigation under D5.2.4.
- **Behavior.** Compare indexed graph queries and order, script outcomes, source spans and normalized layouts with a fixed script-visible clock. Exercise AST, AUTO and MIR, including eval, constructors, closures, imports, tagged literals, optional/private/computed keys and promotion. Include incremental append and cache-clone independence, TS annotations, Lambda in-place node morphs and REPL fragments. Test the 65,534/65,535-byte span boundary, large offsets, a folded node with a sibling, failed expansion and synthetic nodes under sanitizers. Run the direct parser differential and Test262 baseline after each shared-representation milestone. D8.2.4–D8.2.5 and D8.4.1–D8.4.6 govern retained identities and execution. [Formal AST rules](../../doc/Lambda_Formal_Design.md#d82-the-unified-ast) · [Formal JS tier rules](../../doc/Lambda_Formal_Design.md#d84-lambdajs-execution-tiers)
- **Performance and sequencing.** Benchmark uninstrumented release binaries in alternating order. Measure parse, bind, index, import discovery, first layout, T0 identifier reads and hot MIR promotion separately. Repeat on jQuery, Bootstrap, Reveal, a small script and a page with scripting inactive. Keep the prior investigation thresholds of a repeatable **2% median or 5% p95 slowdown**. Land and measure independent index changes first, then bindings, scopes, identifiers and declarators. Recompute the pool shortfall before changing the common header. With both milestones as modeled, the measured combined subtotal falls by **489.22 MiB (31.83%)**. Qualify discovery independently against peak memory and phase time.

**Local reproduction.** The diagnostic sources were generated from Lambda `7003ce6019` on `master`, linked against the round-one arm64 `-O3` full-LTO release archives using compatible Input `b35af04` and Radiant `161ad05`. Temporary instrumentation and both 224-page JS-on replays are under `temp/js-ast-next/`. The manifest is `temp/dom-tree-census/manifest.json`. The comparison uses the previous JS-off controls, all of which had zero JS pools. Run `python3 temp/js-ast-next/analyze-refined.py` for the final owner and index counts and `python3 temp/js-ast-next/discovery.py` for discovery and identifier counts. Both replays measured the same index subtotal. The diagnostic builds were used for attribution only.
