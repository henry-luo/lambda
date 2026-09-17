# JS Tune13 — Native execution and cheaper semantic operations

**Version:** 1.0.0

**Date:** 2026-09-17

**Status:** IN PROGRESS — this proposal remains the active Tune13 design record.
The native-only LambdaJS MIR execution decision in **D8.1.3v11** is implemented;
the remaining package exits and full-population performance acceptance are open.
Recent helper work has removed selected adapter-only work, but has not closed
the allocation/name/property residual exposed by Hyphen.

**Scope:** full LambdaJS, its MIR Direct lowering, runtime helpers, and shared
compiler machinery where measurements identify a cause. Retain full JavaScript
semantics and the shared Lambda value, memory, module, and execution contracts.

**Predecessor:** [JS Tune12](JS_Tune12.md), including both phases. This proposal
corrects the attribution of its Phase 2 results and carries forward unfinished
structural work; it does not replace or rewrite historical raw measurements.

## 1. Decision and objectives

Tune12 removed important numeric and collection bottlenecks. The remaining gap
is not explained simply by “an interpreter versus a JIT.” Some LambdaJS workloads
still execute MIR through an interpreter, while native workloads repeatedly
perform boxed conversions, name resolution, metadata traversal, and generic
property operations. Large generated functions also make compilation expensive.

Tune13 has five objectives, in priority order:

1. **Make MIR mean native execution.** The existing AST interpreter is the JS
   interpreter. Remove every LambdaJS MIR-interpreter selection path, including
   diagnostic switches, automatic thresholds, and inherited global mode.
2. **Finish complete native numeric regions.** Preserve values from array load
   through arithmetic/comparison to store or return, not just one optimized
   helper surrounded by boxing and other generic helpers.
3. **Make ordinary semantic operations cheap.** Preserve names as IDs, avoid
   descriptor allocation for internal inspection, use guarded field layouts,
   and remove unnecessary caller-side preparation for genuinely leaf operations.
4. **Reduce compiler time and peak memory.** Address large initializer MIR and
   root-planner scaling without weakening precise ownership.
5. **Measure the residuals honestly.** Attribute each improvement to its immediate
   predecessor and a fixed control; distinguish execution, startup, memory,
   helper coverage, and the still-unmet QuickJS milestone.

The Tune12 milestone of full-LambdaJS/QuickJS geometric mean **≤0.80x** remains a
stretch performance objective, not a promised consequence of these changes.
The Phase 2 snapshot is 2.936473x, requiring approximately another **3.67x**
improvement to reach that objective. Implementation completion and milestone
attainment are reported separately.

### 1.1 Authority and non-goals

The [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md) govern the proposal:

| Authority | Constraint |
|---|---|
| **S1.11**, **D1.3v3** | Hosted JS keeps JavaScript coercion, object, evaluation-order, and exception semantics. Lambda semantic shortcuts do not transfer to JS. |
| **D8.1.3v11** | AST interpretation and native MIR execution are distinct. Selected JS MIR units never execute through MIR's interpreter. |
| **D2.4.1–D2.4.3**, **D3.3.2v2** | Native carriers require sound facts or guarded entry, with complete semantic fallback. |
| **D3.4.4v2**, **D4.6.1v2–D4.6.2v2** | Static names are linked into the owning context's name domain; preserve ownership and identity. |
| **D5.3**, **D5.4.1**, **D6.2.2v2** | Precise roots, explicit owners, and the established call-entry contract remain authoritative. |
| **D8.4.1v2–D8.4.3v2** | No mutable property/call-site inline caches; preserve explicit call and completion behavior. |
| **D8.2.3**, **D8.2.5v2**, **D8.6.1–D8.6.3** | Reuse indexed analysis and common lowering; verify finalized MIR, zero-slack ratchets, and dynamic rooting. |

This is not an MVP promotion, a new NaN-boxed full-JS ABI, a bytecode engine,
a collector replacement, or a new AUTO/OSR/mixed-tier implementation. It does not
remove the MIR interpreter from other language profiles or modify vendored MIR.
No benchmark-name/pattern whitelist, skipped semantic test, conservative stack
scan, or relaxed MIR ratchet is an optimization technique.

## 2. Tune12 retrospective: what improved

### 2.1 Phase 1 established useful building blocks

Phase 1 introduced node-backed collection indexing, broader native numeric
admission, reduced unnecessary TDZ/root homes in proved numeric frames, ordinary
Number/array helper heads, guarded array/field operations, and RegExp bulk paths.
Its actual frozen-control paired geometric ratio was **0.680706x**: 31.9% less
geometric-mean time, but only 8.5% less total median time
(212,889.908 → 194,782.414 ms). Large numeric wins did not eliminate the dominant
text/object costs. The full-JS/QuickJS snapshot improved from 5.082332x to
3.481106x. These are different comparisons, not interchangeable ratios.

