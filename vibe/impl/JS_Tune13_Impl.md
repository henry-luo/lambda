# JS Tune13 — Implementation Plan

**Version:** 1.0.0

**Date:** 2026-09-17

**Status:** IN PROGRESS — native-only routing, compact static initializers,
guarded typed Number read/store leaves, ordinary enumeration inspection,
root-planner storage reduction, and bounded call/string adapter reductions are
landed locally; full Tune13 correctness and performance acceptance remain open.

**Design authority:** [JS Tune13](../jube/JS_Tune13.md), governed by **S1.11**,
**D2.4**, **D3.3.2v2**, **D3.4.4v2**, **D4.6**, **D5.3**, **D8.1.3v11**,
**D8.4.1v2–D8.4.3v2**, and **D8.6.1–D8.6.3**. The native-only MIR decision is
ratified; the implementation and tuning gates below remain open. See the
proposal for the corrected Tune12 evidence, helper design, source inventory,
and superseded execution policy.

## 1. Work packages

The `T13-*` identifiers below are work packages, not new normative ruling IDs.
All boxes begin unchecked. Implementation status and full logs will be maintained
in this file when code work starts; this plan is not a completion record.

| Package | Deliverable | Dependencies |
|---|---|---|
| T13-0 | Correct provenance, fixed controls, runtime/compile/helper census | None |
| T13-1 | Native-only JS MIR selection and lifetime ownership | T13-0 |
| T13-2 | Compiler/root-planner scaling and large initializer reduction | T13-0/1; measure before further loop work |
| T13-3 | Complete ordinary/typed numeric pipelines | T13-1; share T13-2 artifacts |
| T13-4 | Effect-bounded guard and metadata reuse | T13-3 |
| T13-5 | NameId and nonallocating property/enumeration paths | T13-0/1 |
| T13-6 | Guarded constructor fields and measured call-edge costs | T13-3/5 |
| T13-7 | Profile-gated RegExp and string residuals | Reprofile after T13-1/3/5 |
| T13-8 | Regression resolution, full evidence, consolidation | All packages |

Keep policy, compiler-memory, numeric, name/descriptor, and builtin changes in
separately measurable revisions. Work on independent packages need not wait for
unrelated implementation, but their predecessor and fixed-control evidence must
remain unambiguous.

### Implementation record — 2026-09-16/17 (in progress)

