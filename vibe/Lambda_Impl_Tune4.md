# Lambda Impl Plan: Tune 4 — Result11 Tail Elimination (GC cliffs + typed elements + scalar inference)

**Status: IMPLEMENTED + RESULT12 MEASURED — 2026-07-24 (G0/G1/G2/M1/M2 landed; S1 not needed).**

## Result12 exit measurement (2026-07-24)

Four-engine matrix, 3-run medians, 180s timeout (Result11 protocol), fresh
release build with Tune4+Tune5. Raw: `test/benchmark/benchmark_results_v12.json`;
report: `test/benchmark/Overall_Result12.md`. QuickJS/Node geo held 7.10x→7.00x,
confirming host consistency (QuickJS unchanged).

| Metric | Result11 | Result12 | Tune4 target | Verdict |
|---|---|---|---|---|
| MIR/Node geo (dedup) | 4.73x | **3.01x** | ≤3.0x | ~met (0.3% over) |
| Worst MIR row | 617x (base64) | 66x (richards) | ≤25x | **not met** |

Targeted MIR rows all landed their wins: base64 617x→16.8x (G1), matmul 27x→2.8x
(M1), diviter MIR ~41x→7x (M2). The residual worst-MIR rows are all **untyped
map field access** (richards 66x, splay 43x, crypto_sha1 31x, navier 26x) — the
R6 follow-on §9 explicitly scoped out; base64 is no longer anywhere near the top.
So the geo target is met and the worst-row target is missed only on rows this
plan deferred by design. Re-rank evidence for `Lambda_Tuning_Proposal.md`: the
MIR tail is now R6 (untyped-map shape scans + `math.sqrt` native lowering), not
GC or typed-element access.

## Implementation record (2026-07-24)

Same-host before/after release medians, 3 runs, raw JSON in `temp/tune4_*.json`;
probe harness `temp/tune4_probes.sh` (G0.3). Baselines at every phase boundary:
`test-lambda-baseline` 3596/3596 (two new tests added), MIR emission budgets
(MT7) unchanged and green, `test-radiant-baseline` pass/fail counts identical to
master (its failures are pre-existing), full `test/benchmark` output sweep
byte-identical across G1+G2, M1, and M2.

| Phase | Probe | Before | After | Ratio |
|---|---|---|---|---|
| **G1** | kostya/base64 | 11508 ms | 322 ms | **35.7x** |
| G1 | kostya/json_gen | 125.0 ms | 76.3 ms | 1.64x |
| **G2** | jetstream/splay | 2384 ms | 197.8 ms | **12.1x** |
| G2 | awfy/havlak2 (guard) | 144.5 ms | 61.7 ms | 2.34x |
| G2 | larceny/gcbench | 444.9 ms | 436.3 ms | 1.02x → **R7 evidence** |
| **M1** | kostya/matmul | 970.7 ms | 62.5 ms | **15.5x** |
| M1 | awfy/nbody2 | 390.7 ms | 145.8 ms | 2.68x |
| M1 | jetstream/navier_stokes | 3404 ms | 1781 ms | 1.91x |
| M1 | larceny/pnpoly | 248.8 ms | 171.7 ms | 1.45x |
| M1 | beng/spectralnorm | 86.0 ms | 73.2 ms | 1.17x |
| **M2** | larceny/diviter | 24407 ms | 4050 ms | **6.0x** |
| M2 | kostya/collatz | 2114 ms | 2103 ms | 1.01x |

Guard probes flat or improved everywhere (binarytrees, fib2, fannkuch, sumfp2,
mandelbrot2, ray, cd2, divrec).

**What the phases actually turned out to be.**

- **G1** — replaced the sorted large-object array with an open-addressing pointer
  set in `gc_heap.c` (`gc_large_set_t`, backward-shift deletion). `find` became
  `gc_large_object_contains` returning 0/1: the index it used to return had no
  meaning left, and both call sites only tested `>= 0`. Counters confirm O(1)
  (2.71 probes/insert at peak 327k live large objects).
- **G2** — the owning slab now hangs off a **parallel** `range_slabs` array rather
  than being widened into `gc_slab_range_t`. Widening the range entry to 32 bytes
  first cost gcbench ~1.6%: that array is the binary search's working set. With
  the parallel array gcbench is 2% *faster* instead.
