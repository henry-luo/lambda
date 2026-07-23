# Lambda Impl Plan: Tune 4 — Result11 Tail Elimination (GC cliffs + typed elements + LJS array pathologies)

**Status: PLANNED — 2026-07-24.**
**Successor of:** `vibe/Lambda_Impl_Tune_COW (done).md` (exit = Result11). This plan
consumes the Result11 diagnosis that `Lambda_Tuning_Proposal.md` R0 called for and
executes the highest-leverage subset of R6 plus four newly root-caused defects.
**Diagnosis provenance:** release-build `sample` profiles + disassembly + source
tracing, 2026-07-24, two commits past the Result11 commit (`52c0f3c02`, benchmark
commit `1704c2f43`). Absolute times ran ~1.3–1.5x above the Result11 report
(different host state); all conclusions rest on hot-frame proportions, not
absolutes. Evidence recorded in §1.
**Related plans:** `vibe/Lambda_Tuning_Proposal.md` (R2/R3/R7 stay owned there),
`vibe/Lambda_Design_COW.md` Stage 2 (ArrayNum COW/views — untouched here),
`vibe/Lambda_Design_MIR_Emission_Test.md` (MT7 budgets gate all emission changes).
**Convention:** `file:line` refs verified 2026-07-24; they drift — confirm against
symbol names before editing.

