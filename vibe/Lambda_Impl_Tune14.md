# Tune 14: Result23 Analysis, Measured Attribution, and the Next Tuning Round

- **Date:** 2026-08-06 (rev 2 — rewritten from measured profiles; rev 1's unprofiled
  hypotheses are corrected in §2.6)
- **Input:** `test/benchmark/Overall_Result23.md` (Lambda commit `91dbb15dd5`),
  `vibe/Lambda_Impl_Tune13 (done).md`, sampling profiles in `temp/prof14/` (see §5)
- **Status:** IMPLEMENTED — 2026-08-06 (code, gates, and measured closeout below)
- **Related:** `vibe/Lambda_Impl_Tune13 (done).md`, `vibe/Lambda_Tune_Typed_Vs_C2MIR.md`,
  `vibe/Lambda_Result15_Bottlenecks.md`, `vibe/Lambda_Impl_Tune9_GC (done).md`,
  `vibe/Lambda_Design_Type_Enforcement.md`, `vibe/Lambda_Design_Name_Identity.md`
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S1.4–S1.6, S4.1, S4.5.3, S11.4;
  `doc/Lambda_Formal_Design.md` D2.2–D2.6, D3.2–D3.3, D4.4, D5.2–D5.3, D8.3–D8.4, D8.6

## 1. What Result23 says

| Metric | Result22 | Result23 |
|---|---:|---:|
| MIR (untyped)/Node geo | 2.75x | **2.70x** |
| MIR (typed)/Node geo | 2.27x | **1.91x** |
| MIR (typed)/C2MIR geo | 11.2x | **9.56x** (44 rows) |
| LambdaJS/Node geo | — | **15.4x** |
| QuickJS/Node geo | — | 7.20x |

Result23 confirms the Tune13 close-out on a fresh archived binary and a fresh Test262 gate
(40,261/40,261): `nqueens` 5.90→2.14 ms, `quicksort` 40.8→5.31 ms, `bounce` 2.97→0.815 ms
versus Result22. The two headline problems for the next round are: **typed still buys only
1.41x over untyped** (with 14 rows where the annotation makes the row slower), and **LambdaJS
is 2.14x behind QuickJS** with a catastrophic tail (havlak 444x, cd 268x, hashmap 207x).

## 2. Measured attribution (2026-08-06, archived v23 release binary)

All profiles were taken on `test/benchmark/exe/lambda-v23-91dbb15dd5` — the exact Result23
binary — using `/usr/bin/sample` at 1 ms on looped variants of the shipped benchmarks
(loop count raised until runtime ≥3 s; workload code unchanged), symbolized against the
binary with `nm` for addresses inside large functions. LJS GC attribution uses the built-in
`LAMBDA_GC_STATS=1` counters; checked-store counters use `COW_EXEC_PROFILE=1`
(`temp/cow_exec_profile.tsv`). Raw sample files: `temp/prof14/*.sample.txt`.

### 2.1 MIR typed: per-element runtime admission dominates the numeric rows

`matmul2` (typed 69.7 ms vs untyped 18.2 ms): `COW_EXEC_PROFILE` records
**zero** `array_checked_store_calls` — the Tune13 guarded lanes engage, including through the
borrowed-`var c: float[]` parameter. The cost is above them:
`lambda_numeric_boundary_admit` + `lambda_type_check` together are **≈60% of samples**
(798 + ~470 of ~2,100 non-JIT samples). Every element read/accumulation crossing a declared
`int`/`float` local or parameter boundary calls the runtime admission helper instead of being
elided as an identity admission at emission time. This is the R22-4 gap (Tune13 P1 items 5/7)
still live: the proof exists dynamically but is not carried statically to the boundary
[S4.1, D2.4, D3.2.1, D3.3.1].