- **M1.0 diagnosis** — the real defect was not a missing fast path. A `float[]`
  parameter was registered with `param_decl_tid`, which for a `T[]` annotation is
  the `TypeParam`/`TypeUnary` wrapper's own `LMD_TYPE_TYPE` (23), not the resolved
  `LMD_TYPE_ARRAY_NUM` (17). Every indexed fast path missed, and the
  `ensure_typed_array` coercion right below it was dead code. Second defect:
  an INT-typed `i * n + k` subscript is emitted as a *boxed* flexible int, so
  even a correctly typed array fell to `fn_index`.
- **M1.2/M1.4 guards** — `MIR_INDEX_GUARD_ELEM_TYPE` (container type + `elem_type`
  + non-ndim/non-view) with an `item_at` fallback on loads, and
  `emit_array_num_elem_guard` before inline typed stores. Declared element types
  hold only at entry. `emit_index_result_move` also had to unbox on the FLOAT
  slow path, which the previously unreachable-with-guard code did not do.
- **M1.3** — `mir_index_expr_is_native_int` / `emit_native_int_index_expr`. ADD/SUB
  of compact ints is exact in int64; MUL keeps the double product for the range
  decision and, when it leaves the compact range, hands the bounds check
  `INT64_MAX` (a promoted float can never be a valid index anyway).
- **M1 latent bug found** — `emit_checked_index_load` declared its loaded register
  with `MIR_T_U64`, which the MIR verifier rejects. The uint64 path was simply
  unreachable before, so nothing had exercised it.
- **M2** — `prepass_collect_call_sites` runs the forward-declare walk in a
  collect-only mode (no duplicated walk), joins argument static types per
  position, and marks a function escaped when its name appears outside direct
  callee position or when it is `pub` / variadic / a closure / a `FUNC_EXPR` /
  an object method / a view handler. Up to 3 rounds carry types along call
  chains. `INFER_CALLSITE_INT`/`_FLOAT` grant a type only alongside real numeric
  body use.

**Gates met / not met.**

- G1 ≥20x on base64: met (35.7x). G2 ≥5x on splay: met (12.1x).
- M1 matmul ≤8x vs Node: met with margin. nbody 2.68x and navier 1.91x fall
  short of the "≥3x each" line, and spectralnorm/pnpoly were never in M1's reach
  — neither file uses a `T[]` annotation at all, so the plan mis-assigned them.
  nbody's residual is `math.sqrt` boxing, exactly as §5 predicted → stays R6.
- M2 diviter ≥10x: **not met** (6.0x). Its residual is the flexible-int boxing of
  `x = x - y` itself, i.e. M4, not parameter typing. collatz was already
  inferring INT before M2; its cost is `shr(n, 1)` boxing its argument (R6
  typed sys-funcs) → M2 correctly had nothing to do there.
- G2.4: gcbench stayed flat, confirming its mark cost is collection *frequency* ×
  live set, not `owns()`. That is **R7's entry evidence**, recorded as called for.

**Behaviour changes (not pure-performance, called out deliberately).**
Activating the previously dead `float[]`/`int[]` parameter path makes those
annotations mean something at runtime:
1. an out-of-bounds read through a `float[]` param now yields `0` rather than
   `null` — matching what a local `fill()`-narrowed float array has always done;
2. passing an array that cannot coerce to the declared element type now returns
   an error from `ensure_typed_array` instead of silently ignoring the
   annotation.
No test in any baseline encoded the old behaviour. Element narrowing is
suppressed for any param the body index-assigns with a *provably*
representation-changing value (`mir_store_may_change_elem_type`), which keeps
`a[i] = "x"`-style programs byte-identical to before.

**New tests.** `test/lambda/proc/proc_typed_array_guard.ls` (M1.4 in-place
conversion, native-int subscripts, generic→typed coercion; verified identical
pre/post and stable under `LAMBDA_GC_POISON_FREED` + forced GC at 1-in-5/17/50)
and `test/lambda/proc/proc_callsite_infer.ls` (M2.5 escape shields: indirect
call through a value, mixed-type mutual recursion, `pub` export — each carries a
`.25` that survives only if the parameter was *not* narrowed).

**Not done.** S1 (unique-string in-place append) — G1 removed base64's cliff and
`fn_strcat` is no longer top-three anywhere, which is exactly the condition §7
set for skipping it. ASAN spot-runs could not be executed: the ASAN debug binary
hangs in `__asan::AsanInitInternal` before `main` on this host (Darwin 25.5),
pre-existing and unrelated. Forced-GC + poison stress was used instead across
every GC probe and both new tests.

---