| Package | Landed local work | Verification so far | Status |
|---|---|---|---|
| T13-1 | JS MIR compilation always initializes/links native generation; JS rejects `--mir-interp`; eval/module/batch paths use the explicit native contract. | Focused script/module/eval checks; JS forced-GC corpus no longer requests the retired JS interpreter. | Partial: complete entry-matrix and fixed release controls remain. |
| T13-2 | `em_finalize_semantic_root_write_back` retains exact liveness only for collecting calls instead of two dense instruction-by-candidate matrices. Inline-Number, no-hole/no-spread array literals call one shared fresh-array construction leaf instead of lowering one box/store pair per element. The leaf installs owned dense storage directly when a complete literal exceeds the normal all-hole dense threshold; it never routes a fully populated literal through the sparse-hole setter. | 189/189 forced-GC corpus and 50/50 JS optimization contracts pass under debug native, including a 10,001-element literal that crosses the allocation threshold. The current Hyphen diagnostic emits 342 compact initializer calls; its `js_main` has 260,381 finalized instructions, 15,828 safepoints, and 18,817 root stores. | Partial: allocation census, fixed-control release scaling, and larger initializer forms remain. |
| T13-3 | A guarded typed Number store invokes `js_typed_array_set_number_if_kind`, preserving the original boxed RHS solely for the semantic miss. A paired no-GC read snapshot validates receiver/kind/live bounds and returns a backing pointer consumed immediately by the generated physical load, replacing three separate runtime queries. | Focused typed contracts, all 50 optimization contracts, and 189/189 forced-GC cases pass after the snapshot leaf. | Partial: end-to-end search/FFT carrier coverage and release evidence remain. |
| T13-4 | `Array.prototype.reduce` uses a no-GC dense-own-element leaf and keeps its four callback arguments in one pre-rooted span. It revalidates through the existing `HasProperty`/`Get` semantics after every unknown callback, so it skips holes and observes newly installed accessors without retaining stale array facts across reentry. | Dedicated callback-deletion/accessor contract plus 50/50 optimization contracts and 189/189 forced-GC cases pass. Hyphen records 72,072 dense elements, 72,072 pre-rooted adapter uses, and 13 fallbacks. | Partial: this is one revalidation leaf, not the required general length/data witness reuse; its 103.995-ms focused timing is not a measurable win. |
| T13-5 | Maps and ordinary dense arrays use a nonallocating own-enumerability inspection; exotic, sparse/holey, accessor, and proxy paths retain public-descriptor fallback. `Object.values(array)` now reaches own-key filtering. Compiler-pending functions defer finalization and keep `name`/`length` descriptor storage virtual until direct property observation, reflection, deletion, prototype materialization, ordinary Set, or DefineOwnProperty requires the real ordered shape. Ordinary-name classification now computes routing hash, ASCII, and index facts in one byte walk. Name-pool creation remains catalog-first then parent-before-child, but its local/parent scope searches do not re-probe the catalog; public lookup probes the catalog once after every scope misses. The generic keyed Get boundary canonicalizes an id-less String once after ToPropertyKey and keeps that rooted NameId through ordinary-own/prototype lookup. | The metadata contract covers direct reads, descriptors, reflection order, delete-before-read, prototype-first, Set, and DefineOwnProperty order; the dynamic-key/prototype-get contract covers inherited accessor behavior; 50/50 optimization contracts and 189/189 forced-GC cases pass. The release-profile Hyphen run records 41,043 deferred finalizations and 41,043 lazy metadata admissions. The 22-test name-pool suite verifies catalog and parent creation identity, including both new helper contracts. | Partial: NameId census and workload attribution remain; virtual metadata is restricted to compiler-pending functions. |
| T13-6 | MIR-light bodies whose finalization contract excludes activation-observable facts retain one callee root and directly invoke their native body. Receiver-only bound calls root their wrapper, target, and bound receiver, then forward the caller's exact argument span; bound-argument calls retain the merged-span path. | The light-call dynamic-semantics and receiver-only-bound contracts, all 50 optimization contracts, and 189/189 forced-GC cases pass. Hyphen records 197,234 direct light activations and 133,287 bound-span forwards. | Partial: direct-call and bound-forward focused medians (103.843 and 102.711 ms) are unpaired diagnostics, not a completed call package. |
| T13-7 | URI percent-escape caches are considered only for `%X`, `%XX`, or a complete 12-byte escape candidate, leaving ordinary ASCII concat free of cache root registration/checks. | Unicode URI decode contract plus all 50 optimization contracts and 189/189 forced-GC cases pass. Hyphen records 53,456 ASCII concat leaves; its 102.895-ms URI-cache timing and the execution sample show concat is not its dominant residual. | Partial: regex/all text-outlier attribution and a material residual remain open. |
| T13-8 | JS external dense-array buffers carry an intrusive owner/list header instead of a realm `HashMap` reverse registry. Generic collection growth copies and relocates the scalar tail first, then releases the old external buffer before publishing the GC-owned replacement; GC sweep and full realm reset still neutrally disarm live owners before freeing their headers. The small fresh sparse-length constructor roots its Array across a GC-data allocation and attaches that buffer before slot initialization; growth/species paths retain external ownership. | The ownership trace records 77,014 installs and 50,058 releases in Hyphen. The narrow GC-buffer trace records 77,014 GC-data allocations and zero external installs; 50/50 optimization contracts and 189/189 forced-GC cases pass. The release median is 90.978 ms (98.516/89.317/90.978), versus 91.420 ms in the preceding distinct binary. | Partial: the 0.5% comparison is fresh and unpaired; allocation/GC, name/hash, and property residuals still require broader attribution and controls. |

The root-planner change keeps the block fixed-point solution, reverse transfer,
root-slot coloring, and call reload facts unchanged; only the storage of facts
consumed by collecting calls changed. This is governed by **D5.3** and remains
subject to the final zero-slack and release gates.

The lazy metadata path is an ordinary-shape deferral, not synthetic reflection.
The function retains its final `name` and formal length; the first operation that
can observe an own descriptor publishes real `length` then `name` slots. Any
prototype or user-field creation publishes that same prefix first, and deletion
creates a real tombstone. This retains **D6.2.2v2** property authority and
**D5.3** rooting while avoiding the callback-time maps shown by the profile.