`crypto_sha12` shows the same signature plus two more: `fn_ushr_item` (**boxed unsigned
shift** — hot addresses 0x9d0278/0x9d0424 resolve inside it), boxed `fn_int` conversions, and
`coerce_num_sized`. The int bitwise lane (`shr`/`ushr`/masking) does not stay native, which is
why the typed sha1 row is no better than untyped (297 vs 274 ms) [S4.1, D2.2.2].

`bounce2` adds a third variant: the top leaf is `lambda_type_lane_storage_desc` (367 samples)
— the lane/storage descriptor is **recomputed per store** — plus boxed `fn_array_set`
fallbacks and `cow_prepare_write`. The descriptor is a function of the static contract and
should be computed once at emission, not per element.

`levenshtein2` is not a string-track row at all: the DP inner loop runs fully boxed —
`item_at` 269, `fn_index` 213, `fn_add` 168, `boundary_admit` 139, `type_check` 62. The
`prev`/`curr`/`tmp` **array-rotation rebinding** defeats the declared witness (narrowing dies
with the binding, D3.3.3), so every element access takes the generic path.

### 2.2 MIR typed: fib-class per-call machinery measured at ~45%

`fib2` (looped): `fn_add` 373 + `lambda_item_adopt_scalar_home` 213 +
`lambda_restore_number_frame_top` 197 ≈ **45% of samples**, with `_tlv_get_addr` (TLS context
access) another ~4%. This is the Result18 M3/A3 mechanism finally measured: recursive results
are boxed, recombined through `fn_add`, and every call adopts/restores a scalar home. The
Tune13 P2 entry gate (≥10% attribution) is now met with headroom. The `ack` control row
(typed 17.4 vs C2MIR 11.7 = 1.5x) still shows pure deep recursion is near the ceiling — the
6–68x gaps in fib/tak/towers/permute/queens/bounce/list are per-call overhead, not depth.

### 2.3 MIR typed: deep validator and map-record costs

`towers2`: `validate_against_map_type`/`validate_against_type`/`validate_against_base_type` +
`lambda_type_matches`/`lambda_type_check` ≈ **18%** of samples, with `strcmp`/`strncmp`
field-name comparisons and `pool_calloc`/memset churn. Declared map contracts are re-walked
through the deep validator on hot paths Tune13's stable-binding elision does not cover.

`gcbench2` (MIR): `map_shape_field_to_item` 190 + `fn_member` 115 + `map_get` 110 +
`strlen` 48 — **map-as-record field access walks ShapeEntry chains and compares string
names** — plus the fib-class trio (`restore_number_frame_top` 143, `fn_add` 127,
`adopt_scalar_home` 105) and allocation zeroing (`__bzero`/memset/`gc_heap_bump_alloc` ~10%).
This connects directly to the Name-Identity plan (`ShapeEntry.name_id`, NI ledger W1/W2).

### 2.4 MIR: a quadratic slab-chain walk dominates base64

`base642`: **`gc_object_zone_alloc` alone is ≈64% of samples, and ~99% of those sit at
offsets 240–252** — the two-instruction fresh-slot search loop
(`while (slab && slab->next_fresh >= slab->slot_count) slab = slab->next;` in
`lambda/runtime/gc/gc_object_zone.c`). Between collections the free lists are empty, every
slab in the class chain fills, and each allocation walks the ever-growing chain of full slabs
before reaching a fresh one: O(chain) per allocation, quadratic per GC cycle. The concat-heavy
workload (one growing result string plus four small table strings per 3 input bytes, ×100
iterations) makes the chain thousands of slabs long. The actual string work (`fn_join`,
`fn_string`, `fn_strcat`, memmove) is only ~10–12%.

This is a clean Lambda-side allocator defect with an O(1) fix (track the current fresh slab —
or a fresh-slab list — per class instead of searching from the head). It should also shave
every allocation-heavy row. The string-builder track (P3 in Tune13) is real but secondary on
this row; re-measure after the allocator fix.

### 2.5 LambdaJS: one mechanism family explains the whole tail

