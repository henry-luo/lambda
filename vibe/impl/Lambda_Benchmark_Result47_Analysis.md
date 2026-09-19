# Result42–47: typed and untyped Lambda performance

Analysis date: 2026-09-19. Source inspected: `c08b933aa`. No runtime or
canonical benchmark source changed. Fresh diagnostics use `make release`,
AC power, and `LAMBDA_TIER=jit`; artifacts are in
`temp/result47_analysis/`. Historical figures below come from the checked-in
result JSONs, not a new full-suite run.

## 1. Main findings

1. Typed Lambda improved substantially from Result42, but the last rounds
   mostly improved an already competitive numeric subset. The large gaps
   remain in helper calls, ownership operations, allocation, and strings.
2. Untyped Lambda is approximately flat over Result44–47: on 60 rows after
   excluding changed entry sources/reference ports, C2MIR-normalized time
   falls only **5.1%**, versus **15.5%** for typed Lambda. It still misses
   native array stores and loses element/scalar facts across boundaries.
3. Some apparent regressions are measurement drift or correctness repairs.
   `splay` is the strongest recent unresolved regression signal. Typed
   `fannkuch` has an earlier recorded bisection to excess ownership guards.
   `puzzle`'s old typed speed was semantically wrong.
4. Fresh diagnostics identify large opportunities without changing an
   algorithm: two counter annotations reduce untyped `nbody` time by 39%;
   carrying `int[]` across quicksort's array binding and parameters reduces
   time by 91%; declaring text-search producer returns and consumer
   parameters reduces time by 89%. These are diagnostic source variants,
   not engine fixes.

## 2. What moved in the snapshots

All figures use the same 63 benchmark names and self-reported execution
time, excluding startup and compilation. Ratios are geometric means of
per-row ratios; lower is faster.

| Result | Date | Untyped / C2MIR | Typed / C2MIR | Untyped / Node | Typed / Node |
|---|---|---:|---:|---:|---:|
| 42 | Sep 10 | 8.06x | 6.53x | 1.56x | 1.27x |
| 43 | Sep 13 | 7.13x | 4.64x | 1.36x | 0.88x |
| 44 | Sep 14 | 6.81x | 4.41x | 1.32x | 0.86x |
| 45 | Sep 15 | not measured | not measured | 1.34x | 0.85x |
| 46 | Sep 17 | 6.83x | 4.26x | 1.20x | 0.75x |
| 47 | Sep 18 | 6.35x | 3.59x | 1.26x | 0.71x |

**Result46 is a poor raw-time baseline.** Relative to Result44, untyped
Lambda time rises 12.6%, but C2MIR rises 12.2%, QuickJS also slows, and Node
rises 24.2%. Result47 reverses much of this: untyped time falls 15.0%,
typed 22.9%, C2MIR 8.6%, and Node 19.1%. Consequently untyped/Node worsens
even while untyped Lambda becomes faster in absolute time.

Normalizing each Lambda row by its corresponding C2MIR time drift is a
useful screen, not a causal A/B experiment. In particular, the C
`r7rs/nqueens` port changed between Result46 and Result47; its 2.63x C
slowdown is not host drift. Typed `awfy/cd` also changed source, and
`text/three_way_merge2.ls` changed between Result44 and Result46.

| Comparison | Common names: normalized U / T | Excluding changed entry sources/reference ports: normalized U / T |
|---|---:|---:|
| R42 → R47 | 0.788 / 0.550 | **0.800 / 0.570**, 60 rows |
| R44 → R47 | 0.933 / 0.814 | **0.949 / 0.845**, 60 rows |
| R46 → R47 | 0.930 / 0.843 | **0.946 / 0.875**, 61 rows |

This narrows the latest round's headline typed improvement from 15.7% to
12.5% lower normalized time on the stricter population. The filter checks
entry-source changes and C ports, not every transitive compiler/library
dependency. Result47's `crypto_sha1` and `pidigits` Lambda cells also came
from a separately repaired release, as recorded in its metadata.

Reproduction: `python3 temp/result47_analysis/history.py` writes
`history.json`, including per-row changes and the source exclusions.