**Governing invariant.** Every phase is a pure performance change: no observable
semantic change to Lambda or JS programs. The only permitted observable deltas are
timing, memory footprint, and GC collection counts. Any phase that cannot hold
this line (e.g. J2's `-0`/NaN key edge cases) must prove equivalence with targeted
conformance tests before landing. `test-lambda-baseline` and `test-radiant-baseline`
stay 100% at every phase boundary; MIR emission budgets (`test/mir/mir_budgets.json`)
are updated only by deliberate manual lifts per MT7.

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
| LJS holey-array store O(capacity) | jetstream/hashmap **2609x**, navier_stokes **885x** | `js_array_store_owned` → `js_array_dense_required` backwards hole scan per store | **J1** |
| LJS double keys stringify | awfy/sieve **127x**, permute 87x, towers 61x, queens 50x, storage 21x (= the R0 "regression cluster") | float-typed index misses the `LMD_TYPE_INT` fast path → `js_to_property_key` → dtoa → sscanf → proto walks per access | **J2** |
| LJS per-access C-call shape lookups | puzzle 250x, crypto_sha1 122x, richards 29.6x, deltablue 47.5x | `js_map_get_fast`/`js_find_shape_entry`/`js_own_shape_slot_status`/`js_intrinsic_note_property_mutation` per op | **out — R2/R3/OI-6** |
| MIR untyped map field access | jetstream richards.ls 54x / splay.ls 48x (runner times the *untyped* files; typed `splay2.ls` runs 104ms vs 480ms) | strlen/memcmp ShapeEntry scans + sip hashing per field op | **out — R6 follow-on; note in M3** |
| MIR eager `fn_strcat` | base64, json_gen 17.3x | full-copy concat; `r = r ++ x` loops quadratic (copy cost minor vs GC-1, alloc churn major) | **S1 (optional)** |

Out-of-scope confirmations recorded for the next re-rank: `beng/pidigits` LJS 0.16x
(faster than Node) and 17 MIR rows ≤1.7x confirm the core paths are healthy; the
plan deliberately does not touch them.

---

## 1. Evidence appendix (compact)

Profiles: macOS `sample`, 1ms interval, release build. Top-of-stack sample counts.

- **base64 (MIR, 10.8s run):** `gc_heap_alloc` 9,269 of ~11,150 (83%) at two
  offsets forming the inlined linear-scan loop of `gc_large_object_add`
  (`lambda/runtime/gc/gc_heap.c:163-171`); `_platform_memmove` 1,741 (registry
  shift + string copies). String byte-copy math: ~2.2GB total ≈ 74ms — the
  registry, not the copying, is the 10.8s. 333K strings >256B ⇒ all "large".
- **splay.ls (MIR, untyped):** `gc_object_zone_owns` 2,854 of ~3,200, called from
  `gc_mark_item` under `heap_gc_collect` ← `gc_data_alloc` threshold collections
  triggered by `array_num_new` payloads. The per-class slab walk after the
  successful range binary search is the loop (`gc_object_zone.c:307-319`).
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
- **hashmap (LJS, 143.8s):** hot loop = backwards scan comparing against the hole
  sentinel `0x9e00dead00dead00`, tail-calling `array_set` — matches
  `js_array_dense_required` inlined into `js_array_store_owned`
  (`lambda/js/js_runtime.cpp:288-322`). ~5.04M map ops ⇒ ~28µs/op ≈ one
  131,072-slot scan per `put`. Secondary: GC mark cluster + `js_map_get_fast`.
- **cd2_bundle (LJS, 87.3s):** `gc_object_zone_owns` 11,575 busy-thread samples
  (~70%) — GC-2, not a JS-engine defect. Then `js_find_shape_entry` 885,
  `js_map_get_fast` 873.
- **sieve (LJS, looped ×60):** `js_map_get_fast` 963, `js_find_shape_entry` 725,
  `__svfscanf_l` 276, `__dtoa` 210, `__vfprintf` 168 under
  `js_property_set` → `js_to_property_key` → `js_to_string` — double-typed index
  round-trips through digit strings; sscanf is the canonical-index parse-back.
- **puzzle / crypto_sha1 (LJS):** `js_map_get_fast` 297/235,
  `js_find_shape_entry` 212/176, `js_own_shape_slot_status`,
  `js_intrinsic_note_property_mutation`, sha1 adds `js_string_method` 216.
- LJS runs scripts on a worker thread; the main thread parks in `__ulock_wait` —
  ignore that symbol in all LJS profiles.

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
- **G0.3 LJS array counters:** `js_array_dense_required` invocations + cumulative
  scanned slots; canonical-index fast-path hits/misses (added in J2);
  `js_property_set` branch counters already exist (`JS_PROPERTY_SET_BRANCH`) —
  extend with `top_array_float_index` once J2 lands.
- **G0.4 micro-benchmark harness:** a `temp/tune4_probes.sh` script that runs the
  gate benchmarks (fixed list below) 3× and records medians to
  `temp/tune4_<phase>_<date>.json`. Raw files are never overwritten (R0 protocol).

Gate benchmarks per phase (self-reported `__TIMING__`, medians):

| Phase | Primary probes | Guard probes (must not regress >3%) |
|---|---|---|
| G1 | kostya/base64, kostya/json_gen, kostya/brainfuck | larceny/gcbench, beng/binarytrees, r7rs/fib |
| G2 | jetstream/splay.ls, larceny/gcbench, larceny/triangl, larceny/deriv; LJS awfy/cd | awfy/havlak, beng/fannkuch |
| M1 | kostya/matmul, awfy/nbody2, jetstream/navier_stokes.ls, beng/spectralnorm, larceny/pnpoly | r7rs/sumfp, awfy/mandelbrot, larceny/ray |
| M2 | larceny/diviter, kostya/collatz | awfy/cd2.ls (float-truncation guard), r7rs/fib, larceny/divrec |
| J1 | LJS jetstream/hashmap, LJS jetstream/navier_stokes | LJS jetstream/splay (already fast: 64ms), LJS beng/fasta |
| J2 | LJS awfy/sieve, permute, towers, queens, storage; larceny/puzzle | LJS awfy/json, beng/revcomp, kostya/base64 (LJS) |

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
seconds from 87s (G2 removes ~70% of its busy time; residual is shape lookups).

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

## 7. Phase J1 — LJS holey-array stores: cached dense watermark (2–3 days)

**Defect.** `js_array_store_owned` (`js_runtime.cpp:301`) bypasses directly to
`array_set` only when `arr->extra == 0 && index < dense_capacity`; otherwise it
calls `js_array_dense_required` (`:288`) — a backwards scan over
`JS_DELETED_SENTINEL` holes across the whole dense capacity — **before even
deciding whether growth is needed**. JS arrays routinely carry `extra != 0`
(reserved tail: wide-scalar homes + props-companion slot), so `new Array(n)`
fill patterns pay O(capacity) per store: hashmap = 131,072-slot scan × ~720K
puts. The scan is genuinely load-bearing for growth sizing (it discovers
trailing holes so sparse-spec-length arrays don't balloon — the `:292` comment),
so it cannot simply be deleted; it must be cached.

**Design: monotone upper-bound watermark.** Maintain per-array
`dense_watermark` = an upper bound on (highest non-hole dense index + 1), with
`0 = unknown`:

- store at dense index `i`: `if (i + 1 > wm) wm = i + 1` — O(1), keeps bound;
- hole creation (delete) never lowers it — still a bound;
- consumers needing the exact value (growth sizing) use `min(wm, dense_capacity)`;
  over-approximation only costs at most the doubling the old code would reach a
  few stores later — never the sparse-balloon failure, because `wm ≤
  dense_capacity` by construction;
- `0`/unknown ⇒ one exact scan (`js_array_dense_required`) then cache.

**Storage decision.** Append `int64_t dense_watermark` to `struct List` after
`capacity` (C mirror `lambda.h:758-767` and the C++ definition). Verified
constraints: the ABI static asserts pin only the 8-byte `Container` header
(`lambda.h:721-743`); JIT-emitted member offsets (`items` +8, `length` +16,
`extra` +24, `capacity` +32) are unchanged by appending; allocation sites size
by `sizeof` so zero-init comes free from pool/calloc paths (0 = unknown is the
correct cold value). **Gate:** the MT7 emission diff for the whole Lambda+JS
fixture set must be empty for this step alone — proving no offset leaked.
Lambda-native code never reads the field; it is JS-semantics-only.

**Steps.**

- **J1.0** Counter first (G0.3): confirm on hashmap that scanned-slots ≈
  capacity × puts (validates the diagnosis in-tree before changing behavior).
- **J1.1** Add the field + `js_array_watermark_note_store(arr, i)` /
  `js_array_watermark_invalidate(arr)` inline helpers next to
  `js_array_dense_required`; re-implement `js_array_dense_required` as: cached
  value if `wm != 0`, else exact scan + cache (biased +1 so 0 stays "unknown").
- **J1.2** Invalidate-or-maintain audit — every writer of `items[]`/`length`
  reachable from JS arrays. Enumerated closure to audit (grep list, not
  exhaustive until done): `js_array_store_owned`, `js_array_sparse_delete`,
  `js_array_stamp_dense_tail_holes` (`:5670`), sparse→dense promotion, the
  `js_array_set_length_throw` shrink path, `push`/`pop`/`shift`/`unshift`/
  `splice`/`sort`/`reverse`/`fill`/`copyWithin` implementations, and the
  Lambda-side `expand_list`/`list_relocate_owned_tail` (relocation copies the
  dense prefix — watermark survives; shrink must invalidate). Rule: when exact
  maintenance is not obvious in one line, call
  `js_array_watermark_invalidate` — correctness over cleverness; the next scan
  re-caches.
- **J1.3** Fix the store fast path: in `js_array_store_owned`, allow the direct
  `array_set` bypass whenever `index < dense_capacity` **and** the write cannot
  consume tail capacity — concretely, when the value is not a wide scalar
  needing a new tail home, or when the watermark-based headroom check
  (`min(wm, dc) + extra + 2 ≤ capacity`) passes. Requires reading `array_set`'s
  tail-consumption contract first (the `:304` comment); if `array_set` can
  internally grow safely, prefer delegating and delete the pre-sizing entirely
  for the in-capacity case.
- **J1.4** Re-examine `js_array_fast_own_dense_set` (`:5965`): it funnels into
  `js_array_store_owned` — after J1.3 its hit path must be scan-free
  end-to-end.

**Gates.** LJS hashmap ≥100× (143.8s → ≤1.5s class — residual is shape lookups
+ GC, partly addressed by G2); navier_stokes LJS ≥20×; `make node-baseline`
non-regression (1492/3517 floor); test262 array chunks green; MT7 empty diff
for J1.1's struct step.

---

## 8. Phase J2 — LJS canonical numeric index fast path (1–2 days)

**Defect.** The array fast paths key on `get_type_id(key) == LMD_TYPE_INT`
(`js_runtime.cpp:6847`), but JS numbers are doubles: a float-typed index falls
through to `js_to_property_key` → `js_to_string` (dtoa) → sscanf canonical-index
parse-back → prototype-chain walk, **per element access** (sieve profile).

**Steps.**

- **J2.1** Add one helper, used by every entry point:
  `static inline bool js_key_as_index(Item key, int64_t* out)` — INT keys pass
  through; FLOAT keys qualify iff `(double)(int64_t)d == d && d >= 0 &&
  d <= 0xFFFFFFFE` (this accepts `-0.0` as index 0, which matches
  `ToString(-0) = "0"` = array index 0; rejects NaN/±inf/fractional/negative).
- **J2.2** Apply at: the `js_property_set` top fast path (`:6847`), the
  read-side equivalent in `js_property_access`/`js_array_fast_own_dense_get`
  (`:5947`), and `js_array_set_int`'s callers where the key is still boxed.
  The generic string path remains for everything the predicate rejects — no
  semantic surface moves.
- **J2.3** Conformance shields: property-key edge tests — `a[-0]`, `a[1.5]`
  (string key "1.5"), `a[NaN]`, `a[4294967294]` vs `a[4294967295]` (index
  ceiling), `a["01"]` (non-canonical string stays string), plus the relevant
  test262 chunks. `js_intrinsic_note_property_mutation` and IC invalidation
  behavior must be identical on both routes — verify by branch counters, not
  inspection.

**Gates.** sieve/permute/towers/queens/storage each ≥5× (targets: sieve 50ms →
≤10ms class); puzzle ≥3× (its residual is R2/R3 shape lookups); node-baseline
and test262 non-regression; dtoa/sscanf disappear from the sieve profile.

---

## 9. Phase S1 (optional, after G1) — unique-string in-place append

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

## 10. Sequencing, exit, and Result12

Order: **G0 → G1 → G2 → J1 → M1 → J2 → M2** (→ S1 only on evidence).
Rationale: G1/G2 are small, benefit both engines at once, and change the
baseline under every later phase (re-run probes after each); J1 kills the two
1000x+ rows; M1 is MIR's biggest geo-mean lever; J2 completes the R0 regression
diagnosis; M2 is the most design-sensitive and goes last.

Each phase lands independently green: baseline suites 100%, MT7 budgets
current, probe medians recorded to `temp/` with timestamped filenames.

**Exit = Result12** under the Result9/10/11 protocol (clean release build,
four-engine matrix, 3-run medians, correctness sweep, raw JSON preserved).
Success thresholds for the plan as a whole:

| Metric | Result11 | Target |
|---|---|---|
| MIR/Node geo (dedup) | 4.73x | ≤ 3.0x |
| LambdaJS/Node geo (dedup) | 19.3x | ≤ 10x |
| Worst MIR row | 617x (base64) | ≤ 25x |
| Worst LJS row | 2609x (hashmap) | ≤ 60x |
| LJS rows > 100x | 8 | 0 |

After Result12, re-rank the remaining `Lambda_Tuning_Proposal.md` queue: the
expected top residuals are R2/R3 (LJS shape-lookup C-calls — the ~20–50x OO
band), R7 (mark frequency × live set, with G0.2 numbers as entry evidence), R6
typed sys-funcs (`math.sqrt` native lowering surfacing after M1), and the
untyped-map access family.

---

## 11. Risks

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
- **J1 struct append.** Any hidden `sizeof(List)` coupling (serialized layouts,
  arena block math, C2MIR frozen path) would shift. The MT7 empty-diff gate,
  full baseline, and the frozen-C2MIR rule (untouched by design; it compiles
  against the same header — verify `LAMBDA_C2MIR_RUNTIME` mirrors the field)
  cover this. Watermark staleness is designed to be one-sided: invalidate-to-0
  is always available and always safe.
- **J2 spec surface.** Canonical-index handling is where JS engines
  historically leak semantics (`-0`, `"01"`, 2³²−1 ceiling, frozen arrays,
  accessor shadowing). The J2.3 shield list is the contract; any failure
  demotes the key back to the string path rather than special-casing further.
- **Machine-state variance.** Result11 absolutes were not reproducible on the
  profiling host (~1.3–1.5x). All phase gates are *ratios against same-day
  before/after runs* on one host, never against the Result11 report numbers.