`LAMBDA_GC_STATS=1` on the worst rows (post-bitmap):

| Row | total | mark_collections | mark_ms | mark share |
|---|---:|---:|---:|---:|
| deltablue | 958 ms | **0** | 0 | 0% |
| cd | 9,549 ms | 25 | 7.2 | 0.08% |
| hashmap | 3,123 ms | 2 | 5.1 | 0.16% |
| gcbench | 1,377 ms | 65 | 340 | **25%** |

GC mark is a solved problem everywhere except gcbench — the `alloc_bits` bitmap
(landed `b743f9375`, "tune8 GC impl") did its job. The samples of hashmap, cd, havlak
(bundle), and deltablue all show the **same leaf family, ~50%+ of run time**:

- `js_map_get_fast(Map*, char const*, …)` — property lookup takes a **C-string key** (top
  leaf on all four rows: 286/745/1720/71 samples);
- `well_known_name_id` — a name-table probe on every access (208/687/1301/54);
- `hashmap_sip` — siphash over the key string per lookup;
- numeric keys **round-trip through text**: `js_to_string` → `lambda_finite_double_to_shortest`
  → `snprintf`/`__dtoa` on the way in, `__svfscanf_l`/`fastParse64` parsing them back;
- `name_pool_create_strview` — hot-path interning of those transient key strings;
- plus the shape machinery (`js_find_shape_entry`, `js_own_shape_slot_status`,
  `js_intrinsic_note_property_mutation`, `js_get_prototype[_of]`).

This is Result15's #2–#4 confirmed still live and now dominant: the property-access path is
string-keyed end to end. Integer/interned-ID keys through `js_map_get_fast`, cached
`well_known_name_id` results at the access site, and integer fast paths for numeric keys are
the LJS round. (LJS may keep inline caches — the no-IC rule is Lambda-script-only, D8.4.1.)

### 2.6 Corrections to rev 1's unmeasured claims

- The matmul borrowed-`var` checked-store hypothesis was **wrong**: checked-store calls are
  zero; the cost is per-element admission (§2.1).
- `base64`/`levenshtein` are **not primarily string-semantics rows**: base64 is an allocator
  defect (§2.4), levenshtein is a witness-rebinding defect (§2.1).
- The "P0 build a profiler" phase is **discharged**: the `sample` + `nm` + counter protocol
  used here is sufficient and is now the documented entry-gate instrument (§5).

### 2.7 The typed-worse-than-untyped ledger, now attributed

Fourteen Result23 rows where the annotation costs; each now maps to a measured mechanism:
matmul +283%, sha1 +8% (§2.1 admission/bitwise); bounce +195%, storage +68% (§2.1 descriptor +
`fn_array_set`); tak +59%, cpstak +87%, towers +39%, permute +37%, fib +6%, list +6% (§2.2
per-call machinery, §2.3 validator); brainfuck +29%, fasta +15%, knucleotide +8%, json_gen
+9%, splay +23% (mixed; re-profile after A1/A2 below). The acceptance bar is categorical:
**an annotation may never make a row more than 5% slower.**

## 3. Proposed MIR track (ranked by measured impact)

### A1 — Elide runtime admission on statically proved identity boundaries (top item)

Carry the exact result contract and `ValueRep` of guarded element reads, native int/float
arithmetic, and audited builtins through AST inference and lowering so that identity
declaration/assignment/parameter/return boundaries emit **no**
`lambda_numeric_boundary_admit`/`lambda_type_check` call — only a required carrier conversion
when representations differ [S4.1, D2.4.1, D3.2.1, D3.3.1]. Include:

1. the guarded-load result (`float` from a proved `float[]` lane) feeding a declared local or
   accumulation (`matmul`, `pnpoly`, `spectralnorm`, `nbody`);