The large `sum`/`sumfp` and `fib`/`fibfp` wins belong principally to Phase 1.
Collection indexing and RegExp bulk work also helped `knucleotide` and
`regexredux`. Existing callable-entry selection was verified, not replaced by a
new faster dispatcher. Several changes had only cumulative timing evidence.
See the [Phase 1 closeout](../../test/benchmark/js_mvp/tune12/final_closeout.md).

### 2.2 Phase 2 attribution correction

The published Phase 2 paired “control” is byte-identical to the original
**pre-Tune12** control, not the Phase 1 final or a fresh Phase 2 starting-tree
build. Binary hashes, rather than an archive's filename or README description,
establish the comparison:

| Artifact | SHA-256 |
|---|---|
| Original control / published Phase 2 paired control | `4f158287b8f342ef703b946a47507d18dcfedf89748eaaf50cc9c9bc30aef972` |
| Actual Phase 1 final, `lambda-tune12-final-1864e219659a` | `02593fe504c5030a04e536e2afe738da62cb9c9d9253b258e8a23db55e9201c6` |
| Published Phase 2 paired candidate, `lambda-tune12-p2-final-release` | `f6d61644742644d792ff7fd83cb5bdaac7d57ada3ca903740638b297ec739e5c` |
| Phase 2 standard final, `lambda-tune12-p2-final-standard` | `467920b3a9180623c2512dfab99d8327098c2864287dcfed934af9fea25fb957` |

Consequently the published **0.562698x** paired geometric ratio and
**0.660772x** total-median ratio measure **Phase 1 + Phase 2 cumulatively**.
They cannot support incremental Phase 2 attribution. The archived measurements
remain useful; their interpretation must change. See the
[paired artifact](../../test/benchmark/js_mvp/tune12/phase2/p2-final-full-paired-release.json)
and [publication snapshot](../../test/benchmark/js_mvp/tune12/phase2/final_phase2.json).

The analysis used the archived Phase 2 binary above, not a rebuild of merged
HEAD `815e5021277b845b609619627223cc618a4845bb`. Tune13 must build its own actual
starting-tree control and measure intervening source changes separately.
The new diagnostics in §§2.4–3 use the **standard final**; the earlier published
paired run used the distinct paired candidate. Do not interchange those hashes.

### 2.3 Phase 1 → Phase 2 snapshots

Across all 63 rows, the separate-session LambdaJS snapshots have geometric ratio
**0.842164x** and total median time **194,662.839 → 140,572.512 ms**, a 27.8%
reduction. These are descriptive snapshots, not an interleaved causal test.

| Workload | Phase 1 ms | Phase 2 ms | Reduction | Phase 2 / QuickJS |
|---|---:|---:|---:|---:|
| `primes` | 1,672.699 | 94.115 | 94.4% | 2.44x |
| `fft` | 25.109 | 5.528 | 78.0% | 3.45x |
| `text_search` | 96,100.986 | 53,826.354 | 44.0% | 3.67x |
| `havlak` | 19,269.777 | 15,930.473 | 17.3% | 8.10x |
| `microdiff` | 1,218.023 | 961.372 | 21.1% | 15.46x |
| `three_way_merge` | 14,606.329 | 12,625.169 | 13.6% | 4.33x |
| `log_pipeline` | 15,138.452 | 14,309.190 | 5.5% | 3.14x |
| `hyphen` | 582.295 | 595.875 | −2.3% | 18.21x |
| `prettier_ast` | 4,755.753 | 4,600.316 | 3.3% | 6.01x |

`text_search` accounts for 38.3% of Phase 2 total time and 78.2% of the saved
milliseconds in this snapshot comparison. It, Havlak, `log_pipeline`, and
`three_way_merge` together account for **68.8%** of remaining total time. Use
that concentration to prioritize elapsed-time work, without confusing it with
the equal-log-weight geometric metric.

### 2.4 Actual Phase 1 → Phase 2 interleaved diagnostic

A subsequent release comparison used the actual Phase 1 final and Phase 2 final:
11 alternating pairs per row, 11 rows, all **121 pairs output-equivalent**.

| Row | Phase 2 / Phase 1 | One-sided 95% bootstrap upper |
|---|---:|---:|
| `fib` | 0.9803 | 1.0209 |
| `fibfp` | 1.0086 | 1.1052 |
| `sum` | 0.9935 | 1.0313 |
| `sumfp` | 1.0091 | 1.0582 |
| `fft` | 0.2276 | 0.2391 |
| `mbrot` | 0.8826 | 1.0170 |
| `list` | 1.0918 | 1.1486 |
| `binarytrees` | 0.9952 | 1.0714 |
| `primes` | 0.0584 | 0.0596 |
| `hyphen` | 0.9923 | 1.0643 |
| `crypto_sha1` | 1.0288 | 1.0860 |