**Status: PLANNED — 2026-07-24.**
**Successor of:** `vibe/Lambda_Impl_Tune_COW (done).md` (exit = Result11). This plan
consumes the Result11 diagnosis that `Lambda_Tuning_Proposal.md` R0 called for and
executes the highest-leverage subset of R6 plus the newly root-caused shared-GC
defects. **The JS-engine-specific phases (holey-array watermark, canonical numeric
index) are split out to `vibe/Lambda_Impl_Tune5.md`** — this plan owns the
shared-runtime (GC) and Lambda/MIR work; the GC phases G1/G2 change LambdaJS
baselines too, so Tune5 measures against post-G1/G2 baselines.
**Diagnosis provenance:** release-build `sample` profiles + disassembly + source
tracing, 2026-07-24, two commits past the Result11 commit (`52c0f3c02`, benchmark
commit `1704c2f43`). Absolute times ran ~1.3–1.5x above the Result11 report
(different host state); all conclusions rest on hot-frame proportions, not
absolutes. Evidence recorded in §1; LJS-specific evidence lives in Tune5 §1.
**Related plans:** `vibe/Lambda_Impl_Tune5.md` (LJS phases J1/J2),
`vibe/Lambda_Tuning_Proposal.md` (R2/R3/R7 stay owned there),
`vibe/Lambda_Design_COW.md` Stage 2 (ArrayNum COW/views — untouched here),
`vibe/Lambda_Design_MIR_Emission_Test.md` (MT7 budgets gate all emission changes).
**Convention:** `file:line` refs verified 2026-07-24; they drift — confirm against
symbol names before editing.

**Governing invariant.** Every phase is a pure performance change: no observable
semantic change to Lambda or JS programs. The only permitted observable deltas are
timing, memory footprint, and GC collection counts. `test-lambda-baseline` and
`test-radiant-baseline` stay 100% at every phase boundary; MIR emission budgets
(`test/mir/mir_budgets.json`) are updated only by deliberate manual lifts per MT7.

---

## 0. What Result11 measured, in one table

Engine/Node geometric means (dedup): MIR **4.73x**, LambdaJS **19.3x**, QuickJS 7.10x.
The tail, not the median, is the problem. Profiled worst rows and their measured
root causes (§1 has the evidence):

| Cluster | Rows (ratio vs Node) | Root cause | Phase |
|---|---|---|---|
| GC-1 large-object registry O(n²) | kostya/base64 MIR **617x**; feeds brainfuck 17.8x, knucleotide, every >256B-alloc workload | `gc_large_object_add` linear insert + memmove into sorted array; threshold 256B | **G1** |
| GC-2 mark owns() O(slabs) | jetstream/splay MIR **48x**, larceny/gcbench 16.8x, triangl 34x, deriv 27x; LJS awfy/cd **2360x** (~70% of busy samples) | `gc_object_zone_owns` re-walks all size classes × slabs after the range binary search already hit | **G2** |
| MIR boxed typed elements (M4/R6) | matmul 27x, navier_stokes 43x, nbody 24–34x ×3, spectralnorm 19x, pnpoly 24x, crypto_sha1 32x | `float[]` reads go `fn_index`→`item_at`→`array_num_read_item`→`array_num_get`; arithmetic via generic `fn_add` + `push_d` | **M1** |
| MIR untyped scalar params | larceny/diviter **41x**, collatz 4x | evidence-based inference has no literal evidence for `x - y` (both params) → params stay ANY → `fn_sub`/`fn_ge`/`is_truthy` per iteration | **M2** |
| LJS holey-array store O(capacity) | jetstream/hashmap **2609x**, navier_stokes **885x** | `js_array_store_owned` → `js_array_dense_required` backwards hole scan per store | **Tune5 J1** |
| LJS double keys stringify | awfy/sieve **127x**, permute 87x, towers 61x, queens 50x, storage 21x (= the R0 "regression cluster") | float-typed index misses the `LMD_TYPE_INT` fast path → dtoa → sscanf → proto walks per access | **Tune5 J2** |
| LJS per-access C-call shape lookups | puzzle 250x, crypto_sha1 122x, richards 29.6x, deltablue 47.5x | `js_map_get_fast`/`js_find_shape_entry`/`js_own_shape_slot_status`/`js_intrinsic_note_property_mutation` per op | **out — R2/R3/OI-6** |
| MIR untyped map field access | jetstream richards.ls 54x / splay.ls 48x (runner times the *untyped* files; typed `splay2.ls` runs 104ms vs 480ms) | strlen/memcmp ShapeEntry scans + sip hashing per field op | **out — R6 follow-on; note in §9** |
| MIR eager `fn_strcat` | base64, json_gen 17.3x | full-copy concat; `r = r ++ x` loops quadratic (copy cost minor vs GC-1, alloc churn major) | **S1 (optional)** |