## 3. Where regression is and is not established

| Row / variant | Relevant measurements, ms | Assessment |
|---|---|---|
| splay, untyped | R44 313.381 → R46 317.863 → R47 341.701 | Strong unresolved signal: +7.5% R46→47 and +9.0% R44→47; C is approximately stable. |
| splay, typed | 295.563 → 305.914 → 325.548 | Same signal: +6.4% R46→47 and +10.1% R44→47. |
| towers, untyped | R44 1.177 → R47 1.327 | +12.7% raw, +8.8% C-normalized; typed stays 0.326 → 0.322. Investigate the untyped call/wrapper path. |
| pidigits, untyped | R44 0.296 → R47 0.333 | +12.5% but only 0.037 ms; repaired snapshot cell and short duration make replay necessary. |
| permute, untyped | R44 0.560 → R47 0.608 | +8.6%, secondary replay candidate. |
| fannkuch, typed | R46 0.375 → R47 0.359; R44 0.354 | Snapshot alone is inconclusive; recorded intermediate-binary A/B found a regression after an earlier improvement. |
| puzzle, typed | R42 2.833 → R43 16.140 → R47 13.898 | Required snapshot semantics were restored; the old time is not a valid target. |

For `splay`, all three Result47 samples are above all three Result46
samples in both Lambda variants, while C is 19.360 → 19.052 ms. That
warrants a bisect, but does not itself identify the responsible commit.
The old archives and `temp/t29`/`temp/t30` intermediate binaries referenced
by prior investigations are absent from this workspace, so this analysis
does not claim a fresh historical binary replay or commit-level bisect.

### The confirmed historical fannkuch mechanism

[Tune31 §1.5](Lambda_Impl_Tune31.md) records an interleaved bisection from
approximately 0.31 ms to 0.34–0.37 ms at Tune29's `lambda-items` step,
within the work later committed in `f029ed1a0`. The changed MIR added eight
shared-bit checks and 22 branches around stores into `perm`, `perm1`, and
`count`, with the same call census. This implicates the generalized COW
loop scan/join, not Tune30's leaf inliner.

The current tree retains `mir_premark_loop_cow_bindings` and
`MirCowLoopJoin` in `transpile-mir.cpp:15438`. Its typed fannkuch dump has
nine static shared-bit loads, while the executed COW profile records no
share marks or copies. This supports revisiting fact precision; it does
not independently reproduce the historical eight-check delta.

### Do not restore puzzle's old mutation behavior

[Tune27 §2.1](<Lambda_Impl_Tune27 (done).md>) documents the v42/v43 replay
and the caller-observable bug: the typed native-witness edge omitted the
plain-parameter snapshot. **S9.1.3** requires writes to a plain parameter
to remain local; only `var` parameters borrow the caller's place.

The current typed workload records **174,909 ArrayNum copies / 15,508,598
copied bytes**. Its C port mutates the same arrays in place and restores
them after recursion. A diagnostic Lambda variant making the three array
parameters `var` gives the same benchmark output and runs **11.839 →
2.149 ms**, seven alternating pairs. This is a different ownership contract,
not an optimization that can be silently applied to arbitrary plain
parameters. It shows why the C reference's ownership model matters.

Tune30 §6 / Tune31 §3's later descriptions of puzzle as an unexplained
`int[]` place-copy issue should not supersede Tune27's reproduction: this
source uses `bool[]` plain parameters with documented snapshot semantics.

### Other misleading signals

- Tune27's archived replay already attributed the large R43 untyped
  microbenchmark spikes to host load.
- `havlak`'s R46→47 C-normalized +22% does not establish a new regression:
  R46 C time was unusually high. From R44→47, typed Lambda improves
  62.883 → 59.996 ms and untyped improves 72.894 → 70.202 ms. It remains a
  major performance gap, but should not be labelled a proven regression.
- `fib` is also a weak regression claim: R44→47 typed is 1.300 → 1.312 ms;
  Result46's C samples span 1.264–1.525 ms.

## 4. Why typed Lambda remains behind native C2MIR