Focused release evidence is intentionally not closeout evidence. The fresh
three-sample Hyphen comparison in `temp/js_tune13_hyphen_lazy_metadata_final.json`
has a 105.083-ms LambdaJS median versus 57.020-ms MVP and 26.008-ms QuickJS;
its immediate unpaired predecessor in
`temp/js_tune13_hyphen_reduce_dense.json` is 130.201 ms. Later distinct-binary
runs measure 103.843 ms for direct light calls,
103.995 ms for pre-rooted reduce arguments, 102.895 ms for URI-cache narrowing,
and 102.711 ms for bound-span forwarding. The ownership redesign's corrected
release result is 94.930 ms (94.787/94.930/96.152); the same run records MVP at
57.438 ms and QuickJS at 26.120 ms. Its Lambda binary SHA-256 is
`7832a4986b1299df696bf48a35b7d10bd2fcd79cdd2a17b2e9d42898a00f8746`.
The one-pass classifier result is 92.748 ms (93.965/91.679/92.748). The
catalog-deduplicated lookup result is 91.420 ms (91.420/89.994/91.967), versus
58.842-ms MVP (one 77.734-ms sample) and 26.839-ms QuickJS; its Lambda binary
SHA-256 is `647e4dfb5e63cf68ce7ff1d5ade0896b72dfdbd75bf070457f94323a30149d75`.
The GC-data run is 90.978 ms (98.516/89.317/90.978), SHA-256
`1359fcba3bbeff2f7f4bd1fd0530a5eda5ab2483a74887ecdcff4a54ac5e1c9a`;
the dynamic property-key run is 90.268 ms (90.268/90.987/88.544), SHA-256
`575a941311a629c829f740de287d41806b27e179cf96cf85414cfa19f9ec71ad`.
These are all fresh, distinct, unpaired binaries, so the 3.7% ownership-to-name
comparison and later sub-percent changes are local attribution rather than an
aggregate acceptance result.
The traces are `temp/js_tune13_hyphen_{lazy_metadata,uri_cache,bound_forward}_profile.tsv`
and `temp/js_tune13_hyphen_{gc_items,property_key}_profile.tsv`, plus
`temp/js_tune13_hyphen_array_ownership_profile.trace.tsv`. The original
`temp/js_tune13_hyphen_execution.sample` captured startup/link work; the delayed
guest sample is `temp/js_tune13_hyphen_array_ownership_guest_execution.sample`
(4,303 worker samples), which identifies GC, canonical-name, and generic
property work without turning overlapping samples into percentages. T13-0 still
requires fixed controls and a full matrix.

### T13-0 — Establish trustworthy controls and an operation census

- [ ] Archive the actual current release starting tree: commit, dirty patch,
  configuration, compiler/toolchain, external-module identities, binary SHA-256,
  input/manifest hashes, QuickJS version/hash, power state, and backend settings.
  Confirm the runner executes those exact bytes.
- [ ] Keep the Tune12 identities in proposal §2.2 distinct. Publish a provenance
  erratum alongside Phase 2's report without altering raw samples or pretending
  they were collected with a different control.
- [ ] Record two controls: **A**, actual pre-Tune13 default behavior; **B**, the
  T13-1 native-only checkpoint. Compare B/A for policy cost, each later revision
  against its immediate predecessor, and final/B plus final/A for cumulative
  tuning and user-visible change. Label A's per-row actual execution mode.
- [ ] Run the complete 63-row baseline, a full same-session MVP comparison where
  supported, and a compiler/memory census. Profile execution separately from
  parse/lower/link/generation/teardown. Do not include profilers in timing runs.
- [ ] Extend existing opt diagnostics with operation/fallback reason, native
  carrier continuity, name re-resolution, descriptor materialization, actual
  helper hits, and root/safepoint counts. Counters are disabled in acceptance
  timing; no always-on logging in hot loops.
- [ ] Recheck `list`, `crypto_sha1`, and inherited Tune12 regressions using genuine
  predecessor archives. Record unreproducible noise separately from fixed bugs.