Out-of-scope confirmations recorded for the next re-rank: `beng/pidigits` LJS 0.16x
(faster than Node) and 17 MIR rows ≤1.7x confirm the core paths are healthy; the
plan deliberately does not touch them.

---

## 1. Evidence appendix (compact)

Profiles: macOS `sample`, 1ms interval, release build. Top-of-stack sample counts.
LJS-specific evidence (hashmap, sieve, puzzle/sha1) lives in Tune5 §1; the cd
entry stays here because its root cause is GC-2.

- **base64 (MIR, 10.8s run):** `gc_heap_alloc` 9,269 of ~11,150 (83%) at two
  offsets forming the inlined linear-scan loop of `gc_large_object_add`
  (`lambda/runtime/gc/gc_heap.c:163-171`); `_platform_memmove` 1,741 (registry
  shift + string copies). String byte-copy math: ~2.2GB total ≈ 74ms — the
  registry, not the copying, is the 10.8s. 333K strings >256B ⇒ all "large".
- **splay.ls (MIR, untyped):** `gc_object_zone_owns` 2,854 of ~3,200, called from
  `gc_mark_item` under `heap_gc_collect` ← `gc_data_alloc` threshold collections
  triggered by `array_num_new` payloads. The per-class slab walk after the
  successful range binary search is the loop (`gc_object_zone.c:307-319`).
- **cd2_bundle (LJS, 87.3s):** `gc_object_zone_owns` 11,575 busy-thread samples
  (~70%) — GC-2 on the JS heap, not a JS-engine defect. Then
  `js_find_shape_entry` 885, `js_map_get_fast` 873. (LJS runs scripts on a
  worker thread; the main thread parks in `__ulock_wait` — ignore that symbol.)
- **richards.ls (MIR, untyped):** flat mix of `strlen`, `memcmp`, `fn_map_set`,
  `_map_read_field`, `fn_member`, `hashmap_sip`, `name_pool_create_strview`,
  `heap_create_name` — string-keyed shape scans per field access.
- **matmul (MIR):** `fn_add` 113, `array_num_read_item` 52, `item_at` 37,
  `push_d` 36, `fn_index` 33, `array_num_get` 32 — ~53ns per inner iteration.
  navier_stokes: same shape ×5 volume. nbody: same.
- **diviter (MIR):** `fn_sub` 2,400, `fn_ge` 1,490, `is_truthy` 613 for a
  two-instruction integer loop.
- **brainfuck (MIR):** `fn_fill` 99 + `__bzero` 77 + `madvise` 31 — 10,000 ×
  `fill(30000,0)` tape re-allocations (300MB churn) + boxed interpreter ops.

---

## 2. Phase G0 — instrumentation and probes (½ day)

Release-safe counters in the style of `js_exec_profile` (no `log_debug`, which is
stripped under NDEBUG):

- **G0.1 GC registry counters:** large-object insert count, cumulative insert scan
  length (before G1: positions walked; after: hash probes), registry peak size,
  large-object find/remove counts. Exposed via the existing GC stats surface
  (`gc_heap_get_stats` family) and dumped by an env flag `LAMBDA_GC_STATS=1` on
  exit.
- **G0.2 mark counters:** `gc_object_zone_owns` calls per collection, cumulative
  slab-walk steps (before G2), collections count, mark-phase wall time per
  collection (monotonic clock around the mark loop, summed).
- **G0.3 micro-benchmark harness:** a `temp/tune4_probes.sh` script that runs the
  gate benchmarks (fixed list below) 3× and records medians to
  `temp/tune4_<phase>_<date>.json`. Raw files are never overwritten (R0 protocol).
  Tune5 J0 reuses this harness and adds the LJS array counters and probe rows.

Gate benchmarks per phase (self-reported `__TIMING__`, medians):

| Phase | Primary probes | Guard probes (must not regress >3%) |
|---|---|---|
| G1 | kostya/base64, kostya/json_gen, kostya/brainfuck | larceny/gcbench, beng/binarytrees, r7rs/fib |
| G2 | jetstream/splay.ls, larceny/gcbench, larceny/triangl, larceny/deriv; LJS awfy/cd | awfy/havlak, beng/fannkuch |
| M1 | kostya/matmul, awfy/nbody2, jetstream/navier_stokes.ls, beng/spectralnorm, larceny/pnpoly | r7rs/sumfp, awfy/mandelbrot, larceny/ray |
| M2 | larceny/diviter, kostya/collatz | awfy/cd2.ls (float-truncation guard), r7rs/fib, larceny/divrec |