FFT and primes are strong incremental wins. Sum/fib are broadly unchanged.
Hyphen is flat; the separate-snapshot binarytrees slowdown did not reproduce.
`list` needs regression investigation, with only 4/11 candidate wins. An upper
confidence bound alone does not establish a statistically significant regression.
This subset is not a substitute for a complete Phase 2 causal matrix.

### 2.5 Structural work that remains

| Phase 2 area | Landed benefit | Remaining deliverable carried into Tune13 |
|---|---|---|
| P2-1 ordinary reads | Early host rejection and ordinary-array length head | Keep names/length/numeric values native through callers; generic read entry still traverses other machinery. |
| P2-2 mutation relevance | Cheap class/prototype relevance checks | Some classless ordinary-map fallback remains; do not keep optimizing yesterday's dominant scan without new samples. |
| P2-3 numeric locals | Locals can specialize despite boxed/void returns | Search functions still fail or incompletely exploit numeric admission. |
| P2-4 load–compute–store | Important typed-load/store and FFT/primes improvements | Boxed typed setters, reboxed load consumers, and generic arithmetic remain. Complete native regions are only partially covered. |
| P2-5 loop reuse | Immutable typed-element kind can be hoisted | Length/data remain per access; effect-bounded reuse is not implemented merely by hoisting kind. |
| P2-6 fields/calls | Existing guards and semantic fallbacks retained | Constructor-assigned field coverage was deferred. Mutable field values do not prohibit immutable layout predictions guarded against live metadata. |
| P2-7 compiler/memory/builtins | Small RSS census and some GC observations | Compiler allocation attribution, root-planner scaling, and residual RegExp/string costs remain open. |

Tune12's “complete by refusal” and bounded completion labels must not imply that
its original broader structural exits were demonstrated. A safe refusal is
correct behavior, but not implementation of the excluded optimization. Tune13
keeps each unfinished exit explicit and will not close it with a cumulative
benchmark win from a different path.

## 3. Why QuickJS and MVP remain faster

### 3.1 The comparison, and its limits

The Phase 2 full-population snapshot is **2.936473x QuickJS** geometrically and
**3.311381x** by total medians; LambdaJS wins 11/63 rows. Outliers include
`hyphen` 18.21x, `revcomp` 17.93x, `microdiff` 15.46x, `quicksort` 13.28x,
`towers` 11.15x, and `nqueens` 10.93x. This is not one missing floating-point
optimization.

A fresh 15-row, three-sample diagnostic ran full JS and MVP from the same Phase 2
binary alongside QuickJS 2026-06-04. Selected **full JS / MVP** ratios:

| Nearer parity | Ratio | Still expensive | Ratio |
|---|---:|---|---:|
| `sum` | 1.065x | `fft` | 5.964x |
| `sumfp` | 1.104x | `havlak` | 3.526x |
| `fib` | 1.356x | `quicksort` | 4.617x |
| `primes` | 1.252x | `microdiff` | 7.589x |
| `binarytrees` | 0.932x | `hyphen` | 8.087x |
| `gcbench` | 0.911x | `regexredux` | 13.704x |

Full JS already wins the last two left-hand rows. MVP has a private NaN-boxed
ABI and simpler object/call machinery within a narrower synchronous benchmark
semantics boundary; it is not a drop-in full ECMAScript/DOM/module/async runtime.
The historical MVP acceptance used a different QuickJS version. Dividing those
historical ratios by today's full-JS ratio is not a valid full-JS/MVP comparison.