2. the int bitwise builtins `shr`/`shl`/`ushr`/bit-and/or and `int()` on proved int operands —
   native lane, no `fn_ushr_item`/`fn_int` (`crypto_sha1`, `brainfuck`) [S4.5.3, D2.2.2];
3. compute `lambda_type_lane_storage_desc` once at emission from the static contract, never
   per store (`bounce`, `storage`);
4. let a declared witness survive **same-contract rebinding** (the `prev`/`curr`/`tmp`
   rotation in `levenshtein`): rebinding to a value carrying the identical occurrence contract
   re-establishes the witness instead of dropping to generic `fn_index`/`item_at`
   [D2.6, D3.3.3].

Acceptance: matmul typed ≤ untyped; sha1 and levenshtein −30% or better; `boundary_admit`
disappears from the four samples' hot leaves; typed/nullable rejection controls and forced-GC
sweeps green [D8.6.3].

### A2 — Fix the O(n²) fresh-slot search in `gc_object_zone_alloc`

Keep a per-class pointer to the current fresh slab (or a fresh list), updated on fill, sweep,
and new-slab prepend, so allocation never walks full slabs. Strictly an allocator-internal
change: no lifetime, ownership, or layout semantics move [D4.4]. Measured ceiling: ~64% of
`base64`; expect gains on every allocation-heavy row (binarytrees, paraffins, list, gcbench,
splay, json_gen). Acceptance: base64 −50% or better; the +240/252 leaf vanishes; full
forced-GC/poison suite green.

### A3 — Native recursive success returns (Tune13 P2, gate now met at ~45%)

Unchanged design from Tune13 §5 items 8–9: whole-function exit-carrier analysis; raw recursive
results feed native arithmetic; guard failure/dynamic call/error merge keeps the Item path
[D3.3.1–D3.3.2, S11.4.2, D8.3.3]. Include the scalar-home adopt/restore pair and the TLS
context access in the reduction scope, per §2.2's measurement [D5.2–D5.3]. Targets:
fib/tak/cpstak/towers/permute/queens/bounce/list. Acceptance: ≥25% on fib (measured headroom
is ~45%); `fn_add` leaves the fib sample; `ack` does not regress.

### A4 — Stop re-validating stable map contracts; finish name-identity field access

Extend Tune13's stable-binding boundary elision to the declared map/record contracts that
`towers` re-walks through the deep validator (~18%), and land `ShapeEntry.name_id` comparisons
so `map_shape_field_to_item`/`fn_member` stop strcmp-ing field names (`gcbench`, `deltablue`
MIR side) — this is Name-Identity W1/W2 [D3.2.2, D8.3.2–D8.3.3;
`vibe/Lambda_Design_Name_Identity.md`]. Acceptance: towers −25%; validator functions leave its
sample; named-contract rejection tests unchanged.

### A5 — String builder track (conditional, re-gated after A2)

Re-profile `base64`/`brainfuck`/`fasta` after A2. Enter only if loop-carried `++` concat,
per-char boxing, or UTF-8 rescanning still attributes ≥15%. Remedy per Tune13 P3: internal
owned builder, one immutable finalize at the observable boundary; Unicode semantics and
precise roots preserved [S1.4–S1.6].

## 4. Proposed LambdaJS track

### L1 — De-stringify the property-access path (top item, ~50%+ measured)

1. **Integer fast path for numeric keys**: array-indexed and integer-keyed access must never
   round-trip through `snprintf`/`__dtoa`/`sscanf`. Key the fast map path on an integer or
   interned NameId variant instead of `char const*`.
2. **Stop probing `well_known_name_id` per access**: resolve the name once at the access site
   (compile-time for static names, per-shape cache otherwise). LJS may use inline caches
   (LC1/D8.4.1 restricts Lambda script only).
3. **No hot-path `name_pool_create_strview`**: transient lookups must not intern.

Beneficiaries (measured): hashmap 207x, cd 268x, havlak 444x, deltablue 82x, triangl,
crypto_sha1-LJS. Acceptance: the §2.5 leaf family drops out of the four samples; hashmap and
cd −40% or better; Test262 40,261/40,261 unchanged.