**Exit:** immutable reproducible controls and an evidence table that distinguishes
measured cause, sampled hypothesis, and unmeasured residual. Historical scratch
profiles are context, not new acceptance evidence.

### T13-1 — Eliminate LambdaJS MIR interpretation

- [ ] Apply the complete inventory in proposal §5.1. Do not implement this as setting
  `LAMBDA_JS_LARGE_INTERP=0` by default while leaving other selection routes live.
- [ ] Make JS compile-unit init/link/cleanup explicitly native and independent of
  ambient MIR-interpreter state. Audit failure, timeout, nested eval/require,
  deferred callback, reused runtime, and batch teardown paths.
- [ ] Remove JS-only thresholds/flags only after auditing shared Lambda consumers.
  Native optimization-level controls and other profiles' interpreter support
  retain their separate contracts.
- [ ] Record effective backend, native generator initialization, and entry
  publication in existing diagnostics. Test actual execution routing, not just
  the absence of one environment variable or a `jit` string in a report.
- [ ] Add coverage for standalone/default/explicit MIR, old size boundaries,
  opt=0, document scripts, CJS/ESM, direct/indirect eval, dynamic functions,
  retained artifacts, nested languages, and a host with interpreter global set.
  Include AST-selected controls proving that AST remains AST.
- [ ] Exercise old env settings and explicit CLI rejection. Eager and opt-in lazy
  native generation must each preserve nested entry, exception, and code/context
  lifetime. Compilation failures must not run the unit a second way.
- [ ] Measure B/A execution and end-to-end cost, including Hyphen and large cold
  document/vendor fixtures. Publish startup regressions rather than hiding them
  with an interpreter escape hatch.

**Exit:** no reachable JS path installs the MIR interpreter interface or silently
diverts a selected MIR unit to AST. Native ownership and execution are observed
across the matrix; other language modes remain unchanged. A library symbol for
another guest is not itself a violation.

### T13-2 — Reduce compiler scaling and initialization volume

Primary source: `lambda/runtime/mir_emitter_shared.hpp`, particularly
`em_finalize_semantic_root_write_back`, plus JS literal/initializer lowering and
indexed function analysis.

- [ ] Instrument allocation bytes/lifetimes for CFG, candidate sets, liveness,
  interference, MIR, AST, native code, and runtime objects. Measure peak overlap,
  not just the total number of allocations. Separate cold and reused contexts.
- [ ] Replace unnecessary dense per-instruction root matrices with block-level
  fixed-point liveness plus reverse block scans/on-demand instruction facts where
  consumers permit it. Use sparse/adaptive interference storage when it reduces
  the measured candidate graph; do not merely move the same quadratic allocation.
- [ ] Preserve instruction effect order, exceptional/branch/loop edges, alias
  kills, scalar-home constraints, and exact root slot interference. Compare old
  and new planner facts during migration, then remove the old owner. This is
  temporary development evidence, not a permanent duplicate liveness verifier;
  the independent acceptance oracle remains dynamic GC stress (**D8.6.3**).
- [ ] Reduce MIR explosion for large literal initialization through shared
  construction primitives or compact immutable initialization descriptions, only
  for semantically admitted literal forms. Root partially constructed objects;
  preserve property order, holes, duplicate keys, `__proto__`, computed keys,
  spreads, getters, and allocation-failure behavior. Unsupported forms retain the
  existing lowering. No benchmark-specific bulk loader or source recognizer.
- [ ] Reuse indexed function-owned plans rather than introducing repeated AST
  walks or another fact cache. Keep Lambda-side fixes outside vendored MIR.
- [ ] Measure generated instruction counts, root stores, safepoints, compiler
  allocation peaks, lower time, native generation time, and total cold latency on
  a size-scaling fixture family plus Hyphen/Prettier/document scripts.

**Exit:** the diagnosed high-cost owners/scans are reduced with exact-root
equivalence, forced-GC and zero-slack MIR evidence. Publish remaining native
code-generation cost separately. No arbitrary performance percentage substitutes
for demonstrating the structural scaling change.

### T13-3 — Complete native array load–compute–store regions

Primary source: `js_mir_expression_lowering.cpp`, existing numeric/parameter
analysis and helper registry, `js_runtime.cpp`, and `js_typed_array.cpp`.