The C2MIR column runs independent native C ports through the MIR C frontend.
It is not the removed Lambda C-text backend. Both routes ultimately use
MIR, so the first target is the extra work Lambda emits and executes.
C uses different ownership, numeric, and storage contracts; its timings are
not a universally attainable lower bound for identical Lambda semantics.

In Result47, typed Lambda has 21 rows within 2x C, 17 at 2–5x, 21 at
5–20x, and four above 20x. Scalar `sum`, `sumfp`, `fibfp`, `tak`, `ack`,
and `diviter`, plus `matmul` and `binarytrees`, are around parity or faster.
The remaining gap is concentrated elsewhere:

| Bottleneck | Result47 typed / C examples | Evidence and next mechanism |
|---|---|---|
| Runtime call plus boxing/root traffic for scalar operations | pnpoly 6.2x, crypto_sha1 8.2x, bounce 4.0x, levenshtein 7.2x | Current pnpoly MIR still calls `fn_ne` for two native comparison results. Tune31's MIR review also identifies boxed bitwise operations, `abs`, and character reads. Native lowering removes the whole call boundary. |
| Ownership/path operations on records and arrays | havlak 32.7x, deltablue 21.9x, splay 17.1x, hashmap 13.2x, richards 11.1x | Re-navigation, checked stores, home transport, and real copies. Carry layout/ownership facts on handles and fix their invalidation; separate excess checks from required two-owner snapshots. **D4.4.4v4, D4.4.6, S9.1.2–S9.1.3**. |
| Construction, conversion and allocation | cube3d 13.2x, brainfuck 7.1x, storage 3.7x | Repeated `fill`, returned small arrays, and array admission. Produce the required representation/certificate once. Buffer reuse needs an actual lifetime/escape proof. **D3.3.3v3, D4.1.4v4**. |
| Strings and dynamic collections | hyphen 46.7x, microdiff 22.6x, knucleotide 17.3x, base64 15.1x, prettier_ast 14.6x | Character helpers, split/substring, hash-map updates, construction and concatenation. Require current profiles before assigning percentages to each. |
| Residual code size/register pressure | fft 3.7x, quicksort 3.6x, nbody 2.3x | Native paths still carry null/band/bounds/ownership control flow and cold siblings. Tune30's machine-code investigation found register spills and bounded additional index-check removal below roughly 10% on its probes. |

The boundary cost is multiplicative: boxed operands → publish live roots →
helper → reload roots → inspect/unbox result. A one-instruction operation
can acquire many surrounding instructions. **D5.3.1–D5.3.2** permit
root-traffic reduction at proven `NO_GC` calls and require live precise
roots at collecting calls. Do not assume generic comparisons/conversions
are `NO_GC`; prove their transitive effects or lower a proven primitive
case directly.

This is also why total emitted instruction count is an insufficient gate.
In the current dump, typed nbody's `advance` has more static instructions
than untyped, but its mandatory numeric loop paths retain only `sqrt`
calls; the untyped body still calls mutation helpers. Cold code counts
must be separated from executed paths and register-pressure measurements.

Prior Tune29/30 profile percentages are historical evidence, not fresh
Result47 attribution. Tune31 itself found towers' formerly expensive
`lambda_type_check` sites had become cold witness-miss arms. Optimizing
those sites from the old percentage would target the wrong work.

## 5. Why untyped Lambda misses the gains

| Result47 workload | Untyped ms | Typed ms | Untyped / typed |
|---|---:|---:|---:|
| quicksort | 11.927 | 0.705 | 16.9x |
| nbody | 51.839 | 3.570 | 14.5x |
| text_search | 15,287.0 | 1,747.680 | 8.75x |
| permute | 0.608 | 0.085 | 7.15x |
| primes | 14.728 | 2.230 | 6.60x |
| deltablue | 121.717 | 25.388 | 4.79x |

Untyped Lambda already has specialization; the problem is its coverage and
fact propagation, not the complete absence of inference.

### Fresh release diagnostics

These source-pair experiments isolate missing information. They do not
claim historical engine speedups and do not change the canonical ports.