QuickJS documents a compact bytecode engine with JS-specific numeric fast paths,
shared property shapes, array specializations, and atomized names. These reduce
the work per semantic operation. On 64-bit systems its documented `JSValue` is
128 bits, so “QuickJS wins because it uses a smaller NaN box” is not an adequate
explanation. See [QuickJS internals](https://bellard.org/quickjs/quickjs.html#Internals).

**Inference from the local evidence:** native code can lose when its loop still
calls a general-purpose runtime pipeline for each small JS operation. Removing
dispatch alone does not remove coercion, lookup, scalar-home work, or call-edge
preparation. Conversely, full LambdaJS's `flt2it` already stores most doubles
inline; the diagnosis is **not** that every floating-point result allocates.

### 3.2 Profile-guided bottlenecks

These release CPU samples are diagnostic, not timing runs. Self and inclusive
counts are kept distinct; sleeping main-thread samples are excluded. They are
sampled windows, not exact whole-program accounting, and JIT/inlined attribution
is incomplete.

| Workload / window | Observation | Action supported by evidence |
|---|---|---|
| `text_search`, 3,901 worker samples during execution | `js_get_number` + `item_try_to_double` + `flt2it`: 701 self samples, 18.0%. Named/key/reference heads: 420, 10.8%. No GC samples. | Complete array-number loops and preserve native operands/lengths. Its hot searches operate on numeric character-code arrays, so this is not evidence that UTF-16 decoding dominates. |
| Havlak, 4,289 execution samples | Object metadata, shape/name lookup, and prototype machinery are prominent. `js_call_entry_generic` itself: 119 self samples, 2.8%. Intrinsic mutation: 42 inclusive, about 1%; collection: 235 inclusive, 5.5%. | Prioritize fields/names/metadata. Inclusive time below a call wrapper is not all dispatcher overhead. Do not infer that GC or the previously expensive mutation scan dominates now. |
| `microdiff`, 721 samples under compiled execution | Name classification/pool lookup/hashing: 81 + 49 + 45 self samples, about 24%. For-in liveness reaches descriptor materialization. | Retain NameIds and use nonallocating descriptor inspection, including ordinary arrays. |
| FFT, finalized MIR | Native `four1` exists, but has eight static typed-setter sites and twelve each of kind/length/data queries; generic arithmetic persists. | Finish native consumers/stores and prove metadata reuse. Static sites include miss arms and are not dynamic call counts. |
| `hyphen`, 2,543 whole-process worker samples | 1,973 samples under lowering (77.6%); frame finalization has 1,012 inclusive (39.8% of all samples), inside that lowering time. | Reduce generated initializer size and root-planner work. These percentages overlap and must not be added. |

The current focused Hyphen execution profile refines the callback portion of that
diagnosis. After the dense-`reduce` leaf, 41,043 compiler-pending functions are
created in one run. Their factories were still publishing `name` and `length`
into writable backing maps before the callback executed. Deferring publication
to finalization, then keeping those two ordinary descriptors virtual until an
observable property operation needs them, removes that repeated allocation while
retaining the real descriptor at every observation boundary. This is governed by
**D5.3**, **D6.2.2v2**, and **D8.4.1v2–D8.4.3v2**: roots survive allocation,
the ordinary function-property shape remains authoritative, and this is not a
mutable site cache.

A fresh, unpaired three-sample release comparison changed the LambdaJS warm
Hyphen median from 130.201 ms to 105.083 ms (−19.3%); MVP was 57.020 ms and
QuickJS 26.008 ms in the exact final release. The profile run took 100.627 ms and recorded
41,043 `mir_lazy_function_metadata` admissions, with the pre-existing reduce,
name, string, and call counters otherwise unchanged. This is strong local
attribution evidence, not a full-matrix or paired acceptance result. LambdaJS
remains about 4.04x QuickJS and 1.84x MVP on that fresh focused run.

Subsequent focused changes are deliberately reported as a sequence of unpaired
diagnostics, not an additive speedup claim. A light native MIR call whose
finalized body cannot observe an activation-local fact now retains one callee
root and forwards its rooted arguments directly to the body. It reduced a fresh
Hyphen median to 103.843 ms and recorded 197,234 direct activations. Reducing
the reducer's callback-adapter roots to its one long-lived pre-rooted span was
semantically useful but measured 103.995 ms, not a credible isolated win.
Narrowing the URI-escape string-cache admission removed root registration from
ordinary ASCII concatenation and measured 102.895 ms. The 53,456 ASCII concat
hits in that run therefore do not establish string concat as the dominant
residual.

Source and trace audit exposed receiver-only bound calls in the Hyphen regex
predicate. Their forwarding path now preserves the caller's rooted argument span
instead of allocating/copying a second span; the new trace records 133,287
admissions. Its fresh median is 102.711 ms (MVP 56.614 ms, QuickJS 26.152 ms),
only 0.2% below the URI-cache run. This confirms adapter work was removed under
**D5.3** and **D6.2.2v2**, but not that it closes the workload gap.

The original five-second process sample captured native MIR linking rather than
guest execution and is not residual evidence. A delayed sample of the extended
4,096-round fixture captured 4,303 worker samples in guest execution. Its
largest relevant cells are GC collection (428), well-known-name classification
(277), environment scalar rehoming (181), generic keyed get (171), and hash-map
work (148); these inclusive sample cells overlap and are not percentages. This
supports allocation/GC, canonical-name, and generic-property work as the next
investigation order while preserving `NameId` identity under **D4.6.1v2**.

The ownership census made one allocation cost concrete: one Hyphen execution
installed 77,014 and released 50,058 external JS dense-array buffers. The former
reverse-ownership `HashMap` hashed each install and release. T13-8 replaces it
with an intrusive buffer list, retaining exact sweep/full-reset cleanup and
releasing the old external buffer before generic collection growth publishes a
GC-owned replacement. Its fresh release median is 94.930 ms (samples
94.787/94.930/96.152), versus 102.711 ms for the preceding bound-forward
binary: a 7.6% **unpaired** improvement. MVP is 57.438 ms and QuickJS 26.120
ms in that run. This removes an attributed allocation-owner cost under **D5.3**;
it does not establish that all GC/allocation cost, or the remaining name and
property work, has been removed.

The guest sample's canonical-name stack led to two scoped helper changes under
**D4.6.1v2**. `name_classify_ordinary` now computes FNV routing hash,
ASCII-ness, and array-index eligibility in one byte walk. Its fresh Hyphen
median is 92.748 ms (93.965/91.679/92.748), a 2.3% unpaired change from the
ownership binary. `name_pool_create_strview` had then probed the well-known
catalog before each parent and local dynamic-pool lookup, while the public
lookup also probed it again at each recursive parent boundary. The new split
performs catalog lookup once at the specified catalog boundary and searches
local/parent scopes without it; creation retains catalog-first then
parent-before-child precedence. The resulting fresh median is 91.420 ms
(91.420/89.994/91.967), 3.7% below the ownership binary and 1.4% below the
classifier-only binary, all unpaired. The simultaneous MVP samples include a
77.734-ms outlier (median 58.842 ms), so they are recorded but not used to
claim a cross-engine ratio change; QuickJS median is 26.839 ms.

A deliberately narrow data-zone experiment then changed only the small,
fresh sparse-length array constructor path. The new Array is rooted across its
collecting `heap_data_alloc_uninit` call, has the GC-owned buffer attached before
its reachable slots are initialized, and retains the external-buffer path for
growth, species, and other unsupported cases. This satisfies precise ownership
under **D5.3** rather than relying on a native-stack scan. The profile replaced
77,014 external-buffer installs with 77,014 GC-data allocations (and zero
external installs). Its 90.978-ms release median (98.516/89.317/90.978) is only
0.5% below the 91.420-ms predecessor, unpaired. Removing the owner bookkeeping
does not by itself remove the workload's allocation/collection cost.

The generic map Get path also had an avoidable repeated operation: an id-less
dynamic String was canonicalized inside every ordinary-own probe, so an inherited
Get could re-intern the same spelling at each prototype level. The outer,
already-rooted Get boundary now canonicalizes once after ToPropertyKey and carries
that NameId through the shared own/prototype kernels. The property kernel remains
the sole authority for descriptors, accessors, tombstones, proxies, and misses;
the dynamic-key/prototype-get contract covers the observable inherited getter.
This applies **D4.6.1v2** and **D5.3** without a mutable property cache. The
following release median is 90.268 ms (90.268/90.987/88.544), 0.8% below the
GC-buffer release, again an unpaired focused diagnostic. Its profile completion
is 89.545 ms and preserves checksum `1183296`; it is structural evidence, not a
claim that property canonicalization alone explains the remaining gap.

The Phase 1 Havlak mutation sample was much larger, but different sampling windows
do not establish an exact causal percentage reduction. Reprofile after the
native-only policy change before fixing the next optimization order.

### 3.3 Hyphen exposes both the execution-policy cliff and compiler cost

The diagnostic source is 118,507 bytes, with 81 functions and **289,375 finalized
MIR instructions**. `js_main` alone has 264,961 instructions, 17,402 safepoints,
and 20,391 root stores. Under the existing policy, passing 100,000 module
instructions selects **MIR interpretation for the whole module**. A benchmark
runner field saying `tier=jit` did not override this JS-specific policy.

| One diagnostic run per mode | Existing default | `LAMBDA_JS_LARGE_INTERP=0` |
|---|---:|---:|
| Parse | 43.561 ms | 59.346 ms |
| MIR lowering | 2,384.141 ms | 2,353.189 ms |
| Link/native generation | 46.723 ms | 934.864 ms |
| Guest timed workload | 590.678 ms | 372.650 ms |
| Complete measured lifecycle | 3,162.170 ms | 3,854.775 ms |

Checksums match. This single observation suggests faster native execution but
higher cold code-generation cost; it is **not an accepted speedup estimate**.
The user's decision settles the policy: remove MIR interpretation, then optimize
the native compiler's cost. Do not retain a size threshold as a hidden fallback.
Explicit AST selection remains available within its admitted semantic coverage.

The shared semantic root planner currently allocates two instruction-by-candidate
bit matrices plus a candidate-by-candidate interference matrix. For `I`
instructions and `C` candidates, these three allocations alone require:

```text
8 × (2I + C) × ceil(C / 64) bytes
```

CPU sampling implicates frame planning. The equation establishes a scaling risk,
not measured RSS attribution. The limited Phase 2 RSS census remains roughly
151 MiB for `microdiff`, 1.45 GiB for `hyphen`, and 1.36 GiB for `prettier_ast`.
Source retention and one sub-millisecond collection do not explain those peaks.
Allocate-by-owner/phase evidence is required before naming a memory root cause.

## 4. Optimizing expensive helpers

### 4.1 Optimize both the helper and its generated caller

The total cost of a helper operation includes:

```text
operand/key preparation + boxing/root publication + call
    + helper dispatch/lookup/coercion/allocation
    + completion checks + scalar/result adoption
```

A fast branch inside a `MAY_GC` helper removes only some interior work. If the
compiler still emits generic operand preparation and root/result handling at
every call, the common path can remain expensive.

Use this shape where justified:

```text
evaluate operands once
  -> non-observable proof/guard
       hit: audited leaf operation, native result if required
       miss: prepare the full call edge, invoke the existing semantic kernel
  -> merge with valid representation, ownership, and completion
```

The miss must occur before observable work or continue from an explicitly valid
semantic point. Never repeat a getter, proxy trap, key conversion, argument
expression, or coercion. Effectful guards cannot be treated as free predicates.
Factor one underlying primitive; do not copy a semantic implementation into a
second “fast runtime.” Search for existing helpers and promote module-local
helpers to the appropriate header when reuse requires it.

### 4.2 Preserve information instead of reconstructing it

| Existing repeated work | Replacement | Required proof |
|---|---|---|
| `NameId → spelling → classify/hash → NameId` | Carry linked `NameId`/name metadata from source or enumeration to lookup. | Correct realm/context owner; symbols and dynamic keys remain distinct. |
| Full descriptor object solely to inspect a flag | POD/internal descriptor inspection from the same descriptor authority. | Proxy/exotic handling and for-in visibility/order remain exact. |
| Number load → Item → numeric helper → Item → numeric conversion | Keep an I64/F64 carrier through admitted producers and consumers. | JavaScript Number/BigInt/coercion rules, overflow, NaN, and negative zero. |
| Typed-array kind/length/data checks at every access | Reuse a receiver witness only within a proved effect/alias region. | No detach/resize/replacement/reentry can invalidate the witness. |
| Repeated spelling/shape walks for a known field | Immutable layout prediction plus live shape/descriptor guards. | Correct own/inherited/accessor behavior; no mutable site cache. |
| Reclassify the same built-in regex pattern on every test | Store immutable classification with the compiled regex owner. | Observable `source`, flags, `exec`, symbol protocol, and `lastIndex` behavior stay unchanged. |

Resolve algorithmic rescans and unnecessary allocations before attempting tiny
branch rearrangements or blanket inlining. Native code size and instruction-cache
pressure are costs too; keep uncommon semantic paths shared.

### 4.3 Truthful helper effects and precise ownership

An audited leaf may be nonallocating, nonreentrant, and nonthrowing. These are
separate properties and must be proved through its entire call graph. `NO_GC`
alone does not establish either of the other two. A generic semantic helper
remains conservatively effectful even if most calls hit a no-GC head.

Use the existing helper/effect registry and `MirValue`/frame machinery. Publish
roots and stable numeric homes where liveness and real safepoints require them;
do not suppress roots around a potentially allocating fallback. A native numeric
carrier is not a GC pointer, but any boxed pointer-scalar materialized on a miss
must have a valid owner and lifetime. No raw array data pointer survives an
invalidating call or safepoint without an established stability contract
(**D2.4**, **D5.3**, **D8.4.3v2**).

### 4.4 Prove that the hot path actually uses the optimization

For each changed semantic operation, retain four kinds of evidence:

1. **Reachability:** real workload admission plus hit/miss counters in a separate
   diagnostic run; failures have useful refusal reasons.
2. **Structure:** finalized MIR for the admitted arm and its fallback, showing
   removed helper/conversion/root traffic. Static call-site counts alone do not
   prove executed savings.
3. **Correctness:** hostile miss cases, effect order, exception identity, and
   forced GC, not just Number-only successes.
4. **Performance:** release paired target and miss-heavy cases, whole-population
   drift, compiler time, code volume, and peak memory.

Do not sum overlapping sample percentages into a promised speedup. Improving a
helper's self time can expose another caller or metadata cost immediately.

## 5. Native-only MIR execution contract

Under **D8.1.3v11** and **JSI16v2**:

- `JS_EXECUTION_BACKEND=ast` continues to select the existing AST interpreter,
  subject to its admission rules. This proposal does not claim full AST feature
  coverage or silently expand it.
- Selecting MIR, including the current unset/default backend, selects generated
  **native machine code**. This applies to script entries, functions, eval and
  dynamic functions, CJS/ESM modules, cached/batch execution, and document scripts.
- There is no LambdaJS MIR-interpreter diagnostic mode or automatic threshold.
  Legacy interpreter flags must not reactivate it. For a JS command, reject an
  explicit incompatible `--mir-interp` request with a clear message. Retired JS
  environment knobs no longer select a backend; document their retirement and
  expose the effective backend in diagnostics.
- A selected MIR unit is not rerouted to AST because it is large or native
  compilation fails. Report compilation/resource failure through the established
  error boundary; never replay user effects. A future explicit admission/AUTO
  design is separate work and cannot be smuggled in as this optimization.
- Eager generation is the initial default. Existing opt-in native lazy generation
  may remain only with correct context/code lifetime and entry publication; a
  lazy thunk compiles then enters native code, never MIR interpretation. Making
  it the default is outside this proposal unless its full ownership gates pass.
- Shared MIR support for Lambda or another guest is not deleted or globally
  disabled. JS uses explicit native compile-unit configuration and records
  generator initialization on the owning artifact, rather than temporarily
  changing a process-global flag around nested calls.

### 5.1 Selection and lifetime inventory

| Current location | Required work |
|---|---|
| `lambda/js/js_mir_entrypoints_require.cpp` | Remove explicit-env and source/module/document-size MIR-interpreter branches. Remove the size-triggered AST diversion associated with the old interpreter policy. Make `js_mir_link_main` native-only. |
| `lambda/js/js_mir_eval_lowering.cpp`, `js_mir_module_batch_lowering.cpp` | Native link/init on eval, dynamic function, module, and batch paths; no inherited `g_mir_interp_mode` selection. |
| `lambda/js/js_mir_internal.hpp`, `transpile_js_mir.cpp` | Remove JS-only interpreter configuration and stale comments; retain useful volume diagnostics independently of backend selection. |
| `lambda/runtime/mir.c`, `mir_policy.hpp` | Introduce/reuse explicit compile-unit mode/init contracts for JS. Pair generator init/finish according to owned state, not the global's value at cleanup. Preserve other profiles. |
| `lambda/main.cpp` | Remove JS dependence on forced-document interpretation and reject incompatible JS CLI requests without changing unrelated Lambda modes. |
| Runtime artifact caches and test/benchmark launchers | Validate native entry/artifact identity on reuse; report actual JS backend and eager/lazy generation, not merely a runner's `tier=jit` label. Retire misleading JS interpreter options without masking tests. |

Source-size, AST-size, and finalized-MIR counters may still diagnose compiler
cost. A native optimization-level choice is distinct from execution-mode
selection and must be reported and measured separately.

## 6. Evidence and reproducibility notes

Durable predecessor evidence:

- [Tune12 Phase 1 final matrix](../../test/benchmark/js_mvp/tune12/final.json),
  [paired comparison](../../test/benchmark/js_mvp/tune12/final_paired.json), and
  [closeout](../../test/benchmark/js_mvp/tune12/final_closeout.md).
- [Phase 2 publication](../../test/benchmark/js_mvp/tune12/phase2/Overall_Phase2.md),
  [matrix](../../test/benchmark/js_mvp/tune12/phase2/final_phase2.json), and
  [cumulative paired comparison](../../test/benchmark/js_mvp/tune12/phase2/p2-final-full-paired-release.json).
  Read their control labels with the correction in §2.2.
- [Typed consumer r3](../../test/benchmark/js_mvp/tune12/phase2/p2-4-load-consumer-r3.json)
  and [kind-hoist r4](../../test/benchmark/js_mvp/tune12/phase2/p2-5-loop-kind-r4.json)
  do not establish isolated wins: the recorded FFT ratios/upper bounds are
  1.0159/1.0631 and 0.7685/1.8356 respectively. Retain negative/uncertain evidence.

The post-Phase-2 analysis additionally used local scratch artifacts, summarized
in this proposal so its conclusions do not depend on scratch-file survival:

- `temp/tune12_phase2_analysis_true_paired.json`: actual P1/P2 11-row comparison.
- `temp/tune12_phase2_analysis_focused.json`: 15-row full-JS/MVP/QuickJS refresh.
- `temp/tune12_phase2_analysis_{text_search,havlak_execution,microdiff,hyphen}.sample`:
  CPU windows described in §3.2. The separate `havlak.sample` captured teardown
  and is not evidence for the execution bottleneck claims.
- `temp/tune12_phase2_analysis_{text_search,havlak,fft}.mir`: structural diagnostics.
- `temp/tune12_phase2_analysis_hyphen_{timing,force_jit}.log`: single-run timing
  observations in §3.3.

These scratch diagnostics are **not a durable Tune13 acceptance archive**. T13-0
must reproduce/archive any evidence used to accept a change under
`test/benchmark/js_mvp/tune13/`, with unique revision/control identities and exact
commands. Exploratory output stays under `./temp/`; never overwrite Tune12 data.

The current exploratory artifacts are
`temp/js_tune13_hyphen_reduce_dense.json`,
`temp/js_tune13_hyphen_lazy_metadata_final.json`,
`temp/js_tune13_hyphen_mir_light_direct_final.json`,
`temp/js_tune13_hyphen_prerooted_reduce_final.json`,
`temp/js_tune13_hyphen_uri_cache_final.json`,
`temp/js_tune13_hyphen_bound_forward_final.json`, and
`temp/js_tune13_hyphen_array_ownership_fixed_final.json`,
`temp/js_tune13_hyphen_name_classification_final.json`, and
`temp/js_tune13_hyphen_name_lookup_final.json`,
`temp/js_tune13_hyphen_gc_items_final.json`, and
`temp/js_tune13_hyphen_property_key_final.json`. The corresponding
available traces include `temp/js_tune13_hyphen_{lazy_metadata,uri_cache,
bound_forward}_profile.tsv` and
`temp/js_tune13_hyphen_array_ownership_profile.trace.tsv`,
`temp/js_tune13_hyphen_gc_items_profile.tsv`, and
`temp/js_tune13_hyphen_property_key_profile.tsv`.
`temp/js_tune13_hyphen_execution.sample` captures startup/link work; the delayed
guest sample is `temp/js_tune13_hyphen_array_ownership_guest_execution.sample`
and uses the checksum-adjusted extended fixture. These are distinct binaries and
fresh three-sample runs: the observed 130.201 → 90.268-ms trend is intentionally
unpaired. They guide the next package but do not substitute for T13-0's archived
control matrix.

## Appendix I — Implementation sequence

The detailed checklist and acceptance gates live in the
[JS Tune13 implementation plan](../impl/JS_Tune13_Impl.md). Landed work is
recorded there; unfinished package exits remain binding.

| Package | Required outcome |
|---|---|
| T13-0 | Verified provenance, actual starting-tree control, operation/compile/memory census. |
| T13-1 | Native-only MIR routing and explicit native artifact lifetime across all JS entry paths. |
| T13-2 | Reduced root-planner scaling and semantically safe large-initializer MIR volume. |
| T13-3 | End-to-end native ordinary-array and typed-array numeric regions. |
| T13-4 | Effect-bounded reuse of length/data/receiver validity, beyond kind-only hoisting. |
| T13-5 | NameId continuity, canonical-name helper work, and nonallocating internal descriptor/enumeration inspection. |
| T13-6 | Guarded constructor-field coverage and separately measured call-edge improvements. |
| T13-7 | Profile-gated RegExp/string fixes, with explicit attribution and dispositions. |
| T13-8 | External JS array-buffer ownership without per-operation hash lookup, then regression resolution, complete correctness/performance evidence, and consolidation. |

Keep two fixed controls: **A**, the actual pre-Tune13 default, and **B**, the
native-only T13-1 checkpoint. Publish B/A policy cost, final/B tuning gains,
and final/A user-visible change, plus each package against its predecessor.
Correctness, forced GC, zero-slack MIR, execution, cold startup, peak memory,
and cross-profile drift all gate closeout. A partial workload win cannot close
an incomplete structural requirement or the full-population QuickJS milestone.

## Appendix S — Superseded direction

The earlier analysis suggested selecting different execution policies for large
cold initializers and small hot functions. The user has now resolved that choice:
**LambdaJS MIR interpretation is retired completely**, not retained as a cold-code
or diagnostic mode. AST interpretation remains an explicit, separately admitted
backend. Compiler-size reduction, valid native lazy generation, and cheaper
native operations are the remaining ways to reduce MIR cost.

This supersedes the MIR-interpreter recommendations in
[Tune6 AST/lazy MIR](Transpile_Js_Tune6_AST.md), but does not invalidate its measured
compiler costs. The revised formal ruling is **D8.1.3v11** and the corresponding
working decision is **JSI16v2** in
[the JS interpreter design](../Lambda_Design_JS_Interpreter.md).