- [ ] Explain the admission/refusal of `naiveSearch`, `boyerMooreSearch`,
  `kmpSearch`, and FFT `four1` with binding-identity facts. Extend sound
  producer/consumer facts to both operand positions, local joins, loop-carried
  values, and boxed/void-return functions; do not assume array element types
  from benchmark inputs.
- [ ] Carry admitted length, induction, load, arithmetic, comparison, and store
  values as native carriers. Materialize Items only at actual generic consumers,
  miss continuations, boxed ABI boundaries, or representation merges.
- [ ] Add/reuse noncoercing physical typed-store primitives for proved Number
  values, called by both the guarded lowering and full semantic setter. The
  existing `js_typed_array_set_numeric_key` boxed/coercing route is not the target
  hot store. Preserve JS index and element conversion semantics, including
  clamped/integer cases only when separately supported; BigInt stays distinct.
- [ ] Finish ordinary dense numeric-array paths with guards for holes, receiver
  kind, companions/descriptors, index, and bounds. A numeric fast path must not
  skip prototype lookup for a hole or coerce a non-Number differently.
- [ ] Preserve left-to-right evaluation and complete misses for proxies, wrong
  receivers, accessors, fractional/negative/out-of-range keys, mixed elements,
  symbols/BigInt, NaN, infinities, negative zero, and object coercion.
- [ ] Add finalized-MIR and dynamic hit fixtures for complete read–compute–write
  regions, not merely presence of a `_n` function. Demonstrate fewer executed
  generic conversions/calls on search and FFT alongside release paired wins.

**Exit:** at least the diagnosed ordinary-search and typed-FFT regions actually
retain native values end to end, with tested misses and precise ownership.
Any still-boxed operation is listed explicitly rather than hidden in aggregate
speedup. Keep `sum`, `fib`, `primes`, `binarytrees`, and `gcbench` as controls.

### T13-4 — Reuse metadata within proved effect boundaries

- [ ] Extend the existing invariant/effect planner to express receiver identity,
  fixed kind, guarded length/data validity, aliases, and invalidating effects.
  Use immutable plans and operation/region-local witnesses, not adaptive caches.
- [ ] Hoist repeated length/data/ordinary-receiver checks only across regions
  proved free of reentry, detach/resize, backing-store replacement, length writes,
  descriptor/prototype change, or unknown alias mutation. An unknown call ends
  the region. Narrow proofs are acceptable; unsound “all loops” assumptions are not.
- [ ] Reload/revalidate after invalidation and at required CFG merges; preserve
  owner roots and interior-pointer validity at all safepoints. Prefer loop
  segmentation over retaining stale raw data pointers across calls.
- [ ] Test direct and aliased mutations, callbacks/accessors, resize/detach,
  sparse/holey transitions, zero-trip loops, exceptional exits, and nested loops.
- [ ] Measure dynamic guard/query counts and timings against T13-3 alone. A
  kind-only hoist or an uncertain short-row result does not close this package.

**Exit:** real admitted regions reuse length/data or equivalent validity evidence,
and adversarial invalidating cases demonstrably re-enter the semantic path.

### T13-5 — Make names, descriptors, and enumeration inexpensive

Primary source: `lambda/core/name_pool.cpp`, `js_runtime.cpp`, `js_globals.cpp`,
and existing property/shape/descriptor kernels.

- [ ] Trace static-name and enumeration-name flow through `js_get_name_id`,
  `js_get_reference`, shape lookup, well-known-name classification, and pool
  lookup. Consume existing linked names/metadata directly when ownership permits;
  do not repeatedly reconstruct IDs from spelling.
- [ ] Retain names in the current context domain and trace any associated owner.
  Dynamic strings, symbols, integer indices, imported scripts, and realm/cache
  reuse require their existing correct conversion/linking boundary.
- [ ] Extend the existing internal descriptor inspection to ordinary arrays where
  sound. `js_for_in_key_is_live` should not allocate a JS descriptor object just
  to inspect enumerability; its current POD shortcut primarily covers maps.
  Public `Object.getOwnPropertyDescriptor` still returns the required JS object.
- [ ] Reuse the same semantic descriptor authority for arrays and objects. Preserve
  deletion/re-addition, prototype shadowing, ordering, nonenumerable properties,
  holes, array indices/length, getters, proxies, and exotic objects during for-in.
