# Lambda source-to-interpreter tuning plan

Keep the syntax, resolved-AST and validated-AST boundaries. The parser already builds the retained tree. The largest measured parsing waste is recognizing the same element content repeatedly. The clearest memory opportunities are in the index arrays and the second set of binding objects. A release prototype reduced parse time on the HTML report from 44.06 ms to 15.99 ms by changing keyword lookup alone. The index and binding changes can proceed independently.

## Current checkpoint

- The 2026-11-20 follow-up targets a further 20% reduction in root AST resolution time on the five modules in §9 and a smaller retained tree (D8.1.1, D8.2.4–D8.2.5).
- The worktree is Lambda-opus `master` at `cac5060e5` on arm64 macOS 26.5.2. The release comparison uses the same Input and Radiant archives as §11. A link against the checkout's older owners exposed mismatched CSS and DOM interfaces.
- The final release comparison reduces resolution by 11.8% and resolution through validation by 18.5% across the five scripts (D8.2.4–D8.2.5). The final bound trees use 14.2–19.5% fewer active arena bytes and have 15.6–22.6% fewer nodes (D5.2.5).
- The final build matches 5,536 AST dumps and diagnostics, passes 491 C syntax cases and matches 72 execution comparisons. The push snapshot fixture matches the golden on T0, JIT and AUTO after the ownership-walker correction (S9.3.1).
- The Lambda baseline and loopback reruns cover 6,117 of 6,119 cases. The remaining two golden mismatches reproduce on the control. The final Test262 run passes 40,261 of 40,261 without retries. §12 records the remaining work toward 20%.
- The prior under-10-ms debug qualification is recorded in §11. This round's raw evidence is under `temp/lambda_ast_compact/`.

The governing rulings cover direct parsing (D8.1.1), the indexed AST and compiler facts (D8.2.4–D8.2.5), tier selection (D8.3.1v3), and forward type visibility (S11.4.12v2). [Formal design](../../doc/Lambda_Formal_Design.md), [formal semantics](../../doc/Lambda_Formal_Semantics.md)

## 1. Pipeline at the baseline revision

| Step | Work and output | Source entry points |
|---|---|---|
| 1. Acquire source and choose the load path | The runner opens a script-cache scope, obtains exact source bytes, looks up an existing runtime script or reusable artifact, and installs a loading shell on a miss. The fresh script owns a source copy. With AST caching and prebuild enabled, root discovery can run concurrently. Its own C parser sink extracts import specifiers. Workers can discover and then build dependencies. The importing runtime waits for the specific module it needs. | [run_script_mir](../../lambda/runtime/script_run.cpp), [load_script and the discovery sink](../../lambda/runtime/runner.cpp), [prebuild scheduler](../../lambda/runtime/module_ast_prebuild.cpp) |
| 2. Lex and recognize syntax | The C lexer streams tokens with half-open byte spans. The parser keeps current and next significant tokens, with line breaks represented by `nl_before`. Recursive descent handles declarations, statements and delimiters. Pratt parsing handles expression precedence. Ambiguous productions copy parser state and probe with the sink, metrics and errors disabled. A syntax failure can invoke a separate bounded recovery parse for diagnostics. | [lexer](../../lambda/runtime/parser/lambda_lexer.c), [lambda_rd_parse_source and parser_probe](../../lambda/runtime/parser/lambda_parser.c) |
| 3. Allocate syntax nodes during reductions | The production callback `syntax_sink_reduce` allocates pool-backed AST nodes and connects children as productions complete. `LambdaSyntaxUnit` holds the root, forward declarations and ordered attachments. Some nodes carry temporary tail slots. Type slots can invoke a dedicated type-pattern parser over their byte span. There is one retained AST and no production Tree-sitter CST. Steps 2 and 3 together are the **parse** timer (D8.1.1). | [lambda_rd_parse_syntax and syntax_type_slot](../../lambda/runtime/build_ast.cpp), [type-pattern syntax](../../lambda/runtime/parse_type_pattern.cpp) |
| 4. Resolve and complete the semantic AST | `resolver_predeclare` installs forward-visible function and type declarations. `resolve_walk` enters lexical scopes in production order, resolves identifiers and imports, builds types, shapes and constants, and completes expressions in place. Function exit already performs some cross-frame and copy-on-write analysis. Alias completion and deferred unresolved-name checks follow the walk. The syntax-unit lists are destroyed. This is **ast_build**, also called **resolve** below. Forward type visibility and diagnostic order constrain fusion (S11.4.12v2). | [lambda_rd_resolve_syntax and resolver_function_end](../../lambda/runtime/build_ast.cpp) |
| 5. Bind and publish the indexed graph | The binder clones scopes and entries into the script pool, rewrites edges to their canonical identities and traverses reachable nodes in structural preorder. It publishes parent, owning function, binding, subtree and derived query information. Allocation called `ast_index_note_allocation` earlier to reserve capacity. Dense IDs are actually assigned during binding. **Bind includes indexing**. The standalone index timer is zero by construction (D8.2.4–D8.2.5). | [direct_bind_clone_scope and direct_bind_rewrite_node](../../lambda/runtime/build_ast.cpp), [ast_index_note_allocation, ast_index_bind_node and ast_index_mark_published](../../lambda/runtime/ast-core.cpp) |
| 6. Validate and finish facts | The validator uses the published graph for captures and permissions, callback and generative effects, outcome propagation, method-value restrictions, call colour, enforcing calls, cross-frame reads, COW borrows and concurrency analysis. Some checks traverse bodies. Others require child-first order or a fixed point across definitions. **Validated** is a distinct fact the next stage may rely on. | [lambda_ast_finalize_script_with_functions and complete_outcome_facts](../../lambda/runtime/build_ast.cpp) |
| 7. Admit and plan T0 | The runner collects direct imports, scans support using the published index and checks that imports can run. The planner registers named patterns, assigns module and function slots and computes scratch requirements. The script adopts the completed transpiler. Forced `interp` rejects unsupported work. AUTO has a MIR fallback (D8.3.1v3). | [lambda_prepare_ast_interpreter and transpile_script](../../lambda/runtime/runner.cpp), [interp_scan_supported and interp_plan_script](../../lambda/runtime/interp_plan.cpp) |
| 8. Prepare the execution context | The caller finishes script loading and sets up the runner context. On macOS and other non-Windows builds, `interp_script_needs_large_stack` walks the tree again to select ordinary or worker-stack entry. The next action is `interp_run_script`, directly or on that worker. | [runner_setup_context](../../lambda/runtime/runner.cpp), [stack selection and T0 entry](../../lambda/runtime/script_run.cpp) |

Cache hits can skip source compilation and activate a template with runtime-owned state (D8.3.5, D8.5.1). A REPL entry instead extends a retained module and must roll back a rejected fragment (D8.3.6). Both paths matter when changing identities, source ownership or derived facts.

## 2. Release profile

The table reports independent medians in **milliseconds**, with the script cache off, import prebuild off and `LAMBDA_EXEC_BACKEND=interp`. Each sample uses a fresh process with warm filesystem caches. **Own** sums the root's compiler phases within each sample. **Context ready** measures from immediately before `load_script` through successful `runner_setup_context`. It includes dependency loading, script publication and context setup. The later stack-selection scan is measured separately below. Program execution, formatting and CLI startup are outside both intervals.