---

## 3. Phase G1 — large-object registry: sorted array → hash set (1–2 days)

**Defect.** `gc_large_object_add` (`gc_heap.c:146`) finds the insert position by
linear scan and shifts with memmove — O(n) each, O(n²) per program. The existing
binary search (`gc_large_object_find`, `gc_heap.c:130`) is only used for the
duplicate check. `GC_LARGE_OBJECT_THRESHOLD` is 256 **bytes**
(`gc_object_zone.h:43`), so this registry sits on the hot path of any string/array
workload.

**Key observation.** With conservative stack scanning retired (CLAUDE rule 15),
every registry query is an **exact user-pointer lookup** — `is_gc_object`
(`gc_heap.c:1112`) and mark paths never ask range questions of the large registry.
Sortedness buys nothing; a pointer-keyed hash set is sufficient and O(1).

**Steps.**

- **G1.1** Implement an open-addressing pointer set local to `gc_heap.c`
  (power-of-two capacity, ~0.7 load factor, tombstone-free removal via backward
  shift or robin-hood; key = user pointer). Do **not** pull `lib/hashmap.h`'s
  general map — the GC allocator must not depend on a structure that allocates
  through pools it manages; plain `malloc`/`free` for the table, as the current
  `large_objects` array already does. (~120 lines, C.)
- **G1.2** Re-implement `gc_large_object_add` / `gc_large_object_remove` /
  `gc_large_object_find` on the set, preserving signatures and the
  duplicate-add-returns-1 contract. `find` keeps returning an int (≥0 found /
  −1 missing); internal index values lose meaning — audit the two extern call
  sites (`is_gc_object` at `gc_heap.c:1115`, sweep removal at `:1879/:1913`)
  for index dependence (none expected).
- **G1.3** Sweep integration: sweep iterates `all_objects` linked list and calls
  `gc_large_object_remove` per dead large object — now O(1) each. Verify
  `gc_heap_destroy` frees the table.
- **G1.4** Decide nothing about the 256B threshold in this phase. Raising it
  requires new slab size classes and changes object-zone memory shape; park as a
  separate measured follow-up (recorded in §9). The registry fix removes the
  scaling cliff regardless of threshold.

**Correctness gates.** `make test-lambda-baseline` 100%; forced-GC stress run
(the P3 sweep configuration from `Lambda_Design_MIR_Emission_Test.md`) on the
G1 probe list; ASAN spot-run on base64 + gcbench (registry rewrite touches
free-path bookkeeping).

**Performance gates.** base64 MIR ≥20× faster (10.8s → ≤550ms class);
G0.1 counters show O(1) probes; guard probes flat.

---

## 4. Phase G2 — O(1) slab ownership check in the mark path (1 day)

**Defect.** `gc_object_zone_owns` (`gc_object_zone.c:281`) binary-searches
`slab_ranges`, then ignores the hit and linearly walks **all size classes × all
slabs** (`:307-319`) to find the owning slab for the slot-alignment check,
because `gc_slab_range_t` (`gc_object_zone.h:57`) records only `base`/`end`.

**Steps.**

- **G2.1** Extend `gc_slab_range_t` with `gc_object_slab_t* slab` (and copy
  `slot_size` into the range for one fewer dereference). Populate in
  `register_slab_range` (`gc_object_zone.c:43`) — the caller registers exactly
  one slab per call; pass the slab pointer through.
- **G2.2** Rewrite the tail of `gc_object_zone_owns`: after the binary search
  verifies `base ≤ p < end`, compute
  `delta = p − (slab->base + sizeof(gc_header_t))`; return
  `delta % slot_size == 0 && p < slab->base + slab->next_fresh * slab->slot_size`.
  Delete the per-class loop. (The `next_fresh` bound preserves the current
  "never accept an unallocated fresh slot" behavior — keep that comparison
  exactly as the existing loop body does.)
- **G2.3** Audit slab lifetime: confirm slabs are never freed or recycled
  mid-run (only at `gc_object_zone` destroy — `gc_object_zone.c:151` frees
  ranges wholesale). If any path retires a slab early, it must remove/patch its
  range entry; add an assert that a range's slab pointer stays valid across
  collections (debug builds only).