- [ ] Keep ordinary receiver admission ahead of irrelevant host/global work when
  proved, with one full semantic miss and no unchecked caller “skip” flag.
- [ ] Measure name hashes/lookups, descriptor allocations, helper self time, and
  end-to-end effect on `microdiff`, Havlak, merges, and log/search pipelines.

**Exit:** targeted real paths avoid repeated spelling resolution and temporary
descriptor objects; all mutation/exotic cases preserve observable behavior.

### T13-6 — Extend field coverage; optimize calls only where measured

- [ ] Extend existing immutable field-layout plans beyond literal class fields to
  statically analyzable constructor assignments. Prove initialization order and
  escape/alias constraints; do not equate a mutable value with an unknowable
  layout or with permission to cache an unguarded offset.
- [ ] Guard current receiver shape, own descriptor, slot representation, and
  prototype assumptions as required. Retain generic get/set for accessors,
  deletion/redefinition, subclass variation, constructor object replacement,
  unusual receivers, and failed layout predictions.
- [ ] Preserve native field values into admitted consumers and stores rather than
  immediately boxing them. Shared shape/descriptor primitives must remain the
  authority; no duplicated lookup or mutable site IC.
- [ ] Separate Get/method resolution from callable-entry setup in profiles.
  Preserve Get-before-arguments and `this`/`new.target`/bound/proxy/construct
  semantics. Optimize the selected callable via the established invoke/construct
  contract, never a fresh property reread or a second dispatcher.
- [ ] Audit temporary argument storage, roots, and repeated callable checks only
  after measuring them. Reuse caller-owned argument storage solely under its
  valid lifetime/GC contract. A 2.8% dispatcher self share is not a claim that
  eliminating the wrapper yields Havlak's entire runtime gap.

**Exit:** constructor-assigned ordinary fields have real guarded coverage in the
diagnosed workloads. Call changes require their own isolated evidence; a measured
low-priority/no-change call disposition cannot close the field deliverable.

### T13-7 — Profile-gated RegExp and string residuals

- [ ] Reprofile `hyphen`, `regexredux`, `revcomp`, and remaining text outliers after
  native selection and the preceding changes. Distinguish property/name routing,
  matching, result construction, character decoding, and allocation.
- [ ] Audit `js_regex_test` and `js_regexp_property_all_mode`: repeated observable
  `source` reads and pattern classifiers currently precede some special paths.
  Move immutable classification to the compiled pattern owner only where the
  operation's semantics permit bypassing that work; otherwise retain the full
  protocol. Reuse existing matching kernels; do not patch vendored RE2.
- [ ] Test custom `exec`, symbol methods, overridden source/flags accessors,
  subclass behavior, global/sticky `lastIndex`, empty matches, Unicode, throws,
  and reentry. A boolean-result optimization must not discard required effects.
- [ ] Investigate repeated non-ASCII UTF-16 length/index scans from the start in
  `js_runtime.cpp`. If dynamic profiles establish material cost, add/reuse
  immutable owner-correct length/index metadata or compact offset checkpoints;
  preserve surrogate/code-unit semantics and quantify the memory tradeoff.
- [ ] Do not attribute array-number `text_search` or object-heavy `microdiff` to
  Unicode based only on their suite name. No new string representation or regex
  engine is justified by the current evidence alone.

**Exit:** every investigated residual has attribution and a measured implementation
or explicit evidence-based non-applicability. No speculative string/regex rewrite
is mandatory; an identified material root cause left unfixed remains open.

### T13-8 — Regression resolution and closeout

- [ ] Resolve repeatable regressions against both predecessor and fixed controls.
  Above 3% triggers mandatory root-cause investigation; smaller systematic
  regressions still count. Native-policy startup costs stay visible until reduced
  or explicitly accepted by the user, not silently relabeled a success.
- [ ] Remove duplicate primitives, stale JS mode switches, temporary tuning
  branches, and redundant conversions/scans. Keep useful diagnostics disabled
  by default and preserve complete semantic fallback.
- [ ] Run the correctness, performance, memory, and code-volume gates in §2 on
  the exact final source/artifacts, including shared Lambda consumers.