| Root fixture | Bytes | Parse | Resolve | Bind | Validate | T0 plan | Own | Context ready | Peak RSS MiB¹ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| [single](../../test/lambda/single.ls) | 3 | 0.014 | 0.007 | 0.006 | 0.007 | 0.003 | 0.035 | 0.194 | 11.52 |
| [closure](../../test/lambda/closure.ls) | 2,112 | 0.110 | 0.447 | 0.045 | 0.033 | 0.015 | 0.653 | 0.811 | 12.94 |
| [cd2](../../test/benchmark/awfy/cd2.ls) | 18,776 | 0.826 | 1.232 | 0.405 | 0.551 | 0.117 | 3.140 | 3.444 | 14.55 |
| [prettier_ast2](../../test/benchmark/text/prettier_ast2.ls) | 19,015 | 0.714 | 1.168 | 0.340 | 0.439 | 0.115 | 2.771 | 3.024 | 14.30 |
| [type_pattern](../../test/lambda/type_pattern.ls) | 27,358 | 0.668 | 0.642 | 0.219 | 0.119 | 0.068 | 1.715 | 1.891 | 14.19 |
| [deltablue2](../../test/benchmark/awfy/deltablue2.ls) | 28,869 | 1.321 | 1.817 | 0.644 | 0.984 | 0.195 | 4.948 | 5.460 | 15.41 |
| [json2](../../test/benchmark/awfy/json2.ls) | 43,016 | 0.682 | 1.138 | 0.299 | 0.379 | 0.123 | 2.622 | 2.904 | 14.42 |
| [commands_basic](../../test/lambda/editor/commands_basic.ls) | 44,328 | 1.123 | 1.558 | 0.554 | 0.515 | 0.182 | 3.926 | 33.285 | 28.64 |
| [complex_iot_report_html](../../test/lambda/complex_iot_report_html.ls) | 48,239 | 43.640 | 1.100 | 0.349 | 0.258 | 0.102 | 45.433 | 45.611 | 14.44 |
| [oracle_poc](../../test/lambda/editor/oracle_poc.ls) | 225,630 | 5.928 | 3.918 | 3.272 | 3.288 | 0.872 | 17.242 | 46.729 | 39.97 |

¹ Process high-water RSS sampled after planning. It includes the runtime, mapped or resident code, dependencies and compiler allocations. The module pool measurements below identify a narrower owner. The timer named `analysis_us` covers selected work inside resolve and must not be added again. `plan_us` is present internally and is recovered exactly from the T0 total minus the other integer phase fields in CLI schema 1. The legacy `utils/compile_phase_bench.sh` reads format-3 fields 4/5 as plan/transpile, although they now hold build-resolve/inline-analysis. Use named fields.

Three different costs warrant different fixes.

- **Nested elements.** The 48,239-byte HTML report spends 43.64 ms parsing. A separate counter build of the same C grammar counted 6,000 ordinary lexer calls and 588,019 speculative calls. The probes traversed 5,638,192 bytes. Of those calls, 517,385 came from the parser copy in `element_content_starts_sibling` alone. It parses a candidate prefix and postfix to decide whether another `<` follows. The committed path then parses that child again. Nested elements repeat the decision inside the probe. For comparison, the 225,630-byte editor oracle recorded 58,413 ordinary calls and only 829 speculative calls. Counts attribute a lexer call to the innermost probe and include newline/EOF reads. [Element recognition](../../lambda/runtime/parser/lambda_parser.c)
- **Semantic construction and validation.** Resolve is the largest individual stage on several single-module scripts. Validation has different costs by workload. In three separately instrumented runs, `complete_outcome_facts` took a median 2.091 ms on the editor oracle. It scans calls to convergence, revisits the script for changed calls, refreshes loops and runs a final propagation loop. COW analysis led on DeltaBlue at 0.445 ms. A single fused validator would need to preserve both dependencies. [Semantic passes](../../lambda/runtime/build_ast.cpp)
- **Imports.** The editor commands root takes 3.93 ms of its own compiler time and 33.29 ms through context setup. With default cache and prebuild settings it took 32.26 ms through setup, versus 44.55 ms for the oracle under those defaults (three fresh processes each). Those runs change two settings together. Use import-graph wall time, worker CPU and cache-hit cohorts to judge discovery changes. Root timings alone hide most of the cost on commands. [Loader timing and worker waits](../../lambda/runtime/runner.cpp)

## 3. Where the bytes go

Requested **live bytes in the root script's pool** at successful phase boundaries, in KiB. These are observed pool counters from separate detail runs. Index arrays allocated through libc, list backing storage, raw source, dependency pools and process overhead fall outside this column. The root arena reported zero active bytes and a 4 KiB chunk capacity throughout these fixtures.

| Fixture | After syntax | After resolve | After bind | After validate | After T0 plan | Indexed nodes / capacity |
|---|---:|---:|---:|---:|---:|---:|
| Single literal | 0.5 | 0.7 | 0.8 | 0.8 | 0.8 | 2 / 256 |
| JSON parser | 222.7 | 387.6 | 449.6 | 451.2 | 489.4 | 2,627 / 4,096 |
| DeltaBlue | 465.3 | 651.4 | 757.5 | 774.0 | 829.4 | 5,900 / 8,192 |
| HTML report | 329.7 | 524.1 | 566.5 | 566.9 | 566.9 | 3,889 / 4,096 |
| Editor commands | 589.7 | 755.4 | 825.5 | 825.6 | 825.7 | 7,842 / 8,192 |
| Editor oracle | 3,458.6 | 4,576.3 | 5,051.1 | 5,051.4 | 5,051.9 | 45,034 / 65,536 |

The oracle's bind phase adds **486,168 pool bytes**. Cloning scopes, bindings and their name indexes is one contributor. That delta is an upper bound on savings from eliminating those clones in this phase. Separately, `ast_index_reserve` allocates ten arrays with one capacity. On this ABI the scopes, bindings and classes arrays alone reserve **1.50 MiB** at 65,536 slots each. The published oracle uses 11 scopes, 1,989 bindings and zero classes, just 16,000 bytes of pointer payload. This gap measures reserved array capacity. An RSS saving depends on which pages the allocator commits and the process touches. A two-node literal also starts with 256 slots in each of the ten arrays. [Index allocation](../../lambda/runtime/ast-core.cpp), [pool counters](../../lambda-input/lib/mempool.h)

## 4. Measured speed experiment

The current lexer tries keyword spellings in sequence, then base types, then named values. Ordinary identifiers miss all three lists. The experiment generated a dispatch on length and first byte **from those same tables** and compared the full spelling within the selected bucket. An unknown word still returns an identifier. This is a temporary C experiment. The production design must also keep the table enumeration used by completion and the binding-name predicate consistent (S16.10.1v3). [Vocabulary lookup and enumeration](../../lambda/runtime/parser/lambda_lexer.c)

The A/B below used an independently compiled original-lexer control and the candidate, identical C compiler flags, otherwise identical release probe objects, opposite execution order on alternating pairs, one discarded warm-up pair and nine measured pairs per fixture. Percentages are medians of per-pair reductions in elapsed time. All nine pairs improved parse time on each of the nine nontrivial fixtures. The single literal was at the timer's noise floor.