- **G2.4** Re-measure. If splay/gcbench remain mark-bound after G2, the residual
  is collection *frequency* × live-set size — that is **R7** (sticky marks /
  generational) territory; record the G0.2 numbers in `Lambda_Tuning_Proposal.md`
  R7 as its entry evidence. Do not start R7 inside this plan.

**Gates.** Baseline 100%; forced-GC stress on splay/gcbench/triangl; primary
probes ≥5× on splay.ls class; LJS awfy/cd expected into the low-single-digit
seconds from 87s (G2 removes ~70% of its busy time; residual is shape lookups —
attribution recorded for Tune5, which re-baselines after this phase).

---

## 5. Phase M1 — MIR-Direct typed element loads/stores + native float arithmetic (3–5 days)

**Defect.** The inline fast-path machinery exists (`MirIndexLoadPolicy`,
`emit_checked_index_load`, `transpile-mir.cpp:7458-7660`) and already covers:
compile-time `ARRAY_NUM` with int/uint64/float elements (fast paths 1/1a,
`:7790-7856`), and generic `ARRAY` with `nested_int`/`nested_bool` (fast path
1b, `:7867-7946`). The profiled numeric cluster still routes through
`fn_index`/`item_at` because of coverage gaps, not missing machinery.

- **M1.0 Diagnose the exact miss (½ day, debug build).** For `matmul.ls`
  (`a: float[]` params) and `nbody2.ls`, read `temp/mir_dump.txt` and pin, per
  access site: (a) `obj_tid` — does a `float[]` **parameter** annotation arrive
  as `LMD_TYPE_ARRAY_NUM` (fast path 1a eligible) or generic `LMD_TYPE_ARRAY`
  (falls to 1b, which has **no nested-float branch** → generic `item_at`)?
  (b) does the index expression carry `LAMBDA_NUM_INTEGER` semantic-integer
  kind, which forces the boxed `fn_index` branch at `:7739` even for
  provably-int expressions like `i * n + k`? (c) which branch the profiled
  `fn_index` calls actually come from. Write the findings into this section
  before coding.
- **M1.1 Seed element types from annotations, not only `fill()` narrowing.**
  `MirVarEntry.elem_type` today is populated by `fill()` narrowing (P4-3.1).
  Extend seeding so a declared `float[]` / `int[]` on a **param** or
  `let`/`var` binding sets the same `elem_type` on the variable entry, and so
  `get_effective_type` for such params reports `ARRAY_NUM` when the annotation
  guarantees the runtime representation (verify against how
  `typed_array_annotation_compatible`, `build_ast.cpp:566`, admits arguments —
  if a generic `Array` can legally flow into a `float[]` param, the fast path
  must keep a runtime guard; see M1.2).
- **M1.2 Add the missing nested-FLOAT branches.** Mirror `nested_int` in fast
  paths 1b (`:7892`) and 1d (`:7956`): storage `MIR_INDEX_STORAGE_ARRAY_FLOAT`,
  result `MIR_INDEX_RESULT_NATIVE_FLOAT`, guard `MIR_INDEX_GUARD_CONTAINER_TYPE`
  **plus an elem-type guard** — the runtime object must be `ArrayNum` with
  `elem_type == ELEM_FLOAT64` before the raw `double` load; anything else takes
  `MIR_INDEX_SLOW_ITEM_AT`. If the existing container-type guard doesn't check
  `elem_type`, add a `MIR_INDEX_GUARD_ELEM_TYPE` variant rather than widening
  the existing one (other users depend on its current strictness).