- [ ] Publish per-package structural evidence and actual dispositions; update
  implementation documentation only for landed behavior. Record outstanding
  issues and performance milestones separately from implementation status.

**Exit:** mandatory packages meet their structural/correctness exits, profile-gated
work has justified dispositions, full evidence is reproducible, and no unresolved
confirmed regression is hidden. Do not call Tune13 fully implemented merely
because all proposed experiments were attempted or one aggregate improved.

## 2. Validation and acceptance

### 2.1 Correctness and structural gates

Use the current full test population, not a frozen historical passing count:

- Full Lambda baseline when shared compiler/runtime code changes; JS baseline,
  full Test262, focused optimization tests, and the forced-GC JS corpus.
- Document/Radiant and module/batch/eval paths affected by native-only selection;
  native failure, timeout, retained-callback, and repeated-context cleanup tests.
- AST/native comparisons only for AST-admitted features; unsupported AST cases
  must not be hidden by fallback or counted as native correctness failures.
- Finalized-MIR structural contracts and zero-slack ratchets; do not regenerate
  expected counts merely to absorb unexplained growth (**D8.6.1–D8.6.3**).
- Precise-root/scalar-home tests across fast/miss joins, nested calls, exception
  paths, loop invalidation, and partially built literals (**D5.3**).
- Expected-output fixtures for semantic regressions, including a matching `.txt`
  whenever a new Lambda `.ls` test is introduced.

Phase 2 reported 5,593/5,593 baseline, 40,263/40,263 full Test262, 189/189
forced-GC, 31/31 optimization, and 19/19 MIR-ratchet checks. Those are historical
observations, not verification of Tune13 or a reason to exclude newly added tests.
Never mask a Test262 crash/timeout/failure by modifying its harness.

### 2.2 Performance and memory gates

Only `make release` builds are eligible. Record at least:

| Metric | Method and reporting |
|---|---|
| Execution | All 63 canonical JS rows, valid/equal output, at least 11 alternating control/candidate pairs per row, raw samples and paired confidence intervals. |
| Full JS / QuickJS | Fresh complete same-session matrix, at least three samples per engine; exact version/binary identity. Geometric mean and total-median ratio, every row/status. |
| Full JS / MVP | Fresh complete supported population with both engines' identities; unsupported rows explicit. A partial refresh remains a subset, not an acceptance score. |
| Cold latency | Parse, analysis/lower, link/generation, execution/event-loop drain, cleanup, and outer process wall time. Count lazy first-call generation where it actually occurs. |
| Memory | Peak RSS and phase/owner allocation census, runtime live/retained objects, MIR/planner/native code, GC activity; repeated/batch lifetimes as well as one-shot runs. |
| Code shape | Finalized instructions, generated native code bytes where available, helper/boxing/root/safepoint counts, runtime hit/miss coverage. |
| Cross-profile drift | Typed/untyped Lambda and affected document paths after shared planner/effect changes. |

Keep acceptance runs free of profilers/counters and keep the standard runner's
build, power, profile, output, and Test262 guards enabled. Confirm rebuilds have
not changed the candidate bytes. Failure/timeout prevents declaring a complete
valid geometric mean; never drop inconvenient rows. Use paired intervals for
causal claims; an uncertain estimate is not a measured win.

Publish both `G = exp(mean(log(candidate_ms / control_ms)))` and the ratio of
summed row medians. Target elapsed-heavy workloads and wide geometric outliers;
neither metric replaces the other. Report A→B policy cost separately from B→final
tuning, and final/A user-visible change. Compiler/RSS regressions cannot disappear
behind an execution-only score.

### 2.3 Completion report template

| Package | Exact revision/binary | Admitted hot path and remaining misses | Semantic/GC/MIR gates | Predecessor and A/B-control results | Status |
|---|---|---|---|---|---|
| T13-0–8 | To be recorded during implementation | No unproved coverage claims | Current logs and fixture IDs | Raw artifact links, startup/RSS included | Not started |

The final report states separately: **native-only policy implemented**, **mandatory
tuning work implemented**, **remaining confirmed bottlenecks/regressions**, and
**0.80x QuickJS milestone reached or unmet**. Policy conformance does not by itself
prove a performance win, and benchmark improvement does not excuse an incomplete
semantic or structural requirement.