| Fixture | Control parse ms | Candidate parse ms | Parse reduction | Root compiler reduction | Context-ready reduction |
|---|---:|---:|---:|---:|---:|
| single | 0.014 | 0.014 | within noise | within noise | within noise |
| closure | 0.120 | 0.081 | 32.2% | 5.7% | 4.6% |
| cd2 | 0.840 | 0.422 | 49.5% | 16.2% | 14.7% |
| prettier_ast2 | 0.726 | 0.389 | 46.5% | 12.8% | 11.9% |
| type_pattern | 0.669 | 0.402 | 39.8% | 16.6% | 15.0% |
| deltablue2 | 1.339 | 0.618 | 53.8% | 16.6% | 15.2% |
| json2 | 0.699 | 0.385 | 44.7% | 14.0% | 12.7% |
| commands_basic | 1.138 | 0.644 | 43.4% | 13.4% | 17.9% |
| complex_iot_report_html | 44.063 | 15.985 | 63.8% | 61.3% | 61.0% |
| oracle_poc | 5.973 | 2.951 | 50.7% | 20.2% | 19.1% |

The stock and candidate syntax-only parsers agreed on all **5,536 tracked Lambda files** in the parent repository. The comparison included status (5,472 accepted, 3 incomplete, 61 errors), token and reduction counts, maximum depth, structural fingerprint, and the location, expected tokens and message of each reported syntax error. Stock release, baseline probe and lookup probe also produced byte-identical canonical AST output and diagnostics for the ten positive fixtures and the rejected `jq_vm.ls` (13 semantic errors, including E200/E201). Only the process RSS telemetry line was removed from diagnostic comparisons. Runtime baselines remain an implementation gate.

## 5. Implementation order and acceptance criteria

The thresholds below are proposed experiment gates. Each phase gets an independent paired comparison against its immediate predecessor.

1. **Ship vocabulary lookup as a small parser-only change.** Generate or derive a compact dispatch from the one vocabulary definition. Keep full byte-wise equality, case sensitivity, completion order and contextual binder rules. Re-run the corpus comparison above and both S16 syntax harnesses. Aim to reproduce at least a 25% parse reduction on JSON, DeltaBlue, the type-pattern fixture and the oracle, with no consistent slowdown on small scripts. Record executable size as well as latency.
2. **Parse an element child once.** Refactor the prefix/postfix part of expression parsing so that `parse_content` can commit that result, inspect the next token, and either finish a sibling child or continue the existing Pratt infix tail from the saved left operand. The current sibling probe runs inside nested probes. Removing this recursion has a larger payoff than merely accelerating the speculative lexer. Preserve nesting, expression and pipe state, line-start classification, the original source span, and exactly one sink callback per committed reduction. Preserve assignment/statement handling and error recovery when the following token does not start a sibling. Pin adjacent elements, postfix on a child, relational `<`, nested/computed elements and failing inputs against their original reductions and diagnostics (S16.2.3v3, S16.7.1, S16.9.3). Require at least 90% fewer speculative lexer reads on the HTML report and repeat the **post-lookup** time comparison. Inspect remaining probes only if still material. [Content decision and expression parser](../../lambda/runtime/parser/lambda_parser.c)
3. **Right-size the AST index.** Give scope, binding and class pointer arrays capacities driven by their own counts. Delay the first allocation for empty columns. Choose the initial node capacity from small-unit measurements. Atomically publish grown buffers after successful allocation and preserve dense preorder, subtree intervals and declaration IDs. Update clone, append, rebuild, destroy and failure paths together, including JS users of the shared index (D8.2.4). Instrument requested and peak bytes for the ten arrays, pointer hash, derived queries and growth copies. Aim to save at least 1 MiB of requested index capacity on the oracle and reduce the two-node fixed cost, with indistinguishable bind times. [Storage and index cloning](../../lambda/runtime/ast-core.cpp)
4. **Consolidate resolution and binding ownership.** On a freshly compiled, exclusively owned script, let the resolver create canonical scopes/entries. Retain one structural bind walk to publish preorder IDs, owner and query columns after resolution. This removes the candidate duplicate scope graph and identity rewrites while keeping the fact boundary in D8.2.5. Inventory every outgoing edge first, including annotation binders, captures, object shape fields, imported entries and links between entries. Keep immutable templates, import overlays and REPL transactions owner-correct (D8.3.5–D8.3.6, D8.5.1). Independently remove the redundant construction of two function lists on the published-index path after comparing membership, method exclusions and order. Gate on binding/owner/capture queries, repeated cache activation in two runtimes, REPL rollback and T0/JIT parity. Measure fresh and retained pool bytes and bind time independently. A first target is half the oracle's 475 KiB bind-phase pool growth. [Scope copying and function collection](../../lambda/runtime/build_ast.cpp)
5. **Separate short-lived builder storage from the script pool.** Start with byte counts for `LambdaSyntaxAttachment`, grown `LambdaSyntaxParts` buffers, `UnresolvedName` records, and syntax-only pointer tails. Today the unit frees its list containers while pool-allocated payloads can live for the script lifetime. Use a per-unit scratch arena for records whose last reader is resolve. Move tails only after auditing late readers, especially lazy aliases and REPL fragments. Ensure errors and nested imports release only their own scratch and no published AST points into it. Preserve diagnostic/source order for attachments. Check both steady pool bytes at entry and peak live memory while scratch and retained data coexist. Report bytes reclaimed by type before considering any per-node layout change. [Syntax allocation and destruction](../../lambda/runtime/build_ast.cpp)
6. **Reuse validated facts across walks.** On the oracle, profile iteration counts and changed-call counts in `complete_outcome_facts`. Use the index's call and binding relations to process affected sites and propagate changes back to their owners until the same fixed point is reached. Keep a reference comparison for per-node outcomes, including recursion, loop back-edges and forward declarations. On DeltaBlue, profile the COW analysis separately (S9.1.2). Check whether `validate_cross_frame_binding_reads` at function close and its later validation call have equivalent inputs, function coverage and diagnostic order before coalescing either invocation. Finally, fold the last stack-selection scan into an existing traversal only if its bounded function/call-name collection order and handler result are identical. In the separate release probe this scan cost 0.339 ms on the oracle, 0.044 ms on DeltaBlue and below resolution on the single literal. The oracle and DeltaBlue selected the worker stack. Worker launch and interpreter execution were excluded (S7.4.3). [Validation](../../lambda/runtime/build_ast.cpp), [stack selection](../../lambda/runtime/script_run.cpp)
7. **Reduce duplicate import discovery work where it pays.** Count bytes read, discovery-parser invocations, joins on futures, cache hits, worker CPU and wall time through context setup. Compare prebuild on/off while holding cache policy fixed, then repeat within one process on a warm cache. On misses, trial publishing import specifiers from committed reductions or sharing the immutable source snapshot and import list with the prebuild scheduler. Keep early scheduling of sibling imports, keyed futures, canonical identity, security I/O authorization and cache invalidation. The source lease must outlive every span consumer if it replaces the script's current source copy. Adopt only if import-heavy startup improves and the warm path stays cheap (D8.3.5, D8.5.1). [Discovery and load](../../lambda/runtime/runner.cpp), [scheduler](../../lambda/runtime/module_ast_prebuild.cpp)

Two lower-priority experiments are bounded by their consumers. Type annotations currently have both a main-grammar span recognizer and a dedicated type-pattern parser. Count those bytes before attempting to share a token cursor. For reduction hashing, `parser_reduce_detail_ex` computes a fallback fingerprint before invoking the sink. Trial lazy hashing only when a nonzero sink result supersedes it. Preserve zero-return fallback, sink-less fingerprints and metrics. Neither change needs a new persistent token buffer. [Type slots](../../lambda/runtime/build_ast.cpp), [reduction contract](../../lambda/runtime/parser/lambda_parser.c)