### L2 — gcbench mark residual (25%) and shape machinery

gcbench is the only row where mark still matters (340 ms / 65 collections). Profile
mark tracing (not ownership — the bitmap solved that) before changing anything; pair with the
R2b shared-root-shape item for the constructor-heavy rows (deltablue runs **zero**
collections — its 82x is pure mutator/shape/closure work).

### L3 — Unboxed storage (B2 → OI-9)

Long pole; enter only after L1 lands and a fresh profile still attributes the residual to
boxed slot storage.

Target for the LJS track: geomean 15.4x → ≤ QuickJS 7.20x; stretch single digits.

## 5. Profiling protocol (replaces rev 1's "P0 build a profiler")

The entry-gate instrument for every phase above is the protocol used to produce §2:

1. Loop the shipped benchmark source (workload untouched) until runtime ≥3 s; run on the
   archived release binary with `run`/`js` exactly as the harness does.
2. `sample <pid> <secs> 1 -f out.txt`; read "Sort by top of stack"; resolve anonymous
   addresses with `nm <binary> | sort` bracketing (large functions report interior offsets —
   0x9d63xx was `lambda_type_check`, 0xd7f7xx was `lambda_numeric_boundary_admit`).
3. `LAMBDA_GC_STATS=1` for collections/mark-ms; `COW_EXEC_PROFILE=1` →
   `temp/cow_exec_profile.tsv` for checked-store/admission counters.
4. Keep the raw sample files under `temp/prof14/` (base64_mir, leven_mir, fib_mir, towers_mir,
   bounce_mir, matmul_mir, sha1_mir, gcbench_mir, hashmap_ljs, cd_ljs, havlak_ljs,
   deltablue_ljs).

Caveat: LJS executes on a worker thread (main parks in `pthread_join`) — read the worker
thread's tree, and treat `__ulock_wait` on the main thread as an artifact.

## 6. Gates and acceptance (house rules, unchanged)

- `make test-lambda-baseline` 100% and `make test262-baseline` 40,261/40,261 after each
  retained phase; MIR emission ratchet updated in the same commit for justified growth
  [D8.6.1]; `mir-check` coverage for every new witness/elision [D8.6.2]; forced-GC + poison
  sweeps for every ownership/root/representation change, stressed output byte-matching
  [D8.6.3].
- Release-build timing only; per-phase acceptance thresholds as stated inline; a change is
  retained only with its measured win on the fixed 56-row population (three runs,
  workload-only `__TIMING__`, headline + fixed matched geomeans).
- Non-goals carried from Tune13 §8: no flex-int revival, no second direct-addressing path, no
  Lambda-script inline caches [D8.4.1], no vendored-dep edits [D1.6] — note A2 is in
  `lambda/runtime/gc/`, Lambda's own code, not vendored MIR.

## 7. Implementation closeout (2026-08-06)

The implementation slices are landed in the current tree. The proposal sections above remain
the measured rationale and acceptance targets; this section records what was actually retained,
including targets that were not met by the measured workload. No semantic relaxation was used:
the optimization boundaries preserve S4.1's representation contract, D2.4's carrier rules,
D3.2.1's checked-boundary invariant, and D8.6.3's forced-GC requirement.

### 7.1 MIR and allocator slices