| Diagnostic change | Control → variant, median ms | Evidence |
|---|---:|---|
| nbody: annotate only the two `j = i + 1` locals as `int` | **45.012 → 27.288**, 0.606x | 7 alternating pairs, identical output in all pairs. |
| quicksort: `int[]` on the array's producer binding and three array parameter positions; scalar code unchanged | **10.387 → 0.953**, 0.092x | 7 alternating pairs, identical output in all pairs. |
| text_search: existing parameter-only typing microbenchmark | **13,185.5 → 32,675.2**, 2.478x | 3 alternating pairs, identical checksum; typing the boundary alone regresses. |
| text_search: search parameters plus `to_codes` return typed `int[]` | **13,003.6 → 1,423.74**, 0.109x | 3 alternating pairs against original untyped source, identical checksum in all pairs. |

The initial quicksort parameter-only diagnostic was rejected with **E207**:
an untyped `array` binding cannot be borrowed as a declared `var int[]`.
It is retained as failed evidence in `source_pairs.json`, excluded from
all speedup claims. Adding the producer binding's contract makes the
diagnostic valid; the original untyped algorithm is unchanged.

**Nbody: reads specialize, stores/counters do not fully follow.** Its
untyped `advance` already has an `_array_witness`, native float arithmetic,
and a direct `sqrt`. Nevertheless the inner `j` counter is boxed: its
comparison calls `fn_lt`/`is_truthy`, its update calls `fn_add`, and stores
using it call `index_assign_cow`. The current executed COW census records
**2,700,000 unique ArrayNum mutations, zero shared copies** for untyped
nbody; typed records no helper-mediated mutations. These are avoidable
helper calls, not evidence that COW data copying is consuming this row.

**Quicksort: the array witness is lost across the call chain.** Untyped
`partition` has no `_array_witness` and its dump contains nine static
`fn_index` sites, six `fn_array_set` sites, and generic `fn_le`/`is_truthy`
comparisons on its loop paths. Typed `partition` carries the witness and
uses element lanes. The producer-to-parameter diagnostic shows that
recovering that information is worth much more than shaving another
branch from the already typed partition.

**Text search: producer/return information and boundary conversion matter.**
Untyped `to_codes` builds an open array by pushing integer code points;
the array flows through a return and through `pattern_codes[index]`.
Untyped search bodies keep generic length/index/equality paths. Adding
only declared `int[]` parameters introduces repeated admission of those
open arrays; a representation-changing admission must return a replacement
and does not turn the caller's original open array into a certified lane.
The runtime path is `runtime_type_admit_array_env` in
`lambda-eval.cpp:11120`: matching existing carriers reuse proof; other
numeric conversions copy, validate, repack, then install the certificate
on the replacement. The fully typed port declares `to_codes(...) int[]`
and therefore performs that boundary work at production instead.

A further isolated variant adds only that `to_codes(...) int[]` return
to the parameter-only microbenchmark. The executed census drops
`fn_mutable_value` from **73,728 calls to nine**. The first number is
exactly `1,536 rounds × 8 patterns × 3 algorithms × 2 array arguments`;
the second is one corpus plus eight patterns. Both produce checksum
`91395120`. This establishes repeated representation conversion at the
consumer boundaries as the partial-typing regression mechanism, rather
than a slower search algorithm. The uninstrumented source-pair test
against the original untyped file confirms the improvement: 13.00 s →
1.424 s, a 9.13x speedup. The causal claim concerns these source variants
on the current release, not a historical engine regression.

The sum of Result47 untyped execution cells is 27.35 seconds; text rows
account for 88.3%, and text_search alone 55.9%. This is a prioritization
view, not the suite geomean and not a general application workload mix.

### The compiler limitation to address

The current inference code distinguishes an honest call-site join from a
specialization candidate join, but still conflates some not-yet-known
facts with dynamic `any`. See `mir_callsite_join_specialization_type`
(`transpile-mir.cpp:39353`) and the array admission gate in
`infer_param_types_batched` (`:34954`). Array element inference depends on
the complete argument/element join, key types, and stores; one lost fact
can keep the callee boxed. Literal record-shape hints also stop at
untyped collection reads (`mir_module_unique_shape_for_field`, `:39009`).