## 6. Reproduction and verification

- The stock binary was built with the normal split release configuration (`-O3`, full LTO, Apple clang 21.0.0). The pinned input and Radiant revisions are `c2ac909a3` and `26b376205`. The executables and raw samples are local investigation artifacts under [temp/lambda_frontend_profile](../../temp/lambda_frontend_profile/). The tables above preserve the baseline if that directory is cleaned.
- The baseline table uses the original lexer and release timer boundaries in `transpile_script`. A temporary copy of `script_run.cpp` timestamps load through context setup and returns before user code. Memory and validator probes are gated off during timing runs. A separate copy runs and times the production stack selector before returning. All timed runs reported zero MIR lowering and stopped before module evaluation. In the ordinary CLI, `--dry-run` governs I/O and still enters the interpreter. For a stock timing spot check use the following in the main checkout.

```sh
mkdir -p temp/lambda_frontend_profile/tmp
TMPDIR="$PWD/temp/lambda_frontend_profile/tmp" make build-lambda-split \
  SPLIT_PROFILE=release SPLIT_OUTPUT=temp/lambda_frontend_profile/lambda-release
LAMBDA_HOME="$PWD/lmd" LAMBDA_COMPILER_TIMING=1 LAMBDA_EXEC_BACKEND=interp \
  LAMBDA_SCRIPT_CACHE=off LAMBDA_MODULE_AST_PREBUILD=0 \
  temp/lambda_frontend_profile/lambda-release --no-log test/benchmark/awfy/cd2.ls
```

- The [binary and fixture SHA-256 manifest](../../temp/lambda_frontend_profile/provenance.json) identifies the measured inputs. The per-sample files cover [baseline and first A/B](../../temp/lambda_frontend_profile/timing.jsonl), [matched-flag A/B](../../temp/lambda_frontend_profile/flag-matched.jsonl), [default cache/prebuild](../../temp/lambda_frontend_profile/default.jsonl), [phase memory and validation substeps](../../temp/lambda_frontend_profile/detail.jsonl), [stack scan](../../temp/lambda_frontend_profile/scan.jsonl), [per-site lexer counts](../../temp/lambda_frontend_profile/lookahead.jsonl), [parser parity](../../temp/lambda_frontend_profile/parser-parity.json), [AST and diagnostic hashes](../../temp/lambda_frontend_profile/ast-checked.json). The fixture manifest, environment and collection loop are in [measure.py](../../temp/lambda_frontend_profile/measure.py). Each timing series used one discarded warm-up and nine measured fresh processes, except the separately marked default-policy and detail runs, which used three measured processes. All successful samples were kept, including a DeltaBlue baseline parse sample of 37.576 ms against a 1.321 ms median.
- For each implementation slice, compare the same file bytes with interleaved release binaries. Report absolute time, per-pair deltas, medians and spread for each fixture. Compare root compiler time, import-inclusive wall time, script-pool high water, retained bytes, allocated index capacity and process RSS separately. Use a blank or single-literal script to bound fixed costs. Repeat close results on an idle machine and treat changes within observed variation as inconclusive.
- For correctness, compare parser status, fingerprints and error spans over tracked scripts, run `test/c_s16_conformance.sh` and `test/ts_s16_conformance.sh`, then compare canonical AST dumps, including negative diagnostics. For binder, index, validation and stack-plan changes, also run forced T0 and eager JIT goldens, REPL rollback and cache/ownership tests, followed by `make test-lambda-baseline` and `make test262-baseline`. Keep focused and aggregate outcomes separate. The full baselines were not run for this plan.

## 7. Implementation at e126b4b89

| Change | Implementation |
|---|---|
| Word lookup | One C vocabulary supplies completion order and fixed-length lookup arms. Constant-size comparisons let the compiler eliminate unrelated vocabulary entries (S16.10.1v3). |
| Element recognition | Nested content parses the prefix and postfix once, then decides between a sibling and the Pratt infix tail. When a top-level relation or active pipe can change the result, the original lookahead still chooses the path (D8.1.1, S16.2.3v3, S16.7.1). |
| Reduction fingerprints | The committed sink's value replaces the fingerprint. Syntax-only parses and sinks that decline a reduction still compute the original hash. |
| Indexed storage | The scope, binding and class columns grow independently. The initial node and pointer-table capacity is 32. Bind resolves the structural parent before publishing the node and its declaration edges (D8.2.4). |
| Binding ownership | The compiler indexes the scopes and entries created by resolution. Imports already introduce local binding entries, while provider ASTs and cache overlays retain their existing owners. Validation gets one function list from the published index (D8.2.4–D8.2.5, D8.3.5). |
| Temporary syntax storage | A lazy arena owned by the syntax unit holds attachments, ordered clause scratch and unresolved-name records. Resolution clears clause-tail pointers it consumes. Releasing the unit also releases the scratch, including on parse failure. |
| Outcome propagation | Calls with a signature that has no `var` parameters skip borrow-argument mapping. The propagation order and fixed point stay intact (S9.2.1). |

On the HTML report, speculative lexer calls fell from **588,019 to 42,463** (92.8%) while committed lexer calls remained at 6,000. The editor oracle's AST pool at the T0 plan boundary fell from **5,173,130 to 4,686,674 bytes**. Its bind phase now adds zero requested pool bytes, down from 486,168. The oracle's three declaration columns dropped from **1,572,864 to 16,512 bytes** of requested capacity. These are distinct allocations. Together they save 1.95 MiB in the two measured memory owners. For the single-literal fixture, the seven node columns, three declaration columns and pointer hash requested 18,432 bytes before the change and request 1,600 bytes after it, excluding allocator and derived query-table costs. [Lexer site counts](../../temp/lambda_frontend_impl/lookahead.jsonl), [pool samples](../../temp/lambda_frontend_impl/final-memory.jsonl)

The import scheduler and full outcome worklist remain separate experiments. The discovery sink already receives the cheaper lexer and skips redundant fingerprints. With cache policy held at `all`, prebuild performed a median 11 discovery parses on each editor fixture. Across nine pairs it reduced context-ready time from 26.39 to 23.27 ms for editor commands and from 36.30 to 31.88 ms for the oracle. The import-free HTML report performed one discovery parse and took 3.48 ms with prebuild off and 3.59 ms with it on. These results support retaining the scheduling overlap while measuring any proposal to reuse the parse. A rewrite needs its own candidate and warm-cache verification under D8.3.5. [Discovery measurements](../../temp/lambda_frontend_impl/final-discovery.jsonl)


### Paired release timings

Medians in milliseconds from nine fresh-process pairs per fixture on the same machine as the baseline, with cache and prebuild off. Both probes stop at context setup. All nine pairs reduced parse and bind time for each nontrivial fixture. The single literal's context-ready median increased by 7 µs.

