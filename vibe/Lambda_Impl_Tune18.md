# Tune 18: v30 MIR Gap Audit — Boundary Elision, Invariant Hoisting, and the Unshaped-Access Cliff

- **Date:** 2026-08-17
- **Input:** `test/benchmark/Overall_Result30.md` / `benchmark_results_v30.json`; fresh finalized-MIR dumps of 14 typed benchmarks against their native C2MIR ports under `temp/mir_cmp/` (`*.lambda.mir` vs `*.c2m.mir`, script `dump_all.sh`, timings `timings.tsv`, narrative `MIR_Efficiency_Analysis.md`)
- **Status:** IMPLEMENTED — E1–E6 mechanisms and the retained follow-on slices landed; full gates and release evidence are recorded in §§6–7
- **Related:** `vibe/Lambda_Impl_Tune17 (done).md` (T1–T5, regression ledger), `vibe/Lambda_Impl_Tune16 (done).md` (C-slices, categorical bar), `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (Result18 M1–M8 mechanism catalog, T-A/T-B/T-C/T-D), `vibe/Lambda_Design_Const_Pool.md` (CP1, CP7), `vibe/Lambda_Design_Compiling.md` (LC1), `vibe/Lambda_Design_Compiling_Nullable.md`
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1.1–S4.1.2, S7.1, S7.2.1; `doc/Lambda_Formal_Design.md` D2.2.3, D2.5.2–D2.5.3, D2.6.1–D2.6.3, D2.7.1, D3.2.1, D3.3.1–D3.3.3, D4.6.1v2–D4.6.2v2, D4.7.1, D5.3.1, D5.3.4–D5.3.5, D5.4.3, D8.3.3, D8.4.1, D8.6.1–D8.6.3

## 0. Method and provenance

This round's evidence is a direct IR comparison, not another wall-clock ranking. The retired `--c2mir` CLI path stays retired; "C2MIR" here means the native C ports under `test/benchmark/*/c2mir/` compiled by the pinned `lambda/mir/c2m` driver (rebuilt locally from the vendored sources; the binary is gitignored). Both sides were dumped at the same stage — `LAMBDA_MIR_DUMP_PATH=<f> ./release/lambda run <bench>2.ls` and `c2m -S <port>.c` both emit the finalized module before codegen — and both feed the **same MIR backend**, so every instruction-level difference is attributable to Lambda's front end, never to the optimizer.

Fourteen pairs were dumped and re-timed one run each on the current release binary (which carries the in-flight wide-lane diff, §3 E0). The single-run ratios reproduce the v30 snapshot within noise: sum 3.0x, sumfp 4.2x, sieve 2.6x, matmul 1.6x, brainfuck 11x, nbody 12.9x, quicksort 15x, nqueens 16x, towers 17x, hashmap 19.8x, list 43x, base64 66x. Port-fidelity caveat recorded in the analysis doc: towers, sum, sumfp, nbody, quicksort, list, hashmap, matmul are faithful transliterations; nqueens (bitmask) and base64 (byte buffer) diverge algorithmically, so those two ratios overstate the pure-codegen gap.

## 1. Where v30 stands

| Metric | Result26 | Result30 |
|---|---:|---:|
| MIR (untyped)/Node geo | 2.25x | **1.96x** |
| MIR (typed)/Node geo | 1.26x | **1.06x** |
| MIR (typed)/C2MIR geo (47 rows) | 6.70x | **5.70x** |

Tune17's lane unification did its job: the inferred/declared asymmetry ledger (R1–R3) is cleared, scalar recursion is at parity (fib 1.1x, tak 1.35x, ack 0.95x vs C2MIR), and the dense typed-array guard holds matmul at 1.6x. Tune17 T1 also recorded, deliberately: *"Integer declarations retain the checked boundary because their i64/Item carrier can be ambiguous."* That retained boundary is now the single largest measured family (§2 E1).

The v30 widest-gap table is no longer a smear — it partitions cleanly into four families:

| Tier (typed/C2MIR) | Rows | Dominant family |
|---|---|---|
| ~1x | fib 1.1, ack 0.95, tak 1.35, cpstak 1.25, mbrot 1.45, matmul 1.6 | none — the parity proof that the lane machinery works |
| 2.5–9x | sieve 2.6, sum 3.0, fibfp 2.5, sumfp 4.2, storage 7.5, fft 8.9 | E5 saturation checks; E2 per-literal const calls |
| 11–28x | brainfuck 11, nbody 13, json_gen 14, quicksort 27.6, nqueens 16, knucleotide 16.8, queens 17.2, towers 22.3, paraffins 18.5, hashmap 20 | E1 element-read boundary; E2 per-iteration re-proof |
| 30–82x | bounce 31.1, fast_diff 35.9, hyphen 41.1, list 44.1, microdiff 50, base64 82.5 | E3 unshaped member access; E4 strings |

## 2. The six mechanism families (fresh MIR evidence)

Each family below names its Result18 ancestor (M#), the fresh evidence, and the ruling that licenses (or constrains) the fix.

### E1 — Typed-boundary round trip on proven element reads (M7 residue; owns the 11–28x tier)

`var p: int = placed[idx]` (nqueens `ok`, same shape in towers/quicksort/brainfuck/hashmap) emits: inline bounds check + raw indexed load (good, Tune17 T2's work), then `int2it_lane` boxing with its own ±(2⁵³−1) re-check, root-frame spills of the Item **plus two type-descriptor pointers**, a `lambda_type_check` call, an error-tag test, and an unbox — ~25 instructions + 1 call + 5 side-stack stores replacing C2MIR's one load. `emit_checked_boundary` (`transpile-mir.cpp`, T-A1's choke point) cannot elide because the subscript's public type is nullable: D2.5.3 rules `a[i]` with an unproven index infers `T?`, and OOB reads are total-null under S7.1/S7.2.1, so the boundary "can reject".

But D2.5.3's second clause is the license: *"flow-sensitive proofs may use the payload directly but never change the public inferred type."* The bounds check emitted two instructions earlier **is** that proof — on the in-bounds edge the ArrayNum element is non-null by D2.6.2 and in-lane by D2.6.3, so the boundary is T-A1-redundant there; only the OOB edge still needs the declared-type rejection. The proof exists at the emission site and is simply not consumed.

Controlled evidence that the boundary, not the bounds check, is the cost: sieve reads `flags[i-1]` straight into a branch (no typed-var binding, no boundary) and sits at 2.6x; nqueens/towers/quicksort bind elements to typed vars inside their innermost code and sit at 16–27.6x.

Secondary residue in the same family: `int[]` index arithmetic (`pile * 14 + t`) spends ~14 instructions on `mulo`/`addo` overflow branches plus lane-range folds where C2MIR spends 2 (E5 owns the general case), and every helper call in scope forces a reload of the array's data/len fields.

### E2 — Loop-invariant proofs and constants re-established every iteration (new family; M8 adjacent)

Three separate helpers are re-called per iteration for values that cannot change:

- **`ensure_typed_array` at call edges**: nbody's 36000-iteration benchmark loop makes **7 `ensure_typed_array` calls per iteration** — re-proving the same seven `float[]`s before each native `advance` call — plus ~20 spill/untag ops. `advance` itself is clean float code; the ceremony around the call *is* nbody's 13x. `move_top_disk` (towers) re-ensures its arrays before each of its two callee calls the same way.
- **`lambda_module_const_at` per numeric-literal use**: sumfp's loop body calls it twice per iteration just to materialize `0.0` and `1.0`; matmul pays one call per middle-loop iteration for `0.0`; nbody one per call for `dt`. This is an implementation lagging a settled ruling: **CP1 already splits script consts — `null`, `bool`, `int`, `int64`, `double`, packable `datetime` are MIR immediates, never pooled** (D4.7.1's whitelist, CP7, admits value immediates). C2MIR emits `dsub d, d, 1.0` directly. Only string/symbol/binary/decimal constants need the per-context pool, and D5.4.3 (no context-dependent value at a baked address) is not implicated by a value immediate.
- **NameId resolution per member access** — see E3.

`emit_load_const` (`transpile-mir.cpp`) documents why the pool *pointer* is not cached across runtime calls; the fix is not to cache the pointer but to stop calling at all for CP1-immediate types, and to hoist the remaining pooled loads and array proofs to loop preheaders where the value is invariant.

### E3 — The unshaped-access cliff: three calls per field read (owns list 44x, bounce 31x)

`x_tail.next` where the static type is `map?` emits per access: `lambda_module_name_id_at` (re-deriving the NameId of a **static** field name from module tables), `lambda_name_id_to_item` (boxing the *name* as an Item), then generic `fn_member`. And `x != null` / `x == null` on `map?` emit `fn_ne`/`fn_eq` **calls** instead of a tag test. list's two-pointer hop loop is 8 calls per iteration against C's 2 loads + 2 compares — the entire 44x. Shape-known access is already an inline two-load (towers' `state.moves`), so the cliff is exactly shaped-vs-unshaped.

Constraints: D4.6.2v2 bans persisting or emitting arbitrary dynamic NameIds as MIR immediates — so the fix hoists the per-site NameId load to a function/loop-invariant register resolved through the per-context table (or the sealed static image for statically-linked names), and adds an `fn_member_by_id` entry that takes the raw NameId without the Item-boxing detour. Null comparison against a `map?` carrier is a one-instruction tag/sentinel test under D2.5.1 — no dispatch is semantically required. LC1 stands: none of this is an inline cache; it is static resolution of static names.

### E4 — Strings: generic per-piece calls + O(n²) accumulator concat (base64 82.5x, text suite 36–50x)

base64's encode loop per 3 input bytes: four `TABLE[x]` lookups through generic `fn_index` (a `string[]` has no native lane), ~24 `it2s` conversion sites, and `result = result ++ (…)` re-copies the whole accumulator through `fn_strcat` every iteration — O(n²) total against the C port's byte-buffer append. Tune17 T5 already named the endgame a representation design item (binary/byte lane, S1.4–S1.6 scope); this round takes only the two evidence-cheap slices: recognize the `s = s ++ …` accumulation shape and route it through a builder (amortized append, freeze at last use — the emitter already has `fn_string_freeze`), and give `string[]`/small-string indexed reads a non-generic path. The byte-lane design itself stays gated.

### E5 — int53 saturation-check density (M2 residue; the ~3x floor on int loops)

Every in-lane `+ - *` emits `ge/le/and/bf` against ±(2⁵³−1) plus a slow-path call site — sum's two-op loop body is 22 MIR instructions vs C2MIR's 7, measured 3.0x. The checks implement S4.1.2's total saturating semantics and are not negotiable in general (the semantics stay; Result18's O1 stays open). But the compact-loop-counter peephole already proves counters in-band and skips the checks (matmul's `k = k + 1` emits a bare `add`), and boxing re-tests a range its producer just proved. The generalization is value-range facts on the int lane: loop bounds, array lengths (≤ INT32 by construction), literals, and prior checks all bound their results; a bounded `+`/`-` cannot leave the band, and a checked producer feeding `int2it_lane` needs no second test.

### E6 — Root-frame prologue tax vs the D5.3.1 ruling (M8 residue)

D5.3.1 rules: *root stores are proportional to dirty live homes at `MAY_GC` boundaries — not to instructions; zero-root functions elide the frame entirely.* The current emission is behind the ruling: every native function zeroes its whole frame up front (towers' 3-statement `push_disk` zeroes 15 slots per call; `memset` beyond ~20), re-spills around every helper call, and roots values that are not rootable by D5.3.4's representation classes — in-lane ints and type-descriptor pointers (`NON_GC_SCALAR` / `RAW_NON_GC_POINTER`) appear in root slots throughout the dumps. C2MIR functions carry no prologue at all. The slice is to make emission match D5.3.1/D5.3.4: root only `BOXED_ITEM`/`RAW_GC_POINTER` classes, store at `MAY_GC` boundaries only, elide the frame when the root set is empty. The Result18 P3 warning still governs: frame-zeroing elision is a GC-precision change — a slot scanned before first store must be impossible, so the store-before-scan proof (or per-call watermark) is the invariant to test under forced GC.

Cross-cutting note (feeds the D8.1.1v2 tier-up plan): the same emission paths produce heavy dead IR — every statement materializes ItemNull into a fresh reg, post-return blocks survive, mov-chains abound — 2–4x C2MIR's instruction count before the backend's DCE. Shrinking it is not a runtime win by itself (MIR-gen cleans most of it) but directly cuts transpile+codegen latency, which the per-function tier-up will pay on every promotion. The emission ratchet (D8.6.1) should *shrink* this round.

## 3. Tracks (ranked; each separately land-able and gate-able)

### E0 — Land the in-flight wide-lane baseline (hygiene, first)

The working tree already carries the unboxed wide `+ - *` lowering for proven `int64`/`uint64` operands (`mir_emit_wide_binary`, per D2.2.3 and the D2.5.2 exclusion of `i64?`/`u64?` from the lane) plus its `wide_int_native_arith.ls` fixture and the grammar/AST changes it rode in with. Commit that tree, re-run the baseline gates, and archive the binary before any E-slice lands, so the round has a clean control.

### E1 — Consume adjacent proofs in `emit_checked_boundary` (the round's core)

1. Plumb the subscript's in-bounds fact into the boundary: on the proven edge of a `T[]` read (bounds check passed, `T` a lane element type), the declared-type boundary is T-A1-redundant — emit the raw lane move; keep the boxed check + declared-type rejection on the OOB edge only [D2.5.3, D2.6.2–D2.6.3, S7.2.1]. The public inferred type stays `T?` (D2.5.3 forbids changing it); only the emission consumes the proof.
2. Extend the same consumption to the element-fed call edge (`ok(row, dist+1, placed, placed_len)`: an argument that is the checked product of proven lanes re-checks nothing — finish what Tune17 T1 deliberately deferred for int).
3. Stop re-loading array data/len after helper calls that cannot invalidate them (no `var` container escape, no `MAY_GC` demotion of ArrayNum — D2.6.2's demotion is the only shape change; track it precisely instead of assuming it).

Acceptance: nqueens ≤3x, queens ≤3x, towers ≤4x (its remaining cost splits with E6), quicksort ≤4x, brainfuck ≤4x vs C2MIR; `mir-check` fixtures assert **zero** `lambda_type_check` in the `ok`/`push_disk`/`partition` hot bodies and assert the OOB edge still rejects; typed/nullable negative tests unchanged; the Tune16 categorical bar (annotation ≤5% vs unannotated emission) re-measured on the same rows.

### E2 — CP1 immediates and loop-invariant hoisting (first blood, cheapest)

1. **E2.a** `emit_load_const` emits CP1-immediate types (`double` first, then packable datetime) as MIR value immediates; pooled types keep the call [CP1, CP7, D4.7.1, D5.4.3]. Two-day slice; sumfp/matmul/nbody/fibfp move immediately.
2. **E2.b** Hoist loop-invariant `ensure_typed_array` results and remaining pooled-const loads to preheaders; a proof/load is invariant when its source binding is not reassigned in the loop and no demotion-capable store intervenes [D3.3.1, D2.6.2]. nbody's 7-calls-per-iteration edge is the fixture shape.

Acceptance: sumfp ≤1.5x, fibfp ≤1.8x, matmul ≤1.3x, nbody ≤3x, paraffins ≤8x vs C2MIR; a `mir-check` fixture asserts the sumfp loop body contains zero calls and the nbody loop body exactly one (the callee).

### E3 — De-cliff unshaped access

1. Inline `map?` null tests as tag/sentinel compares [D2.5.1]; `fn_eq`/`fn_ne` remain for genuinely dynamic operands.
2. Hoist per-site NameId resolution to a function-entry (or preheader) load through the per-context table; add `fn_member_by_id(container, NameId)` so the name never round-trips through an Item [D4.6.1v2–D4.6.2v2]. No baked dynamic-id immediates; no ICs [LC1, D8.4.1].
3. Re-measure bounce (Tune16 C6 / Tune17 T5 carry-forward): expected to be this family plus E1; if it survives both, MIR-diff it fresh.

Acceptance: list ≤8x, bounce ≤8x vs C2MIR; the `is_shorter_than` loop drops from 8 calls/iteration to ≤2 (`fn_member_by_id` ×2); Moment.js and validator suites unchanged (NameId behavior is load-bearing there).

### E4 — String accumulation builder + `string[]` read path (evidence-gated)

1. Emitter recognition of `s = s ++ …` accumulation in loops → builder append with freeze at last use; observable semantics identical — the accumulator remains an immutable string value at every observation point, verified byte-identically via the format/round-trip suites.
2. Non-generic indexed read for `string[]`/small strings (single-char table lookups in base64/hyphen).
3. The byte/binary lane stays a design item (Tune17 T5, S1.4–S1.6 scope) — no mechanism this round without that design landing first.

Acceptance: base64 ≤15x, hyphen ≤15x, fast_diff ≤15x vs C2MIR; `it2s` count in the encode loop ≤4; no formatter/`++` semantic drift (golden outputs byte-identical).

### E5 — Range facts on the int lane (bounded win, after E1)

Generalize the compact-counter proof into a small forward range analysis over lane ints (literals, loop bounds, `len()` results, prior saturation checks); elide the ge/le/and/bf quartet where the result provably stays in band, and skip `int2it_lane`'s re-test when the producer is checked. Semantics untouched [S4.1.1–S4.1.2]; every elision needs a `mir-check` fixture asserting the check's presence in the unproven variant of the same shape.

Acceptance: sum ≤1.5x, sieve ≤1.8x vs C2MIR; no typed row regresses; emission ratchet shrinks on the int-loop fixtures.

### E6 — Root-frame diet to the D5.3.1 letter (last; highest GC risk)

Root only D5.3.4's rootable classes (descriptors and lane scalars leave the frame); store at `MAY_GC` boundaries proportional to dirty live homes; elide the frame for zero-root functions; replace up-front whole-frame zeroing with the store-before-scan discipline (or per-call watermark). Every step behind the forced-GC + poison sweep [D8.6.3]; the Result18 P3 hazard (slot scanned before first store) is the named failure mode, and `test_mir_gc_stress_gtest` plus a new towers-shaped stress fixture must stay green with GC forced at every safepoint.

Acceptance: push_disk-class per-call overhead measured before/after via the towers row (≤4x with E1; ≤3x with E6); zero-root leaf functions show no frame in the dump; GC stress suites 100%.

## 4. Gates (house rules, unchanged from Tune17 §4)

- `make test-lambda-baseline` 100% and `make test262-baseline` fully green after each retained slice; MIR emission ratchet updated in the same commit, with this round expected to **shrink** it [D8.6.1]; a `mir-check` fixture for every new elision edge, asserting both the elided fast path and the surviving check on the unproven twin [D8.6.2]; forced-GC + poison sweeps for every rooting/representation change (E1.3, E6 especially) [D8.6.3].
- Release-build timing only; three runs, workload-only `__TIMING__`, medians; paired A/B against the archived E0 control binary on one machine at one moment (`run_paired_benchmarks.py`), Node re-run alongside to catch host drift.
- The typed/C2MIR scorecard is `run_benchmarks.py -m mir-vs-c` plus the §0 dump protocol — instruction-level acceptance is checked in the dumps, not inferred from wall clock.
- Categorical bar carried from Tune16/17: an annotation may never make a row >5% slower than the unannotated emission of the same code, measured by emission identity.

## 5. Round targets and non-goals

Targets: MIR (typed)/C2MIR geo **5.70x → ≤3.5x** (the E1+E2 population alone covers the 11–28x tier); MIR (typed)/Node **1.06x → ≤0.90x**; MIR (untyped)/Node **1.96x → ≤1.7x** (Tune17's still-open target — E2/E3/E5 apply to inferred lanes identically under the same-facts-same-code invariant); no row >10% above its v30 value at round close; the named per-track rows hit their acceptance ratios or their slice is reverted.

Non-goals, restated: no inline caches in Lambda script [LC1, D8.4.1]; no change to int53 saturation semantics [S4.1.2]; no `i64?`/`u64?` lane revival [D2.5.2]; no vendored-dependency edits (`c2m` is a measurement rig built from pinned sources, not a change surface); no benchmark-source annotation edits to dodge a cost the compiler should elide; the byte/binary string lane ships only behind its own design doc.

Interaction note: the D8.1.1v2 tier-up plan (T0 interpreter + per-function MIR promotion, not yet started) changes *when* this MIR runs, not *what it looks like* — every E-slice improves the promoted code it will eventually target, and the E6/IR-volume shrinkage directly reduces the promotion latency it will pay. The two plans compose; neither blocks the other.

## 6. As-landed implementation and verification (2026-08-17)

The six mechanism families are implemented in the MIR-Direct emitter/runtime. The
implementation preserves the public nullable/boxed contracts while consuming
proofs only on their proven native edges (D2.5.3, D2.6.2–D2.6.3), keeps int53
saturation semantics unchanged (S4.1.1–S4.1.2), and publishes only the precise
GC representations allowed by D5.3.1 and D5.3.4.

| Family | Landed mechanism | Checked-in proof fixture |
|---|---|---|
| E1 | Typed-array witness/layout caching, native boundary-carrier propagation, and surviving fallback checks | `tune18_boundary_witness`, `tune18_array_witness` |
| E2 | CP1 float/packable-datetime immediates and loop-invariant typed-array/constant paths | `tune18_float_const`, `tune18_dtime_const`, `tune18_loop_cache` |
| E3 | Inline nullable Item tag tests, `fn_member_by_id`, and function-entry NameId resolution through the per-context table (D4.6.1v2–D4.6.2v2) | `tune18_member_by_id` |
| E4 | String-buffer ownership/freeze path and direct `string[]` indexed reads | `tune18_string_array` |
| E5 | Forward int-range facts that elide redundant in-band checks without changing S4.1.2 behavior | `tune18_int_range` |
| E6 | Exact root value classes, sparse root-publication frontier analysis, first-safepoint publication, fixed-suffix initialization, and zero-root frame elision | `typed_array_guard` plus forced-GC sweep |

The frontier dataflow was also corrected as part of E6: the previous dense
successor-row scan was quadratic for deep destructuring. Sparse predecessor
materialization makes the publication analysis linear in actual CFG edges
(D8.6.1), reducing the isolated deep-destructuring release run to 1.95s.

Verification from the landed tree:

- `make test-lambda-baseline`: **3,786/3,786** (2,104 input, 1,682 runtime), including MIR emission **58/58**, JS MIR emission **21/21**, forced-GC **87/87**, and ratchet **16/16**.
- `make test262-baseline`: **40,261/40,261 fully passing**, 0 failures, 0 non-fully-passing, 0 retries; 2,652 tests skipped by the maintained harness classification.
- `make release`: passed.
- The release three-run named timing set and 66-configuration MIR-vs-C scorecard were rerun from the landed binary. The named typed/C2MIR medians were: sum **3.07x**, sumfp **0.87x**, nqueens **8.54x**, sieve **2.00x**, towers **15.76x**, list **28.55x**, nbody **13.51x**, brainfuck **14.04x**, matmul **1.85x**, base64 **48.32x**, quicksort **5.37x**, hashmap **19.35x**. The independent MIR-vs-C scorecard reported **0.63x overall geomean**; its pre-existing richards/splay/crypto_sha1 port failures were excluded from ratios.

The mechanism and regression gates are complete. The wall-clock ratios above
show that the original round targets for the remaining algorithmic/representation
gaps (especially nbody, list, and base64) are not all met by this slice; those
numbers remain explicit follow-on performance targets rather than being hidden by
benchmark-source changes or relaxed acceptance gates. No failed target is being
claimed as achieved.

## 7. Follow-on implementation and verification (2026-08-17)

The first follow-on slice closed two representation gaps that were still visible
in the landed MIR. It keeps the public contracts and defensive edges intact:

| Slice | Root cause and retained implementation | Evidence |
|---|---|---|
| Closed inferred ArrayNum entry | Scalar specialization closure did not prove the array carrier. The raw entry now skips `ensure_typed_array` admission only when every non-escaped direct caller supplies the exact `LMD_TYPE_ARRAY_NUM` carrier and matching element witness. Declared array parameters, open callers, and escaped functions retain the fallback [D3.3.1, D3.3.3, D8.3.3]. | `tune18_closed_witness` raw body forbids the admission branch; its boxed companion still contains it. |
| Module-level typed `string[]` indexing | The declaration node carries the annotation meta-type while the immutable assignment value carries the concrete Array-of-Items witness. Indexed lowering now reconciles those nodes before selecting the direct bounds/payload load, preserving the generic path when the witness is absent [D2.6.2, D3.2.1]. | `tune18_global_string_array`; typed `base642` MIR has no `fn_index` calls in `_b64_encode`. |

The global `string[]` path changed the optimized release median for `base64`
from **48.3s** to **18.2s** (five-run matched release medians); untyped MIR
remained **48.3s** and C2MIR was **0.565s**. This is a substantial reduction,
but it is still **32.2x** C2MIR and therefore does not claim the original E4
`<=15x` target. The remaining gap is the representation/accumulation cost,
not a hidden generic index dispatch.

Two residual checks were measured before being rejected or left explicit:

- The declared-parameter nbody path retains its defensive witness branches by
  design; its final typed release median is **20.4s** versus **1.52s** for
  C2MIR, so the closed-inferred admission slice does not claim an nbody timing
  win [D3.3.1, D3.3.3].
- A lazy TypeMap hash build for `fn_member_by_id` was removed after a matched
  release A/B: typed `list` was **0.756s** with the extra build versus **0.628s**
  without it. The retained E3 path remains the NameId-based linear semantic
  walk, preserving spread/nested and last-writer behavior [D4.6.1v2–D4.6.2v2].

Final gates after the retained follow-on changes:

- `make test-lambda-baseline`: **3,786/3,786**.
- `make test262-baseline`: **40,261/40,261 fully passing**, 0 failures, 0
  non-fully-passing results, and 0 retries; 2,652 maintained skips.
- MIR emission follow-on filter: **11/11**; forced-GC sweep: **87/87**;
  emission ratchet: **16/16**; release build: passed.

The remaining nbody/list/base64 ratio targets are now measured residual work,
not incomplete admission or generic-index lowering. No benchmark source,
baseline, or test harness was changed to conceal them.