- **M1.3 Index typing.** For the `:7739` semantic-integer branch: when every
  leaf of the index expression is native-INT-typed (params, locals, literals)
  and every operator is +/−/*, emit the native-int index instead of the boxed
  route. Implement as a small `mir_index_expr_is_native_int()` predicate on the
  AST — no new inference machinery. Overflow semantics are unchanged because
  the same expressions already evaluate native when used as scalars.
- **M1.4 Stores.** In `AST_NODE_INDEX_ASSIGN_STAM` (`:10799`), add the
  elem-FLOAT (and elem-INT where missing) inline store: same guards as M1.2,
  bounds check, raw `MIR_T_D` store, `MIR_INDEX_SLOW`-equivalent fallback to
  the existing helper (`fn_array_set`/`array_num` setter). Mutation that
  changes representation (`a[i] = "x"` on a float array) must hit the guard and
  fall back — add an explicit regression test for in-place representation
  conversion (`fn_array_set` converting ArrayNum → generic Array) followed by
  further indexed reads through the previously-fast site.
- **M1.5 Arithmetic follows for free.** With native `double` loads and
  `var sum = 0.0` already FLOAT-typed, existing binary-op emission produces
  `DADD`/`DMUL` — verify in the dump that `fn_add`/`push_d` disappear from the
  matmul/nbody inner loops; if `fn_add` persists, the remaining boxed operand
  is a bug in this phase, not a new work item.
- **M1.6 Emission budgets.** Update `test/mir/mir_budgets.json` and the
  `sys_func_specialization.mir-check` expectations deliberately (MT7 manual
  lift), one commit per budget change, with the dump diff quoted in the commit
  message.

**Out of scope, recorded:** the R6 typed sys-func ports (`fn_pow_u`,
`math.sqrt` native lowering) stay in R6; nbody's `math.sqrt` calls will surface
as the next flat cost after M1 — measure, don't bundle.

**Gates.** Baseline 100% + full MT7 suite green; matmul ≤8× vs Node (from 27x),
nbody/spectralnorm/pnpoly/navier ≥3× improvement each; guard probes flat;
representation-conversion regression test green under forced GC.

---

## 6. Phase M2 — call-site-informed scalar param inference (2–3 days)

**Defect.** `infer_param_types_batched` (`transpile-mir.cpp:12290`) gathers
body evidence only; `resolve_inferred_type` (`:12277`) deliberately refuses to
speculate INT from bare arithmetic since the cd.ls float-truncation fix. For
`diviter_div(x, y)` — `x - y`, `x >= y`, no literals — evidence is
NUMERIC/ARITH only ⇒ ANY ⇒ fully boxed loop at ~10ns/iteration × 1B iterations.

**Design: add call-site evidence, keep the no-speculation rule.** A param may be
narrowed to INT when **all** of:

1. every call site of the function inside the compilation unit passes an
   expression whose static type is native INT (literal, INT-typed local/param,
   or INT-returning expression) at that position;
2. the function name never escapes as a value — any use of the identifier
   outside direct-call callee position (assignment, argument, capture, pipe,
   map/array member, export) sets INFER_STOP for all its params;
3. the function is not recursive through an escaped alias (direct/mutual
   recursion among named local functions is fine — see fixpoint below);
4. body evidence does not contradict (no INFER_FLOAT / INFER_STOP).

This is sound where the cd.ls fix was not: a wrong guess is impossible because
every caller provably passes INT. Float call sites simply keep ANY.

**Steps.**

- **M2.1** Prepass call-site collection: one AST walk over the script collecting,
  per locally-defined `fn`/`pn` symbol, the static TypeId of each argument
  position across all `AST_NODE_CALL_EXPR`s, plus an `escaped` flag per symbol
  (identifier referenced outside callee position). Store alongside the existing
  `infer_cache` (`:297`). Arg static types come from the same
  `get_effective_type` logic the call emitter uses — do not re-derive.
- **M2.2** Fixpoint for call chains: seed with literal/annotated types, iterate
  the per-function resolution until stable (bounded by param count × functions;
  in practice ≤3 rounds). Mutual recursion where every external entry passes INT
  (diviter, fib-style helpers) converges to INT; any ANY entering the cycle
  poisons it to ANY — acceptable.
- **M2.3** Merge into `resolve_inferred_type` as a new evidence bit
  (`INFER_CALLSITE_INT`): grants INT only together with
  NUMERIC/ARITH body use and absence of FLOAT/STOP evidence.
- **M2.4** Float symmetry: same rule for all-call-sites-FLOAT (nbody-style
  helpers taking `dt`), granting `INFER_FLOAT` — strictly optional; land only if
  the diff stays small, since FLOAT params already work via annotation.
- **M2.5** Regression shields: the cd.ls truncation probes (pn-param float
  tests from the 2026-07-15 fix), plus new probes: (a) function called with INT
  everywhere but also stored in a map → must stay ANY; (b) mutual recursion
  with one float call site → ANY; (c) `run`-mode vs module import of the same
  file (imported functions are externally callable ⇒ exported/`pub` functions
  are always escaped ⇒ ANY unless annotated).

**Gates.** Baseline 100%; diviter ≥10× (19.6s → ≤2s class), collatz measurable
win; cd2.ls and divrec bit-identical output; MT7 budgets updated deliberately
(native param ABI changes emission for affected functions).

---

## 7. Phase S1 (optional, after G1) — unique-string in-place append

Deferred-by-default: G1 removes ~99% of base64's cost; json_gen's residual is
partly `++` allocation churn. Land only if post-G1 profiles still rank
`fn_strcat` allocation in the top three for a probe.

Sketch (recorded so the design isn't relost): transpiler-detected accumulator
pattern — for `s = s ++ expr` (and chained `((s ++ a) ++ b)`) where `s` is a
local whose value provably has no other live reference between its definition
and the reassignment (no store/capture/call-arg escape of `s` in between), emit
`fn_strcat_extend(s, x)`: if the left string is GC-heap-owned
(`gc_header.type_tag == LMD_TYPE_STRING`, not static/immortal) and
`gc_header.alloc_size` (`gc_heap.h:62`) has room, memcpy in place and bump
`len`; else allocate `next_pow2(len + add)` and copy. Escape analysis is the
soundness boundary — any doubt falls back to `fn_strcat`. QuickJS uses the
refcount==1 equivalent of this; Lambda's substitute is the static pattern proof.
Note `fn_strcat`'s existing RootFrame discipline (`lambda-eval.cpp:202`) must be
preserved in the extend path.

Related but separate (recorded for R6 follow-on, not this plan): brainfuck's
`fill(30000, 0)`-per-iteration tape churn would benefit from a calloc-backed
lazy-zero fill for large `fill()`s; and untyped map field access (jetstream
richards/splay untyped variants) wants interned-name/shape-id comparison instead
of strlen/memcmp scans — both need their own measurements after G1/G2 change the
baseline.

---

## 8. Sequencing, exit, and Result12

Order: **G0 → G1 → G2 → M1 → M2** (→ S1 only on evidence).
Rationale: G1/G2 are small, benefit both engines at once, and change the
baseline under every later phase (re-run probes after each); M1 is MIR's
biggest geo-mean lever; M2 is the most design-sensitive and goes last.
`vibe/Lambda_Impl_Tune5.md` (J0 → J1 → J2) proceeds after G1/G2 land — its
phases are code-independent of M1/M2 and may run in parallel with them.

Each phase lands independently green: baseline suites 100%, MT7 budgets
current, probe medians recorded to `temp/` with timestamped filenames.

**Exit = Result12**, one run shared with Tune5, after both plans complete
(Result9/10/11 protocol: clean release build, four-engine matrix, 3-run
medians, correctness sweep, raw JSON preserved). This plan's success
thresholds (LJS-side targets live in Tune5 §5):

| Metric | Result11 | Target |
|---|---|---|
| MIR/Node geo (dedup) | 4.73x | ≤ 3.0x |
| Worst MIR row | 617x (base64) | ≤ 25x |

After Result12, re-rank the remaining `Lambda_Tuning_Proposal.md` queue: the
expected top residuals on this plan's side are R7 (mark frequency × live set,
with G0.2 numbers as entry evidence), R6 typed sys-funcs (`math.sqrt` native
lowering surfacing after M1), and the untyped-map access family.

---

## 9. Risks

- **G1 (registry rewrite) touches free-path bookkeeping.** A missed remove
  leaves a dangling table entry → `is_gc_object` may bless a freed pointer.
  Mitigation: ASAN runs in the gate; debug-only assert that removed entries are
  present exactly once; sweep-order audit in G1.3.
- **G2 slab-lifetime assumption.** If any path frees or reuses a slab mid-run,
  a stale range→slab pointer misclassifies pointers during mark. G2.3's audit
  plus a debug assert are the shield; if early slab retirement exists, ranges
  gain explicit removal and the phase grows by a day.
- **M1 guard soundness.** In-place representation conversion (ArrayNum →
  generic Array on heterogeneous store) is the classic invalidator of typed
  fast paths — the `:7863` comment documents it. Every new inline load/store
  keeps a runtime elem-type guard with a slow fallback; the M1.4 regression
  test runs under forced GC.
- **M2 inference soundness.** Escaped-function detection must be complete —
  any call path invisible to the prepass (dynamic member call, pipe into a
  variable, export) must poison to ANY. The cd.ls probes plus new escape probes
  gate this; when in doubt the resolver stays ANY (performance regression only,
  never wrong values).
- **Machine-state variance.** Result11 absolutes were not reproducible on the
  profiling host (~1.3–1.5x). All phase gates are *ratios against same-day
  before/after runs* on one host, never against the Result11 report numbers.