| Fixture | Parse before → after | Bind before → after | Root compiler before → after | Context ready before → after |
|---|---:|---:|---:|---:|
| single | 0.014 → 0.015 | 0.006 → 0.005 | 0.036 → 0.035 | 0.199 → 0.206 |
| closure | 0.111 → 0.078 | 0.044 → 0.033 | 0.651 → 0.606 | 0.818 → 0.786 |
| cd2 | 0.832 → 0.420 | 0.410 → 0.295 | 3.147 → 2.555 | 3.481 → 2.888 |
| prettier_ast2 | 0.722 → 0.391 | 0.340 → 0.264 | 2.786 → 2.349 | 3.020 → 2.618 |
| type_pattern | 0.668 → 0.401 | 0.217 → 0.174 | 1.710 → 1.366 | 1.883 → 1.542 |
| deltablue2 | 1.310 → 0.605 | 0.625 → 0.465 | 4.938 → 4.030 | 5.456 → 4.558 |
| json2 | 0.686 → 0.381 | 0.304 → 0.226 | 2.643 → 2.213 | 2.936 → 2.510 |
| commands_basic | 1.151 → 0.642 | 0.557 → 0.452 | 3.980 → 3.380 | 33.630 → 26.682 |
| complex_iot_report_html | 43.972 → 1.654 | 0.356 → 0.275 | 45.826 → 3.324 | 46.038 → 3.528 |
| oracle_poc | 5.967 → 2.911 | 3.302 → 2.565 | 17.561 → 13.130 | 47.301 → 36.428 |

[Raw release pairs](../../temp/lambda_frontend_impl/final-timing.jsonl), [memory samples](../../temp/lambda_frontend_impl/final-memory.jsonl), [measurement script](../../temp/lambda_frontend_impl/measure_final.py), [release binary hashes](../../temp/lambda_frontend_impl/final-provenance.json)

### Qualification

- Production parser accepted, rejected and recovered identically across 5,536 tracked scripts. An additional 2,100 combinations of nested elements, postfixes, relational operators, pipes and invalid delimiters matched the baseline. [Comparison driver](../../temp/lambda_frontend_impl/check_nested.py)
- S16 passed 491/491 for the C parser and 478/478 for the regenerated Tree-sitter reference. [C parser](../../temp/lambda_frontend_impl/c-s16-final.log), [reference](../../temp/lambda_frontend_impl/ts-s16-final.log)
- The final release binary produced byte-identical canonical AST output, diagnostic text and exit status across 5,536 tracked scripts after removing volatile RSS telemetry. Twelve program fixtures matched across T0, AUTO and JIT. [Canonical comparison](../../temp/lambda_frontend_impl/final-ast.log), [tier comparison](../../temp/lambda_frontend_impl/runtime-compare.log)
- The sandboxed Lambda baseline passed 6,102/6,119. The focused reruns passed 13/13 runtime cases and 1/1 JS media case. The complete server suite passed 151/151 with loopback access. REPL and script cache also passed in the aggregate at 51/51 and 20/20. [Baseline](../../temp/lambda_frontend_impl/lambda-baseline.log), [runtime reruns](../../temp/lambda_frontend_impl/lambda-failures-allowed.log), [media rerun](../../temp/lambda_frontend_impl/js-failure-allowed.log), [server rerun](../../temp/lambda_frontend_impl/serve-allowed.log)
- `make test262-baseline` passed 40,261/40,261, with zero regressions and no retries. The harness skipped 2,652 cases outside the passing baseline. [Test262 log](../../temp/lambda_frontend_impl/test262-baseline.log)

## 8. Legacy cleanup after the tuning

Binding now allocates the node index when it publishes D8.2.4 facts. The syntax allocator no longer invokes an eager reservation hook. Class IDs remain in the declaration identity map with a dense count. The write-only class pointer array, its clone and growth paths, and two unused index queries were deleted. The AST dump also uses the graph already published by the binding pass instead of building a second graph before validation (D8.2.4–D8.2.5).

The cleanup also deleted a write-only statement-delimiter flag, a write-only function-body syntax bit, two type-pattern forms without producers, two unused source-cursor newline helpers, and an unused compiler pass-notification helper. The live statement separator and diagnostic paths still implement S16.2.3v3 and S16.9.3. [C parser](../../lambda/runtime/parser/lambda_parser.c), [syntax forms](../../lambda/runtime/ast_build.hpp), [index](../../lambda/runtime/ast-core.cpp), [AST dump](../../lambda/runtime/emit_ast_dump.cpp)

The pinned pre-cleanup and post-cleanup release binaries matched across 5,536 parser fingerprints and 5,536 canonical AST/diagnostic results. The C S16 suite passed 491/491 and 36 focused T0, AUTO and JIT runs matched. [Parser parity](../../temp/lambda_frontend_cleanup/parser-parity.json), [canonical AST comparison](../../temp/lambda_frontend_cleanup/ast-corpus.log), [C S16](../../temp/lambda_frontend_cleanup/c-s16.log), [tier comparison](../../temp/lambda_frontend_cleanup/runtime-compare.log)

The final source passed `make test-lambda-baseline` at 6,119/6,119 and the standalone `make test262-baseline` at 40,261/40,261 with zero retries or regressions. The targeted LambdaJS declaration-ID test passed across clone, append and rebind. A ten-file canonical check on the final release build also matched. [Lambda baseline](../../temp/lambda_frontend_cleanup/lambda-baseline-final.log), [Test262](../../temp/lambda_frontend_cleanup/test262-final.log), [declaration-ID test](../../temp/lambda_frontend_cleanup/js-ownership.log), [final release sample](../../temp/lambda_frontend_cleanup/ast-final-sample.log)

## 9. Follow-up on the five slowest large modules

The debug build reaches the 10 ms target on four of the five modules. The baseline is `e7524adec` and both arms use the checkout's ordinary `make build` profile (`DEBUG`, `-O3 -g`) on arm64 macOS. Each arm runs in a fresh process with warm filesystem caches, T0 selected, logging disabled, and script caching and module AST prebuild off. One warm-up pair precedes eleven measured pairs. The combined column takes the median of the per-run sums. Parse includes syntax node construction. Resolve is `ast_build_us`, which subtracts nested module compilation and still includes work performed by the importing resolver. Binding, validation, T0 planning and execution are outside this sum (D8.1.1, D8.2.4–D8.2.5).

| Module | Debug parse before → after (ms) | Debug resolve before → after (ms) | Debug combined before → after (ms) | Debug runs under 10 ms | Release combined before → after (ms) |
|---|---:|---:|---:|---:|---:|
| [expansion.ls](../../lmd/package/doc/latex/tex_engine/expansion.ls) | 5.404 → 5.259 | 37.277 → 9.396 | 42.667 → 14.661 | 0/11 | 41.262 → 13.692 |
| [render.ls](../../lmd/package/doc/latex/render.ls) | 3.275 → 3.199 | 15.180 → 6.373 | 18.476 → 9.561 | 11/11 | 17.836 → 9.119 |
| [digest.ls](../../lmd/package/doc/latex/tex_engine/digest.ls) | 2.862 → 2.811 | 14.428 → 4.899 | 17.294 → 7.726 | 11/11 | 16.619 → 7.264 |
| [commands.ls](../../lmd/package/editor/core/commands.ls) | 2.442 → 2.264 | 7.890 → 3.637 | 10.335 → 5.911 | 11/11 | 9.795 → 5.583 |
| [mod_ui.ls](../../test/demo/word/mod_ui.ls) | 1.529 → 1.515 | 6.548 → 4.354 | 8.075 → 5.861 | 11/11 | 7.699 → 5.600 |