Improve producer → local → argument → recursive call → return propagation
as one analysis, with distinct unknown/unresolved/conflicting states.
These must be inferred representation facts, not silently inserted source
annotations that would narrow the program's accepted values. A growing
untyped array must retain its legal widening behavior. Hoisting a
conversion also needs an unchanged-value/ownership proof across the uses.
Do not simply weaken the existing guards: they prevent real mixed-carrier
miscompilations. **D3.3.2v2–D3.3.4, D8.3.1v2–D8.3.4v3** require inferred
specialization to retain exact entry proof and a complete boxed fallback
where the caller set is open. **D8.4.1v2** prohibits adding mutable inline
caches as a shortcut. The existing DF12 speculative-lift and TG8
guard-hoist experiments remain opt-in; enabling them wholesale is not an
established performance fix.

## 6. Recommended order

1. **Close regressions with evidence.** Recover/rebuild matched R46/R47
   releases for splay and the untyped towers/permute candidates. Reproduce
   fannkuch's loop-fact precision defect and pin its emitted ownership
   checks. Preserve puzzle's required plain-parameter semantics.
2. **Give untyped code the existing native paths.** Start with nbody's
   nested counter/store facts, quicksort's array witness chain, and
   text_search's producer/return/element facts. Add a diagnostic explaining
   why specialization was refused, not just whether it was emitted.
   Measure coverage as disappearance of generic operations on executed
   hot paths, while checking mixed types, nulls, errors, aliasing and GC.
3. **Remove scalar helper boundaries shared by both variants.** Native
   boolean equality, safe bitwise operations, scalar `abs`, and proven
   character reads; then extend leaf inlining to those operations. Retain
   numeric-band/null/error behavior under **S4.1.2, D2.5.3, D2.8.1–D2.8.3**.
4. **Reduce ownership work and allocation in the expensive rows.** Carry
   handle facts through valid regions; avoid unnecessary re-navigation and
   repeated admission; construct certified arrays directly. Profile
   splay/havlak/deltablue/hashmap and string workloads on the actual
   candidate before sizing work. Treat real snapshots separately from
   false-positive sharing facts.
5. **Only then tackle the remaining codegen structure.** Use dynamic
   profiles and native spill counts for fft/quicksort/nbody. Additional
   whole-loop proof or storage-lifetime schemes need their design and
   correctness obligations resolved before implementation; no speculative
   promise that another guard-elision pass will deliver a large leap.

For every track, keep typed and untyped release A/B measurements, identical
source and output checks, and engine changes separate from benchmark-port
edits. Retain raw samples, binary hashes, and C control provenance. Gate
both execution time and auto end-to-end latency: this analysis concerns
the former, and MIR code-size growth can still affect the latter.

## 7. Evidence inventory and limitations

- Historical inputs: `test/benchmark/benchmark_results_v42.json` through
  `v47.json`; `history.py` / `history.json` contain calculations.
- Current release identity, source hashes and outputs: `probe.json`.
- Fourteen current untyped/typed diagnostic executions: all return success
  with the expected benchmark output/checksum; MIR and COW artifacts are
  named by suite/workload/variant. Their profiling-enabled timings are
  not used as benchmark estimates.
- `mir_census.json` uses the existing `mir_mandatory_census.py` parser and
  loop-dominance analysis. Static call counts include cold paths;
  dominance is a conservative structural diagnostic, not a sample profile.
- Uninstrumented experiments: `source_pairs.json`,
  `quicksort_chain_pairs.json`, `text_params_pairs.json`,
  `text_return_pairs.json`; all valid pairs
  compare one release binary against itself with isolated source edits.
- Text-boundary conversion census: `text_params_cow.tsv` versus
  `text_params_return_cow.tsv`, corresponding MIR dumps and stdout files.
- Existing historical attribution: Tune27 §2.1, Tune29/Tune30 measurement
  logs, and Tune31 §1.4–1.5. Their absent raw archives prevent independently
  repeating those historical bisections here.
- No engine fix or new full-suite performance claim is included in this
  analysis. No formal semantic/design ruling is changed.