| Slice | Landed implementation | Evidence and disposition |
|---|---|---|
| A1 | Native boundary identity proofs; typed-array lane descriptors are hoisted into checked stores; U32 bitwise/USHR lowering stays on the native lane; direct same-contract aliases preserve the Levenshtein witness. | Release 3-run medians: matmul 18.6→18.0 ms, base64 105→69.6 ms, Levenshtein 27.8→21.3 ms, crypto-sha1 235→215 ms (untyped→typed). `tune13_array_lane.mir-check`, `typed_array_guard.mir-check`, the nullable/rejection controls, and the MIR ratchet pass. The original −30% Levenshtein and −50% base64 stretch targets are not claimed; the code is retained for the measured wins and the safe fallback paths. [S4.1, S4.5.3, D2.2.2, D2.4, D3.2.1] |
| A2 | `gc_object_zone` now keeps one fresh-slab cursor per size class, eliminating the full-slab-chain walk while preserving free-list, sweep, and zeroing behavior. | The allocator ownership suite passes 67/67, and forced-GC/poison runs pass for `levenshtein2`, `bounce2`, and `towers2`. The base64 row improves 33.7% on the matched release sample; the −50% target is not overstated. [D4.4] |
| A3 | Recursive native-success analysis carries proven int/float results through arithmetic and keeps error/guard joins on the boxed Item path. Mutable accumulator bindings are excluded from the immutable witness so the native ABI cannot be applied to an unsafe return. | `fib2`, `tak2`, `cpstak2`, `nqueens2`, `bounce2`, `towers2`, `levenshtein2`, `crypto_sha12`, `base642`, `matmul2`, `pnpoly2`, `spectralnorm2`, and `gcbench2` all produce their expected PASS/DONE results. Current medians include fib 6.09→1.95 ms, tak 0.590→0.202 ms, and cpstak 1.21→0.415 ms. [D3.3.1, D3.3.2, D8.3.3] |
| A4 | Existing stable-contract map/record access and fixed-shape field paths were verified and retained; no duplicate validator or second direct-addressing path was added. The current `towers2` MIR contains no `validate_against_*`, `fn_member`, or `map_shape_field_to_item` path. | The typed towers row remains slower because its remaining cost is the typed-array boundary path, not map-contract revalidation; the proposed −25% target is therefore recorded as unmet rather than hidden. [D3.2.2, D8.3.2–D8.3.3] |

### 7.2 LambdaJS identity slice

L1's safe static-name slice is landed. MIR member loads and stores now carry the compile-time
`PropertyKeyRef` into the IC ABI, allowing catalog-backed `NameId` keys to bypass repeated
`well_known_name_id` probing and transient name construction. Ordinary pooled names without a
stable catalog identity deliberately fall back to canonical named lookup: the `dom_module_props`
regression demonstrated that treating cross-pool spelling as pointer identity changes expando
deletion semantics. This preserves D2.6's key-identity distinction and S4.1's semantic boundary.

The existing numeric array-index path remains the J2 direct index path; L1 does not broaden that
identity proof to arbitrary dynamic strings, Symbols, DOM host properties, or proxies. The static
controls and the full JavaScript suite pass, and the pinned Test262 gate remains unchanged. The
broader historical L1 target (−40% hashmap/CD and removal of every dynamic numeric/string leaf)
is not claimed from this static-key slice alone. [D8.4.1, D8.6.3]

### 7.3 Verification record

- `make test-lambda-baseline`: input 2104/2104, runtime 1496/1496, combined 3600/3600.
- `make test262-baseline`: pinned commit `673e9bacbe28590f501e2dcd817aadcc31899191`,
  40261/40261 passing, 0 failures, 0 regressions, 0 retries, 2652 skipped.
- MIR ratchet: 15/15; the Tune14-reduced probes are locked at 1093 module instructions for
  `typed_array_guard` and 550 for `callsite_inference`; the lane function/frame budgets were
  reduced with them.
- `test_gc_heap_gtest`: 67/67; forced-GC plus freed-payload poisoning passed on all three
  representative MIR benchmarks above; `git diff --check` is clean.

The conditional A5 string-builder track and L2/L3 LambdaJS tracks were not entered: the current
post-A2 evidence does not justify changing string ownership or boxed-slot representation, and
those changes would exceed the measured Tune14 scope without a new attribution gate. [S1.4–S1.6,
D4.4, D8.6.3]