- Resolving a conditional used to compute its diagnostic line before testing the condition's type. A sample of repeated `expansion.ls` loads attributed 502 leaf samples to that scan. Deferring it cut the paired debug median from 42.55 ms to 23.51 ms on its own. Actual resolver diagnostics and raise sites now use a lazily built table of line start byte offsets in the syntax scratch arena. Diagnostic printing finds the first context line once, then advances through the excerpt (S3.2, S7.4.4). [Initial profile](../../temp/lambda_frontend_top5/expansion.sample), [lint-only pairs](../../temp/lambda_frontend_top5/debug-lint-pairs.jsonl)
- Runtime system function registration records unique callable values in the existing name index, including overload collisions with compiler-only definitions. Local identifier resolution reuses its interned name. A resolver prepares built-in module descriptors once per unit and still consults the live registry. These changes reuse the lookup rules in D5.6.2, D9.3.4, S12.3.7 and S17.2.1.
- COW retention stops descending once both facts are set, and a function's parameter result checks reuse one collection of explicit return sites. These computations still run at function close in source order (S9.1.2, S9.1.3v4). Parser probes skip unused fingerprints and classify a statement `if` with one condition probe (D8.1.1, S16.6.8v3).
- `expansion.ls` still needs another 4.7 ms reduction to meet the debug target. Its 345,409 bytes produce 76,837 token reads in the committed parser and 57,107 reductions. The inline function analysis counter reports about 2.4 ms and can include nested imports. The remaining resolve samples span typed resolution and allocations. About half of the parse samples fall in the syntax sink, including name interning and node allocation. Consolidation of source-order function analysis would need to preserve return, capture and COW facts. A separate allocation experiment could compare unit-owned node storage against the current pool while measuring retained bytes and REPL rollback (D5.2.5, D8.2.5, D8.3.6). [Profile after the lookup changes](../../temp/lambda_frontend_top5/expansion-lookups.sample)

[Measurement driver](../../temp/lambda_frontend_top5/measure.py), [debug pairs](../../temp/lambda_frontend_top5/debug-final-pairs.jsonl), [release pairs](../../temp/lambda_frontend_top5/release-pairs.jsonl), [binary hashes](../../temp/lambda_frontend_top5/provenance.txt)

The parser fingerprints, error spans and recovery results matched for 5,536 scripts. The release binary matched canonical AST dumps, diagnostics and exit status across the same corpus. The five profiled modules produced byte-identical program output and diagnostics across the timing arms, apart from process RSS telemetry. The C S16 suite passed 491/491 and the Lambda baseline passed 6,119/6,119. [Parser comparison](../../temp/lambda_frontend_top5/parser-parity.json), [canonical comparison](../../temp/lambda_frontend_top5/ast-corpus.log), [C syntax suite](../../temp/lambda_frontend_top5/c-s16.log), [Lambda baseline](../../temp/lambda_frontend_top5/lambda-baseline.log)

The standalone Test262 baseline passed 40,261/40,261, with zero retries or regressions. The harness skipped 2,652 cases outside the passing baseline. [Test262 baseline](../../temp/lambda_frontend_top5/test262-baseline.log)

## 10. Callable body pass consolidation

The preceding build is the control for this follow-up. The benchmark retains the 10 ms debug target and the scope in §9. Every sample uses the same source bytes, with eleven interleaved fresh-process pairs per script after one warm-up.

| Module | Debug parse before → after (ms) | Debug resolve before → after (ms) | Debug combined before → after (ms) | Debug runs under 10 ms | Release combined before → after (ms) |
|---|---:|---:|---:|---:|---:|
| `expansion.ls` | 5.216 → 5.039 | 9.205 → 8.077 | 14.417 → 13.116 | 0/11 | 13.870 → 12.822 |
| `render.ls` | 3.188 → 3.070 | 6.117 → 5.555 | 9.304 → 8.640 | 11/11 | 9.110 → 8.635 |
| `digest.ls` | 2.795 → 2.654 | 4.839 → 4.161 | 7.634 → 6.831 | 11/11 | 7.411 → 6.705 |
| `commands.ls` | 2.252 → 2.139 | 3.599 → 3.242 | 5.864 → 5.416 | 11/11 | 5.623 → 5.301 |
| `mod_ui.ls` | 1.501 → 1.440 | 4.176 → 3.851 | 5.683 → 5.281 | 11/11 | 5.603 → 5.278 |

- The source-order resolver records whether each function body could contain an explicit return, raise, or propagation. An empty census skips the corresponding body scans. When the return-contract check walks the body, it also collects the return sites needed by parameter alias analysis. These checks retain their diagnostic order and use the established body traversal to establish exact membership (S7.4.2v4, S7.6.3v3, S11.4.2v3, S12.1.2v3).
- Parameter and local alias mutation queries share one walk of the completed body and look up its write targets by binding identity. Range queries such as a loop snapshot use the same visitor with their own interval and can stop at the first matching write (S9.1.2, S9.1.3v4, S9.2.3).
- The qualified-member probes share a bounded spelling helper. Retained nodes use the compilation Input's existing arena when available. The arena and pool share the Input's teardown. The separate allocation trial lowered the debug parse median for `expansion.ls` from 5.218 to 5.006 ms (D5.2.5, D5.6.2, S12.3.3v2). [Allocation trial](../../temp/lambda_ast_passes/arena-debug.jsonl)
- Module predeclaration establishes forward visibility before source-order resolution. Structural binding publishes stable IDs before capture analysis and outcome convergence. Earlier references can depend on completed callable facts, while assertions involving captures or forward callees can require facts published later. The shared traversal removes repeated discovery inside function completion while respecting these dependencies (S11.4.12v2, D8.2.4–D8.2.5).

The first pass-only candidate reduced `expansion.ls` from 14.822 to 14.008 ms. The final candidate is still 3.116 ms above target. Its inline function-analysis counter fell from 2.409 to 1.657 ms, so eliminating every remaining operation in that counter would still leave a gap. The remaining time spans parsing and typed resolution, including lookup, call checking, and allocation. [Pass-only pairs](../../temp/lambda_ast_passes/fusion-debug.jsonl), [final debug pairs](../../temp/lambda_ast_passes/paired-debug.jsonl), [control profile](../../temp/lambda_ast_passes/expansion-control.sample)

The matched release comparison also reduced combined time on all five modules. Program output and diagnostic text matched within each pair. [Release pairs](../../temp/lambda_ast_passes/paired-release.jsonl), [binary hashes](../../temp/lambda_ast_passes/provenance.txt)

The canonical AST, diagnostics and exit status matched the control for all 5,536 tracked scripts. The C syntax suite passed 491/491. The Lambda baseline passed 6,119/6,119, including 368 forced-GC cases. [Canonical comparison](../../temp/lambda_ast_passes/ast-corpus.log), [C syntax suite](../../temp/lambda_ast_passes/c-s16.log), [Lambda baseline](../../temp/lambda_ast_passes/lambda-baseline.log)

The standalone Test262 baseline passed 40,261/40,261 with zero retries or regressions. [Test262 baseline](../../temp/lambda_ast_passes/test262-baseline.log)

## 11. Sharing return, retention and call analysis

The rebuilt `cf1f0acad` control and candidate use the same parent-pinned Input and Radiant sources, compiler profile, script bytes and cache policy. The active owner checkouts have different APIs, so both builds use isolated copies of the parent's recorded owner revisions. Quick relink trials had a smaller symbol set. The table uses the standard debug link from the split build target. The benchmark keeps the phase boundaries in §9. Two series of 11 interleaved fresh-process pairs each follow a discarded warm-up pair. Program output and diagnostics match within each pair after excluding process RSS telemetry (D8.1.1, D8.2.4–D8.2.5). [Source and executable hashes](../../temp/lambda_ast_sub10/provenance-final.txt), [standard debug build](../../temp/lambda_ast_sub10/build-debug-official.log)

The starting `expansion.ls` profile attributed 150 of 354 root parse samples to the lexer and 156 to the syntax sink. Of 718 root resolve samples, 126 included function completion, 118 included import loading, and 106 included call resolution. These are inclusive sample counts and can overlap. [Starting native sample](../../temp/lambda_ast_sub10/expansion.sample)

| Module | Debug parse before → after (ms) | Debug resolve before → after (ms) | Debug combined before → after (ms) | Debug runs under 10 ms |
|---|---:|---:|---:|---:|
| `expansion.ls` | 4.978 → 4.056 | 7.968 → 5.559 | 12.944 → 9.607 | 22/22 |
| `render.ls` | 3.048 → 2.616 | 5.492 → 4.262 | 8.543 → 6.879 | 22/22 |
| `digest.ls` | 2.648 → 2.185 | 4.136 → 3.252 | 6.787 → 5.438 | 22/22 |
| `commands.ls` | 2.124 → 1.762 | 3.187 → 2.058 | 5.310 → 3.821 | 22/22 |
| `mod_ui.ls` | 1.427 → 1.212 | 3.799 → 2.860 | 5.226 → 4.080 | 22/22 |

`expansion.ls` improves 25.8% in combined time and 30.2% in resolution. Its two combined medians are 9.597 and 9.637 ms, and its measured runs span 9.555–9.846 ms. Its inline function-analysis counter falls from 1.639 to 0.716 ms. That counter can include nested module work and should be read independently of the root resolve timer. Median process peak RSS is approximately flat at 38.95 versus 38.84 MiB. [First debug series](../../temp/lambda_ast_sub10/debug-official.jsonl), [repeat series](../../temp/lambda_ast_sub10/debug-official-confirm.jsonl), [timing driver](../../temp/lambda_ast_sub10/measure.py)

In a separate release sweep, the five combined medians ranged from 3.666 to 9.179 ms. The slowest module was `expansion.ls`. [Release samples](../../temp/lambda_ast_sub10/release-captures.jsonl)

- The return-contract walk collects potential copy-on-write retention sites alongside explicit returns. Function close reuses the collection for every parameter and transitive local alias, with a single collection walk when the return check skips the body. The exact walk remains available if a diagnostic prunes collection or scratch allocation fails (S9.1.2, S9.1.3v4, S11.4.2v3).
- A shared write-target census answers mutation queries. Positional calls stream written arguments and the injected receiver, while named calls use the established parameter map. An empty write set skips the local-alias walk. Argument validation shares its named function lookup and avoids deferred binder tests on proven paths (S9.1.2, S12.3.2, S11.4.8v2).
- An early build reached 9.879 ms but repeated at 10.170 ms. The final candidate checks cross-frame reads once after the binding graph supplies callable captures. That removes the earlier function-body walk. A concrete clean argument reaches the implicit clean contract without a composite type check (D8.2.4–D8.2.5, S9.1.4v2, S7.7.3). [Early comparison](../../temp/lambda_ast_sub10/debug-final.jsonl), [early repeat](../../temp/lambda_ast_sub10/debug-confirm.jsonl)
- Resolution reuses canonical local spellings, filters impossible qualified imports before building composite names and skips structural field scans when it only needs an object method. Imports keep exact source acquisition and defer the fallback existence check until a native load fails. Worker AST preparation keeps its separate handling for cross-language imports (D5.6.2, D8.5.1, D9.3.4, S12.3.3v2, S17.2.1).
- Tokenization dispatches punctuation by its leading byte and scans identifier and horizontal-space runs in bulk. The syntax sink remembers recent names after an exact byte check and allocates each identifier with its primary wrapper. Both AST identities remain available to binding. The name cache and retention lists are compilation scratch (D8.1.1, D8.2.4, S16.2.1, S16.10.2).

The production parser preserved token positions, lexer state and token kinds across 15,000 generated streams. It also matched all 5,536 tracked parser fingerprints, syntax diagnostics and recovery outcomes. Canonical AST output, diagnostic text and exit status match on the same corpus. The C S16 suite passed 491/491. The capture-mutation fixture produced identical E231 diagnostics on each tier (S9.1.4v2). [Raw lexer comparison](../../temp/lambda_ast_sub10/lexer-parity.log), [parser comparison](../../temp/lambda_ast_sub10/parser-parity.json), [canonical AST comparison](../../temp/lambda_ast_sub10/ast-captures-parity.log), [C syntax suite](../../temp/lambda_ast_sub10/c-s16-captures.log), [T0 diagnostic](../../temp/lambda_ast_sub10/debug-captures-capture-interp.log), [JIT diagnostic](../../temp/lambda_ast_sub10/debug-captures-capture-jit.log)

The final Lambda baseline passed 6,117 of 6,119, including all 2,495 Lambda script cases and 368 forced-GC cases. The rebuilt control reproduces both aggregate failures. DOM text coercion prints the same output on both arms. The phase-six PDF has the same SHA-256 across both arms and across T0 and JIT, and its object color differs from the golden. [Lambda baseline](../../temp/lambda_ast_sub10/lambda-captures-baseline.log), [control hashes](../../temp/lambda_ast_sub10/regressions/hashes.txt), [candidate hashes](../../temp/lambda_ast_sub10/regressions/final/hashes.txt)

The final release Test262 baseline passed 40,261/40,261 with zero retries or regressions. The harness skipped 2,652 cases outside the baseline. [Test262 baseline](../../temp/lambda_ast_sub10/test262-captures-baseline.log)

## 12. Compact name nodes and share resolution work

The `cac5060e5` control is the release executable from §11. The four frontend source hashes match that commit. Both arms link the same Input and Radiant revisions used in §11. The checkout's older owner revisions expose different DOM and CSS interfaces. The release comparisons use the same timer boundaries as §9, warm filesystem caches, exact output, opposing execution order, T0, and both shared script reuse and AST prebuild disabled (D8.1.1, D8.2.4–D8.2.5, D8.5.1). [Timing driver](../../temp/lambda_ast_compact/measure.py), [control and candidate hashes](../../temp/lambda_ast_compact/provenance-census.txt)

The starting stack sample attributed 41 of 648 root resolution leaf samples to scope lookup, 27 to write traversal, 27 to retention evaluation, and 23 to pool allocation. These cover repeated lexical probes, repeated ownership analysis and storage that has only compilation lifetime. [Native sample](../../temp/lambda_ast_compact/expansion.sample)

| Stage | Implemented change | Dependency that sets the stage boundary |
|---|---|---|
| Syntax construction | A bare name has one mutable-in-place syntax node. The C token classification supplies operator identity when the syntax node is created, so resolution consumes the operator directly. Explicit group nodes carry their own span and lexical scope. | Source-order bindings remain open through syntax construction (D8.1.1, D8.2.5, S11.4.12v2). |
| Lexical and call resolution | Reuse a lexical probe for a bare member receiver. A per-unit lookup cache keys resolved bindings by interned name, start scope and insertion generation. Successful qualified reads morph in place. A direct builtin can reuse its name storage and its unique registry row. A rejected arity still has the callable contract for diagnostics (D5.6.2, S12.3.2, S12.3.7, S17.2.1). | Imported sources still enter the same acquisition and visibility rules (D8.5.1, S16.9.8). |
| Literal shape assembly | Check keys and classify dynamic shape in the same visit. Static maps keep the syntax item chain. A field and its name view share one allocation. Overflow duplicate-key storage is compilation scratch (D3.4.3, S2.3.1v2, S16.8.9). | Duplicate written-key diagnostics precede namespace lowering. Computed keys and spreads still use runtime replacement order. |
| Function completion | One scan derives whole-root and part retention. Explicit return checking or defect discovery also builds the retention census. A source-order write census skips the body write walk when no call or store can write through a binding. The late return-alias fixpoint reuses the resolver's proof when there are no explicit returns and otherwise shares one site list across a function's queries in each iteration (D7.1.3, S9.1.2, S9.1.3v4, S11.4.2v3). | Closed signatures are visible for forwards and recursion. Call-dependent alias facts still converge after signatures exist (S11.4.1v5, D8.2.5). |
| Bound graph and validation | Indexed functions feed module validation. REPL fragments collect their functions once for both borrow and concurrency analysis. Ownership scans with early exits use the shared semantic child walker, which visits every sibling at its parent. Concurrency releases its temporary call graph when it publishes the durable may-await facts (D5.4.4, D7.1.2, D8.2.4–D8.2.5, D8.3.6, S9.3.1). | Captures need published canonical binding edges. Effect and outcome propagation still require completed callees and fixed-point order (S9.1.4v2, S11.4.2v3, S12.1.4v3). |

Retired code includes the implicit-primary syntax form, both operator-spelling decoders, local duplicates of the shared primary and compound-root queries, the duplicate REPL callable collector and the completed CW29 rollout warning. The normalizer used for the AST comparison removes transparent expression-primary shells from both dumps. Explicit parentheses have the same dump encoding as bare-name wrappers, so the comparison is paired with the S16 and execution suites. [Comparison driver](../../temp/lambda_ast_compact/compare_ast.py)

Two series of 11 measured pairs per script followed one discarded warm-up pair each. Order was reversed in the second series. The table shows pooled medians and the reduction from paired ratios. The geometric mean of the per-script resolution ratios is an **11.8% reduction**, short of the 20% target. Resolution, binding and validation together decreased by **18.5%**. The combined parse and resolve time decreased by **7.4%**. The final release resolved faster in 108 of 110 measured pairs. [First series](../../temp/lambda_ast_compact/census-a.jsonl), [second series](../../temp/lambda_ast_compact/census-b.jsonl), [paired summary](../../temp/lambda_ast_compact/census-summary.json)

| Module | Resolution before → after (ms) | Paired resolution reduction | Peak compiler RSS before → after (MiB) |
|---|---:|---:|---:|
| `expansion.ls` | 5.393 → 4.616 | 14.6% | 34.20 → 32.20 |
| `render.ls` | 4.265 → 3.895 | 8.9% | 106.44 → 94.14 |
| `digest.ls` | 3.139 → 2.639 | 16.1% | 25.78 → 22.68 |
| `commands.ls` | 2.001 → 1.699 | 14.9% | 25.12 → 23.67 |
| `mod_ui.ls` | 2.883 → 2.748 | 4.1% | 179.84 → 159.27 |

LLDB read the root module's published index and allocator counters at entry to validation. These are memory measurements from the two symbolized debug builds. Active arena bytes include syntax allocations held for the unit's lifetime. Pool live bytes and process peak RSS describe different scopes. `digest.ls` also drops from 65,536 reserved index slots to 32,768 (D5.2.5, D8.2.4). [Memory probe](../../temp/lambda_ast_compact/read_memory.py), [control counts](../../temp/lambda_ast_compact/memory-control.jsonl), [candidate counts](../../temp/lambda_ast_compact/memory-census.jsonl)

| Module | Bound nodes before → after | Active arena before → after (MiB) | Arena reduction | Pool live-byte reduction |
|---|---:|---:|---:|---:|
| `expansion.ls` | 61,914 → 49,041 | 5.060 → 4.191 | 17.2% | 4.9% |
| `render.ls` | 30,648 → 24,231 | 2.678 → 2.176 | 18.8% | 4.8% |
| `digest.ls` | 33,342 → 26,372 | 2.704 → 2.215 | 18.1% | 6.6% |
| `commands.ls` | 25,259 → 19,539 | 2.036 → 1.640 | 19.5% | 4.5% |
| `mod_ui.ls` | 14,013 → 11,830 | 1.148 → 0.984 | 14.2% | 5.5% |

The candidate matched all 5,536 normalized dumps, diagnostics and exit statuses. C S16 passed 491/491. The 72 focused execution comparisons used T0 and JIT, with default and forced GC. In two strict T0 comparisons, both binaries returned the same E212 for `mir_mvp_method_capture_cow.ls`. The inherited DOM text-coercion and phase-six PDF golden mismatches also reproduced with byte-identical output on control and candidate. [AST checks](../../temp/lambda_ast_compact/census-ast.log), [C syntax checks](../../temp/lambda_ast_compact/census-c-s16.log), [focused execution](../../temp/lambda_ast_compact/census-runtime.log), [DOM and PDF output comparisons](../../temp/lambda_ast_compact/census-owner-regressions.log)

The sandboxed Lambda baseline passed 6,100 of 6,119 cases. All 17 cases that needed local listeners passed in focused reruns with loopback access. The two control-reproduced golden mismatches remain. All 2,495 Lambda script cases and 368 forced-GC cases passed across those runs. [Lambda baseline](../../temp/lambda_ast_compact/lambda-census.log), [Lambda loopback](../../temp/lambda_ast_compact/census-loopback-lambda.log), [Hotwire loopback](../../temp/lambda_ast_compact/census-loopback-serve.log), [DOM media loopback](../../temp/lambda_ast_compact/census-loopback-media.log)

Test262 used the normal baseline runner with isolated release executables. The first candidate run passed 40,258 of 40,261 in the batches. Three cases exceeded the batch time threshold and passed on isolated retry. The control and a candidate repeat seeded with the control's batch timing records both passed 40,261 of 40,261 without retries or regressions. Each run skipped 2,652 cases outside the baseline. [First candidate run](../../temp/lambda_ast_compact/test262-census.log), [control](../../temp/lambda_ast_compact/test262-control-repeat.log), [candidate repeat](../../temp/lambda_ast_compact/test262-census-confirm.log)

The remaining resolution budget differs by script. `render.ls` and `mod_ui.ls` have 51 and 46 imports. The root timer subtracts nested compilation and still includes source acquisition and importing-scope export registration. Split that cost into canonicalization, exact source acquisition and export registration before changing the loader. Any reusable result has to respect provider access and the current source generation (D8.5.1, S16.9.8).

The next pass-fusion candidate is a scratch return-site table shared across fixpoint iterations for functions with explicit returns. Site identities stay stable while the callee facts change. Reuse the list produced at function completion and release it when alias propagation finishes. Captures, forward return contracts and converging callee effects still require the bound identities or completed signatures specified by D8.2.4–D8.2.5, S9.1.4v2 and S11.4.1v5.
